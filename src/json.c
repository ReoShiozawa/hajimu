/* ネイティブ版とWASM版で共有するJSON変換。HTTP通信には依存しない。 */
#include "http.h"
#include "array_grow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>

#define JSON_STRING_INITIAL_CAPACITY 64
#define JSON_MAX_DEPTH 64

// =============================================================================
// JSON パーサー
// =============================================================================

// JSON パーサーの状態
typedef struct {
    const char *input;
    int pos;
    int length;
    bool error;
    int depth;
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
    int capacity = JSON_STRING_INITIAL_CAPACITY;
    int length = 0;
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        p->error = true;
        return value_null();
    }

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
                    for (int i = 0; i < 4; i++) {
                        if (p->pos >= p->length || !isxdigit((unsigned char)p->input[p->pos])) {
                            free(buffer);
                            p->error = true;
                            return value_null();
                        }
                        hex[i] = json_advance(p);
                    }
                    unsigned int codepoint = (unsigned int)strtol(hex, NULL, 16);

                    // UTF-8にエンコード
                    if (codepoint < 0x80) {
                        ARRAY_GROW(buffer, length + 1, capacity, char, free(buffer); return value_null());
                        buffer[length++] = (char)codepoint;
                    } else if (codepoint < 0x800) {
                        ARRAY_GROW(buffer, length + 2, capacity, char, free(buffer); return value_null());
                        buffer[length++] = (char)(0xC0 | (codepoint >> 6));
                        buffer[length++] = (char)(0x80 | (codepoint & 0x3F));
                    } else {
                        ARRAY_GROW(buffer, length + 3, capacity, char, free(buffer); return value_null());
                        buffer[length++] = (char)(0xE0 | (codepoint >> 12));
                        buffer[length++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        buffer[length++] = (char)(0x80 | (codepoint & 0x3F));
                    }
                    continue;
                }
                default:
                    free(buffer);
                    p->error = true;
                    return value_null();
            }
        } else if ((unsigned char)c < 0x20) {
            free(buffer);
            p->error = true;
            return value_null();
        }

        if (length + 1 >= capacity) {
            ARRAY_GROW(buffer, length + 1, capacity, char, free(buffer); return value_null());
        }
        buffer[length++] = c;
    }

    free(buffer);
    p->error = true;
    return value_null();
}

static Value json_parse_number(JsonParser *p) {
    int start = p->pos;

    if (p->pos < p->length && p->input[p->pos] == '-') p->pos++;

    int digit_start = p->pos;
    while (p->pos < p->length && isdigit((unsigned char)p->input[p->pos])) p->pos++;
    if (p->pos == digit_start) {
        p->error = true;
        return value_null();
    }

    if (p->pos < p->length && p->input[p->pos] == '.') {
        p->pos++;
        int frac_start = p->pos;
        while (p->pos < p->length && isdigit((unsigned char)p->input[p->pos])) p->pos++;
        if (p->pos == frac_start) {
            p->error = true;
            return value_null();
        }
    }

    // 指数部
    if (p->pos < p->length && (p->input[p->pos] == 'e' || p->input[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->length && (p->input[p->pos] == '+' || p->input[p->pos] == '-')) p->pos++;
        int exp_start = p->pos;
        while (p->pos < p->length && isdigit((unsigned char)p->input[p->pos])) p->pos++;
        if (p->pos == exp_start) {
            p->error = true;
            return value_null();
        }
    }

    size_t number_length = (size_t)(p->pos - start);
    char *numstr = malloc(number_length + 1);
    if (numstr == NULL) {
        p->error = true;
        return value_null();
    }
    memcpy(numstr, p->input + start, number_length);
    numstr[number_length] = '\0';
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
        if (p->error) {
            value_free(&array);
            return value_null();
        }
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
    if (!json_match(p, ']')) {
        value_free(&array);
        p->error = true;
        return value_null();
    }

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
            value_free(&dict);
            p->error = true;
            return value_null();
        }

        json_skip_whitespace(p);
        if (!json_match(p, ':')) {
            value_free(&key);
            value_free(&dict);
            p->error = true;
            return value_null();
        }
        json_skip_whitespace(p);

        // 値
        Value val = json_parse_value(p);
        if (p->error) {
            value_free(&key);
            value_free(&val);
            value_free(&dict);
            return value_null();
        }
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
    if (!json_match(p, '}')) {
        value_free(&dict);
        p->error = true;
        return value_null();
    }

    return dict;
}

static Value json_parse_value(JsonParser *p) {
    json_skip_whitespace(p);

    char c = json_peek(p);

    if (c == '"') {
        return json_parse_string(p);
    } else if (c == '{' || c == '[') {
        if (p->depth >= JSON_MAX_DEPTH) {
            p->error = true;
            return value_null();
        }
        p->depth++;
        Value result = c == '{' ? json_parse_object(p) : json_parse_array(p);
        p->depth--;
        return result;
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

    p->error = true;
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
    int depth;
    bool failed;
} StringBuffer;

static void sb_init(StringBuffer *sb) {
    sb->capacity = 128;
    sb->length = 0;
    sb->depth = 0;
    sb->failed = false;
    sb->data = malloc(sb->capacity);
    if (sb->data == NULL) {
        sb->capacity = 0;
        sb->failed = true;
        return;
    }
    sb->data[0] = '\0';
}

static void sb_append(StringBuffer *sb, const char *str, int len) {
    if (sb->failed) return;
    if (len < 0 || len > INT_MAX - sb->length - 1) {
        sb->failed = true;
        return;
    }
    while (sb->length + len + 1 >= sb->capacity) {
        ARRAY_GROW(sb->data, sb->length + len + 1, sb->capacity, char, sb->failed = true; return);
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
    if (sb->failed) return;
    bool container = v.type == VALUE_ARRAY || v.type == VALUE_DICT;
    if (container && sb->depth >= JSON_MAX_DEPTH) {
        sb->failed = true;
        return;
    }
    if (container) sb->depth++;
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
            json_encode_string(sb, v.string.data, v.string.byte_length);
            break;

        case VALUE_ARRAY:
            sb_append_char(sb, '[');
            for (int i = 0; i < v.array.length; i++) {
                if (i > 0) sb_append_char(sb, ',');
                json_encode_value(sb, v.array.elements[i]);
            }
            sb_append_char(sb, ']');
            break;

        case VALUE_NUMERIC_ARRAY:
            sb_append_char(sb, '[');
            for (int i = 0; i < v.numeric_array.length; i++) {
                if (i > 0) sb_append_char(sb, ',');
                char buf[64];
                double intpart;
                double n = numeric_array_get(&v, i);
                if (modf(n, &intpart) == 0.0 &&
                    n >= -999999999 && n <= 999999999) {
                    snprintf(buf, sizeof(buf), "%.0f", n);
                } else {
                    snprintf(buf, sizeof(buf), "%g", n);
                }
                sb_append_str(sb, buf);
            }
            sb_append_char(sb, ']');
            break;

        case VALUE_MATRIX:
            sb_append_char(sb, '[');
            for (int r = 0; r < v.matrix.rows; r++) {
                if (r > 0) sb_append_char(sb, ',');
                sb_append_char(sb, '[');
                for (int c = 0; c < v.matrix.cols; c++) {
                    if (c > 0) sb_append_char(sb, ',');
                    char buf[64];
                    double intpart;
                    double n = matrix_get(&v, r, c);
                    if (modf(n, &intpart) == 0.0 &&
                        n >= -999999999 && n <= 999999999) {
                        snprintf(buf, sizeof(buf), "%.0f", n);
                    } else {
                        snprintf(buf, sizeof(buf), "%g", n);
                    }
                    sb_append_str(sb, buf);
                }
                sb_append_char(sb, ']');
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
    if (container) sb->depth--;
}

// =============================================================================
// JSON 公開API
// =============================================================================

Value json_encode(Value v) {
    StringBuffer sb;
    sb_init(&sb);
    json_encode_value(&sb, v);
    Value result = sb.failed ? value_null() : value_string_n(sb.data, sb.length);
    free(sb.data);
    return result;
}

bool json_decode_checked(const char *json, int length, Value *out) {
    if (out != NULL) *out = value_null();
    if (json == NULL) return false;
    if (length < 0) {
        size_t size = strlen(json);
        if (size > INT_MAX) return false;
        length = (int)size;
    }
    JsonParser parser;
    parser.input = json;
    parser.pos = 0;
    parser.length = length;
    parser.error = false;
    parser.depth = 0;
    Value result = json_parse_value(&parser);
    json_skip_whitespace(&parser);
    if (parser.error || parser.pos != parser.length) {
        value_free(&result);
        if (out != NULL) *out = value_null();
        return false;
    }
    if (out != NULL) {
        *out = result;
    } else {
        value_free(&result);
    }
    return true;
}

Value json_decode(const char *json, int length) {
    Value result = value_null();
    if (!json_decode_checked(json, length, &result)) {
        return value_null();
    }
    return result;
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
    return json_decode(argv[0].string.data, argv[0].string.byte_length);
}
