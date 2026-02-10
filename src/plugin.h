/**
 * はじむ - C拡張プラグインシステム
 * 
 * 統一拡張子 .hjp（Hajimu Plugin）によるクロスプラットフォーム
 * ネイティブプラグインの読み込み・管理。
 * 
 * .hjp は内部的には共有ライブラリ（macOS: dylib, Linux: so, Windows: dll）
 * と同一だが、拡張子を統一することでユーザーはOSの違いを意識する必要がない。
 * 
 * 使い方:
 *   取り込む "math_plugin"              // 拡張子不要！自動で .hjp を検索
 *   取り込む "math_plugin" として 数学    // 名前空間付きも可
 *   取り込む "math_plugin.hjp"          // 明示的に拡張子を指定してもOK
 */

#ifndef PLUGIN_H
#define PLUGIN_H

#include "value.h"
#include <stdbool.h>

// 前方宣言（hajimu_plugin.h で定義）
typedef struct {
    Value (*call)(Value *func, int argc, Value *argv);
} HajimuRuntime;

// =============================================================================
// 統一プラグイン拡張子
// =============================================================================

#define HJP_EXTENSION       ".hjp"
#define HJP_EXTENSION_LEN   4

// =============================================================================
// プラグインAPI（プラグイン開発者が使う公開インターフェース）
// =============================================================================

/**
 * プラグイン関数の型
 */
typedef Value (*HajimuPluginFn)(int argc, Value *argv);

/**
 * 関数登録用の構造体
 */
typedef struct {
    const char *name;       // はじむ側に公開する関数名（日本語OK）
    HajimuPluginFn fn;      // 関数ポインタ
    int min_args;           // 最小引数数
    int max_args;           // 最大引数数（-1で可変長）
} HajimuPluginFunc;

/**
 * プラグイン情報構造体
 */
typedef struct {
    const char *name;           // プラグイン名
    const char *version;        // バージョン
    const char *author;         // 作者
    const char *description;    // 説明
    HajimuPluginFunc *functions;// 関数テーブル
    int function_count;         // 関数の数
} HajimuPluginInfo;

/**
 * プラグイン初期化関数の型
 */
typedef HajimuPluginInfo *(*HajimuPluginInitFn)(void);

// プラグイン初期化関数のシンボル名
#define HAJIMU_PLUGIN_INIT_SYMBOL "hajimu_plugin_init"

// ランタイム設定関数のシンボル名
#define HAJIMU_PLUGIN_SET_RUNTIME_SYMBOL "hajimu_plugin_set_runtime"
typedef void (*HajimuPluginSetRuntimeFn)(HajimuRuntime *);

// =============================================================================
// プラグインマネージャ（内部用）
// =============================================================================

typedef struct {
    char *path;             // ファイルパス
    char *name;             // プラグイン名
    void *handle;           // dlopen / LoadLibrary ハンドル
    HajimuPluginInfo *info; // プラグイン情報
} LoadedPlugin;

typedef struct {
    LoadedPlugin *plugins;
    int count;
    int capacity;
} PluginManager;

// =============================================================================
// API
// =============================================================================

void plugin_manager_init(PluginManager *mgr);
void plugin_manager_free(PluginManager *mgr);

/**
 * .hjp ファイルをプラグインとして読み込む
 */
bool plugin_load(PluginManager *mgr, const char *path, HajimuPluginInfo **info_out);

/**
 * パスが .hjp プラグインかどうかを判定
 */
bool plugin_is_hjp(const char *path);

/**
 * プラグイン名から .hjp ファイルを検索して解決
 * 検索順序:
 *   1. 呼び出し元ファイルからの相対パス
 *   2. CWD基準
 *   3. hajimu_packages/ 内
 *   4. ~/.hajimu/plugins/ （グローバルプラグインディレクトリ）
 * 
 * @param name     プラグイン名（拡張子なしでOK）
 * @param caller   呼び出し元ファイルのパス（NULL可）
 * @param out      解決されたパスを格納するバッファ
 * @param out_size バッファサイズ
 * @return 見つかった場合 true
 */
bool plugin_resolve_hjp(const char *name, const char *caller,
                        char *out, int out_size);

/**
 * プラグインを名前で検索
 */
LoadedPlugin *plugin_find(PluginManager *mgr, const char *name);

/**
 * ロード済みプラグインにランタイムコールバックを注入
 */
void plugin_set_runtime(LoadedPlugin *plugin, HajimuRuntime *rt);

#endif // PLUGIN_H
