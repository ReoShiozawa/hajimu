/**
 * はじむ - パッケージ管理システム
 * 
 * パッケージのインストール・削除・管理機能を提供
 * 
 * パッケージ構造:
 *   ~/.hajimu/packages/<パッケージ名>/
 *     hajimu.json       - パッケージマニフェスト
 *     *.jp              - ソースファイル
 *
 * プロジェクト構造:
 *   ./hajimu.json        - プロジェクトマニフェスト（依存パッケージ定義）
 *   ./hajimu_packages/   - ローカルにインストールされたパッケージ
 *
 * hajimu.json:
 *   {
 *     "名前": "パッケージ名",
 *     "バージョン": "1.0.0",
 *     "説明": "説明文",
 *     "作者": "作者名",
 *     "メイン": "main.jp",
 *     "依存": {
 *       "パッケージ名": "GitHubリポジトリURL"
 *     }
 *   }
 */

#ifndef PACKAGE_H
#define PACKAGE_H

#include <stdbool.h>

// =============================================================================
// 定数
// =============================================================================

#define PACKAGE_MANIFEST_FILE "hajimu.json"
#define PACKAGE_LOCAL_DIR     "hajimu_packages"
#define PACKAGE_GLOBAL_DIR    ".hajimu/packages"
#define PACKAGE_MAX_DEPS      64
#define PACKAGE_MAX_NAME      256
#define PACKAGE_MAX_PATH      1024

// =============================================================================
// パッケージ依存情報
// =============================================================================

typedef struct {
    char name[PACKAGE_MAX_NAME];      // パッケージ名
    char source[PACKAGE_MAX_PATH];    // GitHubリポジトリURL
} PackageDep;

// =============================================================================
// パッケージマニフェスト
// =============================================================================

typedef struct {
    char name[PACKAGE_MAX_NAME];      // パッケージ名
    char version[64];                 // バージョン
    char description[512];            // 説明
    char author[PACKAGE_MAX_NAME];    // 作者
    char main_file[PACKAGE_MAX_NAME]; // エントリポイント（デフォルト: main.jp）
    char build_cmd[PACKAGE_MAX_PATH];  // ビルドコマンド（例: "make"）
    
    PackageDep deps[PACKAGE_MAX_DEPS]; // 依存パッケージ
    int dep_count;                     // 依存数
} PackageManifest;

// =============================================================================
// パッケージ管理コマンド
// =============================================================================

/**
 * プロジェクトを初期化 (hajimu.json を作成)
 * @return 成功なら0、失敗なら1
 */
int package_init(void);

/**
 * パッケージをインストール
 * @param name_or_url パッケージ名またはGitHubリポジトリURL
 * @return 成功なら0、失敗なら1
 */
int package_install(const char *name_or_url);

/**
 * hajimu.json の全依存パッケージをインストール
 * @return 成功なら0、失敗なら1
 */
int package_install_all(void);

/**
 * パッケージを削除
 * @param name パッケージ名
 * @return 成功なら0、失敗なら1
 */
int package_remove(const char *name);

/**
 * インストール済みパッケージ一覧を表示
 * @return 成功なら0、失敗なら1
 */
int package_list(void);

// =============================================================================
// パッケージパス解決（evaluatorから利用）
// =============================================================================

/**
 * パッケージ名からエントリポイントファイルのパスを解決
 * @param package_name パッケージ名
 * @param caller_file 呼び出し元ファイルのパス（相対パス解決用）
 * @param resolved_path 結果パスを格納するバッファ
 * @param max_len バッファの最大長
 * @return パッケージが見つかればtrue
 */
bool package_resolve(const char *package_name, const char *caller_file,
                     char *resolved_path, int max_len);

/**
 * hajimu.json を読み込み、マニフェストを解析
 * @param path hajimu.json のパス
 * @param manifest 結果を格納するマニフェスト
 * @return 成功ならtrue
 */
bool package_read_manifest(const char *path, PackageManifest *manifest);

#endif // PACKAGE_H
