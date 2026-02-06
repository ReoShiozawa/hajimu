/**
 * 日本語プログラミング言語 - 評価器実装
 */

#include "evaluator.h"
#include "parser.h"
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <regex.h>

// 高階関数用のグローバル評価器ポインタ
static Evaluator *g_eval = NULL;

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

// システムユーティリティ
static Value builtin_sleep(int argc, Value *argv);
static Value builtin_exec(int argc, Value *argv);
static Value builtin_env_get(int argc, Value *argv);
static Value builtin_env_set(int argc, Value *argv);
static Value builtin_exit_program(int argc, Value *argv);

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
    eval->debug_mode = false;
    eval->step_mode = false;
    eval->last_line = 0;
    eval->had_error = false;
    eval->error_message[0] = '\0';
    eval->error_line = 0;
    eval->recursion_depth = 0;
    
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
                eval->return_value = evaluate(eval, node->return_stmt.value);
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
    
    if (node->call.callee->type == NODE_MEMBER) {
        instance = evaluate(eval, node->call.callee->member.object);
        if (eval->had_error) return value_null();
        if (instance.type == VALUE_INSTANCE) {
            is_method_call = true;
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
    
    // 引数を評価
    Value *args = NULL;
    if (node->call.arg_count > 0) {
        args = malloc(sizeof(Value) * node->call.arg_count);
        for (int i = 0; i < node->call.arg_count; i++) {
            args[i] = evaluate(eval, node->call.arguments[i]);
            if (eval->had_error) {
                free(args);
                return value_null();
            }
        }
    }
    
    Value result = value_null();
    
    // 組み込み関数
    if (callee.type == VALUE_BUILTIN) {
        // 引数の数をチェック
        int min = callee.builtin.min_args;
        int max = callee.builtin.max_args;
        
        if (node->call.arg_count < min) {
            runtime_error(eval, node->location.line,
                         "%sには少なくとも%d個の引数が必要です",
                         callee.builtin.name, min);
        } else if (max >= 0 && node->call.arg_count > max) {
            runtime_error(eval, node->location.line,
                         "%sの引数は最大%d個です",
                         callee.builtin.name, max);
        } else {
            result = callee.builtin.fn(node->call.arg_count, args);
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
        for (int i = 0; i < expected_count; i++) {
            if (params[i].default_value == NULL) {
                min_required++;
            }
        }
        
        // 引数の数をチェック（デフォルト引数対応）
        if (node->call.arg_count < min_required || node->call.arg_count > expected_count) {
            if (min_required == expected_count) {
                runtime_error(eval, node->location.line,
                             "%sには%d個の引数が必要です（%d個渡されました）",
                             func_name,
                             expected_count,
                             node->call.arg_count);
            } else {
                runtime_error(eval, node->location.line,
                             "%sには%d〜%d個の引数が必要です（%d個渡されました）",
                             func_name,
                             min_required,
                             expected_count,
                             node->call.arg_count);
            }
        } else {
            // 新しいスコープを作成
            Environment *local = env_new(callee.function.closure);
            
            // 引数をバインド（渡された引数 + デフォルト値）
            for (int i = 0; i < expected_count; i++) {
                if (i < node->call.arg_count) {
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
            
            // メソッド呼び出しの場合、current_instanceを設定
            Value *prev_instance = eval->current_instance;
            Value *instance_ptr = NULL;
            if (is_method_call) {
                instance_ptr = malloc(sizeof(Value));
                *instance_ptr = instance;
                eval->current_instance = instance_ptr;
            }
            
            evaluate(eval, body);
            
            // current_instanceを復元
            eval->current_instance = prev_instance;
            if (instance_ptr != NULL) {
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
        if (idx < 0 || idx >= array.array.length) {
            runtime_error(eval, node->location.line,
                         "インデックスが範囲外です: %d（長さ: %d）",
                         idx, array.array.length);
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
        
        if (idx < 0 || idx >= len) {
            runtime_error(eval, node->location.line,
                         "インデックスが範囲外です: %d（長さ: %d）",
                         idx, len);
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
    Value object = evaluate(eval, node->member.object);
    if (eval->had_error) return value_null();
    
    const char *member_name = node->member.member_name;
    
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
    
    runtime_error(eval, node->location.line,
                 "メンバーアクセスはインスタンスまたは辞書に対してのみ使用できます");
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
            return evaluate(eval, node->switch_stmt.case_bodies[i]);
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
        // 辞書のキーをループ
        for (int i = 0; i < iterable.dict.length; i++) {
            if (iterable.dict.keys[i] != NULL) {
                env_define(eval->current, node->foreach_stmt.var_name,
                          value_string(iterable.dict.keys[i]), false);
                
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
        char *str = value_to_string(argv[i]);
        printf("%s", str);
        free(str);
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

static Value builtin_to_number(int argc, Value *argv) {
    (void)argc;
    return value_to_number(argv[0]);
}

static Value builtin_to_string(int argc, Value *argv) {
    (void)argc;
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