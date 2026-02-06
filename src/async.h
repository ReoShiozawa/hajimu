/**
 * 日本語プログラミング言語 - 非同期・並列・スケジューラモジュール ヘッダー
 * 
 * pthread ベースの非同期処理、並列実行、スケジューラ機能
 */

#ifndef ASYNC_H
#define ASYNC_H

#include "value.h"
#include <pthread.h>
#include <stdbool.h>

// =============================================================================
// 定数
// =============================================================================

#define MAX_ASYNC_TASKS 256
#define MAX_SCHEDULED_TASKS 64
#define MAX_CHANNELS 64

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
    pthread_t thread;           // スレッド
    Value result;               // 結果値
    Value function;             // 実行する関数
    Value *args;                // 引数
    int arg_count;              // 引数数
    char error_message[256];    // エラーメッセージ
    bool used;                  // 使用中フラグ
} AsyncTask;

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
// 非同期ランタイム（グローバル状態）
// =============================================================================

typedef struct {
    // 非同期タスク管理
    AsyncTask tasks[MAX_ASYNC_TASKS];
    int next_task_id;
    pthread_mutex_t task_mutex;
    
    // チャネル管理
    Channel channels[MAX_CHANNELS];
    int next_channel_id;
    pthread_mutex_t channel_mutex;
    
    // スケジュール管理
    ScheduledTask scheduled[MAX_SCHEDULED_TASKS];
    int next_schedule_id;
    pthread_mutex_t schedule_mutex;
    
    // ミューテックス管理（ユーザー向け排他制御）
    pthread_mutex_t user_mutexes[64];
    bool user_mutex_used[64];
    int next_mutex_id;
    pthread_mutex_t mutex_mgr_mutex;
    
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
// 組み込み関数（非同期処理）
// =============================================================================

/** 非同期実行(関数) → タスクID */
Value builtin_async_run(int argc, Value *argv);

/** 待機(タスクID) → 結果値 */
Value builtin_async_await(int argc, Value *argv);

/** 待機全(タスクID配列) → 結果配列 */
Value builtin_async_await_all(int argc, Value *argv);

/** タスク状態(タスクID) → "待機中"/"実行中"/"完了"/"失敗" */
Value builtin_task_status(int argc, Value *argv);

// =============================================================================
// 組み込み関数（並列処理）
// =============================================================================

/** 並列実行(関数配列) → 結果配列（全タスク完了まで待機） */
Value builtin_parallel_run(int argc, Value *argv);

/** 排他作成() → ミューテックスID */
Value builtin_mutex_create(int argc, Value *argv);

/** 排他実行(ミューテックスID, 関数) → 結果 */
Value builtin_mutex_exec(int argc, Value *argv);

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
