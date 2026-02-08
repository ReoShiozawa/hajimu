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
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
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
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    return home;
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
    strcpy(manifest->main_file, "main.jp"); // デフォルト
    strcpy(manifest->version, "0.0.0");
    
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
    FILE *f = fopen(path, "w");
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
        snprintf(name, max_len, "%s", last_slash + 1);
    } else {
        snprintf(name, max_len, "%s", clean_url);
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
 * git clone でパッケージをダウンロード
 */
static int git_clone(const char *url, const char *dest) {
    char cmd[PACKAGE_MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "git clone --depth 1 -q \"%s\" \"%s\" 2>&1", url, dest);
    
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "エラー: git clone を実行できません\n");
        return 1;
    }
    
    char output[1024];
    while (fgets(output, sizeof(output), pipe)) {
        // エラー出力を表示
        if (strstr(output, "fatal:") || strstr(output, "error:")) {
            fprintf(stderr, "%s", output);
        }
    }
    
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
    snprintf(manifest.name, sizeof(manifest.name), "%s", dir_name);
    strcpy(manifest.version, "1.0.0");
    strcpy(manifest.description, "");
    strcpy(manifest.author, "");
    strcpy(manifest.main_file, "main.jp");
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
    char git_dir[PACKAGE_MAX_PATH];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", pkg_dir);
    if (dir_exists(git_dir)) {
        remove_directory(git_dir);
    }
    
    // hajimu.json が存在するか確認
    char manifest_path[PACKAGE_MAX_PATH];
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
    // .hjp ファイルが存在しない場合、自動的にビルドを試みる
    {
        // .hjp ファイルを検索
        bool hjp_found = false;
        DIR *pkg_dir_handle = opendir(pkg_dir);
        if (pkg_dir_handle) {
            struct dirent *ent;
            while ((ent = readdir(pkg_dir_handle)) != NULL) {
                size_t nlen = strlen(ent->d_name);
                if (nlen > 4 && strcmp(ent->d_name + nlen - 4, ".hjp") == 0) {
                    hjp_found = true;
                    break;
                }
            }
            closedir(pkg_dir_handle);
        }
        
        if (!hjp_found) {
            // ビルドコマンドを決定（hajimu.json の "ビルド" → Makefile → 自動検出）
            char build_cmd[PACKAGE_MAX_PATH * 2] = {0};
            
            // はじむヘッダーのパスを自動検出
            // 実行ファイルのディレクトリから include/ を探す
            char include_dir[PACKAGE_MAX_PATH] = {0};
            {
                char self_path[PACKAGE_MAX_PATH] = {0};
                #ifdef __APPLE__
                uint32_t self_size = sizeof(self_path);
                _NSGetExecutablePath(self_path, &self_size);
                #elif defined(__linux__)
                readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
                #endif
                
                if (self_path[0]) {
                    char *last_slash = strrchr(self_path, '/');
                    if (last_slash) {
                        *last_slash = '\0';
                        snprintf(include_dir, sizeof(include_dir),
                                 "%s/include", self_path);
                        if (!dir_exists(include_dir)) {
                            include_dir[0] = '\0';
                        }
                    }
                }
            }
            
            if (has_manifest && pkg_manifest.build_cmd[0]) {
                // hajimu.json にビルドコマンドが指定されている
                if (include_dir[0]) {
                    snprintf(build_cmd, sizeof(build_cmd),
                             "cd \"%s\" && HAJIMU_INCLUDE=\"%s\" %s 2>&1",
                             pkg_dir, include_dir, pkg_manifest.build_cmd);
                } else {
                    snprintf(build_cmd, sizeof(build_cmd), "cd \"%s\" && %s 2>&1",
                             pkg_dir, pkg_manifest.build_cmd);
                }
            } else {
                // Makefile を検索
                char makefile_path[PACKAGE_MAX_PATH];
                snprintf(makefile_path, sizeof(makefile_path), "%s/Makefile", pkg_dir);
                if (file_exists(makefile_path)) {
                    if (include_dir[0]) {
                        snprintf(build_cmd, sizeof(build_cmd),
                                 "cd \"%s\" && make HAJIMU_INCLUDE=\"%s\" 2>&1",
                                 pkg_dir, include_dir);
                    } else {
                        snprintf(build_cmd, sizeof(build_cmd),
                                 "cd \"%s\" && make 2>&1", pkg_dir);
                    }
                }
            }
            
            if (build_cmd[0]) {
                printf("   🔨 ビルド中...\n");
                FILE *bp = popen(build_cmd, "r");
                if (bp) {
                    char line[1024];
                    while (fgets(line, sizeof(line), bp)) {
                        // エラーや警告を表示
                        if (strstr(line, "error") || strstr(line, "エラー") ||
                            strstr(line, "warning") || strstr(line, "警告") ||
                            strstr(line, "✅")) {
                            printf("      %s", line);
                        }
                    }
                    int bstatus = pclose(bp);
                    if (WEXITSTATUS(bstatus) == 0) {
                        printf("   ✅ ビルド成功\n");
                    } else {
                        printf("   ⚠  ビルドに失敗しました（手動で make を実行してください）\n");
                    }
                }
            } else {
                printf("   ⚠  .hjp ファイルが見つかりません\n");
                printf("      パッケージディレクトリで make を実行してください:\n");
                printf("      cd %s && make\n", pkg_dir);
            }
        }
    }
    
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
                    char manifest_path[PACKAGE_MAX_PATH];
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
    char search_paths[3][PACKAGE_MAX_PATH];
    int search_count = 0;
    
    // 1. 呼び出し元からの相対 hajimu_packages/
    if (base_dir[0]) {
        snprintf(search_paths[search_count++], PACKAGE_MAX_PATH,
                 "%s%s/%s", base_dir, PACKAGE_LOCAL_DIR, package_name);
    }
    
    // 2. CWDからの hajimu_packages/
    snprintf(search_paths[search_count++], PACKAGE_MAX_PATH,
             "%s/%s", PACKAGE_LOCAL_DIR, package_name);
    
    // 3. グローバル ~/.hajimu/packages/
    char global_dir[PACKAGE_MAX_PATH];
    get_global_package_dir(global_dir, sizeof(global_dir));
    snprintf(search_paths[search_count++], PACKAGE_MAX_PATH,
             "%s/%s", global_dir, package_name);
    
    for (int i = 0; i < search_count; i++) {
        if (!dir_exists(search_paths[i])) continue;
        
        // hajimu.json を確認してメインファイルを取得
        char manifest_path[PACKAGE_MAX_PATH];
        snprintf(manifest_path, sizeof(manifest_path), 
                 "%s/%s", search_paths[i], PACKAGE_MANIFEST_FILE);
        
        PackageManifest manifest;
        if (package_read_manifest(manifest_path, &manifest)) {
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
    }
    
    return false;
}
