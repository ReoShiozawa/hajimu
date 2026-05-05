#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value json_encode(Value v) {
    char *text = value_to_string(v);
    Value result = value_string(text ? text : "");
    free(text);
    return result;
}

Value json_decode(const char *json, int length) {
    if (json == NULL) return value_null();
    return value_string_n(json, length >= 0 ? length : (int)strlen(json));
}

Value builtin_json_encode(int argc, Value *argv) {
    if (argc < 1) return value_string("");
    return json_encode(argv[0]);
}

Value builtin_json_decode(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_STRING) return value_null();
    return json_decode(argv[0].string.data, argv[0].string.byte_length);
}

Value builtin_http_get(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_string("WASM版ではHTTP取得はブラウザ連携機能として扱います");
}

Value builtin_http_post(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_string("WASM版ではHTTP送信は未対応です");
}

Value builtin_http_put(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_string("WASM版ではHTTP送信は未対応です");
}

Value builtin_http_delete(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_string("WASM版ではHTTP削除は未対応です");
}

Value builtin_http_request(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_string("WASM版ではHTTPリクエストは未対応です");
}

Value builtin_http_serve(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_string("WASM版ではHTTPサーバーは未対応です");
}

Value builtin_http_stop(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_null();
}

Value builtin_url_encode(int argc, Value *argv) {
    if (argc < 1) return value_string("");
    return value_copy(argv[0]);
}

Value builtin_url_decode(int argc, Value *argv) {
    if (argc < 1) return value_string("");
    return value_copy(argv[0]);
}
