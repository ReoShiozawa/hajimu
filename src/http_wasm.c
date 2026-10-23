#include "http.h"

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
