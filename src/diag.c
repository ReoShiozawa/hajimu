/**
 * 日本語プログラミング言語 - 診断メッセージユーティリティ実装
 */

#include "diag.h"
#include "lexer.h"  /* utf8_char_length */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>  /* isatty */

// =============================================================================
// ANSI カラー (TTY のみ有効)
// =============================================================================

static bool use_color(void) {
    static int cached = -1;
    if (cached < 0) cached = isatty(fileno(stderr));
    return cached;
}

#define C_RESET   (use_color() ? "\033[0m"  : "")
#define C_BOLD    (use_color() ? "\033[1m"  : "")
#define C_RED     (use_color() ? "\033[1;31m" : "")
#define C_YELLOW  (use_color() ? "\033[1;33m" : "")
#define C_CYAN    (use_color() ? "\033[1;36m" : "")
#define C_BLUE    (use_color() ? "\033[1;34m" : "")
#define C_GRAY    (use_color() ? "\033[0;90m" : "")

// =============================================================================
// エラー種別ラベル
// =============================================================================

const char *diag_kind_label(DiagKind kind) {
    switch (kind) {
        case DIAG_SYNTAX:    return "構文エラー";
        case DIAG_RUNTIME:   return "実行時エラー";
        case DIAG_NAME:      return "名前エラー";
        case DIAG_TYPE:      return "型エラー";
        case DIAG_VALUE:     return "値エラー";
        case DIAG_INDEX:     return "インデックスエラー";
        case DIAG_ZERO_DIV:  return "ゼロ除算エラー";
        case DIAG_OVERFLOW:  return "スタックオーバーフロー";
        case DIAG_ATTRIBUTE: return "属性エラー";
        case DIAG_USER:      return "例外";
        default:             return "エラー";
    }
}

// =============================================================================
// ソース行抽出
// =============================================================================

int diag_extract_line(const char *source, int line_num,
                      char *buf, int buf_size) {
    if (!source || line_num <= 0 || !buf || buf_size <= 0) return 0;

    int cur_line = 1;
    const char *p = source;

    /* 目的行の先頭まで進む */
    while (*p && cur_line < line_num) {
        if (*p == '\n') cur_line++;
        p++;
    }
    if (!*p && cur_line < line_num) return 0;  /* 行が存在しない */

    /* 行末 or バッファ末まで書き写す */
    int len = 0;
    while (*p && *p != '\n' && len < buf_size - 1) {
        buf[len++] = *p++;
    }
    buf[len] = '\0';
    return len;
}

// =============================================================================
// UTF-8 文字数カウント (バイト列の文字数 ≒ 表示列幅に近似)
// =============================================================================

int diag_utf8_strlen(const char *s, int byte_len) {
    int chars = 0;
    int i = 0;
    while (i < byte_len && s[i]) {
        int clen = utf8_char_length((unsigned char)s[i]);
        if (clen <= 0) clen = 1;
        i += clen;
        chars++;
    }
    return chars;
}

// =============================================================================
// 診断出力
// =============================================================================

void diag_report(
    DiagKind  kind,
    const char *filename,
    const char *source,
    int  line,
    int  col,
    int  col_end,
    const char *message
) {
    const char *label = diag_kind_label(kind);
    const char *color = (kind == DIAG_SYNTAX) ? C_RED : C_RED;
    (void)color;

    /* ── 見出し行 ──────────────────────────────────────────── */
    /*  例: [構文エラー] --> test.jp:15:8                        */
    fprintf(stderr, "%s%s%s", C_RED, C_BOLD, label);
    fprintf(stderr, "%s", C_RESET);
    if (filename) {
        fprintf(stderr, " %s-->%s %s%s:%d:%d%s\n",
                C_BLUE, C_RESET,
                C_BOLD, filename, line, col, C_RESET);
    } else {
        fprintf(stderr, "\n");
    }

    /* ── ソース行がない場合 ───────────────────────────────── */
    if (!source || line <= 0) {
        fprintf(stderr, "   %s=%s %s\n", C_GRAY, C_RESET, message);
        return;
    }

    /* ── ソース行を抽出 ───────────────────────────────────── */
    char line_buf[1024];
    int  line_len = diag_extract_line(source, line, line_buf, sizeof(line_buf));
    if (line_len <= 0) {
        fprintf(stderr, "   %s=%s %s\n", C_GRAY, C_RESET, message);
        return;
    }

    /* 行番号の表示幅を決める (最大5桁) */
    char line_num_str[16];
    snprintf(line_num_str, sizeof(line_num_str), "%d", line);
    int num_width = (int)strlen(line_num_str);
    if (num_width < 2) num_width = 2;

    /* ── 区切り行 ─────────────────────────────────────────── */
    /*    |                                                       */
    fprintf(stderr, "%s%*s |%s\n", C_BLUE, num_width, "", C_RESET);

    /* ── ソース行 ─────────────────────────────────────────── */
    /*  15 |     変数 x = もし 条件 なら                         */
    fprintf(stderr, "%s%*d |%s %s\n",
            C_BLUE, num_width, line, C_RESET, line_buf);

    /* ── キャレット行 ─────────────────────────────────────── */
    /*     |          ^^^^^^^^^^^                                 */
    fprintf(stderr, "%s%*s |%s ", C_BLUE, num_width, "", C_RESET);

    /* col 列目まで空白を出力 (バイト→文字変換を考慮) */
    /* col_end が col より小さければ 1 文字分のみハイライト */
    if (col_end < col) col_end = col;

    /* col 列 (1-based) の前を空白で埋める */
    /* ソース行を先頭から col-1 文字分スキャンしてバイト数を計算 */
    int skip_bytes = 0;
    {
        const char *p = line_buf;
        int char_count = 0;
        while (*p && char_count < col - 1) {
            int clen = utf8_char_length((unsigned char)*p);
            if (clen <= 0) clen = 1;
            /* 日本語全角は表示幅 2 として空白 2 個 */
            int w = (clen > 1) ? 2 : 1;
            for (int i = 0; i < w; i++) fputc(' ', stderr);
            skip_bytes += clen;
            p += clen;
            char_count++;
        }
    }

    /* ハイライト部分の文字数を計算 */
    {
        const char *p = line_buf + skip_bytes;
        int char_count = 0;
        int target     = col_end - col + 1;  /* ハイライト文字数 */
        if (target < 1) target = 1;

        fprintf(stderr, "%s", C_YELLOW);
        while (*p && char_count < target) {
            int clen = utf8_char_length((unsigned char)*p);
            if (clen <= 0) clen = 1;
            int w = (clen > 1) ? 2 : 1;
            for (int i = 0; i < w; i++) fputc('^', stderr);
            p += clen;
            char_count++;
        }
        if (char_count == 0) fprintf(stderr, "^"); /* 最低1文字 */
        fprintf(stderr, "%s", C_RESET);
    }
    fputc('\n', stderr);

    /* ── メッセージ行 ─────────────────────────────────────── */
    /*     = メッセージ本文                                      */
    fprintf(stderr, "%s%*s |%s\n", C_BLUE, num_width, "", C_RESET);
    fprintf(stderr, "   %s=%s %s\n\n", C_GRAY, C_RESET, message);
}
