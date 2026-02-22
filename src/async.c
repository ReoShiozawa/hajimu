/**
 * 日本語プログラミング言語 - 非同期・並列・スケジューラモジュール実装
 * 
 * v1.2: スレッドプール、条件変数待機、Promise チェーン、
 *       rwlock、セマフォ、アトミックカウンター、チャネル select 等を追加
 *
 * pthread ベースの非同期処理、並列実行、スケジューラ機能
 * WebSocket はシステムソケットベース
 */

#include "async.h"
#include "evaluator.h"
#include "environment.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ── プラットフォーム依存ヘッダー ─────────────────────────── */
#ifdef _WIN32
#  include "win_compat.h"   /* Winsock2 + usleep + gettimeofday + close→closesocket */
#else
#  include <unistd.h>
#  include <sys/time.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <arpa/inet.h>
#  include <fcntl.h>
   /* SSL/TLS用（wssサポート: macOS のみ） */
#  ifdef __APPLE__
#    include <Security/Security.h>
#    include <Security/SecureTransport.h>
#  endif
#endif

// =============================================================================
// グローバル状態
// =============================================================================

static AsyncRuntime g_runtime;
extern Evaluator *g_eval_for_async;  // evaluator.c で公開する

// 非同期タスク実行用の評価器をスレッドセーフに取得するためのグローバル
static Evaluator *get_async_evaluator(void) {
    extern Evaluator *g_eval_for_async;
    return g_eval_for_async;
}

// =============================================================================
// WebSocket 構造体（内部）
// =============================================================================

#define MAX_WS_CONNECTIONS 32

typedef struct {
    int id;
    int sockfd;
    bool connected;
    bool used;
    bool is_ssl;
    char host[256];
    int port;
    pthread_mutex_t mutex;
} WSConnection;

static WSConnection g_ws_connections[MAX_WS_CONNECTIONS];
static int g_next_ws_id = 1;
static pthread_mutex_t g_ws_mutex = PTHREAD_MUTEX_INITIALIZER;

// =============================================================================
// スレッドプール - 内部実装
// =============================================================================

// タスクスロットを ID で検索する（task_mutex をロックした状態で呼ぶ）
static AsyncTask *find_task_locked(int task_id) {
    for (int i = 0; i < MAX_ASYNC_TASKS; i++) {
        if (g_runtime.tasks[i].used && g_runtime.tasks[i].id == task_id) {
            return &g_runtime.tasks[i];
        }
    }
    return NULL;
}

// タスクを1つ実行する共通ロジック
static void execute_task(AsyncTask *task) {
    Evaluator *eval = get_async_evaluator();
    if (eval == NULL) {
        task->status = TASK_FAILED;
        snprintf(task->error_message, sizeof(task->error_message), "評価器が利用できません");
        return;
    }
    
    task->status = TASK_RUNNING;
    
    if (task->function.type == VALUE_BUILTIN) {
        task->result = task->function.builtin.fn(task->arg_count, task->args);
        task->status = TASK_COMPLETED;
    } else if (task->function.type == VALUE_FUNCTION) {
        Evaluator *thread_eval = evaluator_new();
        
        ASTNode *def = task->function.function.definition;
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
        
        Environment *local = env_new(task->function.function.closure);
        for (int i = 0; i < param_count && i < task->arg_count; i++) {
            env_define(local, params[i].name, value_copy(task->args[i]), false);
        }
        
        Environment *prev = thread_eval->current;
        thread_eval->current = local;
        
        Value result = evaluate(thread_eval, body);
        
        if (thread_eval->returning) {
            result = thread_eval->return_value;
            thread_eval->returning = false;
        }
        
        thread_eval->current = prev;
        
        if (thread_eval->had_error) {
            task->status = TASK_FAILED;
            snprintf(task->error_message, sizeof(task->error_message),
                     "%s", thread_eval->error_message);
            task->result = value_null();
        } else {
            task->result = result;
            task->status = TASK_COMPLETED;
        }
        
        env_release(local);
        evaluator_free(thread_eval);
    } else {
        task->status = TASK_FAILED;
        snprintf(task->error_message, sizeof(task->error_message), "呼び出し可能ではありません");
    }
}

// Promise チェーンを処理（タスク完了後に呼ばれる）
static void process_promise_chain(AsyncTask *task) {
    if (task->status == TASK_COMPLETED && task->then_fn.type != VALUE_NULL) {
        // then コールバックを実行: fn(result) → 新結果
        Value args[1] = { value_copy(task->result) };
        AsyncTask temp;
        memset(&temp, 0, sizeof(temp));
        temp.function = task->then_fn;
        temp.args = args;
        temp.arg_count = 1;
        temp.status = TASK_PENDING;
        execute_task(&temp);
        
        // チェーン先タスクに結果を渡す
        if (task->chain_next_id >= 0) {
            pthread_mutex_lock(&g_runtime.task_mutex);
            AsyncTask *next = find_task_locked(task->chain_next_id);
            if (next) {
                next->result = temp.status == TASK_COMPLETED ? temp.result : value_null();
                next->status = temp.status;
                if (temp.status == TASK_FAILED) {
                    strncpy(next->error_message, temp.error_message, sizeof(next->error_message) - 1);
                }
                // 完了通知
                pthread_mutex_lock(&next->completion_mutex);
                next->completion_signaled = true;
                pthread_cond_broadcast(&next->completion_cond);
                pthread_mutex_unlock(&next->completion_mutex);
            }
            pthread_mutex_unlock(&g_runtime.task_mutex);
        }
        value_free(&args[0]);
    } else if (task->status == TASK_FAILED && task->catch_fn.type != VALUE_NULL) {
        // catch コールバックを実行
        Value err_val = value_string(task->error_message);
        Value args[1] = { err_val };
        AsyncTask temp;
        memset(&temp, 0, sizeof(temp));
        temp.function = task->catch_fn;
        temp.args = args;
        temp.arg_count = 1;
        temp.status = TASK_PENDING;
        execute_task(&temp);
        
        if (task->chain_next_id >= 0) {
            pthread_mutex_lock(&g_runtime.task_mutex);
            AsyncTask *next = find_task_locked(task->chain_next_id);
            if (next) {
                next->result = temp.status == TASK_COMPLETED ? temp.result : value_null();
                next->status = temp.status;
                pthread_mutex_lock(&next->completion_mutex);
                next->completion_signaled = true;
                pthread_cond_broadcast(&next->completion_cond);
                pthread_mutex_unlock(&next->completion_mutex);
            }
            pthread_mutex_unlock(&g_runtime.task_mutex);
        }
        value_free(&args[0]);
    }
}

// タスク完了を通知する（条件変数をシグナル）
static void signal_task_completion(AsyncTask *task) {
    pthread_mutex_lock(&task->completion_mutex);
    task->completion_signaled = true;
    pthread_cond_broadcast(&task->completion_cond);
    pthread_mutex_unlock(&task->completion_mutex);
}

// スレッドプール ワーカー関数
static void *pool_worker_thread(void *arg) {
    (void)arg;
    ThreadPool *pool = &g_runtime.pool;
    
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // キューにジョブが来るまで待機
        while (pool->queue_count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_not_empty, &pool->queue_mutex);
        }
        
        if (pool->shutdown && pool->queue_count == 0) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }
        
        // ジョブを取り出す
        PoolJob job = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;
        pool->queue_count--;
        
        pthread_cond_signal(&pool->queue_not_full);
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // タスクを実行
        pthread_mutex_lock(&g_runtime.task_mutex);
        AsyncTask *task = find_task_locked(job.task_id);
        pthread_mutex_unlock(&g_runtime.task_mutex);
        
        if (task && task->used && task->status == TASK_PENDING) {
            execute_task(task);
            process_promise_chain(task);
            signal_task_completion(task);
            
            pthread_mutex_lock(&pool->queue_mutex);
            pool->completed_jobs++;
            pthread_mutex_unlock(&pool->queue_mutex);
        }
    }
    
    return NULL;
}

// スレッドプールを初期化
static void thread_pool_init(int num_threads) {
    ThreadPool *pool = &g_runtime.pool;
    if (pool->initialized) return;
    
    if (num_threads <= 0) num_threads = THREAD_POOL_DEFAULT_SIZE;
    if (num_threads > THREAD_POOL_MAX_SIZE) num_threads = THREAD_POOL_MAX_SIZE;
    
    pool->thread_count = num_threads;
    pool->threads = calloc(num_threads, sizeof(pthread_t));
    pool->queue_capacity = THREAD_POOL_QUEUE_SIZE;
    pool->queue = calloc(pool->queue_capacity, sizeof(PoolJob));
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;
    pool->shutdown = false;
    pool->total_jobs = 0;
    pool->completed_jobs = 0;
    
    pthread_mutex_init(&pool->queue_mutex, NULL);
    pthread_cond_init(&pool->queue_not_empty, NULL);
    pthread_cond_init(&pool->queue_not_full, NULL);
    
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, pool_worker_thread, NULL);
    }
    
    pool->initialized = true;
}

// スレッドプールにジョブを投入
static bool thread_pool_submit(int task_id) {
    ThreadPool *pool = &g_runtime.pool;
    if (!pool->initialized) {
        thread_pool_init(THREAD_POOL_DEFAULT_SIZE);
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    // キューが満杯なら少し待つ
    while (pool->queue_count >= pool->queue_capacity && !pool->shutdown) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;  // 最大1秒待機
        int ret = pthread_cond_timedwait(&pool->queue_not_full, &pool->queue_mutex, &ts);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&pool->queue_mutex);
            return false;
        }
    }
    
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue_mutex);
        return false;
    }
    
    pool->queue[pool->queue_tail].task_id = task_id;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_capacity;
    pool->queue_count++;
    pool->total_jobs++;
    
    pthread_cond_signal(&pool->queue_not_empty);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return true;
}

// スレッドプールをシャットダウン
static void thread_pool_shutdown(void) {
    ThreadPool *pool = &g_runtime.pool;
    if (!pool->initialized) return;
    
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->queue_not_empty);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    free(pool->threads);
    free(pool->queue);
    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_not_empty);
    pthread_cond_destroy(&pool->queue_not_full);
    
    pool->initialized = false;
}

// =============================================================================
// 初期化・解放
// =============================================================================

void async_runtime_init(void) {
    if (g_runtime.initialized) return;
    
    memset(&g_runtime, 0, sizeof(AsyncRuntime));
    
    pthread_mutex_init(&g_runtime.task_mutex, NULL);
    pthread_mutex_init(&g_runtime.channel_mutex, NULL);
    pthread_mutex_init(&g_runtime.schedule_mutex, NULL);
    pthread_mutex_init(&g_runtime.mutex_mgr_mutex, NULL);
    pthread_mutex_init(&g_runtime.rwlock_mgr_mutex, NULL);
    pthread_mutex_init(&g_runtime.semaphore_mgr_mutex, NULL);
    pthread_mutex_init(&g_runtime.atomic_mgr_mutex, NULL);
    
    g_runtime.next_task_id = 1;
    g_runtime.next_channel_id = 1;
    g_runtime.next_schedule_id = 1;
    g_runtime.next_mutex_id = 0;
    g_runtime.next_rwlock_id = 0;
    g_runtime.next_semaphore_id = 0;
    g_runtime.next_atomic_id = 0;
    
    // WebSocket接続の初期化
    memset(g_ws_connections, 0, sizeof(g_ws_connections));
    
    g_runtime.initialized = true;
    
    // デフォルトでスレッドプールを起動
    thread_pool_init(THREAD_POOL_DEFAULT_SIZE);
}

void async_runtime_cleanup(void) {
    if (!g_runtime.initialized) return;
    
    // スレッドプールをシャットダウン
    thread_pool_shutdown();
    
    // スケジュールタスクを全停止
    pthread_mutex_lock(&g_runtime.schedule_mutex);
    for (int i = 0; i < MAX_SCHEDULED_TASKS; i++) {
        if (g_runtime.scheduled[i].used && g_runtime.scheduled[i].active) {
            g_runtime.scheduled[i].active = false;
        }
    }
    pthread_mutex_unlock(&g_runtime.schedule_mutex);
    
    // 少し待ってスレッドが終了するのを待つ
    usleep(100000);  // 100ms
    
    // 非同期タスクをクリーンアップ
    pthread_mutex_lock(&g_runtime.task_mutex);
    for (int i = 0; i < MAX_ASYNC_TASKS; i++) {
        if (g_runtime.tasks[i].used) {
            AsyncTask *task = &g_runtime.tasks[i];
            if (task->args) {
                for (int j = 0; j < task->arg_count; j++) {
                    value_free(&task->args[j]);
                }
                free(task->args);
            }
            value_free(&task->result);
            value_free(&task->function);
            if (task->then_fn.type != VALUE_NULL) value_free(&task->then_fn);
            if (task->catch_fn.type != VALUE_NULL) value_free(&task->catch_fn);
            pthread_mutex_destroy(&task->completion_mutex);
            pthread_cond_destroy(&task->completion_cond);
            task->used = false;
        }
    }
    pthread_mutex_unlock(&g_runtime.task_mutex);
    
    // チャネルをクリーンアップ
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (g_runtime.channels[i].used) {
            g_runtime.channels[i].closed = true;
            pthread_cond_broadcast(&g_runtime.channels[i].not_empty);
            pthread_cond_broadcast(&g_runtime.channels[i].not_full);
            if (g_runtime.channels[i].buffer) {
                free(g_runtime.channels[i].buffer);
            }
            pthread_mutex_destroy(&g_runtime.channels[i].mutex);
            pthread_cond_destroy(&g_runtime.channels[i].not_empty);
            pthread_cond_destroy(&g_runtime.channels[i].not_full);
        }
    }
    
    // WebSocket接続を閉じる
    pthread_mutex_lock(&g_ws_mutex);
    for (int i = 0; i < MAX_WS_CONNECTIONS; i++) {
        if (g_ws_connections[i].used && g_ws_connections[i].connected) {
            close(g_ws_connections[i].sockfd);
            g_ws_connections[i].connected = false;
        }
    }
    pthread_mutex_unlock(&g_ws_mutex);
    
    // ユーザーミューテックスを破棄
    for (int i = 0; i < MAX_USER_MUTEXES; i++) {
        if (g_runtime.user_mutex_used[i]) {
            pthread_mutex_destroy(&g_runtime.user_mutexes[i]);
        }
    }
    
    // 読み書きロックを破棄
    for (int i = 0; i < MAX_USER_RWLOCKS; i++) {
        if (g_runtime.rwlocks[i].used) {
            pthread_rwlock_destroy(&g_runtime.rwlocks[i].lock);
        }
    }
    
    // セマフォを破棄
    for (int i = 0; i < MAX_USER_SEMAPHORES; i++) {
        if (g_runtime.semaphores[i].used) {
            pthread_mutex_destroy(&g_runtime.semaphores[i].mutex);
            pthread_cond_destroy(&g_runtime.semaphores[i].cond);
        }
    }
    
    // アトミックカウンターを破棄
    for (int i = 0; i < MAX_ATOMIC_COUNTERS; i++) {
        if (g_runtime.atomics[i].used) {
            pthread_mutex_destroy(&g_runtime.atomics[i].mutex);
        }
    }
    
    pthread_mutex_destroy(&g_runtime.task_mutex);
    pthread_mutex_destroy(&g_runtime.channel_mutex);
    pthread_mutex_destroy(&g_runtime.schedule_mutex);
    pthread_mutex_destroy(&g_runtime.mutex_mgr_mutex);
    pthread_mutex_destroy(&g_runtime.rwlock_mgr_mutex);
    pthread_mutex_destroy(&g_runtime.semaphore_mgr_mutex);
    pthread_mutex_destroy(&g_runtime.atomic_mgr_mutex);
    
    g_runtime.initialized = false;
}

// =============================================================================
// スレッドプール - 組み込み関数
// =============================================================================

// プール作成(ワーカー数) → 真偽
Value builtin_pool_create(int argc, Value *argv) {
    int num = THREAD_POOL_DEFAULT_SIZE;
    if (argc > 0 && argv[0].type == VALUE_NUMBER) {
        num = (int)argv[0].number;
    }
    if (!g_runtime.initialized) async_runtime_init();
    
    // 既存プールがあればシャットダウンして再作成
    if (g_runtime.pool.initialized) {
        thread_pool_shutdown();
    }
    thread_pool_init(num);
    return value_bool(g_runtime.pool.initialized);
}

// プール情報() → 辞書
Value builtin_pool_stats(int argc, Value *argv) {
    (void)argc; (void)argv;
    
    Value dict = value_dict();
    ThreadPool *pool = &g_runtime.pool;
    
    if (!pool->initialized) {
        dict_set(&dict, "ワーカー数", value_number(0));
        dict_set(&dict, "キュー待ち", value_number(0));
        dict_set(&dict, "完了数", value_number(0));
        dict_set(&dict, "総数", value_number(0));
        return dict;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    dict_set(&dict, "ワーカー数", value_number(pool->thread_count));
    dict_set(&dict, "キュー待ち", value_number(pool->queue_count));
    dict_set(&dict, "完了数", value_number((double)pool->completed_jobs));
    dict_set(&dict, "総数", value_number((double)pool->total_jobs));
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return dict;
}

// =============================================================================
// 非同期タスク - スレッドラッパー（プール未使用時のフォールバック）
// =============================================================================

static void *async_task_runner_standalone(void *arg) {
    AsyncTask *task = (AsyncTask *)arg;
    execute_task(task);
    process_promise_chain(task);
    signal_task_completion(task);
    return NULL;
}

// =============================================================================
// 非同期処理 - 組み込み関数
// =============================================================================

// 非同期実行(関数, [引数...]) → タスクID
Value builtin_async_run(int argc, Value *argv) {
    if (argc < 1) return value_null();
    if (argv[0].type != VALUE_FUNCTION && argv[0].type != VALUE_BUILTIN) {
        return value_null();
    }
    
    if (!g_runtime.initialized) async_runtime_init();
    
    pthread_mutex_lock(&g_runtime.task_mutex);
    
    // 空きスロットを探す
    int slot = -1;
    for (int i = 0; i < MAX_ASYNC_TASKS; i++) {
        if (!g_runtime.tasks[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.task_mutex);
        return value_number(-1);
    }
    
    AsyncTask *task = &g_runtime.tasks[slot];
    memset(task, 0, sizeof(AsyncTask));
    task->id = g_runtime.next_task_id++;
    task->status = TASK_PENDING;
    task->used = true;
    task->function = value_copy(argv[0]);
    task->result = value_null();
    task->then_fn = value_null();
    task->catch_fn = value_null();
    task->chain_next_id = -1;
    
    // 条件変数を初期化
    pthread_mutex_init(&task->completion_mutex, NULL);
    pthread_cond_init(&task->completion_cond, NULL);
    task->completion_signaled = false;
    
    // 引数をコピー
    if (argc > 1) {
        task->arg_count = argc - 1;
        task->args = malloc(sizeof(Value) * task->arg_count);
        for (int i = 0; i < task->arg_count; i++) {
            task->args[i] = value_copy(argv[i + 1]);
        }
    } else {
        task->arg_count = 0;
        task->args = NULL;
    }
    
    int task_id = task->id;
    pthread_mutex_unlock(&g_runtime.task_mutex);
    
    // スレッドプールにジョブを投入
    if (g_runtime.pool.initialized) {
        task->use_pool = true;
        if (!thread_pool_submit(task_id)) {
            // プールに投入できなかった場合はフォールバック
            task->use_pool = false;
            pthread_create(&task->thread, NULL, async_task_runner_standalone, task);
            pthread_detach(task->thread);
        }
    } else {
        task->use_pool = false;
        pthread_create(&task->thread, NULL, async_task_runner_standalone, task);
        pthread_detach(task->thread);
    }
    
    return value_number(task_id);
}

// 待機(タスクID, タイムアウト秒=-1) → 結果値
Value builtin_async_await(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int task_id = (int)argv[0].number;
    double timeout_sec = -1.0;  // デフォルト: 無制限
    if (argc > 1 && argv[1].type == VALUE_NUMBER) {
        timeout_sec = argv[1].number;
    }
    
    // タスクを見つける
    pthread_mutex_lock(&g_runtime.task_mutex);
    AsyncTask *task = find_task_locked(task_id);
    pthread_mutex_unlock(&g_runtime.task_mutex);
    
    if (task == NULL) return value_null();
    
    // 条件変数で完了を待機（ポーリングの代わり）
    pthread_mutex_lock(&task->completion_mutex);
    if (!task->completion_signaled) {
        if (timeout_sec < 0) {
            // 無制限待機
            while (!task->completion_signaled) {
                pthread_cond_wait(&task->completion_cond, &task->completion_mutex);
            }
        } else {
            // タイムアウト付き待機
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += (long)timeout_sec;
            ts.tv_nsec += (long)((timeout_sec - (long)timeout_sec) * 1000000000);
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            
            while (!task->completion_signaled) {
                int ret = pthread_cond_timedwait(&task->completion_cond, &task->completion_mutex, &ts);
                if (ret == ETIMEDOUT) {
                    pthread_mutex_unlock(&task->completion_mutex);
                    return value_null();  // タイムアウト
                }
            }
        }
    }
    pthread_mutex_unlock(&task->completion_mutex);
    
    Value result = value_copy(task->result);
    
    // タスクをクリーンアップ
    pthread_mutex_lock(&g_runtime.task_mutex);
    if (task->args) {
        for (int i = 0; i < task->arg_count; i++) {
            value_free(&task->args[i]);
        }
        free(task->args);
        task->args = NULL;
    }
    value_free(&task->result);
    value_free(&task->function);
    if (task->then_fn.type != VALUE_NULL) value_free(&task->then_fn);
    if (task->catch_fn.type != VALUE_NULL) value_free(&task->catch_fn);
    pthread_mutex_destroy(&task->completion_mutex);
    pthread_cond_destroy(&task->completion_cond);
    task->used = false;
    pthread_mutex_unlock(&g_runtime.task_mutex);
    
    return result;
}

// 待機全(タスクID配列) → 結果配列
Value builtin_async_await_all(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_ARRAY) return value_null();
    
    Value results = value_array_with_capacity(argv[0].array.length);
    
    for (int i = 0; i < argv[0].array.length; i++) {
        Value id_val = argv[0].array.elements[i];
        Value result = builtin_async_await(1, &id_val);
        array_push(&results, result);
    }
    
    return results;
}

// タスク状態(タスクID) → 状態文字列
Value builtin_task_status(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int task_id = (int)argv[0].number;
    
    pthread_mutex_lock(&g_runtime.task_mutex);
    for (int i = 0; i < MAX_ASYNC_TASKS; i++) {
        if (g_runtime.tasks[i].used && g_runtime.tasks[i].id == task_id) {
            const char *status;
            switch (g_runtime.tasks[i].status) {
                case TASK_PENDING:   status = "待機中"; break;
                case TASK_RUNNING:   status = "実行中"; break;
                case TASK_COMPLETED: status = "完了"; break;
                case TASK_FAILED:    status = "失敗"; break;
                default:             status = "不明"; break;
            }
            pthread_mutex_unlock(&g_runtime.task_mutex);
            return value_string(status);
        }
    }
    pthread_mutex_unlock(&g_runtime.task_mutex);
    
    return value_string("不明");
}

// 競争待機(タスクID配列) → 辞書{番号, 結果}
Value builtin_async_race(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_ARRAY) return value_null();
    
    int count = argv[0].array.length;
    if (count == 0) return value_null();
    
    // 各タスクの完了を待つ — 短いスリープでポーリング
    while (1) {
        pthread_mutex_lock(&g_runtime.task_mutex);
        for (int i = 0; i < count; i++) {
            int tid = (int)argv[0].array.elements[i].number;
            AsyncTask *t = find_task_locked(tid);
            if (t && (t->status == TASK_COMPLETED || t->status == TASK_FAILED)) {
                Value result = value_copy(t->result);
                int index = i;
                pthread_mutex_unlock(&g_runtime.task_mutex);
                
                // 結果の辞書を作成
                Value dict = value_dict();
                dict_set(&dict, "番号", value_number(index));
                dict_set(&dict, "結果", result);
                
                // 完了したタスクのリソースを解放
                Value tid_val = value_number(tid);
                builtin_async_await(1, &tid_val);
                
                return dict;
            }
        }
        pthread_mutex_unlock(&g_runtime.task_mutex);
        usleep(500);  // 0.5ms
    }
}

// タスクキャンセル(タスクID) → 真偽
Value builtin_task_cancel(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_bool(false);
    
    int task_id = (int)argv[0].number;
    
    pthread_mutex_lock(&g_runtime.task_mutex);
    AsyncTask *task = find_task_locked(task_id);
    if (task && task->status == TASK_PENDING) {
        task->status = TASK_FAILED;
        snprintf(task->error_message, sizeof(task->error_message), "キャンセルされました");
        task->result = value_null();
        signal_task_completion(task);
        pthread_mutex_unlock(&g_runtime.task_mutex);
        return value_bool(true);
    }
    pthread_mutex_unlock(&g_runtime.task_mutex);
    return value_bool(false);
}

// =============================================================================
// Promise チェーン - 組み込み関数
// =============================================================================

// 成功時(タスクID, 関数) → 新タスクID
Value builtin_then(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER) return value_number(-1);
    if (argv[1].type != VALUE_FUNCTION && argv[1].type != VALUE_BUILTIN) return value_number(-1);
    
    if (!g_runtime.initialized) async_runtime_init();
    
    int task_id = (int)argv[0].number;
    
    // チェーン先となる新しいタスクスロットを確保
    pthread_mutex_lock(&g_runtime.task_mutex);
    
    AsyncTask *source = find_task_locked(task_id);
    if (!source) {
        pthread_mutex_unlock(&g_runtime.task_mutex);
        return value_number(-1);
    }
    
    // 新しいスロットを確保（チェーン結果の受け皿）
    int slot = -1;
    for (int i = 0; i < MAX_ASYNC_TASKS; i++) {
        if (!g_runtime.tasks[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.task_mutex);
        return value_number(-1);
    }
    
    AsyncTask *chain_task = &g_runtime.tasks[slot];
    memset(chain_task, 0, sizeof(AsyncTask));
    chain_task->id = g_runtime.next_task_id++;
    chain_task->status = TASK_PENDING;
    chain_task->used = true;
    chain_task->function = value_null();
    chain_task->result = value_null();
    chain_task->then_fn = value_null();
    chain_task->catch_fn = value_null();
    chain_task->chain_next_id = -1;
    pthread_mutex_init(&chain_task->completion_mutex, NULL);
    pthread_cond_init(&chain_task->completion_cond, NULL);
    chain_task->completion_signaled = false;
    
    int chain_id = chain_task->id;
    
    // ソースタスクに then コールバックを設定
    source->then_fn = value_copy(argv[1]);
    source->chain_next_id = chain_id;
    
    // ソースが既に完了している場合は即座にチェーンを実行
    if (source->status == TASK_COMPLETED || source->status == TASK_FAILED) {
        pthread_mutex_unlock(&g_runtime.task_mutex);
        process_promise_chain(source);
    } else {
        pthread_mutex_unlock(&g_runtime.task_mutex);
    }
    
    return value_number(chain_id);
}

// 失敗時(タスクID, 関数) → 新タスクID
Value builtin_catch(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER) return value_number(-1);
    if (argv[1].type != VALUE_FUNCTION && argv[1].type != VALUE_BUILTIN) return value_number(-1);
    
    if (!g_runtime.initialized) async_runtime_init();
    
    int task_id = (int)argv[0].number;
    
    pthread_mutex_lock(&g_runtime.task_mutex);
    
    AsyncTask *source = find_task_locked(task_id);
    if (!source) {
        pthread_mutex_unlock(&g_runtime.task_mutex);
        return value_number(-1);
    }
    
    // チェーン先タスクが既にあればそれを使う、なければ新規作成
    int chain_id = source->chain_next_id;
    if (chain_id < 0) {
        int slot = -1;
        for (int i = 0; i < MAX_ASYNC_TASKS; i++) {
            if (!g_runtime.tasks[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            pthread_mutex_unlock(&g_runtime.task_mutex);
            return value_number(-1);
        }
        AsyncTask *chain_task = &g_runtime.tasks[slot];
        memset(chain_task, 0, sizeof(AsyncTask));
        chain_task->id = g_runtime.next_task_id++;
        chain_task->status = TASK_PENDING;
        chain_task->used = true;
        chain_task->function = value_null();
        chain_task->result = value_null();
        chain_task->then_fn = value_null();
        chain_task->catch_fn = value_null();
        chain_task->chain_next_id = -1;
        pthread_mutex_init(&chain_task->completion_mutex, NULL);
        pthread_cond_init(&chain_task->completion_cond, NULL);
        chain_task->completion_signaled = false;
        chain_id = chain_task->id;
        source->chain_next_id = chain_id;
    }
    
    source->catch_fn = value_copy(argv[1]);
    
    if (source->status == TASK_COMPLETED || source->status == TASK_FAILED) {
        pthread_mutex_unlock(&g_runtime.task_mutex);
        process_promise_chain(source);
    } else {
        pthread_mutex_unlock(&g_runtime.task_mutex);
    }
    
    return value_number(chain_id);
}

// =============================================================================
// 並列処理 - 組み込み関数
// =============================================================================

// 並列実行(関数配列) → 結果配列
Value builtin_parallel_run(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_ARRAY) return value_null();
    
    if (!g_runtime.initialized) async_runtime_init();
    
    int count = argv[0].array.length;
    if (count == 0) return value_array();
    
    // 全関数を非同期実行
    Value task_ids = value_array_with_capacity(count);
    
    for (int i = 0; i < count; i++) {
        Value func = argv[0].array.elements[i];
        if (func.type != VALUE_FUNCTION && func.type != VALUE_BUILTIN) {
            array_push(&task_ids, value_number(-1));
            continue;
        }
        Value id = builtin_async_run(1, &func);
        array_push(&task_ids, id);
    }
    
    // 全タスクの完了を待機
    Value results = builtin_async_await_all(1, &task_ids);
    
    value_free(&task_ids);
    return results;
}

// 並列マップ(配列, 関数) → 結果配列
Value builtin_parallel_map(int argc, Value *argv) {
    if (argc < 2) return value_null();
    if (argv[0].type != VALUE_ARRAY) return value_null();
    if (argv[1].type != VALUE_FUNCTION && argv[1].type != VALUE_BUILTIN) return value_null();
    
    if (!g_runtime.initialized) async_runtime_init();
    
    int count = argv[0].array.length;
    if (count == 0) return value_array();
    
    Value task_ids = value_array_with_capacity(count);
    
    for (int i = 0; i < count; i++) {
        Value args[2] = { value_copy(argv[1]), value_copy(argv[0].array.elements[i]) };
        Value id = builtin_async_run(2, args);
        array_push(&task_ids, id);
        value_free(&args[0]);
        value_free(&args[1]);
    }
    
    Value results = builtin_async_await_all(1, &task_ids);
    value_free(&task_ids);
    return results;
}

// 排他作成() → ミューテックスID
Value builtin_mutex_create(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    
    if (!g_runtime.initialized) async_runtime_init();
    
    pthread_mutex_lock(&g_runtime.mutex_mgr_mutex);
    
    int slot = -1;
    for (int i = 0; i < MAX_USER_MUTEXES; i++) {
        if (!g_runtime.user_mutex_used[i]) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.mutex_mgr_mutex);
        return value_number(-1);
    }
    
    pthread_mutex_init(&g_runtime.user_mutexes[slot], NULL);
    g_runtime.user_mutex_used[slot] = true;
    
    pthread_mutex_unlock(&g_runtime.mutex_mgr_mutex);
    
    return value_number(slot);
}

// 排他実行(ミューテックスID, 関数) → 結果
Value builtin_mutex_exec(int argc, Value *argv) {
    if (argc < 2) return value_null();
    if (argv[0].type != VALUE_NUMBER) return value_null();
    if (argv[1].type != VALUE_FUNCTION && argv[1].type != VALUE_BUILTIN) return value_null();
    
    int mutex_id = (int)argv[0].number;
    if (mutex_id < 0 || mutex_id >= MAX_USER_MUTEXES || !g_runtime.user_mutex_used[mutex_id]) {
        return value_null();
    }
    
    pthread_mutex_lock(&g_runtime.user_mutexes[mutex_id]);
    
    Value result;
    if (argv[1].type == VALUE_BUILTIN) {
        result = argv[1].builtin.fn(0, NULL);
    } else {
        Value id = builtin_async_run(1, &argv[1]);
        result = builtin_async_await(1, &id);
    }
    
    pthread_mutex_unlock(&g_runtime.user_mutexes[mutex_id]);
    
    return result;
}

// =============================================================================
// 読み書きロック - 組み込み関数
// =============================================================================

// 読書ロック作成() → ロックID
Value builtin_rwlock_create(int argc, Value *argv) {
    (void)argc; (void)argv;
    
    if (!g_runtime.initialized) async_runtime_init();
    
    pthread_mutex_lock(&g_runtime.rwlock_mgr_mutex);
    
    int slot = -1;
    for (int i = 0; i < MAX_USER_RWLOCKS; i++) {
        if (!g_runtime.rwlocks[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.rwlock_mgr_mutex);
        return value_number(-1);
    }
    
    pthread_rwlock_init(&g_runtime.rwlocks[slot].lock, NULL);
    g_runtime.rwlocks[slot].used = true;
    
    pthread_mutex_unlock(&g_runtime.rwlock_mgr_mutex);
    
    return value_number(slot);
}

// 読取実行(ロックID, 関数) → 結果
Value builtin_rwlock_read(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER) return value_null();
    if (argv[1].type != VALUE_FUNCTION && argv[1].type != VALUE_BUILTIN) return value_null();
    
    int lock_id = (int)argv[0].number;
    if (lock_id < 0 || lock_id >= MAX_USER_RWLOCKS || !g_runtime.rwlocks[lock_id].used) {
        return value_null();
    }
    
    pthread_rwlock_rdlock(&g_runtime.rwlocks[lock_id].lock);
    
    Value result;
    if (argv[1].type == VALUE_BUILTIN) {
        result = argv[1].builtin.fn(0, NULL);
    } else {
        Value id = builtin_async_run(1, &argv[1]);
        result = builtin_async_await(1, &id);
    }
    
    pthread_rwlock_unlock(&g_runtime.rwlocks[lock_id].lock);
    
    return result;
}

// 書込実行(ロックID, 関数) → 結果
Value builtin_rwlock_write(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER) return value_null();
    if (argv[1].type != VALUE_FUNCTION && argv[1].type != VALUE_BUILTIN) return value_null();
    
    int lock_id = (int)argv[0].number;
    if (lock_id < 0 || lock_id >= MAX_USER_RWLOCKS || !g_runtime.rwlocks[lock_id].used) {
        return value_null();
    }
    
    pthread_rwlock_wrlock(&g_runtime.rwlocks[lock_id].lock);
    
    Value result;
    if (argv[1].type == VALUE_BUILTIN) {
        result = argv[1].builtin.fn(0, NULL);
    } else {
        Value id = builtin_async_run(1, &argv[1]);
        result = builtin_async_await(1, &id);
    }
    
    pthread_rwlock_unlock(&g_runtime.rwlocks[lock_id].lock);
    
    return result;
}

// =============================================================================
// セマフォ - 組み込み関数
// =============================================================================

// セマフォ作成(上限数) → セマフォID
Value builtin_semaphore_create(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_number(-1);
    
    if (!g_runtime.initialized) async_runtime_init();
    
    int max_count = (int)argv[0].number;
    if (max_count < 1) max_count = 1;
    
    pthread_mutex_lock(&g_runtime.semaphore_mgr_mutex);
    
    int slot = -1;
    for (int i = 0; i < MAX_USER_SEMAPHORES; i++) {
        if (!g_runtime.semaphores[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.semaphore_mgr_mutex);
        return value_number(-1);
    }
    
    UserSemaphore *sem = &g_runtime.semaphores[slot];
    pthread_mutex_init(&sem->mutex, NULL);
    pthread_cond_init(&sem->cond, NULL);
    sem->count = max_count;
    sem->max_count = max_count;
    sem->used = true;
    
    pthread_mutex_unlock(&g_runtime.semaphore_mgr_mutex);
    
    return value_number(slot);
}

// セマフォ獲得(セマフォID) → 真偽
Value builtin_semaphore_acquire(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_bool(false);
    
    int sem_id = (int)argv[0].number;
    if (sem_id < 0 || sem_id >= MAX_USER_SEMAPHORES || !g_runtime.semaphores[sem_id].used) {
        return value_bool(false);
    }
    
    UserSemaphore *sem = &g_runtime.semaphores[sem_id];
    
    pthread_mutex_lock(&sem->mutex);
    while (sem->count <= 0) {
        pthread_cond_wait(&sem->cond, &sem->mutex);
    }
    sem->count--;
    pthread_mutex_unlock(&sem->mutex);
    
    return value_bool(true);
}

// セマフォ解放(セマフォID) → 真偽
Value builtin_semaphore_release(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_bool(false);
    
    int sem_id = (int)argv[0].number;
    if (sem_id < 0 || sem_id >= MAX_USER_SEMAPHORES || !g_runtime.semaphores[sem_id].used) {
        return value_bool(false);
    }
    
    UserSemaphore *sem = &g_runtime.semaphores[sem_id];
    
    pthread_mutex_lock(&sem->mutex);
    if (sem->count < sem->max_count) {
        sem->count++;
        pthread_cond_signal(&sem->cond);
    }
    pthread_mutex_unlock(&sem->mutex);
    
    return value_bool(true);
}

// セマフォ実行(セマフォID, 関数) → 結果
Value builtin_semaphore_exec(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER) return value_null();
    if (argv[1].type != VALUE_FUNCTION && argv[1].type != VALUE_BUILTIN) return value_null();
    
    // 獲得
    builtin_semaphore_acquire(1, &argv[0]);
    
    Value result;
    if (argv[1].type == VALUE_BUILTIN) {
        result = argv[1].builtin.fn(0, NULL);
    } else {
        Value id = builtin_async_run(1, &argv[1]);
        result = builtin_async_await(1, &id);
    }
    
    // 解放
    builtin_semaphore_release(1, &argv[0]);
    
    return result;
}

// =============================================================================
// アトミックカウンター - 組み込み関数
// =============================================================================

// カウンター作成(初期値=0) → カウンターID
Value builtin_atomic_create(int argc, Value *argv) {
    if (!g_runtime.initialized) async_runtime_init();
    
    long long initial = 0;
    if (argc > 0 && argv[0].type == VALUE_NUMBER) {
        initial = (long long)argv[0].number;
    }
    
    pthread_mutex_lock(&g_runtime.atomic_mgr_mutex);
    
    int slot = -1;
    for (int i = 0; i < MAX_ATOMIC_COUNTERS; i++) {
        if (!g_runtime.atomics[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.atomic_mgr_mutex);
        return value_number(-1);
    }
    
    AtomicCounter *ac = &g_runtime.atomics[slot];
    ac->value = initial;
    pthread_mutex_init(&ac->mutex, NULL);
    ac->used = true;
    
    pthread_mutex_unlock(&g_runtime.atomic_mgr_mutex);
    
    return value_number(slot);
}

// カウンター加算(カウンターID, 加算値=1) → 加算後の値
Value builtin_atomic_add(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int id = (int)argv[0].number;
    if (id < 0 || id >= MAX_ATOMIC_COUNTERS || !g_runtime.atomics[id].used) {
        return value_null();
    }
    
    long long delta = 1;
    if (argc > 1 && argv[1].type == VALUE_NUMBER) {
        delta = (long long)argv[1].number;
    }
    
    AtomicCounter *ac = &g_runtime.atomics[id];
    pthread_mutex_lock(&ac->mutex);
    ac->value += delta;
    long long new_val = ac->value;
    pthread_mutex_unlock(&ac->mutex);
    
    return value_number((double)new_val);
}

// カウンター取得(カウンターID) → 値
Value builtin_atomic_get(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int id = (int)argv[0].number;
    if (id < 0 || id >= MAX_ATOMIC_COUNTERS || !g_runtime.atomics[id].used) {
        return value_null();
    }
    
    AtomicCounter *ac = &g_runtime.atomics[id];
    pthread_mutex_lock(&ac->mutex);
    long long val = ac->value;
    pthread_mutex_unlock(&ac->mutex);
    
    return value_number((double)val);
}

// カウンター設定(カウンターID, 値) → 古い値
Value builtin_atomic_set(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) return value_null();
    
    int id = (int)argv[0].number;
    if (id < 0 || id >= MAX_ATOMIC_COUNTERS || !g_runtime.atomics[id].used) {
        return value_null();
    }
    
    AtomicCounter *ac = &g_runtime.atomics[id];
    pthread_mutex_lock(&ac->mutex);
    long long old_val = ac->value;
    ac->value = (long long)argv[1].number;
    pthread_mutex_unlock(&ac->mutex);
    
    return value_number((double)old_val);
}

// =============================================================================
// チャネル - 組み込み関数
// =============================================================================

// チャネル作成(容量=1) → チャネルID
Value builtin_channel_create(int argc, Value *argv) {
    if (!g_runtime.initialized) async_runtime_init();
    
    int capacity = 1;
    if (argc > 0 && argv[0].type == VALUE_NUMBER) {
        capacity = (int)argv[0].number;
        if (capacity < 1) capacity = 1;
        if (capacity > 4096) capacity = 4096;
    }
    
    pthread_mutex_lock(&g_runtime.channel_mutex);
    
    int slot = -1;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!g_runtime.channels[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.channel_mutex);
        return value_number(-1);
    }
    
    Channel *ch = &g_runtime.channels[slot];
    memset(ch, 0, sizeof(Channel));
    ch->id = g_runtime.next_channel_id++;
    ch->capacity = capacity;
    ch->buffer = calloc(capacity, sizeof(Value));
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->closed = false;
    ch->used = true;
    
    pthread_mutex_init(&ch->mutex, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    
    int ch_id = ch->id;
    pthread_mutex_unlock(&g_runtime.channel_mutex);
    
    return value_number(ch_id);
}

// チャネルをIDから検索するヘルパー
static Channel *find_channel(int ch_id) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (g_runtime.channels[i].used && g_runtime.channels[i].id == ch_id) {
            return &g_runtime.channels[i];
        }
    }
    return NULL;
}

// チャネル送信(チャネルID, 値) → 真偽
Value builtin_channel_send(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER) return value_bool(false);
    
    int ch_id = (int)argv[0].number;
    Channel *ch = find_channel(ch_id);
    
    if (ch == NULL || ch->closed) return value_bool(false);
    
    pthread_mutex_lock(&ch->mutex);
    
    // バッファが満杯なら待機
    while (ch->count >= ch->capacity && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->mutex);
    }
    
    if (ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        return value_bool(false);
    }
    
    // 値をバッファに追加
    ch->buffer[ch->tail] = value_copy(argv[1]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;
    
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
    
    return value_bool(true);
}

// チャネル受信(チャネルID) → 値
Value builtin_channel_receive(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int ch_id = (int)argv[0].number;
    Channel *ch = find_channel(ch_id);
    
    if (ch == NULL) return value_null();
    
    pthread_mutex_lock(&ch->mutex);
    
    // バッファが空なら待機
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->mutex);
    }
    
    if (ch->count == 0) {
        pthread_mutex_unlock(&ch->mutex);
        return value_null();  // closed かつ空
    }
    
    // 値を取り出す
    Value result = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;
    
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    
    return result;
}

// チャネル閉じる(チャネルID)
Value builtin_channel_close(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int ch_id = (int)argv[0].number;
    Channel *ch = find_channel(ch_id);
    
    if (ch) {
        pthread_mutex_lock(&ch->mutex);
        ch->closed = true;
        pthread_cond_broadcast(&ch->not_empty);
        pthread_cond_broadcast(&ch->not_full);
        pthread_mutex_unlock(&ch->mutex);
    }
    
    return value_null();
}

// チャネル試送信(チャネルID, 値) → 真偽（非ブロッキング）
Value builtin_channel_try_send(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER) return value_bool(false);
    
    int ch_id = (int)argv[0].number;
    Channel *ch = find_channel(ch_id);
    
    if (ch == NULL || ch->closed) return value_bool(false);
    
    pthread_mutex_lock(&ch->mutex);
    
    if (ch->count >= ch->capacity || ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        return value_bool(false);
    }
    
    ch->buffer[ch->tail] = value_copy(argv[1]);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;
    
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
    
    return value_bool(true);
}

// チャネル試受信(チャネルID) → 辞書{成功, 値}（非ブロッキング）
Value builtin_channel_try_receive(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) {
        Value d = value_dict();
        dict_set(&d, "成功", value_bool(false));
        dict_set(&d, "値", value_null());
        return d;
    }
    
    int ch_id = (int)argv[0].number;
    Channel *ch = find_channel(ch_id);
    
    Value dict = value_dict();
    
    if (ch == NULL) {
        dict_set(&dict, "成功", value_bool(false));
        dict_set(&dict, "値", value_null());
        return dict;
    }
    
    pthread_mutex_lock(&ch->mutex);
    
    if (ch->count == 0) {
        pthread_mutex_unlock(&ch->mutex);
        dict_set(&dict, "成功", value_bool(false));
        dict_set(&dict, "値", value_null());
        return dict;
    }
    
    Value result = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;
    
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    
    dict_set(&dict, "成功", value_bool(true));
    dict_set(&dict, "値", result);
    return dict;
}

// チャネル残量(チャネルID) → 数値
Value builtin_channel_count(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_number(0);
    
    int ch_id = (int)argv[0].number;
    Channel *ch = find_channel(ch_id);
    
    if (ch == NULL) return value_number(0);
    
    pthread_mutex_lock(&ch->mutex);
    int count = ch->count;
    pthread_mutex_unlock(&ch->mutex);
    
    return value_number(count);
}

// チャネル選択(チャネルID配列, タイムアウト秒=-1) → 辞書{番号, 値}
Value builtin_channel_select(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_ARRAY) return value_null();
    
    int ch_count = argv[0].array.length;
    if (ch_count == 0) return value_null();
    
    double timeout_sec = -1.0;
    if (argc > 1 && argv[1].type == VALUE_NUMBER) {
        timeout_sec = argv[1].number;
    }
    
    struct timespec deadline;
    bool has_deadline = (timeout_sec >= 0);
    if (has_deadline) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += (long)timeout_sec;
        deadline.tv_nsec += (long)((timeout_sec - (long)timeout_sec) * 1000000000);
        if (deadline.tv_nsec >= 1000000000) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000;
        }
    }
    
    // チャネル配列を取得
    Channel **channels = malloc(sizeof(Channel *) * ch_count);
    for (int i = 0; i < ch_count; i++) {
        int cid = (int)argv[0].array.elements[i].number;
        channels[i] = find_channel(cid);
    }
    
    // ラウンドロビンで各チャネルをポーリング
    while (1) {
        for (int i = 0; i < ch_count; i++) {
            Channel *ch = channels[i];
            if (!ch || ch->closed) continue;
            
            pthread_mutex_lock(&ch->mutex);
            if (ch->count > 0) {
                Value result = ch->buffer[ch->head];
                ch->head = (ch->head + 1) % ch->capacity;
                ch->count--;
                pthread_cond_signal(&ch->not_full);
                pthread_mutex_unlock(&ch->mutex);
                
                Value dict = value_dict();
                dict_set(&dict, "番号", value_number(i));
                dict_set(&dict, "値", result);
                free(channels);
                return dict;
            }
            pthread_mutex_unlock(&ch->mutex);
        }
        
        // タイムアウトチェック
        if (has_deadline) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > deadline.tv_sec ||
                (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                free(channels);
                return value_null();
            }
        }
        
        // 全チャネルが閉じていればnull
        bool all_closed = true;
        for (int i = 0; i < ch_count; i++) {
            if (channels[i] && !channels[i]->closed) {
                all_closed = false;
                break;
            }
        }
        if (all_closed) {
            free(channels);
            return value_null();
        }
        
        usleep(500);  // 0.5ms スリープ
    }
}

// =============================================================================
// スケジューラ - 内部ヘルパー
// =============================================================================

static void *schedule_task_runner(void *arg) {
    ScheduledTask *task = (ScheduledTask *)arg;
    
    // 初期遅延
    if (task->delay_sec > 0) {
        usleep((useconds_t)(task->delay_sec * 1000000));
    }
    
    do {
        if (!task->active) break;
        
        // 関数を実行
        if (task->function.type == VALUE_BUILTIN) {
            task->function.builtin.fn(0, NULL);
        } else if (task->function.type == VALUE_FUNCTION) {
            Evaluator *thread_eval = evaluator_new();
            
            ASTNode *def = task->function.function.definition;
            ASTNode *body;
            
            if (def->type == NODE_LAMBDA) {
                body = def->lambda.body;
            } else {
                body = def->function.body;
            }
            
            Environment *local = env_new(task->function.function.closure);
            Environment *prev = thread_eval->current;
            thread_eval->current = local;
            
            evaluate(thread_eval, body);
            
            if (thread_eval->returning) {
                thread_eval->returning = false;
            }
            
            thread_eval->current = prev;
            env_release(local);
            evaluator_free(thread_eval);
        }
        
        if (!task->repeat || !task->active) break;
        
        // インターバル待機
        usleep((useconds_t)(task->interval_sec * 1000000));
        
    } while (task->active && task->repeat);
    
    task->active = false;
    return NULL;
}

// =============================================================================
// スケジューラ - 組み込み関数
// =============================================================================

// 定期実行(関数, 間隔秒) → スケジュールID
Value builtin_schedule_interval(int argc, Value *argv) {
    if (argc < 2) return value_number(-1);
    if (argv[0].type != VALUE_FUNCTION && argv[0].type != VALUE_BUILTIN) return value_number(-1);
    if (argv[1].type != VALUE_NUMBER) return value_number(-1);
    
    if (!g_runtime.initialized) async_runtime_init();
    
    pthread_mutex_lock(&g_runtime.schedule_mutex);
    
    int slot = -1;
    for (int i = 0; i < MAX_SCHEDULED_TASKS; i++) {
        if (!g_runtime.scheduled[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.schedule_mutex);
        return value_number(-1);
    }
    
    ScheduledTask *task = &g_runtime.scheduled[slot];
    memset(task, 0, sizeof(ScheduledTask));
    task->id = g_runtime.next_schedule_id++;
    task->function = value_copy(argv[0]);
    task->interval_sec = argv[1].number;
    task->delay_sec = 0;
    task->repeat = true;
    task->active = true;
    task->used = true;
    
    int task_id = task->id;
    
    // スレッドを作成
    pthread_create(&task->thread, NULL, schedule_task_runner, task);
    pthread_detach(task->thread);
    
    pthread_mutex_unlock(&g_runtime.schedule_mutex);
    
    return value_number(task_id);
}

// 遅延実行(関数, 遅延秒) → スケジュールID
Value builtin_schedule_delay(int argc, Value *argv) {
    if (argc < 2) return value_number(-1);
    if (argv[0].type != VALUE_FUNCTION && argv[0].type != VALUE_BUILTIN) return value_number(-1);
    if (argv[1].type != VALUE_NUMBER) return value_number(-1);
    
    if (!g_runtime.initialized) async_runtime_init();
    
    pthread_mutex_lock(&g_runtime.schedule_mutex);
    
    int slot = -1;
    for (int i = 0; i < MAX_SCHEDULED_TASKS; i++) {
        if (!g_runtime.scheduled[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_runtime.schedule_mutex);
        return value_number(-1);
    }
    
    ScheduledTask *task = &g_runtime.scheduled[slot];
    memset(task, 0, sizeof(ScheduledTask));
    task->id = g_runtime.next_schedule_id++;
    task->function = value_copy(argv[0]);
    task->interval_sec = 0;
    task->delay_sec = argv[1].number;
    task->repeat = false;
    task->active = true;
    task->used = true;
    
    int task_id = task->id;
    
    pthread_create(&task->thread, NULL, schedule_task_runner, task);
    pthread_detach(task->thread);
    
    pthread_mutex_unlock(&g_runtime.schedule_mutex);
    
    return value_number(task_id);
}

// スケジュール停止(スケジュールID)
Value builtin_schedule_stop(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int sched_id = (int)argv[0].number;
    
    pthread_mutex_lock(&g_runtime.schedule_mutex);
    for (int i = 0; i < MAX_SCHEDULED_TASKS; i++) {
        if (g_runtime.scheduled[i].used && g_runtime.scheduled[i].id == sched_id) {
            g_runtime.scheduled[i].active = false;
            break;
        }
    }
    pthread_mutex_unlock(&g_runtime.schedule_mutex);
    
    return value_null();
}

// 全スケジュール停止()
Value builtin_schedule_stop_all(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    
    pthread_mutex_lock(&g_runtime.schedule_mutex);
    for (int i = 0; i < MAX_SCHEDULED_TASKS; i++) {
        if (g_runtime.scheduled[i].used) {
            g_runtime.scheduled[i].active = false;
        }
    }
    pthread_mutex_unlock(&g_runtime.schedule_mutex);
    
    return value_null();
}

// =============================================================================
// WebSocket - 内部ヘルパー
// =============================================================================

// URL解析: ws://host:port/path → host, port, path
static bool parse_ws_url(const char *url, char *host, int *port, char *path, bool *use_ssl) {
    *use_ssl = false;
    *port = 80;
    
    const char *p = url;
    
    if (strncmp(p, "wss://", 6) == 0) {
        *use_ssl = true;
        *port = 443;
        p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
    } else {
        return false;
    }
    
    // ホスト名を取得
    const char *host_start = p;
    while (*p && *p != ':' && *p != '/' && *p != '?') p++;
    int host_len = (int)(p - host_start);
    if (host_len >= 255) return false;
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';
    
    // ポート番号
    if (*p == ':') {
        p++;
        *port = atoi(p);
        while (*p && *p != '/') p++;
    }
    
    // パス
    if (*p == '/') {
        strncpy(path, p, 1023);
        path[1023] = '\0';
    } else {
        strcpy(path, "/");
    }
    
    return true;
}

// Base64エンコード（WebSocketハンドシェイク用）
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char *input, int length, char *output) {
    int i, j = 0;
    for (i = 0; i < length - 2; i += 3) {
        output[j++] = b64_table[(input[i] >> 2) & 0x3F];
        output[j++] = b64_table[((input[i] & 0x3) << 4) | ((input[i+1] >> 4) & 0xF)];
        output[j++] = b64_table[((input[i+1] & 0xF) << 2) | ((input[i+2] >> 6) & 0x3)];
        output[j++] = b64_table[input[i+2] & 0x3F];
    }
    if (i < length) {
        output[j++] = b64_table[(input[i] >> 2) & 0x3F];
        if (i + 1 < length) {
            output[j++] = b64_table[((input[i] & 0x3) << 4) | ((input[i+1] >> 4) & 0xF)];
            output[j++] = b64_table[(input[i+1] & 0xF) << 2];
        } else {
            output[j++] = b64_table[(input[i] & 0x3) << 4];
            output[j++] = '=';
        }
        output[j++] = '=';
    }
    output[j] = '\0';
}

// ランダムなWebSocketキーを生成
static void generate_ws_key(char *key) {
    unsigned char random_bytes[16];
    for (int i = 0; i < 16; i++) {
        random_bytes[i] = rand() & 0xFF;
    }
    base64_encode(random_bytes, 16, key);
}

// WebSocketハンドシェイク
static bool ws_handshake(int sockfd, const char *host, int port, const char *path) {
    char key[32];
    generate_ws_key(key);
    
    char request[2048];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, port, key);
    
    if (send(sockfd, request, strlen(request), 0) < 0) {
        return false;
    }
    
    // レスポンスを読み取り
    char response[4096];
    int total = 0;
    while (total < (int)sizeof(response) - 1) {
        int n = (int)recv(sockfd, response + total, sizeof(response) - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        response[total] = '\0';
        if (strstr(response, "\r\n\r\n")) break;
    }
    
    // 101 Switching Protocols を確認
    if (strstr(response, "101") == NULL) {
        return false;
    }
    
    return true;
}

// WebSocketフレーム送信
static bool ws_send_frame(int sockfd, const char *data, int len) {
    int frame_size = 14 + len;  // ヘッダー最大10 + マスク4 + データ
    unsigned char *frame = malloc(frame_size);
    if (!frame) return false;
    int offset = 0;
    
    // FIN + TEXT opcode
    frame[offset++] = 0x81;
    
    // マスクビット + ペイロード長
    if (len <= 125) {
        frame[offset++] = 0x80 | len;
    } else if (len <= 65535) {
        frame[offset++] = 0x80 | 126;
        frame[offset++] = (len >> 8) & 0xFF;
        frame[offset++] = len & 0xFF;
    } else {
        frame[offset++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) {
            frame[offset++] = (len >> (8 * i)) & 0xFF;
        }
    }
    
    // マスクキー（ランダム）
    unsigned char mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = rand() & 0xFF;
    }
    memcpy(frame + offset, mask, 4);
    offset += 4;
    
    // マスク済みデータ
    for (int i = 0; i < len; i++) {
        frame[offset++] = data[i] ^ mask[i % 4];
    }
    
    int sent = 0;
    while (sent < offset) {
        int n = (int)send(sockfd, frame + sent, offset - sent, 0);
        if (n <= 0) { free(frame); return false; }
        sent += n;
    }
    
    free(frame);
    return true;
}

// WebSocketフレーム受信
static int ws_recv_frame(int sockfd, char *buffer, int buf_size, double timeout_sec) {
    // タイムアウト設定
    struct timeval tv;
    tv.tv_sec = (long)timeout_sec;
    tv.tv_usec = (long)((timeout_sec - (long)timeout_sec) * 1000000);
#ifdef _WIN32
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    
    unsigned char header[2];
    int n = (int)recv(sockfd, header, 2, 0);
    if (n <= 0) return -1;
    
    // opcode確認
    int opcode = header[0] & 0x0F;
    if (opcode == 0x8) return -2;  // Close frame
    
    bool masked = (header[1] & 0x80) != 0;
    int payload_len = header[1] & 0x7F;
    
    if (payload_len == 126) {
        unsigned char len16[2];
        if (recv(sockfd, len16, 2, 0) != 2) return -1;
        payload_len = (len16[0] << 8) | len16[1];
    } else if (payload_len == 127) {
        unsigned char len64[8];
        if (recv(sockfd, len64, 8, 0) != 8) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | len64[i];
        }
    }
    
    if (payload_len >= buf_size) payload_len = buf_size - 1;
    
    // マスクキー
    unsigned char mask[4] = {0};
    if (masked) {
        if (recv(sockfd, mask, 4, 0) != 4) return -1;
    }
    
    // ペイロード
    int received = 0;
    while (received < payload_len) {
        n = (int)recv(sockfd, buffer + received, payload_len - received, 0);
        if (n <= 0) break;
        received += n;
    }
    
    // アンマスク
    if (masked) {
        for (int i = 0; i < received; i++) {
            buffer[i] ^= mask[i % 4];
        }
    }
    
    buffer[received] = '\0';
    return received;
}

// =============================================================================
// WebSocket - 組み込み関数
// =============================================================================

// WS接続(URL) → 接続ID
Value builtin_ws_connect(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_STRING) return value_number(-1);
    
    if (!g_runtime.initialized) async_runtime_init();
    
    char host[256] = {0};
    int port = 80;
    char path[1024] = {0};
    bool use_ssl = false;
    
    if (!parse_ws_url(argv[0].string.data, host, &port, path, &use_ssl)) {
        return value_number(-1);
    }
    
    // SSL/TLSは現在未サポート（ws://のみ）
    if (use_ssl) {
        fprintf(stderr, "警告: wss:// は現在未サポートです。ws:// を使用してください。\n");
        return value_number(-1);
    }
    
    // ソケット作成
    struct hostent *he = gethostbyname(host);
    if (he == NULL) return value_number(-1);
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return value_number(-1);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return value_number(-1);
    }
    
    // WebSocketハンドシェイク
    if (!ws_handshake(sockfd, host, port, path)) {
        close(sockfd);
        return value_number(-1);
    }
    
    // 接続を保存
    pthread_mutex_lock(&g_ws_mutex);
    
    int slot = -1;
    for (int i = 0; i < MAX_WS_CONNECTIONS; i++) {
        if (!g_ws_connections[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        close(sockfd);
        pthread_mutex_unlock(&g_ws_mutex);
        return value_number(-1);
    }
    
    g_ws_connections[slot].id = g_next_ws_id++;
    g_ws_connections[slot].sockfd = sockfd;
    g_ws_connections[slot].connected = true;
    g_ws_connections[slot].used = true;
    g_ws_connections[slot].is_ssl = false;
    strncpy(g_ws_connections[slot].host, host, sizeof(g_ws_connections[slot].host) - 1);
    g_ws_connections[slot].port = port;
    pthread_mutex_init(&g_ws_connections[slot].mutex, NULL);
    
    int ws_id = g_ws_connections[slot].id;
    pthread_mutex_unlock(&g_ws_mutex);
    
    return value_number(ws_id);
}

// WS送信(接続ID, メッセージ) → 真偽
Value builtin_ws_send(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_STRING) {
        return value_bool(false);
    }
    
    int ws_id = (int)argv[0].number;
    
    pthread_mutex_lock(&g_ws_mutex);
    WSConnection *conn = NULL;
    for (int i = 0; i < MAX_WS_CONNECTIONS; i++) {
        if (g_ws_connections[i].used && g_ws_connections[i].id == ws_id) {
            conn = &g_ws_connections[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_ws_mutex);
    
    if (conn == NULL || !conn->connected) return value_bool(false);
    
    pthread_mutex_lock(&conn->mutex);
    bool result = ws_send_frame(conn->sockfd, argv[1].string.data, argv[1].string.length);
    pthread_mutex_unlock(&conn->mutex);
    
    return value_bool(result);
}

// WS受信(接続ID, タイムアウト秒=5) → メッセージ文字列
Value builtin_ws_receive(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int ws_id = (int)argv[0].number;
    double timeout = 5.0;
    if (argc > 1 && argv[1].type == VALUE_NUMBER) {
        timeout = argv[1].number;
    }
    
    pthread_mutex_lock(&g_ws_mutex);
    WSConnection *conn = NULL;
    for (int i = 0; i < MAX_WS_CONNECTIONS; i++) {
        if (g_ws_connections[i].used && g_ws_connections[i].id == ws_id) {
            conn = &g_ws_connections[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_ws_mutex);
    
    if (conn == NULL || !conn->connected) return value_null();
    
    char buffer[65536];
    pthread_mutex_lock(&conn->mutex);
    int len = ws_recv_frame(conn->sockfd, buffer, sizeof(buffer), timeout);
    pthread_mutex_unlock(&conn->mutex);
    
    if (len < 0) {
        if (len == -2) {
            // Close frame received
            conn->connected = false;
        }
        return value_null();
    }
    
    return value_string(buffer);
}

// WS切断(接続ID)
Value builtin_ws_close(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int ws_id = (int)argv[0].number;
    
    pthread_mutex_lock(&g_ws_mutex);
    for (int i = 0; i < MAX_WS_CONNECTIONS; i++) {
        if (g_ws_connections[i].used && g_ws_connections[i].id == ws_id) {
            WSConnection *conn = &g_ws_connections[i];
            if (conn->connected) {
                // Close frame送信
                unsigned char close_frame[] = {0x88, 0x80, 0x00, 0x00, 0x00, 0x00};
                send(conn->sockfd, close_frame, sizeof(close_frame), 0);
                close(conn->sockfd);
                conn->connected = false;
            }
            pthread_mutex_destroy(&conn->mutex);
            conn->used = false;
            break;
        }
    }
    pthread_mutex_unlock(&g_ws_mutex);
    
    return value_null();
}

// WS状態(接続ID) → "接続中"/"切断"
Value builtin_ws_status(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_string("不明");
    
    int ws_id = (int)argv[0].number;
    
    pthread_mutex_lock(&g_ws_mutex);
    for (int i = 0; i < MAX_WS_CONNECTIONS; i++) {
        if (g_ws_connections[i].used && g_ws_connections[i].id == ws_id) {
            bool connected = g_ws_connections[i].connected;
            pthread_mutex_unlock(&g_ws_mutex);
            return value_string(connected ? "接続中" : "切断");
        }
    }
    pthread_mutex_unlock(&g_ws_mutex);
    
    return value_string("不明");
}
