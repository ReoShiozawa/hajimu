/**
 * 日本語プログラミング言語 - 環境（スコープ）ヘッダー
 * 
 * 変数のバインディングを管理
 */

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "value.h"
#include <stdbool.h>

// =============================================================================
// 定数
// =============================================================================

#define ENV_HASH_SIZE 64

// =============================================================================
// 環境エントリ
// =============================================================================

typedef struct EnvEntry {
    char *name;             // 変数名
    Value value;            // 値
    bool is_const;          // 定数かどうか
    struct EnvEntry *next;  // ハッシュチェーン
} EnvEntry;

// =============================================================================
// 環境構造体
// =============================================================================

typedef struct Environment {
    EnvEntry *table[ENV_HASH_SIZE];  // ハッシュテーブル
    struct Environment *parent;       // 親スコープ
    int depth;                        // ネスト深度
    int ref_count;                    // 参照カウント
} Environment;

// =============================================================================
// 環境の作成・解放
// =============================================================================

/**
 * 新しい環境を作成
 * @param parent 親環境（NULLならグローバル）
 * @return 新しい環境
 */
Environment *env_new(Environment *parent);

/**
 * 環境を解放
 * @param env 環境
 */
void env_free(Environment *env);

/**
 * 環境の参照カウントを増加
 * @param env 環境
 */
void env_retain(Environment *env);

/**
 * 環境の参照カウントを減少し、0になれば解放
 * @param env 環境
 */
void env_release(Environment *env);

// =============================================================================
// 変数操作
// =============================================================================

/**
 * 変数を定義（現在のスコープに）
 * @param env 環境
 * @param name 変数名
 * @param value 値
 * @param is_const 定数かどうか
 * @return 成功したらtrue
 */
bool env_define(Environment *env, const char *name, Value value, bool is_const);

/**
 * 変数を取得（親スコープも検索）
 * @param env 環境
 * @param name 変数名
 * @return 値へのポインタ（見つからなければNULL）
 */
Value *env_get(Environment *env, const char *name);

/**
 * 変数に代入（親スコープも検索）
 * @param env 環境
 * @param name 変数名
 * @param value 新しい値
 * @return 成功したらtrue（変数が存在し、定数でない場合）
 */
bool env_set(Environment *env, const char *name, Value value);

/**
 * 変数が存在するか確認
 * @param env 環境
 * @param name 変数名
 * @return 存在すればtrue
 */
bool env_exists(Environment *env, const char *name);

/**
 * 変数が定数かどうか確認
 * @param env 環境
 * @param name 変数名
 * @return 定数ならtrue
 */
bool env_is_const(Environment *env, const char *name);

/**
 * 現在のスコープに変数が存在するか確認（親は検索しない）
 * @param env 環境
 * @param name 変数名
 * @return 存在すればtrue
 */
bool env_exists_local(Environment *env, const char *name);

// =============================================================================
// デバッグ
// =============================================================================

/**
 * 環境の内容を表示
 * @param env 環境
 */
void env_print(Environment *env);

#endif // ENVIRONMENT_H
