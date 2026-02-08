/**
 * 日本語プログラミング言語 - 評価器ヘッダー
 * 
 * ASTを評価して実行
 */

#ifndef EVALUATOR_H
#define EVALUATOR_H

#include "ast.h"
#include "value.h"
#include "environment.h"
#include "plugin.h"
#include <stdbool.h>

// =============================================================================
// 定数
// =============================================================================

#define MAX_RECURSION_DEPTH 1000

// =============================================================================
// インポートされたモジュール
// =============================================================================

typedef struct {
    char *source;       // ソースコード
    ASTNode *ast;       // AST
} ImportedModule;

// =============================================================================
// 評価器構造体
// =============================================================================

typedef struct {
    Environment *global;        // グローバル環境
    Environment *current;       // 現在の環境
    
    // 制御フロー
    bool returning;             // return中
    bool breaking;              // break中
    bool continuing;            // continue中
    Value return_value;         // return値
    
    // 例外処理
    bool throwing;              // 例外発生中
    Value exception_value;      // 例外値
    
    // OOP
    Value *current_instance;    // 現在のインスタンス（自分）
    
    // ジェネレータ
    bool in_generator;          // ジェネレータ関数実行中
    Value *generator_target;    // yield値の蓄積先
    
    // デバッグ
    bool debug_mode;            // デバッグモード
    bool step_mode;             // ステップ実行モード
    int last_line;              // 最後に実行した行
    
    // エラー情報
    bool had_error;
    char error_message[512];
    int error_line;
    int error_column;
    
    // 再帰深度
    int recursion_depth;
    
    // コールスタック（スタックトレース用）
    struct {
        const char *func_name;
        int line;
    } call_stack[128];
    int call_stack_depth;
    
    // インポートされたモジュール
    ImportedModule *imported_modules;
    int imported_count;
    int imported_capacity;
    
    // 現在実行中のファイルパス（インポートの相対パス解決用）
    const char *current_file;
    
    // インポート済みパスのキャッシュ（重複防止）
    char **imported_paths;
    int imported_path_count;
    int imported_path_capacity;
    
    // C拡張プラグインマネージャ
    PluginManager plugin_manager;
} Evaluator;

// =============================================================================
// 評価器の初期化・解放
// =============================================================================

/**
 * 評価器を作成
 * @return 新しい評価器
 */
Evaluator *evaluator_new(void);

/**
 * 評価器を解放
 * @param eval 評価器
 */
void evaluator_free(Evaluator *eval);

// =============================================================================
// 評価関数
// =============================================================================

/**
 * プログラムを実行
 * @param eval 評価器
 * @param program プログラムAST
 * @return 最後の評価結果
 */
Value evaluator_run(Evaluator *eval, ASTNode *program);

/**
 * 単一のノードを評価
 * @param eval 評価器
 * @param node ASTノード
 * @return 評価結果
 */
Value evaluate(Evaluator *eval, ASTNode *node);

// =============================================================================
// 組み込み関数の登録
// =============================================================================

/**
 * 組み込み関数を登録
 * @param eval 評価器
 */
void register_builtins(Evaluator *eval);

// =============================================================================
// エラー処理
// =============================================================================

/**
 * エラーが発生したか
 * @param eval 評価器
 * @return エラーがあればtrue
 */
bool evaluator_had_error(Evaluator *eval);

/**
 * エラーメッセージを取得
 * @param eval 評価器
 * @return エラーメッセージ
 */
const char *evaluator_error_message(Evaluator *eval);

/**
 * エラーをクリア
 * @param eval 評価器
 */
void evaluator_clear_error(Evaluator *eval);

/**
 * デバッグモードを設定
 * @param eval 評価器
 * @param enabled 有効/無効
 */
void evaluator_set_debug_mode(Evaluator *eval, bool enabled);

/**
 * 実行時エラーを報告
 * @param eval 評価器
 * @param line 行番号
 * @param format フォーマット文字列
 * @param ... 引数
 */
void runtime_error(Evaluator *eval, int line, const char *format, ...);

#endif // EVALUATOR_H
