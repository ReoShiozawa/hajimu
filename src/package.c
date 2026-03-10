/**
 * はじむ - パッケージ管理システム実装
 * 
 * GitHubリポジトリからパッケージをインストール・管理する
 * libcurl を使用してダウンロード
 */

#include "package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
/* TokenType collision guard: winnt.h defines TokenType as enum value */
#  define TokenType  _winnt_TokenType_collision_guard_
#  include <windows.h>   /* GetModuleFileNameA, FindFirstFile等 */
#  undef TokenType
#  include <direct.h>    /* _mkdir, _rmdir */
#  define mkdir(p,m)  _mkdir(p)   /* Windows mkdir はモード引数なし */
#  ifndef rmdir
#    define rmdir(p)  _rmdir(p)   /* 安全のため明示定義 */
#  endif
/* pclose() の戒り値は終了コードが上位バイトに格納されない (直接終了コード) */
#  ifndef WEXITSTATUS
#    define WEXITSTATUS(s)  (s)
#  endif
/* POSIX unlink エイリアス */
#  include <io.h>   /* _unlink → unlink, _access など */
#  ifndef unlink
#    define unlink(p)  _unlink(p)
#  endif
/* Windows 用 DIR エミュレーション (win_compat2.h と同等) */
#  include "win_compat2.h"
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <unistd.h>
#  include <dirent.h>
#else
#  include <unistd.h>
#  include <dirent.h>
#endif

// =============================================================================
// ヘルパー関数
// =============================================================================

/**
 * ディレクトリを再帰的に作成
 */
static int mkdirs(const char *path) {
    char tmp[PACKAGE_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

/**
 * ディレクトリが存在するかチェック
 */
static bool dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * ファイルが存在するかチェック
 */
static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * ディレクトリを再帰的に走査し .hjp ファイルを検索（最大 3 階層）
 */
static bool find_hjp_recursive(const char *dir, char *out, int out_size, int depth) {
    if (depth > 3) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    /* まずルートの .hjp を探す */
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        size_t nlen = strlen(ent->d_name);
        if (nlen > 4 && strcmp(ent->d_name + nlen - 4, ".hjp") == 0) {
            snprintf(out, out_size, "%s/%s", dir, ent->d_name);
            closedir(d);
            return true;
        }
    }
    closedir(d);
    /* 次にサブディレクトリを再帰探索 */
    d = opendir(dir);
    if (!d) return false;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char subpath[PACKAGE_MAX_PATH];
        snprintf(subpath, sizeof(subpath), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(subpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (find_hjp_recursive(subpath, out, out_size, depth + 1)) {
                closedir(d);
                return true;
            }
        }
    }
    closedir(d);
    return false;
}

/**
 * ディレクトリを再帰的に削除
 */
static int remove_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return -1;
    
    struct dirent *entry;
    char filepath[PACKAGE_MAX_PATH];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_directory(filepath);
            } else {
                unlink(filepath);
            }
        }
    }
    closedir(dir);
    return rmdir(path);
}

/**
 * ホームディレクトリを取得
 */
static const char *get_home_dir(void) {
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOMEPATH");
    if (!home) home = "C:\\Users\\Public";
    return home;
#else
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    return home;
#endif
}

/**
 * グローバルパッケージディレクトリのパスを構築
 */
static void get_global_package_dir(char *buf, int max_len) {
    snprintf(buf, max_len, "%s/%s", get_home_dir(), PACKAGE_GLOBAL_DIR);
}

/**
 * パッケージのインストール先パスを構築
 */
static void get_package_path(const char *name, bool is_local, char *buf, int max_len) {
    if (is_local) {
        snprintf(buf, max_len, "%s/%s", PACKAGE_LOCAL_DIR, name);
    } else {
        char global_dir[PACKAGE_MAX_PATH];
        get_global_package_dir(global_dir, sizeof(global_dir));
        snprintf(buf, max_len, "%s/%s", global_dir, name);
    }
}

// =============================================================================
// 簡易JSONパーサ（hajimu.json用）
// =============================================================================

/**
 * JSONの空白をスキップ
 */
static const char *json_skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/**
 * JSON文字列を抽出（"..." をパース）
 */
static const char *json_parse_string(const char *p, char *out, int max_len) {
    if (*p != '"') return NULL;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < max_len - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                default: out[i++] = *p; break;
            }
        } else {
            // UTF-8マルチバイト対応
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/**
 * JSON文字列値の比較用キーチェック
 */
static bool json_key_equals(const char *key, const char *target) {
    return strcmp(key, target) == 0;
}

/**
 * hajimu.json を読み込み・パース
 */
bool package_read_manifest(const char *path, PackageManifest *manifest) {
    memset(manifest, 0, sizeof(PackageManifest));
    snprintf(manifest->main_file, sizeof(manifest->main_file), "main.jp");
    snprintf(manifest->version, sizeof(manifest->version), "0.0.0");
    
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    
    const char *p = json_skip_ws(buf);
    if (*p != '{') { free(buf); return false; }
    p++;
    
    while (*p && *p != '}') {
        p = json_skip_ws(p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }
        
        // キーを読む
        char key[256] = {0};
        p = json_parse_string(p, key, sizeof(key));
        if (!p) break;
        
        p = json_skip_ws(p);
        if (*p != ':') break;
        p++;
        p = json_skip_ws(p);
        
        if (json_key_equals(key, "名前") || json_key_equals(key, "name")) {
            p = json_parse_string(p, manifest->name, sizeof(manifest->name));
        } else if (json_key_equals(key, "バージョン") || json_key_equals(key, "version")) {
            p = json_parse_string(p, manifest->version, sizeof(manifest->version));
        } else if (json_key_equals(key, "説明") || json_key_equals(key, "description")) {
            p = json_parse_string(p, manifest->description, sizeof(manifest->description));
        } else if (json_key_equals(key, "作者") || json_key_equals(key, "author")) {
            p = json_parse_string(p, manifest->author, sizeof(manifest->author));
        } else if (json_key_equals(key, "メイン") || json_key_equals(key, "main")) {
            p = json_parse_string(p, manifest->main_file, sizeof(manifest->main_file));
        } else if (json_key_equals(key, "ビルド") || json_key_equals(key, "build")) {
            p = json_parse_string(p, manifest->build_cmd, sizeof(manifest->build_cmd));
        } else if (json_key_equals(key, "リリース") || json_key_equals(key, "release")) {
            p = json_parse_string(p, manifest->release_url, sizeof(manifest->release_url));
        } else if (json_key_equals(key, "依存") || json_key_equals(key, "dependencies")) {
            // 依存オブジェクトをパース
            if (*p != '{') break;
            p++;
            
            while (*p && *p != '}') {
                p = json_skip_ws(p);
                if (*p == '}') break;
                if (*p == ',') { p++; continue; }
                
                if (manifest->dep_count >= PACKAGE_MAX_DEPS) break;
                
                PackageDep *dep = &manifest->deps[manifest->dep_count];
                p = json_parse_string(p, dep->name, sizeof(dep->name));
                if (!p) break;
                
                p = json_skip_ws(p);
                if (*p != ':') break;
                p++;
                p = json_skip_ws(p);
                
                p = json_parse_string(p, dep->source, sizeof(dep->source));
                if (!p) break;
                
                manifest->dep_count++;
            }
            
            if (*p == '}') p++;
        } else {
            // 未知のキーはスキップ
            if (*p == '"') {
                char dummy[512];
                p = json_parse_string(p, dummy, sizeof(dummy));
            } else if (*p == '{') {
                // オブジェクトをスキップ（単純ネスト対応）
                int depth = 1;
                p++;
                while (*p && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
            } else if (*p == '[') {
                int depth = 1;
                p++;
                while (*p && depth > 0) {
                    if (*p == '[') depth++;
                    else if (*p == ']') depth--;
                    p++;
                }
            } else {
                // 数値・真偽値などをスキップ
                while (*p && *p != ',' && *p != '}') p++;
            }
        }
        
        if (!p) break;
    }
    
    free(buf);
    return manifest->name[0] != '\0';
}

/**
 * hajimu.json を書き出し
 */
static bool write_manifest(const char *path, const PackageManifest *manifest) {
    /* "wb": バイナリモードで書たることで Windows の CRLF 変換を防ぎ、
     * UTF-8 JSON として常に LF 改行で保存する。 */
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    
    fprintf(f, "{\n");
    fprintf(f, "  \"名前\": \"%s\",\n", manifest->name);
    fprintf(f, "  \"バージョン\": \"%s\",\n", manifest->version);
    fprintf(f, "  \"説明\": \"%s\",\n", manifest->description);
    fprintf(f, "  \"作者\": \"%s\",\n", manifest->author);
    fprintf(f, "  \"メイン\": \"%s\",\n", manifest->main_file);
    if (manifest->build_cmd[0]) {
        fprintf(f, "  \"ビルド\": \"%s\",\n", manifest->build_cmd);
    }
    fprintf(f, "  \"依存\": {");
    
    for (int i = 0; i < manifest->dep_count; i++) {
        if (i > 0) fprintf(f, ",");
        fprintf(f, "\n    \"%s\": \"%s\"", 
                manifest->deps[i].name, manifest->deps[i].source);
    }
    
    if (manifest->dep_count > 0) fprintf(f, "\n  ");
    fprintf(f, "}\n");
    fprintf(f, "}\n");
    
    fclose(f);
    return true;
}

// =============================================================================
// GitHubリポジトリ操作
// =============================================================================

/**
 * GitHubリポジトリURLからパッケージ名を抽出
 * "https://github.com/user/repo" → "repo"
 * "user/repo" → "repo" 
 */
static void extract_package_name(const char *url, char *name, int max_len) {
    // 末尾の .git を除去
    char clean_url[PACKAGE_MAX_PATH];
    snprintf(clean_url, sizeof(clean_url), "%s", url);
    char *git_ext = strstr(clean_url, ".git");
    if (git_ext && strlen(git_ext) == 4) {
        *git_ext = '\0';
    }
    
    // 最後の / 以降を取得
    const char *last_slash = strrchr(clean_url, '/');
    if (last_slash) {
        snprintf(name, max_len, "%.*s", max_len - 1, last_slash + 1);
    } else {
        snprintf(name, max_len, "%.*s", max_len - 1, clean_url);
    }
}

/**
 * 入力がリモートURL（GitHub等）かローカルパスか単純な名前かを判定
 * 0: パッケージ名のみ, 1: user/repo 形式またはURL, 2: ローカルパス
 */
static int classify_source(const char *str) {
    // http(s):// で始まる → URL
    if (strncmp(str, "https://", 8) == 0 || strncmp(str, "http://", 7) == 0) {
        return 1;
    }
    // 絶対パス or 相対パス or ~ → ローカル
    if (str[0] == '/' || str[0] == '.' || str[0] == '~') {
        return 2;
    }
    // github.com を含む → URL
    if (strstr(str, "github.com") != NULL) {
        return 1;
    }
    // user/repo 形式（スラッシュ1つだけ） → GitHub
    const char *slash = strchr(str, '/');
    if (slash && strchr(slash + 1, '/') == NULL && slash != str && *(slash + 1) != '\0') {
        return 1;
    }
    // パッケージ名のみ
    return 0;
}

/**
 * GitHub URLを正規化
 * "user/repo" → "https://github.com/user/repo.git"
 */
static void normalize_github_url(const char *input, char *url, int max_len) {
    if (strncmp(input, "https://", 8) == 0 || strncmp(input, "http://", 7) == 0) {
        // 既にフルURL
        if (strstr(input, ".git")) {
            snprintf(url, max_len, "%s", input);
        } else {
            snprintf(url, max_len, "%s.git", input);
        }
    } else if (input[0] == '/' || input[0] == '.' || input[0] == '~') {
        // ローカルパス → そのまま使用
        snprintf(url, max_len, "%s", input);
    } else if (strchr(input, '/') != NULL) {
        // user/repo 形式
        snprintf(url, max_len, "https://github.com/%s.git", input);
    } else {
        // パッケージ名のみ → 解決不可
        url[0] = '\0';
    }
}

/**
 * URL からファイルをダウンロードして dest_path に保存する
 * curl コマンドを使用（Windows 10+/macOS/Linux で利用可、MSYS2 usr/bin にも存在）
 * 成功なら true、失敗（404含む）なら false
 */
static bool download_to_file(const char *url, const char *dest_path) {
    char cmd[PACKAGE_MAX_PATH * 3];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd),
        "curl -fsSL --max-time 30 -o \"%s\" \"%s\" >NUL 2>&1",
        dest_path, url);
#else
    snprintf(cmd, sizeof(cmd),
        "curl -fsSL --max-time 30 -o \"%s\" \"%s\" >/dev/null 2>&1",
        dest_path, url);
#endif
    return system(cmd) == 0;
}

/**
 * リポジトリURLから GitHub ベースURL（.git なし）を生成
 * "https://github.com/user/repo.git" → "https://github.com/user/repo"
 */
static void repo_base_url(const char *repo_url, char *base, int max_len) {
    snprintf(base, max_len, "%s", repo_url);
    char *git_ext = strstr(base, ".git");
    if (git_ext && strlen(git_ext) == 4) *git_ext = '\0';
}

/**
 * git clone でパッケージをダウンロード
 */
static int git_clone(const char *url, const char *dest) {
    char cmd[PACKAGE_MAX_PATH * 2];

#ifdef _WIN32
    /* where コマンドで git の存在を事前確認 */
    if (system("where git >nul 2>&1") != 0) {
        fprintf(stderr, "エラー: git が見つかりません\n");
        fprintf(stderr, "  Git for Windows をインストールしてください。\n");
        fprintf(stderr, "  https://git-scm.com/download/win\n");
        return 1;
    }
#else
    /* Unix系: which で確認 */
    if (system("which git >/dev/null 2>&1") != 0) {
        fprintf(stderr, "エラー: git が見つかりません。git をインストールしてください。\n");
        return 1;
    }
#endif

    snprintf(cmd, sizeof(cmd), "git clone --depth 1 -q \"%s\" \"%s\" 2>&1", url, dest);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "エラー: git clone を実行できません\n");
        return 1;
    }

    char output[1024];
    bool any_output = false;
    while (fgets(output, sizeof(output), pipe)) {
        /* 全てのエラー出力を表示（git メッセージや CMD エラーも含む） */
        fprintf(stderr, "  %s", output);
        any_output = true;
    }
    if (any_output) fflush(stderr);

    int status = pclose(pipe);
    return WEXITSTATUS(status);
}

// =============================================================================
// パッケージ管理コマンド
// =============================================================================

/**
 * プロジェクトを初期化
 */
int package_init(void) {
    if (file_exists(PACKAGE_MANIFEST_FILE)) {
        printf("⚠  %s は既に存在します\n", PACKAGE_MANIFEST_FILE);
        return 1;
    }
    
    // カレントディレクトリ名をプロジェクト名にする
    char cwd[PACKAGE_MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "エラー: カレントディレクトリを取得できません\n");
        return 1;
    }
    
    char *dir_name = strrchr(cwd, '/');
    dir_name = dir_name ? dir_name + 1 : cwd;
    
    PackageManifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    snprintf(manifest.name, sizeof(manifest.name), "%.*s",
             (int)(sizeof(manifest.name) - 1), dir_name);
    snprintf(manifest.version, sizeof(manifest.version), "1.0.0");
    manifest.description[0] = '\0';
    manifest.author[0] = '\0';
    snprintf(manifest.main_file, sizeof(manifest.main_file), "main.jp");
    manifest.dep_count = 0;
    
    if (!write_manifest(PACKAGE_MANIFEST_FILE, &manifest)) {
        fprintf(stderr, "エラー: %s を作成できません\n", PACKAGE_MANIFEST_FILE);
        return 1;
    }
    
    printf("✓ %s を作成しました\n", PACKAGE_MANIFEST_FILE);
    printf("\n");
    printf("  プロジェクト名: %s\n", manifest.name);
    printf("  バージョン:     %s\n", manifest.version);
    printf("  メインファイル: %s\n", manifest.main_file);
    printf("\n");
    
    return 0;
}

/**
 * パッケージをインストール
 */
int package_install(const char *name_or_url) {
    char url[PACKAGE_MAX_PATH] = {0};
    char package_name[PACKAGE_MAX_NAME] = {0};
    
    int source_type = classify_source(name_or_url);
    
    if (source_type == 1 || source_type == 2) {
        // URL またはローカルパス
        normalize_github_url(name_or_url, url, sizeof(url));
        extract_package_name(name_or_url, package_name, sizeof(package_name));
    } else {
        // パッケージ名のみ → hajimu.json の依存から検索
        snprintf(package_name, sizeof(package_name), "%s", name_or_url);
        
        PackageManifest manifest;
        if (package_read_manifest(PACKAGE_MANIFEST_FILE, &manifest)) {
            for (int i = 0; i < manifest.dep_count; i++) {
                if (strcmp(manifest.deps[i].name, package_name) == 0) {
                    normalize_github_url(manifest.deps[i].source, url, sizeof(url));
                    break;
                }
            }
        }
        
        if (url[0] == '\0') {
            fprintf(stderr, "エラー: パッケージ '%s' のソースが見つかりません\n", package_name);
            fprintf(stderr, "  GitHubリポジトリURLを指定してください:\n");
            fprintf(stderr, "  例: hajimu パッケージ 追加 ユーザー名/リポジトリ名\n");
            return 1;
        }
    }
    
    // ローカルパッケージディレクトリに配置
    char pkg_dir[PACKAGE_MAX_PATH];
    get_package_path(package_name, true, pkg_dir, sizeof(pkg_dir));
    
    if (dir_exists(pkg_dir)) {
        printf("⚠  パッケージ '%s' は既にインストールされています\n", package_name);
        printf("  再インストールするには先に削除してください:\n");
        printf("  hajimu パッケージ 削除 %s\n", package_name);
        return 1;
    }
    
    // hajimu_packages ディレクトリを作成
    if (!dir_exists(PACKAGE_LOCAL_DIR)) {
        mkdirs(PACKAGE_LOCAL_DIR);
    }
    
    printf("📦 パッケージ '%s' をインストール中...\n", package_name);
    printf("   ソース: %s\n", url);
    
    // git clone
    int result = git_clone(url, pkg_dir);
    if (result != 0) {
        fprintf(stderr, "エラー: パッケージ '%s' のダウンロードに失敗しました\n", package_name);
        remove_directory(pkg_dir);
        return 1;
    }
    
    // .git ディレクトリを削除（容量削減）
    char git_dir[PACKAGE_MAX_PATH + 32];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", pkg_dir);
    if (dir_exists(git_dir)) {
        remove_directory(git_dir);
    }
    
    // hajimu.json が存在するか確認
    char manifest_path[PACKAGE_MAX_PATH + 32];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", pkg_dir, PACKAGE_MANIFEST_FILE);
    
    PackageManifest pkg_manifest;
    bool has_manifest = package_read_manifest(manifest_path, &pkg_manifest);
    if (has_manifest) {
        printf("   パッケージ: %s v%s\n", pkg_manifest.name, pkg_manifest.version);
        if (pkg_manifest.description[0]) {
            printf("   説明: %s\n", pkg_manifest.description);
        }
        
        // 依存パッケージも再帰的にインストール
        for (int i = 0; i < pkg_manifest.dep_count; i++) {
            char dep_dir[PACKAGE_MAX_PATH];
            get_package_path(pkg_manifest.deps[i].name, true, dep_dir, sizeof(dep_dir));
            
            if (!dir_exists(dep_dir)) {
                printf("\n   → 依存パッケージ '%s' をインストール中...\n", 
                       pkg_manifest.deps[i].name);
                package_install(pkg_manifest.deps[i].source);
            }
        }
    }
    
    // ポストインストールビルド:
    // パッケージの種類に応じて処理を分岐:
    //   スクリプトパッケージ (.jp): ビルド不要、そのまま使用可能
    //   バイトコードパッケージ (.hjp HJPB): ビルド済み or 自動コンパイル
    //   ネイティブ C プラグイン (.hjp DLL/dylib): ビルドまたは pre-built ダウンロード
    {
        // ── スクリプトパッケージの判定 ──
        // hajimu.json の "メイン" が .jp ファイルを指し、ビルドコマンドがない場合
        bool is_script_package = false;
        if (has_manifest) {
            const char *mf = pkg_manifest.main_file;
            size_t mf_len = strlen(mf);
            bool main_is_jp = (mf_len >= 3 && strcmp(mf + mf_len - 3, ".jp") == 0
                               && (mf_len < 4 || mf[mf_len - 4] != 'h')); /* .jp but not .hjp */
            if (main_is_jp && !pkg_manifest.build_cmd[0]) {
                is_script_package = true;
            }
        } else {
            // hajimu.json がない場合、main.jp / <name>.jp の存在でスクリプトと判定
            char jp_path[PACKAGE_MAX_PATH + 32];
            snprintf(jp_path, sizeof(jp_path), "%s/main.jp", pkg_dir);
            if (file_exists(jp_path)) is_script_package = true;
        }

        if (is_script_package) {
            printf("   📜 スクリプトパッケージとして認識しました\n");
            printf("      ビルド不要・クロスプラットフォームで動作します\n");
            // pure .jp パッケージはそのまま使用可能 (goto 後の依存追加に進む)
            goto post_install_done;
        }

        char found_hjp[PACKAGE_MAX_PATH] = {0};
        bool hjp_found = find_hjp_recursive(pkg_dir, found_hjp, sizeof(found_hjp), 0);

        /* ---- pre-built .hjp のダウンロードを試みる ---- */
        if (!hjp_found) {
            /* GitHub Releases の URL を構築
             * 優先順位:
             *   1. hajimu.json の "release" フィールドに指定された URL
             *   2. <repo>/releases/latest/download/<name>-windows-x64.hjp  (Win)
             *   3. <repo>/releases/latest/download/<name>-macos.hjp        (Mac)
             *   4. <repo>/releases/latest/download/<name>-linux-x64.hjp   (Linux)
             *   5. <repo>/releases/latest/download/<name>.hjp              (共通)
             */
            char base_url[PACKAGE_MAX_PATH] = {0};
            repo_base_url(url, base_url, sizeof(base_url));

            char release_candidates[5][PACKAGE_MAX_PATH];
            int  n_candidates = 0;

            /* hajimu.json に明示的な release URL があれば最優先 */
            if (has_manifest && pkg_manifest.release_url[0]) {
                snprintf(release_candidates[n_candidates++],
                    PACKAGE_MAX_PATH, "%s", pkg_manifest.release_url);
            }

            if (base_url[0]) {
#ifdef _WIN32
                snprintf(release_candidates[n_candidates++], PACKAGE_MAX_PATH,
                    "%s/releases/latest/download/%s-windows-x64.hjp", base_url, package_name);
                snprintf(release_candidates[n_candidates++], PACKAGE_MAX_PATH,
                    "%s/releases/latest/download/%s-win64.hjp",        base_url, package_name);
#elif defined(__APPLE__)
                snprintf(release_candidates[n_candidates++], PACKAGE_MAX_PATH,
                    "%s/releases/latest/download/%s-macos.hjp",        base_url, package_name);
                snprintf(release_candidates[n_candidates++], PACKAGE_MAX_PATH,
                    "%s/releases/latest/download/%s-darwin.hjp",       base_url, package_name);
#else
                snprintf(release_candidates[n_candidates++], PACKAGE_MAX_PATH,
                    "%s/releases/latest/download/%s-linux-x64.hjp",   base_url, package_name);
#endif
                snprintf(release_candidates[n_candidates++], PACKAGE_MAX_PATH,
                    "%s/releases/latest/download/%s.hjp",              base_url, package_name);
            }

            /* 候補を順に試す */
            for (int ci = 0; ci < n_candidates && !hjp_found; ci++) {
                char dest[PACKAGE_MAX_PATH + 32];
                snprintf(dest, sizeof(dest), "%s/%s.hjp", pkg_dir, package_name);
                printf("   🌐 ビルド済みバイナリを確認中...\n");
                if (download_to_file(release_candidates[ci], dest) && file_exists(dest)) {
                    printf("   ✅ ビルド済みバイナリをダウンロードしました\n");
                    strncpy(found_hjp, dest, sizeof(found_hjp)-1);
                    hjp_found = true;
                    /* main を更新 */
                    PackageManifest updated_m;
                    bool got_m = package_read_manifest(manifest_path, &updated_m);
                    if (!got_m) memcpy(&updated_m, &pkg_manifest, sizeof(pkg_manifest));
                    snprintf(updated_m.main_file, sizeof(updated_m.main_file),
                             "%s.hjp", package_name);
                    write_manifest(manifest_path, &updated_m);
                    printf("   → プラグイン: %s.hjp\n", package_name);
                }
            }
        }
        /* ---------------------------------------------------------- */

        if (!hjp_found) {
            // ビルドコマンドを決定（hajimu.json の "ビルド" → Makefile → 自動検出）
            char build_cmd[PACKAGE_MAX_PATH * 2] = {0};

            // はじむヘッダーのパスを自動検出
            char include_dir[PACKAGE_MAX_PATH] = {0};
            {
                char self_path[PACKAGE_MAX_PATH] = {0};
                #ifdef __APPLE__
                uint32_t self_size = sizeof(self_path);
                _NSGetExecutablePath(self_path, &self_size);
                #elif defined(_WIN32)
                GetModuleFileNameA(NULL, self_path, (DWORD)sizeof(self_path));
                for (char *p = self_path; *p; p++) { if (*p == '\\') *p = '/'; }
                #elif defined(__linux__)
                readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
                #endif
                if (self_path[0]) {
                    char *last_slash = strrchr(self_path, '/');
                    if (last_slash) {
                        *last_slash = '\0';
                        snprintf(include_dir, sizeof(include_dir), "%s/include", self_path);
                        if (!dir_exists(include_dir)) include_dir[0] = '\0';
                    }
                }
                /* バイナリ隣に include/ がない場合はシステム標準パスを試す */
                if (!include_dir[0]) {
                    const char *fallbacks[] = {
                        "/usr/local/include/hajimu",
                        "/usr/include/hajimu",
                        "/opt/homebrew/include/hajimu",
                        NULL
                    };
                    for (int _fi = 0; fallbacks[_fi]; _fi++) {
                        if (dir_exists(fallbacks[_fi])) {
                            snprintf(include_dir, sizeof(include_dir), "%s", fallbacks[_fi]);
                            break;
                        }
                    }
                }
            }

            char user_cmd[PACKAGE_MAX_PATH] = {0};
            if (has_manifest && pkg_manifest.build_cmd[0]) {
                snprintf(user_cmd, sizeof(user_cmd), "%s", pkg_manifest.build_cmd);
            } else {
                char makefile_path[PACKAGE_MAX_PATH + 32];
                snprintf(makefile_path, sizeof(makefile_path), "%s/Makefile", pkg_dir);
                if (file_exists(makefile_path)) {
                    snprintf(user_cmd, sizeof(user_cmd), "make");
                }
            }

#ifdef _WIN32
            /* make コマンドかどうか判定し、引数部分を保存する */
            bool is_make_cmd = false;
            char make_args_only[PACKAGE_MAX_PATH] = {0};
            if (user_cmd[0] &&
                strncmp(user_cmd, "make", 4) == 0 &&
                (user_cmd[4] == '\0' || user_cmd[4] == ' ')) {
                is_make_cmd = true;
                if (user_cmd[4] == ' ') {
                    strncpy(make_args_only, user_cmd + 5, sizeof(make_args_only) - 1);
                }
            }
#endif

            if (user_cmd[0]) {
#ifdef _WIN32
                /* ================================================================
                 * Windows ビルド戦略
                 *
                 * MSYS2 の gcc / sh.exe のパスを動的に発見して PATH に追加し、
                 * その後 MSYS2 bash --login 経由でビルドする。
                 * bash が見つからない場合は mingw32-make を直接呼ぶ。
                 *
                 * MSYS2 ルートの発見順:
                 *   1. where.exe で gcc.exe を直接検索
                 *   2. where.exe で mingw32-make.exe を検索 → 同じ bin ディレクトリ
                 *   3. where.exe で make.exe を検索 → 同じ bin ディレクトリ
                 *   4. where.exe で bash.exe / sh.exe を検索 → usr\bin から root を逆算
                 *   5. 固定パス候補 (C:\msys64, C:\msys2, D:\msys64 等)
                 *
                 * 文字化け対策:
                 *   LANG=C / LC_ALL=C を設定し make/gcc エラーを英語出力にする。
                 * ================================================================ */

                /* Windows パスの / → \ 変換 */
                char win_pkg_dir[PACKAGE_MAX_PATH];
                snprintf(win_pkg_dir, sizeof(win_pkg_dir), "%s", pkg_dir);
                for (char *p = win_pkg_dir; *p; p++) if (*p == '/') *p = '\\';

                char orig_dir[PACKAGE_MAX_PATH] = {0};
                _getcwd(orig_dir, sizeof(orig_dir));

                char win_inc_dir[PACKAGE_MAX_PATH] = {0};
                if (include_dir[0]) {
                    snprintf(win_inc_dir, sizeof(win_inc_dir), "%s", include_dir);
                    for (char *p = win_inc_dir; *p; p++) if (*p == '/') *p = '\\';
                }

                /* ---- STEP 1: gcc の bin ディレクトリを発見 ---- */
                char gcc_bin_dir[PACKAGE_MAX_PATH] = {0};  /* e.g. C:\msys64\mingw64\bin */
                char msys2_root[PACKAGE_MAX_PATH]  = {0};  /* e.g. C:\msys64            */

                /*
                 * MSYS2 root の発見戦略:
                 *
                 *   STEP 1: PATH 環境変数の各エントリから上位ディレクトリを辿り
                 *            <ancestor>\mingw64\bin\gcc.exe が実在する祖先を root とする。
                 *            (C:\msys64\usr\bin が PATH にあれば2段上で C:\msys64 を発見)
                 *   STEP 2: where mingw32-make.exe の全行走査 (AppData/WindowsApps除外)
                 *   STEP 3: 固定パス候補を mingw64\bin\gcc.exe で検証。
                 */

                /* PATH エントリから祖先を辿って msys2_root を探すヘルパー */
#define TRY_FIND_MSYS2_ROOT(entry) do { \
    char _tmp[PACKAGE_MAX_PATH]; \
    strncpy(_tmp, (entry), sizeof(_tmp)-1); \
    /* 最大4段上まで辿る */ \
    for (int _d = 0; _d <= 4 && !msys2_root[0]; _d++) { \
        /* AppData / WindowsApps は無条件スキップ */ \
        if (strstr(_tmp, "WindowsApps") || strstr(_tmp, "AppData")) break; \
        char _probe[PACKAGE_MAX_PATH + 30]; \
        snprintf(_probe, sizeof(_probe), "%s\\mingw64\\bin\\gcc.exe", _tmp); \
        if (GetFileAttributesA(_probe) != INVALID_FILE_ATTRIBUTES) { \
            strncpy(msys2_root, _tmp, sizeof(msys2_root)-1); \
            break; \
        } \
        /* 1段上へ */ \
        char *_sep = strrchr(_tmp, '\\'); \
        if (!_sep || _sep == _tmp) break; \
        *_sep = '\0'; \
    } \
} while(0)

                /* ---- STEP 1: PATH 環境変数の各エントリから msys2_root を探索 ---- */
                {
                    char path_env[8192] = {0};
                    GetEnvironmentVariableA("PATH", path_env, sizeof(path_env));
                    char *tok = path_env;
                    while (*tok && !msys2_root[0]) {
                        char *semi = strchr(tok, ';');
                        size_t elen = semi ? (size_t)(semi - tok) : strlen(tok);
                        if (elen > 0 && elen < PACKAGE_MAX_PATH) {
                            char entry[PACKAGE_MAX_PATH] = {0};
                            memcpy(entry, tok, elen);
                            /* mingw または msys を含むエントリのみ対象 */
                            char lower[PACKAGE_MAX_PATH] = {0};
                            for (size_t i = 0; i < elen && i < sizeof(lower)-1; i++)
                                lower[i] = (char)tolower((unsigned char)entry[i]);
                            if (strstr(lower, "mingw") || strstr(lower, "msys")) {
                                TRY_FIND_MSYS2_ROOT(entry);
                            }
                        }
                        if (!semi) break;
                        tok = semi + 1;
                    }
                }

                /* ---- STEP 2: where mingw32-make.exe の全行走査 ---- */
                if (!msys2_root[0]) {
                    FILE *wh = popen("where mingw32-make.exe 2>NUL", "r");
                    if (wh) {
                        char line[PACKAGE_MAX_PATH];
                        while (!msys2_root[0] && fgets(line, sizeof(line), wh)) {
                            size_t ln = strlen(line);
                            while (ln > 0 && (line[ln-1]=='\n'||line[ln-1]=='\r'||line[ln-1]==' ')) line[--ln]='\0';
                            if (strstr(line, "WindowsApps") || strstr(line, "AppData")) continue;
                            /* "C:\msys64\mingw64\bin\mingw32-make.exe" → dir = "...\bin" */
                            char *sep = strrchr(line, '\\');
                            if (sep) { *sep = '\0'; TRY_FIND_MSYS2_ROOT(line); }
                        }
                        pclose(wh);
                    }
                }

                /* ---- STEP 3: 固定パス候補を mingw64\bin\gcc.exe で検証 ---- */
                if (!msys2_root[0]) {
                    static const char * const roots[] = {
                        "C:\\msys64", "C:\\msys2",
                        "D:\\msys64", "D:\\msys2",
                        "E:\\msys64", "E:\\msys2",
                        "C:\\tools\\msys64", "C:\\tools\\msys2",
                        "C:\\ProgramData\\chocolatey\\lib\\msys2\\tools\\msys64",
                        NULL
                    };
                    for (int ri = 0; roots[ri] && !msys2_root[0]; ri++) {
                        char probe[PACKAGE_MAX_PATH + 30];
                        snprintf(probe, sizeof(probe), "%s\\mingw64\\bin\\gcc.exe", roots[ri]);
                        if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) {
                            strncpy(msys2_root, roots[ri], sizeof(msys2_root)-1);
                        }
                    }
                }

#undef TRY_FIND_MSYS2_ROOT

                /* msys2_root が確定したら gcc_bin_dir を設定 */
                if (msys2_root[0]) {
                    snprintf(gcc_bin_dir, sizeof(gcc_bin_dir), "%s\\mingw64\\bin", msys2_root);
                }

                /* ---- STEP 3: LANG=C で文字化け防止、PATH に gcc bin を追加 ---- */
                SetEnvironmentVariableA("LANG", "C");
                SetEnvironmentVariableA("LC_ALL", "C");
                if (win_inc_dir[0]) SetEnvironmentVariableA("HAJIMU_INCLUDE", win_inc_dir);

                if (gcc_bin_dir[0]) {
                    char cur_path[8192] = {0};
                    GetEnvironmentVariableA("PATH", cur_path, sizeof(cur_path));
                    char new_path[8192];
                    if (cur_path[0])
                        snprintf(new_path, sizeof(new_path), "%s;%s", gcc_bin_dir, cur_path);
                    else
                        snprintf(new_path, sizeof(new_path), "%s", gcc_bin_dir);
                    SetEnvironmentVariableA("PATH", new_path);
                }

                /* MSYS2/MinGW が見つからない場合はビルドをスキップ */
                if (!msys2_root[0] && !gcc_bin_dir[0]) {
                    printf("   ⚠  MSYS2/MinGW が見つかりません。ソースからのビルドをスキップします。\n");
                    printf("      MSYS2 をインストールして MSYS2 MinGW64 シェルから再実行してください:\n");
                    printf("      https://www.msys2.org/\n");
                    printf("      インストール後: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make\n");
                    if (orig_dir[0]) _chdir(orig_dir);
                    /* このブロックを抜けるために build_cmd を空のままにして後続処理へ進む */
                    goto win_build_skip;
                }

                /* ---- STEP 4: ビルドコマンド構築 ---- */
                if (msys2_root[0] && is_make_cmd) {
                    /* bash --login 経由: /etc/profile.d/ が mingw64/bin を PATH に追加 */
                    char bash_exe[PACKAGE_MAX_PATH];
                    snprintf(bash_exe, sizeof(bash_exe), "%s\\usr\\bin\\bash.exe", msys2_root);

                    /* Windows パス → MSYS2 POSIX パス変換 (C:\foo → /c/foo) */
                    char msys2_dir[PACKAGE_MAX_PATH] = {0};
                    if (pkg_dir[1] == ':') {
                        msys2_dir[0] = '/';
                        msys2_dir[1] = (char)tolower((unsigned char)pkg_dir[0]);
                        strncpy(msys2_dir + 2, pkg_dir + 2, sizeof(msys2_dir) - 3);
                        for (char *p = msys2_dir; *p; p++) if (*p == '\\') *p = '/';
                    } else {
                        strncpy(msys2_dir, pkg_dir, sizeof(msys2_dir)-1);
                        for (char *p = msys2_dir; *p; p++) if (*p == '\\') *p = '/';
                    }

                    const char *make_target = make_args_only[0] ? make_args_only : "";
                    if (win_inc_dir[0]) {
                        char msys2_inc[PACKAGE_MAX_PATH] = {0};
                        if (include_dir[1] == ':') {
                            msys2_inc[0] = '/';
                            msys2_inc[1] = (char)tolower((unsigned char)include_dir[0]);
                            strncpy(msys2_inc + 2, include_dir + 2, sizeof(msys2_inc) - 3);
                            for (char *p = msys2_inc; *p; p++) if (*p == '\\') *p = '/';
                        } else {
                            strncpy(msys2_inc, include_dir, sizeof(msys2_inc)-1);
                        }
                        snprintf(build_cmd, sizeof(build_cmd),
                            "\"%s\" --login -c \"cd '%s' && HAJIMU_INCLUDE='%s' make %s\" 2>&1",
                            bash_exe, msys2_dir, msys2_inc, make_target);
                    } else {
                        snprintf(build_cmd, sizeof(build_cmd),
                            "\"%s\" --login -c \"cd '%s' && make %s\" 2>&1",
                            bash_exe, msys2_dir, make_target);
                    }
                } else {
                    /* フォールバック: mingw32-make を直接呼ぶ (PATH は既に補完済み) */
                    char make_tool[32] = "mingw32-make";
                    /* make が PATH にあればそちらを優先 */
                    FILE *gm = popen("make --version 2>NUL", "r");
                    if (gm) {
                        char _t[64] = {0};
                        if (fgets(_t, sizeof(_t), gm)) strncpy(make_tool, "make", sizeof(make_tool)-1);
                        pclose(gm);
                    }
                    char final_cmd[PACKAGE_MAX_PATH] = {0};
                    if (make_args_only[0])
                        snprintf(final_cmd, sizeof(final_cmd), "%s %s", make_tool, make_args_only);
                    else if (is_make_cmd)
                        strncpy(final_cmd, make_tool, sizeof(final_cmd)-1);
                    else
                        strncpy(final_cmd, user_cmd, sizeof(final_cmd)-1);

                    if (_chdir(win_pkg_dir) != 0) {
                        fprintf(stderr, "   ⚠  ディレクトリ変更失敗: %s\n", win_pkg_dir);
                    } else {
                        snprintf(build_cmd, sizeof(build_cmd), "%s 2>&1", final_cmd);
                    }
                }
#else
                if (include_dir[0]) {
                    snprintf(build_cmd, sizeof(build_cmd),
                             "cd \"%s\" && HAJIMU_INCLUDE=\"%s\" %s 2>&1",
                             pkg_dir, include_dir, user_cmd);
                } else {
                    snprintf(build_cmd, sizeof(build_cmd),
                             "cd \"%s\" && %s 2>&1", pkg_dir, user_cmd);
                }
#endif
                if (build_cmd[0]) {
#ifdef _WIN32
                /* ビルド環境情報を表示 */
                if (msys2_root[0])
                    printf("   → MSYS2: %s\n", msys2_root);
                else if (gcc_bin_dir[0])
                    printf("   → gcc: %s\n", gcc_bin_dir);
#endif
                printf("   🔨 ビルド中...\n");
                FILE *bp = popen(build_cmd, "r");
                if (bp) {
                    /* 全出力をバッファリングし、失敗時に全行を表示する */
                    char line[1024];
                    char *build_log = NULL;
                    size_t log_len = 0;
                    while (fgets(line, sizeof(line), bp)) {
                        size_t ll = strlen(line);
                        char *tmp = realloc(build_log, log_len + ll + 1);
                        if (tmp) { build_log = tmp; memcpy(build_log + log_len, line, ll + 1); log_len += ll; }
                    }
                    int bstatus = pclose(bp);
#ifdef _WIN32
                    if (orig_dir[0]) _chdir(orig_dir); /* 元ディレクトリに復帰 */
#endif
                    if (WEXITSTATUS(bstatus) == 0) {
                        printf("   ✅ ビルド成功\n");
                        /* ビルド後に生成された .hjp を再帰検索し hajimu.json の main を更新 */
                        char built_hjp[PACKAGE_MAX_PATH] = {0};
                        if (find_hjp_recursive(pkg_dir, built_hjp, sizeof(built_hjp), 0)) {
                            /* pkg_dir 相対パスに変換 */
                            const char *rel = built_hjp;
                            size_t prefix_len = strlen(pkg_dir);
                            if (strncmp(built_hjp, pkg_dir, prefix_len) == 0 &&
                                (built_hjp[prefix_len] == '/' || built_hjp[prefix_len] == '\\')) {
                                rel = built_hjp + prefix_len + 1;
                            }
                            PackageManifest updated_m;
                            bool got_m = package_read_manifest(manifest_path, &updated_m);
                            if (!got_m) memcpy(&updated_m, &pkg_manifest, sizeof(pkg_manifest));
                            snprintf(updated_m.main_file, sizeof(updated_m.main_file), "%s", rel);
                            write_manifest(manifest_path, &updated_m);
                            printf("   → プラグイン: %s\n", rel);
                        }
                    } else {
                        printf("   ⚠  ビルドに失敗しました\n");
                        if (build_log && log_len > 0) {
                            printf("   --- ビルドログ ---\n");
                            /* 行ごとに表示（最大40行）*/
                            char *p = build_log;
                            int shown = 0;
                            while (*p && shown < 40) {
                                char *nl = strchr(p, '\n');
                                size_t span = nl ? (size_t)(nl - p + 1) : strlen(p);
                                printf("      %.*s", (int)span, p);
                                if (!nl) { printf("\n"); }
                                p += span;
                                shown++;
                            }
                            if (*p) printf("      ... (省略)\n");
                            printf("   ------------------\n");
                        }
                    }
                    free(build_log);
                }
                } /* if (build_cmd[0]) */
#ifdef _WIN32
                win_build_skip:; /* MSYS2/MinGW が見つからない場合の goto 着地点 */
#endif
            } else {
                printf("   ⚠  .hjp ファイルが見つかりません\n");
                printf("      パッケージディレクトリで make を実行してください:\n");
                printf("      cd %s && make\n", pkg_dir);
            }
        }
    }

    post_install_done:; /* スクリプトパッケージは goto でここにジャンプ */

    // プロジェクトの hajimu.json に依存を追加
    PackageManifest project;
    if (package_read_manifest(PACKAGE_MANIFEST_FILE, &project)) {
        // 既に存在するかチェック
        bool exists = false;
        for (int i = 0; i < project.dep_count; i++) {
            if (strcmp(project.deps[i].name, package_name) == 0) {
                exists = true;
                break;
            }
        }
        
        if (!exists && project.dep_count < PACKAGE_MAX_DEPS) {
            snprintf(project.deps[project.dep_count].name, 
                     sizeof(project.deps[0].name), "%s", package_name);
            // URLから.gitを除去してソースとして保存
            char clean_url[PACKAGE_MAX_PATH];
            snprintf(clean_url, sizeof(clean_url), "%s", url);
            char *git_ext = strstr(clean_url, ".git");
            if (git_ext && strlen(git_ext) == 4) *git_ext = '\0';
            // https://github.com/ を除去して user/repo 形式に
            const char *gh_prefix = "https://github.com/";
            if (strncmp(clean_url, gh_prefix, strlen(gh_prefix)) == 0) {
                snprintf(project.deps[project.dep_count].source,
                         sizeof(project.deps[0].source), "%s", 
                         clean_url + strlen(gh_prefix));
            } else {
                snprintf(project.deps[project.dep_count].source,
                         sizeof(project.deps[0].source), "%s", clean_url);
            }
            project.dep_count++;
            write_manifest(PACKAGE_MANIFEST_FILE, &project);
            printf("   → %s に依存を追加しました\n", PACKAGE_MANIFEST_FILE);
        }
    }
    
    printf("✓ パッケージ '%s' をインストールしました\n", package_name);
    return 0;
}

/**
 * hajimu.json の全依存パッケージをインストール
 */
int package_install_all(void) {
    PackageManifest manifest;
    if (!package_read_manifest(PACKAGE_MANIFEST_FILE, &manifest)) {
        fprintf(stderr, "エラー: %s が見つかりません\n", PACKAGE_MANIFEST_FILE);
        fprintf(stderr, "  先に初期化してください: hajimu パッケージ 初期化\n");
        return 1;
    }
    
    if (manifest.dep_count == 0) {
        printf("依存パッケージはありません\n");
        return 0;
    }
    
    printf("📦 %d 個の依存パッケージをインストール中...\n\n", manifest.dep_count);
    
    int failed = 0;
    for (int i = 0; i < manifest.dep_count; i++) {
        char pkg_dir[PACKAGE_MAX_PATH];
        get_package_path(manifest.deps[i].name, true, pkg_dir, sizeof(pkg_dir));
        
        if (dir_exists(pkg_dir)) {
            printf("✓ %s (インストール済み)\n", manifest.deps[i].name);
            continue;
        }
        
        if (package_install(manifest.deps[i].source) != 0) {
            failed++;
        }
    }
    
    printf("\n");
    if (failed > 0) {
        printf("⚠  %d 個のパッケージのインストールに失敗しました\n", failed);
        return 1;
    }
    printf("✓ すべての依存パッケージをインストールしました\n");
    return 0;
}

/**
 * パッケージを削除
 */
int package_remove(const char *name) {
    char pkg_dir[PACKAGE_MAX_PATH];
    get_package_path(name, true, pkg_dir, sizeof(pkg_dir));
    
    if (!dir_exists(pkg_dir)) {
        fprintf(stderr, "エラー: パッケージ '%s' はインストールされていません\n", name);
        return 1;
    }
    
    printf("🗑  パッケージ '%s' を削除中...\n", name);
    
    if (remove_directory(pkg_dir) != 0) {
        fprintf(stderr, "エラー: パッケージ '%s' を削除できません\n", name);
        return 1;
    }
    
    // hajimu.json から依存を削除
    PackageManifest project;
    if (package_read_manifest(PACKAGE_MANIFEST_FILE, &project)) {
        for (int i = 0; i < project.dep_count; i++) {
            if (strcmp(project.deps[i].name, name) == 0) {
                // 削除: 後ろの要素を前にシフト
                for (int j = i; j < project.dep_count - 1; j++) {
                    project.deps[j] = project.deps[j + 1];
                }
                project.dep_count--;
                write_manifest(PACKAGE_MANIFEST_FILE, &project);
                printf("   → %s から依存を削除しました\n", PACKAGE_MANIFEST_FILE);
                break;
            }
        }
    }
    
    printf("✓ パッケージ '%s' を削除しました\n", name);
    return 0;
}

/**
 * インストール済みパッケージ一覧を表示
 */
int package_list(void) {
    printf("📋 インストール済みパッケージ:\n\n");
    
    int count = 0;
    
    // ローカルパッケージ (hajimu_packages/)
    if (dir_exists(PACKAGE_LOCAL_DIR)) {
        DIR *dir = opendir(PACKAGE_LOCAL_DIR);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                
                char pkg_dir[PACKAGE_MAX_PATH];
                snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", PACKAGE_LOCAL_DIR, entry->d_name);
                
                struct stat st;
                if (stat(pkg_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
                    // hajimu.json を読んで情報表示
                    char manifest_path[PACKAGE_MAX_PATH + 32];
                    snprintf(manifest_path, sizeof(manifest_path), 
                             "%s/%s", pkg_dir, PACKAGE_MANIFEST_FILE);
                    
                    PackageManifest manifest;
                    if (package_read_manifest(manifest_path, &manifest)) {
                        printf("  📦 %s v%s", manifest.name, manifest.version);
                        if (manifest.description[0]) {
                            printf(" - %s", manifest.description);
                        }
                        printf("\n");
                    } else {
                        printf("  📦 %s (マニフェストなし)\n", entry->d_name);
                    }
                    count++;
                }
            }
            closedir(dir);
        }
    }
    
    if (count == 0) {
        printf("  (パッケージはインストールされていません)\n");
        printf("\n  パッケージをインストールするには:\n");
        printf("  hajimu パッケージ 追加 ユーザー名/リポジトリ名\n");
    }
    
    printf("\n合計: %d パッケージ\n", count);
    return 0;
}

// =============================================================================
// パッケージパス解決
// =============================================================================

/**
 * パッケージ名からエントリポイントファイルのパスを解決
 * 
 * 解決順序:
 * 1. ローカル hajimu_packages/<パッケージ名>/
 *    a. hajimu.json のメインファイル
 *    b. main.jp
 *    c. <パッケージ名>.jp
 * 2. グローバル ~/.hajimu/packages/<パッケージ名>/
 */
bool package_resolve(const char *package_name, const char *caller_file,
                     char *resolved_path, int max_len) {
    char base_dir[PACKAGE_MAX_PATH] = {0};
    
    // 呼び出し元ファイルのディレクトリを基準にする
    if (caller_file) {
        snprintf(base_dir, sizeof(base_dir), "%s", caller_file);
        char *last_sep = strrchr(base_dir, '/');
        if (last_sep) {
            *(last_sep + 1) = '\0';
        } else {
            base_dir[0] = '\0';
        }
    }
    
    // 検索パスリスト
    char search_paths[3][PACKAGE_MAX_PATH + PACKAGE_MAX_NAME];
    int search_count = 0;
    
    // 1. 呼び出し元からの相対 hajimu_packages/
    if (base_dir[0]) {
        snprintf(search_paths[search_count++], PACKAGE_MAX_PATH + PACKAGE_MAX_NAME,
                 "%s%s/%s", base_dir, PACKAGE_LOCAL_DIR, package_name);
    }
    
    // 2. CWDからの hajimu_packages/
    snprintf(search_paths[search_count++], PACKAGE_MAX_PATH + PACKAGE_MAX_NAME,
             "%s/%s", PACKAGE_LOCAL_DIR, package_name);
    
    // 3. グローバル ~/.hajimu/packages/
    char global_dir[PACKAGE_MAX_PATH];
    get_global_package_dir(global_dir, sizeof(global_dir));
    snprintf(search_paths[search_count++], PACKAGE_MAX_PATH + PACKAGE_MAX_NAME,
             "%s/%s", global_dir, package_name);
    
    for (int i = 0; i < search_count; i++) {
        if (!dir_exists(search_paths[i])) continue;
        
        // hajimu.json を確認してメインファイルを取得
        char manifest_path_r[4096];
        snprintf(manifest_path_r, sizeof(manifest_path_r),
                 "%s/%s", search_paths[i], PACKAGE_MANIFEST_FILE);

        PackageManifest manifest;
        if (package_read_manifest(manifest_path_r, &manifest)) {
            snprintf(resolved_path, max_len, "%s/%s",
                     search_paths[i], manifest.main_file);
            if (file_exists(resolved_path)) return true;
        }

        // main.jp を試す
        snprintf(resolved_path, max_len, "%s/main.jp", search_paths[i]);
        if (file_exists(resolved_path)) return true;

        // <パッケージ名>.jp を試す
        snprintf(resolved_path, max_len, "%s/%s.jp", search_paths[i], package_name);
        if (file_exists(resolved_path)) return true;

        /* --- .hjp ファイルのフォールバック検索 ---
         * hajimu.json の main が未設定 / 検出できなかった場合に
         * ネイティブプラグインとして利用可能な .hjp を探す。 */
        // ルートの main.hjp / <name>.hjp
        snprintf(resolved_path, max_len, "%s/main.hjp", search_paths[i]);
        if (file_exists(resolved_path)) return true;
        snprintf(resolved_path, max_len, "%s/%s.hjp", search_paths[i], package_name);
        if (file_exists(resolved_path)) return true;
        // ビルド出力サブディレクトリ内の <name>.hjp / main.hjp
        {
            static const char * const build_subdirs[] = {"build", "dist", "lib", "bin", NULL};
            for (int _s = 0; build_subdirs[_s] != NULL; _s++) {
                snprintf(resolved_path, max_len, "%s/%s/%s.hjp",
                         search_paths[i], build_subdirs[_s], package_name);
                if (file_exists(resolved_path)) return true;
                snprintf(resolved_path, max_len, "%s/%s/main.hjp",
                         search_paths[i], build_subdirs[_s]);
                if (file_exists(resolved_path)) return true;
            }
        }
    }
    
    return false;
}
