/**
 * 日本語プログラミング言語 - 非同期・並列・スケジューラモジュール実装
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
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

// WebSocket用
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>

// SSL/TLS用（wssサポート）
#ifdef __APPLE__
#include <Security/Security.h>
#include <Security/SecureTransport.h>
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
// 初期化・解放
// =============================================================================

void async_runtime_init(void) {
    if (g_runtime.initialized) return;
    
    memset(&g_runtime, 0, sizeof(AsyncRuntime));
    
    pthread_mutex_init(&g_runtime.task_mutex, NULL);
    pthread_mutex_init(&g_runtime.channel_mutex, NULL);
    pthread_mutex_init(&g_runtime.schedule_mutex, NULL);
    pthread_mutex_init(&g_runtime.mutex_mgr_mutex, NULL);
    
    g_runtime.next_task_id = 1;
    g_runtime.next_channel_id = 1;
    g_runtime.next_schedule_id = 1;
    g_runtime.next_mutex_id = 0;
    
    // WebSocket接続の初期化
    memset(g_ws_connections, 0, sizeof(g_ws_connections));
    
    g_runtime.initialized = true;
}

void async_runtime_cleanup(void) {
    if (!g_runtime.initialized) return;
    
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
    for (int i = 0; i < 64; i++) {
        if (g_runtime.user_mutex_used[i]) {
            pthread_mutex_destroy(&g_runtime.user_mutexes[i]);
        }
    }
    
    pthread_mutex_destroy(&g_runtime.task_mutex);
    pthread_mutex_destroy(&g_runtime.channel_mutex);
    pthread_mutex_destroy(&g_runtime.schedule_mutex);
    pthread_mutex_destroy(&g_runtime.mutex_mgr_mutex);
    
    g_runtime.initialized = false;
}

// =============================================================================
// 非同期タスク - 内部ヘルパー
// =============================================================================

// スレッドで実行される関数
static void *async_task_runner(void *arg) {
    AsyncTask *task = (AsyncTask *)arg;
    
    Evaluator *eval = get_async_evaluator();
    if (eval == NULL) {
        task->status = TASK_FAILED;
        snprintf(task->error_message, sizeof(task->error_message), "評価器が利用できません");
        return NULL;
    }
    
    task->status = TASK_RUNNING;
    
    // 関数を呼び出す
    // 注意: 評価器はスレッドセーフではないため、シンプルな組み込み関数のみ直接呼べる
    // ユーザー定義関数は専用の評価器コピーが必要
    
    if (task->function.type == VALUE_BUILTIN) {
        task->result = task->function.builtin.fn(task->arg_count, task->args);
        task->status = TASK_COMPLETED;
    } else if (task->function.type == VALUE_FUNCTION) {
        // ユーザー定義関数: 新しい評価器を作成して安全に実行
        Evaluator *thread_eval = evaluator_new();
        
        // グローバル環境から組み込み関数を引き継ぐ（既にregister_builtinsで済み）
        
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
        
        // 引数をバインド
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
        
        env_free(local);
        evaluator_free(thread_eval);
    } else {
        task->status = TASK_FAILED;
        snprintf(task->error_message, sizeof(task->error_message), "呼び出し可能ではありません");
    }
    
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
        return value_number(-1);  // スロット不足
    }
    
    AsyncTask *task = &g_runtime.tasks[slot];
    memset(task, 0, sizeof(AsyncTask));
    task->id = g_runtime.next_task_id++;
    task->status = TASK_PENDING;
    task->used = true;
    task->function = value_copy(argv[0]);
    task->result = value_null();
    
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
    
    // スレッドを作成
    int ret = pthread_create(&task->thread, NULL, async_task_runner, task);
    if (ret != 0) {
        task->used = false;
        if (task->args) free(task->args);
        pthread_mutex_unlock(&g_runtime.task_mutex);
        return value_number(-1);
    }
    
    // スレッドをデタッチ（待機はjoinで行う代わりにポーリング）
    pthread_detach(task->thread);
    
    int task_id = task->id;
    pthread_mutex_unlock(&g_runtime.task_mutex);
    
    return value_number(task_id);
}

// 待機(タスクID) → 結果値
Value builtin_async_await(int argc, Value *argv) {
    if (argc < 1 || argv[0].type != VALUE_NUMBER) return value_null();
    
    int task_id = (int)argv[0].number;
    
    // タスクを見つける
    AsyncTask *task = NULL;
    pthread_mutex_lock(&g_runtime.task_mutex);
    for (int i = 0; i < MAX_ASYNC_TASKS; i++) {
        if (g_runtime.tasks[i].used && g_runtime.tasks[i].id == task_id) {
            task = &g_runtime.tasks[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_runtime.task_mutex);
    
    if (task == NULL) return value_null();
    
    // 完了するまで待機（ポーリング）
    while (task->status == TASK_PENDING || task->status == TASK_RUNNING) {
        usleep(1000);  // 1ms
    }
    
    Value result = value_copy(task->result);
    
    // タスクをクリーンアップ
    pthread_mutex_lock(&g_runtime.task_mutex);
    if (task->args) {
        for (int i = 0; i < task->arg_count; i++) {
            value_free(&task->args[i]);
        }
        free(task->args);
    }
    value_free(&task->result);
    value_free(&task->function);
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

// 排他作成() → ミューテックスID
Value builtin_mutex_create(int argc, Value *argv) {
    (void)argc;
    (void)argv;
    
    if (!g_runtime.initialized) async_runtime_init();
    
    pthread_mutex_lock(&g_runtime.mutex_mgr_mutex);
    
    int slot = -1;
    for (int i = 0; i < 64; i++) {
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
    if (mutex_id < 0 || mutex_id >= 64 || !g_runtime.user_mutex_used[mutex_id]) {
        return value_null();
    }
    
    pthread_mutex_lock(&g_runtime.user_mutexes[mutex_id]);
    
    Value result;
    if (argv[1].type == VALUE_BUILTIN) {
        result = argv[1].builtin.fn(0, NULL);
    } else {
        // ユーザー定義関数を呼ぶ
        Value no_args = value_null();
        result = builtin_async_run(1, &argv[1]);
        result = builtin_async_await(1, &result);
        (void)no_args;
    }
    
    pthread_mutex_unlock(&g_runtime.user_mutexes[mutex_id]);
    
    return result;
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
        if (capacity > 1024) capacity = 1024;
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

// チャネル送信(チャネルID, 値) → 真偽
Value builtin_channel_send(int argc, Value *argv) {
    if (argc < 2 || argv[0].type != VALUE_NUMBER) return value_bool(false);
    
    int ch_id = (int)argv[0].number;
    
    Channel *ch = NULL;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (g_runtime.channels[i].used && g_runtime.channels[i].id == ch_id) {
            ch = &g_runtime.channels[i];
            break;
        }
    }
    
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
    
    Channel *ch = NULL;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (g_runtime.channels[i].used && g_runtime.channels[i].id == ch_id) {
            ch = &g_runtime.channels[i];
            break;
        }
    }
    
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
    
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (g_runtime.channels[i].used && g_runtime.channels[i].id == ch_id) {
            Channel *ch = &g_runtime.channels[i];
            pthread_mutex_lock(&ch->mutex);
            ch->closed = true;
            pthread_cond_broadcast(&ch->not_empty);
            pthread_cond_broadcast(&ch->not_full);
            pthread_mutex_unlock(&ch->mutex);
            break;
        }
    }
    
    return value_null();
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
            env_free(local);
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
    unsigned char frame[10 + len + 4];  // ヘッダー + データ + マスク
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
        if (n <= 0) return false;
        sent += n;
    }
    
    return true;
}

// WebSocketフレーム受信
static int ws_recv_frame(int sockfd, char *buffer, int buf_size, double timeout_sec) {
    // タイムアウト設定
    struct timeval tv;
    tv.tv_sec = (long)timeout_sec;
    tv.tv_usec = (long)((timeout_sec - (long)timeout_sec) * 1000000);
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
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
