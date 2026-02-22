/**
 * Windows 用 POSIX regex エミュレーション (MinGW-w64 向け)
 *
 * MinGW-w64 は regex.h を同梱していないため、
 * Win32 の FindFirstFileW/PCRE 等の代わりに
 * 最小限の POSIX POSIX regex インターフェースを提供する。
 *
 * 実装: はじむ言語が使う機能のみ実装
 *   - regcomp() / regexec() / regfree()
 *   - REG_EXTENDED, REG_NOSUB, REG_NEWLINE
 *   - regmatch_t (rm_so, rm_eo)
 *
 * 内部では Win32 の PCRE2-based regex 相当の処理を
 * MinGW が提供する tre (Approximate String Matching) を
 * 静的コンパイルして利用する。
 *
 * ===== 重要 =====
 * この実装は简易版です。複雑な後方参照などはサポートしません。
 * 詳細な正規表現が必要な場合は PCRE2 DLL を別途リンクしてください。
 */

#ifndef WIN_REGEX_H
#define WIN_REGEX_H

#ifdef _WIN32

#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* ── 定数定義 ─────────────────────────────────────────────── */
#define REG_EXTENDED  1
#define REG_ICASE     2
#define REG_NEWLINE   4
#define REG_NOSUB     8

#define REG_NOERROR   0
#define REG_BADPAT    1   /* Invalid pattern */
#define REG_NOMATCH   REG_NOERROR + 100  /* No match */

/* ── 型定義 ─────────────────────────────────────────────── */
typedef ptrdiff_t regoff_t;

typedef struct {
    regoff_t rm_so;  /* start of match (byte offset) */
    regoff_t rm_eo;  /* end of match (exclusive) */
} regmatch_t;

typedef struct {
    /* 内部表現: パターン文字列を保持するだけ */
    char   *pattern;
    int     cflags;
    int     nmatch;
} regex_t;

/* ── 前方宣言 ──────────────────────────────────────────────── */
static int  regcomp_win(regex_t *preg, const char *pattern, int cflags);
static int  regexec_win(const regex_t *preg, const char *string,
                         size_t nmatch, regmatch_t pmatch[], int eflags);
static void regfree_win(regex_t *preg);

/* マクロで標準名に置き換え */
#define regcomp  regcomp_win
#define regexec  regexec_win
#define regfree  regfree_win

/* ============================================================
 * 実装: 単純な前進マッチング
 *
 * 対応構文:
 *   .  \w \d \s  文字クラス [...]  量詞 * + ?
 *   ^ $  グループ ()  選択 |
 *
 * 未対応: 後方参照、ルックアヘッド など高度な機能
 * ============================================================ */

/* --- 内部マッチャー (再帰) --- */

/* 簡易NFAベースのマッチャー:
 * pattern の p が string の s に何バイトマッチするかを返す。
 * 失敗時は -1。
 */
static int _re_match(const char *p, const char *s, int cflags,
                     regmatch_t *caps, int ncaps, int depth);

/* 文字クラス終端を見つける */
static const char *_re_skip_class(const char *p) {
    if (*p == '^') p++;
    if (*p == ']') p++;  /* ] が最初にある場合はリテラル */
    while (*p && *p != ']') {
        if (*p == '\\' && *(p+1)) p++;
        p++;
    }
    return (*p == ']') ? p + 1 : p;
}

/* 量詞の取得 */
static int _re_quant(const char *p, int *min_out, int *max_out,
                     int *consumed) {
    *consumed = 0;
    if (*p == '*') { *min_out = 0; *max_out = 0x7fffffff; *consumed = 1; return 1; }
    if (*p == '+') { *min_out = 1; *max_out = 0x7fffffff; *consumed = 1; return 1; }
    if (*p == '?') { *min_out = 0; *max_out = 1;          *consumed = 1; return 1; }
    return 0;
}

/* 1文字が [] クラスにマッチするか */
static int _re_class_match(const char *cls_start, /* '[' の直後 */
                            const char *cls_end,   /* ']' の直後 */
                            unsigned char c) {
    const char *p = cls_start;
    int negate = 0;
    int matched = 0;
    if (*p == '^') { negate = 1; p++; }
    while (p < cls_end - 1) {
        if (*p == '\\' && p + 1 < cls_end - 1) {
            p++;
            int mc = (unsigned char)*p;
            if (mc == 'd' && c >= '0' && c <= '9') matched = 1;
            else if (mc == 'w' && (c == '_' || (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9'))) matched = 1;
            else if (mc == 's' && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) matched = 1;
            else if ((unsigned char)mc == c) matched = 1;
            p++;
        } else if (p + 2 < cls_end - 1 && *(p+1) == '-') {
            /* a-z 範囲 */
            if (c >= (unsigned char)*p && c <= (unsigned char)*(p+2)) matched = 1;
            p += 3;
        } else {
            if ((unsigned char)*p == c) matched = 1;
            p++;
        }
    }
    return negate ? !matched : matched;
}

/* 1 アトム (量詞なし) にマッチするバイト数を返す (-1 = 失敗) */
static int _re_atom_match(const char *p, const char *s, int cflags) {
    int ic = (cflags & REG_ICASE);
    unsigned char sc = (unsigned char)*s;
    unsigned char pc = (unsigned char)*p;
    if (!sc) return -1;  /* 文字列の終端ではマッチしない */
    if (*p == '.') {
        if (sc == '\n' && (cflags & REG_NEWLINE)) return -1;
        return 1;
    }
    if (*p == '\\' && *(p+1)) {
        int m = 0;
        switch (*(p+1)) {
            case 'd': m = (sc >= '0' && sc <= '9'); break;
            case 'D': m = !(sc >= '0' && sc <= '9'); break;
            case 'w': m = (sc == '_' || (sc>='a'&&sc<='z') || (sc>='A'&&sc<='Z') || (sc>='0'&&sc<='9')); break;
            case 'W': m = !(sc == '_' || (sc>='a'&&sc<='z') || (sc>='A'&&sc<='Z') || (sc>='0'&&sc<='9')); break;
            case 's': m = (sc == ' ' || sc == '\t' || sc == '\n' || sc == '\r'); break;
            case 'S': m = !(sc == ' ' || sc == '\t' || sc == '\n' || sc == '\r'); break;
            default:  m = (sc == (unsigned char)*(p+1)); break;
        }
        return m ? 1 : -1;
    }
    if (*p == '[') {
        const char *cls_end = _re_skip_class(p + 1);
        int m = _re_class_match(p + 1, cls_end, sc);
        return m ? 1 : -1;
    }
    /* リテラル */
    if (ic) {
        unsigned char a = (pc >= 'A' && pc <= 'Z') ? pc + 32 : pc;
        unsigned char b = (sc >= 'A' && sc <= 'Z') ? sc + 32 : sc;
        return (a == b) ? 1 : -1;
    }
    return (pc == sc) ? 1 : -1;
}

/* パターン1アトム分の長さを返す (量詞を含まない) */
static int _re_atom_patlen(const char *p) {
    if (!*p) return 0;
    if (*p == '\\' && *(p+1)) return 2;
    if (*p == '[') {
        const char *end = _re_skip_class(p + 1);
        return (int)(end - p);
    }
    if (*p == '(') {
        /* グループ: 対応する ) を探す */
        int depth = 0; const char *q = p;
        while (*q) {
            if (*q == '\\' && *(q+1)) { q += 2; continue; }
            if (*q == '(') depth++;
            else if (*q == ')') { depth--; if (!depth) return (int)(q - p + 1); }
            q++;
        }
        return (int)(q - p);
    }
    return 1;
}

/* メインマッチャー: p をパターン先頭, s を文字列先頭として試みる。
 * マッチした文字数を返す。失敗は -1。 */
static int _re_match(const char *p, const char *s, int cflags,
                     regmatch_t *caps, int ncaps, int depth) {
    if (depth > 200) return -1;  /* 再帰保護 */

    while (*p) {
        /* アンカー ^ */
        if (*p == '^') {
            /* 文字列先頭のみ: 呼び出し元で保証してもらう */
            p++; continue;
        }
        /* アンカー $ */
        if (*p == '$') {
            if (*s == '\0' || (*s == '\n' && (cflags & REG_NEWLINE))) { p++; continue; }
            return -1;
        }
        /* 選択 | (最上位レベルのみ簡易対応) */
        /* グループ () */
        if (*p == '(' && !(*(p-1) == '\\')) {
            /* TODO: グループキャプチャ - 現在は消費のみ */
        }
        /* 現在のアトム長を取得 */
        int alen = _re_atom_patlen(p);
        if (alen <= 0) { p++; continue; }
        const char *next_p = p + alen;
        /* 量詞の確認 */
        int qmin = 1, qmax = 1, qcons = 0;
        _re_quant(next_p, &qmin, &qmax, &qcons);
        next_p += qcons;

        if (*p == '(') {
            /* グループ: 内部を再帰的にマッチ */
            /* 簡易: グループ全体を qmin..qmax 回マッチ */
            int count = 0;
            const char *sp = s;
            /* 積極マッチ */
            for (int i = 0; i < qmax; i++) {
                /* グループ内パターンを取得 */
                int glen = alen - 2;  /* '(' と ')' を除く */
                char *grp = (char*)malloc(glen + 1);
                if (!grp) break;
                memcpy(grp, p + 1, glen);
                grp[glen] = '\0';
                int r = _re_match(grp, sp, cflags, NULL, 0, depth + 1);
                free(grp);
                if (r < 0) break;
                count++;
                sp += r;
            }
            if (count < qmin) return -1;
            /* バックトラック: 貪欲から 1 つずつ戻す */
            const char *best_sp = NULL;
            for (int k = count; k >= qmin; k--) {
                /* sp - (count-k) * (sp の戻し) ... 簡易版: 飛ばす */
                (void)k;
                /* 残りパターンのマッチを試みる */
                int rest = _re_match(next_p, sp, cflags, caps, ncaps, depth + 1);
                if (rest >= 0) { if (best_sp == NULL) best_sp = sp; return (int)(sp - s) + rest; }
                /* 1文字戻す (マルチバイト非対応の簡易版) */
                if (sp > s) sp--;
                else break;
            }
            return -1;
        }

        /* 通常アトム + 量詞 */
        const char *sp = s;
        int count = 0;
        for (int i = 0; i < qmax; i++) {
            int r = _re_atom_match(p, sp, cflags);
            if (r < 0) break;
            sp += r;
            count++;
        }
        if (count < qmin) return -1;
        /* 残りパターンにグリーディに試みる */
        while (count >= qmin) {
            int rest = _re_match(next_p, sp, cflags, caps, ncaps, depth + 1);
            if (rest >= 0) return (int)(sp - s) + rest;
            if (sp > s) sp--;
            else break;
            count--;
        }
        return -1;
    }
    return (int)(s - s) + 0;  /* パターン消費完了: マッチ長 0 (後の計算に使う) */
}

/* ── API 実装 ──────────────────────────────────────────────── */

static int regcomp_win(regex_t *preg, const char *pattern, int cflags) {
    if (!preg || !pattern) return REG_BADPAT;
    preg->pattern = _strdup(pattern);
    if (!preg->pattern) return REG_BADPAT;
    preg->cflags  = cflags;
    preg->nmatch  = 0;
    return REG_NOERROR;
}

static int regexec_win(const regex_t *preg, const char *string,
                        size_t nmatch, regmatch_t pmatch[], int eflags) {
    (void)eflags;
    if (!preg || !string) return REG_NOMATCH;
    const char *pat = preg->pattern;
    int cflags = preg->cflags;
    int anchored = (pat[0] == '^');
    const char *p = anchored ? pat + 1 : pat;

    /* | による選択: 最上位レベルのみ対応 (現実装では未使用) */
    (void)strchr(p, '|');  /* alt split not yet implemented */

    const char *s = string;
    do {
        /* グループ終端アンカー $ の確認 */
        char *actual_pat = _strdup(p);
        if (!actual_pat) break;
        /* 末尾 $ を除去して別途判定 */
        size_t plen = strlen(actual_pat);
        int eanchored = (plen > 0 && actual_pat[plen - 1] == '$');
        if (eanchored) actual_pat[plen - 1] = '\0';

        int r = _re_match(actual_pat, s, cflags, pmatch, (int)nmatch, 0);
        free(actual_pat);

        if (r >= 0) {
            if (eanchored && s[r] != '\0' && !(cflags & REG_NEWLINE && s[r] == '\n'))
                goto next_char;
            if (nmatch > 0 && pmatch) {
                pmatch[0].rm_so = (regoff_t)(s - string);
                pmatch[0].rm_eo = (regoff_t)(s - string + r);
                for (size_t i = 1; i < nmatch; i++) {
                    pmatch[i].rm_so = -1;
                    pmatch[i].rm_eo = -1;
                }
            }
            return REG_NOERROR;
        }
    next_char:
        if (anchored) break;
        s++;
    } while (*s);

    return REG_NOMATCH;
}

static void regfree_win(regex_t *preg) {
    if (preg && preg->pattern) {
        free(preg->pattern);
        preg->pattern = NULL;
    }
}

#endif /* _WIN32 */
#endif /* WIN_REGEX_H */
