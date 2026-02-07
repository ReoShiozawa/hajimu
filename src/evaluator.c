/**
 * 日本語プログラミング言語 - 評価器実装
 */

#include "evaluator.h"
#include "parser.h"
#include "http.h"
#include "async.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <regex.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

// グローバルevaluatorポインタ（高階関数・toStringプロトコル用）
static Evaluator *g_eval = NULL;

// 非同期モジュール用のグローバル評価器ポインタ
Evaluator *g_eval_for_async = NULL;

// =============================================================================
// 前方宣言
// =============================================================================

static Value evaluate_binary(Evaluator *eval, ASTNode *node);
static Value evaluate_unary(Evaluator *eval, ASTNode *node);
static Value evaluate_call(Evaluator *eval, ASTNode *node);
static Value evaluate_index(Evaluator *eval, ASTNode *node);
static Value evaluate_member(Evaluator *eval, ASTNode *node);
static Value evaluate_function_def(Evaluator *eval, ASTNode *node);
static Value evaluate_var_decl(Evaluator *eval, ASTNode *node);
static Value evaluate_assign(Evaluator *eval, ASTNode *node);
static Value evaluate_if(Evaluator *eval, ASTNode *node);
static Value evaluate_while(Evaluator *eval, ASTNode *node);
static Value evaluate_for(Evaluator *eval, ASTNode *node);
static Value evaluate_return(Evaluator *eval, ASTNode *node);
static Value evaluate_block(Evaluator *eval, ASTNode *node);
static Value evaluate_import(Evaluator *eval, ASTNode *node);
static Value evaluate_class_def(Evaluator *eval, ASTNode *node);
static Value evaluate_new(Evaluator *eval, ASTNode *node);
static Value evaluate_try(Evaluator *eval, ASTNode *node);
static Value evaluate_throw(Evaluator *eval, ASTNode *node);
static Value evaluate_lambda(Evaluator *eval, ASTNode *node);
static Value evaluate_switch(Evaluator *eval, ASTNode *node);
static Value evaluate_foreach(Evaluator *eval, ASTNode *node);
static Value evaluate_string_interpolation(Evaluator *eval, const char *str, int line);
static Value call_function_value(Value *func, Value *args, int argc);

// =============================================================================
// 組み込み関数のプロトタイプ
// =============================================================================

static Value builtin_print(int argc, Value *argv);
static Value builtin_input(int argc, Value *argv);
static Value builtin_length(int argc, Value *argv);
static Value builtin_append(int argc, Value *argv);
static Value builtin_remove(int argc, Value *argv);
static Value builtin_type(int argc, Value *argv);
static Value builtin_to_number(int argc, Value *argv);
static Value builtin_to_string(int argc, Value *argv);
static Value builtin_abs(int argc, Value *argv);
static Value builtin_sqrt(int argc, Value *argv);
static Value builtin_floor(int argc, Value *argv);
static Value builtin_ceil(int argc, Value *argv);
static Value builtin_round(int argc, Value *argv);
static Value builtin_random(int argc, Value *argv);
static Value builtin_max(int argc, Value *argv);
static Value builtin_min(int argc, Value *argv);

// 辞書関数
static Value builtin_dict_keys(int argc, Value *argv);
static Value builtin_dict_values(int argc, Value *argv);
static Value builtin_dict_has(int argc, Value *argv);

// 文字列関数
static Value builtin_split(int argc, Value *argv);
static Value builtin_join(int argc, Value *argv);
static Value builtin_find(int argc, Value *argv);
static Value builtin_replace(int argc, Value *argv);
static Value builtin_upper(int argc, Value *argv);
static Value builtin_lower(int argc, Value *argv);
static Value builtin_trim(int argc, Value *argv);

// 配列関数
static Value builtin_sort(int argc, Value *argv);
static Value builtin_reverse(int argc, Value *argv);
static Value builtin_slice(int argc, Value *argv);
static Value builtin_index_of(int argc, Value *argv);
static Value builtin_contains(int argc, Value *argv);

// ファイル関数
static Value builtin_file_read(int argc, Value *argv);
static Value builtin_file_write(int argc, Value *argv);
static Value builtin_file_exists(int argc, Value *argv);

// 日時関数
static Value builtin_now(int argc, Value *argv);
static Value builtin_date(int argc, Value *argv);
static Value builtin_time(int argc, Value *argv);

// 高階配列関数
static Value builtin_map(int argc, Value *argv);
static Value builtin_filter(int argc, Value *argv);
static Value builtin_reduce(int argc, Value *argv);
static Value builtin_foreach(int argc, Value *argv);

// 正規表現関数
static Value builtin_regex_match(int argc, Value *argv);
static Value builtin_regex_search(int argc, Value *argv);
static Value builtin_regex_replace(int argc, Value *argv);

// 型チェック関数
static Value builtin_is_number(int argc, Value *argv);
static Value builtin_is_string(int argc, Value *argv);
static Value builtin_is_bool(int argc, Value *argv);
static Value builtin_is_array(int argc, Value *argv);
static Value builtin_is_dict(int argc, Value *argv);
static Value builtin_is_function(int argc, Value *argv);
static Value builtin_is_null(int argc, Value *argv);

// 範囲関数
static Value builtin_range(int argc, Value *argv);

// ビット演算関数
static Value builtin_bit_and(int argc, Value *argv);
static Value builtin_bit_or(int argc, Value *argv);
static Value builtin_bit_xor(int argc, Value *argv);
static Value builtin_bit_not(int argc, Value *argv);
static Value builtin_bit_lshift(int argc, Value *argv);
static Value builtin_bit_rshift(int argc, Value *argv);

// 追加文字列関数
static Value builtin_substring(int argc, Value *argv);
static Value builtin_starts_with(int argc, Value *argv);
static Value builtin_ends_with(int argc, Value *argv);
static Value builtin_char_code(int argc, Value *argv);
static Value builtin_from_char_code(int argc, Value *argv);
static Value builtin_string_repeat(int argc, Value *argv);

// 追加配列関数
static Value builtin_pop(int argc, Value *argv);
static Value builtin_find_item(int argc, Value *argv);
static Value builtin_every(int argc, Value *argv);
static Value builtin_some(int argc, Value *argv);
static Value builtin_unique(int argc, Value *argv);
static Value builtin_zip(int argc, Value *argv);
static Value builtin_flat(int argc, Value *argv);
static Value builtin_insert(int argc, Value *argv);
static Value builtin_sort_by(int argc, Value *argv);

// 数学関数
static Value builtin_sin(int argc, Value *argv);
static Value builtin_cos(int argc, Value *argv);
static Value builtin_tan(int argc, Value *argv);
static Value builtin_log(int argc, Value *argv);
static Value builtin_log10_fn(int argc, Value *argv);
static Value builtin_random_int(int argc, Value *argv);

// ファイル追記・ディレクトリ
static Value builtin_file_append(int argc, Value *argv);
static Value builtin_dir_list(int argc, Value *argv);
static Value builtin_dir_create(int argc, Value *argv);

// その他ユーティリティ
static Value builtin_assert(int argc, Value *argv);
static Value builtin_typeof_check(int argc, Value *argv);

// システムユーティリティ
static Value builtin_sleep(int argc, Value *argv);
static Value builtin_exec(int argc, Value *argv);
static Value builtin_env_get(int argc, Value *argv);
static Value builtin_env_set(int argc, Value *argv);
static Value builtin_exit_program(int argc, Value *argv);

// ジェネレータ関数
static Value builtin_generator_next(int argc, Value *argv);
static Value builtin_generator_done(int argc, Value *argv);
static Value builtin_generator_collect(int argc, Value *argv);

// パス操作関数
static Value builtin_path_join(int argc, Value *argv);
static Value builtin_basename(int argc, Value *argv);
static Value builtin_dirname(int argc, Value *argv);
static Value builtin_extension(int argc, Value *argv);

// Base64関数
static Value builtin_base64_encode(int argc, Value *argv);
static Value builtin_base64_decode(int argc, Value *argv);

// toStringプロトコル
static Value call_instance_to_string(Value *instance);

// =============================================================================
// 評価器の初期化・解放
// =============================================================================

Evaluator *evaluator_new(void) {
    Evaluator *eval = calloc(1, sizeof(Evaluator));
    eval->global = env_new(NULL);
    eval->current = eval->global;
    eval->returning = false;
    eval->breaking = false;
    eval->continuing = false;
    eval->return_value = value_null();
    eval->throwing = false;
    eval->exception_value = value_null();
    eval->current_instance = NULL;
    eval->in_generator = false;
    eval->generator_target = NULL;
    eval->debug_mode = false;
    eval->step_mode = false;
    eval->last_line = 0;
    eval->had_error = false;
    eval->error_message[0] = '\0';
    eval->error_line = 0;
    eval->recursion_depth = 0;
    eval->call_stack_depth = 0;
    
    g_eval = eval;  // グローバルポインタ設定
    
    // インポートモジュールの初期化
    eval->imported_modules = NULL;
    eval->imported_count = 0;
    eval->imported_capacity = 0;
    
    // 乱数初期化
    srand((unsigned int)time(NULL));
    
    // 組み込み関数を登録
    register_builtins(eval);
    
    return eval;
}

void evaluator_free(Evaluator *eval) {
    if (eval == NULL) return;
    
    // 非同期ランタイムをクリーンアップ
    if (eval == g_eval_for_async) {
        async_runtime_cleanup();
        g_eval_for_async = NULL;
    }
    
    // インポートされたモジュールを解放
    for (int i = 0; i < eval->imported_count; i++) {
        free(eval->imported_modules[i].source);
        node_free(eval->imported_modules[i].ast);
    }
    free(eval->imported_modules);
    
    env_free(eval->global);
    free(eval);
}

// =============================================================================
// 組み込み関数の登録
// =============================================================================

void register_builtins(Evaluator *eval) {
    // 入出力
    env_define(eval->global, "表示", 
               value_builtin(builtin_print, "表示", 0, -1), true);
    env_define(eval->global, "入力",
               value_builtin(builtin_input, "入力", 0, 1), true);
    
    // コレクション
    env_define(eval->global, "長さ",
               value_builtin(builtin_length, "長さ", 1, 1), true);
    env_define(eval->global, "追加",
               value_builtin(builtin_append, "追加", 2, 2), true);
    env_define(eval->global, "削除",
               value_builtin(builtin_remove, "削除", 2, 2), true);
    
    // 型変換
    env_define(eval->global, "型",
               value_builtin(builtin_type, "型", 1, 1), true);
    env_define(eval->global, "数値化",
               value_builtin(builtin_to_number, "数値化", 1, 1), true);
    env_define(eval->global, "文字列化",
               value_builtin(builtin_to_string, "文字列化", 1, 1), true);
    
    // 型チェック関数
    env_define(eval->global, "数値か",
               value_builtin(builtin_is_number, "数値か", 1, 1), true);
    env_define(eval->global, "文字列か",
               value_builtin(builtin_is_string, "文字列か", 1, 1), true);
    env_define(eval->global, "真偽か",
               value_builtin(builtin_is_bool, "真偽か", 1, 1), true);
    env_define(eval->global, "配列か",
               value_builtin(builtin_is_array, "配列か", 1, 1), true);
    env_define(eval->global, "辞書か",
               value_builtin(builtin_is_dict, "辞書か", 1, 1), true);
    env_define(eval->global, "関数か",
               value_builtin(builtin_is_function, "関数か", 1, 1), true);
    env_define(eval->global, "無か",
               value_builtin(builtin_is_null, "無か", 1, 1), true);
    
    // 範囲関数
    env_define(eval->global, "範囲",
               value_builtin(builtin_range, "範囲", 1, 3), true);
    
    // ビット演算関数
    env_define(eval->global, "ビット積",
               value_builtin(builtin_bit_and, "ビット積", 2, 2), true);
    env_define(eval->global, "ビット和",
               value_builtin(builtin_bit_or, "ビット和", 2, 2), true);
    env_define(eval->global, "ビット排他",
               value_builtin(builtin_bit_xor, "ビット排他", 2, 2), true);
    env_define(eval->global, "ビット否定",
               value_builtin(builtin_bit_not, "ビット否定", 1, 1), true);
    env_define(eval->global, "左シフト",
               value_builtin(builtin_bit_lshift, "左シフト", 2, 2), true);
    env_define(eval->global, "右シフト",
               value_builtin(builtin_bit_rshift, "右シフト", 2, 2), true);
    
    // 追加文字列関数
    env_define(eval->global, "部分文字列",
               value_builtin(builtin_substring, "部分文字列", 2, 3), true);
    env_define(eval->global, "始まる",
               value_builtin(builtin_starts_with, "始まる", 2, 2), true);
    env_define(eval->global, "終わる",
               value_builtin(builtin_ends_with, "終わる", 2, 2), true);
    env_define(eval->global, "文字コード",
               value_builtin(builtin_char_code, "文字コード", 1, 2), true);
    env_define(eval->global, "コード文字",
               value_builtin(builtin_from_char_code, "コード文字", 1, 1), true);
    env_define(eval->global, "繰り返し",
               value_builtin(builtin_string_repeat, "繰り返し", 2, 2), true);
    
    // 追加配列関数
    env_define(eval->global, "末尾削除",
               value_builtin(builtin_pop, "末尾削除", 1, 1), true);
    env_define(eval->global, "探す",
               value_builtin(builtin_find_item, "探す", 2, 2), true);
    env_define(eval->global, "全て",
               value_builtin(builtin_every, "全て", 2, 2), true);
    env_define(eval->global, "一つでも",
               value_builtin(builtin_some, "一つでも", 2, 2), true);
    env_define(eval->global, "一意",
               value_builtin(builtin_unique, "一意", 1, 1), true);
    env_define(eval->global, "圧縮",
               value_builtin(builtin_zip, "圧縮", 2, 2), true);
    env_define(eval->global, "平坦化",
               value_builtin(builtin_flat, "平坦化", 1, 1), true);
    env_define(eval->global, "挿入",
               value_builtin(builtin_insert, "挿入", 3, 3), true);
    env_define(eval->global, "比較ソート",
               value_builtin(builtin_sort_by, "比較ソート", 2, 2), true);
    
    // 数学関数（拡張）
    env_define(eval->global, "正弦",
               value_builtin(builtin_sin, "正弦", 1, 1), true);
    env_define(eval->global, "余弦",
               value_builtin(builtin_cos, "余弦", 1, 1), true);
    env_define(eval->global, "正接",
               value_builtin(builtin_tan, "正接", 1, 1), true);
    env_define(eval->global, "対数",
               value_builtin(builtin_log, "対数", 1, 1), true);
    env_define(eval->global, "常用対数",
               value_builtin(builtin_log10_fn, "常用対数", 1, 1), true);
    env_define(eval->global, "乱数整数",
               value_builtin(builtin_random_int, "乱数整数", 2, 2), true);
    
    // 数学定数
    env_define(eval->global, "円周率", value_number(3.14159265358979323846), true);
    env_define(eval->global, "自然対数の底", value_number(2.71828182845904523536), true);
    
    // ファイル・ディレクトリ
    env_define(eval->global, "追記",
               value_builtin(builtin_file_append, "追記", 2, 2), true);
    env_define(eval->global, "ディレクトリ一覧",
               value_builtin(builtin_dir_list, "ディレクトリ一覧", 1, 1), true);
    env_define(eval->global, "ディレクトリ作成",
               value_builtin(builtin_dir_create, "ディレクトリ作成", 1, 1), true);
    
    // ユーティリティ
    env_define(eval->global, "表明",
               value_builtin(builtin_assert, "表明", 1, 2), true);
    env_define(eval->global, "型判定",
               value_builtin(builtin_typeof_check, "型判定", 2, 2), true);
    
    // 数学関数
    env_define(eval->global, "絶対値",
               value_builtin(builtin_abs, "絶対値", 1, 1), true);
    env_define(eval->global, "平方根",
               value_builtin(builtin_sqrt, "平方根", 1, 1), true);
    env_define(eval->global, "切り捨て",
               value_builtin(builtin_floor, "切り捨て", 1, 1), true);
    env_define(eval->global, "切り上げ",
               value_builtin(builtin_ceil, "切り上げ", 1, 1), true);
    env_define(eval->global, "四捨五入",
               value_builtin(builtin_round, "四捨五入", 1, 1), true);
    env_define(eval->global, "乱数",
               value_builtin(builtin_random, "乱数", 0, 0), true);
    env_define(eval->global, "最大",
               value_builtin(builtin_max, "最大", 1, -1), true);
    env_define(eval->global, "最小",
               value_builtin(builtin_min, "最小", 1, -1), true);
    
    // 辞書関数
    env_define(eval->global, "キー",
               value_builtin(builtin_dict_keys, "キー", 1, 1), true);
    env_define(eval->global, "値一覧",
               value_builtin(builtin_dict_values, "値一覧", 1, 1), true);
    env_define(eval->global, "含む",
               value_builtin(builtin_dict_has, "含む", 2, 2), true);
    
    // 文字列関数
    env_define(eval->global, "分割",
               value_builtin(builtin_split, "分割", 2, 2), true);
    env_define(eval->global, "結合",
               value_builtin(builtin_join, "結合", 2, 2), true);
    env_define(eval->global, "検索",
               value_builtin(builtin_find, "検索", 2, 2), true);
    env_define(eval->global, "置換",
               value_builtin(builtin_replace, "置換", 3, 3), true);
    env_define(eval->global, "大文字",
               value_builtin(builtin_upper, "大文字", 1, 1), true);
    env_define(eval->global, "小文字",
               value_builtin(builtin_lower, "小文字", 1, 1), true);
    env_define(eval->global, "空白除去",
               value_builtin(builtin_trim, "空白除去", 1, 1), true);
    
    // 配列関数
    env_define(eval->global, "ソート",
               value_builtin(builtin_sort, "ソート", 1, 1), true);
    env_define(eval->global, "逆順",
               value_builtin(builtin_reverse, "逆順", 1, 1), true);
    env_define(eval->global, "スライス",
               value_builtin(builtin_slice, "スライス", 2, 3), true);
    env_define(eval->global, "位置",
               value_builtin(builtin_index_of, "位置", 2, 2), true);
    env_define(eval->global, "存在",
               value_builtin(builtin_contains, "存在", 2, 2), true);
    
    // ファイル関数
    env_define(eval->global, "読み込む",
               value_builtin(builtin_file_read, "読み込む", 1, 1), true);
    env_define(eval->global, "書き込む",
               value_builtin(builtin_file_write, "書き込む", 2, 2), true);
    env_define(eval->global, "ファイル存在",
               value_builtin(builtin_file_exists, "ファイル存在", 1, 1), true);
    
    // 日時関数
    env_define(eval->global, "現在時刻",
               value_builtin(builtin_now, "現在時刻", 0, 0), true);
    env_define(eval->global, "日付",
               value_builtin(builtin_date, "日付", 0, 1), true);
    env_define(eval->global, "時間",
               value_builtin(builtin_time, "時間", 0, 1), true);
    
    // JSON関数
    env_define(eval->global, "JSON化",
               value_builtin(builtin_json_encode, "JSON化", 1, 1), true);
    env_define(eval->global, "JSON解析",
               value_builtin(builtin_json_decode, "JSON解析", 1, 1), true);
    
    // HTTPクライアント関数
    env_define(eval->global, "HTTP取得",
               value_builtin(builtin_http_get, "HTTP取得", 1, 2), true);
    env_define(eval->global, "HTTP送信",
               value_builtin(builtin_http_post, "HTTP送信", 1, 3), true);
    env_define(eval->global, "HTTP更新",
               value_builtin(builtin_http_put, "HTTP更新", 1, 3), true);
    env_define(eval->global, "HTTP削除",
               value_builtin(builtin_http_delete, "HTTP削除", 1, 2), true);
    env_define(eval->global, "HTTPリクエスト",
               value_builtin(builtin_http_request, "HTTPリクエスト", 2, 4), true);
    
    // HTTPサーバー/Webhook関数
    env_define(eval->global, "サーバー起動",
               value_builtin(builtin_http_serve, "サーバー起動", 1, 2), true);
    env_define(eval->global, "サーバー停止",
               value_builtin(builtin_http_stop, "サーバー停止", 0, 0), true);
    
    // URLエンコード/デコード
    env_define(eval->global, "URLエンコード",
               value_builtin(builtin_url_encode, "URLエンコード", 1, 1), true);
    env_define(eval->global, "URLデコード",
               value_builtin(builtin_url_decode, "URLデコード", 1, 1), true);
    
    // 高階配列関数
    env_define(eval->global, "変換",
               value_builtin(builtin_map, "変換", 2, 2), true);
    env_define(eval->global, "抽出",
               value_builtin(builtin_filter, "抽出", 2, 2), true);
    env_define(eval->global, "集約",
               value_builtin(builtin_reduce, "集約", 3, 3), true);
    env_define(eval->global, "反復",
               value_builtin(builtin_foreach, "反復", 2, 2), true);
    
    // 正規表現関数
    env_define(eval->global, "正規一致",
               value_builtin(builtin_regex_match, "正規一致", 2, 2), true);
    env_define(eval->global, "正規検索",
               value_builtin(builtin_regex_search, "正規検索", 2, 2), true);
    env_define(eval->global, "正規置換",
               value_builtin(builtin_regex_replace, "正規置換", 3, 3), true);
    
    // システムユーティリティ
    env_define(eval->global, "待つ",
               value_builtin(builtin_sleep, "待つ", 1, 1), true);
    env_define(eval->global, "実行",
               value_builtin(builtin_exec, "実行", 1, 1), true);
    env_define(eval->global, "環境変数",
               value_builtin(builtin_env_get, "環境変数", 1, 1), true);
    env_define(eval->global, "環境変数設定",
               value_builtin(builtin_env_set, "環境変数設定", 2, 2), true);
    env_define(eval->global, "終了",
               value_builtin(builtin_exit_program, "終了", 0, 1), true);
    
    // 非同期処理
    env_define(eval->global, "非同期実行",
               value_builtin(builtin_async_run, "非同期実行", 1, -1), true);
    env_define(eval->global, "待機",
               value_builtin(builtin_async_await, "待機", 1, 1), true);
    env_define(eval->global, "全待機",
               value_builtin(builtin_async_await_all, "全待機", 1, 1), true);
    env_define(eval->global, "タスク状態",
               value_builtin(builtin_task_status, "タスク状態", 1, 1), true);
    
    // 並列処理
    env_define(eval->global, "並列実行",
               value_builtin(builtin_parallel_run, "並列実行", 1, 1), true);
    env_define(eval->global, "排他作成",
               value_builtin(builtin_mutex_create, "排他作成", 0, 0), true);
    env_define(eval->global, "排他実行",
               value_builtin(builtin_mutex_exec, "排他実行", 2, 2), true);
    
    // チャネル（スレッド間通信）
    env_define(eval->global, "チャネル作成",
               value_builtin(builtin_channel_create, "チャネル作成", 0, 1), true);
    env_define(eval->global, "チャネル送信",
               value_builtin(builtin_channel_send, "チャネル送信", 2, 2), true);
    env_define(eval->global, "チャネル受信",
               value_builtin(builtin_channel_receive, "チャネル受信", 1, 1), true);
    env_define(eval->global, "チャネル閉じる",
               value_builtin(builtin_channel_close, "チャネル閉じる", 1, 1), true);
    
    // スケジューラ
    env_define(eval->global, "定期実行",
               value_builtin(builtin_schedule_interval, "定期実行", 2, 2), true);
    env_define(eval->global, "遅延実行",
               value_builtin(builtin_schedule_delay, "遅延実行", 2, 2), true);
    env_define(eval->global, "スケジュール停止",
               value_builtin(builtin_schedule_stop, "スケジュール停止", 1, 1), true);
    env_define(eval->global, "全スケジュール停止",
               value_builtin(builtin_schedule_stop_all, "全スケジュール停止", 0, 0), true);
    
    // WebSocket
    env_define(eval->global, "WS接続",
               value_builtin(builtin_ws_connect, "WS接続", 1, 1), true);
    env_define(eval->global, "WS送信",
               value_builtin(builtin_ws_send, "WS送信", 2, 2), true);
    env_define(eval->global, "WS受信",
               value_builtin(builtin_ws_receive, "WS受信", 1, 2), true);
    env_define(eval->global, "WS切断",
               value_builtin(builtin_ws_close, "WS切断", 1, 1), true);
    env_define(eval->global, "WS状態",
               value_builtin(builtin_ws_status, "WS状態", 1, 1), true);
    
    // ジェネレータ関数
    env_define(eval->global, "次",
               value_builtin(builtin_generator_next, "次", 1, 1), true);
    env_define(eval->global, "完了",
               value_builtin(builtin_generator_done, "完了", 1, 1), true);
    env_define(eval->global, "全値",
               value_builtin(builtin_generator_collect, "全値", 1, 1), true);
    
    // パス操作関数
    env_define(eval->global, "パス結合",
               value_builtin(builtin_path_join, "パス結合", 2, 2), true);
    env_define(eval->global, "ファイル名",
               value_builtin(builtin_basename, "ファイル名", 1, 1), true);
    env_define(eval->global, "ディレクトリ名",
               value_builtin(builtin_dirname, "ディレクトリ名", 1, 1), true);
    env_define(eval->global, "拡張子",
               value_builtin(builtin_extension, "拡張子", 1, 1), true);
    
    // Base64関数
    env_define(eval->global, "Base64エンコード",
               value_builtin(builtin_base64_encode, "Base64エンコード", 1, 1), true);
    env_define(eval->global, "Base64デコード",
               value_builtin(builtin_base64_decode, "Base64デコード", 1, 1), true);
    
    // 非同期ランタイムの初期化
    async_runtime_init();
}

// =============================================================================
// エラー処理
// =============================================================================

void runtime_error(Evaluator *eval, int line, const char *format, ...) {
    eval->had_error = true;
    eval->error_line = line;
    
    va_list args;
    va_start(args, format);
    
    char message[256];
    vsnprintf(message, sizeof(message), format, args);
    
    va_end(args);
    
    snprintf(eval->error_message, sizeof(eval->error_message),
             "[%d行目] 実行時エラー: %s", line, message);
    
    fprintf(stderr, "%s\n", eval->error_message);
    
    // スタックトレースを出力
    if (eval->call_stack_depth > 0) {
        fprintf(stderr, "スタックトレース:\n");
        for (int i = eval->call_stack_depth - 1; i >= 0; i--) {
            fprintf(stderr, "  %s() (%d行目)\n", 
                    eval->call_stack[i].func_name,
                    eval->call_stack[i].line);
        }
    }
}

bool evaluator_had_error(Evaluator *eval) {
    return eval->had_error;
}

const char *evaluator_error_message(Evaluator *eval) {
    return eval->error_message;
}

void evaluator_clear_error(Evaluator *eval) {
    eval->had_error = false;
    eval->error_message[0] = '\0';
}

void evaluator_set_debug_mode(Evaluator *eval, bool enabled) {
    eval->debug_mode = enabled;
    eval->step_mode = enabled;  // デバッグモードではステップモードも有効
}

// デバッグ: 行トレース
static void debug_trace(Evaluator *eval, ASTNode *node) {
    if (!eval->debug_mode) return;
    if (node == NULL) return;
    
    int line = node->location.line;
    
    // 同じ行は2回表示しない
    if (line == eval->last_line) return;
    eval->last_line = line;
    
    printf("[デバッグ] 行 %d: %s\n", line, node_type_name(node->type));
    
    if (eval->step_mode) {
        printf("  続行するにはEnterを押してください（'v'で変数表示, 'c'で継続実行）> ");
        fflush(stdout);
        
        char input[64];
        if (fgets(input, sizeof(input), stdin) != NULL) {
            if (input[0] == 'v' || input[0] == 'V') {
                // 現在のスコープの変数を表示
                printf("  [変数一覧]\n");
                env_print(eval->current);
            } else if (input[0] == 'c' || input[0] == 'C') {
                eval->step_mode = false;  // 継続実行
            }
        }
    }
}

// =============================================================================
// 評価関数
// =============================================================================

Value evaluator_run(Evaluator *eval, ASTNode *program) {
    if (program == NULL || program->type != NODE_PROGRAM) {
        return value_null();
    }
    
    g_eval = eval;
    g_eval_for_async = eval;
    
    Value result = value_null();
    
    // トップレベルの宣言を処理
    for (int i = 0; i < program->block.count; i++) {
        result = evaluate(eval, program->block.statements[i]);
        if (eval->had_error) break;
    }
    
    // メイン関数があれば実行
    Value *main_func = env_get(eval->global, "メイン");
    if (main_func != NULL && main_func->type == VALUE_FUNCTION) {
        // 新しいスコープを作成
        Environment *local = env_new(main_func->function.closure);
        Environment *prev = eval->current;
        eval->current = local;
        
        // メイン関数の本体を実行
        result = evaluate(eval, main_func->function.definition->function.body);
        
        if (eval->returning) {
            result = eval->return_value;
            eval->returning = false;
        }
        
        eval->current = prev;
        env_free(local);
    }
    
    return result;
}

Value evaluate(Evaluator *eval, ASTNode *node) {
    if (node == NULL) return value_null();
    if (eval->had_error) return value_null();
    
    // 制御フローのチェック
    if (eval->returning || eval->breaking || eval->continuing) {
        return value_null();
    }
    
    // デバッグトレース（文レベルのノードのみ）
    if (node->type == NODE_VAR_DECL || node->type == NODE_ASSIGN ||
        node->type == NODE_IF || node->type == NODE_WHILE ||
        node->type == NODE_FOR || node->type == NODE_RETURN ||
        node->type == NODE_EXPR_STMT || node->type == NODE_TRY ||
        node->type == NODE_THROW || node->type == NODE_FUNCTION_DEF) {
        debug_trace(eval, node);
    }
    
    // 再帰深度チェック
    eval->recursion_depth++;
    if (eval->recursion_depth > MAX_RECURSION_DEPTH) {
        runtime_error(eval, node->location.line, "スタックオーバーフロー");
        eval->recursion_depth--;
        return value_null();
    }
    
    Value result = value_null();
    
    switch (node->type) {
        case NODE_NUMBER:
            result = value_number(node->number_value);
            break;
            
        case NODE_STRING:
            result = evaluate_string_interpolation(eval, node->string_value, node->location.line);
            break;
            
        case NODE_BOOL:
            result = value_bool(node->bool_value);
            break;
            
        case NODE_NULL:
            result = value_null();
            break;
            
        case NODE_IDENTIFIER: {
            Value *val = env_get(eval->current, node->string_value);
            if (val == NULL) {
                runtime_error(eval, node->location.line,
                             "未定義の変数: %s", node->string_value);
            } else {
                result = *val;
            }
            break;
        }
        
        case NODE_ARRAY: {
            result = value_array_with_capacity(node->block.count);
            for (int i = 0; i < node->block.count; i++) {
                Value elem = evaluate(eval, node->block.statements[i]);
                if (eval->had_error) break;
                array_push(&result, elem);
            }
            break;
        }
        
        case NODE_DICT: {
            result = value_dict_with_capacity(node->dict.count);
            for (int i = 0; i < node->dict.count; i++) {
                Value val = evaluate(eval, node->dict.values[i]);
                if (eval->had_error) break;
                dict_set(&result, node->dict.keys[i], val);
            }
            break;
        }
        
        case NODE_BINARY:
            result = evaluate_binary(eval, node);
            break;
            
        case NODE_UNARY:
            result = evaluate_unary(eval, node);
            break;
            
        case NODE_CALL:
            result = evaluate_call(eval, node);
            break;
            
        case NODE_INDEX:
            result = evaluate_index(eval, node);
            break;
            
        case NODE_FUNCTION_DEF:
            result = evaluate_function_def(eval, node);
            break;
            
        case NODE_VAR_DECL:
            result = evaluate_var_decl(eval, node);
            break;
            
        case NODE_ASSIGN:
            result = evaluate_assign(eval, node);
            break;
            
        case NODE_IF:
            result = evaluate_if(eval, node);
            break;
            
        case NODE_WHILE:
            result = evaluate_while(eval, node);
            break;
            
        case NODE_FOR:
            result = evaluate_for(eval, node);
            break;
            
        case NODE_RETURN:
            if (node->return_stmt.value != NULL) {
                eval->return_value = value_copy(evaluate(eval, node->return_stmt.value));
            } else {
                eval->return_value = value_null();
            }
            eval->returning = true;
            break;
            
        case NODE_BREAK:
            eval->breaking = true;
            break;
            
        case NODE_CONTINUE:
            eval->continuing = true;
            break;
        
        case NODE_YIELD:
            if (eval->in_generator && eval->generator_target != NULL) {
                Value yield_val = evaluate(eval, node->yield_stmt.value);
                if (!eval->had_error) {
                    generator_add_value(eval->generator_target, yield_val);
                }
            } else {
                runtime_error(eval, node->location.line,
                             "'譲渡' は生成関数内でのみ使用できます");
            }
            break;
        
        case NODE_IMPORT:
            result = evaluate_import(eval, node);
            break;
        
        case NODE_CLASS_DEF:
            result = evaluate_class_def(eval, node);
            break;
        
        case NODE_TRY:
            result = evaluate_try(eval, node);
            break;
        
        case NODE_THROW:
            result = evaluate_throw(eval, node);
            break;
        
        case NODE_LAMBDA:
            result = evaluate_lambda(eval, node);
            break;
        
        case NODE_SWITCH:
            result = evaluate_switch(eval, node);
            break;
        
        case NODE_FOREACH:
            result = evaluate_foreach(eval, node);
            break;
        
        case NODE_NEW:
            result = evaluate_new(eval, node);
            break;
        
        case NODE_MEMBER:
            result = evaluate_member(eval, node);
            break;
        
        case NODE_SELF:
            if (eval->current_instance == NULL) {
                runtime_error(eval, node->location.line,
                             "'自分' はメソッド内でのみ使用できます");
                result = value_null();
            } else {
                result = value_copy(*eval->current_instance);
            }
            break;
            
        case NODE_EXPR_STMT:
            result = evaluate(eval, node->expr_stmt.expression);
            break;
            
        case NODE_BLOCK:
        case NODE_PROGRAM:
            for (int i = 0; i < node->block.count; i++) {
                result = evaluate(eval, node->block.statements[i]);
                if (eval->had_error || eval->returning || 
                    eval->breaking || eval->continuing || eval->throwing) {
                    break;
                }
            }
            break;
            
        default:
            runtime_error(eval, node->location.line,
                         "未実装のノードタイプ: %s", node_type_name(node->type));
            break;
    }
    
    eval->recursion_depth--;
    return result;
}

// =============================================================================
// 各種評価関数
// =============================================================================

static Value evaluate_binary(Evaluator *eval, ASTNode *node) {
    // 短絡評価
    if (node->binary.operator == TOKEN_AND) {
        Value left = evaluate(eval, node->binary.left);
        if (eval->had_error) return value_null();
        if (!value_is_truthy(left)) return value_bool(false);
        Value right = evaluate(eval, node->binary.right);
        return value_bool(value_is_truthy(right));
    }
    
    if (node->binary.operator == TOKEN_OR) {
        Value left = evaluate(eval, node->binary.left);
        if (eval->had_error) return value_null();
        if (value_is_truthy(left)) return left;
        return evaluate(eval, node->binary.right);
    }
    
    // null合体演算子: 左辺がnullなら右辺を返す
    if (node->binary.operator == TOKEN_NULL_COALESCE) {
        Value left = evaluate(eval, node->binary.left);
        if (eval->had_error) return value_null();
        if (left.type != VALUE_NULL) return left;
        return evaluate(eval, node->binary.right);
    }
    
    Value left = evaluate(eval, node->binary.left);
    if (eval->had_error) return value_null();
    
    Value right = evaluate(eval, node->binary.right);
    if (eval->had_error) return value_null();
    
    // 数値演算
    if (left.type == VALUE_NUMBER && right.type == VALUE_NUMBER) {
        double l = left.number;
        double r = right.number;
        
        switch (node->binary.operator) {
            case TOKEN_PLUS:    return value_number(l + r);
            case TOKEN_MINUS:   return value_number(l - r);
            case TOKEN_STAR:    return value_number(l * r);
            case TOKEN_SLASH:
                if (r == 0) {
                    runtime_error(eval, node->location.line, "ゼロ除算");
                    return value_null();
                }
                return value_number(l / r);
            case TOKEN_PERCENT:
                if (r == 0) {
                    runtime_error(eval, node->location.line, "ゼロ除算");
                    return value_null();
                }
                return value_number(fmod(l, r));
            case TOKEN_POWER:   return value_number(pow(l, r));
            case TOKEN_EQ:      return value_bool(l == r);
            case TOKEN_NE:      return value_bool(l != r);
            case TOKEN_LT:      return value_bool(l < r);
            case TOKEN_LE:      return value_bool(l <= r);
            case TOKEN_GT:      return value_bool(l > r);
            case TOKEN_GE:      return value_bool(l >= r);
            default: break;
        }
    }
    
    // 文字列結合
    if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
        if (node->binary.operator == TOKEN_PLUS) {
            return string_concat(left, right);
        }
        if (node->binary.operator == TOKEN_EQ) {
            return value_bool(value_equals(left, right));
        }
        if (node->binary.operator == TOKEN_NE) {
            return value_bool(!value_equals(left, right));
        }
    }
    
    // 文字列と数値の結合
    if (node->binary.operator == TOKEN_PLUS) {
        if (left.type == VALUE_STRING || right.type == VALUE_STRING) {
            char *left_str = value_to_string(left);
            char *right_str = value_to_string(right);
            
            size_t len = strlen(left_str) + strlen(right_str) + 1;
            char *result = malloc(len);
            snprintf(result, len, "%s%s", left_str, right_str);
            
            free(left_str);
            free(right_str);
            
            Value v = value_string(result);
            free(result);
            return v;
        }
    }
    
    // 等価比較
    if (node->binary.operator == TOKEN_EQ) {
        return value_bool(value_equals(left, right));
    }
    if (node->binary.operator == TOKEN_NE) {
        return value_bool(!value_equals(left, right));
    }
    
    runtime_error(eval, node->location.line,
                 "不正な演算: %s %s %s",
                 value_type_name(left.type),
                 token_type_name(node->binary.operator),
                 value_type_name(right.type));
    return value_null();
}

static Value evaluate_unary(Evaluator *eval, ASTNode *node) {
    Value operand = evaluate(eval, node->unary.operand);
    if (eval->had_error) return value_null();
    
    switch (node->unary.operator) {
        case TOKEN_MINUS:
            if (operand.type == VALUE_NUMBER) {
                return value_number(-operand.number);
            }
            runtime_error(eval, node->location.line,
                         "数値以外に単項マイナスは使えません");
            return value_null();
            
        case TOKEN_NOT:
            return value_bool(!value_is_truthy(operand));
            
        default:
            runtime_error(eval, node->location.line,
                         "未知の単項演算子");
            return value_null();
    }
}

static Value evaluate_call(Evaluator *eval, ASTNode *node) {
    // メソッド呼び出しの場合、インスタンスを保存
    Value instance = value_null();
    bool is_method_call = false;
    bool is_super_call = false;
    
    if (node->call.callee->type == NODE_MEMBER) {
        // 親クラスのメソッド呼び出しかチェック
        if (node->call.callee->member.object->type == NODE_IDENTIFIER &&
            strcmp(node->call.callee->member.object->string_value, "親") == 0) {
            is_super_call = true;
            // superの場合、instanceは現在のインスタンス
            if (eval->current_instance != NULL) {
                instance = value_copy(*eval->current_instance);
                is_method_call = true;
            }
        } else {
            instance = evaluate(eval, node->call.callee->member.object);
            if (eval->had_error) return value_null();
            if (instance.type == VALUE_INSTANCE) {
                is_method_call = true;
            }
        }
    }
    
    Value callee = evaluate(eval, node->call.callee);
    if (eval->had_error) return value_null();
    
    // 組み込み関数で配列を変更するもの（追加、削除）は特別に処理
    if (callee.type == VALUE_BUILTIN && 
        (strcmp(callee.builtin.name, "追加") == 0 || 
         strcmp(callee.builtin.name, "削除") == 0)) {
        
        if (node->call.arg_count >= 1 && 
            node->call.arguments[0]->type == NODE_IDENTIFIER) {
            
            const char *arr_name = node->call.arguments[0]->string_value;
            Value *array_ptr = env_get(eval->current, arr_name);
            
            if (array_ptr == NULL || array_ptr->type != VALUE_ARRAY) {
                runtime_error(eval, node->location.line,
                             "%sは配列ではありません", arr_name);
                return value_null();
            }
            
            if (strcmp(callee.builtin.name, "追加") == 0 && 
                node->call.arg_count == 2) {
                Value element = evaluate(eval, node->call.arguments[1]);
                if (eval->had_error) return value_null();
                array_push(array_ptr, element);
                return value_null();
            }
            else if (strcmp(callee.builtin.name, "削除") == 0 && 
                     node->call.arg_count == 2) {
                Value index = evaluate(eval, node->call.arguments[1]);
                if (eval->had_error) return value_null();
                if (index.type != VALUE_NUMBER) {
                    runtime_error(eval, node->location.line,
                                 "インデックスは数値でなければなりません");
                    return value_null();
                }
                int idx = (int)index.number;
                if (idx < 0 || idx >= array_ptr->array.length) {
                    runtime_error(eval, node->location.line,
                                 "インデックスが範囲外です");
                    return value_null();
                }
                Value removed = array_ptr->array.elements[idx];
                for (int i = idx; i < array_ptr->array.length - 1; i++) {
                    array_ptr->array.elements[i] = array_ptr->array.elements[i + 1];
                }
                array_ptr->array.length--;
                return removed;
            }
        }
    }
    
    // 引数を評価（スプレッド演算子対応）
    Value *args = NULL;
    int actual_arg_count = 0;
    int args_capacity = node->call.arg_count > 0 ? node->call.arg_count * 2 : 4;
    if (node->call.arg_count > 0) {
        args = malloc(sizeof(Value) * args_capacity);
        for (int i = 0; i < node->call.arg_count; i++) {
            ASTNode *arg_node = node->call.arguments[i];
            // スプレッド演算子: ...配列 → 配列の各要素を展開
            if (arg_node->type == NODE_UNARY && arg_node->unary.operator == TOKEN_SPREAD) {
                Value spread_val = evaluate(eval, arg_node->unary.operand);
                if (eval->had_error) {
                    free(args);
                    return value_null();
                }
                if (spread_val.type != VALUE_ARRAY) {
                    runtime_error(eval, arg_node->location.line, "スプレッド演算子は配列にのみ使用できます");
                    free(args);
                    return value_null();
                }
                // 配列の各要素を引数に追加
                for (int j = 0; j < spread_val.array.length; j++) {
                    if (actual_arg_count >= args_capacity) {
                        args_capacity *= 2;
                        args = realloc(args, sizeof(Value) * args_capacity);
                    }
                    args[actual_arg_count++] = value_copy(spread_val.array.elements[j]);
                }
            } else {
                if (actual_arg_count >= args_capacity) {
                    args_capacity *= 2;
                    args = realloc(args, sizeof(Value) * args_capacity);
                }
                args[actual_arg_count] = evaluate(eval, arg_node);
                if (eval->had_error) {
                    free(args);
                    return value_null();
                }
                actual_arg_count++;
            }
        }
    }
    // スプレッド展開後の実際の引数数を使う
    int original_arg_count = node->call.arg_count;
    // 一時的にarg_countを実引数数に更新（後で戻す）
    // NOTE: node自体を変更せず、ローカル変数で管理
    int effective_arg_count = actual_arg_count;
    
    Value result = value_null();
    
    // 組み込み関数
    if (callee.type == VALUE_BUILTIN) {
        // 引数の数をチェック
        int min = callee.builtin.min_args;
        int max = callee.builtin.max_args;
        
        if (effective_arg_count < min) {
            runtime_error(eval, node->location.line,
                         "%sには少なくとも%d個の引数が必要です",
                         callee.builtin.name, min);
        } else if (max >= 0 && effective_arg_count > max) {
            runtime_error(eval, node->location.line,
                         "%sの引数は最大%d個です",
                         callee.builtin.name, max);
        } else {
            result = callee.builtin.fn(effective_arg_count, args);
        }
    }
    // ユーザー定義関数
    else if (callee.type == VALUE_FUNCTION) {
        ASTNode *def = callee.function.definition;
        
        // ラムダと通常関数の両方に対応
        int expected_count;
        Parameter *params;
        ASTNode *body;
        const char *func_name;
        
        if (def->type == NODE_LAMBDA) {
            expected_count = def->lambda.param_count;
            params = def->lambda.params;
            body = def->lambda.body;
            func_name = "無名関数";
        } else {
            expected_count = def->function.param_count;
            params = def->function.params;
            body = def->function.body;
            func_name = def->function.name;
        }
        
        // 必須引数の数をカウント（デフォルト値なしのパラメータ）
        int min_required = 0;
        bool has_variadic = false;
        for (int i = 0; i < expected_count; i++) {
            if (params[i].is_variadic) {
                has_variadic = true;
            } else if (params[i].default_value == NULL) {
                min_required++;
            }
        }
        
        // 引数の数をチェック（デフォルト引数・可変長引数対応）
        int max_allowed = has_variadic ? 999 : expected_count;
        if (effective_arg_count < min_required || 
            (!has_variadic && effective_arg_count > expected_count)) {
            if (min_required == expected_count) {
                runtime_error(eval, node->location.line,
                             "%sには%d個の引数が必要です（%d個渡されました）",
                             func_name,
                             expected_count,
                             effective_arg_count);
            } else {
                runtime_error(eval, node->location.line,
                             "%sには%d〜%d個の引数が必要です（%d個渡されました）",
                             func_name,
                             min_required,
                             expected_count,
                             effective_arg_count);
            }
        } else {
            // 新しいスコープを作成
            Environment *local = env_new(callee.function.closure);
            
            // 引数をバインド（渡された引数 + デフォルト値 + 可変長）
            for (int i = 0; i < expected_count; i++) {
                if (params[i].is_variadic) {
                    // 可変長引数: 残りの引数を配列に収集
                    Value rest = value_array();
                    for (int j = i; j < effective_arg_count; j++) {
                        array_push(&rest, value_copy(args[j]));
                    }
                    env_define(local, params[i].name, rest, false);
                } else if (i < effective_arg_count) {
                    // 渡された引数を使用
                    env_define(local, params[i].name, value_copy(args[i]), false);
                } else if (params[i].default_value != NULL) {
                    // デフォルト値を評価して使用
                    Value def_val = evaluate(eval, params[i].default_value);
                    env_define(local, params[i].name, value_copy(def_val), false);
                }
            }
            
            // 関数本体を実行
            Environment *prev = eval->current;
            eval->current = local;
            
            // コールスタックにプッシュ
            if (eval->call_stack_depth < 128) {
                eval->call_stack[eval->call_stack_depth].func_name = func_name;
                eval->call_stack[eval->call_stack_depth].line = node->location.line;
                eval->call_stack_depth++;
            }
            
            // メソッド呼び出しの場合、current_instanceを設定
            Value *prev_instance = eval->current_instance;
            Value *instance_ptr = NULL;
            if (is_method_call) {
                if (is_super_call && eval->current_instance != NULL) {
                    // superの場合は現在のインスタンスをそのまま使う（コピーしない）
                    instance_ptr = eval->current_instance;
                } else {
                    instance_ptr = malloc(sizeof(Value));
                    *instance_ptr = instance;
                }
                eval->current_instance = instance_ptr;
            }
            
            // ジェネレータ関数かどうかをチェック
            bool func_is_generator = (callee.function.definition != NULL &&
                                       callee.function.definition->type == NODE_FUNCTION_DEF &&
                                       callee.function.definition->function.is_generator);
            
            if (func_is_generator) {
                // ジェネレータモードで実行：yield値を収集
                bool prev_in_generator = eval->in_generator;
                Value *prev_gen_target = eval->generator_target;
                
                Value gen = value_generator();
                eval->in_generator = true;
                eval->generator_target = &gen;
                
                evaluate(eval, body);
                
                eval->in_generator = prev_in_generator;
                eval->generator_target = prev_gen_target;
                
                // returning状態をクリア（ジェネレータのreturnは無視）
                eval->returning = false;
                
                result = gen;
            } else {
                evaluate(eval, body);
            }
            
            // コールスタックからポップ
            if (eval->call_stack_depth > 0) {
                eval->call_stack_depth--;
            }
            
            // current_instanceを復元
            eval->current_instance = prev_instance;
            if (instance_ptr != NULL && !is_super_call) {
                free(instance_ptr);
            }
            
            if (eval->returning) {
                result = eval->return_value;
                eval->returning = false;
            }
            
            eval->current = prev;
            env_free(local);
        }
    }
    else {
        runtime_error(eval, node->location.line,
                     "呼び出し可能ではありません");
    }
    
    if (args != NULL) {
        free(args);
    }
    
    return result;
}

static Value evaluate_index(Evaluator *eval, ASTNode *node) {
    Value array = evaluate(eval, node->index.array);
    if (eval->had_error) return value_null();
    
    Value index = evaluate(eval, node->index.index);
    if (eval->had_error) return value_null();
    
    if (array.type == VALUE_ARRAY) {
        if (index.type != VALUE_NUMBER) {
            runtime_error(eval, node->location.line,
                         "配列のインデックスは数値でなければなりません");
            return value_null();
        }
        
        int idx = (int)index.number;
        // 負のインデックス: -1 = 最後, -2 = 最後から2番目...
        if (idx < 0) idx += array.array.length;
        if (idx < 0 || idx >= array.array.length) {
            runtime_error(eval, node->location.line,
                         "インデックスが範囲外です: %d（長さ: %d）",
                         (int)index.number, array.array.length);
            return value_null();
        }
        
        return array.array.elements[idx];
    }
    
    if (array.type == VALUE_STRING) {
        if (index.type != VALUE_NUMBER) {
            runtime_error(eval, node->location.line,
                         "文字列のインデックスは数値でなければなりません");
            return value_null();
        }
        
        int idx = (int)index.number;
        int len = string_length(&array);
        
        // 負のインデックス対応
        if (idx < 0) idx += len;
        if (idx < 0 || idx >= len) {
            runtime_error(eval, node->location.line,
                         "インデックスが範囲外です: %d（長さ: %d）",
                         (int)index.number, len);
            return value_null();
        }
        
        return string_substring(&array, idx, idx + 1);
    }
    
    if (array.type == VALUE_DICT) {
        if (index.type != VALUE_STRING) {
            runtime_error(eval, node->location.line,
                         "辞書のキーは文字列でなければなりません");
            return value_null();
        }
        
        return dict_get(&array, index.string.data);
    }
    
    runtime_error(eval, node->location.line,
                 "インデックスアクセスは配列、文字列、辞書にのみ使用できます");
    return value_null();
}

static Value evaluate_function_def(Evaluator *eval, ASTNode *node) {
    Value func = value_function(node, eval->current);
    env_define(eval->current, node->function.name, func, false);
    return value_null();
}

static Value evaluate_var_decl(Evaluator *eval, ASTNode *node) {
    Value value = evaluate(eval, node->var_decl.initializer);
    if (eval->had_error) return value_null();
    
    // 環境に保存するときはコピーを作成
    Value copy = value_copy(value);
    if (!env_define(eval->current, node->var_decl.name, copy, node->var_decl.is_const)) {
        if (env_is_const(eval->current, node->var_decl.name)) {
            runtime_error(eval, node->location.line,
                         "定数 %s は再定義できません", node->var_decl.name);
        }
        value_free(&copy);  // 失敗した場合はコピーを解放
    }
    
    return value;
}

static Value evaluate_assign(Evaluator *eval, ASTNode *node) {
    Value value = evaluate(eval, node->assign.value);
    if (eval->had_error) return value_null();
    
    // 複合代入演算子の処理
    if (node->assign.operator != TOKEN_ASSIGN) {
        Value current;
        
        if (node->assign.target->type == NODE_IDENTIFIER) {
            Value *ptr = env_get(eval->current, node->assign.target->string_value);
            if (ptr == NULL) {
                runtime_error(eval, node->location.line,
                             "未定義の変数: %s", node->assign.target->string_value);
                return value_null();
            }
            current = *ptr;
        } else if (node->assign.target->type == NODE_INDEX) {
            current = evaluate_index(eval, node->assign.target);
            if (eval->had_error) return value_null();
        } else {
            runtime_error(eval, node->location.line, "不正な代入先");
            return value_null();
        }
        
        if (current.type != VALUE_NUMBER || value.type != VALUE_NUMBER) {
            runtime_error(eval, node->location.line,
                         "複合代入は数値にのみ使用できます");
            return value_null();
        }
        
        switch (node->assign.operator) {
            case TOKEN_PLUS_ASSIGN:
                value = value_number(current.number + value.number);
                break;
            case TOKEN_MINUS_ASSIGN:
                value = value_number(current.number - value.number);
                break;
            case TOKEN_STAR_ASSIGN:
                value = value_number(current.number * value.number);
                break;
            case TOKEN_SLASH_ASSIGN:
                if (value.number == 0) {
                    runtime_error(eval, node->location.line, "ゼロ除算");
                    return value_null();
                }
                value = value_number(current.number / value.number);
                break;
            case TOKEN_PERCENT_ASSIGN:
                if (value.number == 0) {
                    runtime_error(eval, node->location.line, "ゼロ除算");
                    return value_null();
                }
                value = value_number(fmod(current.number, value.number));
                break;
            case TOKEN_POWER_ASSIGN:
                value = value_number(pow(current.number, value.number));
                break;
            default:
                break;
        }
    }
    
    // 代入先の処理
    if (node->assign.target->type == NODE_IDENTIFIER) {
        const char *name = node->assign.target->string_value;
        
        if (env_is_const(eval->current, name)) {
            runtime_error(eval, node->location.line,
                         "定数 %s には代入できません", name);
            return value_null();
        }
        
        // 環境に保存するときはコピーを作成
        Value copy = value_copy(value);
        if (!env_set(eval->current, name, copy)) {
            // 変数が存在しない場合は新規定義
            env_define(eval->current, name, copy, false);
        }
    }
    else if (node->assign.target->type == NODE_INDEX) {
        ASTNode *idx_node = node->assign.target;
        Value *target_ptr = NULL;
        
        if (idx_node->index.array->type == NODE_IDENTIFIER) {
            target_ptr = env_get(eval->current, idx_node->index.array->string_value);
        }
        
        if (target_ptr == NULL) {
            runtime_error(eval, node->location.line, "配列または辞書が見つかりません");
            return value_null();
        }
        
        Value index = evaluate(eval, idx_node->index.index);
        if (eval->had_error) return value_null();
        
        if (target_ptr->type == VALUE_ARRAY) {
            // 配列の場合
            if (index.type != VALUE_NUMBER) {
                runtime_error(eval, node->location.line,
                             "配列のインデックスは数値でなければなりません");
                return value_null();
            }
            
            int idx = (int)index.number;
            if (!array_set(target_ptr, idx, value)) {
                runtime_error(eval, node->location.line,
                             "インデックスが範囲外です: %d", idx);
                return value_null();
            }
        }
        else if (target_ptr->type == VALUE_DICT) {
            // 辞書の場合
            if (index.type != VALUE_STRING) {
                runtime_error(eval, node->location.line,
                             "辞書のキーは文字列でなければなりません");
                return value_null();
            }
            
            dict_set(target_ptr, index.string.data, value);
        }
        else {
            runtime_error(eval, node->location.line, "配列または辞書が見つかりません");
            return value_null();
        }
    }
    else if (node->assign.target->type == NODE_MEMBER) {
        // メンバーへの代入（インスタンスフィールド）
        ASTNode *member_node = node->assign.target;
        Value object = evaluate(eval, member_node->member.object);
        if (eval->had_error) return value_null();
        
        if (object.type == VALUE_INSTANCE) {
            instance_set_field(&object, member_node->member.member_name, value);
            // 元の変数を更新する必要がある
            if (member_node->member.object->type == NODE_IDENTIFIER) {
                const char *var_name = member_node->member.object->string_value;
                env_set(eval->current, var_name, object);
            } else if (member_node->member.object->type == NODE_SELF) {
                // 自分の場合、current_instanceを更新
                if (eval->current_instance != NULL) {
                    instance_set_field(eval->current_instance, member_node->member.member_name, value);
                }
            }
        } else {
            runtime_error(eval, node->location.line, "メンバー代入はインスタンスにのみ使用できます");
            return value_null();
        }
    }
    else {
        runtime_error(eval, node->location.line, "不正な代入先");
        return value_null();
    }
    
    return value;
}

static Value evaluate_if(Evaluator *eval, ASTNode *node) {
    Value condition = evaluate(eval, node->if_stmt.condition);
    if (eval->had_error) return value_null();
    
    if (value_is_truthy(condition)) {
        return evaluate(eval, node->if_stmt.then_branch);
    } else if (node->if_stmt.else_branch != NULL) {
        return evaluate(eval, node->if_stmt.else_branch);
    }
    
    return value_null();
}

static Value evaluate_while(Evaluator *eval, ASTNode *node) {
    Value result = value_null();
    
    while (true) {
        Value condition = evaluate(eval, node->while_stmt.condition);
        if (eval->had_error) return value_null();
        
        if (!value_is_truthy(condition)) break;
        
        result = evaluate(eval, node->while_stmt.body);
        
        if (eval->returning) break;
        
        if (eval->breaking) {
            eval->breaking = false;
            break;
        }
        
        if (eval->continuing) {
            eval->continuing = false;
            continue;
        }
    }
    
    return result;
}

static Value evaluate_for(Evaluator *eval, ASTNode *node) {
    Value start = evaluate(eval, node->for_stmt.start);
    if (eval->had_error) return value_null();
    
    Value end = evaluate(eval, node->for_stmt.end);
    if (eval->had_error) return value_null();
    
    if (start.type != VALUE_NUMBER || end.type != VALUE_NUMBER) {
        runtime_error(eval, node->location.line,
                     "繰り返しの範囲は数値でなければなりません");
        return value_null();
    }
    
    Value result = value_null();
    double step = (start.number <= end.number) ? 1.0 : -1.0;
    
    // ステップ値がある場合
    if (node->for_stmt.step != NULL) {
        Value step_val = evaluate(eval, node->for_stmt.step);
        if (eval->had_error) return value_null();
        if (step_val.type != VALUE_NUMBER) {
            runtime_error(eval, node->location.line,
                         "ステップ値は数値でなければなりません");
            return value_null();
        }
        step = step_val.number;
    }
    
    // ループ変数を現在のスコープに定義（コピーを作成）
    env_define(eval->current, node->for_stmt.var_name, value_copy(start), false);
    
    for (double i = start.number; 
         (step > 0) ? (i <= end.number) : (i >= end.number);
         i += step) {
        
        env_set(eval->current, node->for_stmt.var_name, value_number(i));
        
        result = evaluate(eval, node->for_stmt.body);
        
        if (eval->returning) break;
        
        if (eval->breaking) {
            eval->breaking = false;
            break;
        }
        
        if (eval->continuing) {
            eval->continuing = false;
            continue;
        }
    }
    
    return result;
}

static Value evaluate_import(Evaluator *eval, ASTNode *node) {
    const char *module_path = node->import_stmt.module_path;
    
    // 現在のファイルディレクトリを基準にパスを解決
    char resolved_path[1024];
    
    // 絶対パスの場合はそのまま使用
    if (module_path[0] == '/') {
        snprintf(resolved_path, sizeof(resolved_path), "%s", module_path);
    } else {
        // 相対パスの場合、.jp拡張子を追加
        if (strstr(module_path, ".jp") == NULL) {
            snprintf(resolved_path, sizeof(resolved_path), "%s.jp", module_path);
        } else {
            snprintf(resolved_path, sizeof(resolved_path), "%s", module_path);
        }
    }
    
    // ファイルを読み込む
    FILE *file = fopen(resolved_path, "rb");
    if (file == NULL) {
        runtime_error(eval, node->location.line,
                     "モジュール '%s' を読み込めません", resolved_path);
        return value_null();
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *source = malloc(size + 1);
    size_t read = fread(source, 1, size, file);
    source[read] = '\0';
    fclose(file);
    
    // パースと実行
    Parser parser;
    parser_init(&parser, source, resolved_path);
    
    ASTNode *program = parse_program(&parser);
    
    if (parser.had_error) {
        node_free(program);
        free(source);
        runtime_error(eval, node->location.line,
                     "モジュール '%s' のパースに失敗しました", resolved_path);
        return value_null();
    }
    
    // モジュールを保存（ASTと ソースを解放しないように）
    if (eval->imported_count >= eval->imported_capacity) {
        eval->imported_capacity = eval->imported_capacity == 0 ? 4 : eval->imported_capacity * 2;
        eval->imported_modules = realloc(eval->imported_modules, 
                                         eval->imported_capacity * sizeof(ImportedModule));
    }
    eval->imported_modules[eval->imported_count].source = source;
    eval->imported_modules[eval->imported_count].ast = program;
    eval->imported_count++;
    
    // インポートしたモジュールを現在の環境で評価
    Value result = value_null();
    for (int i = 0; i < program->block.count; i++) {
        result = evaluate(eval, program->block.statements[i]);
        if (eval->had_error) break;
    }
    
    // ソースとASTは保持しておく（関数定義で参照されるため）
    return result;
}

static Value evaluate_class_def(Evaluator *eval, ASTNode *node) {
    // 親クラスを取得（あれば）
    Value *parent = NULL;
    if (node->class_def.parent_name != NULL) {
        Value *parent_ptr = env_get(eval->current, node->class_def.parent_name);
        if (parent_ptr == NULL || parent_ptr->type != VALUE_CLASS) {
            runtime_error(eval, node->location.line,
                         "'%s' はクラスではありません", node->class_def.parent_name);
            return value_null();
        }
        // 親クラスを保存
        parent = malloc(sizeof(Value));
        *parent = value_copy(*parent_ptr);
    }
    
    // クラス値を作成
    Value class_val = value_class(node->class_def.name, node, parent);
    
    // グローバル環境に登録
    env_define(eval->current, node->class_def.name, class_val, true);
    
    return class_val;
}

static Value evaluate_new(Evaluator *eval, ASTNode *node) {
    // クラスを取得
    Value *class_ptr = env_get(eval->current, node->new_expr.class_name);
    if (class_ptr == NULL || class_ptr->type != VALUE_CLASS) {
        runtime_error(eval, node->location.line,
                     "'%s' はクラスではありません", node->new_expr.class_name);
        return value_null();
    }
    Value class_val = *class_ptr;
    
    // クラス値をヒープに格納
    Value *class_heap = malloc(sizeof(Value));
    *class_heap = value_copy(class_val);
    
    // インスタンスを作成
    Value instance = value_instance(class_heap);
    
    // 初期化メソッドを呼び出す
    ASTNode *class_def = class_val.class_value.definition;
    if (class_def->class_def.init_method != NULL) {
        ASTNode *init = class_def->class_def.init_method;
        
        // 引数の数をチェック
        if (node->new_expr.arg_count != init->method.param_count) {
            runtime_error(eval, node->location.line,
                         "初期化メソッドは %d 個の引数が必要です（%d 個渡されました）",
                         init->method.param_count, node->new_expr.arg_count);
            return value_null();
        }
        
        // 新しい環境を作成
        Environment *method_env = env_new(eval->current);
        Environment *saved_env = eval->current;
        eval->current = method_env;
        
        // 引数をバインド
        for (int i = 0; i < init->method.param_count; i++) {
            Value arg = evaluate(eval, node->new_expr.arguments[i]);
            if (eval->had_error) {
                eval->current = saved_env;
                env_free(method_env);
                return value_null();
            }
            env_define(method_env, init->method.params[i].name, arg, false);
        }
        
        // インスタンスをヒープに確保して保持
        Value *instance_ptr = malloc(sizeof(Value));
        *instance_ptr = instance;
        
        // 現在のインスタンスを設定
        Value *saved_instance = eval->current_instance;
        eval->current_instance = instance_ptr;
        
        // 初期化メソッドを実行
        evaluate(eval, init->method.body);
        
        // インスタンスを取り戻す
        instance = *eval->current_instance;
        
        // 環境とインスタンスを復元
        eval->current_instance = saved_instance;
        eval->current = saved_env;
        
        eval->returning = false;
        eval->return_value = value_null();
        
        // クリーンアップ
        free(instance_ptr);
        env_free(method_env);
    }
    
    return instance;
}

static Value evaluate_member(Evaluator *eval, ASTNode *node) {
    const char *member_name = node->member.member_name;
    
    // 親クラスのメソッド呼び出し（super）
    // 「親」キーワードが使われた場合は、先にオブジェクトを評価せずに処理
    if (node->member.object->type == NODE_IDENTIFIER && 
        strcmp(node->member.object->string_value, "親") == 0) {
        // 現在のインスタンスから親クラスを探す
        if (eval->current_instance == NULL) {
            runtime_error(eval, node->location.line,
                         "'親' はメソッド内でのみ使用できます");
            return value_null();
        }
        
        Value *instance = eval->current_instance;
        if (instance->type != VALUE_INSTANCE || instance->instance.class_ref == NULL) {
            runtime_error(eval, node->location.line,
                         "インスタンスが無効です");
            return value_null();
        }
        
        // 現在のクラスの親クラスを取得
        Value *class_ref = instance->instance.class_ref;
        if (class_ref->type != VALUE_CLASS || class_ref->class_value.parent == NULL) {
            runtime_error(eval, node->location.line,
                         "親クラスがありません");
            return value_null();
        }
        
        Value *parent_class = class_ref->class_value.parent;
        
        // 親クラスからメソッドを検索
        while (parent_class != NULL && parent_class->type == VALUE_CLASS) {
            ASTNode *parent_def = parent_class->class_value.definition;
            
            // 初期化メソッドをチェック
            if (strcmp(member_name, "初期化") == 0 && parent_def->class_def.init_method != NULL) {
                return value_function(parent_def->class_def.init_method, eval->current);
            }
            
            // 通常のメソッドをチェック
            for (int i = 0; i < parent_def->class_def.method_count; i++) {
                ASTNode *method = parent_def->class_def.methods[i];
                if (strcmp(method->method.name, member_name) == 0) {
                    return value_function(method, eval->current);
                }
            }
            
            // さらに親へ
            if (parent_def->class_def.parent_name != NULL) {
                Value *grandparent = env_get(eval->current, parent_def->class_def.parent_name);
                if (grandparent != NULL && grandparent->type == VALUE_CLASS) {
                    parent_class = grandparent;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
        
        runtime_error(eval, node->location.line,
                     "親クラスに '%s' というメソッドがありません", member_name);
        return value_null();
    }
    
    Value object = evaluate(eval, node->member.object);
    if (eval->had_error) return value_null();
    
    // インスタンスのフィールドアクセス
    if (object.type == VALUE_INSTANCE) {
        Value *field = instance_get_field(&object, member_name);
        if (field != NULL) {
            return value_copy(*field);
        }
        
        // メソッドを探す（親クラスも含む）
        Value *class_ref = object.instance.class_ref;
        while (class_ref != NULL && class_ref->type == VALUE_CLASS) {
            ASTNode *class_def = class_ref->class_value.definition;
            for (int i = 0; i < class_def->class_def.method_count; i++) {
                ASTNode *method = class_def->class_def.methods[i];
                if (strcmp(method->method.name, member_name) == 0) {
                    // バインドされたメソッドを返す（関数として）
                    return value_function(method, eval->current);
                }
            }
            // 親クラスを探す
            if (class_def->class_def.parent_name != NULL) {
                Value *parent = env_get(eval->current, class_def->class_def.parent_name);
                if (parent != NULL && parent->type == VALUE_CLASS) {
                    class_ref = parent;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
        
        runtime_error(eval, node->location.line,
                     "インスタンスに '%s' というフィールドまたはメソッドがありません",
                     member_name);
        return value_null();
    }
    
    // 辞書のメンバーアクセス
    if (object.type == VALUE_DICT) {
        Value val = dict_get(&object, member_name);
        if (val.type != VALUE_NULL) {
            return value_copy(val);
        }
        runtime_error(eval, node->location.line,
                     "辞書に '%s' というキーがありません", member_name);
        return value_null();
    }
    
    // クラスの静的メソッドアクセス
    if (object.type == VALUE_CLASS) {
        ASTNode *class_def = object.class_value.definition;
        for (int i = 0; i < class_def->class_def.static_method_count; i++) {
            ASTNode *method = class_def->class_def.static_methods[i];
            if (strcmp(method->method.name, member_name) == 0) {
                return value_function(method, eval->current);
            }
        }
        runtime_error(eval, node->location.line,
                     "クラス '%s' に静的メソッド '%s' がありません",
                     class_def->class_def.name, member_name);
        return value_null();
    }
    
    runtime_error(eval, node->location.line,
                 "メンバーアクセスはインスタンス、辞書、またはクラスに対してのみ使用できます");
    return value_null();
}

// =============================================================================
// 例外処理の評価
// =============================================================================

static Value evaluate_try(Evaluator *eval, ASTNode *node) {
    Value result = value_null();
    
    // 試行ブロックを実行
    evaluate(eval, node->try_stmt.try_block);
    
    // 例外が発生した場合
    if (eval->throwing && node->try_stmt.catch_block != NULL) {
        // 例外をキャッチ
        eval->throwing = false;
        
        // 新しいスコープを作成して例外変数をバインド
        Environment *catch_scope = env_new(eval->current);
        if (node->try_stmt.catch_var != NULL) {
            env_define(catch_scope, node->try_stmt.catch_var, 
                      value_copy(eval->exception_value), false);
        }
        
        // 捕獲ブロックを実行
        Environment *prev = eval->current;
        eval->current = catch_scope;
        
        evaluate(eval, node->try_stmt.catch_block);
        
        eval->current = prev;
        env_free(catch_scope);
    }
    
    // 最終ブロックがあれば必ず実行
    if (node->try_stmt.finally_block != NULL) {
        // 例外状態を保存
        bool was_throwing = eval->throwing;
        Value saved_exception = eval->exception_value;
        bool was_returning = eval->returning;
        Value saved_return = eval->return_value;
        
        // 例外・戻り値状態を一時的にクリア
        eval->throwing = false;
        eval->returning = false;
        
        evaluate(eval, node->try_stmt.finally_block);
        
        // finally中に新しい例外や戻り値がなければ元に戻す
        if (!eval->throwing && was_throwing) {
            eval->throwing = true;
            eval->exception_value = saved_exception;
        }
        if (!eval->returning && was_returning) {
            eval->returning = true;
            eval->return_value = saved_return;
        }
    }
    
    return result;
}

static Value evaluate_throw(Evaluator *eval, ASTNode *node) {
    Value exception = evaluate(eval, node->throw_stmt.expression);
    if (eval->had_error) return value_null();
    
    eval->throwing = true;
    eval->exception_value = exception;
    
    return value_null();
}

// 文字列補間: "こんにちは{名前}さん" → 式を評価して埋め込む
static Value evaluate_string_interpolation(Evaluator *eval, const char *str, int line) {
    // {を含まない場合はそのまま返す
    if (strchr(str, '{') == NULL) {
        return value_string(str);
    }
    
    int capacity = 256;
    int length = 0;
    char *result = malloc(capacity);
    result[0] = '\0';
    
    const char *p = str;
    while (*p) {
        if (*p == '\\' && *(p + 1) == '{') {
            // エスケープされた{はそのまま
            if (length + 1 >= capacity) {
                capacity *= 2;
                result = realloc(result, capacity);
            }
            result[length++] = '{';
            p += 2;
        } else if (*p == '{') {
            p++;  // { をスキップ
            
            // } までの式を取得
            const char *expr_start = p;
            int brace_depth = 1;
            while (*p && brace_depth > 0) {
                if (*p == '{') brace_depth++;
                else if (*p == '}') brace_depth--;
                if (brace_depth > 0) p++;
            }
            
            if (brace_depth != 0) {
                runtime_error(eval, line, "文字列補間の '}' が閉じられていません");
                free(result);
                return value_null();
            }
            
            // 式を抽出
            int expr_len = (int)(p - expr_start);
            char *expr_str = malloc(expr_len + 1);
            memcpy(expr_str, expr_start, expr_len);
            expr_str[expr_len] = '\0';
            
            p++;  // } をスキップ
            
            // 式をパースして評価
            Parser expr_parser;
            parser_init(&expr_parser, expr_str, "<interpolation>");
            ASTNode *expr = parse_expression(&expr_parser);
            
            if (!parser_had_error(&expr_parser)) {
                Value val = evaluate(eval, expr);
                if (!eval->had_error) {
                    char *val_str = value_to_string(val);
                    int val_len = strlen(val_str);
                    while (length + val_len + 1 >= capacity) {
                        capacity *= 2;
                        result = realloc(result, capacity);
                    }
                    memcpy(result + length, val_str, val_len);
                    length += val_len;
                    free(val_str);
                }
            } else {
                runtime_error(eval, line, "文字列補間の式が不正です: %s", expr_str);
            }
            
            node_free(expr);
            parser_free(&expr_parser);
            free(expr_str);
        } else {
            // UTF-8マルチバイト文字を正しくコピー
            int char_len = utf8_char_length((unsigned char)*p);
            if (char_len == 0) char_len = 1;
            while (length + char_len + 1 >= capacity) {
                capacity *= 2;
                result = realloc(result, capacity);
            }
            memcpy(result + length, p, char_len);
            length += char_len;
            p += char_len;
        }
    }
    
    result[length] = '\0';
    Value ret = value_string(result);
    free(result);
    return ret;
}

static Value evaluate_lambda(Evaluator *eval, ASTNode *node) {
    // ラムダノードをそのまま関数値として包む
    return value_function(node, eval->current);
}

static Value evaluate_switch(Evaluator *eval, ASTNode *node) {
    Value target = evaluate(eval, node->switch_stmt.target);
    if (eval->had_error) return value_null();
    
    // 各場合句をチェック
    for (int i = 0; i < node->switch_stmt.case_count; i++) {
        Value case_val = evaluate(eval, node->switch_stmt.case_values[i]);
        if (eval->had_error) return value_null();
        
        if (value_equals(target, case_val)) {
            // bodyがNULLの場合（複数パターンのフォールスルー）は次の非NULL bodyを探す
            ASTNode *body = node->switch_stmt.case_bodies[i];
            if (body == NULL) {
                for (int j = i + 1; j < node->switch_stmt.case_count; j++) {
                    if (node->switch_stmt.case_bodies[j] != NULL) {
                        body = node->switch_stmt.case_bodies[j];
                        break;
                    }
                }
                // それでもNULLならdefault_bodyを使う
                if (body == NULL) {
                    body = node->switch_stmt.default_body;
                }
            }
            if (body != NULL) {
                return evaluate(eval, body);
            }
            return value_null();
        }
    }
    
    // 既定句
    if (node->switch_stmt.default_body != NULL) {
        return evaluate(eval, node->switch_stmt.default_body);
    }
    
    return value_null();
}

static Value evaluate_foreach(Evaluator *eval, ASTNode *node) {
    Value iterable = evaluate(eval, node->foreach_stmt.iterable);
    if (eval->had_error) return value_null();
    
    Value result = value_null();
    
    // ループ用の新しいスコープを作成
    Environment *loop_env = env_new(eval->current);
    Environment *prev = eval->current;
    eval->current = loop_env;
    
    if (iterable.type == VALUE_ARRAY) {
        for (int i = 0; i < iterable.array.length; i++) {
            env_define(eval->current, node->foreach_stmt.var_name,
                      value_copy(iterable.array.elements[i]), false);
            
            result = evaluate(eval, node->foreach_stmt.body);
            
            if (eval->returning) break;
            if (eval->breaking) {
                eval->breaking = false;
                break;
            }
            if (eval->continuing) {
                eval->continuing = false;
                continue;
            }
        }
    } else if (iterable.type == VALUE_STRING) {
        // 文字列の各文字をループ
        int len = string_length(&iterable);
        for (int i = 0; i < len; i++) {
            Value ch = string_substring(&iterable, i, i + 1);
            env_define(eval->current, node->foreach_stmt.var_name, ch, false);
            
            result = evaluate(eval, node->foreach_stmt.body);
            
            if (eval->returning) break;
            if (eval->breaking) {
                eval->breaking = false;
                break;
            }
            if (eval->continuing) {
                eval->continuing = false;
                continue;
            }
        }
    } else if (iterable.type == VALUE_DICT) {
        // 辞書のキー（＋値）をループ
        for (int i = 0; i < iterable.dict.length; i++) {
            if (iterable.dict.keys[i] != NULL) {
                env_define(eval->current, node->foreach_stmt.var_name,
                          value_string(iterable.dict.keys[i]), false);
                
                // キー・値ペア展開: 各 キー, 値 を 辞書 の中:
                if (node->foreach_stmt.value_name) {
                    env_define(eval->current, node->foreach_stmt.value_name,
                              value_copy(iterable.dict.values[i]), false);
                }
                
                result = evaluate(eval, node->foreach_stmt.body);
                
                if (eval->returning) break;
                if (eval->breaking) {
                    eval->breaking = false;
                    break;
                }
                if (eval->continuing) {
                    eval->continuing = false;
                    continue;
                }
            }
        }
    } else {
        runtime_error(eval, node->location.line,
                     "反復できるのは配列、文字列、辞書のみです");
    }
    
    eval->current = prev;
    env_free(loop_env);
    
    return result;
}

// =============================================================================
// 組み込み関数の実装
// =============================================================================

static Value builtin_print(int argc, Value *argv) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        // toStringプロトコル
        if (argv[i].type == VALUE_INSTANCE) {
            Value str_val = call_instance_to_string(&argv[i]);
            char *str = value_to_string(str_val);
            printf("%s", str);
            free(str);
            value_free(&str_val);
        } else {
            char *str = value_to_string(argv[i]);
            printf("%s", str);
            free(str);
        }
    }
    printf("\n");
    return value_null();
}

static Value builtin_input(int argc, Value *argv) {
    if (argc > 0) {
        char *prompt = value_to_string(argv[0]);
        printf("%s", prompt);
        free(prompt);
    }
    
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        // 改行を削除
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        return value_string(buffer);
    }
    
    return value_string("");
}

static Value builtin_length(int argc, Value *argv) {
    (void)argc;
    
    if (argv[0].type == VALUE_ARRAY) {
        return value_number(argv[0].array.length);
    }
    if (argv[0].type == VALUE_STRING) {
        return value_number(string_length(&argv[0]));
    }
    
    return value_number(0);
}

static Value builtin_append(int argc, Value *argv) {
    (void)argc;
    
    if (argv[0].type != VALUE_ARRAY) {
        return value_null();
    }
    
    array_push(&argv[0], argv[1]);
    return value_null();
}

static Value builtin_remove(int argc, Value *argv) {
    (void)argc;
    
    if (argv[0].type != VALUE_ARRAY) {
        return value_null();
    }
    
    if (argv[1].type != VALUE_NUMBER) {
        return value_null();
    }
    
    int index = (int)argv[1].number;
    if (index < 0 || index >= argv[0].array.length) {
        return value_null();
    }
    
    Value removed = argv[0].array.elements[index];
    
    // 要素をシフト
    for (int i = index; i < argv[0].array.length - 1; i++) {
        argv[0].array.elements[i] = argv[0].array.elements[i + 1];
    }
    argv[0].array.length--;
    
    return removed;
}

static Value builtin_type(int argc, Value *argv) {
    (void)argc;
    return value_string(value_type_name(argv[0].type));
}

// 型チェック関数
static Value builtin_is_number(int argc, Value *argv) {
    (void)argc;
    return value_bool(argv[0].type == VALUE_NUMBER);
}

static Value builtin_is_string(int argc, Value *argv) {
    (void)argc;
    return value_bool(argv[0].type == VALUE_STRING);
}

static Value builtin_is_bool(int argc, Value *argv) {
    (void)argc;
    return value_bool(argv[0].type == VALUE_BOOL);
}

static Value builtin_is_array(int argc, Value *argv) {
    (void)argc;
    return value_bool(argv[0].type == VALUE_ARRAY);
}

static Value builtin_is_dict(int argc, Value *argv) {
    (void)argc;
    return value_bool(argv[0].type == VALUE_DICT);
}

static Value builtin_is_function(int argc, Value *argv) {
    (void)argc;
    return value_bool(argv[0].type == VALUE_FUNCTION || argv[0].type == VALUE_BUILTIN);
}

static Value builtin_is_null(int argc, Value *argv) {
    (void)argc;
    return value_bool(argv[0].type == VALUE_NULL);
}

// 範囲関数: 範囲(終了) / 範囲(開始, 終了) / 範囲(開始, 終了, ステップ)
static Value builtin_range(int argc, Value *argv) {
    double start, end, step;
    
    if (argc == 1) {
        if (argv[0].type != VALUE_NUMBER) return value_null();
        start = 0;
        end = argv[0].number;
        step = 1;
    } else if (argc == 2) {
        if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) return value_null();
        start = argv[0].number;
        end = argv[1].number;
        step = (start <= end) ? 1 : -1;
    } else {
        if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER || argv[2].type != VALUE_NUMBER) return value_null();
        start = argv[0].number;
        end = argv[1].number;
        step = argv[2].number;
        if (step == 0) return value_null();
    }
    
    // 要素数を計算
    int count = 0;
    if (step > 0) {
        for (double i = start; i < end; i += step) count++;
    } else {
        for (double i = start; i > end; i += step) count++;
    }
    
    if (count <= 0) return value_array();
    if (count > 1000000) return value_null(); // 安全制限
    
    Value result = value_array_with_capacity(count);
    if (step > 0) {
        for (double i = start; i < end; i += step) {
            array_push(&result, value_number(i));
        }
    } else {
        for (double i = start; i > end; i += step) {
            array_push(&result, value_number(i));
        }
    }
    
    return result;
}

// ビット演算関数
static Value builtin_bit_and(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) return value_null();
    return value_number((double)((long long)argv[0].number & (long long)argv[1].number));
}

static Value builtin_bit_or(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) return value_null();
    return value_number((double)((long long)argv[0].number | (long long)argv[1].number));
}

static Value builtin_bit_xor(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) return value_null();
    return value_number((double)((long long)argv[0].number ^ (long long)argv[1].number));
}

static Value builtin_bit_not(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number((double)(~(long long)argv[0].number));
}

static Value builtin_bit_lshift(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) return value_null();
    return value_number((double)((long long)argv[0].number << (int)argv[1].number));
}

static Value builtin_bit_rshift(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) return value_null();
    return value_number((double)((long long)argv[0].number >> (int)argv[1].number));
}

// =============================================================================
// 追加文字列関数
// =============================================================================

// 部分文字列: 文字列の部分を取得（開始位置, [長さ]）
static Value builtin_substring(int argc, Value *argv) {
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_NUMBER) return value_null();
    
    const char *str = argv[0].string.data;
    int len = argv[0].string.length;
    int start = (int)argv[1].number;
    
    if (start < 0) start += len;
    if (start < 0) start = 0;
    if (start >= len) return value_string("");
    
    int sub_len;
    if (argc >= 3 && argv[2].type == VALUE_NUMBER) {
        sub_len = (int)argv[2].number;
        if (sub_len < 0) sub_len = 0;
    } else {
        sub_len = len - start;
    }
    
    if (start + sub_len > len) sub_len = len - start;
    
    return value_string_n(str + start, sub_len);
}

// 始まる: 文字列が指定のプレフィックスで始まるか
static Value builtin_starts_with(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) return value_bool(false);
    
    const char *str = argv[0].string.data;
    const char *prefix = argv[1].string.data;
    size_t prefix_len = strlen(prefix);
    
    return value_bool(strncmp(str, prefix, prefix_len) == 0);
}

// 終わる: 文字列が指定のサフィックスで終わるか
static Value builtin_ends_with(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) return value_bool(false);
    
    const char *str = argv[0].string.data;
    const char *suffix = argv[1].string.data;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return value_bool(false);
    
    return value_bool(strcmp(str + str_len - suffix_len, suffix) == 0);
}

// 文字コード: 文字列の指定位置の文字コード（UTF-8）を返す
static Value builtin_char_code(int argc, Value *argv) {
    if (argv[0].type != VALUE_STRING) return value_null();
    
    int pos = 0;
    if (argc >= 2 && argv[1].type == VALUE_NUMBER) {
        pos = (int)argv[1].number;
    }
    
    const unsigned char *str = (const unsigned char *)argv[0].string.data;
    int len = argv[0].string.length;
    if (pos < 0 || pos >= len) return value_null();
    
    // UTF-8の先頭バイトからコードポイントを取得
    unsigned char c = str[pos];
    int code = 0;
    if (c < 0x80) {
        code = c;
    } else if (c < 0xE0 && pos + 1 < len) {
        code = ((c & 0x1F) << 6) | (str[pos + 1] & 0x3F);
    } else if (c < 0xF0 && pos + 2 < len) {
        code = ((c & 0x0F) << 12) | ((str[pos + 1] & 0x3F) << 6) | (str[pos + 2] & 0x3F);
    } else if (pos + 3 < len) {
        code = ((c & 0x07) << 18) | ((str[pos + 1] & 0x3F) << 12) | ((str[pos + 2] & 0x3F) << 6) | (str[pos + 3] & 0x3F);
    }
    
    return value_number(code);
}

// コード文字: コードポイントから文字列を生成
static Value builtin_from_char_code(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    
    int code = (int)argv[0].number;
    char buf[5] = {0};
    
    if (code < 0x80) {
        buf[0] = (char)code;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
    } else {
        buf[0] = (char)(0xF0 | (code >> 18));
        buf[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (code & 0x3F));
    }
    
    return value_string(buf);
}

// 繰り返し: 文字列を指定回数繰り返す
static Value builtin_string_repeat(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_NUMBER) return value_null();
    
    int count = (int)argv[1].number;
    if (count <= 0) return value_string("");
    
    const char *str = argv[0].string.data;
    size_t str_len = strlen(str);
    size_t total = str_len * count;
    
    char *buffer = malloc(total + 1);
    buffer[0] = '\0';
    for (int i = 0; i < count; i++) {
        memcpy(buffer + i * str_len, str, str_len);
    }
    buffer[total] = '\0';
    
    Value result = value_string(buffer);
    free(buffer);
    return result;
}

// =============================================================================
// 追加配列関数
// =============================================================================

// 末尾削除: 配列の末尾要素を削除して返す
static Value builtin_pop(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_null();
    if (argv[0].array.length == 0) return value_null();
    
    Value last = value_copy(argv[0].array.elements[argv[0].array.length - 1]);
    argv[0].array.length--;
    return last;
}

// 探す: 配列から条件に合う最初の要素を検索
static Value builtin_find_item(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_FUNCTION) return value_null();
    
    for (int i = 0; i < argv[0].array.length; i++) {
        Value arg = argv[0].array.elements[i];
        Value result = call_function_value(&argv[1], &arg, 1);
        if (g_eval && g_eval->had_error) return value_null();
        if (value_is_truthy(result)) {
            return value_copy(arg);
        }
    }
    
    return value_null();
}

// 全て: 配列の全要素が条件を満たすか
static Value builtin_every(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_FUNCTION) return value_bool(false);
    
    for (int i = 0; i < argv[0].array.length; i++) {
        Value arg = argv[0].array.elements[i];
        Value result = call_function_value(&argv[1], &arg, 1);
        if (g_eval && g_eval->had_error) return value_bool(false);
        if (!value_is_truthy(result)) return value_bool(false);
    }
    
    return value_bool(true);
}

// 一つでも: 配列の少なくとも一つの要素が条件を満たすか
static Value builtin_some(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_FUNCTION) return value_bool(false);
    
    for (int i = 0; i < argv[0].array.length; i++) {
        Value arg = argv[0].array.elements[i];
        Value result = call_function_value(&argv[1], &arg, 1);
        if (g_eval && g_eval->had_error) return value_bool(false);
        if (value_is_truthy(result)) return value_bool(true);
    }
    
    return value_bool(false);
}

// 一意: 配列の重複を除去
static Value builtin_unique(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_array();
    
    Value result = value_array();
    for (int i = 0; i < argv[0].array.length; i++) {
        bool found = false;
        for (int j = 0; j < result.array.length; j++) {
            if (value_compare(argv[0].array.elements[i], result.array.elements[j]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            array_push(&result, value_copy(argv[0].array.elements[i]));
        }
    }
    
    return result;
}

// 圧縮: 2つの配列をペアの配列に結合
static Value builtin_zip(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_ARRAY) return value_array();
    
    int len = argv[0].array.length < argv[1].array.length ? 
              argv[0].array.length : argv[1].array.length;
    
    Value result = value_array_with_capacity(len);
    for (int i = 0; i < len; i++) {
        Value pair = value_array_with_capacity(2);
        array_push(&pair, value_copy(argv[0].array.elements[i]));
        array_push(&pair, value_copy(argv[1].array.elements[i]));
        array_push(&result, pair);
    }
    
    return result;
}

// 平坦化: ネストされた配列を一段フラットにする
static Value builtin_flat(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_array();
    
    Value result = value_array();
    for (int i = 0; i < argv[0].array.length; i++) {
        Value elem = argv[0].array.elements[i];
        if (elem.type == VALUE_ARRAY) {
            for (int j = 0; j < elem.array.length; j++) {
                array_push(&result, value_copy(elem.array.elements[j]));
            }
        } else {
            array_push(&result, value_copy(elem));
        }
    }
    
    return result;
}

// 挿入: 配列の指定位置に要素を挿入
static Value builtin_insert(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_NUMBER) return value_null();
    
    int pos = (int)argv[1].number;
    int len = argv[0].array.length;
    
    if (pos < 0) pos += len;
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;
    
    Value result = value_array_with_capacity(len + 1);
    for (int i = 0; i < pos; i++) {
        array_push(&result, value_copy(argv[0].array.elements[i]));
    }
    array_push(&result, value_copy(argv[2]));
    for (int i = pos; i < len; i++) {
        array_push(&result, value_copy(argv[0].array.elements[i]));
    }
    
    return result;
}

// 比較ソート用グローバル
static Value *g_sort_func = NULL;

static int compare_by_func(const void *a, const void *b) {
    Value va = *(const Value *)a;
    Value vb = *(const Value *)b;
    Value args[2] = { va, vb };
    Value result = call_function_value(g_sort_func, args, 2);
    if (result.type == VALUE_NUMBER) {
        if (result.number < 0) return -1;
        if (result.number > 0) return 1;
        return 0;
    }
    return 0;
}

// 比較ソート: カスタム比較関数でソート
static Value builtin_sort_by(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_FUNCTION) return value_array();
    
    Value result = value_copy(argv[0]);
    g_sort_func = &argv[1];
    qsort(result.array.elements, result.array.length, sizeof(Value), compare_by_func);
    g_sort_func = NULL;
    
    return result;
}

// =============================================================================
// 拡張数学関数
// =============================================================================

static Value builtin_sin(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(sin(argv[0].number));
}

static Value builtin_cos(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(cos(argv[0].number));
}

static Value builtin_tan(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(tan(argv[0].number));
}

static Value builtin_log(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(log(argv[0].number));
}

static Value builtin_log10_fn(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(log10(argv[0].number));
}

static Value builtin_random_int(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) return value_null();
    
    int min_val = (int)argv[0].number;
    int max_val = (int)argv[1].number;
    if (min_val > max_val) { int tmp = min_val; min_val = max_val; max_val = tmp; }
    
    return value_number(min_val + rand() % (max_val - min_val + 1));
}

// =============================================================================
// ファイル追記・ディレクトリ操作
// =============================================================================

// 追記: ファイルにテキストを追記
static Value builtin_file_append(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) return value_bool(false);
    
    FILE *fp = fopen(argv[0].string.data, "a");
    if (!fp) return value_bool(false);
    
    fprintf(fp, "%s", argv[1].string.data);
    fclose(fp);
    return value_bool(true);
}

// ディレクトリ一覧: ディレクトリの内容をリスト
static Value builtin_dir_list(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_array();
    
    DIR *dir = opendir(argv[0].string.data);
    if (!dir) return value_array();
    
    Value result = value_array();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        array_push(&result, value_string(entry->d_name));
    }
    closedir(dir);
    
    return result;
}

// ディレクトリ作成: ディレクトリを再帰的に作成
static Value builtin_dir_create(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_bool(false);
    
    // mkdir -p 相当（簡易版）
    char *path = strdup(argv[0].string.data);
    char *p = path;
    
    while (*p) {
        if (*p == '/' && p != path) {
            *p = '\0';
            mkdir(path, 0755);
            *p = '/';
        }
        p++;
    }
    int ret = mkdir(path, 0755);
    free(path);
    
    return value_bool(ret == 0 || errno == EEXIST);
}

// =============================================================================
// ユーティリティ関数
// =============================================================================

// 表明: アサーション（条件が偽なら実行停止）
static Value builtin_assert(int argc, Value *argv) {
    if (!value_is_truthy(argv[0])) {
        const char *msg = "表明失敗";
        if (argc >= 2 && argv[1].type == VALUE_STRING) {
            msg = argv[1].string.data;
        }
        if (g_eval) {
            runtime_error(g_eval, 0, "%s", msg);
        } else {
            fprintf(stderr, "表明失敗: %s\n", msg);
        }
        return value_null();
    }
    return value_bool(true);
}

// 型判定: オブジェクトが指定の型/クラスかどうかをチェック
static Value builtin_typeof_check(int argc, Value *argv) {
    (void)argc;
    if (argv[1].type != VALUE_STRING) return value_bool(false);
    
    const char *type_name = argv[1].string.data;
    
    if (strcmp(type_name, "数値") == 0) return value_bool(argv[0].type == VALUE_NUMBER);
    if (strcmp(type_name, "文字列") == 0) return value_bool(argv[0].type == VALUE_STRING);
    if (strcmp(type_name, "真偽") == 0) return value_bool(argv[0].type == VALUE_BOOL);
    if (strcmp(type_name, "配列") == 0) return value_bool(argv[0].type == VALUE_ARRAY);
    if (strcmp(type_name, "辞書") == 0) return value_bool(argv[0].type == VALUE_DICT);
    if (strcmp(type_name, "関数") == 0) return value_bool(argv[0].type == VALUE_FUNCTION);
    if (strcmp(type_name, "無") == 0) return value_bool(argv[0].type == VALUE_NULL);
    if (strcmp(type_name, "ジェネレータ") == 0) return value_bool(argv[0].type == VALUE_GENERATOR);
    
    // クラスインスタンスの場合、クラス名と比較
    if (argv[0].type == VALUE_INSTANCE && argv[0].instance.class_ref != NULL) {
        Value *class_ref = argv[0].instance.class_ref;
        while (class_ref != NULL && class_ref->type == VALUE_CLASS) {
            if (strcmp(class_ref->class_value.name, type_name) == 0) {
                return value_bool(true);
            }
            // 親クラスも確認（instanceof 的な動作）
            class_ref = class_ref->class_value.parent;
        }
    }
    
    return value_bool(false);
}

static Value builtin_to_number(int argc, Value *argv) {
    (void)argc;
    return value_to_number(argv[0]);
}

// インスタンスの文字列化メソッドを呼ぶ（toStringプロトコル）
static Value call_instance_to_string(Value *instance) {
    if (g_eval == NULL || instance->type != VALUE_INSTANCE) {
        char *str = value_to_string(*instance);
        Value result = value_string(str);
        free(str);
        return result;
    }
    
    // 文字列化メソッドを探す
    Value *class_ref = instance->instance.class_ref;
    while (class_ref != NULL && class_ref->type == VALUE_CLASS) {
        ASTNode *class_def = class_ref->class_value.definition;
        for (int i = 0; i < class_def->class_def.method_count; i++) {
            ASTNode *method = class_def->class_def.methods[i];
            if (strcmp(method->method.name, "文字列化") == 0) {
                // メソッドを呼び出す
                Environment *method_env = env_new(g_eval->current);
                Environment *saved = g_eval->current;
                Value *saved_instance = g_eval->current_instance;
                
                g_eval->current = method_env;
                g_eval->current_instance = instance;
                env_define(method_env, "自分", value_copy(*instance), false);
                
                Value ret = evaluate(g_eval, method->method.body);
                
                if (g_eval->returning) {
                    ret = g_eval->return_value;
                    g_eval->returning = false;
                }
                
                g_eval->current = saved;
                g_eval->current_instance = saved_instance;
                env_free(method_env);
                
                if (ret.type == VALUE_STRING) {
                    return value_copy(ret);
                }
                char *str = value_to_string(ret);
                Value result = value_string(str);
                free(str);
                return result;
            }
        }
        // 親クラスを探す
        if (class_def->class_def.parent_name != NULL) {
            Value *parent = env_get(g_eval->current, class_def->class_def.parent_name);
            if (parent != NULL && parent->type == VALUE_CLASS) {
                class_ref = parent;
            } else break;
        } else break;
    }
    
    // 文字列化メソッドがなければデフォルト
    char *str = value_to_string(*instance);
    Value result = value_string(str);
    free(str);
    return result;
}

static Value builtin_to_string(int argc, Value *argv) {
    (void)argc;
    
    // toStringプロトコル: インスタンスの文字列化メソッドを呼ぶ
    if (argv[0].type == VALUE_INSTANCE) {
        return call_instance_to_string(&argv[0]);
    }
    
    char *str = value_to_string(argv[0]);
    Value result = value_string(str);
    free(str);
    return result;
}

static Value builtin_abs(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(fabs(argv[0].number));
}

static Value builtin_sqrt(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(sqrt(argv[0].number));
}

static Value builtin_floor(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(floor(argv[0].number));
}

static Value builtin_ceil(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(ceil(argv[0].number));
}

static Value builtin_round(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    return value_number(round(argv[0].number));
}

static Value builtin_random(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_number((double)rand() / RAND_MAX);
}

static Value builtin_max(int argc, Value *argv) {
    if (argc == 0) return value_null();
    
    double max = -INFINITY;
    for (int i = 0; i < argc; i++) {
        if (argv[i].type == VALUE_NUMBER) {
            if (argv[i].number > max) {
                max = argv[i].number;
            }
        }
    }
    
    return value_number(max);
}

static Value builtin_min(int argc, Value *argv) {
    if (argc == 0) return value_null();
    
    double min = INFINITY;
    for (int i = 0; i < argc; i++) {
        if (argv[i].type == VALUE_NUMBER) {
            if (argv[i].number < min) {
                min = argv[i].number;
            }
        }
    }
    
    return value_number(min);
}
// =============================================================================
// 辞書関数
// =============================================================================

static Value builtin_dict_keys(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_DICT) return value_array();
    return dict_keys(&argv[0]);
}

static Value builtin_dict_values(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_DICT) return value_array();
    return dict_values(&argv[0]);
}

static Value builtin_dict_has(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_DICT || argv[1].type != VALUE_STRING) {
        return value_bool(false);
    }
    return value_bool(dict_has(&argv[0], argv[1].string.data));
}

// =============================================================================
// 文字列関数
// =============================================================================

static Value builtin_split(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_array();
    }
    
    Value result = value_array();
    char *str = strdup(argv[0].string.data);
    char *delim = argv[1].string.data;
    
    char *token = strtok(str, delim);
    while (token != NULL) {
        Value s = value_string(token);
        array_push(&result, s);
        value_free(&s);
        token = strtok(NULL, delim);
    }
    
    free(str);
    return result;
}

static Value builtin_join(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_STRING) {
        return value_string("");
    }
    
    // 結果の長さを計算
    size_t total_len = 0;
    size_t delim_len = strlen(argv[1].string.data);
    
    for (int i = 0; i < argv[0].array.length; i++) {
        char *s = value_to_string(argv[0].array.elements[i]);
        total_len += strlen(s);
        if (i > 0) total_len += delim_len;
        free(s);
    }
    
    // 結果を構築
    char *buffer = malloc(total_len + 1);
    buffer[0] = '\0';
    
    for (int i = 0; i < argv[0].array.length; i++) {
        if (i > 0) strcat(buffer, argv[1].string.data);
        char *s = value_to_string(argv[0].array.elements[i]);
        strcat(buffer, s);
        free(s);
    }
    
    Value result = value_string(buffer);
    free(buffer);
    return result;
}

static Value builtin_find(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_number(-1);
    }
    
    char *pos = strstr(argv[0].string.data, argv[1].string.data);
    if (pos == NULL) return value_number(-1);
    
    return value_number(pos - argv[0].string.data);
}

static Value builtin_replace(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING || 
        argv[2].type != VALUE_STRING) {
        return argv[0];
    }
    
    char *src = argv[0].string.data;
    char *old = argv[1].string.data;
    char *new = argv[2].string.data;
    
    size_t old_len = strlen(old);
    size_t new_len = strlen(new);
    
    if (old_len == 0) return value_string(src);
    
    // 置換回数を数える
    int count = 0;
    char *p = src;
    while ((p = strstr(p, old)) != NULL) {
        count++;
        p += old_len;
    }
    
    // 結果バッファを確保
    size_t result_len = strlen(src) + count * (new_len - old_len);
    char *buffer = malloc(result_len + 1);
    char *dst = buffer;
    
    p = src;
    char *q;
    while ((q = strstr(p, old)) != NULL) {
        size_t len = q - p;
        memcpy(dst, p, len);
        dst += len;
        memcpy(dst, new, new_len);
        dst += new_len;
        p = q + old_len;
    }
    strcpy(dst, p);
    
    Value result = value_string(buffer);
    free(buffer);
    return result;
}

static Value builtin_upper(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_string("");
    
    char *buffer = strdup(argv[0].string.data);
    for (char *p = buffer; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    }
    
    Value result = value_string(buffer);
    free(buffer);
    return result;
}

static Value builtin_lower(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_string("");
    
    char *buffer = strdup(argv[0].string.data);
    for (char *p = buffer; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
    }
    
    Value result = value_string(buffer);
    free(buffer);
    return result;
}

static Value builtin_trim(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_string("");
    
    char *str = argv[0].string.data;
    int len = argv[0].string.length;
    
    // 先頭の空白をスキップ
    int start = 0;
    while (start < len && (str[start] == ' ' || str[start] == '\t' || 
                           str[start] == '\n' || str[start] == '\r')) {
        start++;
    }
    
    // 末尾の空白をスキップ
    int end = len;
    while (end > start && (str[end-1] == ' ' || str[end-1] == '\t' || 
                           str[end-1] == '\n' || str[end-1] == '\r')) {
        end--;
    }
    
    return value_string_n(str + start, end - start);
}

// =============================================================================
// 配列関数
// =============================================================================

// 比較関数（ソート用）
static int compare_values(const void *a, const void *b) {
    Value va = *(Value *)a;
    Value vb = *(Value *)b;
    return value_compare(va, vb);
}

static Value builtin_sort(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_array();
    
    Value result = value_copy(argv[0]);
    qsort(result.array.elements, result.array.length, 
          sizeof(Value), compare_values);
    return result;
}

static Value builtin_reverse(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_array();
    
    Value result = value_array_with_capacity(argv[0].array.length);
    for (int i = argv[0].array.length - 1; i >= 0; i--) {
        array_push(&result, argv[0].array.elements[i]);
    }
    return result;
}

static Value builtin_slice(int argc, Value *argv) {
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_NUMBER) {
        return value_array();
    }
    
    int start = (int)argv[1].number;
    int end = argv[0].array.length;
    
    if (argc >= 3 && argv[2].type == VALUE_NUMBER) {
        end = (int)argv[2].number;
    }
    
    if (start < 0) start = 0;
    if (end > argv[0].array.length) end = argv[0].array.length;
    if (start >= end) return value_array();
    
    Value result = value_array_with_capacity(end - start);
    for (int i = start; i < end; i++) {
        array_push(&result, argv[0].array.elements[i]);
    }
    return result;
}

static Value builtin_index_of(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_number(-1);
    
    for (int i = 0; i < argv[0].array.length; i++) {
        if (value_equals(argv[0].array.elements[i], argv[1])) {
            return value_number(i);
        }
    }
    return value_number(-1);
}

static Value builtin_contains(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_bool(false);
    
    for (int i = 0; i < argv[0].array.length; i++) {
        if (value_equals(argv[0].array.elements[i], argv[1])) {
            return value_bool(true);
        }
    }
    return value_bool(false);
}

// =============================================================================
// ファイル関数
// =============================================================================

static Value builtin_file_read(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    FILE *file = fopen(argv[0].string.data, "rb");
    if (file == NULL) return value_null();
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *buffer = malloc(size + 1);
    size_t read = fread(buffer, 1, size, file);
    buffer[read] = '\0';
    fclose(file);
    
    Value result = value_string(buffer);
    free(buffer);
    return result;
}

static Value builtin_file_write(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_bool(false);
    }
    
    FILE *file = fopen(argv[0].string.data, "wb");
    if (file == NULL) return value_bool(false);
    
    size_t written = fwrite(argv[1].string.data, 1, argv[1].string.length, file);
    fclose(file);
    
    return value_bool(written == (size_t)argv[1].string.length);
}

static Value builtin_file_exists(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_bool(false);
    
    FILE *file = fopen(argv[0].string.data, "r");
    if (file != NULL) {
        fclose(file);
        return value_bool(true);
    }
    return value_bool(false);
}

// =============================================================================
// 日時関数
// =============================================================================

static Value builtin_now(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    return value_number((double)time(NULL));
}

static Value builtin_date(int argc, Value *argv) {
    time_t t;
    if (argc > 0 && argv[0].type == VALUE_NUMBER) {
        t = (time_t)argv[0].number;
    } else {
        t = time(NULL);
    }
    
    struct tm *tm = localtime(&t);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", tm);
    return value_string(buffer);
}

static Value builtin_time(int argc, Value *argv) {
    time_t t;
    if (argc > 0 && argv[0].type == VALUE_NUMBER) {
        t = (time_t)argv[0].number;
    } else {
        t = time(NULL);
    }
    
    struct tm *tm = localtime(&t);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", tm);
    return value_string(buffer);
}

// =============================================================================
// 高階配列関数ヘルパー
// =============================================================================

// 関数値を呼び出すヘルパー（評価器が必要）
static Value call_function_value(Value *func, Value *args, int argc) {
    if (g_eval == NULL) return value_null();
    if (func->type != VALUE_FUNCTION) return value_null();
    
    ASTNode *def = func->function.definition;
    
    // ラムダと通常関数の両方に対応
    Parameter *params;
    ASTNode *body;
    int param_count;
    
    if (def->type == NODE_LAMBDA) {
        params = def->lambda.params;
        body = def->lambda.body;
        param_count = def->lambda.param_count;
    } else {
        params = def->function.params;
        body = def->function.body;
        param_count = def->function.param_count;
    }
    
    if (argc != param_count) return value_null();
    
    // 新しいスコープを作成
    Environment *local = env_new(func->function.closure);
    
    for (int i = 0; i < param_count; i++) {
        env_define(local, params[i].name, value_copy(args[i]), false);
    }
    
    Environment *prev = g_eval->current;
    g_eval->current = local;
    
    Value result = evaluate(g_eval, body);
    
    if (g_eval->returning) {
        result = g_eval->return_value;
        g_eval->returning = false;
    }
    
    g_eval->current = prev;
    env_free(local);
    
    return result;
}

// =============================================================================
// 高階配列関数
// =============================================================================

// 変換（map）: 配列の各要素に関数を適用して新しい配列を返す
static Value builtin_map(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_null();
    if (argv[1].type != VALUE_FUNCTION) return value_null();
    
    Value result = value_array_with_capacity(argv[0].array.length);
    
    for (int i = 0; i < argv[0].array.length; i++) {
        Value arg = argv[0].array.elements[i];
        Value mapped = call_function_value(&argv[1], &arg, 1);
        if (g_eval && g_eval->had_error) return value_null();
        array_push(&result, mapped);
    }
    
    return result;
}

// 抽出（filter）: 条件に合う要素だけを抽出
static Value builtin_filter(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_null();
    if (argv[1].type != VALUE_FUNCTION) return value_null();
    
    Value result = value_array();
    
    for (int i = 0; i < argv[0].array.length; i++) {
        Value arg = argv[0].array.elements[i];
        Value keep = call_function_value(&argv[1], &arg, 1);
        if (g_eval && g_eval->had_error) return value_null();
        if (value_is_truthy(keep)) {
            array_push(&result, value_copy(arg));
        }
    }
    
    return result;
}

// 集約（reduce）: 配列を一つの値に集約
static Value builtin_reduce(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_null();
    if (argv[1].type != VALUE_FUNCTION) return value_null();
    
    Value accumulator = value_copy(argv[2]);
    
    for (int i = 0; i < argv[0].array.length; i++) {
        Value args[2] = { accumulator, argv[0].array.elements[i] };
        accumulator = call_function_value(&argv[1], args, 2);
        if (g_eval && g_eval->had_error) return value_null();
    }
    
    return accumulator;
}

// 反復（forEach）: 配列の各要素に対して関数を実行（戻り値なし）
static Value builtin_foreach(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_null();
    if (argv[1].type != VALUE_FUNCTION) return value_null();
    
    for (int i = 0; i < argv[0].array.length; i++) {
        Value arg = argv[0].array.elements[i];
        call_function_value(&argv[1], &arg, 1);
        if (g_eval && g_eval->had_error) return value_null();
    }
    
    return value_null();
}

// =============================================================================
// 正規表現関数
// =============================================================================

// 正規一致: 文字列が正規表現パターンに完全一致するか
static Value builtin_regex_match(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_bool(false);
    }
    
    regex_t regex;
    int ret = regcomp(&regex, argv[1].string.data, REG_EXTENDED | REG_NOSUB);
    if (ret != 0) return value_bool(false);
    
    ret = regexec(&regex, argv[0].string.data, 0, NULL, 0);
    regfree(&regex);
    
    return value_bool(ret == 0);
}

// 正規検索: 文字列から正規表現にマッチする部分を検索
static Value builtin_regex_search(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_null();
    }
    
    regex_t regex;
    int ret = regcomp(&regex, argv[1].string.data, REG_EXTENDED);
    if (ret != 0) return value_null();
    
    regmatch_t matches[10];
    ret = regexec(&regex, argv[0].string.data, 10, matches, 0);
    
    if (ret != 0) {
        regfree(&regex);
        return value_null();
    }
    
    // マッチした部分文字列の配列を返す
    Value result = value_array();
    
    for (int i = 0; i < 10 && matches[i].rm_so != -1; i++) {
        int start = matches[i].rm_so;
        int end = matches[i].rm_eo;
        int len = end - start;
        
        char *match_str = malloc(len + 1);
        memcpy(match_str, argv[0].string.data + start, len);
        match_str[len] = '\0';
        
        array_push(&result, value_string(match_str));
        free(match_str);
    }
    
    regfree(&regex);
    return result;
}

// 正規置換: 正規表現パターンにマッチする部分を置換
static Value builtin_regex_replace(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING ||
        argv[2].type != VALUE_STRING) {
        return value_null();
    }
    
    regex_t regex;
    int ret = regcomp(&regex, argv[1].string.data, REG_EXTENDED);
    if (ret != 0) return value_copy(argv[0]);
    
    const char *src = argv[0].string.data;
    const char *replacement = argv[2].string.data;
    int rep_len = argv[2].string.length;
    
    // 結果バッファ
    int buf_capacity = argv[0].string.length * 2 + 64;
    char *buf = malloc(buf_capacity);
    int buf_len = 0;
    
    regmatch_t match;
    
    while (*src && regexec(&regex, src, 1, &match, 0) == 0) {
        // マッチ前の部分をコピー
        int prefix_len = match.rm_so;
        while (buf_len + prefix_len + rep_len + 1 >= buf_capacity) {
            buf_capacity *= 2;
            buf = realloc(buf, buf_capacity);
        }
        memcpy(buf + buf_len, src, prefix_len);
        buf_len += prefix_len;
        
        // 置換文字列をコピー
        memcpy(buf + buf_len, replacement, rep_len);
        buf_len += rep_len;
        
        src += match.rm_eo;
        if (match.rm_eo == 0) {
            // 空マッチの場合、1文字進める
            if (*src) {
                buf[buf_len++] = *src++;
            } else {
                break;
            }
        }
    }
    
    // 残りの部分をコピー
    int remaining = strlen(src);
    while (buf_len + remaining + 1 >= buf_capacity) {
        buf_capacity *= 2;
        buf = realloc(buf, buf_capacity);
    }
    memcpy(buf + buf_len, src, remaining);
    buf_len += remaining;
    buf[buf_len] = '\0';
    
    Value result = value_string(buf);
    free(buf);
    regfree(&regex);
    
    return result;
}

// =============================================================================
// システムユーティリティ
// =============================================================================

// 待つ（sleep）: 指定秒数スリープ
static Value builtin_sleep(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return value_null();
    
    double seconds = argv[0].number;
    if (seconds > 0) {
        usleep((useconds_t)(seconds * 1000000));
    }
    
    return value_null();
}

// 実行: シェルコマンドを実行して出力を返す
static Value builtin_exec(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    FILE *pipe = popen(argv[0].string.data, "r");
    if (pipe == NULL) return value_null();
    
    int capacity = 1024;
    char *buffer = malloc(capacity);
    int length = 0;
    
    char chunk[256];
    while (fgets(chunk, sizeof(chunk), pipe) != NULL) {
        int chunk_len = strlen(chunk);
        while (length + chunk_len + 1 >= capacity) {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
        }
        memcpy(buffer + length, chunk, chunk_len);
        length += chunk_len;
    }
    buffer[length] = '\0';
    
    int status = pclose(pipe);
    (void)status;
    
    Value result = value_string(buffer);
    free(buffer);
    return result;
}

// 環境変数: 環境変数の値を取得
static Value builtin_env_get(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    const char *val = getenv(argv[0].string.data);
    if (val == NULL) return value_null();
    return value_string(val);
}

// 環境変数設定: 環境変数を設定
static Value builtin_env_set(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_bool(false);
    }
    
    int result = setenv(argv[0].string.data, argv[1].string.data, 1);
    return value_bool(result == 0);
}

// 終了: プログラムを終了
static Value builtin_exit_program(int argc, Value *argv) {
    int code = 0;
    if (argc > 0 && argv[0].type == VALUE_NUMBER) {
        code = (int)argv[0].number;
    }
    exit(code);
    return value_null();  // 到達しない
}

// =============================================================================
// ジェネレータ関連ビルトイン関数
// =============================================================================

// 次: ジェネレータから次の値を取得
static Value builtin_generator_next(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_GENERATOR || argv[0].generator.state == NULL) {
        return value_null();
    }
    
    GeneratorState *s = argv[0].generator.state;
    if (s->index >= s->length) {
        s->done = true;
        return value_null();
    }
    
    return value_copy(s->values[s->index++]);
}

// 完了: ジェネレータが完了したかチェック
static Value builtin_generator_done(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_GENERATOR || argv[0].generator.state == NULL) {
        return value_bool(true);
    }
    
    return value_bool(argv[0].generator.state->index >= argv[0].generator.state->length);
}

// 全値: ジェネレータの残りの値を配列として取得
static Value builtin_generator_collect(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_GENERATOR || argv[0].generator.state == NULL) {
        return value_array();
    }
    
    GeneratorState *s = argv[0].generator.state;
    Value result = value_array();
    
    while (s->index < s->length) {
        array_push(&result, value_copy(s->values[s->index++]));
    }
    s->done = true;
    
    return result;
}

// =============================================================================
// パス操作関数
// =============================================================================

static Value builtin_path_join(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_null();
    }
    
    char result[2048];
    const char *base = argv[0].string.data;
    const char *part = argv[1].string.data;
    
    size_t base_len = strlen(base);
    if (base_len > 0 && base[base_len - 1] == '/') {
        snprintf(result, sizeof(result), "%s%s", base, part);
    } else {
        snprintf(result, sizeof(result), "%s/%s", base, part);
    }
    
    return value_string(result);
}

static Value builtin_basename(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    const char *path = argv[0].string.data;
    const char *last_slash = strrchr(path, '/');
    
    if (last_slash) {
        return value_string(last_slash + 1);
    }
    return value_string(path);
}

static Value builtin_dirname(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    const char *path = argv[0].string.data;
    const char *last_slash = strrchr(path, '/');
    
    if (last_slash) {
        size_t len = last_slash - path;
        char *dir = malloc(len + 1);
        memcpy(dir, path, len);
        dir[len] = '\0';
        Value result = value_string(dir);
        free(dir);
        return result;
    }
    return value_string(".");
}

static Value builtin_extension(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    const char *path = argv[0].string.data;
    const char *last_dot = strrchr(path, '.');
    const char *last_slash = strrchr(path, '/');
    
    // ドットがスラッシュより前にある場合は拡張子なし
    if (last_dot && (!last_slash || last_dot > last_slash)) {
        return value_string(last_dot);
    }
    return value_string("");
}

// =============================================================================
// Base64関数
// =============================================================================

static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static Value builtin_base64_encode(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    const unsigned char *input = (const unsigned char *)argv[0].string.data;
    size_t input_len = strlen((const char *)input);
    size_t output_len = 4 * ((input_len + 2) / 3);
    char *output = malloc(output_len + 1);
    
    size_t j = 0;
    for (size_t i = 0; i < input_len; i += 3) {
        unsigned int octet_a = input[i];
        unsigned int octet_b = (i + 1 < input_len) ? input[i + 1] : 0;
        unsigned int octet_c = (i + 2 < input_len) ? input[i + 2] : 0;
        
        unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        
        output[j++] = base64_chars[(triple >> 18) & 0x3F];
        output[j++] = base64_chars[(triple >> 12) & 0x3F];
        output[j++] = (i + 1 < input_len) ? base64_chars[(triple >> 6) & 0x3F] : '=';
        output[j++] = (i + 2 < input_len) ? base64_chars[triple & 0x3F] : '=';
    }
    output[j] = '\0';
    
    Value result = value_string(output);
    free(output);
    return result;
}

static int base64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static Value builtin_base64_decode(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    const char *input = argv[0].string.data;
    size_t input_len = strlen(input);
    if (input_len % 4 != 0) return value_string("");
    
    size_t output_len = input_len / 4 * 3;
    if (input_len > 0 && input[input_len - 1] == '=') output_len--;
    if (input_len > 1 && input[input_len - 2] == '=') output_len--;
    
    char *output = malloc(output_len + 1);
    size_t j = 0;
    
    for (size_t i = 0; i < input_len; i += 4) {
        int a = base64_decode_char(input[i]);
        int b = base64_decode_char(input[i + 1]);
        int c = (input[i + 2] != '=') ? base64_decode_char(input[i + 2]) : 0;
        int d = (input[i + 3] != '=') ? base64_decode_char(input[i + 3]) : 0;
        
        if (a < 0 || b < 0) break;
        
        unsigned int triple = (a << 18) | (b << 12) | (c << 6) | d;
        
        if (j < output_len) output[j++] = (triple >> 16) & 0xFF;
        if (j < output_len) output[j++] = (triple >> 8) & 0xFF;
        if (j < output_len) output[j++] = triple & 0xFF;
    }
    output[j] = '\0';
    
    Value result = value_string(output);
    free(output);
    return result;
}