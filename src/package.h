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
#define PACKAGE_LOCK_FILE     "hajimu.lock"
#define PACKAGE_LOCAL_DIR     "hajimu_packages"
#define PACKAGE_GLOBAL_DIR    ".hajimu/packages"
#define PACKAGE_MAX_DEPS      64
#define PACKAGE_MAX_NAME      256
#define PACKAGE_MAX_PATH      1024
#define PACKAGE_MAX_VERSION   96
#define PACKAGE_MAX_REQUIREMENT 192
#define PACKAGE_SHA256_HEX    65
#define PACKAGE_MANIFEST_SCHEMA_VERSION 2

// =============================================================================
// パッケージ依存情報
// =============================================================================

typedef struct {
    char name[PACKAGE_MAX_NAME];      // パッケージ名
    char source[PACKAGE_MAX_PATH];    // GitリポジトリURLまたはローカルパス
    char requirement[PACKAGE_MAX_REQUIREMENT]; // SemVer制約（例: ^1.2.0, >=1.0.0）
    char checksum[PACKAGE_SHA256_HEX]; // 配布バイナリのSHA-256（省略可）
} PackageDep;

// =============================================================================
// パッケージマニフェスト
// =============================================================================

typedef struct {
    int schema_version;               // マニフェスト仕様バージョン
    char name[PACKAGE_MAX_NAME];      // パッケージ名
    char version[PACKAGE_MAX_VERSION]; // バージョン
    char description[512];            // 説明
    char author[PACKAGE_MAX_NAME];    // 作者
    char license[64];                 // SPDXライセンス識別子
    char repository[PACKAGE_MAX_PATH]; // ソースリポジトリ
    char main_file[PACKAGE_MAX_NAME]; // エントリポイント（デフォルト: main.jp）
    char build_cmd[PACKAGE_MAX_PATH]; // ビルドコマンド（例: "make"）
    char release_url[PACKAGE_MAX_PATH]; // ビルド済み .hjp の配布URL（省略可）
    char release_checksum[PACKAGE_SHA256_HEX]; // release_urlのSHA-256

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

/** lockfileの固定を更新し、依存を安全に入れ替える。nameがNULLなら全依存を更新する。 */
int package_update(const char *name);

/** マニフェストを検証し、問題があれば診断を表示する。 */
int package_validate(const char *path);

/** 公式レジストリからパッケージを検索する。 */
int package_search(const char *query);

/** インストール状態とlockfileの整合性を検査する。 */
int package_audit(void);

/** 外部パッケージのビルドスクリプト実行を明示的に許可する。 */
void package_set_allow_scripts(bool allow);

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
