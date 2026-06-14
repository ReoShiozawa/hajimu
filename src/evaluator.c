/**
 * 日本語プログラミング言語 - 評価器実装
 */

#include "evaluator.h"
#include "array_grow.h"
#include "parser.h"
#include "http.h"
#include "async.h"
#include "package.h"
#include "diag.h"
#include "bytecode.h"
#include "gc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#if defined(HAJIMU_USE_ACCELERATE)
#  include <Accelerate/Accelerate.h>
#elif defined(HAJIMU_USE_CBLAS)
#  include <cblas.h>
#endif

#define EVALUATOR_PATH_BUFFER_SIZE 1024
#define EVALUATOR_LONG_PATH_BUFFER_SIZE 2048
#define EVALUATOR_INITIAL_IMPORT_CAPACITY 8
#define EVALUATOR_INITIAL_IMPORT_SET_CAPACITY 16
#define STRING_INTERPOLATION_INITIAL_CAPACITY 256

/* ── プラットフォーム依存ヘッダー ───────────────────────────── */
#ifdef _WIN32
#  include "win_regex.h"   /* POSIX regex エミュレーション */
#  include "win_compat2.h" /* dirent, usleep, realpath, setenv, mkdir */
#else
#  include <unistd.h>
#  include <regex.h>
#  include <sys/stat.h>
#  include <dirent.h>
#endif

// グローバルevaluatorポインタ（高階関数・toStringプロトコル用）
static Evaluator *g_eval = NULL;

// スレッドごとの現在評価器。
// 非同期ワーカーでは detached 評価器をここに入れ、メイン評価器を書き換えない。
#if defined(_MSC_VER)
#  define HAJIMU_THREAD_LOCAL __declspec(thread)
#else
#  define HAJIMU_THREAD_LOCAL __thread
#endif
static HAJIMU_THREAD_LOCAL Evaluator *g_thread_eval = NULL;

// グローバルGCインスタンス
static GC g_gc_state;
GC *g_gc = &g_gc_state;

static double evaluator_now_ms(void) {
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

static void maybe_collect_gc(void) {
    if (g_gc != NULL && g_gc->tracked_count > g_gc->threshold) {
        gc_collect(g_gc);
    }
}

// =============================================================================
// プラグインランタイムコールバック
// =============================================================================

/**
 * プラグインからはじむ関数を呼び出すためのコールバック。
 * VALUE_FUNCTION と VALUE_BUILTIN の両方に対応。
 */
static Value plugin_call_function(Value *func, int argc, Value *argv) {
    if (g_eval == NULL || func == NULL) return value_null();
    
    if (func->type == VALUE_BUILTIN) {
        return func->builtin.fn(argc, argv);
    }
    if (func->type == VALUE_FUNCTION) {
        /* call_function_value 相当の処理を直接実行 */
        ASTNode *def = func->function.definition;
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
        
        /* 引数が足りない場合はデフォルト値で埋める */
        Environment *local = env_new(func->function.closure);
        for (int i = 0; i < param_count; i++) {
            if (i < argc) {
                env_define(local, params[i].name, value_copy(argv[i]), false);
            } else if (params[i].default_value) {
                Value def_val = evaluate(g_eval, params[i].default_value);
                env_define(local, params[i].name, def_val, false);
            } else {
                env_define(local, params[i].name, value_null(), false);
            }
        }
        
        Environment *prev = g_eval->current;
        g_eval->current = local;
        
        Value result = evaluate(g_eval, body);
        
        if (g_eval->returning) {
            result = g_eval->return_value;
            g_eval->returning = false;
        }
        
        g_eval->current = prev;
        env_release(local);
        maybe_collect_gc();
        return result;
    }
    return value_null();
}

static HajimuRuntime g_plugin_runtime = {
    .call = plugin_call_function,
};

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
static Value evaluate_import(Evaluator *eval, ASTNode *node);
static Value evaluate_import_plugin(Evaluator *eval, ASTNode *node,
                                     const char *resolved_path);
static Value evaluate_import_hjpb(Evaluator *eval, ASTNode *node,
                                   const char *hjp_path);
static Value evaluate_source_as_module(Evaluator *eval, ASTNode *node,
                                       const char *source, const char *source_path,
                                       const char *alias);
static Value evaluate_class_def(Evaluator *eval, ASTNode *node);
static Value evaluate_new(Evaluator *eval, ASTNode *node);
static Value evaluate_try(Evaluator *eval, ASTNode *node);
static Value evaluate_throw(Evaluator *eval, ASTNode *node);
static Value evaluate_lambda(Evaluator *eval, ASTNode *node);
static Value evaluate_switch(Evaluator *eval, ASTNode *node);
static Value evaluate_foreach(Evaluator *eval, ASTNode *node);
static Value evaluate_list_comprehension(Evaluator *eval, ASTNode *node);
static Value evaluate_string_interpolation(Evaluator *eval, const char *str, int line, int column);
static Value call_function_value(Value *func, Value *args, int argc);
static void undefined_variable_error(Evaluator *eval, ASTNode *node, const char *name);
static bool is_protected_runtime_name(const char *name);
static const char *protected_runtime_name_kind(const char *name);
static void protected_runtime_name_error(Evaluator *eval, ASTNode *node,
                                         const char *name, const char *action);
static bool require_integer_index(Evaluator *eval, ASTNode *node, Value index, const char *target_name);
static const char *find_similar_dict_key(Value *dict, const char *name);
static const char *find_similar_instance_member(Value *instance, const char *name);
static const char *find_similar_class_static_method(ASTNode *class_def, const char *name);

// =============================================================================
// 組み込み関数のプロトタイプ
// =============================================================================

static Value builtin_print(int argc, Value *argv);
static Value builtin_input(int argc, Value *argv);
static Value builtin_length(int argc, Value *argv);
static Value builtin_append(int argc, Value *argv);
static Value builtin_remove(int argc, Value *argv);
static Value builtin_type(int argc, Value *argv);
static Value builtin_dtype(int argc, Value *argv);
static Value builtin_dtype_size(int argc, Value *argv);
static Value builtin_nbytes(int argc, Value *argv);
static Value builtin_storage_bytes(int argc, Value *argv);
static Value builtin_astype(int argc, Value *argv);
static Value builtin_to_number(int argc, Value *argv);
static Value builtin_to_string(int argc, Value *argv);
static Value builtin_to_array(int argc, Value *argv);
static Value builtin_abs(int argc, Value *argv);
static Value builtin_sqrt(int argc, Value *argv);
static Value builtin_floor(int argc, Value *argv);
static Value builtin_ceil(int argc, Value *argv);
static Value builtin_round(int argc, Value *argv);
static Value builtin_random(int argc, Value *argv);
static Value builtin_max(int argc, Value *argv);
static Value builtin_min(int argc, Value *argv);
static Value builtin_vector(int argc, Value *argv);
static Value builtin_zeros(int argc, Value *argv);
static Value builtin_ones(int argc, Value *argv);
static Value builtin_range_vector(int argc, Value *argv);
static Value builtin_vector_sum(int argc, Value *argv);
static Value builtin_mean(int argc, Value *argv);
static Value builtin_variance(int argc, Value *argv);
static Value builtin_std(int argc, Value *argv);
static Value builtin_quantile(int argc, Value *argv);
static Value builtin_median(int argc, Value *argv);
static Value builtin_normalize(int argc, Value *argv);
static Value builtin_norm(int argc, Value *argv);
static Value builtin_minmax_scale(int argc, Value *argv);
static Value builtin_clip(int argc, Value *argv);
static Value builtin_covariance(int argc, Value *argv);
static Value builtin_correlation(int argc, Value *argv);
static Value builtin_histogram(int argc, Value *argv);
static Value builtin_train_test_split(int argc, Value *argv);
static Value builtin_drop_missing(int argc, Value *argv);
static Value builtin_fill_missing(int argc, Value *argv);
static Value builtin_is_nan(int argc, Value *argv);
static Value builtin_mse(int argc, Value *argv);
static Value builtin_mae(int argc, Value *argv);
static Value builtin_r2_score(int argc, Value *argv);
static Value builtin_accuracy(int argc, Value *argv);
static Value builtin_precision(int argc, Value *argv);
static Value builtin_recall(int argc, Value *argv);
static Value builtin_f1_score(int argc, Value *argv);
static Value builtin_confusion_matrix(int argc, Value *argv);
static Value builtin_vector_add(int argc, Value *argv);
static Value builtin_vector_sub(int argc, Value *argv);
static Value builtin_vector_mul(int argc, Value *argv);
static Value builtin_vector_div(int argc, Value *argv);
static Value builtin_vector_abs(int argc, Value *argv);
static Value builtin_vector_sqrt(int argc, Value *argv);
static Value builtin_vector_sin(int argc, Value *argv);
static Value builtin_vector_cos(int argc, Value *argv);
static Value builtin_vector_log(int argc, Value *argv);
static Value builtin_dot(int argc, Value *argv);
static Value builtin_matrix(int argc, Value *argv);
static Value builtin_shape(int argc, Value *argv);
static Value builtin_matrix_get(int argc, Value *argv);
static Value builtin_matrix_set(int argc, Value *argv);
static Value builtin_matrix_row(int argc, Value *argv);
static Value builtin_matrix_column(int argc, Value *argv);
static Value builtin_transpose(int argc, Value *argv);
static Value builtin_matmul(int argc, Value *argv);
static Value builtin_matrix_add(int argc, Value *argv);
static Value builtin_matrix_sub(int argc, Value *argv);
static Value builtin_matrix_scale(int argc, Value *argv);
static Value builtin_matrix_hadamard(int argc, Value *argv);
static Value builtin_identity(int argc, Value *argv);
static Value builtin_determinant(int argc, Value *argv);
static Value builtin_inverse(int argc, Value *argv);
static Value builtin_solve_linear(int argc, Value *argv);
static Value builtin_linear_regression(int argc, Value *argv);
static Value builtin_predict_linear(int argc, Value *argv);
static Value builtin_kmeans(int argc, Value *argv);
static Value builtin_knn_predict(int argc, Value *argv);
static Value builtin_logistic_regression(int argc, Value *argv);
static Value builtin_predict_logistic(int argc, Value *argv);
static Value builtin_predict_logistic_class(int argc, Value *argv);
static Value builtin_read_csv_numeric(int argc, Value *argv);
static Value builtin_read_tsv_numeric(int argc, Value *argv);
static Value builtin_read_csv(int argc, Value *argv);
static Value builtin_csv_column(int argc, Value *argv);
static Value builtin_read_json_lines(int argc, Value *argv);
static Value builtin_describe(int argc, Value *argv);

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
static Value builtin_is_numeric_array(int argc, Value *argv);
static Value builtin_is_matrix(int argc, Value *argv);
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

// 演算子オーバーロード
static Value call_instance_operator(Value *instance, const char *method_name, Value *arg);

// セット（集合）関数
static Value builtin_set_create(int argc, Value *argv);
static Value builtin_set_add(int argc, Value *argv);
static Value builtin_set_remove(int argc, Value *argv);
static Value builtin_set_contains(int argc, Value *argv);
static Value builtin_set_union(int argc, Value *argv);
static Value builtin_set_intersection(int argc, Value *argv);
static Value builtin_set_difference(int argc, Value *argv);

// テストフレームワーク
static Value builtin_test_register(int argc, Value *argv);
static Value builtin_test_run(int argc, Value *argv);
static Value builtin_expect(int argc, Value *argv);
static Value builtin_expect_error(int argc, Value *argv);
static Value builtin_create_exception(int argc, Value *argv);

// ドキュメントコメント
static Value builtin_doc_set(int argc, Value *argv);
static Value builtin_doc_get(int argc, Value *argv);
static Value builtin_type_alias(int argc, Value *argv);

// ドキュメントレジストリ
typedef struct {
    char *name;
    char *doc;
} DocEntry;

#define MAX_DOCS 256
static DocEntry g_docs[MAX_DOCS];
static int g_doc_count = 0;

// 型エイリアスレジストリ
typedef struct {
    char *alias;
    char *original;
} TypeAlias;

#define MAX_TYPE_ALIASES 128
static TypeAlias g_type_aliases[MAX_TYPE_ALIASES];
static int g_type_alias_count = 0;

// テストレジストリ
typedef struct {
    char *name;
    Value func;
} TestCase;

#define MAX_TESTS 256
static TestCase g_tests[MAX_TESTS];
static int g_test_count = 0;
static int g_expect_failures = 0;
static char g_expect_messages[4096];
static int g_expect_msg_len = 0;

// =============================================================================
// 評価器の初期化・解放
// =============================================================================

static Evaluator *evaluator_new_internal(bool owns_runtime_context) {
    Evaluator *eval = calloc(1, sizeof(Evaluator));
    if (owns_runtime_context) {
        gc_init(g_gc);
    }
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
    eval->ast_profile_enabled = false;
    eval->ast_profile_entries = NULL;
    eval->ast_profile_count = 0;
    eval->ast_profile_capacity = 0;
    eval->owns_runtime_context = owns_runtime_context;
    
    if (owns_runtime_context) {
        g_eval = eval;  // グローバルポインタ設定
    }
    g_thread_eval = eval;
    
    // インポートモジュールの初期化
    eval->imported_modules = NULL;
    eval->imported_count = 0;
    eval->imported_capacity = 0;
    
    // ファイルパス追跡
    eval->current_file = NULL;
    
    // インポート済みパスキャッシュ
    eval->imported_paths = NULL;
    eval->imported_path_count = 0;
    eval->imported_path_capacity = 0;
    eval->imported_path_entries = NULL;
    eval->imported_path_entry_count = 0;
    eval->imported_path_entry_capacity = 0;
    
    // プラグインマネージャの初期化
    plugin_manager_init(&eval->plugin_manager);
    
    // 乱数初期化
    srand((unsigned int)time(NULL));
    
    // 組み込み関数を登録
    register_builtins(eval);
    
    return eval;
}

Evaluator *evaluator_new(void) {
    return evaluator_new_internal(true);
}

Evaluator *evaluator_new_detached(void) {
    return evaluator_new_internal(false);
}

void evaluator_free(Evaluator *eval) {
    if (eval == NULL) return;
    
    if (eval->owns_runtime_context && eval == g_eval) {
        async_runtime_cleanup();
        g_eval = NULL;
    }
    if (eval == g_thread_eval) {
        g_thread_eval = NULL;
    }
    
    // インポートされたモジュールを解放
    for (int i = 0; i < eval->imported_count; i++) {
        free(eval->imported_modules[i].source);
        node_free(eval->imported_modules[i].ast);
    }
    free(eval->imported_modules);
    
    // インポート済みパスキャッシュを解放
    for (int i = 0; i < eval->imported_path_count; i++) {
        free(eval->imported_paths[i]);
    }
    free(eval->imported_paths);
    free(eval->imported_path_entries);
    
    // プラグインマネージャを解放
    plugin_manager_free(&eval->plugin_manager);
    free(eval->ast_profile_entries);
    
    if (eval->owns_runtime_context) {
        gc_collect(g_gc);
    }
    env_release(eval->global);
    if (eval->owns_runtime_context) {
        gc_shutdown(g_gc);
    }
    free(eval);
}

Evaluator *evaluator_current(void) {
    return g_thread_eval != NULL ? g_thread_eval : g_eval;
}

void evaluator_set_ast_profile_enabled(Evaluator *eval, bool enabled) {
    if (eval != NULL) {
        eval->ast_profile_enabled = enabled;
    }
}

static AstProfileEntry *find_or_add_ast_profile_entry(Evaluator *eval, const ASTNode *node) {
    if (eval == NULL || node == NULL) return NULL;

    for (int i = 0; i < eval->ast_profile_count; i++) {
        if (eval->ast_profile_entries[i].node == node) {
            return &eval->ast_profile_entries[i];
        }
    }

    if (eval->ast_profile_count >= eval->ast_profile_capacity) {
        int new_capacity = eval->ast_profile_capacity < 16 ? 16 : eval->ast_profile_capacity * 2;
        AstProfileEntry *entries = realloc(eval->ast_profile_entries,
                                           sizeof(AstProfileEntry) * (size_t)new_capacity);
        if (entries == NULL) return NULL;
        eval->ast_profile_entries = entries;
        eval->ast_profile_capacity = new_capacity;
    }

    AstProfileEntry *entry = &eval->ast_profile_entries[eval->ast_profile_count++];
    entry->node = node;
    entry->type = node->type;
    entry->line = node->location.line;
    entry->column = node->location.column;
    entry->count = 0;
    entry->total_ms = 0.0;
    entry->max_ms = 0.0;
    return entry;
}

static void record_ast_profile(Evaluator *eval, const ASTNode *node, double elapsed_ms) {
    if (eval == NULL || !eval->ast_profile_enabled || node == NULL) return;
    AstProfileEntry *entry = find_or_add_ast_profile_entry(eval, node);
    if (entry == NULL) return;
    entry->count++;
    entry->total_ms += elapsed_ms;
    if (elapsed_ms > entry->max_ms) {
        entry->max_ms = elapsed_ms;
    }
}

static int compare_ast_profile_entries(const void *a, const void *b) {
    const AstProfileEntry *left = *(const AstProfileEntry * const *)a;
    const AstProfileEntry *right = *(const AstProfileEntry * const *)b;
    if (left->total_ms < right->total_ms) return 1;
    if (left->total_ms > right->total_ms) return -1;
    if (left->count < right->count) return 1;
    if (left->count > right->count) return -1;
    return 0;
}

void evaluator_print_ast_profile(Evaluator *eval, int limit) {
    if (eval == NULL || eval->ast_profile_count <= 0) {
        fprintf(stderr, "ASTプロファイル: 記録なし\n");
        return;
    }

    if (limit <= 0 || limit > eval->ast_profile_count) {
        limit = eval->ast_profile_count;
    }

    AstProfileEntry **sorted = malloc(sizeof(AstProfileEntry *) * (size_t)eval->ast_profile_count);
    if (sorted == NULL) {
        fprintf(stderr, "ASTプロファイル: 出力用メモリを確保できませんでした\n");
        return;
    }

    for (int i = 0; i < eval->ast_profile_count; i++) {
        sorted[i] = &eval->ast_profile_entries[i];
    }
    qsort(sorted, (size_t)eval->ast_profile_count, sizeof(AstProfileEntry *),
          compare_ast_profile_entries);

    fprintf(stderr, "ASTプロファイル（上位%d件 / 全%d件）:\n", limit, eval->ast_profile_count);
    fprintf(stderr, "  %-22s %8s %12s %12s %10s\n", "node", "count", "total_ms", "max_ms", "line:col");
    for (int i = 0; i < limit; i++) {
        AstProfileEntry *entry = sorted[i];
        fprintf(stderr, "  %-22s %8ld %12.3f %12.3f %5d:%-4d\n",
                node_type_name(entry->type),
                entry->count,
                entry->total_ms,
                entry->max_ms,
                entry->line,
                entry->column);
    }
    free(sorted);
}

// =============================================================================
// 組み込み関数の登録
// =============================================================================

typedef struct {
    const char *name;
    BuiltinFn fn;
    int min_args;
    int max_args;
} BuiltinEntry;

static const BuiltinEntry builtin_entries[] = {
    {"表示", builtin_print, 0, -1},
    {"print", builtin_print, 0, -1},
    {"println", builtin_print, 0, -1},
    {"入力", builtin_input, 0, 1},
    {"input", builtin_input, 0, 1},
    {"長さ", builtin_length, 1, 1},
    {"len", builtin_length, 1, 1},
    {"length", builtin_length, 1, 1},
    {"追加", builtin_append, 2, 2},
    {"append", builtin_append, 2, 2},
    {"push", builtin_append, 2, 2},
    {"削除", builtin_remove, 2, 2},
    {"remove", builtin_remove, 2, 2},
    {"delete", builtin_remove, 2, 2},
    {"型", builtin_type, 1, 1},
    {"typeof", builtin_type, 1, 1},
    {"データ型", builtin_dtype, 1, 1},
    {"dtype", builtin_dtype, 1, 1},
    {"データ型サイズ", builtin_dtype_size, 1, 1},
    {"dtype_size", builtin_dtype_size, 1, 1},
    {"論理バイト数", builtin_nbytes, 1, 1},
    {"nbytes", builtin_nbytes, 1, 1},
    {"保存バイト数", builtin_storage_bytes, 1, 1},
    {"storage_bytes", builtin_storage_bytes, 1, 1},
    {"型変換", builtin_astype, 2, 2},
    {"astype", builtin_astype, 2, 2},
    {"数値化", builtin_to_number, 1, 1},
    {"to_number", builtin_to_number, 1, 1},
    {"文字列化", builtin_to_string, 1, 1},
    {"to_string", builtin_to_string, 1, 1},
    {"配列化", builtin_to_array, 1, 1},
    {"to_array", builtin_to_array, 1, 1},
    {"数値か", builtin_is_number, 1, 1},
    {"is_number", builtin_is_number, 1, 1},
    {"文字列か", builtin_is_string, 1, 1},
    {"is_string", builtin_is_string, 1, 1},
    {"真偽か", builtin_is_bool, 1, 1},
    {"is_bool", builtin_is_bool, 1, 1},
    {"is_boolean", builtin_is_bool, 1, 1},
    {"配列か", builtin_is_array, 1, 1},
    {"is_array", builtin_is_array, 1, 1},
    {"is_list", builtin_is_array, 1, 1},
    {"数値ベクトルか", builtin_is_numeric_array, 1, 1},
    {"is_numeric_array", builtin_is_numeric_array, 1, 1},
    {"is_vector", builtin_is_numeric_array, 1, 1},
    {"行列か", builtin_is_matrix, 1, 1},
    {"is_matrix", builtin_is_matrix, 1, 1},
    {"辞書か", builtin_is_dict, 1, 1},
    {"is_dict", builtin_is_dict, 1, 1},
    {"is_object", builtin_is_dict, 1, 1},
    {"関数か", builtin_is_function, 1, 1},
    {"is_function", builtin_is_function, 1, 1},
    {"無か", builtin_is_null, 1, 1},
    {"is_null", builtin_is_null, 1, 1},
    {"範囲", builtin_range, 1, 3},
    {"range", builtin_range, 1, 3},
    {"ビット積", builtin_bit_and, 2, 2},
    {"bit_and", builtin_bit_and, 2, 2},
    {"ビット和", builtin_bit_or, 2, 2},
    {"bit_or", builtin_bit_or, 2, 2},
    {"ビット排他", builtin_bit_xor, 2, 2},
    {"bit_xor", builtin_bit_xor, 2, 2},
    {"ビット否定", builtin_bit_not, 1, 1},
    {"bit_not", builtin_bit_not, 1, 1},
    {"左シフト", builtin_bit_lshift, 2, 2},
    {"left_shift", builtin_bit_lshift, 2, 2},
    {"右シフト", builtin_bit_rshift, 2, 2},
    {"right_shift", builtin_bit_rshift, 2, 2},
    {"部分文字列", builtin_substring, 2, 3},
    {"substring", builtin_substring, 2, 3},
    {"slice_text", builtin_substring, 2, 3},
    {"始まる", builtin_starts_with, 2, 2},
    {"starts_with", builtin_starts_with, 2, 2},
    {"startsWith", builtin_starts_with, 2, 2},
    {"終わる", builtin_ends_with, 2, 2},
    {"ends_with", builtin_ends_with, 2, 2},
    {"endsWith", builtin_ends_with, 2, 2},
    {"文字コード", builtin_char_code, 1, 2},
    {"char_code", builtin_char_code, 1, 2},
    {"charCodeAt", builtin_char_code, 1, 2},
    {"コード文字", builtin_from_char_code, 1, 1},
    {"from_char_code", builtin_from_char_code, 1, 1},
    {"fromCharCode", builtin_from_char_code, 1, 1},
    {"繰り返し", builtin_string_repeat, 2, 2},
    {"repeat_string", builtin_string_repeat, 2, 2},
    {"末尾削除", builtin_pop, 1, 1},
    {"pop", builtin_pop, 1, 1},
    {"探す", builtin_find_item, 2, 2},
    {"find_item", builtin_find_item, 2, 2},
    {"全て", builtin_every, 2, 2},
    {"every", builtin_every, 2, 2},
    {"一つでも", builtin_some, 2, 2},
    {"some", builtin_some, 2, 2},
    {"一意", builtin_unique, 1, 1},
    {"unique", builtin_unique, 1, 1},
    {"圧縮", builtin_zip, 2, 2},
    {"zip", builtin_zip, 2, 2},
    {"平坦化", builtin_flat, 1, 1},
    {"flat", builtin_flat, 1, 1},
    {"挿入", builtin_insert, 3, 3},
    {"insert", builtin_insert, 3, 3},
    {"比較ソート", builtin_sort_by, 2, 2},
    {"sort_by", builtin_sort_by, 2, 2},
    {"正弦", builtin_sin, 1, 1},
    {"sin", builtin_sin, 1, 1},
    {"余弦", builtin_cos, 1, 1},
    {"cos", builtin_cos, 1, 1},
    {"正接", builtin_tan, 1, 1},
    {"tan", builtin_tan, 1, 1},
    {"対数", builtin_log, 1, 1},
    {"log", builtin_log, 1, 1},
    {"常用対数", builtin_log10_fn, 1, 1},
    {"log10", builtin_log10_fn, 1, 1},
    {"乱数整数", builtin_random_int, 2, 2},
    {"random_int", builtin_random_int, 2, 2},
    {"randint", builtin_random_int, 2, 2},
    {"追記", builtin_file_append, 2, 2},
    {"append_file", builtin_file_append, 2, 2},
    {"ディレクトリ一覧", builtin_dir_list, 1, 1},
    {"list_dir", builtin_dir_list, 1, 1},
    {"ディレクトリ作成", builtin_dir_create, 1, 1},
    {"make_dir", builtin_dir_create, 1, 1},
    {"mkdir", builtin_dir_create, 1, 1},
    {"表明", builtin_assert, 1, 2},
    {"assert", builtin_assert, 1, 2},
    {"型判定", builtin_typeof_check, 2, 2},
    {"instanceof", builtin_typeof_check, 2, 2},
    {"is_instance", builtin_typeof_check, 2, 2},
    {"絶対値", builtin_abs, 1, 1},
    {"abs", builtin_abs, 1, 1},
    {"平方根", builtin_sqrt, 1, 1},
    {"sqrt", builtin_sqrt, 1, 1},
    {"切り捨て", builtin_floor, 1, 1},
    {"floor", builtin_floor, 1, 1},
    {"切り上げ", builtin_ceil, 1, 1},
    {"ceil", builtin_ceil, 1, 1},
    {"四捨五入", builtin_round, 1, 1},
    {"round", builtin_round, 1, 1},
    {"乱数", builtin_random, 0, 0},
    {"random", builtin_random, 0, 0},
    {"最大", builtin_max, 1, -1},
    {"max", builtin_max, 1, -1},
    {"最小", builtin_min, 1, -1},
    {"min", builtin_min, 1, -1},
    {"ベクトル", builtin_vector, 1, 1},
    {"vector", builtin_vector, 1, 1},
    {"ゼロ配列", builtin_zeros, 1, 1},
    {"zeros", builtin_zeros, 1, 1},
    {"一配列", builtin_ones, 1, 1},
    {"ones", builtin_ones, 1, 1},
    {"範囲ベクトル", builtin_range_vector, 1, 3},
    {"range_vector", builtin_range_vector, 1, 3},
    {"ベクトル合計", builtin_vector_sum, 1, 1},
    {"vector_sum", builtin_vector_sum, 1, 1},
    {"平均", builtin_mean, 1, 1},
    {"mean", builtin_mean, 1, 1},
    {"分散", builtin_variance, 1, 1},
    {"variance", builtin_variance, 1, 1},
    {"標準偏差", builtin_std, 1, 1},
    {"std", builtin_std, 1, 1},
    {"分位点", builtin_quantile, 2, 2},
    {"quantile", builtin_quantile, 2, 2},
    {"中央値", builtin_median, 1, 1},
    {"median", builtin_median, 1, 1},
    {"標準化", builtin_normalize, 1, 1},
    {"normalize", builtin_normalize, 1, 1},
    {"Zスコア", builtin_normalize, 1, 1},
    {"z_score", builtin_normalize, 1, 1},
    {"ノルム", builtin_norm, 1, 2},
    {"norm", builtin_norm, 1, 2},
    {"最小最大スケール", builtin_minmax_scale, 1, 1},
    {"minmax_scale", builtin_minmax_scale, 1, 1},
    {"クリップ", builtin_clip, 3, 3},
    {"clip", builtin_clip, 3, 3},
    {"共分散", builtin_covariance, 2, 2},
    {"covariance", builtin_covariance, 2, 2},
    {"相関", builtin_correlation, 2, 2},
    {"correlation", builtin_correlation, 2, 2},
    {"ヒストグラム", builtin_histogram, 2, 2},
    {"histogram", builtin_histogram, 2, 2},
    {"訓練テスト分割", builtin_train_test_split, 2, 3},
    {"train_test_split", builtin_train_test_split, 2, 3},
    {"欠損削除", builtin_drop_missing, 1, 1},
    {"drop_missing", builtin_drop_missing, 1, 1},
    {"欠損補完", builtin_fill_missing, 2, 2},
    {"fill_missing", builtin_fill_missing, 2, 2},
    {"NaNか", builtin_is_nan, 1, 1},
    {"is_nan", builtin_is_nan, 1, 1},
    {"平均二乗誤差", builtin_mse, 2, 2},
    {"mse", builtin_mse, 2, 2},
    {"平均絶対誤差", builtin_mae, 2, 2},
    {"mae", builtin_mae, 2, 2},
    {"決定係数", builtin_r2_score, 2, 2},
    {"r2_score", builtin_r2_score, 2, 2},
    {"正解率", builtin_accuracy, 2, 2},
    {"accuracy", builtin_accuracy, 2, 2},
    {"適合率", builtin_precision, 2, 2},
    {"precision", builtin_precision, 2, 2},
    {"再現率", builtin_recall, 2, 2},
    {"recall", builtin_recall, 2, 2},
    {"F1スコア", builtin_f1_score, 2, 2},
    {"f1_score", builtin_f1_score, 2, 2},
    {"混同行列", builtin_confusion_matrix, 2, 2},
    {"confusion_matrix", builtin_confusion_matrix, 2, 2},
    {"ベクトル加算", builtin_vector_add, 2, 2},
    {"vector_add", builtin_vector_add, 2, 2},
    {"ベクトル減算", builtin_vector_sub, 2, 2},
    {"vector_sub", builtin_vector_sub, 2, 2},
    {"ベクトル乗算", builtin_vector_mul, 2, 2},
    {"vector_mul", builtin_vector_mul, 2, 2},
    {"ベクトル除算", builtin_vector_div, 2, 2},
    {"vector_div", builtin_vector_div, 2, 2},
    {"ベクトル絶対値", builtin_vector_abs, 1, 1},
    {"vector_abs", builtin_vector_abs, 1, 1},
    {"ベクトル平方根", builtin_vector_sqrt, 1, 1},
    {"vector_sqrt", builtin_vector_sqrt, 1, 1},
    {"ベクトル正弦", builtin_vector_sin, 1, 1},
    {"vector_sin", builtin_vector_sin, 1, 1},
    {"ベクトル余弦", builtin_vector_cos, 1, 1},
    {"vector_cos", builtin_vector_cos, 1, 1},
    {"ベクトル対数", builtin_vector_log, 1, 1},
    {"vector_log", builtin_vector_log, 1, 1},
    {"内積", builtin_dot, 2, 2},
    {"dot", builtin_dot, 2, 2},
    {"行列", builtin_matrix, 1, 1},
    {"matrix", builtin_matrix, 1, 1},
    {"形状", builtin_shape, 1, 1},
    {"shape", builtin_shape, 1, 1},
    {"行列取得", builtin_matrix_get, 3, 3},
    {"matrix_get", builtin_matrix_get, 3, 3},
    {"行列設定", builtin_matrix_set, 4, 4},
    {"matrix_set", builtin_matrix_set, 4, 4},
    {"行取得", builtin_matrix_row, 2, 2},
    {"matrix_row", builtin_matrix_row, 2, 2},
    {"列取得", builtin_matrix_column, 2, 2},
    {"matrix_column", builtin_matrix_column, 2, 2},
    {"転置", builtin_transpose, 1, 1},
    {"transpose", builtin_transpose, 1, 1},
    {"行列積", builtin_matmul, 2, 2},
    {"matmul", builtin_matmul, 2, 2},
    {"行列加算", builtin_matrix_add, 2, 2},
    {"matrix_add", builtin_matrix_add, 2, 2},
    {"行列減算", builtin_matrix_sub, 2, 2},
    {"matrix_sub", builtin_matrix_sub, 2, 2},
    {"行列スケール", builtin_matrix_scale, 2, 2},
    {"matrix_scale", builtin_matrix_scale, 2, 2},
    {"行列要素積", builtin_matrix_hadamard, 2, 2},
    {"matrix_hadamard", builtin_matrix_hadamard, 2, 2},
    {"単位行列", builtin_identity, 1, 1},
    {"identity", builtin_identity, 1, 1},
    {"行列式", builtin_determinant, 1, 1},
    {"determinant", builtin_determinant, 1, 1},
    {"逆行列", builtin_inverse, 1, 1},
    {"inverse", builtin_inverse, 1, 1},
    {"線形方程式を解く", builtin_solve_linear, 2, 2},
    {"solve_linear", builtin_solve_linear, 2, 2},
    {"solve", builtin_solve_linear, 2, 2},
    {"線形回帰", builtin_linear_regression, 2, 3},
    {"linear_regression", builtin_linear_regression, 2, 3},
    {"線形予測", builtin_predict_linear, 2, 2},
    {"predict_linear", builtin_predict_linear, 2, 2},
    {"k平均法", builtin_kmeans, 2, 3},
    {"kmeans", builtin_kmeans, 2, 3},
    {"k近傍予測", builtin_knn_predict, 4, 4},
    {"knn_predict", builtin_knn_predict, 4, 4},
    {"ロジスティック回帰", builtin_logistic_regression, 2, 4},
    {"logistic_regression", builtin_logistic_regression, 2, 4},
    {"ロジスティック予測", builtin_predict_logistic, 2, 2},
    {"predict_logistic", builtin_predict_logistic, 2, 2},
    {"ロジスティック分類", builtin_predict_logistic_class, 2, 3},
    {"predict_logistic_class", builtin_predict_logistic_class, 2, 3},
    {"CSV数値読込", builtin_read_csv_numeric, 1, 3},
    {"read_csv_numeric", builtin_read_csv_numeric, 1, 3},
    {"TSV数値読込", builtin_read_tsv_numeric, 1, 3},
    {"read_tsv_numeric", builtin_read_tsv_numeric, 1, 3},
    {"CSV読込", builtin_read_csv, 1, 2},
    {"read_csv", builtin_read_csv, 1, 2},
    {"CSV列", builtin_csv_column, 2, 2},
    {"csv_column", builtin_csv_column, 2, 2},
    {"JSON行読込", builtin_read_json_lines, 1, 2},
    {"JSONL読込", builtin_read_json_lines, 1, 2},
    {"read_json_lines", builtin_read_json_lines, 1, 2},
    {"要約", builtin_describe, 1, 1},
    {"describe", builtin_describe, 1, 1},
    {"キー", builtin_dict_keys, 1, 1},
    {"keys", builtin_dict_keys, 1, 1},
    {"値一覧", builtin_dict_values, 1, 1},
    {"values", builtin_dict_values, 1, 1},
    {"含む", builtin_dict_has, 2, 2},
    {"has", builtin_dict_has, 2, 2},
    {"分割", builtin_split, 2, 2},
    {"split", builtin_split, 2, 2},
    {"結合", builtin_join, 2, 2},
    {"join", builtin_join, 2, 2},
    {"検索", builtin_find, 2, 2},
    {"find", builtin_find, 2, 2},
    {"置換", builtin_replace, 3, 3},
    {"replace", builtin_replace, 3, 3},
    {"大文字", builtin_upper, 1, 1},
    {"upper", builtin_upper, 1, 1},
    {"小文字", builtin_lower, 1, 1},
    {"lower", builtin_lower, 1, 1},
    {"空白除去", builtin_trim, 1, 1},
    {"trim", builtin_trim, 1, 1},
    {"ソート", builtin_sort, 1, 1},
    {"sort", builtin_sort, 1, 1},
    {"逆順", builtin_reverse, 1, 1},
    {"reverse", builtin_reverse, 1, 1},
    {"スライス", builtin_slice, 2, 3},
    {"slice", builtin_slice, 2, 3},
    {"位置", builtin_index_of, 2, 2},
    {"index_of", builtin_index_of, 2, 2},
    {"存在", builtin_contains, 2, 2},
    {"contains", builtin_contains, 2, 2},
    {"読み込む", builtin_file_read, 1, 1},
    {"read_file", builtin_file_read, 1, 1},
    {"書き込む", builtin_file_write, 2, 2},
    {"write_file", builtin_file_write, 2, 2},
    {"ファイル存在", builtin_file_exists, 1, 1},
    {"file_exists", builtin_file_exists, 1, 1},
    {"現在時刻", builtin_now, 0, 0},
    {"now", builtin_now, 0, 0},
    {"日付", builtin_date, 0, 1},
    {"date", builtin_date, 0, 1},
    {"時間", builtin_time, 0, 1},
    {"time", builtin_time, 0, 1},
    {"JSON化", builtin_json_encode, 1, 1},
    {"json_encode", builtin_json_encode, 1, 1},
    {"JSON解析", builtin_json_decode, 1, 1},
    {"json_decode", builtin_json_decode, 1, 1},
    {"HTTP取得", builtin_http_get, 1, 2},
    {"http_get", builtin_http_get, 1, 2},
    {"HTTP送信", builtin_http_post, 1, 3},
    {"http_post", builtin_http_post, 1, 3},
    {"HTTP更新", builtin_http_put, 1, 3},
    {"http_put", builtin_http_put, 1, 3},
    {"HTTP削除", builtin_http_delete, 1, 2},
    {"http_delete", builtin_http_delete, 1, 2},
    {"HTTPリクエスト", builtin_http_request, 2, 4},
    {"http_request", builtin_http_request, 2, 4},
    {"サーバー起動", builtin_http_serve, 1, 2},
    {"server_start", builtin_http_serve, 1, 2},
    {"serve", builtin_http_serve, 1, 2},
    {"サーバー停止", builtin_http_stop, 0, 0},
    {"server_stop", builtin_http_stop, 0, 0},
    {"URLエンコード", builtin_url_encode, 1, 1},
    {"url_encode", builtin_url_encode, 1, 1},
    {"URLデコード", builtin_url_decode, 1, 1},
    {"url_decode", builtin_url_decode, 1, 1},
    {"変換", builtin_map, 2, 2},
    {"map", builtin_map, 2, 2},
    {"抽出", builtin_filter, 2, 2},
    {"filter", builtin_filter, 2, 2},
    {"集約", builtin_reduce, 3, 3},
    {"reduce", builtin_reduce, 3, 3},
    {"反復", builtin_foreach, 2, 2},
    {"for_each_value", builtin_foreach, 2, 2},
    {"正規一致", builtin_regex_match, 2, 2},
    {"regex_match", builtin_regex_match, 2, 2},
    {"正規検索", builtin_regex_search, 2, 2},
    {"regex_search", builtin_regex_search, 2, 2},
    {"正規置換", builtin_regex_replace, 3, 3},
    {"regex_replace", builtin_regex_replace, 3, 3},
    {"待つ", builtin_sleep, 1, 1},
    {"sleep", builtin_sleep, 1, 1},
    {"実行", builtin_exec, 1, 1},
    {"exec", builtin_exec, 1, 1},
    {"環境変数", builtin_env_get, 1, 1},
    {"env_get", builtin_env_get, 1, 1},
    {"get_env", builtin_env_get, 1, 1},
    {"環境変数設定", builtin_env_set, 2, 2},
    {"env_set", builtin_env_set, 2, 2},
    {"set_env", builtin_env_set, 2, 2},
    {"終了", builtin_exit_program, 0, 1},
    {"exit_program", builtin_exit_program, 0, 1},
    {"非同期実行", builtin_async_run, 1, -1},
    {"async_run", builtin_async_run, 1, -1},
    {"待機", builtin_async_await, 1, 2},
    {"await_task", builtin_async_await, 1, 2},
    {"全待機", builtin_async_await_all, 1, 1},
    {"await_all", builtin_async_await_all, 1, 1},
    {"タスク状態", builtin_task_status, 1, 1},
    {"task_status", builtin_task_status, 1, 1},
    {"競争待機", builtin_async_race, 1, 1},
    {"race", builtin_async_race, 1, 1},
    {"タスクキャンセル", builtin_task_cancel, 1, 1},
    {"task_cancel", builtin_task_cancel, 1, 1},
    {"成功時", builtin_then, 2, 2},
    {"then_do", builtin_then, 2, 2},
    {"失敗時", builtin_catch, 2, 2},
    {"catch_do", builtin_catch, 2, 2},
    {"プール作成", builtin_pool_create, 0, 1},
    {"pool_create", builtin_pool_create, 0, 1},
    {"プール情報", builtin_pool_stats, 0, 0},
    {"pool_stats", builtin_pool_stats, 0, 0},
    {"並列実行", builtin_parallel_run, 1, 1},
    {"parallel_run", builtin_parallel_run, 1, 1},
    {"並列マップ", builtin_parallel_map, 2, 2},
    {"parallel_map", builtin_parallel_map, 2, 2},
    {"排他作成", builtin_mutex_create, 0, 0},
    {"mutex_create", builtin_mutex_create, 0, 0},
    {"排他実行", builtin_mutex_exec, 2, 2},
    {"mutex_exec", builtin_mutex_exec, 2, 2},
    {"読書ロック作成", builtin_rwlock_create, 0, 0},
    {"rwlock_create", builtin_rwlock_create, 0, 0},
    {"読取実行", builtin_rwlock_read, 2, 2},
    {"rwlock_read", builtin_rwlock_read, 2, 2},
    {"書込実行", builtin_rwlock_write, 2, 2},
    {"rwlock_write", builtin_rwlock_write, 2, 2},
    {"セマフォ作成", builtin_semaphore_create, 1, 1},
    {"semaphore_create", builtin_semaphore_create, 1, 1},
    {"セマフォ獲得", builtin_semaphore_acquire, 1, 1},
    {"semaphore_acquire", builtin_semaphore_acquire, 1, 1},
    {"セマフォ解放", builtin_semaphore_release, 1, 1},
    {"semaphore_release", builtin_semaphore_release, 1, 1},
    {"セマフォ実行", builtin_semaphore_exec, 2, 2},
    {"semaphore_exec", builtin_semaphore_exec, 2, 2},
    {"カウンター作成", builtin_atomic_create, 0, 1},
    {"atomic_create", builtin_atomic_create, 0, 1},
    {"カウンター加算", builtin_atomic_add, 1, 2},
    {"atomic_add", builtin_atomic_add, 1, 2},
    {"カウンター取得", builtin_atomic_get, 1, 1},
    {"atomic_get", builtin_atomic_get, 1, 1},
    {"カウンター設定", builtin_atomic_set, 2, 2},
    {"atomic_set", builtin_atomic_set, 2, 2},
    {"チャネル作成", builtin_channel_create, 0, 1},
    {"channel_create", builtin_channel_create, 0, 1},
    {"チャネル送信", builtin_channel_send, 2, 2},
    {"channel_send", builtin_channel_send, 2, 2},
    {"チャネル受信", builtin_channel_receive, 1, 1},
    {"channel_receive", builtin_channel_receive, 1, 1},
    {"チャネル閉じる", builtin_channel_close, 1, 1},
    {"channel_close", builtin_channel_close, 1, 1},
    {"チャネル試送信", builtin_channel_try_send, 2, 2},
    {"channel_try_send", builtin_channel_try_send, 2, 2},
    {"チャネル試受信", builtin_channel_try_receive, 1, 1},
    {"channel_try_receive", builtin_channel_try_receive, 1, 1},
    {"チャネル残量", builtin_channel_count, 1, 1},
    {"channel_count", builtin_channel_count, 1, 1},
    {"チャネル選択", builtin_channel_select, 1, 2},
    {"channel_select", builtin_channel_select, 1, 2},
    {"定期実行", builtin_schedule_interval, 2, 2},
    {"schedule_interval", builtin_schedule_interval, 2, 2},
    {"遅延実行", builtin_schedule_delay, 2, 2},
    {"schedule_delay", builtin_schedule_delay, 2, 2},
    {"スケジュール停止", builtin_schedule_stop, 1, 1},
    {"schedule_stop", builtin_schedule_stop, 1, 1},
    {"全スケジュール停止", builtin_schedule_stop_all, 0, 0},
    {"schedule_stop_all", builtin_schedule_stop_all, 0, 0},
    {"WS接続", builtin_ws_connect, 1, 1},
    {"ws_connect", builtin_ws_connect, 1, 1},
    {"WS送信", builtin_ws_send, 2, 2},
    {"ws_send", builtin_ws_send, 2, 2},
    {"WS受信", builtin_ws_receive, 1, 2},
    {"ws_receive", builtin_ws_receive, 1, 2},
    {"WS切断", builtin_ws_close, 1, 1},
    {"ws_close", builtin_ws_close, 1, 1},
    {"WS状態", builtin_ws_status, 1, 1},
    {"ws_status", builtin_ws_status, 1, 1},
    {"次", builtin_generator_next, 1, 1},
    {"next", builtin_generator_next, 1, 1},
    {"完了", builtin_generator_done, 1, 1},
    {"done", builtin_generator_done, 1, 1},
    {"全値", builtin_generator_collect, 1, 1},
    {"collect", builtin_generator_collect, 1, 1},
    {"パス結合", builtin_path_join, 2, 2},
    {"path_join", builtin_path_join, 2, 2},
    {"ファイル名", builtin_basename, 1, 1},
    {"basename", builtin_basename, 1, 1},
    {"ディレクトリ名", builtin_dirname, 1, 1},
    {"dirname", builtin_dirname, 1, 1},
    {"拡張子", builtin_extension, 1, 1},
    {"extension", builtin_extension, 1, 1},
    {"extname", builtin_extension, 1, 1},
    {"Base64エンコード", builtin_base64_encode, 1, 1},
    {"base64_encode", builtin_base64_encode, 1, 1},
    {"Base64デコード", builtin_base64_decode, 1, 1},
    {"base64_decode", builtin_base64_decode, 1, 1},
    {"集合", builtin_set_create, 0, -1},
    {"set", builtin_set_create, 0, -1},
    {"集合追加", builtin_set_add, 2, 2},
    {"set_add", builtin_set_add, 2, 2},
    {"集合削除", builtin_set_remove, 2, 2},
    {"set_remove", builtin_set_remove, 2, 2},
    {"集合含む", builtin_set_contains, 2, 2},
    {"set_contains", builtin_set_contains, 2, 2},
    {"和集合", builtin_set_union, 2, 2},
    {"set_union", builtin_set_union, 2, 2},
    {"積集合", builtin_set_intersection, 2, 2},
    {"set_intersection", builtin_set_intersection, 2, 2},
    {"差集合", builtin_set_difference, 2, 2},
    {"set_difference", builtin_set_difference, 2, 2},
    {"テスト", builtin_test_register, 2, 2},
    {"test", builtin_test_register, 2, 2},
    {"テスト実行", builtin_test_run, 0, 0},
    {"run_tests", builtin_test_run, 0, 0},
    {"期待", builtin_expect, 2, 3},
    {"expect", builtin_expect, 2, 3},
    {"期待エラー", builtin_expect_error, 1, 1},
    {"expect_error", builtin_expect_error, 1, 1},
    {"例外作成", builtin_create_exception, 2, 2},
    {"create_exception", builtin_create_exception, 2, 2},
    {"文書化", builtin_doc_set, 2, 2},
    {"doc_set", builtin_doc_set, 2, 2},
    {"document", builtin_doc_set, 2, 2},
    {"文書", builtin_doc_get, 1, 1},
    {"doc_get", builtin_doc_get, 1, 1},
    {"documentation", builtin_doc_get, 1, 1},
    {"型別名", builtin_type_alias, 2, 2},
    {"type_alias", builtin_type_alias, 2, 2},
};

static void register_builtin_table(Evaluator *eval) {
    size_t count = sizeof(builtin_entries) / sizeof(builtin_entries[0]);
    for (size_t i = 0; i < count; i++) {
        const BuiltinEntry *entry = &builtin_entries[i];
        env_define(eval->global, entry->name,
                   value_builtin(entry->fn, entry->name, entry->min_args, entry->max_args),
                   true);
    }
}

static bool is_runtime_constant_name(const char *name) {
    static const char *constants[] = {
        "円周率", "pi", "PI", "自然対数の底", "e",
        "システム名", "アーキテクチャ", "はじむバージョン", "システム",
        NULL
    };

    for (int i = 0; constants[i] != NULL; i++) {
        if (strcmp(name, constants[i]) == 0) return true;
    }
    return false;
}

static bool is_builtin_alias_name(const char *name) {
    size_t count = sizeof(builtin_entries) / sizeof(builtin_entries[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(name, builtin_entries[i].name) == 0) return true;
    }
    return false;
}

static bool is_protected_runtime_name(const char *name) {
    return is_builtin_alias_name(name) || is_runtime_constant_name(name);
}

static const char *protected_runtime_name_kind(const char *name) {
    if (is_builtin_alias_name(name)) return "組み込み関数名";
    if (is_runtime_constant_name(name)) return "ランタイム定数";
    return "定数";
}

static const char *protected_runtime_name_suggestion(const char *name) {
    if (strcmp(name, "print") == 0 || strcmp(name, "表示") == 0) return "output や message";
    if (strcmp(name, "len") == 0 || strcmp(name, "length") == 0 || strcmp(name, "長さ") == 0) return "count や size";
    if (strcmp(name, "map") == 0 || strcmp(name, "filter") == 0 || strcmp(name, "reduce") == 0) return "items や result";
    if (strcmp(name, "values") == 0 || strcmp(name, "keys") == 0) return "rows や entries";
    if (strcmp(name, "date") == 0 || strcmp(name, "time") == 0 || strcmp(name, "now") == 0) return "today や timestamp";
    if (strcmp(name, "pi") == 0 || strcmp(name, "PI") == 0 || strcmp(name, "e") == 0) return "pi_value や ratio";
    return "name_value や、用途が分かる具体的な名前";
}

static void protected_runtime_name_error(Evaluator *eval, ASTNode *node,
                                         const char *name, const char *action) {
    runtime_error(eval, node->location.line, node->location.column,
                  "%s `%s` は%sできません。\n"
                  "   `%s` ははじむが予約している実行時の名前です。変数や関数には %s など別の名前を使ってください。",
                  protected_runtime_name_kind(name), name, action, name,
                  protected_runtime_name_suggestion(name));
}

void register_builtins(Evaluator *eval) {
    register_builtin_table(eval);

    // 数学定数
    env_define(eval->global, "円周率", value_number(3.14159265358979323846), true);
    env_define(eval->global, "pi", value_number(3.14159265358979323846), true);
    env_define(eval->global, "PI", value_number(3.14159265358979323846), true);
    env_define(eval->global, "自然対数の底", value_number(2.71828182845904523536), true);
    env_define(eval->global, "e", value_number(2.71828182845904523536), true);

    // ── プラットフォーム定数 ──────────────────────────────
    {
        /* OS 名 */
        const char *os_name;
#if defined(_WIN32)
        os_name = "Windows";
#elif defined(__APPLE__)
        os_name = "macOS";
#else
        os_name = "Linux";
#endif
        /* CPU アーキテクチャ */
        const char *arch;
#if defined(__aarch64__) || defined(__arm64__)
        arch = "arm64";
#elif defined(__x86_64__) || defined(__amd64__)
        arch = "x86-64";
#elif defined(__i386__)
        arch = "x86";
#else
        arch = "不明";
#endif
        /* 個別定数 */
        env_define(eval->global, "システム名",
                   value_string(os_name), true);
        env_define(eval->global, "アーキテクチャ",
                   value_string(arch), true);
        env_define(eval->global, "はじむバージョン",
                   value_string("1.5.0"), true);

        /* システム辞書: システム["OS"], システム["アーキテクチャ"] 等 */
        Value sys = value_dict();
        dict_set(&sys, "OS",           value_string(os_name));
        dict_set(&sys, "アーキテクチャ",    value_string(arch));
        dict_set(&sys, "バージョン",       value_string("1.5.0"));
#if defined(_WIN32)
        dict_set(&sys, "区切り文字",       value_string("\\"));
        dict_set(&sys, "改行",            value_string("\r\n"));
#else
        dict_set(&sys, "区切り文字",       value_string("/"));
        dict_set(&sys, "改行",            value_string("\n"));
#endif
        env_define(eval->global, "システム", sys, true);
    }

    // 非同期ランタイムの初期化
    async_runtime_init();
}

// =============================================================================
// エラー処理
// =============================================================================

void runtime_error(Evaluator *eval, int line, int col, const char *format, ...) {
    eval->had_error = true;
    eval->error_line = line;
    eval->error_column = col;

    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    /* 後方互换: error_message をクリーンな形式で保持 */
    snprintf(eval->error_message, sizeof(eval->error_message),
             "%s (%d行目): %s", eval->current_file ? eval->current_file : "<不明>", line, message);

    /* エラーカテゴリをメッセージ内容から自動分別 */
    DiagKind kind = DIAG_RUNTIME;
    if (strstr(message, "スタックオーバーフロー"))
        kind = DIAG_OVERFLOW;
    else if (strstr(message, "ゼロ除算"))
        kind = DIAG_ZERO_DIV;
    else if (strstr(message, "モジュール") || strstr(message, "プラグイン")
             || strstr(message, "バイトコード") || strstr(message, "読み込めません")
             || strstr(message, "読み込みに失敗") || strstr(message, "長さが一致")
             || strstr(message, "長が一致") || strstr(message, "サイズが合いません")
             || strstr(message, "列数が一致") || strstr(message, "定義域外"))
        kind = DIAG_VALUE;
    else if (strstr(message, "未定義") || strstr(message, "見つかりません")
             || strstr(message, "定義されていません"))
        kind = DIAG_NAME;
    else if (strstr(message, "型") || strstr(message, "数値でなければ")
             || strstr(message, "文字列でなければ") || strstr(message, "配列でなければ")
             || strstr(message, "関数ではありません") || strstr(message, "呼び出せません")
             || strstr(message, "型が"))
        kind = DIAG_TYPE;
    else if (strstr(message, "インデックス") || strstr(message, "範囲外")
             || strstr(message, "範囲を超え") || strstr(message, "キーがありません"))
        kind = DIAG_INDEX;
    else if (strstr(message, "メンバー") || strstr(message, "属性")
             || strstr(message, "プロパティ") || strstr(message, "フィールド")
             || strstr(message, "静的メソッド") || strstr(message, "親クラス"))
        kind = DIAG_ATTRIBUTE;

    /* 視覚的なエラー表示 */
    diag_report(kind,
                eval->current_file,
                eval->source_code,
                line, col, col, message);

    /* スタックトレース (最大20件を表示、超える場合は省略) */
    if (eval->call_stack_depth > 0) {
        fprintf(stderr, "スタックトレース:\n");
        /* 表示する件数の上限 */
        int max_frames = 20;
        int depth = eval->call_stack_depth;
        int show  = depth < max_frames ? depth : max_frames;
        for (int i = depth - 1; i >= depth - show; i--) {
            const char *fname = eval->call_stack[i].func_name;
            int   sline = eval->call_stack[i].line;
            const char *file  = eval->current_file ? eval->current_file : "<不明>";
            fprintf(stderr, "  %d: %s (%s:%d行目)\n",
                    depth - 1 - i,
                    fname ? fname : "<無名>",
                    file, sline);
        }
        if (depth > max_frames) {
            fprintf(stderr, "  ... (他 %d 件省略)\n", depth - max_frames);
        }
        fputc('\n', stderr);
    }
}

// ビルトイン関数内では AST の位置を直接持たないため、現在の評価器へ
// 説明的な実行時エラーを届けるための薄いラッパーを使う。
static void builtin_runtime_error(const char *format, ...) {
    Evaluator *eval = evaluator_current();
    if (eval == NULL || eval->had_error) return;

    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    runtime_error(eval, 0, 0, "%s", message);
}

static void undefined_variable_error(Evaluator *eval, ASTNode *node, const char *name) {
    const char *similar = env_find_similar(eval->current, name);
    if (similar != NULL) {
        runtime_error(eval, node->location.line, node->location.column,
                      "未定義の変数: %s\n   もしかして: %s", name, similar);
    } else {
        runtime_error(eval, node->location.line, node->location.column,
                      "未定義の変数: %s", name);
    }
}

static bool require_integer_index(Evaluator *eval, ASTNode *node, Value index, const char *target_name) {
    if (index.type != VALUE_NUMBER) {
        runtime_error(eval, node->location.line, node->location.column,
                     "%sのインデックスは整数でなければなりません", target_name);
        return false;
    }

    if (!index.is_integer) {
        runtime_error(eval, node->location.line, node->location.column,
                     "%sのインデックスは整数でなければなりません（渡された値: %g）",
                     target_name, index.number);
        return false;
    }

    return true;
}

static int eval_min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int eval_utf8_char_bytes(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int eval_utf8_to_codepoints(const char *s, unsigned int *out, int max_count) {
    if (!s || !out || max_count <= 0) return 0;

    int count = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && count < max_count) {
        int n = eval_utf8_char_bytes(*p);
        unsigned int cp = 0;

        if (n == 1 || p[1] == '\0') {
            cp = *p;
        } else if (n == 2 && p[1] != '\0') {
            cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
        } else if (n == 3 && p[1] != '\0' && p[2] != '\0') {
            cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        } else if (n == 4 && p[1] != '\0' && p[2] != '\0' && p[3] != '\0') {
            cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12)
                 | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        } else {
            cp = *p;
            n = 1;
        }

        out[count++] = cp;
        p += n;
    }

    return count;
}

static int eval_edit_distance(const char *a, const char *b) {
    unsigned int acp[97];
    unsigned int bcp[97];
    int alen = eval_utf8_to_codepoints(a, acp, 97);
    int blen = eval_utf8_to_codepoints(b, bcp, 97);
    if (alen == 0) return blen;
    if (blen == 0) return alen;
    if (alen > 96 || blen > 96) return 1000000;

    int prev[97];
    int curr[97];
    for (int j = 0; j <= blen; j++) prev[j] = j;

    for (int i = 1; i <= alen; i++) {
        curr[0] = i;
        for (int j = 1; j <= blen; j++) {
            int cost = acp[i - 1] == bcp[j - 1] ? 0 : 1;
            curr[j] = eval_min3(prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost);
        }
        for (int j = 0; j <= blen; j++) prev[j] = curr[j];
    }

    return prev[blen];
}

static bool eval_is_close_name(const char *name, int score) {
    unsigned int cp[97];
    int len = eval_utf8_to_codepoints(name, cp, 97);
    int threshold = len <= 4 ? 1 : 2;
    return score <= threshold;
}

static const char *find_similar_dict_key(Value *dict, const char *name) {
    if (!dict || dict->type != VALUE_DICT || !name) return NULL;

    const char *best = NULL;
    int best_score = 1000000;
    for (int i = 0; i < dict->dict.length; i++) {
        int score = eval_edit_distance(name, dict->dict.keys[i]);
        if (score < best_score) {
            best_score = score;
            best = dict->dict.keys[i];
        }
    }

    return eval_is_close_name(name, best_score) ? best : NULL;
}

static const char *find_similar_instance_member(Value *instance, const char *name) {
    if (!instance || instance->type != VALUE_INSTANCE || !name) return NULL;

    const char *best = NULL;
    int best_score = 1000000;
    for (int i = 0; i < instance->instance.field_count; i++) {
        int score = eval_edit_distance(name, instance->instance.field_names[i]);
        if (score < best_score) {
            best_score = score;
            best = instance->instance.field_names[i];
        }
    }

    Value *class_ref = instance->instance.class_ref;
    while (class_ref != NULL && class_ref->type == VALUE_CLASS) {
        ASTNode *class_def = class_ref->class_value.definition;
        for (int i = 0; i < class_def->class_def.method_count; i++) {
            const char *method_name = class_def->class_def.methods[i]->method.name;
            int score = eval_edit_distance(name, method_name);
            if (score < best_score) {
                best_score = score;
                best = method_name;
            }
        }

        if (class_def->class_def.parent_name != NULL) {
            Value *parent = env_get(g_eval ? g_eval->current : NULL, class_def->class_def.parent_name);
            if (parent != NULL && parent->type == VALUE_CLASS) {
                class_ref = parent;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return eval_is_close_name(name, best_score) ? best : NULL;
}

static const char *find_similar_class_static_method(ASTNode *class_def, const char *name) {
    if (!class_def || !name) return NULL;

    const char *best = NULL;
    int best_score = 1000000;
    for (int i = 0; i < class_def->class_def.static_method_count; i++) {
        const char *method_name = class_def->class_def.static_methods[i]->method.name;
        int score = eval_edit_distance(name, method_name);
        if (score < best_score) {
            best_score = score;
            best = method_name;
        }
    }

    return eval_is_close_name(name, best_score) ? best : NULL;
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
        env_release(local);
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

    double profile_start_ms = 0.0;
    bool profile_this_node = eval->ast_profile_enabled;
    if (profile_this_node) {
        profile_start_ms = evaluator_now_ms();
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
        runtime_error(eval, node->location.line, node->location.column, "スタックオーバーフロー");
        eval->recursion_depth--;
        return value_null();
    }
    
    Value result = value_null();
    
    switch (node->type) {
        case NODE_NUMBER:
            result = value_number(node->number_value);
            break;
            
        case NODE_STRING:
            result = evaluate_string_interpolation(eval, node->string_value,
                                                   node->location.line,
                                                   node->location.column);
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
                undefined_variable_error(eval, node, node->string_value);
            } else {
                result = value_copy(*val);
            }
            break;
        }
        
        case NODE_ARRAY: {
            result = value_array_with_capacity(node->block.count);
            for (int i = 0; i < node->block.count; i++) {
                Value elem = evaluate(eval, node->block.statements[i]);
                if (eval->had_error) {
                    value_free(&elem);
                    break;
                }
                array_push(&result, elem);
                value_free(&elem);
            }
            break;
        }
        
        case NODE_DICT: {
            result = value_dict_with_capacity(node->dict.count);
            for (int i = 0; i < node->dict.count; i++) {
                Value val = evaluate(eval, node->dict.values[i]);
                if (eval->had_error) {
                    value_free(&val);
                    break;
                }
                dict_set(&result, node->dict.keys[i], val);
                value_free(&val);
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
                runtime_error(eval, node->location.line, node->location.column,
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
        
        case NODE_LIST_COMPREHENSION:
            result = evaluate_list_comprehension(eval, node);
            break;
        
        case NODE_NEW:
            result = evaluate_new(eval, node);
            break;
        
        case NODE_MEMBER:
            result = evaluate_member(eval, node);
            break;
        
        case NODE_SELF:
            if (eval->current_instance == NULL) {
                runtime_error(eval, node->location.line, node->location.column,
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
            runtime_error(eval, node->location.line, node->location.column,
                         "未実装のノードタイプ: %s", node_type_name(node->type));
            break;
    }
    
    eval->recursion_depth--;
    if (profile_this_node) {
        record_ast_profile(eval, node, evaluator_now_ms() - profile_start_ms);
    }
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
                    runtime_error(eval, node->location.line, node->location.column, "ゼロ除算");
                    return value_null();
                }
                return value_number(l / r);
            case TOKEN_PERCENT:
                if (r == 0) {
                    runtime_error(eval, node->location.line, node->location.column, "ゼロ除算");
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
    
    // 演算子オーバーロード: 左辺がインスタンスの場合、演算子メソッドを呼ぶ
    // （等価比較のデフォルトより先にチェック）
    if (left.type == VALUE_INSTANCE) {
        const char *method = NULL;
        switch (node->binary.operator) {
            case TOKEN_PLUS:    method = "足す"; break;
            case TOKEN_MINUS:   method = "引く"; break;
            case TOKEN_STAR:    method = "掛ける"; break;
            case TOKEN_SLASH:   method = "割る"; break;
            case TOKEN_PERCENT: method = "剰余"; break;
            case TOKEN_EQ:      method = "等しい"; break;
            case TOKEN_NE:      method = "等しくない"; break;
            case TOKEN_LT:      method = "小さい"; break;
            case TOKEN_GT:      method = "大きい"; break;
            case TOKEN_LE:      method = "以下"; break;
            case TOKEN_GE:      method = "以上"; break;
            default: break;
        }
        if (method != NULL) {
            bool prev_error = eval->had_error;
            eval->had_error = false;
            Value result = call_instance_operator(&left, method, &right);
            if (!eval->had_error) {
                return result;
            }
            eval->had_error = prev_error;
        }
    }
    
    // 等価比較（デフォルト）
    if (node->binary.operator == TOKEN_EQ) {
        return value_bool(value_equals(left, right));
    }
    if (node->binary.operator == TOKEN_NE) {
        return value_bool(!value_equals(left, right));
    }
    
    runtime_error(eval, node->location.line, node->location.column,
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
            runtime_error(eval, node->location.line, node->location.column,
                         "数値以外に単項マイナスは使えません");
            return value_null();
            
        case TOKEN_NOT:
            return value_bool(!value_is_truthy(operand));
            
        default:
            runtime_error(eval, node->location.line, node->location.column,
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
         strcmp(callee.builtin.name, "append") == 0 ||
         strcmp(callee.builtin.name, "push") == 0 ||
         strcmp(callee.builtin.name, "削除") == 0 ||
         strcmp(callee.builtin.name, "remove") == 0 ||
         strcmp(callee.builtin.name, "delete") == 0 ||
         strcmp(callee.builtin.name, "行列設定") == 0 ||
         strcmp(callee.builtin.name, "matrix_set") == 0)) {
        
        if (node->call.arg_count >= 1 && 
            node->call.arguments[0]->type == NODE_IDENTIFIER) {
            
            const char *arr_name = node->call.arguments[0]->string_value;
            Value *array_ptr = env_get(eval->current, arr_name);
            
            if (array_ptr == NULL || array_ptr->type != VALUE_ARRAY) {
                if ((strcmp(callee.builtin.name, "行列設定") == 0 ||
                     strcmp(callee.builtin.name, "matrix_set") == 0) &&
                    array_ptr != NULL && array_ptr->type == VALUE_MATRIX &&
                    node->call.arg_count == 4) {
                    Value row = evaluate(eval, node->call.arguments[1]);
                    Value col = evaluate(eval, node->call.arguments[2]);
                    Value element = evaluate(eval, node->call.arguments[3]);
                    if (eval->had_error) return value_null();
                    if (row.type != VALUE_NUMBER || col.type != VALUE_NUMBER ||
                        !row.is_integer || !col.is_integer || element.type != VALUE_NUMBER) {
                        runtime_error(eval, node->location.line, node->location.column,
                                     "matrix_set には 行列, 整数行, 整数列, 数値 が必要です");
                        return value_null();
                    }
                    if (!matrix_set(array_ptr, (int)row.number, (int)col.number, element.number)) {
                        runtime_error(eval, node->location.line, node->location.column,
                                     "行列インデックスが範囲外です: (%d, %d)",
                                     (int)row.number, (int)col.number);
                        return value_null();
                    }
                    return value_null();
                }
                runtime_error(eval, node->location.line, node->location.column,
                             "%sは配列ではありません", arr_name);
                return value_null();
            }
            
            if ((strcmp(callee.builtin.name, "追加") == 0 ||
                 strcmp(callee.builtin.name, "append") == 0 ||
                 strcmp(callee.builtin.name, "push") == 0) &&
                node->call.arg_count == 2) {
                Value element = evaluate(eval, node->call.arguments[1]);
                if (eval->had_error) return value_null();
                array_push(array_ptr, element);
                return value_null();
            }
            else if ((strcmp(callee.builtin.name, "削除") == 0 ||
                      strcmp(callee.builtin.name, "remove") == 0 ||
                      strcmp(callee.builtin.name, "delete") == 0) &&
                     node->call.arg_count == 2) {
                Value index = evaluate(eval, node->call.arguments[1]);
                if (eval->had_error) return value_null();
                if (index.type != VALUE_NUMBER) {
                    runtime_error(eval, node->location.line, node->location.column,
                                 "インデックスは整数でなければなりません");
                    return value_null();
                }
                if (!index.is_integer) {
                    runtime_error(eval, node->location.line, node->location.column,
                                 "インデックスは整数でなければなりません（渡された値: %g）",
                                 index.number);
                    return value_null();
                }
                int idx = (int)index.number;
                if (idx < 0 || idx >= array_ptr->array.length) {
                    runtime_error(eval, node->location.line, node->location.column,
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
                    runtime_error(eval, arg_node->location.line, arg_node->location.column, "スプレッド演算子は配列にのみ使用できます");
                    free(args);
                    return value_null();
                }
                // 配列の各要素を引数に追加
                for (int j = 0; j < spread_val.array.length; j++) {
                    if (actual_arg_count >= args_capacity) {
                        ARRAY_GROW(args, actual_arg_count, args_capacity, Value, free(args); return value_null());
                    }
                    args[actual_arg_count++] = value_copy(spread_val.array.elements[j]);
                }
            } else {
                if (actual_arg_count >= args_capacity) {
                    ARRAY_GROW(args, actual_arg_count, args_capacity, Value, free(args); return value_null());
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
    int effective_arg_count = actual_arg_count;
    
    Value result = value_null();
    
    // 組み込み関数
    if (callee.type == VALUE_BUILTIN) {
        // 引数の数をチェック
        int min = callee.builtin.min_args;
        int max = callee.builtin.max_args;
        
        if (effective_arg_count < min) {
            runtime_error(eval, node->location.line, node->location.column,
                         "%sには少なくとも%d個の引数が必要です",
                         callee.builtin.name, min);
        } else if (max >= 0 && effective_arg_count > max) {
            runtime_error(eval, node->location.line, node->location.column,
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
        if (effective_arg_count < min_required || 
            (!has_variadic && effective_arg_count > expected_count)) {
            if (min_required == expected_count) {
                runtime_error(eval, node->location.line, node->location.column,
                             "%sには%d個の引数が必要です（%d個渡されました）",
                             func_name,
                             expected_count,
                             effective_arg_count);
            } else {
                runtime_error(eval, node->location.line, node->location.column,
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

            // 値セマンティクス維持: メソッド内の変更を呼び出し元レシーバへ反映
            if (is_method_call && instance_ptr != NULL && !is_super_call) {
                ASTNode *receiver = node->call.callee->member.object;
                if (receiver->type == NODE_IDENTIFIER) {
                    env_set(eval->current, receiver->string_value, *instance_ptr);
                } else if (receiver->type == NODE_SELF && prev_instance != NULL) {
                    *prev_instance = *instance_ptr;
                }
            }

            if (instance_ptr != NULL && !is_super_call) {
                free(instance_ptr);
            }
            
            if (eval->returning) {
                result = eval->return_value;
                eval->returning = false;
            }
            
            eval->current = prev;
            env_release(local);
        }
    }
    else {
        runtime_error(eval, node->location.line, node->location.column,
                     "呼び出し可能ではありません");
    }
    
    if (args != NULL) {
        free(args);
    }

    maybe_collect_gc();
    
    return result;
}

static Value evaluate_index(Evaluator *eval, ASTNode *node) {
    Value array = evaluate(eval, node->index.array);
    if (eval->had_error) return value_null();
    
    Value index = evaluate(eval, node->index.index);
    if (eval->had_error) return value_null();
    
    if (array.type == VALUE_ARRAY) {
        if (!require_integer_index(eval, node, index, "配列")) {
            return value_null();
        }
        
        int idx = (int)index.number;
        // 負のインデックス: -1 = 最後, -2 = 最後から2番目...
        if (idx < 0) idx += array.array.length;
        if (idx < 0 || idx >= array.array.length) {
            runtime_error(eval, node->location.line, node->location.column,
                         "インデックスが範囲外です: %d（長さ: %d）",
                         (int)index.number, array.array.length);
            return value_null();
        }
        
        return array.array.elements[idx];
    }

    if (array.type == VALUE_NUMERIC_ARRAY) {
        if (!require_integer_index(eval, node, index, "数値ベクトル")) {
            return value_null();
        }

        int idx = (int)index.number;
        if (idx < 0) idx += array.numeric_array.length;
        if (idx < 0 || idx >= array.numeric_array.length) {
            runtime_error(eval, node->location.line, node->location.column,
                         "インデックスが範囲外です: %d（長さ: %d）",
                         (int)index.number, array.numeric_array.length);
            return value_null();
        }

        return value_number(numeric_array_get(&array, idx));
    }

    if (array.type == VALUE_MATRIX) {
        if (!require_integer_index(eval, node, index, "数値行列")) {
            return value_null();
        }

        int row = (int)index.number;
        if (row < 0) row += array.matrix.rows;
        if (row < 0 || row >= array.matrix.rows) {
            runtime_error(eval, node->location.line, node->location.column,
                         "行インデックスが範囲外です: %d（行数: %d）",
                         (int)index.number, array.matrix.rows);
            return value_null();
        }

        Value result = value_numeric_array_with_capacity(array.matrix.cols);
        for (int c = 0; c < array.matrix.cols; c++) {
            numeric_array_push(&result, matrix_get(&array, row, c));
        }
        return result;
    }
    
    if (array.type == VALUE_STRING) {
        if (!require_integer_index(eval, node, index, "文字列")) {
            return value_null();
        }
        
        int idx = (int)index.number;
        int len = string_length(&array);
        
        // 負のインデックス対応
        if (idx < 0) idx += len;
        if (idx < 0 || idx >= len) {
            runtime_error(eval, node->location.line, node->location.column,
                         "インデックスが範囲外です: %d（長さ: %d）",
                         (int)index.number, len);
            return value_null();
        }
        
        return string_substring(&array, idx, idx + 1);
    }
    
    if (array.type == VALUE_DICT) {
        if (index.type != VALUE_STRING) {
            runtime_error(eval, node->location.line, node->location.column,
                         "辞書のキーは文字列でなければなりません");
            return value_null();
        }
        
        return dict_get(&array, index.string.data);
    }
    
    runtime_error(eval, node->location.line, node->location.column,
                 "インデックスアクセスは配列、文字列、辞書、数値ベクトル、数値行列にのみ使用できます");
    return value_null();
}

static Value evaluate_function_def(Evaluator *eval, ASTNode *node) {
    Value func = value_function(node, eval->current);
    if (!env_define(eval->current, node->function.name, func, false)) {
        if (env_is_const(eval->current, node->function.name) &&
            is_protected_runtime_name(node->function.name)) {
            protected_runtime_name_error(eval, node, node->function.name, "再定義");
        } else if (env_is_const(eval->current, node->function.name)) {
            runtime_error(eval, node->location.line, node->location.column,
                         "定数 %s は関数として再定義できません", node->function.name);
        }
        value_free(&func);
    }
    return value_null();
}

static Value evaluate_var_decl(Evaluator *eval, ASTNode *node) {
    Value value = evaluate(eval, node->var_decl.initializer);
    if (eval->had_error) return value_null();
    
    // 環境に保存するときはコピーを作成
    Value copy = value_copy(value);
    if (!env_define(eval->current, node->var_decl.name, copy, node->var_decl.is_const)) {
        if (env_is_const(eval->current, node->var_decl.name)) {
            if (is_protected_runtime_name(node->var_decl.name)) {
                protected_runtime_name_error(eval, node, node->var_decl.name, "再定義");
            } else {
                runtime_error(eval, node->location.line, node->location.column,
                             "定数 %s は再定義できません", node->var_decl.name);
            }
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
                undefined_variable_error(eval, node, node->assign.target->string_value);
                return value_null();
            }
            current = *ptr;
        } else if (node->assign.target->type == NODE_INDEX) {
            current = evaluate_index(eval, node->assign.target);
            if (eval->had_error) return value_null();
        } else {
            runtime_error(eval, node->location.line, node->location.column, "不正な代入先");
            return value_null();
        }
        
        if (current.type != VALUE_NUMBER || value.type != VALUE_NUMBER) {
            runtime_error(eval, node->location.line, node->location.column,
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
                    runtime_error(eval, node->location.line, node->location.column, "ゼロ除算");
                    return value_null();
                }
                value = value_number(current.number / value.number);
                break;
            case TOKEN_PERCENT_ASSIGN:
                if (value.number == 0) {
                    runtime_error(eval, node->location.line, node->location.column, "ゼロ除算");
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
            if (is_protected_runtime_name(name)) {
                protected_runtime_name_error(eval, node, name, "代入");
            } else {
                runtime_error(eval, node->location.line, node->location.column,
                             "定数 %s には代入できません", name);
            }
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
        /* ネストしたインデックス代入に対応: a["x"]["y"]["z"] = val */
        ASTNode *chain_nodes[64];
        int chain_depth = 0;
        ASTNode *cur = node->assign.target;
        while (cur->type == NODE_INDEX && chain_depth < 63) {
            chain_nodes[chain_depth++] = cur;
            cur = cur->index.array;
        }
        /* chain_nodes[0] が最も内側（最終）のインデックス, chain_nodes[depth-1] が最も外側 */
        if (cur->type != NODE_IDENTIFIER) {
            runtime_error(eval, node->location.line, node->location.column, "不正な代入先");
            return value_null();
        }
        Value *ptr = env_get(eval->current, cur->string_value);
        if (ptr == NULL) {
            runtime_error(eval, node->location.line, node->location.column, "配列または辞書が見つかりません");
            return value_null();
        }
        /* 外側から depth-1 回ナビゲート（最後の1レベルは実際に代入） */
        for (int i = chain_depth - 1; i >= 1; i--) {
            Value idx = evaluate(eval, chain_nodes[i]->index.index);
            if (eval->had_error) return value_null();
            if (ptr->type == VALUE_DICT && idx.type == VALUE_STRING) {
                bool found = false;
                for (int j = 0; j < ptr->dict.length; j++) {
                    if (strcmp(ptr->dict.keys[j], idx.string.data) == 0) {
                        ptr = &ptr->dict.values[j];
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    runtime_error(eval, node->location.line, node->location.column,
                                  "辞書にキー '%s' が見つかりません", idx.string.data);
                    return value_null();
                }
            } else if (ptr->type == VALUE_ARRAY && idx.type == VALUE_NUMBER) {
                if (!idx.is_integer) {
                    runtime_error(eval, node->location.line, node->location.column,
                                  "配列のインデックスは整数でなければなりません（渡された値: %g）",
                                  idx.number);
                    return value_null();
                }
                int aidx = (int)idx.number;
                if (aidx < 0) aidx += ptr->array.length;
                if (aidx < 0 || aidx >= ptr->array.length) {
                    runtime_error(eval, node->location.line, node->location.column,
                                  "インデックスが範囲外です: %d", aidx);
                    return value_null();
                }
                ptr = &ptr->array.elements[aidx];
            } else if (ptr->type == VALUE_NUMERIC_ARRAY && idx.type == VALUE_NUMBER) {
                runtime_error(eval, node->location.line, node->location.column,
                              "数値ベクトルの要素は数値なので、さらにネストした代入はできません");
                return value_null();
            } else {
                runtime_error(eval, node->location.line, node->location.column, "配列または辞書が見つかりません");
                return value_null();
            }
        }
        /* 最終インデックスへ代入 */
        Value index = evaluate(eval, chain_nodes[0]->index.index);
        if (eval->had_error) return value_null();
        if (ptr->type == VALUE_ARRAY) {
            if (!require_integer_index(eval, node, index, "配列")) {
                return value_null();
            }
            int idx = (int)index.number;
            if (!array_set(ptr, idx, value)) {
                runtime_error(eval, node->location.line, node->location.column,
                             "インデックスが範囲外です: %d", idx);
                return value_null();
            }
        }
        else if (ptr->type == VALUE_NUMERIC_ARRAY) {
            if (!require_integer_index(eval, node, index, "数値ベクトル")) {
                return value_null();
            }
            if (value.type != VALUE_NUMBER) {
                runtime_error(eval, node->location.line, node->location.column,
                             "数値ベクトルには数値だけを代入できます");
                return value_null();
            }
            int idx = (int)index.number;
            if (idx < 0) idx += ptr->numeric_array.length;
            if (!numeric_array_set(ptr, idx, value.number)) {
                runtime_error(eval, node->location.line, node->location.column,
                             "インデックスが範囲外です: %d", idx);
                return value_null();
            }
        }
        else if (ptr->type == VALUE_DICT) {
            if (index.type != VALUE_STRING) {
                runtime_error(eval, node->location.line, node->location.column,
                             "辞書のキーは文字列でなければなりません");
                return value_null();
            }
            dict_set(ptr, index.string.data, value);
        }
        else {
            runtime_error(eval, node->location.line, node->location.column, "配列、辞書、数値ベクトルが見つかりません");
            return value_null();
        }
    }
    else if (node->assign.target->type == NODE_MEMBER) {
        // メンバーへの代入（インスタンスフィールド）
        ASTNode *member_node = node->assign.target;
        Value object = evaluate(eval, member_node->member.object);
        if (eval->had_error) return value_null();
        
        if (object.type == VALUE_INSTANCE) {
            // アクセス修飾子: _で始まるフィールドは非公開
            const char *field_name = member_node->member.member_name;
            if (field_name[0] == '_') {
                bool is_self_access = (eval->current_instance != NULL &&
                                       eval->current_instance->type == VALUE_INSTANCE &&
                                       eval->current_instance->instance.fields == object.instance.fields);
                // NODE_SELF（自分.xxx）からのアクセスも許可
                if (!is_self_access && member_node->member.object->type != NODE_SELF) {
                    eval->throwing = true;
                    char msg[256];
                    snprintf(msg, sizeof(msg), "'%s' は非公開フィールドです", field_name);
                    eval->exception_value = value_string(msg);
                    return value_null();
                }
            }
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
            runtime_error(eval, node->location.line, node->location.column, "メンバー代入はインスタンスにのみ使用できます");
            return value_null();
        }
    }
    else {
        runtime_error(eval, node->location.line, node->location.column, "不正な代入先");
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
        runtime_error(eval, node->location.line, node->location.column,
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
            runtime_error(eval, node->location.line, node->location.column,
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

/**
 * インポート済みパスキャッシュにパスが存在するかチェック
 */
static uint32_t import_path_hash(const char *path) {
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

static void import_path_set_insert(Evaluator *eval, const char *path);

static void import_path_set_grow(Evaluator *eval) {
    ImportedPathEntry *old_entries = eval->imported_path_entries;
    int old_capacity = eval->imported_path_entry_capacity;

    eval->imported_path_entry_capacity = old_capacity == 0
        ? EVALUATOR_INITIAL_IMPORT_SET_CAPACITY
        : old_capacity * 2;
    eval->imported_path_entries = calloc((size_t)eval->imported_path_entry_capacity,
                                         sizeof(ImportedPathEntry));
    eval->imported_path_entry_count = 0;

    for (int i = 0; i < old_capacity; i++) {
        if (old_entries[i].occupied) {
            import_path_set_insert(eval, old_entries[i].path);
        }
    }
    free(old_entries);
}

static void import_path_set_insert(Evaluator *eval, const char *path) {
    if (eval->imported_path_entry_capacity == 0 ||
        eval->imported_path_entry_count * 2 >= eval->imported_path_entry_capacity) {
        import_path_set_grow(eval);
    }

    uint32_t index = import_path_hash(path) & (uint32_t)(eval->imported_path_entry_capacity - 1);
    for (int probe = 0; probe < eval->imported_path_entry_capacity; probe++) {
        ImportedPathEntry *entry = &eval->imported_path_entries[index];
        if (!entry->occupied) {
            entry->path = path;
            entry->occupied = true;
            eval->imported_path_entry_count++;
            return;
        }
        if (strcmp(entry->path, path) == 0) {
            return;
        }
        index = (index + 1) & (uint32_t)(eval->imported_path_entry_capacity - 1);
    }
}

static bool is_already_imported(Evaluator *eval, const char *path) {
    if (eval->imported_path_entry_capacity == 0) return false;

    uint32_t index = import_path_hash(path) & (uint32_t)(eval->imported_path_entry_capacity - 1);
    for (int probe = 0; probe < eval->imported_path_entry_capacity; probe++) {
        ImportedPathEntry *entry = &eval->imported_path_entries[index];
        if (!entry->occupied) return false;
        if (strcmp(entry->path, path) == 0) return true;
        index = (index + 1) & (uint32_t)(eval->imported_path_entry_capacity - 1);
    }
    return false;
}

/**
 * インポート済みパスキャッシュにパスを追加
 */
static void add_imported_path(Evaluator *eval, const char *path) {
    if (eval->imported_path_count >= eval->imported_path_capacity) {
        eval->imported_path_capacity = eval->imported_path_capacity == 0
            ? EVALUATOR_INITIAL_IMPORT_CAPACITY
            : eval->imported_path_capacity * 2;
        eval->imported_paths = realloc(eval->imported_paths,
                                       eval->imported_path_capacity * sizeof(char *));
    }
    char *stored_path = strdup(path);
    if (stored_path == NULL) return;
    eval->imported_paths[eval->imported_path_count++] = stored_path;
    import_path_set_insert(eval, stored_path);
}

/**
 * realpath でパスを正規化（循環検出と重複防止用）
 */
static bool normalize_path(const char *path, char *out, int max_len) {
    char *rp = realpath(path, NULL);
    if (rp) {
        snprintf(out, max_len, "%s", rp);
        free(rp);
        return true;
    }
    // realpath 失敗時はそのまま
    snprintf(out, max_len, "%.*s", (int)(max_len - 1), path);
    return false;
}

static bool path_has_suffix(const char *path, const char *suffix) {
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    return path_len >= suffix_len &&
           strcmp(path + path_len - suffix_len, suffix) == 0;
}

static bool is_source_script_path(const char *path) {
    return path_has_suffix(path, ".jp") ||
           path_has_suffix(path, ".haj") ||
           path_has_suffix(path, ".hajimu");
}

static bool file_exists_readable(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static bool resolve_source_import_path(Evaluator *eval, const char *module_path,
                                       char *out, size_t out_size) {
    char with_jp[EVALUATOR_PATH_BUFFER_SIZE];
    char with_haj[EVALUATOR_PATH_BUFFER_SIZE];
    char with_hajimu[EVALUATOR_PATH_BUFFER_SIZE];
    const char *candidates[4] = {0};
    int candidate_count = 0;

    if (is_source_script_path(module_path)) {
        candidates[candidate_count++] = module_path;
    } else {
        snprintf(with_jp, sizeof(with_jp), "%s.jp", module_path);
        snprintf(with_haj, sizeof(with_haj), "%s.haj", module_path);
        snprintf(with_hajimu, sizeof(with_hajimu), "%s.hajimu", module_path);
        candidates[candidate_count++] = with_jp;
        candidates[candidate_count++] = with_haj;
        candidates[candidate_count++] = with_hajimu;
    }

    for (int i = 0; i < candidate_count; i++) {
        const char *candidate = candidates[i];
        bool absolute = candidate[0] == '/';

        if (!absolute && eval->current_file) {
            char dir[EVALUATOR_PATH_BUFFER_SIZE];
            char try_path[EVALUATOR_LONG_PATH_BUFFER_SIZE];
            snprintf(dir, sizeof(dir), "%s", eval->current_file);
            char *sep = strrchr(dir, '/');
            if (sep) {
                *(sep + 1) = '\0';
                snprintf(try_path, sizeof(try_path), "%s%s", dir, candidate);
            } else {
                snprintf(try_path, sizeof(try_path), "%s", candidate);
            }
            if (file_exists_readable(try_path)) {
                snprintf(out, out_size, "%s", try_path);
                return true;
            }
        }

        if (file_exists_readable(candidate)) {
            snprintf(out, out_size, "%s", candidate);
            return true;
        }
    }

    return false;
}

// =============================================================================
// ソース文字列をモジュールとして評価する共通処理
// .jp スクリプトインポートと HJPB バイトコードインポートの共通基盤
// =============================================================================

static Value evaluate_source_as_module(Evaluator *eval, ASTNode *node,
                                       const char *source, const char *source_path,
                                       const char *alias) {
    /* ソースを strdup してモジュールとして保存（AST / ソースの寿命を evaluator に渡す）*/
    char *src_copy = strdup(source);
    if (!src_copy) return value_null();

    Parser parser;
    parser_init(&parser, src_copy, source_path ? source_path : "<module>");
    ASTNode *program = parse_program(&parser);

    if (parser.had_error) {
        node_free(program);
        free(src_copy);
        parser_free(&parser);
        runtime_error(eval, node->location.line, node->location.column,
                     "モジュール '%s' のパースに失敗しました",
                     source_path ? source_path : "<module>");
        return value_null();
    }
    parser_free(&parser);

    /* import 済みモジュールリストに追加 (AST と source の寿命を evaluator に委譲) */
    if (eval->imported_count >= eval->imported_capacity) {
        eval->imported_capacity = eval->imported_capacity == 0 ? 4 : eval->imported_capacity * 2;
        eval->imported_modules = realloc(eval->imported_modules,
                                         eval->imported_capacity * sizeof(ImportedModule));
    }
    eval->imported_modules[eval->imported_count].source = src_copy;
    eval->imported_modules[eval->imported_count].ast    = program;
    eval->imported_count++;

    /* current_file を一時的に切り替え（ネストしたインポートの相対パス解決用）*/
    const char *prev_file = eval->current_file;
    if (source_path) eval->current_file = source_path;

    if (alias != NULL) {
        /* 名前空間付きインポート: 新しいスコープでモジュールを評価 → 辞書に変換 */
        Environment *module_env = env_new(eval->global);
        Environment *prev = eval->current;
        eval->current = module_env;

        for (int i = 0; i < program->block.count; i++) {
            evaluate(eval, program->block.statements[i]);
            if (eval->had_error) {
                eval->current = prev;
                eval->current_file = prev_file;
                env_release(module_env);
                return value_null();
            }
        }

        eval->current = prev;

        /* モジュール環境の定義を辞書に変換 */
        Value ns = value_dict();
        for (int i = 0; i < ENV_HASH_SIZE; i++) {
            EnvEntry *entry = module_env->table[i];
            while (entry != NULL) {
                dict_set(&ns, entry->name, value_copy(entry->value));
                entry = entry->next;
            }
        }
        env_release(module_env);
        env_define(eval->current, alias, ns, true);
    } else {
        /* 直接インポート: 現在の環境で実行 */
        for (int i = 0; i < program->block.count; i++) {
            evaluate(eval, program->block.statements[i]);
            if (eval->had_error) break;
        }
    }

    eval->current_file = prev_file;
    return value_null();
}

// =============================================================================
// HJPB バイトコード .hjp のインポート (クロスプラットフォーム)
// =============================================================================

static Value evaluate_import_hjpb(Evaluator *eval, ASTNode *node,
                                   const char *hjp_path) {
    const char *alias = node->import_stmt.alias;

    HjpbMeta meta  = {0};
    char    *source = NULL;
    size_t   src_len = 0;

    if (!hjpb_decode(hjp_path, &meta, &source, &src_len)) {
        runtime_error(eval, node->location.line, node->location.column,
                     "バイトコード '%s' の読み込みに失敗しました", hjp_path);
        return value_null();
    }

    Value result = evaluate_source_as_module(eval, node, source, hjp_path, alias);
    free(source);
    return result;
}

// =============================================================================
// .hjp ファイルのディスパッチ: バイトコード or ネイティブ C プラグイン
// =============================================================================

/**
 * 解決済み .hjp パスに対して適切なローダーを呼び出す。
 *  - HJPB マジックバイト "HJPB" → バイトコード → evaluate_import_hjpb
 *  - それ以外                   → ネイティブ C プラグイン → evaluate_import_plugin
 */
static Value dispatch_hjp_import(Evaluator *eval, ASTNode *node,
                                  const char *resolved_path) {
    if (hjpb_is_bytecode_file(resolved_path)) {
        return evaluate_import_hjpb(eval, node, resolved_path);
    }
    return evaluate_import_plugin(eval, node, resolved_path);
}

// =============================================================================
// ネイティブプラグイン（.hjp）のインポート
// =============================================================================

static Value evaluate_import_plugin(Evaluator *eval, ASTNode *node,
                                     const char *resolved_path) {
    const char *alias = node->import_stmt.alias;
    
    // プラグインを読み込む
    HajimuPluginInfo *info = NULL;
    if (!plugin_load(&eval->plugin_manager, resolved_path, &info)) {
        runtime_error(eval, node->location.line, node->location.column,
                     "プラグイン '%s' の読み込みに失敗しました", resolved_path);
        return value_null();
    }
    
    if (info == NULL || info->functions == NULL || info->function_count == 0) {
        return value_null();
    }
    
    // ランタイムコールバックをプラグインに注入
    {
        LoadedPlugin *lp = NULL;
        for (int i = 0; i < eval->plugin_manager.count; i++) {
            if (eval->plugin_manager.plugins[i].info == info) {
                lp = &eval->plugin_manager.plugins[i];
                break;
            }
        }
        if (lp) plugin_set_runtime(lp, &g_plugin_runtime);
    }
    
    if (alias != NULL) {
        // 名前空間付きインポート: 辞書に関数を格納
        Value ns = value_dict();
        
        for (int i = 0; i < info->function_count; i++) {
            HajimuPluginFunc *pf = &info->functions[i];
            Value fn = value_builtin((BuiltinFn)pf->fn, pf->name,
                                     pf->min_args, pf->max_args);
            dict_set(&ns, pf->name, fn);
        }
        
        // メタ情報も辞書に追加
        if (info->name) dict_set(&ns, "__名前__", value_string(info->name));
        if (info->version) dict_set(&ns, "__バージョン__", value_string(info->version));
        if (info->author) dict_set(&ns, "__作者__", value_string(info->author));
        if (info->description) dict_set(&ns, "__説明__", value_string(info->description));
        
        env_define(eval->current, alias, ns, true);
    } else {
        // 直接インポート: 現在の環境に関数を登録
        for (int i = 0; i < info->function_count; i++) {
            HajimuPluginFunc *pf = &info->functions[i];
            Value fn = value_builtin((BuiltinFn)pf->fn, pf->name,
                                     pf->min_args, pf->max_args);
            env_define(eval->current, pf->name, fn, true);
        }
    }
    
    return value_null();
}

static Value evaluate_import(Evaluator *eval, ASTNode *node) {
    const char *module_path = node->import_stmt.module_path;
    const char *alias = node->import_stmt.alias;
    
    // ============================================================
    // 1. 明示的 .hjp 指定 → バイトコードまたはネイティブプラグインとして読み込み
    //    HJPB マジックがあればバイトコード、なければ dlopen (後方互換)
    // ============================================================
    if (plugin_is_hjp(module_path)) {
        char resolved[EVALUATOR_PATH_BUFFER_SIZE];
        if (plugin_resolve_hjp(module_path, eval->current_file,
                               resolved, sizeof(resolved))) {
            return dispatch_hjp_import(eval, node, resolved);
        }
        runtime_error(eval, node->location.line, node->location.column,
                     "プラグイン '%s' が見つかりません", module_path);
        return value_null();
    }
    
    char resolved_path[EVALUATOR_LONG_PATH_BUFFER_SIZE];
    bool found = false;
    
    // ============================================================
    // パス解決の優先順位:
    //   1. 相対パス（呼び出し元ファイルのディレクトリ基準）
    //   2. CWD基準の相対パス
    //   3. パッケージ名としてパッケージディレクトリを検索
    // ============================================================
    
    // パスに / やソース拡張子が含まれる → ファイルパスとして扱う
    bool is_file_path = (strchr(module_path, '/') != NULL || 
                         is_source_script_path(module_path));
    
    if (is_file_path) {
        // --- ファイルパスモード ---
        found = resolve_source_import_path(eval, module_path,
                                           resolved_path, sizeof(resolved_path));
    }
    
    // 3. パッケージ名として検索
    if (!found) {
        found = package_resolve(module_path, eval->current_file,
                                resolved_path, sizeof(resolved_path));
        // パッケージのメインが .hjp の場合: HJPB バイトコードまたはネイティブプラグイン
        if (found && plugin_is_hjp(resolved_path)) {
            return dispatch_hjp_import(eval, node, resolved_path);
        }
    }
    
    // 4. .hjp プラグインとして検索（拡張子なしインポート対応）
    if (!found) {
        char hjp_resolved[EVALUATOR_PATH_BUFFER_SIZE];
        if (plugin_resolve_hjp(module_path, eval->current_file,
                               hjp_resolved, sizeof(hjp_resolved))) {
            return dispatch_hjp_import(eval, node, hjp_resolved);
        }
    }
    
    if (!found) {
        runtime_error(eval, node->location.line, node->location.column,
                     "モジュール '%s' が見つかりません\n"
                     "  ファイルパスまたはパッケージ名を確認してください\n"
                     "  パッケージの場合: hajimu パッケージ 追加 ユーザー/リポジトリ",
                     module_path);
        return value_null();
    }
    
    // パスを正規化（重複防止・循環検出用）
    char canonical_path[EVALUATOR_PATH_BUFFER_SIZE];
    normalize_path(resolved_path, canonical_path, sizeof(canonical_path));
    
    // 重複インポート防止: 既にインポート済みなら何もしない
    if (is_already_imported(eval, canonical_path)) {
        return value_null();
    }
    
    // インポート済みとしてマーク（循環参照防止のため読み込み前に登録）
    add_imported_path(eval, canonical_path);
    
    // ファイルを読み込む
    FILE *file = fopen(resolved_path, "rb");
    if (file == NULL) {
        runtime_error(eval, node->location.line, node->location.column,
                     "モジュール '%s' を読み込めません", resolved_path);
        return value_null();
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *source = malloc(size + 1);
    size_t nread = fread(source, 1, size, file);
    source[nread] = '\0';
    fclose(file);
    
    // evaluate_source_as_module が strdup してモジュールリストを管理するため
    // ここでの source はローカルバッファとして扱い最後に解放する
    Value result = evaluate_source_as_module(eval, node, source, resolved_path, alias);
    free(source);
    return result;
}

static Value evaluate_class_def(Evaluator *eval, ASTNode *node) {
    // 親クラスを取得（あれば）
    Value *parent = NULL;
    if (node->class_def.parent_name != NULL) {
        Value *parent_ptr = env_get(eval->current, node->class_def.parent_name);
        if (parent_ptr == NULL || parent_ptr->type != VALUE_CLASS) {
            runtime_error(eval, node->location.line, node->location.column,
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
        runtime_error(eval, node->location.line, node->location.column,
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
            runtime_error(eval, node->location.line, node->location.column,
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
                env_release(method_env);
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
        env_release(method_env);
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
            runtime_error(eval, node->location.line, node->location.column,
                         "'親' はメソッド内でのみ使用できます");
            return value_null();
        }
        
        Value *instance = eval->current_instance;
        if (instance->type != VALUE_INSTANCE || instance->instance.class_ref == NULL) {
            runtime_error(eval, node->location.line, node->location.column,
                         "インスタンスが無効です");
            return value_null();
        }
        
        // 現在のクラスの親クラスを取得
        Value *class_ref = instance->instance.class_ref;
        if (class_ref->type != VALUE_CLASS || class_ref->class_value.parent == NULL) {
            runtime_error(eval, node->location.line, node->location.column,
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
        
        runtime_error(eval, node->location.line, node->location.column,
                     "親クラスに '%s' というメソッドがありません", member_name);
        return value_null();
    }
    
    Value object = evaluate(eval, node->member.object);
    if (eval->had_error) return value_null();
    
    // インスタンスのフィールドアクセス
    if (object.type == VALUE_INSTANCE) {
        // アクセス修飾子: _で始まるフィールドは非公開
        if (member_name[0] == '_') {
            // 自分自身からのアクセスは許可
            bool is_self_access = (node->member.object->type == NODE_SELF) ||
                                  (eval->current_instance != NULL &&
                                   eval->current_instance->type == VALUE_INSTANCE &&
                                   eval->current_instance->instance.fields == object.instance.fields);
            if (!is_self_access) {
                // 例外として投げる（try-catchで捕獲可能）
                eval->throwing = true;
                char msg[256];
                snprintf(msg, sizeof(msg), "'%s' は非公開フィールドです", member_name);
                eval->exception_value = value_string(msg);
                return value_null();
            }
        }
        
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
        
        const char *similar = find_similar_instance_member(&object, member_name);
        if (similar != NULL) {
            runtime_error(eval, node->location.line, node->location.column,
                         "インスタンスに '%s' というフィールドまたはメソッドがありません\n"
                         "   もしかして: %s",
                         member_name, similar);
        } else {
            runtime_error(eval, node->location.line, node->location.column,
                         "インスタンスに '%s' というフィールドまたはメソッドがありません",
                         member_name);
        }
        return value_null();
    }
    
    // 辞書のメンバーアクセス
    if (object.type == VALUE_DICT) {
        Value val = dict_get(&object, member_name);
        if (val.type != VALUE_NULL) {
            return value_copy(val);
        }
        const char *similar = find_similar_dict_key(&object, member_name);
        if (similar != NULL) {
            runtime_error(eval, node->location.line, node->location.column,
                         "辞書に '%s' というキーがありません\n"
                         "   もしかして: %s",
                         member_name, similar);
        } else {
            runtime_error(eval, node->location.line, node->location.column,
                         "辞書に '%s' というキーがありません", member_name);
        }
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
        const char *similar = find_similar_class_static_method(class_def, member_name);
        if (similar != NULL) {
            runtime_error(eval, node->location.line, node->location.column,
                         "クラス '%s' に静的メソッド '%s' がありません\n"
                         "   もしかして: %s",
                         class_def->class_def.name, member_name, similar);
        } else {
            runtime_error(eval, node->location.line, node->location.column,
                         "クラス '%s' に静的メソッド '%s' がありません",
                         class_def->class_def.name, member_name);
        }
        return value_null();
    }
    
    runtime_error(eval, node->location.line, node->location.column,
                 "メンバーアクセスはインスタンス、辞書、またはクラスに対してのみ使用できます");
    return value_null();
}

// =============================================================================
// 例外処理の評価
// =============================================================================

static Value make_runtime_diagnostic_value(Evaluator *eval) {
    Value diag = value_dict();
    const char *file = eval->current_file != NULL ? eval->current_file : "<不明>";
    dict_set(&diag, "message", value_string(eval->error_message));
    dict_set(&diag, "メッセージ", value_string(eval->error_message));
    dict_set(&diag, "file", value_string(file));
    dict_set(&diag, "ファイル", value_string(file));
    dict_set(&diag, "line", value_number(eval->error_line));
    dict_set(&diag, "行", value_number(eval->error_line));
    dict_set(&diag, "column", value_number(eval->error_column));
    dict_set(&diag, "列", value_number(eval->error_column));
    dict_set(&diag, "kind", value_string("runtime"));
    dict_set(&diag, "種類", value_string("runtime"));
    return diag;
}

static Value evaluate_try(Evaluator *eval, ASTNode *node) {
    Value result = value_null();
    
    // 試行ブロックを実行
    evaluate(eval, node->try_stmt.try_block);
    
    bool caught_runtime_error = false;
    if (eval->had_error && node->try_stmt.catch_block != NULL) {
        Value diagnostic = make_runtime_diagnostic_value(eval);
        evaluator_clear_error(eval);
        caught_runtime_error = true;

        Environment *catch_scope = env_new(eval->current);
        if (node->try_stmt.catch_var != NULL) {
            env_define(catch_scope, node->try_stmt.catch_var, diagnostic, false);
        } else {
            value_free(&diagnostic);
        }

        Environment *prev = eval->current;
        eval->current = catch_scope;
        evaluate(eval, node->try_stmt.catch_block);
        eval->current = prev;
        env_release(catch_scope);
    }

    // 例外が発生した場合
    if (!caught_runtime_error && eval->throwing && node->try_stmt.catch_block != NULL) {
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
        env_release(catch_scope);
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
static Value evaluate_string_interpolation(Evaluator *eval, const char *str, int line, int column) {
    // {を含まない場合はそのまま返す
    if (strchr(str, '{') == NULL) {
        return value_string(str);
    }
    
    int capacity = STRING_INTERPOLATION_INITIAL_CAPACITY;
    int length = 0;
    char *result = malloc(capacity);
    result[0] = '\0';
    
    const char *p = str;
    while (*p) {
        if (*p == '\\' && *(p + 1) == '{') {
            // エスケープされた{はそのまま
            if (length + 1 >= capacity) {
                ARRAY_GROW(result, length + 1, capacity, char, free(result); return value_string(""));
            }
            result[length++] = '{';
            p += 2;
        } else if (*p == '{') {
            int brace_col = column + 1 + diag_utf8_strlen(str, (int)(p - str));
            p++;  // { をスキップ
            
            // } までの式を取得
            const char *expr_start = p;
            int brace_depth = 1;
            while (*p && brace_depth > 0) {
                if (*p == '"') {
                    // 文字列リテラル内の { } は補間対象外
                    p++;
                    while (*p && *p != '"') {
                        if (*p == '\\' && *(p + 1)) p++;
                        p++;
                    }
                    if (*p == '"') p++;
                    continue;
                }
                if (*p == '{') brace_depth++;
                else if (*p == '}') brace_depth--;
                if (brace_depth > 0) p++;
            }
            
            if (brace_depth != 0) {
                runtime_error(eval, line, brace_col, "文字列補間の '}' が閉じられていません");
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
            
            // 式が全テキストを消費し、エラーなく解析できた場合のみ補間
            if (!parser_had_error(&expr_parser) && 
                expr_parser.current.type == TOKEN_EOF) {
                Value val = evaluate(eval, expr);
                if (!eval->had_error) {
                    char *val_str = value_to_string(val);
                    int val_len = strlen(val_str);
                    while (length + val_len + 1 >= capacity) {
                        ARRAY_GROW(result, length + val_len + 1, capacity, char, free(result); free(val_str); return value_string(""));
                    }
                    memcpy(result + length, val_str, val_len);
                    length += val_len;
                    free(val_str);
                }
            } else {
                // 補間式として無効 → { と内容をリテラルとしてコピー
                int total = 1 + expr_len + 1; // { + 内容 + }
                while (length + total + 1 >= capacity) {
                    ARRAY_GROW(result, length + total + 1, capacity, char, free(result); return value_string(""));
                }
                result[length++] = '{';
                memcpy(result + length, expr_str, expr_len);
                length += expr_len;
                result[length++] = '}';
            }
            
            node_free(expr);
            parser_free(&expr_parser);
            free(expr_str);
        } else {
            // UTF-8マルチバイト文字を正しくコピー
            int char_len = utf8_char_length((unsigned char)*p);
            if (char_len == 0) char_len = 1;
            while (length + char_len + 1 >= capacity) {
                ARRAY_GROW(result, length + char_len + 1, capacity, char, free(result); return value_string(""));
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
        runtime_error(eval, node->location.line, node->location.column,
                     "反復できるのは配列、文字列、辞書のみです");
    }
    
    eval->current = prev;
    env_release(loop_env);
    
    return result;
}

static Value evaluate_list_comprehension(Evaluator *eval, ASTNode *node) {
    // リスト内包表記: [expr for var in iterable] または [expr for var in iterable if condition]
    
    // 反復対象を評価
    Value iterable = evaluate(eval, node->list_comp.iterable);
    if (eval->had_error) return value_null();
    
    // 結果配列を初期化
    Value result = value_array_with_capacity(16);
    
    // ループ用の新しいスコープを作成
    Environment *loop_env = env_new(eval->current);
    Environment *prev = eval->current;
    eval->current = loop_env;
    
    if (iterable.type == VALUE_ARRAY) {
        for (int i = 0; i < iterable.array.length; i++) {
            // ループ変数を定義
            env_define(eval->current, node->list_comp.var_name,
                      value_copy(iterable.array.elements[i]), false);
            
            // 条件式を評価（あれば）
            if (node->list_comp.condition != NULL) {
                Value cond = evaluate(eval, node->list_comp.condition);
                if (eval->had_error) goto cleanup;
                
                bool include = value_is_truthy(cond);
                value_free(&cond);
                
                if (!include) continue;  // この要素はスキップ
            }
            
            // 式を評価して結果に追加
            Value expr_result = evaluate(eval, node->list_comp.expression);
            if (eval->had_error) goto cleanup;
            
            array_push(&result, expr_result);
        }
    } else if (iterable.type == VALUE_STRING) {
        // 文字列の各文字をループ
        int len = string_length(&iterable);
        for (int i = 0; i < len; i++) {
            Value ch = string_substring(&iterable, i, i + 1);
            env_define(eval->current, node->list_comp.var_name, ch, false);
            
            // 条件式を評価（あれば）
            if (node->list_comp.condition != NULL) {
                Value cond = evaluate(eval, node->list_comp.condition);
                if (eval->had_error) {
                    goto cleanup;
                }
                
                bool include = value_is_truthy(cond);
                value_free(&cond);
                
                if (!include) {
                    continue;
                }
            }
            
            // 式を評価して結果に追加
            Value expr_result = evaluate(eval, node->list_comp.expression);
            if (eval->had_error) {
                goto cleanup;
            }
            
            array_push(&result, expr_result);
        }
    } else if (iterable.type == VALUE_DICT) {
        // 辞書のキーをループ
        for (int i = 0; i < iterable.dict.length; i++) {
            if (iterable.dict.keys[i] != NULL) {
                Value key_val = value_string(iterable.dict.keys[i]);
                env_define(eval->current, node->list_comp.var_name, key_val, false);
                
                // 条件式を評価（あれば）
                if (node->list_comp.condition != NULL) {
                    Value cond = evaluate(eval, node->list_comp.condition);
                    if (eval->had_error) {
                        goto cleanup;
                    }
                    
                    bool include = value_is_truthy(cond);
                    value_free(&cond);
                    
                    if (!include) {
                        continue;
                    }
                }
                
                // 式を評価して結果に追加
                Value expr_result = evaluate(eval, node->list_comp.expression);
                if (eval->had_error) {
                    goto cleanup;
                }
                
                array_push(&result, expr_result);
            }
        }
    } else {
        runtime_error(eval, node->location.line, node->location.column,
                     "リスト内包表記で反復できるのは配列、文字列、辞書のみです");
        goto cleanup;
    }
    
    eval->current = prev;
    env_release(loop_env);
    
    // 結果配列を返す
    return result;

cleanup:
    // エラーが発生した場合のクリーンアップ
    value_free(&result);
    eval->current = prev;
    env_release(loop_env);
    return value_null();
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
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        return value_number(argv[0].numeric_array.length);
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
    return value_string(value_runtime_type_name(argv[0]));
}

static Value builtin_dtype(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        return value_string(numeric_dtype_name(argv[0].numeric_array.dtype));
    }
    if (argv[0].type == VALUE_MATRIX) {
        return value_string(numeric_dtype_name(argv[0].matrix.dtype));
    }
    builtin_runtime_error("dtype は数値ベクトルまたは行列に使います（実際: %s）",
                          value_type_name(argv[0].type));
    return value_null();
}

static Value builtin_dtype_size(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == VALUE_STRING) {
        NumericDType dtype;
        if (!numeric_dtype_from_name(argv[0].string.data, &dtype)) {
            builtin_runtime_error("未知の dtype です: %s（利用可能: f64, f32, i64, i32, bool）",
                                  argv[0].string.data);
            return value_null();
        }
        return value_number(numeric_dtype_size(dtype));
    }
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        return value_number(numeric_dtype_size(argv[0].numeric_array.dtype));
    }
    if (argv[0].type == VALUE_MATRIX) {
        return value_number(numeric_dtype_size(argv[0].matrix.dtype));
    }
    builtin_runtime_error("dtype_size は dtype 名、数値ベクトル、行列に使います（実際: %s）",
                          value_type_name(argv[0].type));
    return value_null();
}

static Value builtin_nbytes(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        return value_number((double)argv[0].numeric_array.length *
                            numeric_dtype_size(argv[0].numeric_array.dtype));
    }
    if (argv[0].type == VALUE_MATRIX) {
        return value_number((double)argv[0].matrix.rows * (double)argv[0].matrix.cols *
                            numeric_dtype_size(argv[0].matrix.dtype));
    }
    builtin_runtime_error("nbytes は数値ベクトルまたは行列に使います（実際: %s）",
                          value_type_name(argv[0].type));
    return value_null();
}

static Value builtin_storage_bytes(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        return value_number((double)argv[0].numeric_array.capacity *
                            (double)numeric_dtype_size(argv[0].numeric_array.dtype));
    }
    if (argv[0].type == VALUE_MATRIX) {
        return value_number((double)argv[0].matrix.rows * (double)argv[0].matrix.cols *
                            (double)numeric_dtype_size(argv[0].matrix.dtype));
    }
    builtin_runtime_error("storage_bytes は数値ベクトルまたは行列に使います（実際: %s）",
                          value_type_name(argv[0].type));
    return value_null();
}

static Value builtin_astype(int argc, Value *argv) {
    (void)argc;
    if (argv[1].type != VALUE_STRING) {
        builtin_runtime_error("astype の第2引数は dtype 名の文字列です（例: \"f64\", \"f32\", \"i64\", \"i32\", \"bool\"）");
        return value_null();
    }

    NumericDType dtype;
    if (!numeric_dtype_from_name(argv[1].string.data, &dtype)) {
        builtin_runtime_error("未知の dtype です: %s（利用可能: f64, f32, i64, i32, bool）",
                              argv[1].string.data);
        return value_null();
    }

    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        Value result = value_numeric_array_with_dtype(argv[0].numeric_array.length, dtype);
        for (int i = 0; i < argv[0].numeric_array.length; i++) {
            numeric_array_push(&result, numeric_array_get(&argv[0], i));
        }
        return result;
    }
    if (argv[0].type == VALUE_MATRIX) {
        Value result = value_matrix_with_dtype(argv[0].matrix.rows, argv[0].matrix.cols, dtype);
        for (int r = 0; r < argv[0].matrix.rows; r++) {
            for (int c = 0; c < argv[0].matrix.cols; c++) {
                matrix_set(&result, r, c, matrix_get(&argv[0], r, c));
            }
        }
        return result;
    }

    builtin_runtime_error("astype の第1引数は数値ベクトルまたは行列でなければなりません（実際: %s）",
                          value_type_name(argv[0].type));
    return value_null();
}

static Value builtin_to_array(int argc, Value *argv) {
    (void)argc;

    if (argv[0].type == VALUE_ARRAY) {
        return value_copy(argv[0]);
    }

    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        Value result = value_array_with_capacity(argv[0].numeric_array.length);
        for (int i = 0; i < argv[0].numeric_array.length; i++) {
            array_push(&result, value_number(numeric_array_get(&argv[0], i)));
        }
        return result;
    }

    if (argv[0].type == VALUE_MATRIX) {
        Value result = value_array_with_capacity(argv[0].matrix.rows);
        for (int r = 0; r < argv[0].matrix.rows; r++) {
            Value row = value_array_with_capacity(argv[0].matrix.cols);
            for (int c = 0; c < argv[0].matrix.cols; c++) {
                array_push(&row, value_number(matrix_get(&argv[0], r, c)));
            }
            array_push(&result, row);
            value_free(&row);
        }
        return result;
    }

    return value_null();
}

// 型チェック関数
#define DEFINE_TYPE_CHECK_BUILTIN(name, condition) \
    static Value builtin_is_##name(int argc, Value *argv) { \
        (void)argc; \
        return value_bool(condition); \
    }

DEFINE_TYPE_CHECK_BUILTIN(number, argv[0].type == VALUE_NUMBER)
DEFINE_TYPE_CHECK_BUILTIN(string, argv[0].type == VALUE_STRING)
DEFINE_TYPE_CHECK_BUILTIN(bool, argv[0].type == VALUE_BOOL)
DEFINE_TYPE_CHECK_BUILTIN(array, argv[0].type == VALUE_ARRAY)
DEFINE_TYPE_CHECK_BUILTIN(numeric_array, argv[0].type == VALUE_NUMERIC_ARRAY)
DEFINE_TYPE_CHECK_BUILTIN(matrix, argv[0].type == VALUE_MATRIX)
DEFINE_TYPE_CHECK_BUILTIN(dict, argv[0].type == VALUE_DICT)
DEFINE_TYPE_CHECK_BUILTIN(function, argv[0].type == VALUE_FUNCTION || argv[0].type == VALUE_BUILTIN)
DEFINE_TYPE_CHECK_BUILTIN(null, argv[0].type == VALUE_NULL)

#undef DEFINE_TYPE_CHECK_BUILTIN

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
    
    int len = string_length(&argv[0]);
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
    
    return string_substring(&argv[0], start, start + sub_len);
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
    
    int char_len = string_length(&argv[0]);
    if (pos < 0 || pos >= char_len) return value_null();
    
    // 文字インデックスからUTF-8先頭バイト位置へ変換
    const unsigned char *str = (const unsigned char *)argv[0].string.data;
    const unsigned char *p = str;
    int i = 0;
    while (i < pos && *p) {
        if (*p < 0x80) p += 1;
        else if (*p < 0xE0) p += 2;
        else if (*p < 0xF0) p += 3;
        else p += 4;
        i++;
    }

    int byte_offset = (int)(p - str);
    int len = argv[0].string.byte_length;
    if (byte_offset >= len) return value_null();

    // UTF-8の先頭バイトからコードポイントを取得
    unsigned char c = str[byte_offset];
    int code = 0;
    if (c < 0x80) {
        code = c;
    } else if (c < 0xE0 && byte_offset + 1 < len) {
        code = ((c & 0x1F) << 6) | (str[byte_offset + 1] & 0x3F);
    } else if (c < 0xF0 && byte_offset + 2 < len) {
        code = ((c & 0x0F) << 12) | ((str[byte_offset + 1] & 0x3F) << 6) | (str[byte_offset + 2] & 0x3F);
    } else if (byte_offset + 3 < len) {
        code = ((c & 0x07) << 18) | ((str[byte_offset + 1] & 0x3F) << 12) | ((str[byte_offset + 2] & 0x3F) << 6) | (str[byte_offset + 3] & 0x3F);
    } else {
        return value_null();
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

typedef struct {
    const Value *value;
    bool occupied;
} UniqueEntry;

static uint32_t unique_hash_bytes(const char *bytes, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (unsigned char)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t unique_mix_hash(uint32_t hash, uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

static uint32_t unique_hash_value(Value value) {
    uint32_t hash = unique_mix_hash(2166136261u, (uint32_t)value.type);

    switch (value.type) {
        case VALUE_NULL:
            return hash;
        case VALUE_NUMBER: {
            double normalized = value.number == 0.0 ? 0.0 : value.number;
            uint64_t bits = 0;
            memcpy(&bits, &normalized, sizeof(bits));
            hash = unique_mix_hash(hash, (uint32_t)(bits & 0xffffffffu));
            return unique_mix_hash(hash, (uint32_t)(bits >> 32));
        }
        case VALUE_BOOL:
            return unique_mix_hash(hash, value.boolean ? 1u : 0u);
        case VALUE_STRING:
            return unique_mix_hash(hash, unique_hash_bytes(value.string.data, value.string.byte_length));
        case VALUE_ARRAY:
            hash = unique_mix_hash(hash, (uint32_t)value.array.length);
            for (int i = 0; i < value.array.length; i++) {
                hash = unique_mix_hash(hash, unique_hash_value(value.array.elements[i]));
            }
            return hash;
        case VALUE_NUMERIC_ARRAY:
            hash = unique_mix_hash(hash, (uint32_t)value.numeric_array.length);
            for (int i = 0; i < value.numeric_array.length; i++) {
                double item = numeric_array_get(&value, i);
                double normalized = item == 0.0 ? 0.0 : item;
                uint64_t bits = 0;
                memcpy(&bits, &normalized, sizeof(bits));
                hash = unique_mix_hash(hash, (uint32_t)(bits & 0xffffffffu));
                hash = unique_mix_hash(hash, (uint32_t)(bits >> 32));
            }
            return hash;
        case VALUE_MATRIX:
            hash = unique_mix_hash(hash, (uint32_t)value.matrix.rows);
            hash = unique_mix_hash(hash, (uint32_t)value.matrix.cols);
            for (int r = 0; r < value.matrix.rows; r++) {
                for (int c = 0; c < value.matrix.cols; c++) {
                    double item = matrix_get(&value, r, c);
                    double normalized = item == 0.0 ? 0.0 : item;
                    uint64_t bits = 0;
                    memcpy(&bits, &normalized, sizeof(bits));
                    hash = unique_mix_hash(hash, (uint32_t)(bits & 0xffffffffu));
                    hash = unique_mix_hash(hash, (uint32_t)(bits >> 32));
                }
            }
            return hash;
        case VALUE_DICT:
            hash = unique_mix_hash(hash, (uint32_t)value.dict.length);
            for (int i = 0; i < value.dict.length; i++) {
                hash = unique_mix_hash(hash, unique_hash_bytes(value.dict.keys[i], (int)strlen(value.dict.keys[i])));
                hash = unique_mix_hash(hash, unique_hash_value(value.dict.values[i]));
            }
            return hash;
        case VALUE_FUNCTION:
            return unique_mix_hash(hash, (uint32_t)(uintptr_t)value.function.definition);
        case VALUE_BUILTIN:
            return unique_mix_hash(hash, (uint32_t)(uintptr_t)value.builtin.fn);
        case VALUE_CLASS:
            return unique_mix_hash(hash, (uint32_t)(uintptr_t)value.class_value.definition);
        case VALUE_INSTANCE:
            return unique_mix_hash(hash, (uint32_t)(uintptr_t)value.instance.class_ref);
        case VALUE_GENERATOR:
            return unique_mix_hash(hash, (uint32_t)(uintptr_t)value.generator.state);
    }

    return hash;
}

static int unique_table_capacity(int length) {
    int capacity = 16;
    while (capacity < length * 2) {
        capacity *= 2;
    }
    return capacity;
}

static bool unique_seen_or_add(UniqueEntry *entries, int capacity, const Value *value) {
    uint32_t index = unique_hash_value(*value) & (uint32_t)(capacity - 1);
    for (int probe = 0; probe < capacity; probe++) {
        UniqueEntry *entry = &entries[index];
        if (!entry->occupied) {
            entry->value = value;
            entry->occupied = true;
            return false;
        }
        if (value_equals(*entry->value, *value)) {
            return true;
        }
        index = (index + 1) & (uint32_t)(capacity - 1);
    }
    return false;
}

// 一意: 配列の重複を除去
static Value builtin_unique(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_array();
    
    Value result = value_array();
    int capacity = unique_table_capacity(argv[0].array.length);
    UniqueEntry *entries = calloc((size_t)capacity, sizeof(UniqueEntry));
    if (entries == NULL) return result;

    for (int i = 0; i < argv[0].array.length; i++) {
        if (!unique_seen_or_add(entries, capacity, &argv[0].array.elements[i])) {
            array_push(&result, value_copy(argv[0].array.elements[i]));
        }
    }
    free(entries);
    
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
            runtime_error(g_eval, 0, 0, "%s", msg);
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
    
    // 型エイリアスを解決
    for (int i = 0; i < g_type_alias_count; i++) {
        if (strcmp(g_type_aliases[i].alias, type_name) == 0) {
            type_name = g_type_aliases[i].original;
            break;
        }
    }
    
    if (strcmp(type_name, "整数") == 0) return value_bool(argv[0].type == VALUE_NUMBER && argv[0].is_integer);
    if (strcmp(type_name, "小数") == 0) return value_bool(argv[0].type == VALUE_NUMBER && !argv[0].is_integer);
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

// =============================================================================
// 演算子オーバーロード
// =============================================================================

// インスタンスの演算子メソッドを呼ぶ（見つからなければ VALUE_NULL + g_eval->had_error=true）
static Value call_instance_operator(Value *instance, const char *method_name, Value *arg) {
    if (g_eval == NULL || instance->type != VALUE_INSTANCE) {
        // フォールバック: メソッドが見つからない
        return value_null();
    }
    
    Value *class_ref = instance->instance.class_ref;
    while (class_ref != NULL && class_ref->type == VALUE_CLASS) {
        ASTNode *class_def = class_ref->class_value.definition;
        for (int i = 0; i < class_def->class_def.method_count; i++) {
            ASTNode *method = class_def->class_def.methods[i];
            if (strcmp(method->method.name, method_name) == 0) {
                // メソッドを呼び出す
                Environment *method_env = env_new(g_eval->current);
                Environment *saved = g_eval->current;
                Value *saved_instance = g_eval->current_instance;
                
                g_eval->current = method_env;
                g_eval->current_instance = instance;
                env_define(method_env, "自分", value_copy(*instance), false);
                
                // 引数をバインド
                ASTNode *func_body = method->method.body;
                if (method->method.param_count > 0) {
                    env_define(method_env, method->method.params[0].name,
                              value_copy(*arg), false);
                }
                
                Value ret = evaluate(g_eval, func_body);
                
                if (g_eval->returning) {
                    ret = g_eval->return_value;
                    g_eval->returning = false;
                }
                
                g_eval->current = saved;
                g_eval->current_instance = saved_instance;
                env_release(method_env);
                
                return ret;
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
    
    // メソッドが見つからなかった場合
    g_eval->had_error = true;
    return value_null();
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
                env_release(method_env);
                
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

    if (argc == 1 && argv[0].type == VALUE_NUMERIC_ARRAY) {
        if (argv[0].numeric_array.length == 0) return value_null();
        double max = numeric_array_get(&argv[0], 0);
        for (int i = 1; i < argv[0].numeric_array.length; i++) {
            double item = numeric_array_get(&argv[0], i);
            if (item > max) {
                max = item;
            }
        }
        return value_number(max);
    }
    
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

    if (argc == 1 && argv[0].type == VALUE_NUMERIC_ARRAY) {
        if (argv[0].numeric_array.length == 0) return value_null();
        double min = numeric_array_get(&argv[0], 0);
        for (int i = 1; i < argv[0].numeric_array.length; i++) {
            double item = numeric_array_get(&argv[0], i);
            if (item < min) {
                min = item;
            }
        }
        return value_number(min);
    }
    
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

static bool vector_length_arg(Value v, int *out) {
    if (v.type != VALUE_NUMBER || !v.is_integer) return false;
    if (v.number < 0 || v.number > 1000000) return false;
    *out = (int)v.number;
    return true;
}

static Value builtin_vector(int argc, Value *argv) {
    (void)argc;

    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        return value_copy(argv[0]);
    }

    if (argv[0].type != VALUE_ARRAY) {
        return value_null();
    }

    Value result = value_numeric_array_with_capacity(argv[0].array.length);
    for (int i = 0; i < argv[0].array.length; i++) {
        Value item = argv[0].array.elements[i];
        if (item.type != VALUE_NUMBER) {
            value_free(&result);
            return value_null();
        }
        numeric_array_push(&result, item.number);
    }

    return result;
}

static Value builtin_zeros(int argc, Value *argv) {
    (void)argc;
    int length = 0;
    if (!vector_length_arg(argv[0], &length)) return value_null();

    Value result = value_numeric_array_with_capacity(length);
    for (int i = 0; i < length; i++) {
        numeric_array_push(&result, 0.0);
    }
    return result;
}

static Value builtin_ones(int argc, Value *argv) {
    (void)argc;
    int length = 0;
    if (!vector_length_arg(argv[0], &length)) return value_null();

    Value result = value_numeric_array_with_capacity(length);
    for (int i = 0; i < length; i++) {
        numeric_array_push(&result, 1.0);
    }
    return result;
}

static Value builtin_range_vector(int argc, Value *argv) {
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

    int count = 0;
    if (step > 0) {
        for (double i = start; i < end; i += step) count++;
    } else {
        for (double i = start; i > end; i += step) count++;
    }

    if (count <= 0) return value_numeric_array();
    if (count > 1000000) return value_null();

    Value result = value_numeric_array_with_capacity(count);
    if (step > 0) {
        for (double i = start; i < end; i += step) {
            numeric_array_push(&result, i);
        }
    } else {
        for (double i = start; i > end; i += step) {
            numeric_array_push(&result, i);
        }
    }

    return result;
}

static Value builtin_vector_sum(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMERIC_ARRAY) return value_null();

    double total = 0.0;
    for (int i = 0; i < argv[0].numeric_array.length; i++) {
        total += numeric_array_get(&argv[0], i);
    }
    return value_number(total);
}

static Value builtin_mean(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        int n = argv[0].numeric_array.length;
        if (n == 0) return value_null();
        double total = 0.0;
        for (int i = 0; i < n; i++) total += numeric_array_get(&argv[0], i);
        return value_number(total / n);
    }

    if (argv[0].type == VALUE_ARRAY) {
        int n = argv[0].array.length;
        if (n == 0) return value_null();
        double total = 0.0;
        for (int i = 0; i < n; i++) {
            if (argv[0].array.elements[i].type != VALUE_NUMBER) return value_null();
            total += argv[0].array.elements[i].number;
        }
        return value_number(total / n);
    }

    return value_null();
}

static Value builtin_variance(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMERIC_ARRAY) return value_null();

    int n = argv[0].numeric_array.length;
    if (n == 0) return value_null();

    double mean = 0.0;
    for (int i = 0; i < n; i++) mean += numeric_array_get(&argv[0], i);
    mean /= n;

    double total = 0.0;
    for (int i = 0; i < n; i++) {
        double delta = numeric_array_get(&argv[0], i) - mean;
        total += delta * delta;
    }

    return value_number(total / n);
}

static Value builtin_std(int argc, Value *argv) {
    Value variance = builtin_variance(argc, argv);
    if (variance.type != VALUE_NUMBER) return value_null();
    return value_number(sqrt(variance.number));
}

static int compare_double_values(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static bool copy_numeric_values(Value input, const char *name, double **out, int *length) {
    *out = NULL;
    *length = 0;

    if (input.type == VALUE_NUMERIC_ARRAY) {
        int n = input.numeric_array.length;
        if (n <= 0) {
            builtin_runtime_error("%s は空の数値ベクトルを扱えません", name);
            return false;
        }
        double *data = malloc(sizeof(double) * (size_t)n);
        if (data == NULL) {
            builtin_runtime_error("%s の作業メモリを確保できませんでした", name);
            return false;
        }
        for (int i = 0; i < n; i++) {
            data[i] = numeric_array_get(&input, i);
        }
        *out = data;
        *length = n;
        return true;
    }

    if (input.type == VALUE_ARRAY) {
        int n = input.array.length;
        if (n <= 0) {
            builtin_runtime_error("%s は空の配列を扱えません", name);
            return false;
        }
        double *data = malloc(sizeof(double) * (size_t)n);
        if (data == NULL) {
            builtin_runtime_error("%s の作業メモリを確保できませんでした", name);
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (input.array.elements[i].type != VALUE_NUMBER) {
                free(data);
                builtin_runtime_error("%s の配列要素はすべて数値でなければなりません（%d番目: %s）",
                                      name, i, value_type_name(input.array.elements[i].type));
                return false;
            }
            data[i] = input.array.elements[i].number;
        }
        *out = data;
        *length = n;
        return true;
    }

    builtin_runtime_error("%s の引数は数値ベクトルまたは数値配列でなければなりません（実際: %s）",
                          name, value_type_name(input.type));
    return false;
}

static Value builtin_quantile(int argc, Value *argv) {
    (void)argc;
    if (argv[1].type != VALUE_NUMBER) {
        builtin_runtime_error("quantile の第2引数は 0 から 1 の数値でなければなりません（実際: %s）",
                              value_type_name(argv[1].type));
        return value_null();
    }

    double q = argv[1].number;
    if (q < 0.0 || q > 1.0 || isnan(q)) {
        builtin_runtime_error("quantile の第2引数は 0 から 1 の範囲で指定してください（実際: %g）", q);
        return value_null();
    }

    double *data = NULL;
    int n = 0;
    if (!copy_numeric_values(argv[0], "quantile", &data, &n)) return value_null();

    qsort(data, (size_t)n, sizeof(double), compare_double_values);
    double pos = q * (double)(n - 1);
    int lower = (int)floor(pos);
    int upper = (int)ceil(pos);
    double frac = pos - lower;
    double value = data[lower] * (1.0 - frac) + data[upper] * frac;
    free(data);
    return value_number(value);
}

static Value builtin_median(int argc, Value *argv) {
    (void)argc;
    Value q = value_number(0.5);
    Value args[2] = { argv[0], q };
    return builtin_quantile(2, args);
}

static Value builtin_normalize(int argc, Value *argv) {
    (void)argc;
    double *data = NULL;
    int n = 0;
    if (!copy_numeric_values(argv[0], "normalize", &data, &n)) return value_null();

    double total = 0.0;
    for (int i = 0; i < n; i++) total += data[i];
    double mean = total / n;

    double variance = 0.0;
    for (int i = 0; i < n; i++) {
        double delta = data[i] - mean;
        variance += delta * delta;
    }
    variance /= n;
    double std = sqrt(variance);

    Value result = value_numeric_array_with_capacity(n);
    for (int i = 0; i < n; i++) {
        numeric_array_push(&result, std == 0.0 ? 0.0 : (data[i] - mean) / std);
    }
    free(data);
    return result;
}

static Value builtin_norm(int argc, Value *argv) {
    double *data = NULL;
    int n = 0;
    if (!copy_numeric_values(argv[0], "norm", &data, &n)) return value_null();

    double p = 2.0;
    if (argc >= 2) {
        if (argv[1].type != VALUE_NUMBER || argv[1].number <= 0.0 || isnan(argv[1].number)) {
            free(data);
            builtin_runtime_error("norm の第2引数 p は 0 より大きい数値でなければなりません（実際: %s）",
                                  value_type_name(argv[1].type));
            return value_null();
        }
        p = argv[1].number;
    }

    double result = 0.0;
    if (p == 1.0) {
        for (int i = 0; i < n; i++) result += fabs(data[i]);
    } else if (p == 2.0) {
        for (int i = 0; i < n; i++) result += data[i] * data[i];
        result = sqrt(result);
    } else {
        for (int i = 0; i < n; i++) result += pow(fabs(data[i]), p);
        result = pow(result, 1.0 / p);
    }
    free(data);
    return value_number(result);
}

static Value builtin_minmax_scale(int argc, Value *argv) {
    (void)argc;
    double *data = NULL;
    int n = 0;
    if (!copy_numeric_values(argv[0], "minmax_scale", &data, &n)) return value_null();

    double min_value = data[0];
    double max_value = data[0];
    for (int i = 1; i < n; i++) {
        if (data[i] < min_value) min_value = data[i];
        if (data[i] > max_value) max_value = data[i];
    }

    double range = max_value - min_value;
    Value result = value_numeric_array_with_capacity(n);
    for (int i = 0; i < n; i++) {
        numeric_array_push(&result, range == 0.0 ? 0.0 : (data[i] - min_value) / range);
    }
    free(data);
    return result;
}

static Value builtin_clip(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMERIC_ARRAY) {
        builtin_runtime_error("clip の第1引数は数値ベクトルでなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }
    if (argv[1].type != VALUE_NUMBER || argv[2].type != VALUE_NUMBER) {
        builtin_runtime_error("clip は (数値ベクトル, 最小値, 最大値) の形で呼び出してください");
        return value_null();
    }
    double min_value = argv[1].number;
    double max_value = argv[2].number;
    if (min_value > max_value) {
        builtin_runtime_error("clip の最小値は最大値以下でなければなりません（最小: %g, 最大: %g）",
                              min_value, max_value);
        return value_null();
    }

    Value result = value_numeric_array_with_capacity(argv[0].numeric_array.length);
    for (int i = 0; i < argv[0].numeric_array.length; i++) {
        double value = numeric_array_get(&argv[0], i);
        if (value < min_value) value = min_value;
        if (value > max_value) value = max_value;
        numeric_array_push(&result, value);
    }
    return result;
}

static bool copy_numeric_pair(Value left_value, Value right_value, const char *name,
                              double **left_out, double **right_out, int *length_out) {
    double *left = NULL;
    double *right = NULL;
    int n = 0;
    int m = 0;
    if (!copy_numeric_values(left_value, name, &left, &n)) return false;
    if (!copy_numeric_values(right_value, name, &right, &m)) {
        free(left);
        return false;
    }
    if (n != m) {
        free(left);
        free(right);
        builtin_runtime_error("%s の左右のベクトル長が一致しません（左: %d, 右: %d）", name, n, m);
        return false;
    }
    *left_out = left;
    *right_out = right;
    *length_out = n;
    return true;
}

static double numeric_covariance_population(const double *left, const double *right, int n,
                                            double *var_left_out, double *var_right_out) {
    double mean_left = 0.0;
    double mean_right = 0.0;
    for (int i = 0; i < n; i++) {
        mean_left += left[i];
        mean_right += right[i];
    }
    mean_left /= n;
    mean_right /= n;

    double covariance = 0.0;
    double var_left = 0.0;
    double var_right = 0.0;
    for (int i = 0; i < n; i++) {
        double dl = left[i] - mean_left;
        double dr = right[i] - mean_right;
        covariance += dl * dr;
        var_left += dl * dl;
        var_right += dr * dr;
    }

    if (var_left_out != NULL) *var_left_out = var_left;
    if (var_right_out != NULL) *var_right_out = var_right;
    return covariance / n;
}

static Value builtin_covariance(int argc, Value *argv) {
    (void)argc;
    double *left = NULL;
    double *right = NULL;
    int n = 0;
    if (!copy_numeric_pair(argv[0], argv[1], "covariance", &left, &right, &n)) {
        return value_null();
    }

    double covariance = numeric_covariance_population(left, right, n, NULL, NULL);
    free(left);
    free(right);
    return value_number(covariance);
}

static Value builtin_correlation(int argc, Value *argv) {
    (void)argc;
    double *left = NULL;
    double *right = NULL;
    int n = 0;
    if (!copy_numeric_pair(argv[0], argv[1], "correlation", &left, &right, &n)) {
        return value_null();
    }

    double var_left = 0.0;
    double var_right = 0.0;
    double covariance = numeric_covariance_population(left, right, n, &var_left, &var_right);
    free(left);
    free(right);

    double denom = sqrt(var_left * var_right);
    if (denom == 0.0) {
        builtin_runtime_error("correlation は分散が0のデータでは計算できません");
        return value_null();
    }
    return value_number((covariance * n) / denom);
}

static Value builtin_histogram(int argc, Value *argv) {
    (void)argc;
    if (argv[1].type != VALUE_NUMBER || !argv[1].is_integer) {
        builtin_runtime_error("histogram の第2引数はビン数を表す整数でなければなりません（実際: %s）",
                              value_type_name(argv[1].type));
        return value_null();
    }

    int bins = (int)argv[1].number;
    if (bins <= 0 || bins > 100000) {
        builtin_runtime_error("histogram のビン数は 1 から 100000 の範囲で指定してください（実際: %d）", bins);
        return value_null();
    }

    double *data = NULL;
    int n = 0;
    if (!copy_numeric_values(argv[0], "histogram", &data, &n)) return value_null();

    double min_value = data[0];
    double max_value = data[0];
    for (int i = 1; i < n; i++) {
        if (data[i] < min_value) min_value = data[i];
        if (data[i] > max_value) max_value = data[i];
    }

    Value counts = value_numeric_array_with_capacity(bins);
    Value edges = value_numeric_array_with_capacity(bins + 1);
    if (counts.type != VALUE_NUMERIC_ARRAY || edges.type != VALUE_NUMERIC_ARRAY) {
        free(data);
        value_free(&counts);
        value_free(&edges);
        builtin_runtime_error("histogram の結果配列を作成できませんでした");
        return value_null();
    }

    for (int i = 0; i < bins; i++) numeric_array_push(&counts, 0.0);

    double width = (max_value - min_value) / bins;
    if (width == 0.0) width = 1.0;
    for (int i = 0; i <= bins; i++) {
        numeric_array_push(&edges, min_value + width * i);
    }

    for (int i = 0; i < n; i++) {
        int index = (int)((data[i] - min_value) / width);
        if (index < 0) index = 0;
        if (index >= bins) index = bins - 1;
        numeric_array_set(&counts, index, numeric_array_get(&counts, index) + 1.0);
    }
    free(data);

    Value result = value_dict();
    dict_set(&result, "counts", counts);
    dict_set(&result, "件数", counts);
    dict_set(&result, "edges", edges);
    dict_set(&result, "境界", edges);
    dict_set(&result, "min", value_number(min_value));
    dict_set(&result, "最小", value_number(min_value));
    dict_set(&result, "max", value_number(max_value));
    dict_set(&result, "最大", value_number(max_value));
    dict_set(&result, "bin_width", value_number(width));
    dict_set(&result, "ビン幅", value_number(width));
    value_free(&counts);
    value_free(&edges);
    return result;
}

static Value split_numeric_array(Value input, int start, int length) {
    if (length <= 0) return value_numeric_array();
    Value result = value_numeric_array_with_dtype(length, input.numeric_array.dtype);
    for (int i = 0; i < length; i++) {
        numeric_array_push(&result, numeric_array_get(&input, start + i));
    }
    return result;
}

static Value split_matrix_rows(Value input, int start, int rows) {
    if (rows <= 0) return value_matrix(0, input.matrix.cols);
    int cols = input.matrix.cols;
    Value result = value_matrix_with_dtype(rows, cols, input.matrix.dtype);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            matrix_set(&result, r, c, matrix_get(&input, start + r, c));
        }
    }
    return result;
}

static unsigned int split_next_random(unsigned int *state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static void split_shuffle_indices(int *indices, int count, unsigned int seed) {
    unsigned int state = seed == 0 ? 1u : seed;
    for (int i = count - 1; i > 0; i--) {
        int j = (int)(split_next_random(&state) % (unsigned int)(i + 1));
        int tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }
}

static Value split_numeric_array_by_indices(Value input, int *indices, int start, int length) {
    Value result = value_numeric_array_with_capacity(length > 0 ? length : 0);
    if (result.type != VALUE_NUMERIC_ARRAY) return result;
    for (int i = 0; i < length; i++) {
        numeric_array_push(&result, numeric_array_get(&input, indices[start + i]));
    }
    return result;
}

static Value split_matrix_rows_by_indices(Value input, int *indices, int start, int rows) {
    Value result = value_matrix(rows, input.matrix.cols);
    if (result.type != VALUE_MATRIX) return result;
    for (int r = 0; r < rows; r++) {
        int src = indices[start + r];
        for (int c = 0; c < input.matrix.cols; c++) {
            matrix_set(&result, r, c, matrix_get(&input, src, c));
        }
    }
    return result;
}

static Value builtin_train_test_split(int argc, Value *argv) {
    if (argv[1].type != VALUE_NUMBER) {
        builtin_runtime_error("train_test_split の第2引数はテスト比率の数値でなければなりません（実際: %s）",
                              value_type_name(argv[1].type));
        return value_null();
    }

    double test_ratio = argv[1].number;
    if (test_ratio < 0.0 || test_ratio > 1.0 || isnan(test_ratio)) {
        builtin_runtime_error("train_test_split のテスト比率は 0 から 1 の範囲で指定してください（実際: %g）",
                              test_ratio);
        return value_null();
    }

    int total = 0;
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        total = argv[0].numeric_array.length;
    } else if (argv[0].type == VALUE_MATRIX) {
        total = argv[0].matrix.rows;
    } else {
        builtin_runtime_error("train_test_split の第1引数は数値ベクトルまたは行列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }

    int test_count = (int)floor(total * test_ratio);
    if (test_ratio > 0.0 && test_count == 0 && total > 0) test_count = 1;
    if (test_count > total) test_count = total;
    int train_count = total - test_count;

    int *indices = malloc(sizeof(int) * (size_t)(total > 0 ? total : 1));
    if (indices == NULL) {
        builtin_runtime_error("train_test_split の作業メモリを確保できませんでした");
        return value_null();
    }
    for (int i = 0; i < total; i++) indices[i] = i;

    bool shuffled = false;
    if (argc >= 3) {
        if (argv[2].type != VALUE_NUMBER || !argv[2].is_integer) {
            free(indices);
            builtin_runtime_error("train_test_split の第3引数はシャッフル用 seed の整数でなければなりません（実際: %s）",
                                  value_type_name(argv[2].type));
            return value_null();
        }
        split_shuffle_indices(indices, total, (unsigned int)argv[2].number);
        shuffled = true;
    }

    Value train;
    Value test;
    if (!shuffled && argv[0].type == VALUE_NUMERIC_ARRAY) {
        train = split_numeric_array(argv[0], 0, train_count);
        test = split_numeric_array(argv[0], train_count, test_count);
    } else if (!shuffled && argv[0].type == VALUE_MATRIX) {
        train = split_matrix_rows(argv[0], 0, train_count);
        test = split_matrix_rows(argv[0], train_count, test_count);
    } else if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        train = split_numeric_array_by_indices(argv[0], indices, 0, train_count);
        test = split_numeric_array_by_indices(argv[0], indices, train_count, test_count);
    } else {
        train = split_matrix_rows_by_indices(argv[0], indices, 0, train_count);
        test = split_matrix_rows_by_indices(argv[0], indices, train_count, test_count);
    }
    free(indices);

    Value result = value_dict();
    dict_set(&result, "train", train);
    dict_set(&result, "訓練", train);
    dict_set(&result, "test", test);
    dict_set(&result, "テスト", test);
    dict_set(&result, "train_count", value_number(train_count));
    dict_set(&result, "訓練件数", value_number(train_count));
    dict_set(&result, "test_count", value_number(test_count));
    dict_set(&result, "テスト件数", value_number(test_count));
    dict_set(&result, "shuffled", value_bool(shuffled));
    dict_set(&result, "シャッフル済み", value_bool(shuffled));
    value_free(&train);
    value_free(&test);
    return result;
}

static Value builtin_is_nan(int argc, Value *argv) {
    (void)argc;
    return value_bool(argv[0].type == VALUE_NUMBER && isnan(argv[0].number));
}

static Value builtin_drop_missing(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        Value result = value_numeric_array_with_capacity(argv[0].numeric_array.length);
        for (int i = 0; i < argv[0].numeric_array.length; i++) {
            double value = numeric_array_get(&argv[0], i);
            if (!isnan(value)) numeric_array_push(&result, value);
        }
        return result;
    }
    if (argv[0].type == VALUE_MATRIX) {
        int keep_rows = 0;
        for (int r = 0; r < argv[0].matrix.rows; r++) {
            bool keep = true;
            for (int c = 0; c < argv[0].matrix.cols; c++) {
                if (isnan(matrix_get(&argv[0], r, c))) {
                    keep = false;
                    break;
                }
            }
            if (keep) keep_rows++;
        }
        Value result = value_matrix(keep_rows, argv[0].matrix.cols);
        int out_r = 0;
        for (int r = 0; r < argv[0].matrix.rows; r++) {
            bool keep = true;
            for (int c = 0; c < argv[0].matrix.cols; c++) {
                if (isnan(matrix_get(&argv[0], r, c))) {
                    keep = false;
                    break;
                }
            }
            if (!keep) continue;
            for (int c = 0; c < argv[0].matrix.cols; c++) {
                matrix_set(&result, out_r, c, matrix_get(&argv[0], r, c));
            }
            out_r++;
        }
        return result;
    }
    builtin_runtime_error("drop_missing の引数は数値ベクトルまたは行列でなければなりません（実際: %s）",
                          value_type_name(argv[0].type));
    return value_null();
}

static Value builtin_fill_missing(int argc, Value *argv) {
    (void)argc;
    if (argv[1].type != VALUE_NUMBER) {
        builtin_runtime_error("fill_missing の第2引数は補完値の数値でなければなりません（実際: %s）",
                              value_type_name(argv[1].type));
        return value_null();
    }
    double fill = argv[1].number;
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        Value result = value_numeric_array_with_capacity(argv[0].numeric_array.length);
        for (int i = 0; i < argv[0].numeric_array.length; i++) {
            double value = numeric_array_get(&argv[0], i);
            numeric_array_push(&result, isnan(value) ? fill : value);
        }
        return result;
    }
    if (argv[0].type == VALUE_MATRIX) {
        Value result = value_matrix(argv[0].matrix.rows, argv[0].matrix.cols);
        for (int r = 0; r < argv[0].matrix.rows; r++) {
            for (int c = 0; c < argv[0].matrix.cols; c++) {
                double value = matrix_get(&argv[0], r, c);
                matrix_set(&result, r, c, isnan(value) ? fill : value);
            }
        }
        return result;
    }
    builtin_runtime_error("fill_missing の第1引数は数値ベクトルまたは行列でなければなりません（実際: %s）",
                          value_type_name(argv[0].type));
    return value_null();
}

static Value metric_pair(const char *name, Value left_value, Value right_value,
                         double (*fn)(const double *, const double *, int)) {
    double *left = NULL;
    double *right = NULL;
    int n = 0;
    if (!copy_numeric_pair(left_value, right_value, name, &left, &right, &n)) return value_null();
    double value = fn(left, right, n);
    free(left);
    free(right);
    return value_number(value);
}

static double metric_mse_impl(const double *actual, const double *pred, int n) {
    double total = 0.0;
    for (int i = 0; i < n; i++) {
        double d = actual[i] - pred[i];
        total += d * d;
    }
    return total / n;
}

static double metric_mae_impl(const double *actual, const double *pred, int n) {
    double total = 0.0;
    for (int i = 0; i < n; i++) total += fabs(actual[i] - pred[i]);
    return total / n;
}

static double metric_r2_impl(const double *actual, const double *pred, int n) {
    double mean = 0.0;
    for (int i = 0; i < n; i++) mean += actual[i];
    mean /= n;
    double ss_res = 0.0;
    double ss_tot = 0.0;
    for (int i = 0; i < n; i++) {
        double d = actual[i] - pred[i];
        ss_res += d * d;
        double t = actual[i] - mean;
        ss_tot += t * t;
    }
    return ss_tot == 0.0 ? 0.0 : 1.0 - (ss_res / ss_tot);
}

static Value builtin_mse(int argc, Value *argv) {
    (void)argc;
    return metric_pair("mse", argv[0], argv[1], metric_mse_impl);
}

static Value builtin_mae(int argc, Value *argv) {
    (void)argc;
    return metric_pair("mae", argv[0], argv[1], metric_mae_impl);
}

static Value builtin_r2_score(int argc, Value *argv) {
    (void)argc;
    return metric_pair("r2_score", argv[0], argv[1], metric_r2_impl);
}

static bool classification_counts(const char *name, Value actual_value, Value pred_value,
                                  int *tp_out, int *fp_out, int *tn_out, int *fn_out) {
    double *actual = NULL;
    double *pred = NULL;
    int n = 0;
    if (!copy_numeric_pair(actual_value, pred_value, name, &actual, &pred, &n)) return false;

    int tp = 0, fp = 0, tn = 0, fn = 0;
    for (int i = 0; i < n; i++) {
        bool a = actual[i] >= 0.5;
        bool p = pred[i] >= 0.5;
        if (a && p) tp++;
        else if (!a && p) fp++;
        else if (!a && !p) tn++;
        else fn++;
    }
    free(actual);
    free(pred);

    *tp_out = tp;
    *fp_out = fp;
    *tn_out = tn;
    *fn_out = fn;
    return true;
}

static Value classification_metric(const char *name, Value actual_value, Value pred_value, const char *kind) {
    int tp = 0, fp = 0, tn = 0, fn = 0;
    if (!classification_counts(name, actual_value, pred_value, &tp, &fp, &tn, &fn)) {
        return value_null();
    }

    int n = tp + fp + tn + fn;
    if (strcmp(kind, "accuracy") == 0) return value_number((double)(tp + tn) / n);
    if (strcmp(kind, "precision") == 0) return value_number((tp + fp) == 0 ? 0.0 : (double)tp / (tp + fp));
    if (strcmp(kind, "f1_score") == 0) {
        double precision = (tp + fp) == 0 ? 0.0 : (double)tp / (tp + fp);
        double recall = (tp + fn) == 0 ? 0.0 : (double)tp / (tp + fn);
        return value_number((precision + recall) == 0.0 ? 0.0 : 2.0 * precision * recall / (precision + recall));
    }
    return value_number((tp + fn) == 0 ? 0.0 : (double)tp / (tp + fn));
}

static Value builtin_accuracy(int argc, Value *argv) {
    (void)argc;
    return classification_metric("accuracy", argv[0], argv[1], "accuracy");
}

static Value builtin_precision(int argc, Value *argv) {
    (void)argc;
    return classification_metric("precision", argv[0], argv[1], "precision");
}

static Value builtin_recall(int argc, Value *argv) {
    (void)argc;
    return classification_metric("recall", argv[0], argv[1], "recall");
}

static Value builtin_f1_score(int argc, Value *argv) {
    (void)argc;
    return classification_metric("f1_score", argv[0], argv[1], "f1_score");
}

static Value builtin_confusion_matrix(int argc, Value *argv) {
    (void)argc;
    int tp = 0, fp = 0, tn = 0, fn = 0;
    if (!classification_counts("confusion_matrix", argv[0], argv[1], &tp, &fp, &tn, &fn)) {
        return value_null();
    }

    Value result = value_matrix(2, 2);
    matrix_set(&result, 0, 0, (double)tn);
    matrix_set(&result, 0, 1, (double)fp);
    matrix_set(&result, 1, 0, (double)fn);
    matrix_set(&result, 1, 1, (double)tp);
    return result;
}

typedef double (*VectorBinaryOp)(double a, double b);

static double vector_op_add(double a, double b) { return a + b; }
static double vector_op_sub(double a, double b) { return a - b; }
static double vector_op_mul(double a, double b) { return a * b; }
static double vector_op_div(double a, double b) { return b == 0.0 ? NAN : a / b; }

static Value vector_binary(const char *name, Value left, Value right, VectorBinaryOp op) {
    if (left.type != VALUE_NUMERIC_ARRAY) {
        builtin_runtime_error("%s の第1引数は数値ベクトルでなければなりません（実際: %s）",
                              name, value_type_name(left.type));
        return value_null();
    }

    int n = left.numeric_array.length;
    Value result = value_numeric_array_with_capacity(n);

    if (right.type == VALUE_NUMBER) {
        for (int i = 0; i < n; i++) {
            double value = op(numeric_array_get(&left, i), right.number);
            if (isnan(value)) {
                value_free(&result);
                builtin_runtime_error("%s の計算結果が不正です（%d番目の要素）。0除算や定義域外の値を確認してください",
                                      name, i);
                return value_null();
            }
            numeric_array_push(&result, value);
        }
        return result;
    }

    if (right.type == VALUE_NUMERIC_ARRAY) {
        if (n != right.numeric_array.length) {
            value_free(&result);
            builtin_runtime_error("%s の左右のベクトル長が一致しません（左: %d, 右: %d）",
                                  name, n, right.numeric_array.length);
            return value_null();
        }
        for (int i = 0; i < n; i++) {
            double value = op(numeric_array_get(&left, i), numeric_array_get(&right, i));
            if (isnan(value)) {
                value_free(&result);
                builtin_runtime_error("%s の計算結果が不正です（%d番目の要素）。0除算や定義域外の値を確認してください",
                                      name, i);
                return value_null();
            }
            numeric_array_push(&result, value);
        }
        return result;
    }

    value_free(&result);
    builtin_runtime_error("%s の第2引数は数値または数値ベクトルでなければなりません（実際: %s）",
                          name, value_type_name(right.type));
    return value_null();
}

static Value builtin_vector_add(int argc, Value *argv) {
    (void)argc;
    return vector_binary("vector_add", argv[0], argv[1], vector_op_add);
}

static Value builtin_vector_sub(int argc, Value *argv) {
    (void)argc;
    return vector_binary("vector_sub", argv[0], argv[1], vector_op_sub);
}

static Value builtin_vector_mul(int argc, Value *argv) {
    (void)argc;
    return vector_binary("vector_mul", argv[0], argv[1], vector_op_mul);
}

static Value builtin_vector_div(int argc, Value *argv) {
    (void)argc;
    return vector_binary("vector_div", argv[0], argv[1], vector_op_div);
}

typedef double (*VectorUnaryOp)(double x);

static Value vector_unary(const char *name, Value input, VectorUnaryOp op) {
    if (input.type != VALUE_NUMERIC_ARRAY) {
        builtin_runtime_error("%s の引数は数値ベクトルでなければなりません（実際: %s）",
                              name, value_type_name(input.type));
        return value_null();
    }

    Value result = value_numeric_array_with_capacity(input.numeric_array.length);
    for (int i = 0; i < input.numeric_array.length; i++) {
        double input_value = numeric_array_get(&input, i);
        double value = op(input_value);
        if (isnan(value)) {
            value_free(&result);
            builtin_runtime_error("%s の計算結果が不正です（%d番目の要素: %g）。平方根や対数の定義域を確認してください",
                                  name, i, input_value);
            return value_null();
        }
        numeric_array_push(&result, value);
    }
    return result;
}

static Value builtin_vector_abs(int argc, Value *argv) {
    (void)argc;
    return vector_unary("vector_abs", argv[0], fabs);
}

static Value builtin_vector_sqrt(int argc, Value *argv) {
    (void)argc;
    return vector_unary("vector_sqrt", argv[0], sqrt);
}

static Value builtin_vector_sin(int argc, Value *argv) {
    (void)argc;
    return vector_unary("vector_sin", argv[0], sin);
}

static Value builtin_vector_cos(int argc, Value *argv) {
    (void)argc;
    return vector_unary("vector_cos", argv[0], cos);
}

static Value builtin_vector_log(int argc, Value *argv) {
    (void)argc;
    return vector_unary("vector_log", argv[0], log);
}

static Value builtin_dot(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMERIC_ARRAY || argv[1].type != VALUE_NUMERIC_ARRAY) {
        builtin_runtime_error("dot の引数はどちらも数値ベクトルでなければなりません（第1引数: %s, 第2引数: %s）",
                              value_type_name(argv[0].type), value_type_name(argv[1].type));
        return value_null();
    }

    int n = argv[0].numeric_array.length;
    if (n != argv[1].numeric_array.length) {
        builtin_runtime_error("dot の左右のベクトル長が一致しません（左: %d, 右: %d）",
                              n, argv[1].numeric_array.length);
        return value_null();
    }

    double total = 0.0;
    for (int i = 0; i < n; i++) {
        total += numeric_array_get(&argv[0], i) * numeric_array_get(&argv[1], i);
    }
    return value_number(total);
}

static Value builtin_matrix(int argc, Value *argv) {
    (void)argc;

    if (argv[0].type == VALUE_MATRIX) {
        return value_copy(argv[0]);
    }

    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        Value result = value_matrix_with_dtype(1, argv[0].numeric_array.length, argv[0].numeric_array.dtype);
        for (int c = 0; c < argv[0].numeric_array.length; c++) {
            matrix_set(&result, 0, c, numeric_array_get(&argv[0], c));
        }
        return result;
    }

    if (argv[0].type != VALUE_ARRAY) {
        builtin_runtime_error("matrix の引数は配列、数値ベクトル、または行列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }

    int rows = argv[0].array.length;
    if (rows == 0) return value_matrix(0, 0);

    int cols = -1;
    for (int r = 0; r < rows; r++) {
        Value row = argv[0].array.elements[r];
        if (row.type == VALUE_NUMERIC_ARRAY) {
            if (cols < 0) cols = row.numeric_array.length;
            if (row.numeric_array.length != cols) {
                builtin_runtime_error("matrix の各行の長さが一致しません（1行目: %d列, %d行目: %d列）",
                                      cols, r + 1, row.numeric_array.length);
                return value_null();
            }
        } else if (row.type == VALUE_ARRAY) {
            if (cols < 0) cols = row.array.length;
            if (row.array.length != cols) {
                builtin_runtime_error("matrix の各行の長さが一致しません（1行目: %d列, %d行目: %d列）",
                                      cols, r + 1, row.array.length);
                return value_null();
            }
            for (int c = 0; c < row.array.length; c++) {
                if (row.array.elements[c].type != VALUE_NUMBER) {
                    builtin_runtime_error("matrix の要素はすべて数値でなければなりません（%d行%d列: %s）",
                                          r + 1, c + 1, value_type_name(row.array.elements[c].type));
                    return value_null();
                }
            }
        } else {
            builtin_runtime_error("matrix の各行は配列または数値ベクトルでなければなりません（%d行目: %s）",
                                  r + 1, value_type_name(row.type));
            return value_null();
        }
    }

    if (cols < 0) {
        builtin_runtime_error("matrix を作れません。行の列数を判定できませんでした");
        return value_null();
    }
    Value result = value_matrix(rows, cols);
    if (result.type != VALUE_MATRIX) {
        builtin_runtime_error("matrix の作成に失敗しました（%d x %d）", rows, cols);
        return value_null();
    }

    for (int r = 0; r < rows; r++) {
        Value row = argv[0].array.elements[r];
        for (int c = 0; c < cols; c++) {
            double value = row.type == VALUE_NUMERIC_ARRAY
                ? numeric_array_get(&row, c)
                : row.array.elements[c].number;
            matrix_set(&result, r, c, value);
        }
    }

    return result;
}

static Value builtin_shape(int argc, Value *argv) {
    (void)argc;
    Value result = value_array_with_capacity(2);
    if (argv[0].type == VALUE_MATRIX) {
        array_push(&result, value_number(argv[0].matrix.rows));
        array_push(&result, value_number(argv[0].matrix.cols));
        return result;
    }
    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        array_push(&result, value_number(argv[0].numeric_array.length));
        return result;
    }
    if (argv[0].type == VALUE_ARRAY) {
        array_push(&result, value_number(argv[0].array.length));
        return result;
    }
    return value_null();
}

static bool matrix_index_args(Value row, Value col, int *r, int *c) {
    if (row.type != VALUE_NUMBER || col.type != VALUE_NUMBER) return false;
    if (!row.is_integer || !col.is_integer) return false;
    *r = (int)row.number;
    *c = (int)col.number;
    return true;
}

static Value builtin_matrix_get(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_MATRIX) {
        builtin_runtime_error("matrix_get の第1引数は行列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }
    int r, c;
    if (!matrix_index_args(argv[1], argv[2], &r, &c)) {
        builtin_runtime_error("matrix_get の行・列インデックスは整数でなければなりません");
        return value_null();
    }
    if (r < 0 || r >= argv[0].matrix.rows || c < 0 || c >= argv[0].matrix.cols) {
        builtin_runtime_error("matrix_get のインデックスが範囲外です（指定: %d行%d列, 行列: %d x %d）",
                              r, c, argv[0].matrix.rows, argv[0].matrix.cols);
        return value_null();
    }
    return value_number(matrix_get(&argv[0], r, c));
}

static Value builtin_matrix_set(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_MATRIX || argv[3].type != VALUE_NUMBER) {
        builtin_runtime_error("matrix_set は (行列, 整数行, 整数列, 数値) の形で呼び出してください（第1引数: %s, 第4引数: %s）",
                              value_type_name(argv[0].type), value_type_name(argv[3].type));
        return value_null();
    }
    int r, c;
    if (!matrix_index_args(argv[1], argv[2], &r, &c)) {
        builtin_runtime_error("matrix_set の行・列インデックスは整数でなければなりません");
        return value_null();
    }
    Value result = value_copy(argv[0]);
    if (!matrix_set(&result, r, c, argv[3].number)) {
        value_free(&result);
        builtin_runtime_error("matrix_set のインデックスが範囲外です（指定: %d行%d列, 行列: %d x %d）",
                              r, c, argv[0].matrix.rows, argv[0].matrix.cols);
        return value_null();
    }
    return result;
}

static Value builtin_matrix_row(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_MATRIX || argv[1].type != VALUE_NUMBER || !argv[1].is_integer) {
        builtin_runtime_error("matrix_row は (行列, 整数行) の形で呼び出してください");
        return value_null();
    }
    int row = (int)argv[1].number;
    if (row < 0) row += argv[0].matrix.rows;
    if (row < 0 || row >= argv[0].matrix.rows) {
        builtin_runtime_error("matrix_row の行インデックスが範囲外です（指定: %d, 行数: %d）",
                              row, argv[0].matrix.rows);
        return value_null();
    }

    Value result = value_numeric_array_with_dtype(argv[0].matrix.cols, argv[0].matrix.dtype);
    for (int c = 0; c < argv[0].matrix.cols; c++) {
        numeric_array_push(&result, matrix_get(&argv[0], row, c));
    }
    return result;
}

static Value builtin_matrix_column(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_MATRIX || argv[1].type != VALUE_NUMBER || !argv[1].is_integer) {
        builtin_runtime_error("matrix_column は (行列, 整数列) の形で呼び出してください");
        return value_null();
    }
    int col = (int)argv[1].number;
    if (col < 0) col += argv[0].matrix.cols;
    if (col < 0 || col >= argv[0].matrix.cols) {
        builtin_runtime_error("matrix_column の列インデックスが範囲外です（指定: %d, 列数: %d）",
                              col, argv[0].matrix.cols);
        return value_null();
    }

    Value result = value_numeric_array_with_dtype(argv[0].matrix.rows, argv[0].matrix.dtype);
    for (int r = 0; r < argv[0].matrix.rows; r++) {
        numeric_array_push(&result, matrix_get(&argv[0], r, col));
    }
    return result;
}

static Value builtin_transpose(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_MATRIX) return value_null();

    if (argv[0].matrix.ref_count != NULL) {
        Value result;
        result.type = VALUE_MATRIX;
        result.is_const = false;
        result.is_integer = false;
        result.ref_count = 1;
        result.matrix.dtype = argv[0].matrix.dtype;
        result.matrix.data = argv[0].matrix.data;
        result.matrix.rows = argv[0].matrix.cols;
        result.matrix.cols = argv[0].matrix.rows;
        result.matrix.row_stride = argv[0].matrix.col_stride;
        result.matrix.col_stride = argv[0].matrix.row_stride;
        result.matrix.offset = argv[0].matrix.offset;
        result.matrix.ref_count = argv[0].matrix.ref_count;
        (*result.matrix.ref_count)++;
        return result;
    }

    Value result = value_matrix_with_dtype(argv[0].matrix.cols, argv[0].matrix.rows, argv[0].matrix.dtype);
    if (result.type != VALUE_MATRIX) return value_null();
    for (int r = 0; r < argv[0].matrix.rows; r++) {
        for (int c = 0; c < argv[0].matrix.cols; c++) {
            matrix_set(&result, c, r, matrix_get(&argv[0], r, c));
        }
    }
    return result;
}

static Value builtin_matmul(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_MATRIX || argv[1].type != VALUE_MATRIX) {
        builtin_runtime_error("matmul の引数はどちらも行列でなければなりません（第1引数: %s, 第2引数: %s）",
                              value_type_name(argv[0].type), value_type_name(argv[1].type));
        return value_null();
    }
    if (argv[0].matrix.cols != argv[1].matrix.rows) {
        builtin_runtime_error("matmul の行列サイズが合いません（左: %d x %d, 右: %d x %d）。左の列数と右の行数を一致させてください",
                              argv[0].matrix.rows, argv[0].matrix.cols,
                              argv[1].matrix.rows, argv[1].matrix.cols);
        return value_null();
    }

    int rows = argv[0].matrix.rows;
    int inner = argv[0].matrix.cols;
    int cols = argv[1].matrix.cols;
    Value result = value_matrix(rows, cols);
    if (result.type != VALUE_MATRIX) {
        builtin_runtime_error("matmul の結果行列を作成できませんでした（%d x %d）", rows, cols);
        return value_null();
    }

#if defined(HAJIMU_USE_ACCELERATE) || defined(HAJIMU_USE_CBLAS)
    if (argv[0].matrix.dtype == NUMERIC_DTYPE_F64 &&
        argv[1].matrix.dtype == NUMERIC_DTYPE_F64 &&
        result.matrix.dtype == NUMERIC_DTYPE_F64 &&
        matrix_is_contiguous(&argv[0]) &&
        matrix_is_contiguous(&argv[1]) &&
        matrix_is_contiguous(&result)) {
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    rows, cols, inner,
                    1.0,
                    (const double *)matrix_raw_data(&argv[0]), inner,
                    (const double *)matrix_raw_data(&argv[1]), cols,
                    0.0,
                    (double *)matrix_raw_data(&result), cols);
        return result;
    }
#endif

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            double total = 0.0;
            for (int k = 0; k < inner; k++) {
                total += matrix_get(&argv[0], r, k) * matrix_get(&argv[1], k, c);
            }
            matrix_set(&result, r, c, total);
        }
    }
    return result;
}

typedef double (*MatrixBinaryOp)(double a, double b);

static double matrix_op_add(double a, double b) { return a + b; }
static double matrix_op_sub(double a, double b) { return a - b; }
static double matrix_op_mul(double a, double b) { return a * b; }

static Value matrix_binary(const char *name, Value left, Value right, MatrixBinaryOp op) {
    if (left.type != VALUE_MATRIX || right.type != VALUE_MATRIX) {
        builtin_runtime_error("%s の引数はどちらも行列でなければなりません（第1引数: %s, 第2引数: %s）",
                              name, value_type_name(left.type), value_type_name(right.type));
        return value_null();
    }
    if (left.matrix.rows != right.matrix.rows || left.matrix.cols != right.matrix.cols) {
        builtin_runtime_error("%s の行列サイズが一致しません（左: %d x %d, 右: %d x %d）",
                              name, left.matrix.rows, left.matrix.cols, right.matrix.rows, right.matrix.cols);
        return value_null();
    }

    Value result = value_matrix(left.matrix.rows, left.matrix.cols);
    if (result.type != VALUE_MATRIX) return value_null();
    for (int r = 0; r < left.matrix.rows; r++) {
        for (int c = 0; c < left.matrix.cols; c++) {
            matrix_set(&result, r, c, op(matrix_get(&left, r, c), matrix_get(&right, r, c)));
        }
    }
    return result;
}

static Value builtin_matrix_add(int argc, Value *argv) {
    (void)argc;
    return matrix_binary("matrix_add", argv[0], argv[1], matrix_op_add);
}

static Value builtin_matrix_sub(int argc, Value *argv) {
    (void)argc;
    return matrix_binary("matrix_sub", argv[0], argv[1], matrix_op_sub);
}

static Value builtin_matrix_hadamard(int argc, Value *argv) {
    (void)argc;
    return matrix_binary("matrix_hadamard", argv[0], argv[1], matrix_op_mul);
}

static Value builtin_matrix_scale(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_MATRIX || argv[1].type != VALUE_NUMBER) {
        builtin_runtime_error("matrix_scale は (行列, 数値) の形で呼び出してください（第1引数: %s, 第2引数: %s）",
                              value_type_name(argv[0].type), value_type_name(argv[1].type));
        return value_null();
    }

    Value result = value_matrix(argv[0].matrix.rows, argv[0].matrix.cols);
    if (result.type != VALUE_MATRIX) return value_null();
    for (int r = 0; r < argv[0].matrix.rows; r++) {
        for (int c = 0; c < argv[0].matrix.cols; c++) {
            matrix_set(&result, r, c, matrix_get(&argv[0], r, c) * argv[1].number);
        }
    }
    return result;
}

static bool require_square_matrix(Value matrix, const char *name) {
    if (matrix.type != VALUE_MATRIX) {
        builtin_runtime_error("%s の引数は行列でなければなりません（実際: %s）",
                              name, value_type_name(matrix.type));
        return false;
    }
    if (matrix.matrix.rows != matrix.matrix.cols) {
        builtin_runtime_error("%s は正方行列だけを扱えます（実際: %d x %d）",
                              name, matrix.matrix.rows, matrix.matrix.cols);
        return false;
    }
    return true;
}

static Value builtin_identity(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || !argv[0].is_integer) {
        builtin_runtime_error("identity の引数は行列サイズを表す整数でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }

    int n = (int)argv[0].number;
    if (n < 0 || n > 10000) {
        builtin_runtime_error("identity のサイズは 0 から 10000 の範囲で指定してください（実際: %d）", n);
        return value_null();
    }

    Value result = value_matrix(n, n);
    if (result.type != VALUE_MATRIX) {
        builtin_runtime_error("identity の行列を作成できませんでした（%d x %d）", n, n);
        return value_null();
    }
    for (int i = 0; i < n; i++) {
        matrix_set(&result, i, i, 1.0);
    }
    return result;
}

static Value builtin_determinant(int argc, Value *argv) {
    (void)argc;
    if (!require_square_matrix(argv[0], "determinant")) return value_null();

    int n = argv[0].matrix.rows;
    if (n == 0) return value_number(1.0);

    double *a = malloc(sizeof(double) * (size_t)n * (size_t)n);
    if (a == NULL) {
        builtin_runtime_error("determinant の作業メモリを確保できませんでした");
        return value_null();
    }
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            a[(size_t)r * (size_t)n + (size_t)c] = matrix_get(&argv[0], r, c);
        }
    }

    double det = 1.0;
    int sign = 1;
    for (int col = 0; col < n; col++) {
        int pivot = col;
        double best = fabs(a[(size_t)col * (size_t)n + (size_t)col]);
        for (int r = col + 1; r < n; r++) {
            double candidate = fabs(a[(size_t)r * (size_t)n + (size_t)col]);
            if (candidate > best) {
                best = candidate;
                pivot = r;
            }
        }
        if (best < 1e-12) {
            free(a);
            return value_number(0.0);
        }
        if (pivot != col) {
            for (int c = 0; c < n; c++) {
                double tmp = a[(size_t)col * (size_t)n + (size_t)c];
                a[(size_t)col * (size_t)n + (size_t)c] = a[(size_t)pivot * (size_t)n + (size_t)c];
                a[(size_t)pivot * (size_t)n + (size_t)c] = tmp;
            }
            sign = -sign;
        }

        double pivot_value = a[(size_t)col * (size_t)n + (size_t)col];
        det *= pivot_value;
        for (int r = col + 1; r < n; r++) {
            double factor = a[(size_t)r * (size_t)n + (size_t)col] / pivot_value;
            for (int c = col + 1; c < n; c++) {
                a[(size_t)r * (size_t)n + (size_t)c] -= factor * a[(size_t)col * (size_t)n + (size_t)c];
            }
        }
    }

    free(a);
    return value_number(det * sign);
}

static bool matrix_inverse_data(Value input, double **out, int *n_out, const char *name) {
    if (!require_square_matrix(input, name)) return false;
    int n = input.matrix.rows;
    int width = n * 2;
    double *aug = calloc((size_t)n * (size_t)width, sizeof(double));
    if (aug == NULL) {
        builtin_runtime_error("%s の作業メモリを確保できませんでした", name);
        return false;
    }

    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            aug[(size_t)r * (size_t)width + (size_t)c] = matrix_get(&input, r, c);
        }
        aug[(size_t)r * (size_t)width + (size_t)(n + r)] = 1.0;
    }

    for (int col = 0; col < n; col++) {
        int pivot = col;
        double best = fabs(aug[(size_t)col * (size_t)width + (size_t)col]);
        for (int r = col + 1; r < n; r++) {
            double candidate = fabs(aug[(size_t)r * (size_t)width + (size_t)col]);
            if (candidate > best) {
                best = candidate;
                pivot = r;
            }
        }
        if (best < 1e-12) {
            free(aug);
            builtin_runtime_error("%s は特異行列を扱えません。行列式が0に近いため逆行列を計算できません", name);
            return false;
        }
        if (pivot != col) {
            for (int c = 0; c < width; c++) {
                double tmp = aug[(size_t)col * (size_t)width + (size_t)c];
                aug[(size_t)col * (size_t)width + (size_t)c] = aug[(size_t)pivot * (size_t)width + (size_t)c];
                aug[(size_t)pivot * (size_t)width + (size_t)c] = tmp;
            }
        }

        double pivot_value = aug[(size_t)col * (size_t)width + (size_t)col];
        for (int c = 0; c < width; c++) {
            aug[(size_t)col * (size_t)width + (size_t)c] /= pivot_value;
        }
        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            double factor = aug[(size_t)r * (size_t)width + (size_t)col];
            for (int c = 0; c < width; c++) {
                aug[(size_t)r * (size_t)width + (size_t)c] -= factor * aug[(size_t)col * (size_t)width + (size_t)c];
            }
        }
    }

    double *inverse = malloc(sizeof(double) * (size_t)n * (size_t)n);
    if (inverse == NULL) {
        free(aug);
        builtin_runtime_error("%s の結果メモリを確保できませんでした", name);
        return false;
    }
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            inverse[(size_t)r * (size_t)n + (size_t)c] = aug[(size_t)r * (size_t)width + (size_t)(n + c)];
        }
    }
    free(aug);
    *out = inverse;
    *n_out = n;
    return true;
}

static Value builtin_inverse(int argc, Value *argv) {
    (void)argc;
    double *inverse = NULL;
    int n = 0;
    if (!matrix_inverse_data(argv[0], &inverse, &n, "inverse")) return value_null();
    Value result = value_matrix_from_data(inverse, n, n);
    free(inverse);
    return result;
}

static Value builtin_solve_linear(int argc, Value *argv) {
    (void)argc;
    double *inverse = NULL;
    int n = 0;
    if (!matrix_inverse_data(argv[0], &inverse, &n, "solve_linear")) return value_null();

    Value inv = value_matrix_from_data(inverse, n, n);
    free(inverse);
    if (inv.type != VALUE_MATRIX) return value_null();

    Value result;
    if (argv[1].type == VALUE_NUMERIC_ARRAY) {
        if (argv[1].numeric_array.length != n) {
            value_free(&inv);
            builtin_runtime_error("solve_linear の右辺ベクトル長が行列サイズと一致しません（行列: %d x %d, 右辺: %d）",
                                  n, n, argv[1].numeric_array.length);
            return value_null();
        }
        result = value_numeric_array_with_capacity(n);
        for (int r = 0; r < n; r++) {
            double total = 0.0;
            for (int c = 0; c < n; c++) {
                total += matrix_get(&inv, r, c) * numeric_array_get(&argv[1], c);
            }
            numeric_array_push(&result, total);
        }
    } else if (argv[1].type == VALUE_MATRIX) {
        if (argv[1].matrix.rows != n) {
            value_free(&inv);
            builtin_runtime_error("solve_linear の右辺行列の行数が係数行列と一致しません（係数: %d x %d, 右辺: %d x %d）",
                                  n, n, argv[1].matrix.rows, argv[1].matrix.cols);
            return value_null();
        }
        Value args[2] = { inv, argv[1] };
        result = builtin_matmul(2, args);
    } else {
        value_free(&inv);
        builtin_runtime_error("solve_linear の第2引数は数値ベクトルまたは行列でなければなりません（実際: %s）",
                              value_type_name(argv[1].type));
        return value_null();
    }
    value_free(&inv);
    return result;
}

static Value builtin_linear_regression(int argc, Value *argv) {
    if (argv[0].type != VALUE_MATRIX) {
        builtin_runtime_error("linear_regression の第1引数は特徴量行列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }
    if (argv[1].type != VALUE_NUMERIC_ARRAY) {
        builtin_runtime_error("linear_regression の第2引数は目的変数の数値ベクトルでなければなりません（実際: %s）",
                              value_type_name(argv[1].type));
        return value_null();
    }

    int rows = argv[0].matrix.rows;
    int features = argv[0].matrix.cols;
    if (argv[1].numeric_array.length != rows) {
        builtin_runtime_error("linear_regression の行数と目的変数の長さが一致しません（X: %d行, y: %d）",
                              rows, argv[1].numeric_array.length);
        return value_null();
    }

    bool fit_intercept = true;
    if (argc >= 3) {
        if (argv[2].type != VALUE_BOOL) {
            builtin_runtime_error("linear_regression の第3引数は切片を学習するかどうかの真偽値でなければなりません（実際: %s）",
                                  value_type_name(argv[2].type));
            return value_null();
        }
        fit_intercept = argv[2].boolean;
    }

    int cols = features + (fit_intercept ? 1 : 0);
    Value design = value_matrix(rows, cols);
    if (design.type != VALUE_MATRIX) {
        builtin_runtime_error("linear_regression の設計行列を作成できませんでした");
        return value_null();
    }

    for (int r = 0; r < rows; r++) {
        int offset = 0;
        if (fit_intercept) {
            matrix_set(&design, r, 0, 1.0);
            offset = 1;
        }
        for (int c = 0; c < features; c++) {
            matrix_set(&design, r, c + offset, matrix_get(&argv[0], r, c));
        }
    }

    Value design_t_args[1] = { design };
    Value design_t = builtin_transpose(1, design_t_args);
    if (design_t.type != VALUE_MATRIX) {
        value_free(&design);
        return value_null();
    }

    Value xtx_args[2] = { design_t, design };
    Value xtx = builtin_matmul(2, xtx_args);
    if (xtx.type != VALUE_MATRIX) {
        value_free(&design);
        value_free(&design_t);
        return value_null();
    }

    Value xty = value_numeric_array_with_capacity(cols);
    for (int c = 0; c < cols; c++) {
        double total = 0.0;
        for (int r = 0; r < rows; r++) {
            total += matrix_get(&design_t, c, r) * numeric_array_get(&argv[1], r);
        }
        numeric_array_push(&xty, total);
    }

    Value solve_args[2] = { xtx, xty };
    Value beta = builtin_solve_linear(2, solve_args);
    value_free(&design);
    value_free(&design_t);
    value_free(&xtx);
    value_free(&xty);
    if (beta.type != VALUE_NUMERIC_ARRAY) {
        return value_null();
    }

    double intercept = 0.0;
    int weight_start = 0;
    if (fit_intercept) {
        intercept = numeric_array_get(&beta, 0);
        weight_start = 1;
    }

    Value weights = value_numeric_array_with_capacity(features);
    for (int i = 0; i < features; i++) {
        numeric_array_push(&weights, numeric_array_get(&beta, i + weight_start));
    }

    Value model = value_dict();
    dict_set(&model, "weights", weights);
    dict_set(&model, "重み", weights);
    dict_set(&model, "intercept", value_number(intercept));
    dict_set(&model, "切片", value_number(intercept));
    dict_set(&model, "feature_count", value_number(features));
    dict_set(&model, "特徴量数", value_number(features));
    dict_set(&model, "fit_intercept", value_bool(fit_intercept));
    dict_set(&model, "切片あり", value_bool(fit_intercept));
    value_free(&weights);
    value_free(&beta);
    return model;
}

static bool model_get_numeric_array(Value model, const char *key_en, const char *key_ja, Value *out) {
    if (model.type != VALUE_DICT) return false;
    Value value = dict_get(&model, key_en);
    if (value.type == VALUE_NULL) value = dict_get(&model, key_ja);
    if (value.type != VALUE_NUMERIC_ARRAY) return false;
    *out = value;
    return true;
}

static bool model_get_number(Value model, const char *key_en, const char *key_ja, double *out) {
    if (model.type != VALUE_DICT) return false;
    Value value = dict_get(&model, key_en);
    if (value.type == VALUE_NULL) value = dict_get(&model, key_ja);
    if (value.type != VALUE_NUMBER) return false;
    *out = value.number;
    return true;
}

static Value builtin_predict_linear(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_DICT) {
        builtin_runtime_error("predict_linear の第1引数は linear_regression が返したモデル辞書でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }

    Value weights;
    if (!model_get_numeric_array(argv[0], "weights", "重み", &weights)) {
        builtin_runtime_error("predict_linear のモデルに weights / 重み が見つかりません");
        return value_null();
    }
    double intercept = 0.0;
    (void)model_get_number(argv[0], "intercept", "切片", &intercept);

    if (argv[1].type == VALUE_NUMERIC_ARRAY) {
        if (argv[1].numeric_array.length != weights.numeric_array.length) {
            builtin_runtime_error("predict_linear の特徴量数がモデルと一致しません（モデル: %d, 入力: %d）",
                                  weights.numeric_array.length, argv[1].numeric_array.length);
            return value_null();
        }
        double total = intercept;
        for (int i = 0; i < weights.numeric_array.length; i++) {
            total += numeric_array_get(&weights, i) * numeric_array_get(&argv[1], i);
        }
        return value_number(total);
    }

    if (argv[1].type == VALUE_MATRIX) {
        if (argv[1].matrix.cols != weights.numeric_array.length) {
            builtin_runtime_error("predict_linear の特徴量列数がモデルと一致しません（モデル: %d, 入力: %d列）",
                                  weights.numeric_array.length, argv[1].matrix.cols);
            return value_null();
        }
        Value result = value_numeric_array_with_capacity(argv[1].matrix.rows);
        for (int r = 0; r < argv[1].matrix.rows; r++) {
            double total = intercept;
            for (int c = 0; c < argv[1].matrix.cols; c++) {
                total += numeric_array_get(&weights, c) * matrix_get(&argv[1], r, c);
            }
            numeric_array_push(&result, total);
        }
        return result;
    }

    builtin_runtime_error("predict_linear の第2引数は数値ベクトルまたは行列でなければなりません（実際: %s）",
                          value_type_name(argv[1].type));
    return value_null();
}

static Value builtin_kmeans(int argc, Value *argv) {
    if (argv[0].type != VALUE_MATRIX) {
        builtin_runtime_error("kmeans の第1引数は行列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }
    if (argv[1].type != VALUE_NUMBER || !argv[1].is_integer) {
        builtin_runtime_error("kmeans の第2引数はクラスタ数の整数でなければなりません（実際: %s）",
                              value_type_name(argv[1].type));
        return value_null();
    }
    int k = (int)argv[1].number;
    int iterations = 20;
    if (argc >= 3) {
        if (argv[2].type != VALUE_NUMBER || !argv[2].is_integer) {
            builtin_runtime_error("kmeans の第3引数は反復回数の整数でなければなりません（実際: %s）",
                                  value_type_name(argv[2].type));
            return value_null();
        }
        iterations = (int)argv[2].number;
    }
    int rows = argv[0].matrix.rows;
    int cols = argv[0].matrix.cols;
    if (k <= 0 || k > rows) {
        builtin_runtime_error("kmeans のクラスタ数は 1 以上かつ行数以下でなければなりません（k: %d, 行数: %d）",
                              k, rows);
        return value_null();
    }
    if (iterations <= 0) iterations = 1;

    Value centers = value_matrix(k, cols);
    Value labels = value_numeric_array_with_capacity(rows);
    for (int r = 0; r < rows; r++) numeric_array_push(&labels, 0.0);
    for (int c = 0; c < k; c++) {
        for (int j = 0; j < cols; j++) {
            matrix_set(&centers, c, j, matrix_get(&argv[0], c, j));
        }
    }

    for (int iter = 0; iter < iterations; iter++) {
        for (int r = 0; r < rows; r++) {
            int best = 0;
            double best_dist = INFINITY;
            for (int cluster = 0; cluster < k; cluster++) {
                double dist = 0.0;
                for (int c = 0; c < cols; c++) {
                    double d = matrix_get(&argv[0], r, c) - matrix_get(&centers, cluster, c);
                    dist += d * d;
                }
                if (dist < best_dist) {
                    best_dist = dist;
                    best = cluster;
                }
            }
            numeric_array_set(&labels, r, best);
        }

        Value next = value_matrix(k, cols);
        int *counts = calloc((size_t)k, sizeof(int));
        if (counts == NULL) {
            value_free(&centers);
            value_free(&labels);
            value_free(&next);
            builtin_runtime_error("kmeans の作業メモリを確保できませんでした");
            return value_null();
        }
        for (int r = 0; r < rows; r++) {
            int cluster = (int)numeric_array_get(&labels, r);
            counts[cluster]++;
            for (int c = 0; c < cols; c++) {
                matrix_set(&next, cluster, c, matrix_get(&next, cluster, c) + matrix_get(&argv[0], r, c));
            }
        }
        for (int cluster = 0; cluster < k; cluster++) {
            if (counts[cluster] == 0) {
                for (int c = 0; c < cols; c++) matrix_set(&next, cluster, c, matrix_get(&centers, cluster, c));
                continue;
            }
            for (int c = 0; c < cols; c++) {
                matrix_set(&next, cluster, c, matrix_get(&next, cluster, c) / counts[cluster]);
            }
        }
        free(counts);
        value_free(&centers);
        centers = next;
    }

    Value result = value_dict();
    dict_set(&result, "centers", centers);
    dict_set(&result, "中心", centers);
    dict_set(&result, "labels", labels);
    dict_set(&result, "ラベル", labels);
    dict_set(&result, "k", value_number(k));
    value_free(&centers);
    value_free(&labels);
    return result;
}

static double knn_predict_one(Value train_x, Value train_y, Value query, int query_row, int k, bool *ok) {
    int rows = train_x.matrix.rows;
    int cols = train_x.matrix.cols;
    double *best_dist = malloc(sizeof(double) * (size_t)k);
    double *best_label = malloc(sizeof(double) * (size_t)k);
    if (best_dist == NULL || best_label == NULL) {
        free(best_dist);
        free(best_label);
        *ok = false;
        return 0.0;
    }

    int used = 0;
    for (int r = 0; r < rows; r++) {
        double dist = 0.0;
        for (int c = 0; c < cols; c++) {
            double q = query.type == VALUE_NUMERIC_ARRAY
                ? numeric_array_get(&query, c)
                : matrix_get(&query, query_row, c);
            double d = matrix_get(&train_x, r, c) - q;
            dist += d * d;
        }

        int pos = -1;
        if (used < k) {
            pos = used++;
        } else if (dist < best_dist[used - 1]) {
            pos = used - 1;
        }
        if (pos < 0) continue;

        while (pos > 0 && dist < best_dist[pos - 1]) {
            best_dist[pos] = best_dist[pos - 1];
            best_label[pos] = best_label[pos - 1];
            pos--;
        }
        best_dist[pos] = dist;
        best_label[pos] = numeric_array_get(&train_y, r);
    }

    if (used <= 0) {
        free(best_dist);
        free(best_label);
        *ok = false;
        return 0.0;
    }

    double chosen_label = best_label[0];
    int chosen_votes = 0;
    double chosen_dist_sum = INFINITY;
    for (int i = 0; i < used; i++) {
        int votes = 0;
        double dist_sum = 0.0;
        for (int j = 0; j < used; j++) {
            if (best_label[j] == best_label[i]) {
                votes++;
                dist_sum += best_dist[j];
            }
        }
        if (votes > chosen_votes ||
            (votes == chosen_votes && dist_sum < chosen_dist_sum) ||
            (votes == chosen_votes && dist_sum == chosen_dist_sum && best_label[i] < chosen_label)) {
            chosen_label = best_label[i];
            chosen_votes = votes;
            chosen_dist_sum = dist_sum;
        }
    }

    free(best_dist);
    free(best_label);
    *ok = true;
    return chosen_label;
}

static Value builtin_knn_predict(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_MATRIX) {
        builtin_runtime_error("knn_predict の第1引数は訓練特徴量の行列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }
    if (argv[1].type != VALUE_NUMERIC_ARRAY) {
        builtin_runtime_error("knn_predict の第2引数は訓練ラベルの数値ベクトルでなければなりません（実際: %s）",
                              value_type_name(argv[1].type));
        return value_null();
    }
    if (argv[3].type != VALUE_NUMBER || !argv[3].is_integer) {
        builtin_runtime_error("knn_predict の第4引数 k は整数でなければなりません（実際: %s）",
                              value_type_name(argv[3].type));
        return value_null();
    }

    int rows = argv[0].matrix.rows;
    int cols = argv[0].matrix.cols;
    int k = (int)argv[3].number;
    if (rows <= 0 || cols <= 0) {
        builtin_runtime_error("knn_predict の訓練行列は 1 行 1 列以上でなければなりません");
        return value_null();
    }
    if (argv[1].numeric_array.length != rows) {
        builtin_runtime_error("knn_predict の訓練行数とラベル長が一致しません（X: %d行, y: %d）",
                              rows, argv[1].numeric_array.length);
        return value_null();
    }
    if (k <= 0 || k > rows) {
        builtin_runtime_error("knn_predict の k は 1 以上かつ訓練行数以下でなければなりません（k: %d, 行数: %d）",
                              k, rows);
        return value_null();
    }

    if (argv[2].type == VALUE_NUMERIC_ARRAY) {
        if (argv[2].numeric_array.length != cols) {
            builtin_runtime_error("knn_predict の入力ベクトル長が訓練列数と一致しません（入力: %d, 訓練: %d列）",
                                  argv[2].numeric_array.length, cols);
            return value_null();
        }
        bool ok = false;
        double label = knn_predict_one(argv[0], argv[1], argv[2], 0, k, &ok);
        if (!ok) {
            builtin_runtime_error("knn_predict の作業メモリを確保できませんでした");
            return value_null();
        }
        return value_number(label);
    }

    if (argv[2].type == VALUE_MATRIX) {
        if (argv[2].matrix.cols != cols) {
            builtin_runtime_error("knn_predict の入力行列の列数が訓練列数と一致しません（入力: %d列, 訓練: %d列）",
                                  argv[2].matrix.cols, cols);
            return value_null();
        }
        Value result = value_numeric_array_with_capacity(argv[2].matrix.rows);
        for (int r = 0; r < argv[2].matrix.rows; r++) {
            bool ok = false;
            double label = knn_predict_one(argv[0], argv[1], argv[2], r, k, &ok);
            if (!ok) {
                value_free(&result);
                builtin_runtime_error("knn_predict の作業メモリを確保できませんでした");
                return value_null();
            }
            numeric_array_push(&result, label);
        }
        return result;
    }

    builtin_runtime_error("knn_predict の第3引数は入力ベクトルまたは入力行列でなければなりません（実際: %s）",
                          value_type_name(argv[2].type));
    return value_null();
}

static double logistic_sigmoid(double x) {
    if (x >= 0.0) {
        double z = exp(-x);
        return 1.0 / (1.0 + z);
    }
    double z = exp(x);
    return z / (1.0 + z);
}

static Value builtin_logistic_regression(int argc, Value *argv) {
    if (argv[0].type != VALUE_MATRIX || argv[1].type != VALUE_NUMERIC_ARRAY) {
        builtin_runtime_error("logistic_regression は (特徴量行列, 0/1ラベルベクトル [, 学習率] [, 反復回数]) の形で呼び出してください");
        return value_null();
    }
    int rows = argv[0].matrix.rows;
    int cols = argv[0].matrix.cols;
    if (argv[1].numeric_array.length != rows) {
        builtin_runtime_error("logistic_regression の行数とラベル長が一致しません（X: %d行, y: %d）",
                              rows, argv[1].numeric_array.length);
        return value_null();
    }
    double lr = 0.1;
    int iterations = 200;
    if (argc >= 3) {
        if (argv[2].type != VALUE_NUMBER) return value_null();
        lr = argv[2].number;
    }
    if (argc >= 4) {
        if (argv[3].type != VALUE_NUMBER || !argv[3].is_integer) return value_null();
        iterations = (int)argv[3].number;
    }
    if (iterations <= 0) iterations = 1;

    Value weights = value_numeric_array_with_capacity(cols);
    for (int c = 0; c < cols; c++) numeric_array_push(&weights, 0.0);
    double intercept = 0.0;

    for (int iter = 0; iter < iterations; iter++) {
        double grad_b = 0.0;
        double *grad_w = calloc((size_t)cols, sizeof(double));
        if (grad_w == NULL) {
            value_free(&weights);
            builtin_runtime_error("logistic_regression の作業メモリを確保できませんでした");
            return value_null();
        }
        for (int r = 0; r < rows; r++) {
            double z = intercept;
            for (int c = 0; c < cols; c++) z += numeric_array_get(&weights, c) * matrix_get(&argv[0], r, c);
            double p = logistic_sigmoid(z);
            double error = p - numeric_array_get(&argv[1], r);
            grad_b += error;
            for (int c = 0; c < cols; c++) grad_w[c] += error * matrix_get(&argv[0], r, c);
        }
        intercept -= lr * grad_b / rows;
        for (int c = 0; c < cols; c++) {
            numeric_array_set(&weights, c, numeric_array_get(&weights, c) - lr * grad_w[c] / rows);
        }
        free(grad_w);
    }

    Value model = value_dict();
    dict_set(&model, "weights", weights);
    dict_set(&model, "重み", weights);
    dict_set(&model, "intercept", value_number(intercept));
    dict_set(&model, "切片", value_number(intercept));
    dict_set(&model, "feature_count", value_number(cols));
    dict_set(&model, "特徴量数", value_number(cols));
    value_free(&weights);
    return model;
}

static Value builtin_predict_logistic(int argc, Value *argv) {
    (void)argc;
    Value weights;
    if (!model_get_numeric_array(argv[0], "weights", "重み", &weights)) {
        builtin_runtime_error("predict_logistic のモデルに weights / 重み が見つかりません");
        return value_null();
    }
    double intercept = 0.0;
    (void)model_get_number(argv[0], "intercept", "切片", &intercept);

    if (argv[1].type == VALUE_NUMERIC_ARRAY) {
        if (argv[1].numeric_array.length != weights.numeric_array.length) {
            builtin_runtime_error("predict_logistic の特徴量数がモデルと一致しません");
            return value_null();
        }
        double z = intercept;
        for (int c = 0; c < weights.numeric_array.length; c++) {
            z += numeric_array_get(&weights, c) * numeric_array_get(&argv[1], c);
        }
        return value_number(logistic_sigmoid(z));
    }
    if (argv[1].type == VALUE_MATRIX) {
        if (argv[1].matrix.cols != weights.numeric_array.length) {
            builtin_runtime_error("predict_logistic の特徴量列数がモデルと一致しません");
            return value_null();
        }
        Value result = value_numeric_array_with_capacity(argv[1].matrix.rows);
        for (int r = 0; r < argv[1].matrix.rows; r++) {
            double z = intercept;
            for (int c = 0; c < argv[1].matrix.cols; c++) z += numeric_array_get(&weights, c) * matrix_get(&argv[1], r, c);
            numeric_array_push(&result, logistic_sigmoid(z));
        }
        return result;
    }
    builtin_runtime_error("predict_logistic の第2引数は数値ベクトルまたは行列でなければなりません（実際: %s）",
                          value_type_name(argv[1].type));
    return value_null();
}

static Value builtin_predict_logistic_class(int argc, Value *argv) {
    double threshold = 0.5;
    if (argc >= 3) {
        if (argv[2].type != VALUE_NUMBER || isnan(argv[2].number)) {
            builtin_runtime_error("predict_logistic_class の第3引数は分類閾値の数値でなければなりません（実際: %s）",
                                  value_type_name(argv[2].type));
            return value_null();
        }
        threshold = argv[2].number;
    }

    Value predict_args[2] = { argv[0], argv[1] };
    Value probabilities = builtin_predict_logistic(2, predict_args);
    if (probabilities.type == VALUE_NULL) return value_null();

    if (probabilities.type == VALUE_NUMBER) {
        return value_number(probabilities.number >= threshold ? 1.0 : 0.0);
    }

    if (probabilities.type == VALUE_NUMERIC_ARRAY) {
        Value result = value_numeric_array_with_dtype(probabilities.numeric_array.length, NUMERIC_DTYPE_BOOL);
        for (int i = 0; i < probabilities.numeric_array.length; i++) {
            numeric_array_push(&result, numeric_array_get(&probabilities, i) >= threshold ? 1.0 : 0.0);
        }
        value_free(&probabilities);
        return result;
    }

    value_free(&probabilities);
    builtin_runtime_error("predict_logistic_class の内部予測結果が想定外です（実際: %s）",
                          value_type_name(probabilities.type));
    return value_null();
}

static bool parse_text_delimited_fields(const char *line, char delimiter, Value *out_fields,
                                        const char *name, int line_no);

static Value read_delimited_numeric(int argc, Value *argv, char delimiter, const char *name, const char *label) {
    if (argv[0].type != VALUE_STRING) {
        builtin_runtime_error("%s の第1引数はファイルパス文字列でなければなりません（実際: %s）",
                              name, value_type_name(argv[0].type));
        return value_null();
    }

    bool has_header = false;
    if (argc >= 2) {
        if (argv[1].type != VALUE_BOOL) {
            builtin_runtime_error("%s の第2引数はヘッダー有無を表す真偽値でなければなりません（実際: %s）",
                                  name, value_type_name(argv[1].type));
            return value_null();
        }
        has_header = argv[1].boolean;
    }

    const char *missing_mode = "error";
    if (argc >= 3) {
        if (argv[2].type != VALUE_STRING) {
            builtin_runtime_error("%s の第3引数は missing mode 文字列でなければなりません（実際: %s）",
                                  name, value_type_name(argv[2].type));
            return value_null();
        }
        missing_mode = argv[2].string.data;
        if (strcmp(missing_mode, "error") != 0 &&
            strcmp(missing_mode, "nan") != 0 &&
            strcmp(missing_mode, "zero") != 0) {
            builtin_runtime_error("%s の missing mode は \"error\" / \"nan\" / \"zero\" のいずれかです（実際: %s）",
                                  name, missing_mode);
            return value_null();
        }
    }

    FILE *f = fopen(argv[0].string.data, "r");
    if (f == NULL) {
        builtin_runtime_error("%sファイルを読み込めません: %s", label, argv[0].string.data);
        return value_null();
    }

    double *data = NULL;
    int data_count = 0;
    int data_capacity = 0;
    int rows = 0;
    int cols = -1;
    char line[8192];
    int line_no = 0;
    bool skipped_header = false;

    while (fgets(line, sizeof(line), f) != NULL) {
        line_no++;
        if (has_header && !skipped_header) {
            skipped_header = true;
            continue;
        }

        size_t line_len = strlen(line);
        if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
            free(data);
            fclose(f);
            builtin_runtime_error("%sの%d行目が長すぎます。1行は8191バイト以内にしてください", label, line_no);
            return value_null();
        }

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r') continue;

        Value fields = value_null();
        if (!parse_text_delimited_fields(line, delimiter, &fields, label, line_no)) {
            free(data);
            fclose(f);
            return value_null();
        }

        double row_values[1024];
        int row_cols = fields.array.length;
        if (row_cols > (int)(sizeof(row_values) / sizeof(row_values[0]))) {
            value_free(&fields);
            free(data);
            fclose(f);
            builtin_runtime_error("%sの%d行目の列数が多すぎます。現在は1行1024列まで対応しています", label, line_no);
            return value_null();
        }

        for (int i = 0; i < row_cols; i++) {
            Value *field = &fields.array.elements[i];
            if (field->type != VALUE_STRING) {
                value_free(&fields);
                free(data);
                fclose(f);
                builtin_runtime_error("%sの%d行%d列目を文字列セルとして扱えません", label, line_no, i + 1);
                return value_null();
            }
            char *token = field->string.data;
            while (*token == ' ' || *token == '\t') token++;
            char *endptr = NULL;
            errno = 0;
            double value = strtod(token, &endptr);
            while (endptr != NULL && (*endptr == ' ' || *endptr == '\t' || *endptr == '\r' || *endptr == '\n')) {
                endptr++;
            }
            char *token_end = token + strlen(token);
            while (token_end > token && (token_end[-1] == ' ' || token_end[-1] == '\t' ||
                   token_end[-1] == '\r' || token_end[-1] == '\n')) {
                token_end--;
            }
            int token_len = (int)(token_end - token);
            bool missing = token_len == 0 || (endptr == token &&
                ((token_len == 2 && strncmp(token, "NA", 2) == 0) ||
                 (token_len == 3 && strncmp(token, "NaN", 3) == 0) ||
                 (token_len == 3 && strncmp(token, "nan", 3) == 0) ||
                 (token_len == 4 && strncmp(token, "null", 4) == 0)));
            if (missing) {
                if (strcmp(missing_mode, "nan") == 0) {
                    value = NAN;
                } else if (strcmp(missing_mode, "zero") == 0) {
                    value = 0.0;
                } else {
                    value_free(&fields);
                    free(data);
                    fclose(f);
                    builtin_runtime_error("%sの%d行%d列目に欠損値があります。第3引数に \"nan\" または \"zero\" を指定すると読み込めます",
                                          label, line_no, i + 1);
                    return value_null();
                }
            } else if (errno != 0 || endptr == token || (endptr != NULL && *endptr != '\0')) {
                value_free(&fields);
                free(data);
                fclose(f);
                builtin_runtime_error("%sの%d行%d列目を数値として読めません: %s",
                                      label, line_no, i + 1, token);
                return value_null();
            }
            row_values[i] = value;
        }

        if (row_cols == 0) continue;
        if (cols < 0) cols = row_cols;
        if (row_cols != cols) {
            value_free(&fields);
            free(data);
            fclose(f);
            builtin_runtime_error("%sの列数が一致しません（期待: %d列, %d行目: %d列）",
                                  label, cols, line_no, row_cols);
            return value_null();
        }

        for (int c = 0; c < cols; c++) {
            if (data_count >= data_capacity) {
                ARRAY_GROW(data, data_count, data_capacity, double,
                           value_free(&fields); free(data); fclose(f);
                           builtin_runtime_error("CSVデータのメモリ確保に失敗しました");
                           return value_null());
            }
            data[data_count++] = row_values[c];
        }
        value_free(&fields);
        rows++;
    }

    fclose(f);

    if (cols < 0) {
        free(data);
        return value_matrix(0, 0);
    }

    Value result = value_matrix_from_data(data, rows, cols);
    free(data);
    return result;
}

static Value builtin_read_csv_numeric(int argc, Value *argv) {
    return read_delimited_numeric(argc, argv, ',', "read_csv_numeric", "CSV");
}

static Value builtin_read_tsv_numeric(int argc, Value *argv) {
    return read_delimited_numeric(argc, argv, '\t', "read_tsv_numeric", "TSV");
}

static bool parse_text_delimited_fields(const char *line, char delimiter, Value *out_fields,
                                        const char *name, int line_no) {
    size_t line_len = strlen(line);
    char *buffer = (char *)malloc(line_len + 1);
    if (buffer == NULL) {
        builtin_runtime_error("%s の%d行目を読むためのメモリを確保できませんでした", name, line_no);
        return false;
    }

    Value fields = value_array_with_capacity(8);
    bool in_quotes = false;
    bool field_started = false;
    size_t field_len = 0;

    for (size_t i = 0; i < line_len; i++) {
        char c = line[i];
        if (!in_quotes && (c == '\n' || c == '\r')) {
            break;
        }

        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line_len && line[i + 1] == '"') {
                    buffer[field_len++] = '"';
                    i++;
                } else {
                    in_quotes = false;
                }
            } else {
                buffer[field_len++] = c;
            }
            continue;
        }

        if (c == '"' && !field_started) {
            in_quotes = true;
            field_started = true;
            continue;
        }

        if (c == delimiter) {
            Value cell = value_string_n(buffer, field_len);
            array_push(&fields, cell);
            value_free(&cell);
            field_len = 0;
            field_started = false;
            continue;
        }

        buffer[field_len++] = c;
        field_started = true;
    }

    if (in_quotes) {
        value_free(&fields);
        free(buffer);
        builtin_runtime_error("%s の%d行目に閉じていない引用符があります", name, line_no);
        return false;
    }

    Value cell = value_string_n(buffer, field_len);
    array_push(&fields, cell);
    value_free(&cell);
    free(buffer);
    *out_fields = fields;
    return true;
}

static bool is_blank_csv_line(const char *line) {
    for (const char *p = line; *p != '\0'; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            return false;
        }
    }
    return true;
}

static Value builtin_read_csv(int argc, Value *argv) {
    if (argv[0].type != VALUE_STRING) {
        builtin_runtime_error("read_csv の第1引数はファイルパス文字列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }

    bool has_header = true;
    if (argc >= 2) {
        if (argv[1].type != VALUE_BOOL) {
            builtin_runtime_error("read_csv の第2引数はヘッダー有無を表す真偽値でなければなりません（実際: %s）",
                                  value_type_name(argv[1].type));
            return value_null();
        }
        has_header = argv[1].boolean;
    }

    FILE *f = fopen(argv[0].string.data, "r");
    if (f == NULL) {
        builtin_runtime_error("CSVファイルを読み込めません: %s", argv[0].string.data);
        return value_null();
    }

    Value rows = value_array();
    Value headers = value_null();
    int expected_cols = -1;
    int line_no = 0;
    bool header_seen = false;
    char line[8192];

    while (fgets(line, sizeof(line), f) != NULL) {
        line_no++;
        size_t line_len = strlen(line);
        if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
            value_free(&rows);
            value_free(&headers);
            fclose(f);
            builtin_runtime_error("CSVの%d行目が長すぎます。1行は8191バイト以内にしてください", line_no);
            return value_null();
        }
        if (is_blank_csv_line(line)) continue;

        Value fields = value_null();
        if (!parse_text_delimited_fields(line, ',', &fields, "CSV", line_no)) {
            value_free(&rows);
            value_free(&headers);
            fclose(f);
            return value_null();
        }

        if (expected_cols < 0) {
            expected_cols = fields.array.length;
        } else if (fields.array.length != expected_cols) {
            int actual_cols = fields.array.length;
            value_free(&fields);
            value_free(&rows);
            value_free(&headers);
            fclose(f);
            builtin_runtime_error("CSVの列数が一致しません（期待: %d列, %d行目: %d列）",
                                  expected_cols, line_no, actual_cols);
            return value_null();
        }

        if (has_header && !header_seen) {
            headers = fields;
            header_seen = true;
            continue;
        }

        if (has_header) {
            Value row = value_dict();
            for (int i = 0; i < fields.array.length; i++) {
                Value *key = &headers.array.elements[i];
                if (key->type != VALUE_STRING) continue;
                dict_set(&row, key->string.data, fields.array.elements[i]);
            }
            array_push(&rows, row);
            value_free(&row);
            value_free(&fields);
        } else {
            array_push(&rows, fields);
            value_free(&fields);
        }
    }

    fclose(f);
    value_free(&headers);
    return rows;
}

static Value builtin_csv_column(int argc, Value *argv) {
    (void)argc;

    if (argv[0].type != VALUE_ARRAY) {
        builtin_runtime_error("csv_column の第1引数は read_csv が返した行配列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }

    Value result = value_array_with_capacity(argv[0].array.length);
    for (int r = 0; r < argv[0].array.length; r++) {
        Value row = argv[0].array.elements[r];
        if (row.type == VALUE_DICT && argv[1].type == VALUE_STRING) {
            Value cell = dict_get(&row, argv[1].string.data);
            array_push(&result, cell);
            continue;
        }

        if (row.type == VALUE_ARRAY && argv[1].type == VALUE_NUMBER && floor(argv[1].number) == argv[1].number) {
            int index = (int)argv[1].number;
            if (index < 0) index += row.array.length;
            if (index < 0 || index >= row.array.length) {
                value_free(&result);
                builtin_runtime_error("csv_column の列番号が範囲外です（指定: %d, %d行目の列数: %d）",
                                      (int)argv[1].number, r + 1, row.array.length);
                return value_null();
            }
            array_push(&result, row.array.elements[index]);
            continue;
        }

        value_free(&result);
        builtin_runtime_error("csv_column は、辞書行には列名文字列、配列行には列番号整数を指定してください（%d行目: %s, 指定: %s）",
                              r + 1, value_type_name(row.type), value_type_name(argv[1].type));
        return value_null();
    }

    return result;
}

static Value builtin_read_json_lines(int argc, Value *argv) {
    if (argv[0].type != VALUE_STRING) {
        builtin_runtime_error("read_json_lines の第1引数はファイルパス文字列でなければなりません（実際: %s）",
                              value_type_name(argv[0].type));
        return value_null();
    }

    int max_lines = 100000;
    if (argc >= 2) {
        if (argv[1].type != VALUE_NUMBER || floor(argv[1].number) != argv[1].number) {
            builtin_runtime_error("read_json_lines の第2引数は最大行数を表す整数でなければなりません（実際: %s）",
                                  value_type_name(argv[1].type));
            return value_null();
        }
        max_lines = (int)argv[1].number;
        if (max_lines < 0) {
            builtin_runtime_error("read_json_lines の最大行数は0以上でなければなりません（実際: %d）", max_lines);
            return value_null();
        }
    }

    FILE *f = fopen(argv[0].string.data, "r");
    if (f == NULL) {
        builtin_runtime_error("JSON Linesファイルを読み込めません: %s", argv[0].string.data);
        return value_null();
    }

    Value rows = value_array();
    char line[8192];
    int line_no = 0;
    int parsed_lines = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        line_no++;
        size_t line_len = strlen(line);
        if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
            value_free(&rows);
            fclose(f);
            builtin_runtime_error("JSON Linesの%d行目が長すぎます。1行は8191バイト以内にしてください", line_no);
            return value_null();
        }
        if (is_blank_csv_line(line)) continue;

        if (parsed_lines >= max_lines) {
            value_free(&rows);
            fclose(f);
            builtin_runtime_error("JSON Linesの読み込み行数が上限を超えました（上限: %d行）。第2引数で上限を調整できます",
                                  max_lines);
            return value_null();
        }

        Value item = value_null();
        if (!json_decode_checked(line, (int)line_len, &item)) {
            value_free(&rows);
            fclose(f);
            builtin_runtime_error("JSON Linesの%d行目をJSONとして解析できません", line_no);
            return value_null();
        }
        array_push(&rows, item);
        value_free(&item);
        parsed_lines++;
    }

    fclose(f);
    return rows;
}

static Value describe_numeric_buffer(const double *data, int length) {
    Value result = value_dict();
    dict_set(&result, "count", value_number(length));
    dict_set(&result, "件数", value_number(length));

    if (length <= 0) {
        dict_set(&result, "mean", value_null());
        dict_set(&result, "平均", value_null());
        dict_set(&result, "std", value_null());
        dict_set(&result, "標準偏差", value_null());
        dict_set(&result, "min", value_null());
        dict_set(&result, "最小", value_null());
        dict_set(&result, "max", value_null());
        dict_set(&result, "最大", value_null());
        return result;
    }

    double min = data[0];
    double max = data[0];
    double total = 0.0;
    for (int i = 0; i < length; i++) {
        double value = data[i];
        if (value < min) min = value;
        if (value > max) max = value;
        total += value;
    }
    double mean = total / length;

    double variance = 0.0;
    for (int i = 0; i < length; i++) {
        double delta = data[i] - mean;
        variance += delta * delta;
    }
    variance /= length;

    dict_set(&result, "mean", value_number(mean));
    dict_set(&result, "平均", value_number(mean));
    dict_set(&result, "std", value_number(sqrt(variance)));
    dict_set(&result, "標準偏差", value_number(sqrt(variance)));
    dict_set(&result, "min", value_number(min));
    dict_set(&result, "最小", value_number(min));
    dict_set(&result, "max", value_number(max));
    dict_set(&result, "最大", value_number(max));
    return result;
}

static Value builtin_describe(int argc, Value *argv) {
    (void)argc;

    if (argv[0].type == VALUE_NUMERIC_ARRAY) {
        double *data = malloc(sizeof(double) * (size_t)argv[0].numeric_array.length);
        if (data == NULL) {
            builtin_runtime_error("describe の作業メモリを確保できませんでした");
            return value_null();
        }
        for (int i = 0; i < argv[0].numeric_array.length; i++) {
            data[i] = numeric_array_get(&argv[0], i);
        }
        Value result = describe_numeric_buffer(data, argv[0].numeric_array.length);
        free(data);
        return result;
    }

    if (argv[0].type == VALUE_MATRIX) {
        Value result = value_array_with_capacity(argv[0].matrix.cols);
        for (int c = 0; c < argv[0].matrix.cols; c++) {
            Value col = value_numeric_array_with_capacity(argv[0].matrix.rows);
            for (int r = 0; r < argv[0].matrix.rows; r++) {
                numeric_array_push(&col, matrix_get(&argv[0], r, c));
            }
            double *data = malloc(sizeof(double) * (size_t)col.numeric_array.length);
            if (data == NULL) {
                value_free(&col);
                value_free(&result);
                builtin_runtime_error("describe の作業メモリを確保できませんでした");
                return value_null();
            }
            for (int i = 0; i < col.numeric_array.length; i++) {
                data[i] = numeric_array_get(&col, i);
            }
            Value summary = describe_numeric_buffer(data, col.numeric_array.length);
            free(data);
            array_push(&result, summary);
            value_free(&summary);
            value_free(&col);
        }
        return result;
    }

    return value_null();
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
    if (!buffer) return value_string("");
    size_t offset = 0;

    for (int i = 0; i < argv[0].array.length; i++) {
        if (i > 0) {
            memcpy(buffer + offset, argv[1].string.data, delim_len);
            offset += delim_len;
        }
        char *s = value_to_string(argv[0].array.elements[i]);
        size_t slen = strlen(s);
        memcpy(buffer + offset, s, slen);
        offset += slen;
        free(s);
    }
    buffer[offset] = '\0';
    
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
    
    // 結果バッファを確保（size_tアンダーフロー防止）
    size_t src_len = strlen(src);
    size_t result_len;
    if (new_len >= old_len) {
        result_len = src_len + count * (new_len - old_len);
    } else {
        size_t shrink = count * (old_len - new_len);
        result_len = (shrink <= src_len) ? src_len - shrink : 0;
    }
    char *buffer = malloc(result_len + 1);
    if (!buffer) return value_string(src);
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
    size_t remaining = strlen(p);
    memcpy(dst, p, remaining + 1);
    
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
    int len = argv[0].string.byte_length;
    
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
    
    size_t written = fwrite(argv[1].string.data, 1, argv[1].string.byte_length, file);
    fclose(file);
    
    return value_bool(written == (size_t)argv[1].string.byte_length);
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
    env_release(local);
    maybe_collect_gc();
    
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
    int rep_len = argv[2].string.byte_length;
    
    // 結果バッファ
    int buf_capacity = argv[0].string.byte_length * 2 + 64;
    char *buf = malloc(buf_capacity);
    int buf_len = 0;
    
    regmatch_t match;
    
    while (*src && regexec(&regex, src, 1, &match, 0) == 0) {
        // マッチ前の部分をコピー
        int prefix_len = match.rm_so;
        while (buf_len + prefix_len + rep_len + 1 >= buf_capacity) {
            ARRAY_GROW(buf, buf_len + prefix_len + rep_len + 1, buf_capacity, char, free(buf); regfree(&regex); return value_string(""));
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
        ARRAY_GROW(buf, buf_len + remaining + 1, buf_capacity, char, free(buf); regfree(&regex); return value_string(""));
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
            ARRAY_GROW(buffer, length + chunk_len + 1, capacity, char, free(buffer); pclose(pipe); return value_string(""));
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

// =============================================================================
// セット（集合）関数
// =============================================================================

// ヘルパー: 配列に値が既に存在するかチェック
static bool array_contains_value(Value *arr, Value val) {
    for (int i = 0; i < arr->array.length; i++) {
        if (value_equals(arr->array.elements[i], val)) {
            return true;
        }
    }
    return false;
}

// 集合(要素1, 要素2, ...) - 重複を排除して配列として返す
static Value builtin_set_create(int argc, Value *argv) {
    Value result = value_array();
    for (int i = 0; i < argc; i++) {
        if (!array_contains_value(&result, argv[i])) {
            array_push(&result, argv[i]);
        }
    }
    return result;
}

// 集合追加(集合, 値) - 値がなければ追加した新しい集合を返す
static Value builtin_set_add(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_null();
    
    Value result = value_copy(argv[0]);
    if (!array_contains_value(&result, argv[1])) {
        array_push(&result, argv[1]);
    }
    return result;
}

// 集合削除(集合, 値) - 値を除いた新しい集合を返す
static Value builtin_set_remove(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_null();
    
    Value result = value_array();
    for (int i = 0; i < argv[0].array.length; i++) {
        if (!value_equals(argv[0].array.elements[i], argv[1])) {
            array_push(&result, argv[0].array.elements[i]);
        }
    }
    return result;
}

// 集合含む(集合, 値) - 値が含まれるか
static Value builtin_set_contains(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY) return value_bool(false);
    return value_bool(array_contains_value(&argv[0], argv[1]));
}

// 和集合(A, B) - AとBの和集合
static Value builtin_set_union(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_ARRAY) return value_null();
    
    Value result = value_array();
    for (int i = 0; i < argv[0].array.length; i++) {
        if (!array_contains_value(&result, argv[0].array.elements[i])) {
            array_push(&result, argv[0].array.elements[i]);
        }
    }
    for (int i = 0; i < argv[1].array.length; i++) {
        if (!array_contains_value(&result, argv[1].array.elements[i])) {
            array_push(&result, argv[1].array.elements[i]);
        }
    }
    return result;
}

// 積集合(A, B) - AとBの共通要素
static Value builtin_set_intersection(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_ARRAY) return value_null();
    
    Value result = value_array();
    for (int i = 0; i < argv[0].array.length; i++) {
        if (array_contains_value(&argv[1], argv[0].array.elements[i]) &&
            !array_contains_value(&result, argv[0].array.elements[i])) {
            array_push(&result, argv[0].array.elements[i]);
        }
    }
    return result;
}

// 差集合(A, B) - AにあってBにない要素
static Value builtin_set_difference(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_ARRAY || argv[1].type != VALUE_ARRAY) return value_null();
    
    Value result = value_array();
    for (int i = 0; i < argv[0].array.length; i++) {
        if (!array_contains_value(&argv[1], argv[0].array.elements[i]) &&
            !array_contains_value(&result, argv[0].array.elements[i])) {
            array_push(&result, argv[0].array.elements[i]);
        }
    }
    return result;
}

// =============================================================================
// テストフレームワーク
// =============================================================================

// テスト(名前, 関数) - テストケースを登録
static Value builtin_test_register(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    if (argv[1].type != VALUE_FUNCTION && argv[1].type != VALUE_BUILTIN) return value_null();
    
    if (g_test_count >= MAX_TESTS) return value_null();
    
    g_tests[g_test_count].name = strdup(argv[0].string.data);
    g_tests[g_test_count].func = value_copy(argv[1]);
    g_test_count++;
    
    return value_null();
}

// 期待(実際, 期待値 [, メッセージ]) - テスト内アサーション
static Value builtin_expect(int argc, Value *argv) {
    Value actual = argv[0];
    Value expected = argv[1];
    
    if (value_equals(actual, expected)) {
        return value_bool(true);
    }
    
    // 失敗
    g_expect_failures++;
    char *actual_str = value_to_string(actual);
    char *expected_str = value_to_string(expected);
    
    int written;
    if (argc >= 3 && argv[2].type == VALUE_STRING) {
        written = snprintf(g_expect_messages + g_expect_msg_len,
                          sizeof(g_expect_messages) - g_expect_msg_len,
                          "    ✗ %s: 期待=%s, 実際=%s\n",
                          argv[2].string.data, expected_str, actual_str);
    } else {
        written = snprintf(g_expect_messages + g_expect_msg_len,
                          sizeof(g_expect_messages) - g_expect_msg_len,
                          "    ✗ 期待=%s, 実際=%s\n",
                          expected_str, actual_str);
    }
    if (written > 0) g_expect_msg_len += written;
    
    free(actual_str);
    free(expected_str);
    return value_bool(false);
}

// 期待エラー(関数) - 関数が例外を投げることを期待
static Value builtin_expect_error(int argc, Value *argv) {
    (void)argc;
    if (g_eval == NULL) return value_bool(false);
    
    if (argv[0].type == VALUE_FUNCTION) {
        // 関数を呼び出して例外を期待
        Environment *call_env = env_new(g_eval->current);
        Environment *saved = g_eval->current;
        g_eval->current = call_env;
        
        evaluate(g_eval, argv[0].function.definition->function.body);
        
        g_eval->current = saved;
        env_release(call_env);
        
        if (g_eval->throwing) {
            // 例外が発生した = 期待通り
            g_eval->throwing = false;
            return value_bool(true);
        }
        
        // 例外が発生しなかった = 失敗
        g_expect_failures++;
        int written = snprintf(g_expect_messages + g_expect_msg_len,
                              sizeof(g_expect_messages) - g_expect_msg_len,
                              "    ✗ 例外が発生しませんでした\n");
        if (written > 0) g_expect_msg_len += written;
        return value_bool(false);
    }
    
    return value_bool(false);
}

// テスト実行() - 登録済みテストを全実行
static Value builtin_test_run(int argc, Value *argv) {
    (void)argc; (void)argv;
    if (g_eval == NULL) return value_null();
    
    int passed = 0;
    int failed = 0;
    
    printf("\n=== テスト実行 ===\n");
    
    for (int i = 0; i < g_test_count; i++) {
        // テスト前にリセット
        g_expect_failures = 0;
        g_expect_msg_len = 0;
        g_expect_messages[0] = '\0';
        
        bool test_error = false;
        
        if (g_tests[i].func.type == VALUE_FUNCTION) {
            Environment *test_env = env_new(g_eval->current);
            Environment *saved = g_eval->current;
            g_eval->current = test_env;
            
            evaluate(g_eval, g_tests[i].func.function.definition->function.body);
            
            if (g_eval->returning) {
                g_eval->returning = false;
            }
            if (g_eval->throwing) {
                g_eval->throwing = false;
                test_error = true;
            }
            if (g_eval->had_error) {
                g_eval->had_error = false;
                test_error = true;
            }
            
            g_eval->current = saved;
            env_release(test_env);
        }
        
        if (g_expect_failures == 0 && !test_error) {
            printf("  ✓ %s\n", g_tests[i].name);
            passed++;
        } else {
            printf("  ✗ %s\n", g_tests[i].name);
            if (g_expect_msg_len > 0) {
                printf("%s", g_expect_messages);
            }
            failed++;
        }
        
        // クリーンアップ
        free(g_tests[i].name);
        value_free(&g_tests[i].func);
    }
    
    printf("\nテスト結果: %d/%d 成功", passed, passed + failed);
    if (failed > 0) {
        printf(" (%d 失敗)", failed);
    }
    printf("\n");
    
    g_test_count = 0;
    
    // 結果辞書を返す
    Value result = value_dict();
    dict_set(&result, "成功", value_number(passed));
    dict_set(&result, "失敗", value_number(failed));
    dict_set(&result, "合計", value_number(passed + failed));
    return result;
}

// =============================================================================
// カスタム例外
// =============================================================================

// 例外作成(種類, メッセージ) - 構造化例外辞書を作成
static Value builtin_create_exception(int argc, Value *argv) {
    (void)argc;
    Value exc = value_dict();
    dict_set(&exc, "種類", value_copy(argv[0]));
    dict_set(&exc, "メッセージ", value_copy(argv[1]));
    return exc;
}

// =============================================================================
// ドキュメントコメント
// =============================================================================

// 文書化(名前, 説明) - ドキュメントを登録
static Value builtin_doc_set(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) return value_null();
    
    // 既存エントリを更新
    for (int i = 0; i < g_doc_count; i++) {
        if (strcmp(g_docs[i].name, argv[0].string.data) == 0) {
            free(g_docs[i].doc);
            g_docs[i].doc = strdup(argv[1].string.data);
            return value_null();
        }
    }
    
    // 新規登録
    if (g_doc_count < MAX_DOCS) {
        g_docs[g_doc_count].name = strdup(argv[0].string.data);
        g_docs[g_doc_count].doc = strdup(argv[1].string.data);
        g_doc_count++;
    }
    return value_null();
}

// 文書(名前) - ドキュメントを取得
static Value builtin_doc_get(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING) return value_null();
    
    for (int i = 0; i < g_doc_count; i++) {
        if (strcmp(g_docs[i].name, argv[0].string.data) == 0) {
            return value_string(g_docs[i].doc);
        }
    }
    return value_null();
}

// 型別名(エイリアス, 元の型名) - 型エイリアスを登録
static Value builtin_type_alias(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_STRING || argv[1].type != VALUE_STRING) {
        return value_null();
    }
    
    // 既存のエイリアスを更新
    for (int i = 0; i < g_type_alias_count; i++) {
        if (strcmp(g_type_aliases[i].alias, argv[0].string.data) == 0) {
            free(g_type_aliases[i].original);
            g_type_aliases[i].original = strdup(argv[1].string.data);
            return value_bool(true);
        }
    }
    
    // 新規登録
    if (g_type_alias_count < MAX_TYPE_ALIASES) {
        g_type_aliases[g_type_alias_count].alias = strdup(argv[0].string.data);
        g_type_aliases[g_type_alias_count].original = strdup(argv[1].string.data);
        g_type_alias_count++;
    }
    return value_bool(true);
}
