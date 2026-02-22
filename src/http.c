/**
 * 日本語プログラミング言語 - HTTP/JSON/Webhookモジュール実装
 * 
 * libcurlを使ったHTTP通信、JSONパーサー、簡易HTTPサーバー
 */

#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ── プラットフォーム依存ヘッダー ─────────────────────────── */
#ifdef _WIN32
#  include "win_compat.h"   /* Winsock2 + usleep + gettimeofday + close→closesocket */
#  include <curl/curl.h>    /* Windows 用 libcurl (DLL) */
#else
#  include <curl/curl.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <signal.h>
#  include <errno.h>
#  include <fcntl.h>
#endif

// =============================================================================
// JSON パーサー
// =============================================================================

// JSON パーサーの状態
typedef struct {
    const char *input;
    int pos;
    int length;
} JsonParser;

static void json_skip_whitespace(JsonParser *p) {
    while (p->pos < p->length) {
        char c = p->input[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static char json_peek(JsonParser *p) {
    if (p->pos >= p->length) return '\0';
    return p->input[p->pos];
}

static char json_advance(JsonParser *p) {
    if (p->pos >= p->length) return '\0';
    return p->input[p->pos++];
}

static bool json_match(JsonParser *p, char expected) {
    json_skip_whitespace(p);
    if (p->pos < p->length && p->input[p->pos] == expected) {
        p->pos++;
        return true;
    }
    return false;
}

// 前方宣言
static Value json_parse_value(JsonParser *p);

static Value json_parse_string(JsonParser *p) {
    if (json_advance(p) != '"') return value_null();
    
    // 文字列バッファ
    int capacity = 64;
    int length = 0;
    char *buffer = malloc(capacity);
    
    while (p->pos < p->length) {
        char c = json_advance(p);
        
        if (c == '"') {
            // 文字列終了
            buffer[length] = '\0';
            Value result = value_string_n(buffer, length);
            free(buffer);
            return result;
        }
        
        if (c == '\\') {
            // エスケープシーケンス
            c = json_advance(p);
            switch (c) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u': {
                    // Unicode escape: \uXXXX
                    char hex[5] = {0};
                    for (int i = 0; i < 4 && p->pos < p->length; i++) {
                        hex[i] = json_advance(p);
                    }
                    unsigned int codepoint = (unsigned int)strtol(hex, NULL, 16);
                    
                    // UTF-8にエンコード
                    if (codepoint < 0x80) {
                        if (length + 1 >= capacity) { capacity *= 2; buffer = realloc(buffer, capacity); }
                        buffer[length++] = (char)codepoint;
                    } else if (codepoint < 0x800) {
                        if (length + 2 >= capacity) { capacity *= 2; buffer = realloc(buffer, capacity); }
                        buffer[length++] = (char)(0xC0 | (codepoint >> 6));
                        buffer[length++] = (char)(0x80 | (codepoint & 0x3F));
                    } else {
                        if (length + 3 >= capacity) { capacity *= 2; buffer = realloc(buffer, capacity); }
                        buffer[length++] = (char)(0xE0 | (codepoint >> 12));
                        buffer[length++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        buffer[length++] = (char)(0x80 | (codepoint & 0x3F));
                    }
                    continue;
                }
                default: break;
            }
        }
        
        if (length + 1 >= capacity) {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
        }
        buffer[length++] = c;
    }
    
    free(buffer);
    return value_null();
}

static Value json_parse_number(JsonParser *p) {
    int start = p->pos;
    
    if (p->pos < p->length && p->input[p->pos] == '-') p->pos++;
    
    while (p->pos < p->length && isdigit((unsigned char)p->input[p->pos])) p->pos++;
    
    if (p->pos < p->length && p->input[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->length && isdigit((unsigned char)p->input[p->pos])) p->pos++;
    }
    
    // 指数部
    if (p->pos < p->length && (p->input[p->pos] == 'e' || p->input[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->length && (p->input[p->pos] == '+' || p->input[p->pos] == '-')) p->pos++;
        while (p->pos < p->length && isdigit((unsigned char)p->input[p->pos])) p->pos++;
    }
    
    char *numstr = strndup(p->input + start, p->pos - start);
    double value = strtod(numstr, NULL);
    free(numstr);
    
    return value_number(value);
}

static Value json_parse_array(JsonParser *p) {
    json_advance(p); // '['
    json_skip_whitespace(p);
    
    Value array = value_array();
    
    if (json_peek(p) == ']') {
        json_advance(p);
        return array;
    }
    
    while (1) {
        json_skip_whitespace(p);
        Value elem = json_parse_value(p);
        array_push(&array, elem);
        value_free(&elem);
        
        json_skip_whitespace(p);
        if (json_peek(p) == ',') {
            json_advance(p);
        } else {
            break;
        }
    }
    
    json_skip_whitespace(p);
    json_match(p, ']');
    
    return array;
}

static Value json_parse_object(JsonParser *p) {
    json_advance(p); // '{'
    json_skip_whitespace(p);
    
    Value dict = value_dict();
    
    if (json_peek(p) == '}') {
        json_advance(p);
        return dict;
    }
    
    while (1) {
        json_skip_whitespace(p);
        
        // キー（文字列）
        Value key = json_parse_string(p);
        if (key.type != VALUE_STRING) {
            value_free(&key);
            break;
        }
        
        json_skip_whitespace(p);
        json_match(p, ':');
        json_skip_whitespace(p);
        
        // 値
        Value val = json_parse_value(p);
        dict_set(&dict, key.string.data, val);
        
        value_free(&key);
        value_free(&val);
        
        json_skip_whitespace(p);
        if (json_peek(p) == ',') {
            json_advance(p);
        } else {
            break;
        }
    }
    
    json_skip_whitespace(p);
    json_match(p, '}');
    
    return dict;
}

static Value json_parse_value(JsonParser *p) {
    json_skip_whitespace(p);
    
    char c = json_peek(p);
    
    if (c == '"') {
        return json_parse_string(p);
    } else if (c == '{') {
        return json_parse_object(p);
    } else if (c == '[') {
        return json_parse_array(p);
    } else if (c == 't') {
        // true
        if (p->pos + 4 <= p->length && strncmp(p->input + p->pos, "true", 4) == 0) {
            p->pos += 4;
            return value_bool(true);
        }
    } else if (c == 'f') {
        // false
        if (p->pos + 5 <= p->length && strncmp(p->input + p->pos, "false", 5) == 0) {
            p->pos += 5;
            return value_bool(false);
        }
    } else if (c == 'n') {
        // null
        if (p->pos + 4 <= p->length && strncmp(p->input + p->pos, "null", 4) == 0) {
            p->pos += 4;
            return value_null();
        }
    } else if (c == '-' || isdigit((unsigned char)c)) {
        return json_parse_number(p);
    }
    
    return value_null();
}

// =============================================================================
// JSON エンコーダー
// =============================================================================

// 動的文字列バッファ
typedef struct {
    char *data;
    int length;
    int capacity;
} StringBuffer;

static void sb_init(StringBuffer *sb) {
    sb->capacity = 128;
    sb->length = 0;
    sb->data = malloc(sb->capacity);
    sb->data[0] = '\0';
}

static void sb_append(StringBuffer *sb, const char *str, int len) {
    while (sb->length + len + 1 >= sb->capacity) {
        sb->capacity *= 2;
        sb->data = realloc(sb->data, sb->capacity);
    }
    memcpy(sb->data + sb->length, str, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
}

static void sb_append_str(StringBuffer *sb, const char *str) {
    sb_append(sb, str, (int)strlen(str));
}

static void sb_append_char(StringBuffer *sb, char c) {
    sb_append(sb, &c, 1);
}

static void json_encode_value(StringBuffer *sb, Value v);

static void json_encode_string(StringBuffer *sb, const char *s, int len) {
    sb_append_char(sb, '"');
    
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  sb_append_str(sb, "\\\""); break;
            case '\\': sb_append_str(sb, "\\\\"); break;
            case '\b': sb_append_str(sb, "\\b");  break;
            case '\f': sb_append_str(sb, "\\f");  break;
            case '\n': sb_append_str(sb, "\\n");  break;
            case '\r': sb_append_str(sb, "\\r");  break;
            case '\t': sb_append_str(sb, "\\t");  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    sb_append_str(sb, buf);
                } else {
                    sb_append_char(sb, (char)c);
                }
                break;
        }
    }
    
    sb_append_char(sb, '"');
}

static void json_encode_value(StringBuffer *sb, Value v) {
    switch (v.type) {
        case VALUE_NULL:
            sb_append_str(sb, "null");
            break;
            
        case VALUE_BOOL:
            sb_append_str(sb, v.boolean ? "true" : "false");
            break;
            
        case VALUE_NUMBER: {
            char buf[64];
            double intpart;
            if (modf(v.number, &intpart) == 0.0 &&
                v.number >= -999999999 && v.number <= 999999999) {
                snprintf(buf, sizeof(buf), "%.0f", v.number);
            } else {
                snprintf(buf, sizeof(buf), "%g", v.number);
            }
            sb_append_str(sb, buf);
            break;
        }
        
        case VALUE_STRING:
            json_encode_string(sb, v.string.data, v.string.length);
            break;
            
        case VALUE_ARRAY:
            sb_append_char(sb, '[');
            for (int i = 0; i < v.array.length; i++) {
                if (i > 0) sb_append_char(sb, ',');
                json_encode_value(sb, v.array.elements[i]);
            }
            sb_append_char(sb, ']');
            break;
            
        case VALUE_DICT:
            sb_append_char(sb, '{');
            for (int i = 0; i < v.dict.length; i++) {
                if (i > 0) sb_append_char(sb, ',');
                json_encode_string(sb, v.dict.keys[i], (int)strlen(v.dict.keys[i]));
                sb_append_char(sb, ':');
                json_encode_value(sb, v.dict.values[i]);
            }
            sb_append_char(sb, '}');
            break;
            
        default:
            sb_append_str(sb, "null");
            break;
    }
}

// =============================================================================
// JSON 公開API
// =============================================================================

Value json_encode(Value v) {
    StringBuffer sb;
    sb_init(&sb);
    json_encode_value(&sb, v);
    Value result = value_string_n(sb.data, sb.length);
    free(sb.data);
    return result;
}

Value json_decode(const char *json, int length) {
    JsonParser parser;
    parser.input = json;
    parser.pos = 0;
    parser.length = length;
    return json_parse_value(&parser);
}

// =============================================================================
// JSON 組み込み関数
// =============================================================================

Value builtin_json_encode(int argc, Value *argv) {
    (void)argc;
    return json_encode(argv[0]);
}

Value builtin_json_decode(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    return json_decode(argv[0].string.data, argv[0].string.length);
}

// =============================================================================
// libcurl レスポンスバッファ
// =============================================================================

typedef struct {
    char *data;
    size_t size;
} CurlBuffer;

static size_t http_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    CurlBuffer *buf = (CurlBuffer *)userp;
    
    char *ptr = realloc(buf->data, buf->size + realsize + 1);
    if (ptr == NULL) return 0;
    
    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    
    return realsize;
}

// レスポンスヘッダー収集用
typedef struct {
    Value *dict;   // ヘッダー辞書
} HeaderData;

static size_t curl_header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t realsize = size * nitems;
    HeaderData *hd = (HeaderData *)userdata;
    
    // "Key: Value\r\n" 形式をパース
    char *colon = memchr(buffer, ':', realsize);
    if (colon && hd->dict) {
        int key_len = (int)(colon - buffer);
        char *key = strndup(buffer, key_len);
        
        // 値の前の空白をスキップ
        char *val_start = colon + 1;
        while (val_start < buffer + realsize && *val_start == ' ') val_start++;
        
        // 末尾の\r\nを除去
        int val_len = (int)(buffer + realsize - val_start);
        while (val_len > 0 && (val_start[val_len-1] == '\r' || val_start[val_len-1] == '\n')) val_len--;
        
        // 小文字に変換
        for (int i = 0; i < key_len; i++) {
            key[i] = (char)tolower((unsigned char)key[i]);
        }
        
        Value val = value_string_n(val_start, val_len);
        dict_set(hd->dict, key, val);
        value_free(&val);
        free(key);
    }
    
    return realsize;
}

// =============================================================================
// HTTP リクエスト共通処理
// =============================================================================

/**
 * HTTP リクエストを実行し、結果を辞書で返す
 * 戻り値: {"状態": ステータスコード, "本文": レスポンスボディ, "ヘッダー": {...}}
 */
static Value http_request(const char *method, const char *url, 
                          const char *body, int body_len,
                          Value *headers_dict) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        Value result = value_dict();
        Value err = value_string("curlの初期化に失敗しました");
        dict_set(&result, "エラー", err);
        value_free(&err);
        return result;
    }
    
    CurlBuffer response_body = {NULL, 0};
    response_body.data = malloc(1);
    response_body.data[0] = '\0';
    
    Value resp_headers = value_dict();
    HeaderData header_data = { &resp_headers };
    
    // URL設定
    curl_easy_setopt(curl, CURLOPT_URL, url);
    
    // メソッド設定
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (strcmp(method, "HEAD") == 0) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    
    // ボディ設定
    if (body && body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    }
    
    // ヘッダー設定
    struct curl_slist *slist = NULL;
    if (headers_dict && headers_dict->type == VALUE_DICT) {
        for (int i = 0; i < headers_dict->dict.length; i++) {
            char header_line[1024];
            char *val_str = value_to_string(headers_dict->dict.values[i]);
            snprintf(header_line, sizeof(header_line), "%s: %s", 
                     headers_dict->dict.keys[i], val_str);
            free(val_str);
            slist = curl_slist_append(slist, header_line);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    }
    
    // コールバック設定
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);
    
    // リダイレクト追跡
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    
    // タイムアウト設定（30秒）
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    
    // SSL設定
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // User-Agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nihongo-lang/1.0");
    
    // リクエスト実行
    CURLcode res = curl_easy_perform(curl);
    
    // 結果を辞書に格納
    Value result = value_dict();
    
    if (res != CURLE_OK) {
        Value err = value_string(curl_easy_strerror(res));
        dict_set(&result, "エラー", err);
        Value status = value_number(0);
        dict_set(&result, "状態", status);
        value_free(&err);
        value_free(&status);
    } else {
        // ステータスコード
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        Value status = value_number((double)http_code);
        dict_set(&result, "状態", status);
        value_free(&status);
        
        // レスポンスボディ
        Value body_val = value_string_n(response_body.data, (int)response_body.size);
        dict_set(&result, "本文", body_val);
        value_free(&body_val);
        
        // レスポンスヘッダー
        dict_set(&result, "ヘッダー", resp_headers);
    }
    
    // クリーンアップ
    if (slist) curl_slist_free_all(slist);
    curl_easy_cleanup(curl);
    free(response_body.data);
    value_free(&resp_headers);
    
    return result;
}

// =============================================================================
// HTTP 組み込み関数
// =============================================================================

// HTTP取得(URL) または HTTP取得(URL, ヘッダー辞書)
Value builtin_http_get(int argc, Value *argv) {
    if (argv[0].type != VALUE_STRING) return value_null();
    
    Value *headers = (argc >= 2 && argv[1].type == VALUE_DICT) ? &argv[1] : NULL;
    return http_request("GET", argv[0].string.data, NULL, 0, headers);
}

// HTTP送信(URL, ボディ) または HTTP送信(URL, ボディ, ヘッダー辞書)
Value builtin_http_post(int argc, Value *argv) {
    if (argv[0].type != VALUE_STRING) return value_null();
    
    // ボディの準備
    const char *body = NULL;
    int body_len = 0;
    Value json_body = value_null();
    
    if (argc >= 2) {
        if (argv[1].type == VALUE_STRING) {
            body = argv[1].string.data;
            body_len = argv[1].string.length;
        } else if (argv[1].type == VALUE_DICT || argv[1].type == VALUE_ARRAY) {
            // 辞書/配列はJSONに変換
            json_body = json_encode(argv[1]);
            body = json_body.string.data;
            body_len = json_body.string.length;
        }
    }
    
    Value *headers = (argc >= 3 && argv[2].type == VALUE_DICT) ? &argv[2] : NULL;
    
    // Content-Typeが設定されていなければJSONとして送信
    Value auto_headers = value_dict();
    if (json_body.type == VALUE_STRING && headers == NULL) {
        Value ct = value_string("application/json; charset=utf-8");
        dict_set(&auto_headers, "Content-Type", ct);
        value_free(&ct);
        headers = &auto_headers;
    }
    
    Value result = http_request("POST", argv[0].string.data, body, body_len, headers);
    
    value_free(&json_body);
    value_free(&auto_headers);
    
    return result;
}

// HTTP更新(URL, ボディ) または HTTP更新(URL, ボディ, ヘッダー辞書)
Value builtin_http_put(int argc, Value *argv) {
    if (argv[0].type != VALUE_STRING) return value_null();
    
    const char *body = NULL;
    int body_len = 0;
    Value json_body = value_null();
    
    if (argc >= 2) {
        if (argv[1].type == VALUE_STRING) {
            body = argv[1].string.data;
            body_len = argv[1].string.length;
        } else if (argv[1].type == VALUE_DICT || argv[1].type == VALUE_ARRAY) {
            json_body = json_encode(argv[1]);
            body = json_body.string.data;
            body_len = json_body.string.length;
        }
    }
    
    Value *headers = (argc >= 3 && argv[2].type == VALUE_DICT) ? &argv[2] : NULL;
    
    Value auto_headers = value_dict();
    if (json_body.type == VALUE_STRING && headers == NULL) {
        Value ct = value_string("application/json; charset=utf-8");
        dict_set(&auto_headers, "Content-Type", ct);
        value_free(&ct);
        headers = &auto_headers;
    }
    
    Value result = http_request("PUT", argv[0].string.data, body, body_len, headers);
    
    value_free(&json_body);
    value_free(&auto_headers);
    
    return result;
}

// HTTP削除(URL) または HTTP削除(URL, ヘッダー辞書)
Value builtin_http_delete(int argc, Value *argv) {
    if (argv[0].type != VALUE_STRING) return value_null();
    
    Value *headers = (argc >= 2 && argv[1].type == VALUE_DICT) ? &argv[1] : NULL;
    return http_request("DELETE", argv[0].string.data, NULL, 0, headers);
}

// 汎用リクエスト: HTTPリクエスト(メソッド, URL, ボディ, ヘッダー)
Value builtin_http_request(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_null();
    }
    
    const char *method = argv[0].string.data;
    const char *url = argv[1].string.data;
    const char *body = NULL;
    int body_len = 0;
    Value json_body = value_null();
    
    if (argc >= 3 && argv[2].type != VALUE_NULL) {
        if (argv[2].type == VALUE_STRING) {
            body = argv[2].string.data;
            body_len = argv[2].string.length;
        } else if (argv[2].type == VALUE_DICT || argv[2].type == VALUE_ARRAY) {
            json_body = json_encode(argv[2]);
            body = json_body.string.data;
            body_len = json_body.string.length;
        }
    }
    
    Value *headers = (argc >= 4 && argv[3].type == VALUE_DICT) ? &argv[3] : NULL;
    
    Value result = http_request(method, url, body, body_len, headers);
    value_free(&json_body);
    
    return result;
}

// =============================================================================
// 簡易HTTPサーバー (Webhook用)
// =============================================================================

// グローバルサーバー状態
static volatile int server_running = 0;
static int server_socket_fd = -1;

// リクエスト解析
typedef struct {
    char method[16];
    char path[2048];
    char body[65536];
    int body_length;
    Value headers;
    char query[4096];
} HttpRequest;

static void parse_http_request(const char *raw, int raw_len, HttpRequest *req) {
    memset(req, 0, sizeof(HttpRequest));
    req->headers = value_dict();
    
    // リクエストライン: METHOD /path HTTP/1.x
    const char *p = raw;
    const char *end = raw + raw_len;
    
    // メソッド
    int i = 0;
    while (p < end && *p != ' ' && i < 15) {
        req->method[i++] = *p++;
    }
    req->method[i] = '\0';
    
    // スペーススキップ
    while (p < end && *p == ' ') p++;
    
    // パス（クエリ含む）
    i = 0;
    const char *query_start = NULL;
    while (p < end && *p != ' ' && *p != '\r' && *p != '\n' && i < 2047) {
        if (*p == '?' && !query_start) {
            req->path[i] = '\0';
            query_start = p + 1;
        }
        if (!query_start) {
            req->path[i++] = *p;
        }
        p++;
    }
    if (!query_start) {
        req->path[i] = '\0';
    }
    
    // クエリ文字列
    if (query_start) {
        const char *qend = p;
        int qlen = (int)(qend - query_start);
        if (qlen > 4095) qlen = 4095;
        memcpy(req->query, query_start, qlen);
        req->query[qlen] = '\0';
    }
    
    // ヘッダー行までスキップ
    while (p < end && *p != '\n') p++;
    if (p < end) p++; // \n
    
    // ヘッダー終端(\r\n\r\n)を見つけてボディ開始位置を特定
    const char *body_start = NULL;
    {
        const char *s = p;
        while (s < end - 1) {
            if (s[0] == '\r' && s[1] == '\n' && s + 2 < end && s[2] == '\r' && s[3] == '\n') {
                body_start = s + 4;
                break;
            }
            if (s[0] == '\n' && s[1] == '\n') {
                body_start = s + 2;
                break;
            }
            s++;
        }
    }
    
    // ヘッダーの解析（body_startまで）
    const char *headers_end = body_start ? body_start : end;
    while (p < headers_end) {
        // 空行スキップ
        if (*p == '\r' || *p == '\n') {
            p++;
            continue;
        }
        
        // "Key: Value"
        const char *key_start = p;
        while (p < headers_end && *p != ':') p++;
        if (p >= headers_end) break;
        
        int key_len = (int)(p - key_start);
        p++; // ':'
        while (p < headers_end && *p == ' ') p++; // 値の前の空白
        
        const char *val_start = p;
        while (p < headers_end && *p != '\r' && *p != '\n') p++;
        int val_len = (int)(p - val_start);
        
        // ヘッダー辞書に追加
        char *key = strndup(key_start, key_len);
        // 小文字に変換
        for (int j = 0; j < key_len; j++) {
            key[j] = (char)tolower((unsigned char)key[j]);
        }
        Value val = value_string_n(val_start, val_len);
        dict_set(&req->headers, key, val);
        value_free(&val);
        free(key);
        
        while (p < headers_end && (*p == '\r' || *p == '\n')) p++;
    }
    
    // ボディ
    if (body_start && body_start < end) {
        req->body_length = (int)(end - body_start);
        if (req->body_length > (int)sizeof(req->body) - 1) {
            req->body_length = (int)sizeof(req->body) - 1;
        }
        memcpy(req->body, p, req->body_length);
        req->body[req->body_length] = '\0';
    }
}

static void send_http_response(int client_fd, int status_code, const char *status_text,
                                const char *content_type, const char *body, int body_len) {
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len);
    
    write(client_fd, header, header_len);
    if (body && body_len > 0) {
        write(client_fd, body, body_len);
    }
}

// Webhook受信（1回だけリクエストを受けて返す）
// サーバー起動(ポート番号) -> リクエスト辞書を返す
Value builtin_http_serve(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int port = (int)argv[0].number;
    
    // タイムアウト（秒）- オプション
    int timeout_sec = 60;
    if (argc >= 2 && argv[1].type == VALUE_NUMBER) {
        timeout_sec = (int)argv[1].number;
    }
    
    // ソケット作成
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        Value result = value_dict();
        Value err = value_string("ソケットの作成に失敗しました");
        dict_set(&result, "エラー", err);
        value_free(&err);
        return result;
    }
    
    // ポート再利用を許可
    int opt = 1;
#ifdef _WIN32
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        Value result = value_dict();
        char errmsg[128];
        snprintf(errmsg, sizeof(errmsg), "ポート%dへのバインドに失敗しました: %s", port, strerror(errno));
        Value err = value_string(errmsg);
        dict_set(&result, "エラー", err);
        value_free(&err);
        return result;
    }
    
    listen(sockfd, 5);
    server_socket_fd = sockfd;
    server_running = 1;
    
    printf("[サーバー] ポート%dで待機中...\n", port);
    fflush(stdout);
    
    // タイムアウト設定
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
#ifdef _WIN32
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    
    // 接続待ち
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
    
    if (client_fd < 0) {
        close(sockfd);
        server_running = 0;
        server_socket_fd = -1;
        
        Value result = value_dict();
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            Value err = value_string("タイムアウトしました");
            dict_set(&result, "エラー", err);
            value_free(&err);
        } else {
            Value err = value_string("接続の受け入れに失敗しました");
            dict_set(&result, "エラー", err);
            value_free(&err);
        }
        return result;
    }
    
    // リクエスト読み込み
    char buffer[65536];
    int total_received = 0;
    
    // まずヘッダーを読み込む
    while (total_received < (int)sizeof(buffer) - 1) {
        int n = (int)read(client_fd, buffer + total_received, sizeof(buffer) - 1 - total_received);
        if (n <= 0) break;
        total_received += n;
        buffer[total_received] = '\0';
        
        // ヘッダー終端(\r\n\r\n)が見つかったか確認
        char *header_end = strstr(buffer, "\r\n\r\n");
        if (header_end) {
            // Content-Length を確認して残りのボディを読む
            char *cl = strcasestr(buffer, "content-length:");
            if (cl) {
                int content_length = atoi(cl + 15);
                int header_size = (int)(header_end + 4 - buffer);
                int body_received = total_received - header_size;
                
                while (body_received < content_length && total_received < (int)sizeof(buffer) - 1) {
                    n = (int)read(client_fd, buffer + total_received, sizeof(buffer) - 1 - total_received);
                    if (n <= 0) break;
                    total_received += n;
                    body_received += n;
                }
                buffer[total_received] = '\0';
            }
            break;
        }
    }
    
    if (total_received <= 0) {
        close(client_fd);
        close(sockfd);
        server_running = 0;
        server_socket_fd = -1;
        return value_null();
    }
    
    // リクエスト解析
    HttpRequest req;
    parse_http_request(buffer, total_received, &req);
    
    // OPTIONS（CORS preflight）は自動応答
    if (strcmp(req.method, "OPTIONS") == 0) {
        send_http_response(client_fd, 200, "OK", "text/plain", "", 0);
        close(client_fd);
        value_free(&req.headers);
        
        // 次のリクエストを待つ（再帰）
        close(sockfd);
        server_running = 0;
        server_socket_fd = -1;
        return builtin_http_serve(argc, argv);
    }
    
    // レスポンスを返す（200 OK）
    const char *resp_body = "{\"状態\":\"受信完了\"}";
    send_http_response(client_fd, 200, "OK", "application/json", resp_body, (int)strlen(resp_body));
    
    close(client_fd);
    close(sockfd);
    server_running = 0;
    server_socket_fd = -1;
    
    // リクエスト情報を辞書として返す
    Value result = value_dict();
    
    Value method_val = value_string(req.method);
    dict_set(&result, "メソッド", method_val);
    value_free(&method_val);
    
    Value path_val = value_string(req.path);
    dict_set(&result, "パス", path_val);
    value_free(&path_val);
    
    Value body_val = value_string(req.body);
    dict_set(&result, "本文", body_val);
    value_free(&body_val);
    
    dict_set(&result, "ヘッダー", req.headers);
    value_free(&req.headers);
    
    Value query_val = value_string(req.query);
    dict_set(&result, "クエリ", query_val);
    value_free(&query_val);
    
    // JSON本文を自動パース
    if (req.body_length > 0) {
        Value parsed = json_decode(req.body, req.body_length);
        if (parsed.type != VALUE_NULL) {
            dict_set(&result, "データ", parsed);
        }
        value_free(&parsed);
    }
    
    return result;
}

// サーバー停止
Value builtin_http_stop(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    
    server_running = 0;
    if (server_socket_fd >= 0) {
        close(server_socket_fd);
        server_socket_fd = -1;
    }
    printf("[サーバー] 停止しました\n");
    return value_bool(true);
}

// =============================================================================
// URL エンコード/デコード
// =============================================================================

Value builtin_url_encode(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    CURL *curl = curl_easy_init();
    if (!curl) return value_null();
    
    char *encoded = curl_easy_escape(curl, argv[0].string.data, argv[0].string.length);
    Value result = value_string(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    
    return result;
}

Value builtin_url_decode(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    CURL *curl = curl_easy_init();
    if (!curl) return value_null();
    
    int out_len = 0;
    char *decoded = curl_easy_unescape(curl, argv[0].string.data, argv[0].string.length, &out_len);
    Value result = value_string_n(decoded, out_len);
    curl_free(decoded);
    curl_easy_cleanup(curl);
    
    return result;
}
