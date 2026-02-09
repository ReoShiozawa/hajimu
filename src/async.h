/**
 * 日本語プログラミング言語 - 非同期・並列・スケジューラモジュール ヘッダー
 * 
 * pthread ベースの非同期処理、並列実行、スケジューラ機能
 * v1.2: スレッドプール、条件変数待機、Promise チェーン、
 *       rwlock、セマフォ、アトミックカウンター、チャネル select 等を追加
 */

#ifndef ASYNC_H
#define ASYNC_H

#include "value.h"
#include <pthread.h>
#include <stdbool.h>

// =============================================================================
// 定数
// =============================================================================

#define MAX_ASYNC_TASKS 4096
#define MAX_SCHEDULED_TASKS 256
#define MAX_CHANNELS 256

// スレッドプール設定
#define THREAD_POOL_DEFAULT_SIZE 8
#define THREAD_POOL_MAX_SIZE 64
#define THREAD_POOL_QUEUE_SIZE 8192

// 同期プリミティブ上限
#define MAX_USER_MUTEXES 256
#define MAX_USER_RWLOCKS 128
#define MAX_USER_SEMAPHORES 128
#define MAX_ATOMIC_COUNTERS 256

// =============================================================================
// 非同期タスク
// =============================================================================

typedef enum {
    TASK_PENDING,       // 待機中
    TASK_RUNNING,       // 実行中
    TASK_COMPLETED,     // 完了
    TASK_FAILED         // 失敗
} TaskStatus;

typedef struct {
    int id;                     // タスクID
    TaskStatus status;          // ステータス
    pthread_t thread;           // スレッド（スレッドプール不使用時）
    Value result;               // 結果値
    Value function;             // 実行する関数
    Value *args;                // 引数
    int arg_count;              // 引数数
    char error_message[256];    // エラーメッセージ
    bool used;                  // 使用中フラグ
    bool use_pool;              // スレッドプール使用フラグ
    
    // 条件変数待機（ポーリングの代わり）
    pthread_mutex_t completion_mutex;
    pthread_cond_t  completion_cond;
    bool            completion_signaled;

    // Promiseチェーン
    Value then_fn;              // 成功時コールバック
    Value catch_fn;             // 失敗時コールバック
    int   chain_next_id;        // チェーン先タスクID（-1 = なし）
} AsyncTask;

// =============================================================================
// スレッドプール
// =============================================================================

typedef struct {
    int task_id;                // 実行するタスクID
} PoolJob;

typedef struct {
    pthread_t *threads;         // ワーカースレッド配列
    int thread_count;           // ワーカー数
    
    PoolJob *queue;             // ジョブキュー（リングバッファ）
    int queue_capacity;
    int queue_head;
    int queue_tail;
    int queue_count;
    
    pthread_mutex_t queue_mutex;
    pthread_cond_t  queue_not_empty;
    pthread_cond_t  queue_not_full;
    
    bool shutdown;              // シャットダウンフラグ
    bool initialized;
    
    // 統計
    long long total_jobs;
    long long completed_jobs;
} ThreadPool;

// =============================================================================
// チャネル（スレッド間通信）
// =============================================================================

typedef struct {
    int id;                     // チャネルID
    Value *buffer;              // メッセージバッファ
    int capacity;               // バッファ容量
    int count;                  // 現在のメッセージ数
    int head;                   // 読み出し位置
    int tail;                   // 書き込み位置
    pthread_mutex_t mutex;      // ミューテックス
    pthread_cond_t not_empty;   // バッファ空でない条件
    pthread_cond_t not_full;    // バッファ満杯でない条件
    bool closed;                // クローズ済みフラグ
    bool used;                  // 使用中フラグ
} Channel;

// =============================================================================
// スケジュールタスク
// =============================================================================

typedef struct {
    int id;                     // タスクID
    Value function;             // 実行する関数
    double interval_sec;        // 実行間隔（秒）
    double delay_sec;           // 初期遅延（秒）
    bool repeat;                // 繰り返しフラグ
    bool active;                // アクティブフラグ
    bool used;                  // 使用中フラグ
    pthread_t thread;           // スレッド
} ScheduledTask;

// =============================================================================
// 同期プリミティブ（読み書きロック・セマフォ・アトミックカウンター）
// =============================================================================

typedef struct {
    pthread_rwlock_t lock;
    bool used;
} UserRWLock;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int max_count;
    bool used;
} UserSemaphore;

typedef struct {
    long long value;
    pthread_mutex_t mutex;
    bool used;
} AtomicCounter;

// =============================================================================
// 非同期ランタイム（グローバル状態）
// =============================================================================

typedef struct {
    // 非同期タスク管理
    AsyncTask tasks[MAX_ASYNC_TASKS];
    int next_task_id;
    pthread_mutex_t task_mutex;
    
    // スレッドプール
    ThreadPool pool;
    
    // チャネル管理
    Channel channels[MAX_CHANNELS];
    int next_channel_id;
    pthread_mutex_t channel_mutex;
    
    // スケジュール管理
    ScheduledTask scheduled[MAX_SCHEDULED_TASKS];
    int next_schedule_id;
    pthread_mutex_t schedule_mutex;
    
    // ミューテックス管理（ユーザー向け排他制御）
    pthread_mutex_t user_mutexes[MAX_USER_MUTEXES];
    bool user_mutex_used[MAX_USER_MUTEXES];
    int next_mutex_id;
    pthread_mutex_t mutex_mgr_mutex;
    
    // 読み書きロック管理
    UserRWLock rwlocks[MAX_USER_RWLOCKS];
    int next_rwlock_id;
    pthread_mutex_t rwlock_mgr_mutex;
    
    // セマフォ管理
    UserSemaphore semaphores[MAX_USER_SEMAPHORES];
    int next_semaphore_id;
    pthread_mutex_t semaphore_mgr_mutex;
    
    // アトミックカウンター管理
    AtomicCounter atomics[MAX_ATOMIC_COUNTERS];
    int next_atomic_id;
    pthread_mutex_t atomic_mgr_mutex;
    
    bool initialized;
} AsyncRuntime;

// =============================================================================
// 初期化・解放
// =============================================================================

/**
 * 非同期ランタイムを初期化
 */
void async_runtime_init(void);

/**
 * 非同期ランタイムを解放（全タスクを停止）
 */
void async_runtime_cleanup(void);

// =============================================================================
// スレッドプール
// =============================================================================

/** プール作成(ワーカー数) → 真偽 */
Value builtin_pool_create(int argc, Value *argv);

/** プール情報() → 辞書 {ワーカー数, キュー待ち, 完了数, 総数} */
Value builtin_pool_stats(int argc, Value *argv);

// =============================================================================
// 組み込み関数（非同期処理）
// =============================================================================

/** 非同期実行(関数) → タスクID */
Value builtin_async_run(int argc, Value *argv);

/** 待機(タスクID, タイムアウト秒=-1) → 結果値 */
Value builtin_async_await(int argc, Value *argv);

/** 待機全(タスクID配列) → 結果配列 */
Value builtin_async_await_all(int argc, Value *argv);

/** タスク状態(タスクID) → "待機中"/"実行中"/"完了"/"失敗" */
Value builtin_task_status(int argc, Value *argv);

/** 競争待機(タスクID配列) → 辞書{番号, 結果} 最初に完了したタスクの結果を返す */
Value builtin_async_race(int argc, Value *argv);

/** タスクキャンセル(タスクID) → 真偽 */
Value builtin_task_cancel(int argc, Value *argv);

// =============================================================================
// 組み込み関数（Promise チェーン）
// =============================================================================

/** 成功時(タスクID, 関数) → 新タスクID */
Value builtin_then(int argc, Value *argv);

/** 失敗時(タスクID, 関数) → 新タスクID */
Value builtin_catch(int argc, Value *argv);

// =============================================================================
// 組み込み関数（並列処理）
// =============================================================================

/** 並列実行(関数配列) → 結果配列（全タスク完了まで待機） */
Value builtin_parallel_run(int argc, Value *argv);

/** 並列マップ(配列, 関数) → 結果配列 */
Value builtin_parallel_map(int argc, Value *argv);

/** 排他作成() → ミューテックスID */
Value builtin_mutex_create(int argc, Value *argv);

/** 排他実行(ミューテックスID, 関数) → 結果 */
Value builtin_mutex_exec(int argc, Value *argv);

// =============================================================================
// 組み込み関数（読み書きロック）
// =============================================================================

/** 読書ロック作成() → ロックID */
Value builtin_rwlock_create(int argc, Value *argv);

/** 読取実行(ロックID, 関数) → 結果 */
Value builtin_rwlock_read(int argc, Value *argv);

/** 書込実行(ロックID, 関数) → 結果 */
Value builtin_rwlock_write(int argc, Value *argv);

// =============================================================================
// 組み込み関数（セマフォ）
// =============================================================================

/** セマフォ作成(上限数) → セマフォID */
Value builtin_semaphore_create(int argc, Value *argv);

/** セマフォ獲得(セマフォID) → 真偽 */
Value builtin_semaphore_acquire(int argc, Value *argv);

/** セマフォ解放(セマフォID) → 真偽 */
Value builtin_semaphore_release(int argc, Value *argv);

/** セマフォ実行(セマフォID, 関数) → 結果（自動獲得・解放） */
Value builtin_semaphore_exec(int argc, Value *argv);

// =============================================================================
// 組み込み関数（アトミックカウンター）
// =============================================================================

/** カウンター作成(初期値=0) → カウンターID */
Value builtin_atomic_create(int argc, Value *argv);

/** カウンター加算(カウンターID, 加算値=1) → 加算後の値 */
Value builtin_atomic_add(int argc, Value *argv);

/** カウンター取得(カウンターID) → 値 */
Value builtin_atomic_get(int argc, Value *argv);

/** カウンター設定(カウンターID, 値) → 古い値 */
Value builtin_atomic_set(int argc, Value *argv);

// =============================================================================
// 組み込み関数（チャネル）
// =============================================================================

/** チャネル作成(容量=1) → チャネルID */
Value builtin_channel_create(int argc, Value *argv);

/** チャネル送信(チャネルID, 値) → 真偽 */
Value builtin_channel_send(int argc, Value *argv);

/** チャネル受信(チャネルID) → 値 */
Value builtin_channel_receive(int argc, Value *argv);

/** チャネル閉じる(チャネルID) */
Value builtin_channel_close(int argc, Value *argv);

/** チャネル試送信(チャネルID, 値) → 真偽（非ブロッキング） */
Value builtin_channel_try_send(int argc, Value *argv);

/** チャネル試受信(チャネルID) → 辞書{成功, 値}（非ブロッキング） */
Value builtin_channel_try_receive(int argc, Value *argv);

/** チャネル残量(チャネルID) → 数値 */
Value builtin_channel_count(int argc, Value *argv);

/** チャネル選択(チャネルID配列, タイムアウト秒=-1) → 辞書{番号, 値} */
Value builtin_channel_select(int argc, Value *argv);

// =============================================================================
// 組み込み関数（スケジューラ）
// =============================================================================

/** 定期実行(関数, 間隔秒) → スケジュールID */
Value builtin_schedule_interval(int argc, Value *argv);

/** 遅延実行(関数, 遅延秒) → スケジュールID */
Value builtin_schedule_delay(int argc, Value *argv);

/** スケジュール停止(スケジュールID) */
Value builtin_schedule_stop(int argc, Value *argv);

/** 全スケジュール停止() */
Value builtin_schedule_stop_all(int argc, Value *argv);

// =============================================================================
// 組み込み関数（WebSocket）
// =============================================================================

/** WS接続(URL) → 接続ID */
Value builtin_ws_connect(int argc, Value *argv);

/** WS送信(接続ID, メッセージ) → 真偽 */
Value builtin_ws_send(int argc, Value *argv);

/** WS受信(接続ID, タイムアウト秒=5) → メッセージ文字列 */
Value builtin_ws_receive(int argc, Value *argv);

/** WS切断(接続ID) */
Value builtin_ws_close(int argc, Value *argv);

/** WS状態(接続ID) → "接続中"/"切断" */
Value builtin_ws_status(int argc, Value *argv);

#endif // ASYNC_H
