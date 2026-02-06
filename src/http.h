/**
 * 日本語プログラミング言語 - HTTP/JSON/Webhookモジュール
 * 
 * libcurlを使ったHTTP通信とJSON処理機能
 */

#ifndef HTTP_H
#define HTTP_H

#include "value.h"

// =============================================================================
// JSON関数
// =============================================================================

/**
 * ValueをJSON文字列に変換
 */
Value json_encode(Value v);

/**
 * JSON文字列をValueに変換
 */
Value json_decode(const char *json, int length);

// =============================================================================
// 組み込み関数（JSON）
// =============================================================================

Value builtin_json_encode(int argc, Value *argv);
Value builtin_json_decode(int argc, Value *argv);

// =============================================================================
// 組み込み関数（HTTPクライアント）
// =============================================================================

Value builtin_http_get(int argc, Value *argv);
Value builtin_http_post(int argc, Value *argv);
Value builtin_http_put(int argc, Value *argv);
Value builtin_http_delete(int argc, Value *argv);
Value builtin_http_request(int argc, Value *argv);

// =============================================================================
// 組み込み関数（HTTPサーバー/Webhook）
// =============================================================================

Value builtin_http_serve(int argc, Value *argv);
Value builtin_http_stop(int argc, Value *argv);
Value builtin_url_encode(int argc, Value *argv);
Value builtin_url_decode(int argc, Value *argv);

#endif // HTTP_H
