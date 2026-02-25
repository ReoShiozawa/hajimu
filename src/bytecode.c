/**
 * はじむ - バイトコード (.hjp) エンコード/デコード実装
 *
 * HJPB フォーマット仕様は bytecode.h 参照。
 */

#include "bytecode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// 内部ユーティリティ
// =============================================================================

/** uint32_t をリトルエンディアンで 4 バイトに書き出す */
static void write_u32le(FILE *f, uint32_t v) {
    uint8_t buf[4];
    buf[0] = (uint8_t)(v & 0xFF);
    buf[1] = (uint8_t)((v >> 8) & 0xFF);
    buf[2] = (uint8_t)((v >> 16) & 0xFF);
    buf[3] = (uint8_t)((v >> 24) & 0xFF);
    fwrite(buf, 1, 4, f);
}

/** バッファから uint32_t をリトルエンディアンで読み込む */
static uint32_t read_u32le(const uint8_t *buf) {
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
}

/**
 * JSON 文字列値として安全にエスケープして書き出す。
 * 制御文字・ダブルクォート・バックスラッシュをエスケープする。
 */
static void write_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"')       { fputs("\\\"", f); }
        else if (c == '\\') { fputs("\\\\", f); }
        else if (c == '\n') { fputs("\\n",  f); }
        else if (c == '\r') { fputs("\\r",  f); }
        else if (c == '\t') { fputs("\\t",  f); }
        else if (c < 0x20)  { fprintf(f, "\\u%04x", c); }
        else                { fputc(c, f); }
    }
    fputc('"', f);
}

/**
 * 最小限の JSON パーサ: キー "key" の文字列値を out_buf に書き込む。
 * JSON は {"k":"v",...} 形式のみサポート（ネストなし）。
 * 見つかった場合 true を返す。
 */
static bool json_extract_string(const char *json, const char *key, char *out_buf, size_t out_size) {
    if (!json || !key || !out_buf || out_size == 0) return false;

    /* "key": を探す */
    size_t keylen = strlen(key);
    const char *p = json;
    while (*p) {
        /* ダブルクォートを探す */
        if (*p == '"') {
            p++;
            /* キーと一致するか */
            if (strncmp(p, key, keylen) == 0 && p[keylen] == '"') {
                p += keylen + 1;
                /* ':' をスキップ (空白含む) */
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (*p != ':') { continue; }
                p++;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (*p != '"') { continue; }
                p++; /* 開きクォートをスキップ */
                /* 値を読む */
                size_t idx = 0;
                while (*p && *p != '"' && idx + 1 < out_size) {
                    if (*p == '\\' && *(p + 1)) {
                        p++;
                        switch (*p) {
                            case '"':  out_buf[idx++] = '"';  break;
                            case '\\': out_buf[idx++] = '\\'; break;
                            case 'n':  out_buf[idx++] = '\n'; break;
                            case 'r':  out_buf[idx++] = '\r'; break;
                            case 't':  out_buf[idx++] = '\t'; break;
                            default:   out_buf[idx++] = *p;   break;
                        }
                    } else {
                        out_buf[idx++] = *p;
                    }
                    p++;
                }
                out_buf[idx] = '\0';
                return true;
            }
            /* このキーは違う: 閉じクォートを探して進む */
            while (*p && *p != '"') {
                if (*p == '\\' && *(p + 1)) p++;
                p++;
            }
            if (*p) p++; /* closing " */
        } else {
            p++;
        }
    }
    return false;
}

// =============================================================================
// ファイル判別
// =============================================================================

bool hjpb_is_bytecode_buf(const uint8_t *buf, size_t size) {
    if (buf == NULL || size < HJPB_MAGIC_LEN) return false;
    return memcmp(buf, HJPB_MAGIC, HJPB_MAGIC_LEN) == 0;
}

bool hjpb_is_bytecode_file(const char *path) {
    if (path == NULL) return false;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    uint8_t magic[HJPB_MAGIC_LEN];
    size_t n = fread(magic, 1, HJPB_MAGIC_LEN, f);
    fclose(f);
    return hjpb_is_bytecode_buf(magic, n);
}

// =============================================================================
// エンコード (.jp → .hjp)
// =============================================================================

bool hjpb_encode(const char *out_path, const HjpbMeta *meta,
                 const char *source, size_t source_len) {
    if (out_path == NULL || source == NULL) return false;
    if (source_len == 0) source_len = strlen(source);

    /* メタデータ JSON を組み立てる */
    HjpbMeta safe_meta = {0};
    if (meta) safe_meta = *meta;

    /* name が空なら out_path のベース名から推定 */
    if (safe_meta.name[0] == '\0') {
        const char *base = strrchr(out_path, '/');
#ifdef _WIN32
        const char *base2 = strrchr(out_path, '\\');
        if (base2 > base) base = base2;
#endif
        base = base ? base + 1 : out_path;
        snprintf(safe_meta.name, sizeof(safe_meta.name), "%s", base);
        /* .hjp 拡張子を取り除く */
        char *dot = strrchr(safe_meta.name, '.');
        if (dot && strcmp(dot, ".hjp") == 0) *dot = '\0';
    }
    if (safe_meta.version[0] == '\0') {
        snprintf(safe_meta.version, sizeof(safe_meta.version), "0.0.0");
    }

    /* JSON を一時バッファに書く */
    char json_buf[2048];
    {
        /* スタックバッファに snprintf で組み立てると日本語・特殊文字のエスケープが
         * 複雑になるため、メモリ上の FILE* を使って write_json_string を流用する。 */
        FILE *mf = NULL;
        char *mbuf = NULL;
        size_t msz = 0;
#ifdef _WIN32
        /* Windows: open_memstream 非対応のため tmpfile を使う */
        mf = tmpfile();
#else
        mf = open_memstream(&mbuf, &msz);
#endif
        if (mf == NULL) {
            /* フォールバック: 簡易 snprintf（エスケープ省略） */
            snprintf(json_buf, sizeof(json_buf),
                     "{\"name\":\"%s\",\"version\":\"%s\","
                     "\"author\":\"%s\",\"description\":\"%s\"}",
                     safe_meta.name, safe_meta.version,
                     safe_meta.author, safe_meta.description);
        } else {
            fputs("{\"name\":", mf);
            write_json_string(mf, safe_meta.name);
            fputs(",\"version\":", mf);
            write_json_string(mf, safe_meta.version);
            fputs(",\"author\":", mf);
            write_json_string(mf, safe_meta.author);
            fputs(",\"description\":", mf);
            write_json_string(mf, safe_meta.description);
            fputs("}", mf);

#ifdef _WIN32
            /* Windows tmpfile: 内容を json_buf に読み戻す */
            long pos = ftell(mf);
            rewind(mf);
            size_t rd = (size_t)(pos < (long)(sizeof(json_buf) - 1) ? pos : (long)(sizeof(json_buf) - 1));
            rd = fread(json_buf, 1, rd, mf);
            json_buf[rd] = '\0';
            fclose(mf);
#else
            fclose(mf);
            if (mbuf && msz < sizeof(json_buf)) {
                memcpy(json_buf, mbuf, msz);
                json_buf[msz] = '\0';
            } else if (mbuf) {
                /* 長すぎる場合は切り詰め */
                memcpy(json_buf, mbuf, sizeof(json_buf) - 1);
                json_buf[sizeof(json_buf) - 1] = '\0';
            }
            free(mbuf);
#endif
        }
    }

    uint32_t meta_len = (uint32_t)strlen(json_buf);
    uint32_t src_len  = (uint32_t)source_len;

    /* .hjp ファイルを書き出す */
    FILE *f = fopen(out_path, "wb");
    if (f == NULL) {
        fprintf(stderr, "エラー: ファイルを開けません: %s\n", out_path);
        return false;
    }

    /* マジック */
    fwrite(HJPB_MAGIC, 1, HJPB_MAGIC_LEN, f);
    /* バージョン */
    fputc(HJPB_VERSION_MAJOR, f);
    fputc(HJPB_VERSION_MINOR, f);
    /* フラグ (4 バイト, 予約) */
    write_u32le(f, 0);
    /* メタデータ長 + データ */
    write_u32le(f, meta_len);
    fwrite(json_buf, 1, meta_len, f);
    /* ソース長 + データ */
    write_u32le(f, src_len);
    fwrite(source, 1, src_len, f);

    fclose(f);
    return true;
}

// =============================================================================
// デコード (.hjp → ソース取り出し)
// =============================================================================

bool hjpb_decode(const char *path, HjpbMeta *meta_out,
                 char **source_out, size_t *source_len_out) {
    if (path == NULL) return false;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "エラー: ファイルを開けません: %s\n", path);
        return false;
    }

    /* ファイルサイズを取得 */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size < (long)HJPB_HEADER_MIN) {
        fprintf(stderr, "エラー: HJPB ファイルが小さすぎます: %s\n", path);
        fclose(f);
        return false;
    }

    /* ヘッダーを読む */
    uint8_t hdr[HJPB_HEADER_MIN];
    if (fread(hdr, 1, HJPB_HEADER_MIN, f) != HJPB_HEADER_MIN) {
        fprintf(stderr, "エラー: ヘッダーを読み込めません: %s\n", path);
        fclose(f);
        return false;
    }

    /* マジック確認 */
    if (!hjpb_is_bytecode_buf(hdr, HJPB_HEADER_MIN)) {
        fprintf(stderr, "エラー: HJPB マジックが見つかりません: %s\n", path);
        fclose(f);
        return false;
    }

    /* バージョン確認 */
    uint8_t ver_major = hdr[4];
    uint8_t ver_minor = hdr[5];
    if (ver_major != HJPB_VERSION_MAJOR) {
        fprintf(stderr, "エラー: サポートされていない HJPB バージョン %d.%d: %s\n",
                ver_major, ver_minor, path);
        fclose(f);
        return false;
    }
    (void)ver_minor; /* 現在 minor は無視 */

    /* フラグ (予約, 現在は無視) */
    /* uint32_t flags = read_u32le(hdr + 6); // 将来的拡張用 */

    /* メタデータ長 */
    uint32_t meta_len = read_u32le(hdr + 10);
    if (meta_len > 1024 * 1024) { /* 1 MB 超は異常 */
        fprintf(stderr, "エラー: メタデータが大きすぎます: %s\n", path);
        fclose(f);
        return false;
    }

    /* メタデータ JSON を読む */
    char *json_buf = NULL;
    if (meta_len > 0) {
        json_buf = malloc(meta_len + 1);
        if (json_buf == NULL) { fclose(f); return false; }
        if (fread(json_buf, 1, meta_len, f) != meta_len) {
            fprintf(stderr, "エラー: メタデータを読み込めません: %s\n", path);
            free(json_buf);
            fclose(f);
            return false;
        }
        json_buf[meta_len] = '\0';
    }

    /* メタデータをパース */
    if (meta_out && json_buf) {
        memset(meta_out, 0, sizeof(*meta_out));
        json_extract_string(json_buf, "name",        meta_out->name,        sizeof(meta_out->name));
        json_extract_string(json_buf, "version",     meta_out->version,     sizeof(meta_out->version));
        json_extract_string(json_buf, "author",      meta_out->author,      sizeof(meta_out->author));
        json_extract_string(json_buf, "description", meta_out->description, sizeof(meta_out->description));
    }
    free(json_buf);

    /* ソースコード長 */
    uint8_t src_len_buf[4];
    if (fread(src_len_buf, 1, 4, f) != 4) {
        fprintf(stderr, "エラー: ソース長を読み込めません: %s\n", path);
        fclose(f);
        return false;
    }
    uint32_t src_len = read_u32le(src_len_buf);

    if (src_len > 64 * 1024 * 1024) { /* 64 MB 超は異常 */
        fprintf(stderr, "エラー: ソースコードが大きすぎます: %s\n", path);
        fclose(f);
        return false;
    }

    /* ソースコードを読む */
    char *src_buf = malloc(src_len + 1);
    if (src_buf == NULL) { fclose(f); return false; }

    if (src_len > 0 && fread(src_buf, 1, src_len, f) != src_len) {
        fprintf(stderr, "エラー: ソースコードを読み込めません: %s\n", path);
        free(src_buf);
        fclose(f);
        return false;
    }
    src_buf[src_len] = '\0';
    fclose(f);

    if (source_out)     *source_out     = src_buf;
    else                free(src_buf);
    if (source_len_out) *source_len_out = (size_t)src_len;

    return true;
}

// =============================================================================
// 診断情報表示
// =============================================================================

void hjpb_print_info(const char *path) {
    if (!hjpb_is_bytecode_file(path)) {
        printf("%s は HJPB バイトコードではありません (ネイティブプラグインの可能性)\n", path);
        return;
    }

    HjpbMeta meta = {0};
    char *src = NULL;
    size_t src_len = 0;

    if (!hjpb_decode(path, &meta, &src, &src_len)) {
        printf("デコードに失敗しました: %s\n", path);
        return;
    }

    printf("=== HJPB バイトコード情報 ===\n");
    printf("  ファイル     : %s\n", path);
    printf("  フォーマット : HJPB v%d.%d\n", HJPB_VERSION_MAJOR, HJPB_VERSION_MINOR);
    printf("  名前         : %s\n", meta.name[0]        ? meta.name        : "(未設定)");
    printf("  バージョン   : %s\n", meta.version[0]     ? meta.version     : "(未設定)");
    printf("  作者         : %s\n", meta.author[0]      ? meta.author      : "(未設定)");
    printf("  説明         : %s\n", meta.description[0] ? meta.description : "(未設定)");
    printf("  ソースサイズ : %zu バイト\n", src_len);

    /* ソースの最初の行を表示 */
    if (src && src_len > 0) {
        const char *nl = strchr(src, '\n');
        size_t first_len = nl ? (size_t)(nl - src) : src_len;
        if (first_len > 80) first_len = 80;
        printf("  先頭行       : %.*s%s\n", (int)first_len, src,
               (nl && src_len > first_len + 1) ? " ..." : "");
    }
    printf("============================\n");

    free(src);
}
