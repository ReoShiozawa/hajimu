/**
 * はじむ - パッケージ管理システム実装
 * 
 * GitHubリポジトリからパッケージをインストール・管理する
 * libcurl を使用してダウンロード
 */

#include "package.h"
#include "sha256.h"
#include "registry_key.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#ifndef _WIN32
#  include <strings.h>
#endif
#ifndef HAJIMU_WASM
#  include <curl/curl.h>
#endif

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
#  define strcasecmp _stricmp
#  define strtok_r strtok_s
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

static bool g_allow_scripts = false;
static bool package_name_is_valid(const char *name);
static bool relative_package_path_is_safe(const char *path);
static bool is_hex_digest(const char *value, size_t length);
static int validate_manifest_data(const PackageManifest *manifest, const char *path);

void package_set_allow_scripts(bool allow) {
    g_allow_scripts = allow;
}

static bool shell_arg_is_safe(const char *s) {
    if (s == NULL || s[0] == '\0') return false;

    /* git clone は互換性のため shell 経由で実行している。
     * URL/パスに shell メタ文字が入るとコマンド注入につながるため拒否する。 */
    const char *unsafe = "\"'`$&|;<>\\\r\n";
#ifdef _WIN32
    unsafe = "\"'`$&|;<>\\^%!()\r\n";
#endif
    return strpbrk(s, unsafe) == NULL;
}

static void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) return;
    if (src == NULL) src = "";

    size_t i = 0;
    while (i + 1 < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

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
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/**
 * ファイルが存在するかチェック
 */
static bool file_exists(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static bool path_exists(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return lstat(path, &st) == 0;
#endif
}

static bool has_source_script_extension(const char *path) {
    size_t len = strlen(path);
    return (len >= 3 && strcmp(path + len - 3, ".jp") == 0 &&
            (len < 4 || path[len - 4] != 'h')) ||
           (len >= 4 && strcmp(path + len - 4, ".haj") == 0) ||
           (len >= 7 && strcmp(path + len - 7, ".hajimu") == 0);
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
#ifdef _WIN32
        DWORD attributes = GetFileAttributesA(subpath);
        bool is_directory = attributes != INVALID_FILE_ATTRIBUTES &&
                            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
        struct stat st;
        bool is_directory = lstat(subpath, &st) == 0 && S_ISDIR(st.st_mode);
#endif
        if (is_directory) {
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
#ifdef _WIN32
    DWORD root_attributes = GetFileAttributesA(path);
    if (root_attributes == INVALID_FILE_ATTRIBUTES) return -1;
    if ((root_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return (root_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? rmdir(path) : unlink(path);
    }
    if ((root_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return unlink(path);
#else
    struct stat root_st;
    if (lstat(path, &root_st) != 0) return -1;
    if (!S_ISDIR(root_st.st_mode)) return unlink(path);
#endif

    DIR *dir = opendir(path);
    if (!dir) return -1;
    
    struct dirent *entry;
    char filepath[PACKAGE_MAX_PATH];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        
#ifdef _WIN32
        DWORD attributes = GetFileAttributesA(filepath);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) rmdir(filepath);
                else unlink(filepath);
            } else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                remove_directory(filepath);
            } else {
                unlink(filepath);
            }
        }
#else
        struct stat st;
        if (lstat(filepath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_directory(filepath);
            } else {
                unlink(filepath);
            }
        }
#endif
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
    if (*p != '"' || out == NULL || max_len <= 0) return NULL;
    p++;
    int i = 0;
    while (*p && *p != '"') {
        char decoded;
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case '"': decoded = '"'; break;
                case '\\': decoded = '\\'; break;
                case 'n': decoded = '\n'; break;
                case 'r': decoded = '\r'; break;
                case 't': decoded = '\t'; break;
                default: decoded = *p; break;
            }
        } else {
            decoded = *p;
        }
        if (i < max_len - 1) out[i++] = decoded;
        p++;
    }
    out[i] = '\0';
    if (*p != '"') return NULL;
    return p + 1;
}

/**
 * JSON文字列値の比較用キーチェック
 */
static bool json_key_equals(const char *key, const char *target) {
    return strcmp(key, target) == 0;
}

static const char *json_skip_value(const char *p) {
    p = json_skip_ws(p);
    if (*p == '"') {
        char dummy[2];
        return json_parse_string(p, dummy, sizeof(dummy));
    }
    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = open == '{' ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        do {
            char ch = *p++;
            if (in_string) {
                if (escaped) escaped = false;
                else if (ch == '\\') escaped = true;
                else if (ch == '"') in_string = false;
            } else if (ch == '"') {
                in_string = true;
            } else if (ch == open) {
                depth++;
            } else if (ch == close) {
                depth--;
            }
        } while (*p && depth > 0);
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}

static const char *json_parse_integer(const char *p, int *value) {
    char *end = NULL;
    long parsed = strtol(p, &end, 10);
    if (end == p) return NULL;
    *value = (int)parsed;
    return end;
}

typedef struct {
    int major;
    int minor;
    int patch;
} PackageSemVer;

static bool semver_parse(const char *text, PackageSemVer *out) {
    if (text == NULL || out == NULL) return false;
    while (isspace((unsigned char)*text)) text++;
    if (*text == 'v' || *text == 'V') text++;
    char trailing = '\0';
    int count = sscanf(text, "%d.%d.%d%c", &out->major, &out->minor, &out->patch, &trailing);
    if (count < 1 || out->major < 0) return false;
    if (count == 1) {
        out->minor = 0;
        out->patch = 0;
    } else if (count == 2) {
        out->patch = 0;
    }
    return true;
}

static int semver_compare(PackageSemVer left, PackageSemVer right) {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    return 0;
}

static bool semver_satisfies_one(PackageSemVer version, const char *term) {
    while (isspace((unsigned char)*term)) term++;
    if (*term == '\0' || strcmp(term, "*") == 0 || strcasecmp(term, "latest") == 0) return true;

    enum { OP_EQ, OP_GT, OP_GTE, OP_LT, OP_LTE, OP_CARET, OP_TILDE } op = OP_EQ;
    if (strncmp(term, ">=", 2) == 0) { op = OP_GTE; term += 2; }
    else if (strncmp(term, "<=", 2) == 0) { op = OP_LTE; term += 2; }
    else if (*term == '>') { op = OP_GT; term++; }
    else if (*term == '<') { op = OP_LT; term++; }
    else if (*term == '^') { op = OP_CARET; term++; }
    else if (*term == '~') { op = OP_TILDE; term++; }
    else if (*term == '=') { term++; }

    PackageSemVer requested;
    if (!semver_parse(term, &requested)) return false;
    int comparison = semver_compare(version, requested);
    switch (op) {
        case OP_GT: return comparison > 0;
        case OP_GTE: return comparison >= 0;
        case OP_LT: return comparison < 0;
        case OP_LTE: return comparison <= 0;
        case OP_CARET:
            if (comparison < 0) return false;
            if (requested.major > 0) return version.major == requested.major;
            if (requested.minor > 0) return version.major == 0 && version.minor == requested.minor;
            return version.major == 0 && version.minor == 0 && version.patch == requested.patch;
        case OP_TILDE:
            return comparison >= 0 && version.major == requested.major && version.minor == requested.minor;
        case OP_EQ: return comparison == 0;
    }
    return false;
}

static bool semver_satisfies(const char *version_text, const char *requirement) {
    if (requirement == NULL || requirement[0] == '\0' || strcmp(requirement, "*") == 0) return true;
    PackageSemVer version;
    if (!semver_parse(version_text, &version)) return false;

    char copy[PACKAGE_MAX_VERSION * 2];
    safe_copy(copy, sizeof(copy), requirement);
    char *save = NULL;
    char *term = strtok_r(copy, " ,", &save);
    while (term != NULL) {
        if (!semver_satisfies_one(version, term)) return false;
        term = strtok_r(NULL, " ,", &save);
    }
    return true;
}

static bool looks_like_requirement(const char *value) {
    if (value == NULL || value[0] == '\0') return false;
    return value[0] == '^' || value[0] == '~' || value[0] == '>' ||
           value[0] == '<' || value[0] == '=' || value[0] == '*' ||
           value[0] == 'v' || isdigit((unsigned char)value[0]) ||
           strcasecmp(value, "latest") == 0;
}

static const char *json_parse_dependency(const char *p, PackageDep *dep) {
    p = json_skip_ws(p);
    safe_copy(dep->requirement, sizeof(dep->requirement), "*");
    if (*p == '"') {
        char value[PACKAGE_MAX_PATH];
        p = json_parse_string(p, value, sizeof(value));
        if (p == NULL) return NULL;
        if (looks_like_requirement(value)) safe_copy(dep->requirement, sizeof(dep->requirement), value);
        else safe_copy(dep->source, sizeof(dep->source), value);
        return p;
    }
    if (*p != '{') return NULL;
    p++;
    while (*p) {
        p = json_skip_ws(p);
        if (*p == '}') return p + 1;
        if (*p == ',') { p++; continue; }
        char key[64];
        p = json_parse_string(p, key, sizeof(key));
        if (p == NULL) return NULL;
        p = json_skip_ws(p);
        if (*p++ != ':') return NULL;
        p = json_skip_ws(p);
        if (json_key_equals(key, "source") || json_key_equals(key, "ソース")) {
            p = json_parse_string(p, dep->source, sizeof(dep->source));
        } else if (json_key_equals(key, "version") || json_key_equals(key, "バージョン")) {
            p = json_parse_string(p, dep->requirement, sizeof(dep->requirement));
        } else if (json_key_equals(key, "sha256") || json_key_equals(key, "checksum")) {
            p = json_parse_string(p, dep->checksum, sizeof(dep->checksum));
        } else {
            p = json_skip_value(p);
        }
        if (p == NULL) return NULL;
    }
    return NULL;
}

/**
 * hajimu.json を読み込み・パース
 */
bool package_read_manifest(const char *path, PackageManifest *manifest) {
    memset(manifest, 0, sizeof(PackageManifest));
    manifest->schema_version = 1;
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
        
        if (json_key_equals(key, "schema_version") || json_key_equals(key, "仕様バージョン")) {
            p = json_parse_integer(p, &manifest->schema_version);
        } else if (json_key_equals(key, "名前") || json_key_equals(key, "name")) {
            p = json_parse_string(p, manifest->name, sizeof(manifest->name));
        } else if (json_key_equals(key, "バージョン") || json_key_equals(key, "version")) {
            p = json_parse_string(p, manifest->version, sizeof(manifest->version));
        } else if (json_key_equals(key, "説明") || json_key_equals(key, "description")) {
            p = json_parse_string(p, manifest->description, sizeof(manifest->description));
        } else if (json_key_equals(key, "作者") || json_key_equals(key, "author")) {
            p = json_parse_string(p, manifest->author, sizeof(manifest->author));
        } else if (json_key_equals(key, "ライセンス") || json_key_equals(key, "license")) {
            p = json_parse_string(p, manifest->license, sizeof(manifest->license));
        } else if (json_key_equals(key, "リポジトリ") || json_key_equals(key, "repository")) {
            p = json_parse_string(p, manifest->repository, sizeof(manifest->repository));
        } else if (json_key_equals(key, "メイン") || json_key_equals(key, "main") ||
                   json_key_equals(key, "entry") || json_key_equals(key, "plugin") ||
                   json_key_equals(key, "output")) {
            p = json_parse_string(p, manifest->main_file, sizeof(manifest->main_file));
        } else if (json_key_equals(key, "ビルド") || json_key_equals(key, "build") ||
                   json_key_equals(key, "ビルドコマンド")) {
            p = json_parse_string(p, manifest->build_cmd, sizeof(manifest->build_cmd));
        } else if (json_key_equals(key, "リリース") || json_key_equals(key, "release")) {
            p = json_parse_string(p, manifest->release_url, sizeof(manifest->release_url));
        } else if (json_key_equals(key, "sha256") || json_key_equals(key, "release_sha256")) {
            p = json_parse_string(p, manifest->release_checksum, sizeof(manifest->release_checksum));
        } else if (json_key_equals(key, "依存") || json_key_equals(key, "dependencies")) {
            if (*p == '[') {
                p = json_skip_value(p);
                continue;
            }
            if (*p != '{') { p = NULL; break; }
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
                
                p = json_parse_dependency(p, dep);
                if (!p) break;
                
                manifest->dep_count++;
            }
            
            if (*p == '}') p++;
        } else {
            p = json_skip_value(p);
        }
        
        if (!p) break;
    }
    
    free(buf);
    return manifest->name[0] != '\0';
}

static void json_write_string(FILE *f, const char *value) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        switch (*p) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default: fputc(*p, f); break;
        }
    }
    fputc('"', f);
}

/**
 * hajimu.json を書き出し
 */
static bool write_manifest(const char *path, const PackageManifest *manifest) {
    /* "wb": バイナリモードで書たることで Windows の CRLF 変換を防ぎ、
     * UTF-8 JSON として常に LF 改行で保存する。 */
    char temporary_path[PACKAGE_MAX_PATH + 64];
    int path_length = snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path);
    if (path_length < 0 || path_length >= (int)sizeof(temporary_path)) return false;
    FILE *f = fopen(temporary_path, "wb");
    if (!f) return false;
    
    fprintf(f, "{\n  \"schema_version\": %d,\n", PACKAGE_MANIFEST_SCHEMA_VERSION);
    fprintf(f, "  \"name\": "); json_write_string(f, manifest->name); fprintf(f, ",\n");
    fprintf(f, "  \"version\": "); json_write_string(f, manifest->version); fprintf(f, ",\n");
    fprintf(f, "  \"description\": "); json_write_string(f, manifest->description); fprintf(f, ",\n");
    fprintf(f, "  \"author\": "); json_write_string(f, manifest->author); fprintf(f, ",\n");
    if (manifest->license[0]) {
        fprintf(f, "  \"license\": "); json_write_string(f, manifest->license); fprintf(f, ",\n");
    }
    if (manifest->repository[0]) {
        fprintf(f, "  \"repository\": "); json_write_string(f, manifest->repository); fprintf(f, ",\n");
    }
    fprintf(f, "  \"entry\": "); json_write_string(f, manifest->main_file); fprintf(f, ",\n");
    if (manifest->build_cmd[0]) {
        fprintf(f, "  \"build\": "); json_write_string(f, manifest->build_cmd); fprintf(f, ",\n");
    }
    if (manifest->release_url[0]) {
        fprintf(f, "  \"release\": "); json_write_string(f, manifest->release_url); fprintf(f, ",\n");
    }
    if (manifest->release_checksum[0]) {
        fprintf(f, "  \"release_sha256\": "); json_write_string(f, manifest->release_checksum); fprintf(f, ",\n");
    }
    fprintf(f, "  \"dependencies\": {");
    
    for (int i = 0; i < manifest->dep_count; i++) {
        if (i > 0) fprintf(f, ",");
        fprintf(f, "\n    "); json_write_string(f, manifest->deps[i].name); fprintf(f, ": {");
        bool wrote = false;
        if (manifest->deps[i].requirement[0]) {
            fprintf(f, "\"version\": "); json_write_string(f, manifest->deps[i].requirement); wrote = true;
        }
        if (manifest->deps[i].source[0]) {
            if (wrote) fprintf(f, ", ");
            fprintf(f, "\"source\": "); json_write_string(f, manifest->deps[i].source); wrote = true;
        }
        if (manifest->deps[i].checksum[0]) {
            if (wrote) fprintf(f, ", ");
            fprintf(f, "\"sha256\": "); json_write_string(f, manifest->deps[i].checksum);
        }
        fprintf(f, "}");
    }
    
    if (manifest->dep_count > 0) fprintf(f, "\n  ");
    fprintf(f, "}\n");
    fprintf(f, "}\n");
    
    if (fclose(f) != 0) {
        unlink(temporary_path);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExA(temporary_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
    if (rename(temporary_path, path) != 0) {
#endif
        unlink(temporary_path);
        return false;
    }
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

static bool download_to_file(const char *url, const char *dest_path) {
    if (url == NULL || dest_path == NULL) return false;
    if (strncmp(url, "https://", 8) != 0) {
        fprintf(stderr, "エラー: ネットワーク取得にはHTTPS URLが必要です\n");
        return false;
    }

#ifdef HAJIMU_WASM
    (void)url;
    (void)dest_path;
    return false;
#else
    CURL *curl = curl_easy_init();
    if (curl == NULL) return false;

    char temporary_path[PACKAGE_MAX_PATH + 64];
    int path_length = snprintf(temporary_path, sizeof(temporary_path), "%s.download", dest_path);
    if (path_length < 0 || path_length >= (int)sizeof(temporary_path)) {
        curl_easy_cleanup(curl);
        return false;
    }

    FILE *out = fopen(temporary_path, "wb");
    if (out == NULL) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "hajimu-package-manager/1.0");
#ifndef _WIN32
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
#endif

    CURLcode res = curl_easy_perform(curl);
    fclose(out);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        unlink(temporary_path);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExA(temporary_path, dest_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
    if (rename(temporary_path, dest_path) != 0) {
#endif
        unlink(temporary_path);
        return false;
    }
    return true;
#endif
}

// =============================================================================
// 公式レジストリとlockfile
// =============================================================================

#define PACKAGE_MAX_REGISTRY_ENTRIES 256

typedef struct {
    char name[PACKAGE_MAX_NAME];
    char version[PACKAGE_MAX_VERSION];
    char source[PACKAGE_MAX_PATH];
    char description[512];
    char checksum[PACKAGE_SHA256_HEX];
} PackageRegistryEntry;

typedef struct {
    PackageRegistryEntry entries[PACKAGE_MAX_REGISTRY_ENTRIES];
    int count;
} PackageRegistry;

typedef struct {
    char name[PACKAGE_MAX_NAME];
    char version[PACKAGE_MAX_VERSION];
    char source[PACKAGE_MAX_PATH];
    char commit[65];
    char checksum[PACKAGE_SHA256_HEX];
} PackageLockEntry;

typedef struct {
    PackageLockEntry entries[PACKAGE_MAX_DEPS];
    int count;
} PackageLock;

static char *read_text_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || length > 16L * 1024L * 1024L) { fclose(file); return NULL; }
    rewind(file);
    char *text = malloc((size_t)length + 1);
    if (text == NULL) { fclose(file); return NULL; }
    size_t actual = fread(text, 1, (size_t)length, file);
    fclose(file);
    if (actual != (size_t)length) { free(text); return NULL; }
    text[length] = '\0';
    return text;
}

static const char *json_find_object(const char *json, const char *wanted_key) {
    const char *p = json_skip_ws(json);
    if (*p++ != '{') return NULL;
    while (*p) {
        p = json_skip_ws(p);
        if (*p == '}') return NULL;
        if (*p == ',') { p++; continue; }
        char key[128];
        p = json_parse_string(p, key, sizeof(key));
        if (p == NULL) return NULL;
        p = json_skip_ws(p);
        if (*p++ != ':') return NULL;
        p = json_skip_ws(p);
        if (strcmp(key, wanted_key) == 0 && *p == '{') return p + 1;
        p = json_skip_value(p);
    }
    return NULL;
}

static bool parse_registry_entry(const char **cursor, PackageRegistryEntry *entry) {
    const char *p = json_skip_ws(*cursor);
    p = json_parse_string(p, entry->name, sizeof(entry->name));
    if (p == NULL) return false;
    p = json_skip_ws(p);
    if (*p++ != ':') return false;
    p = json_skip_ws(p);
    if (*p++ != '{') return false;

    while (*p) {
        p = json_skip_ws(p);
        if (*p == '}') { *cursor = p + 1; return true; }
        if (*p == ',') { p++; continue; }
        char key[64];
        p = json_parse_string(p, key, sizeof(key));
        if (p == NULL) return false;
        p = json_skip_ws(p);
        if (*p++ != ':') return false;
        p = json_skip_ws(p);
        if (strcmp(key, "version") == 0) p = json_parse_string(p, entry->version, sizeof(entry->version));
        else if (strcmp(key, "source") == 0) p = json_parse_string(p, entry->source, sizeof(entry->source));
        else if (strcmp(key, "description") == 0) p = json_parse_string(p, entry->description, sizeof(entry->description));
        else if (strcmp(key, "sha256") == 0) p = json_parse_string(p, entry->checksum, sizeof(entry->checksum));
        else p = json_skip_value(p);
        if (p == NULL) return false;
    }
    return false;
}

static bool registry_parse_file(const char *path, PackageRegistry *registry) {
    memset(registry, 0, sizeof(*registry));
    char *json = read_text_file(path);
    if (json == NULL) return false;
    const char *p = json_find_object(json, "packages");
    if (p == NULL) { free(json); return false; }

    while (*p) {
        p = json_skip_ws(p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }
        if (registry->count >= PACKAGE_MAX_REGISTRY_ENTRIES) { free(json); return false; }
        if (!parse_registry_entry(&p, &registry->entries[registry->count])) {
            free(json);
            return false;
        }
        PackageRegistryEntry *entry = &registry->entries[registry->count];
        PackageSemVer version;
        int source_type = classify_source(entry->source);
        if (!package_name_is_valid(entry->name) ||
            !semver_parse(entry->version, &version) ||
            source_type == 0 ||
            strncmp(entry->source, "http://", 7) == 0 ||
            (entry->checksum[0] && !is_hex_digest(entry->checksum, 64))) {
            free(json);
            return false;
        }
        for (int i = 0; i < registry->count; i++) {
            if (strcmp(registry->entries[i].name, entry->name) == 0) {
                free(json);
                return false;
            }
        }
        registry->count++;
    }
    free(json);
    return registry->count > 0;
}

static bool registry_verify(const char *path, const char *key) {
    char temporary_key[PACKAGE_MAX_PATH]={0};
    if (!key) {
#ifdef _WIN32
        char temporary_dir[MAX_PATH];
        if(!GetTempPathA(sizeof(temporary_dir),temporary_dir) || !GetTempFileNameA(temporary_dir,"hjk",0,temporary_key)) return false;
        FILE *file=fopen(temporary_key,"wb");
#else
        snprintf(temporary_key,sizeof(temporary_key),"/tmp/hajimu-registry-key.XXXXXX");
        int fd=mkstemp(temporary_key);
        FILE *file=fd<0 ? NULL : fdopen(fd,"wb");
        if(fd>=0 && !file) close(fd);
#endif
        if(!file) return false;
        fputs(hajimu_registry_public_key,file);
        fclose(file);
        bool result=registry_verify(path,temporary_key);
        unlink(temporary_key);
        return result;
    }
    char signature[PACKAGE_MAX_PATH + 64], cmd[PACKAGE_MAX_PATH * 4 + 256];
    snprintf(signature, sizeof(signature), "%s.sig", path);
    if (!shell_arg_is_safe(path) || !shell_arg_is_safe(key) || !file_exists(signature) || !file_exists(key)) return false;
    int written=snprintf(cmd,sizeof(cmd),"openssl dgst -sha256 -verify \"%s\" -signature \"%s\" \"%s\"",key,signature,path);
    if(written<0 || written>=(int)sizeof(cmd)) return false;
    FILE *pipe=popen(cmd,"r");
    if(!pipe) return false;
    char ignored[256]; while(fgets(ignored,sizeof(ignored),pipe)) {}
    int status=pclose(pipe);
    bool verified=status!=-1 && WEXITSTATUS(status)==0;
    if(!verified) fprintf(stderr,"エラー: レジストリ署名の検証に失敗しました\n");
    return verified;
}

static bool registry_load(PackageRegistry *registry) {
    const char *configured = getenv("HAJIMU_REGISTRY");
    if (configured != NULL && configured[0] != '\0') {
        if (strncmp(configured, "https://", 8) == 0) {
            char cache_dir[PACKAGE_MAX_PATH];
            char cache_path[PACKAGE_MAX_PATH + 32];
            snprintf(cache_dir, sizeof(cache_dir), "%s/.hajimu", get_home_dir());
            mkdirs(cache_dir);
            snprintf(cache_path, sizeof(cache_path), "%s/registry.json", cache_dir);
            const char *key=getenv("HAJIMU_REGISTRY_PUBLIC_KEY");
            if (!key || !key[0]) {
                fprintf(stderr,"エラー: 外部HTTPSレジストリにはHAJIMU_REGISTRY_PUBLIC_KEYを指定してください\n");
                return false;
            }
            char signature_url[PACKAGE_MAX_PATH + 64], signature_path[PACKAGE_MAX_PATH + 64];
            snprintf(signature_url,sizeof(signature_url),"%s.sig",configured);
            snprintf(signature_path,sizeof(signature_path),"%s.sig",cache_path);
            if (!download_to_file(configured, cache_path) || !download_to_file(signature_url,signature_path) || !registry_verify(cache_path,key)) return false;
            return registry_parse_file(cache_path, registry);
        }
        const char *key=getenv("HAJIMU_REGISTRY_PUBLIC_KEY");
        return (!key || !key[0] || registry_verify(configured,key)) && registry_parse_file(configured, registry);
    }

    if (file_exists("registry/index.json") && registry_verify("registry/index.json",NULL) && registry_parse_file("registry/index.json", registry)) {
        return true;
    }
    static const char *installed_registry_paths[] = {
        "/usr/local/share/hajimu/registry/index.json",
        "/opt/homebrew/share/hajimu/registry/index.json",
        "/usr/share/hajimu/registry/index.json",
        NULL
    };
    for (int i = 0; installed_registry_paths[i] != NULL; i++) {
        if (file_exists(installed_registry_paths[i]) && registry_verify(installed_registry_paths[i],NULL) &&
            registry_parse_file(installed_registry_paths[i], registry)) return true;
    }

    char cache_dir[PACKAGE_MAX_PATH];
    char cache_path[PACKAGE_MAX_PATH + 32];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.hajimu", get_home_dir());
    mkdirs(cache_dir);
    snprintf(cache_path, sizeof(cache_path), "%s/registry.json", cache_dir);
    const char *default_url =
        "https://raw.githubusercontent.com/ReoShiozawa/hajimu/main/registry/index.json";
    char signature_path[PACKAGE_MAX_PATH + 64];
    snprintf(signature_path,sizeof(signature_path),"%s.sig",cache_path);
    char signature_url[PACKAGE_MAX_PATH];
    snprintf(signature_url,sizeof(signature_url),"%s.sig",default_url);
    if(download_to_file(default_url,cache_path) && download_to_file(signature_url,signature_path) &&
       registry_verify(cache_path,NULL) && registry_parse_file(cache_path,registry)) return true;
    return file_exists(cache_path) && registry_verify(cache_path,NULL) && registry_parse_file(cache_path,registry);
}

static const PackageRegistryEntry *registry_find(const PackageRegistry *registry,
                                                 const char *name) {
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->entries[i].name, name) == 0) return &registry->entries[i];
    }
    return NULL;
}

static bool parse_lock_entry(const char **cursor, PackageLockEntry *entry) {
    const char *p = json_skip_ws(*cursor);
    p = json_parse_string(p, entry->name, sizeof(entry->name));
    if (p == NULL) return false;
    p = json_skip_ws(p);
    if (*p++ != ':') return false;
    p = json_skip_ws(p);
    if (*p++ != '{') return false;
    while (*p) {
        p = json_skip_ws(p);
        if (*p == '}') { *cursor = p + 1; return true; }
        if (*p == ',') { p++; continue; }
        char key[64];
        p = json_parse_string(p, key, sizeof(key));
        if (p == NULL) return false;
        p = json_skip_ws(p);
        if (*p++ != ':') return false;
        p = json_skip_ws(p);
        if (strcmp(key, "version") == 0) p = json_parse_string(p, entry->version, sizeof(entry->version));
        else if (strcmp(key, "source") == 0) p = json_parse_string(p, entry->source, sizeof(entry->source));
        else if (strcmp(key, "commit") == 0) p = json_parse_string(p, entry->commit, sizeof(entry->commit));
        else if (strcmp(key, "sha256") == 0) p = json_parse_string(p, entry->checksum, sizeof(entry->checksum));
        else p = json_skip_value(p);
        if (p == NULL) return false;
    }
    return false;
}

static bool lock_read(PackageLock *lock) {
    memset(lock, 0, sizeof(*lock));
    if (!file_exists(PACKAGE_LOCK_FILE)) return true;
    char *json = read_text_file(PACKAGE_LOCK_FILE);
    if (json == NULL) return false;
    const char *p = json_find_object(json, "packages");
    if (p == NULL) { free(json); return false; }
    while (*p) {
        p = json_skip_ws(p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }
        if (lock->count >= PACKAGE_MAX_DEPS || !parse_lock_entry(&p, &lock->entries[lock->count])) {
            free(json);
            return false;
        }
        PackageLockEntry *entry = &lock->entries[lock->count];
        PackageSemVer version;
        int source_type = classify_source(entry->source);
        if (!package_name_is_valid(entry->name) ||
            !semver_parse(entry->version, &version) ||
            source_type == 0 ||
            strncmp(entry->source, "http://", 7) == 0 ||
            (!is_hex_digest(entry->commit, 40) && !is_hex_digest(entry->commit, 64)) ||
            (entry->checksum[0] && !is_hex_digest(entry->checksum, 64))) {
            free(json);
            return false;
        }
        for (int i = 0; i < lock->count; i++) {
            if (strcmp(lock->entries[i].name, entry->name) == 0) {
                free(json);
                return false;
            }
        }
        lock->count++;
    }
    free(json);
    return true;
}

static PackageLockEntry *lock_find(PackageLock *lock, const char *name) {
    for (int i = 0; i < lock->count; i++) {
        if (strcmp(lock->entries[i].name, name) == 0) return &lock->entries[i];
    }
    return NULL;
}

static bool lock_write(const PackageLock *lock) {
    FILE *file = fopen(PACKAGE_LOCK_FILE ".tmp", "wb");
    if (file == NULL) return false;
    fputs("{\n  \"schema_version\": 1,\n  \"packages\": {", file);
    for (int i = 0; i < lock->count; i++) {
        const PackageLockEntry *entry = &lock->entries[i];
        if (i > 0) fputc(',', file);
        fputs("\n    ", file); json_write_string(file, entry->name); fputs(": {\n", file);
        fputs("      \"version\": ", file); json_write_string(file, entry->version); fputs(",\n", file);
        fputs("      \"source\": ", file); json_write_string(file, entry->source); fputs(",\n", file);
        fputs("      \"commit\": ", file); json_write_string(file, entry->commit);
        if (entry->checksum[0]) {
            fputs(",\n      \"sha256\": ", file); json_write_string(file, entry->checksum);
        }
        fputs("\n    }", file);
    }
    fputs(lock->count > 0 ? "\n  }\n}\n" : "}\n}\n", file);
    if (fclose(file) != 0) return false;
#ifdef _WIN32
    if (!MoveFileExA(PACKAGE_LOCK_FILE ".tmp", PACKAGE_LOCK_FILE,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
    if (rename(PACKAGE_LOCK_FILE ".tmp", PACKAGE_LOCK_FILE) != 0) {
#endif
        unlink(PACKAGE_LOCK_FILE ".tmp");
        return false;
    }
    return true;
}

static bool is_hex_digest(const char *value, size_t length) {
    if (strlen(value) != length) return false;
    for (size_t i = 0; i < length; i++) {
        if (!isxdigit((unsigned char)value[i])) return false;
    }
    return true;
}

static bool verify_file_checksum(const char *path, const char *expected) {
    if (expected == NULL || expected[0] == '\0') return false;
    char actual[PACKAGE_SHA256_HEX];
    if (!hajimu_sha256_file(path, actual)) return false;
    if (strcasecmp(actual, expected) == 0) return true;
    fprintf(stderr, "エラー: %s のSHA-256が一致しません\n", path);
    fprintf(stderr, "  期待値: %s\n", expected);
    fprintf(stderr, "  実際値: %s\n", actual);
    return false;
}

/**
 * git clone でパッケージをダウンロード
 */
static bool git_cache_path(const char *url, char *path, size_t path_size) {
    if (strncmp(url, "https://", 8) != 0) return false;
    HajimuSha256 ctx;
    unsigned char digest[32];
    char hex[65];
    static const char digits[] = "0123456789abcdef";
    hajimu_sha256_init(&ctx);
    hajimu_sha256_update(&ctx, url, strlen(url));
    hajimu_sha256_final(&ctx, digest);
    for (size_t i = 0; i < sizeof(digest); i++) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
    hex[64] = '\0';

    char cache_dir[PACKAGE_MAX_PATH];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.hajimu/cache/git", get_home_dir());
    mkdirs(cache_dir);
    int written = snprintf(path, path_size, "%s/%s.git", cache_dir, hex);
    return written > 0 && (size_t)written < path_size;
}

static int git_clone_from_cache(const char *cache, const char *dest, const char *ref) {
    if (!dir_exists(cache) || !shell_arg_is_safe(cache) || !shell_arg_is_safe(dest)) return 1;
    char cmd[PACKAGE_MAX_PATH * 3];
    int written;
    if (ref != NULL && ref[0] != '\0') {
        written = snprintf(cmd, sizeof(cmd),
            "git --git-dir=\"%s\" cat-file -e \"%s^{commit}\" 2>/dev/null && "
            "git clone -q --no-checkout \"%s\" \"%s\" && "
            "git -C \"%s\" checkout -q --detach \"%s\"",
            cache, ref, cache, dest, dest, ref);
    } else {
        written = snprintf(cmd, sizeof(cmd), "git clone -q --depth 1 \"%s\" \"%s\"",
                           cache, dest);
    }
    if (written < 0 || written >= (int)sizeof(cmd)) return 1;
    int status = system(cmd);
    return status == -1 ? 1 : WEXITSTATUS(status);
}

static void git_refresh_cache(const char *url, const char *cache) {
    if (!shell_arg_is_safe(url) || !shell_arg_is_safe(cache)) return;
    char cmd[PACKAGE_MAX_PATH * 3];
    if (dir_exists(cache)) {
        snprintf(cmd, sizeof(cmd),
                 "git --git-dir=\"%s\" fetch -q --prune origin "
                 "\"+refs/heads/*:refs/heads/*\" \"+refs/tags/*:refs/tags/*\" >/dev/null 2>&1",
                 cache);
    } else {
        snprintf(cmd, sizeof(cmd), "git clone --mirror -q \"%s\" \"%s\" >/dev/null 2>&1",
                 url, cache);
    }
    (void)system(cmd);
}

static int git_clone_at_ref(const char *url, const char *dest, const char *ref) {
    char cmd[PACKAGE_MAX_PATH * 2];

    if (!shell_arg_is_safe(url) || !shell_arg_is_safe(dest)) {
        fprintf(stderr, "エラー: パッケージURLまたは保存先パスに使用できない文字が含まれています\n");
        return 1;
    }
    if (ref != NULL && ref[0] != '\0' &&
        !is_hex_digest(ref, 40) && !is_hex_digest(ref, 64)) {
        fprintf(stderr, "エラー: lockfileのコミットIDが不正です\n");
        return 1;
    }

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

    char cache[PACKAGE_MAX_PATH] = {0};
    if (git_cache_path(url, cache, sizeof(cache)) && dir_exists(cache)) {
        /* オンラインなら更新し、失敗しても固定済みコミットのキャッシュを利用する。 */
        git_refresh_cache(url, cache);
        if (git_clone_from_cache(cache, dest, ref) == 0) {
            printf("   キャッシュから取得しました\n");
            return 0;
        }
    }

    int written;
    if (ref != NULL && ref[0] != '\0') {
        written = snprintf(cmd, sizeof(cmd),
            "git init -q \"%s\" && git -C \"%s\" remote add origin \"%s\" && "
            "git -C \"%s\" fetch --depth 1 -q origin \"%s\" && "
            "git -C \"%s\" checkout -q --detach FETCH_HEAD 2>&1",
            dest, dest, url, dest, ref, dest);
    } else {
        written = snprintf(cmd, sizeof(cmd),
                           "git clone --depth 1 -q \"%s\" \"%s\" 2>&1", url, dest);
    }
    if (written < 0 || written >= (int)sizeof(cmd)) {
        fprintf(stderr, "エラー: git clone コマンドが長すぎます\n");
        return 1;
    }

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
    int result = WEXITSTATUS(status);
    if (result == 0 && cache[0]) git_refresh_cache(url, cache);
    return result;
}

static bool git_read_commit(const char *repo_dir, char commit[65]) {
    if (!shell_arg_is_safe(repo_dir)) return false;
    char cmd[PACKAGE_MAX_PATH + 64];
    int written = snprintf(cmd, sizeof(cmd),
                           "git -C \"%s\" rev-parse HEAD 2>/dev/null", repo_dir);
    if (written < 0 || written >= (int)sizeof(cmd)) return false;
    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) return false;
    bool ok = fgets(commit, 65, pipe) != NULL;
    int status = pclose(pipe);
    if (!ok || WEXITSTATUS(status) != 0) return false;
    commit[strcspn(commit, "\r\n")] = '\0';
    return is_hex_digest(commit, 40) || is_hex_digest(commit, 64);
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

static int package_install_spec(const char *name_or_url,
                                const char *explicit_source,
                                const char *explicit_requirement,
                                const char *explicit_checksum,
                                bool add_to_project) {
    if (add_to_project) {
        PackageManifest project_manifest;
        if (!package_read_manifest(PACKAGE_MANIFEST_FILE, &project_manifest)) {
            fprintf(stderr, "エラー: %sがありません。先にpkg initを実行してください\n",
                    PACKAGE_MANIFEST_FILE);
            return 1;
        }
        if (validate_manifest_data(&project_manifest, PACKAGE_MANIFEST_FILE) != 0) return 1;
    }

    char url[PACKAGE_MAX_PATH] = {0};
    char package_name[PACKAGE_MAX_NAME] = {0};
    char requirement[PACKAGE_MAX_REQUIREMENT] = "*";
    char expected_checksum[PACKAGE_SHA256_HEX] = {0};
    char resolved_registry_version[PACKAGE_MAX_VERSION] = {0};

    if (explicit_requirement != NULL && explicit_requirement[0]) {
        safe_copy(requirement, sizeof(requirement), explicit_requirement);
    }
    if (explicit_checksum != NULL) {
        safe_copy(expected_checksum, sizeof(expected_checksum), explicit_checksum);
    }

    int source_type = explicit_source != NULL ? classify_source(explicit_source)
                                              : classify_source(name_or_url);

    if (explicit_source != NULL) {
        safe_copy(package_name, sizeof(package_name), name_or_url);
        normalize_github_url(explicit_source, url, sizeof(url));
    } else if (source_type == 1 || source_type == 2) {
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
                    safe_copy(requirement, sizeof(requirement), manifest.deps[i].requirement);
                    safe_copy(expected_checksum, sizeof(expected_checksum), manifest.deps[i].checksum);
                    break;
                }
            }
        }

        if (url[0] == '\0') {
            PackageRegistry registry;
            if (registry_load(&registry)) {
                const PackageRegistryEntry *entry = registry_find(&registry, package_name);
                if (entry != NULL) {
                    if (!semver_satisfies(entry->version, requirement)) {
                        fprintf(stderr,
                                "エラー: %s v%s は要求バージョン %s を満たしません\n",
                                package_name, entry->version, requirement);
                        return 1;
                    }
                    normalize_github_url(entry->source, url, sizeof(url));
                    safe_copy(resolved_registry_version, sizeof(resolved_registry_version), entry->version);
                    if (!expected_checksum[0]) {
                        safe_copy(expected_checksum, sizeof(expected_checksum), entry->checksum);
                    }
                }
            }
            if (url[0] == '\0') {
                fprintf(stderr, "エラー: パッケージ '%s' を公式レジストリで解決できません\n", package_name);
                fprintf(stderr, "  ソースを明示するか、HAJIMU_REGISTRYを設定してください。\n");
                return 1;
            }
        }
    }

    if (!package_name_is_valid(package_name)) {
        fprintf(stderr, "エラー: 不正なパッケージ名です: %s\n", package_name);
        return 1;
    }
    if (strncmp(url, "http://", 7) == 0) {
        fprintf(stderr, "エラー: リモートパッケージにはHTTPSを使用してください\n");
        return 1;
    }

    PackageLock lock;
    if (!lock_read(&lock)) {
        fprintf(stderr, "エラー: %s を読み込めません\n", PACKAGE_LOCK_FILE);
        return 1;
    }
    PackageLockEntry *locked = lock_find(&lock, package_name);
    const char *locked_commit = NULL;
    if (locked != NULL) {
        if (!semver_satisfies(locked->version, requirement)) {
            fprintf(stderr, "エラー: lockfileの %s v%s は要求 %s と互換性がありません\n",
                    package_name, locked->version, requirement);
            return 1;
        }
        char locked_url[PACKAGE_MAX_PATH] = {0};
        normalize_github_url(locked->source, locked_url, sizeof(locked_url));
        if (url[0] && locked_url[0] && strcmp(url,locked_url)!=0) {
            fprintf(stderr,"依存競合: %sの取得元がlockfileと一致しません\n",package_name);
            return 1;
        }
        if (locked_url[0]) safe_copy(url, sizeof(url), locked_url);
        if (locked->checksum[0]) safe_copy(expected_checksum, sizeof(expected_checksum), locked->checksum);
        locked_commit = locked->commit;
    }
    
    // ローカルパッケージディレクトリに配置
    char pkg_dir[PACKAGE_MAX_PATH];
    get_package_path(package_name, true, pkg_dir, sizeof(pkg_dir));
    
    if (dir_exists(pkg_dir)) {
        char installed_path[PACKAGE_MAX_PATH + 32];
        snprintf(installed_path, sizeof(installed_path), "%s/%s", pkg_dir, PACKAGE_MANIFEST_FILE);
        PackageManifest installed;
        if (!package_read_manifest(installed_path, &installed)) {
            fprintf(stderr, "エラー: インストール済みの%sに有効なマニフェストがありません\n",
                    package_name);
            return 1;
        }
        if (validate_manifest_data(&installed, installed_path) != 0) return 1;
        if (!semver_satisfies(installed.version, requirement)) {
            fprintf(stderr,
                    "依存競合: %sはv%sが導入済みですが、%sが要求されています\n",
                    package_name, installed.version, requirement);
            return 1;
        }
        if (locked == NULL || strcmp(locked->version, installed.version) != 0) {
            fprintf(stderr,
                    "エラー: %sは導入済みですがlockfileで固定されていません。updateを実行してください。\n",
                    package_name);
            return 1;
        }
        printf("✓ %s v%s (lockfileと一致)\n", package_name, installed.version);
        return 0;
    }
    
    // hajimu_packages ディレクトリを作成
    if (!dir_exists(PACKAGE_LOCAL_DIR)) {
        mkdirs(PACKAGE_LOCAL_DIR);
    }
    
    printf("📦 パッケージ '%s' をインストール中...\n", package_name);
    printf("   ソース: %s\n", url);
    
    // git clone
    int result = git_clone_at_ref(url, pkg_dir, locked_commit);
    if (result != 0) {
        fprintf(stderr, "エラー: パッケージ '%s' のダウンロードに失敗しました\n", package_name);
        remove_directory(pkg_dir);
        return 1;
    }
    
    char resolved_commit[65] = {0};
    if (!git_read_commit(pkg_dir, resolved_commit)) {
        fprintf(stderr, "エラー: パッケージ '%s' のコミットを特定できません\n", package_name);
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
        if (validate_manifest_data(&pkg_manifest, manifest_path) != 0) {
            remove_directory(pkg_dir);
            return 1;
        }
        if (!semver_satisfies(pkg_manifest.version, requirement)) {
            fprintf(stderr, "エラー: %s v%s は要求バージョン %s を満たしません\n",
                    package_name, pkg_manifest.version, requirement);
            remove_directory(pkg_dir);
            return 1;
        }
        if (resolved_registry_version[0] &&
            strcmp(pkg_manifest.version, resolved_registry_version) != 0) {
            fprintf(stderr,
                    "エラー: レジストリのバージョン %s と取得したマニフェスト %s が一致しません\n",
                    resolved_registry_version, pkg_manifest.version);
            remove_directory(pkg_dir);
            return 1;
        }
        printf("   パッケージ: %s v%s\n", pkg_manifest.name, pkg_manifest.version);
        if (pkg_manifest.description[0]) {
            printf("   説明: %s\n", pkg_manifest.description);
        }
        
        // 依存パッケージも再帰的にインストール
        for (int i = 0; i < pkg_manifest.dep_count; i++) {
            char dep_dir[PACKAGE_MAX_PATH];
            get_package_path(pkg_manifest.deps[i].name, true, dep_dir, sizeof(dep_dir));
            
            {
                printf("\n   → 依存パッケージ '%s' をインストール中...\n", 
                       pkg_manifest.deps[i].name);
                if (package_install_spec(pkg_manifest.deps[i].name,
                                         pkg_manifest.deps[i].source[0]
                                             ? pkg_manifest.deps[i].source : NULL,
                                         pkg_manifest.deps[i].requirement,
                                         pkg_manifest.deps[i].checksum,
                                         false) != 0) {
                    remove_directory(pkg_dir);
                    return 1;
                }
            }
        }
    } else {
        fprintf(stderr, "エラー: パッケージ '%s' に有効なhajimu.jsonがありません\n", package_name);
        remove_directory(pkg_dir);
        return 1;
    }
    
    // ポストインストールビルド:
    // パッケージの種類に応じて処理を分岐:
    //   スクリプトパッケージ (.jp): ビルド不要、そのまま使用可能
    //   バイトコードパッケージ (.hjp HJPB): ビルド済み or 自動コンパイル
    //   ネイティブ C プラグイン (.hjp DLL/dylib): ビルドまたは pre-built ダウンロード
    {
        // ── スクリプトパッケージの判定 ──
        // hajimu.json の "メイン" がソースファイルを指し、ビルドコマンドがない場合
        bool is_script_package = false;
        if (has_manifest) {
            const char *mf = pkg_manifest.main_file;
            if (has_source_script_extension(mf) && !pkg_manifest.build_cmd[0]) {
                is_script_package = true;
            }
        } else {
            // hajimu.json がない場合、main.* / <name>.* の存在でスクリプトと判定
            static const char * const source_exts[] = {".jp", ".haj", ".hajimu", NULL};
            char source_path[PACKAGE_MAX_PATH + PACKAGE_MAX_NAME];
            for (int ext_i = 0; source_exts[ext_i] != NULL && !is_script_package; ext_i++) {
                snprintf(source_path, sizeof(source_path), "%s/main%s", pkg_dir, source_exts[ext_i]);
                if (file_exists(source_path)) {
                    is_script_package = true;
                    break;
                }
                snprintf(source_path, sizeof(source_path), "%s/%s%s",
                         pkg_dir, package_name, source_exts[ext_i]);
                if (file_exists(source_path)) is_script_package = true;
            }
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
            char release_candidates[1][PACKAGE_MAX_PATH];
            int  n_candidates = 0;
            const char *release_checksum = expected_checksum;
            if (!release_checksum[0] && pkg_manifest.release_checksum[0]) {
                release_checksum = pkg_manifest.release_checksum;
            }

            /* URLとSHA-256が揃った配布物だけを取得する。latestの推測取得は再現性がないため行わない。 */
            if (has_manifest && pkg_manifest.release_url[0] && release_checksum[0]) {
                snprintf(release_candidates[n_candidates++],
                    PACKAGE_MAX_PATH, "%s", pkg_manifest.release_url);
            } else if (has_manifest && pkg_manifest.release_url[0]) {
                printf("   配布バイナリにSHA-256がないためダウンロードを省略します\n");
            }

            /* 候補を順に試す */
            for (int ci = 0; ci < n_candidates && !hjp_found; ci++) {
                char dest[PACKAGE_MAX_PATH + 32];
                snprintf(dest, sizeof(dest), "%s/%s.hjp", pkg_dir, package_name);
                printf("   🌐 ビルド済みバイナリを確認中...\n");
                if (download_to_file(release_candidates[ci], dest) && file_exists(dest) &&
                    verify_file_checksum(dest, release_checksum)) {
                    printf("   ✅ ビルド済みバイナリをダウンロードしました\n");
                    safe_copy(found_hjp, sizeof(found_hjp), dest);
                    hjp_found = true;
                    /* main を更新 */
                    PackageManifest updated_m;
                    bool got_m = package_read_manifest(manifest_path, &updated_m);
                    if (!got_m) memcpy(&updated_m, &pkg_manifest, sizeof(pkg_manifest));
                    snprintf(updated_m.main_file, sizeof(updated_m.main_file),
                             "%s.hjp", package_name);
                    if (!write_manifest(manifest_path, &updated_m)) {
                        fprintf(stderr, "エラー: ダウンロード後のマニフェストを更新できません\n");
                        unlink(dest);
                        remove_directory(pkg_dir);
                        return 1;
                    }
                    printf("   → プラグイン: %s.hjp\n", package_name);
                } else if (file_exists(dest)) {
                    unlink(dest);
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
                    safe_copy(make_args_only, sizeof(make_args_only), user_cmd + 5);
                }
            }
#endif

            if (user_cmd[0]) {
                const char *allow_env = getenv("HAJIMU_ALLOW_SCRIPTS");
                bool scripts_allowed = g_allow_scripts ||
                    (allow_env != NULL && strcmp(allow_env, "1") == 0);
                if (!scripts_allowed) {
                    fprintf(stderr,
                            "エラー: パッケージ '%s' はビルドスクリプトの実行を要求しています\n",
                            package_name);
                    fprintf(stderr,
                            "  内容を確認後、--allow-scripts を付けて再実行してください。\n");
                    remove_directory(pkg_dir);
                    return 1;
                }
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
    safe_copy(_tmp, sizeof(_tmp), (entry)); \
    /* 最大4段上まで辿る */ \
    for (int _d = 0; _d <= 4 && !msys2_root[0]; _d++) { \
        /* AppData / WindowsApps は無条件スキップ */ \
        if (strstr(_tmp, "WindowsApps") || strstr(_tmp, "AppData")) break; \
        char _probe[PACKAGE_MAX_PATH + 30]; \
        snprintf(_probe, sizeof(_probe), "%s\\mingw64\\bin\\gcc.exe", _tmp); \
        if (GetFileAttributesA(_probe) != INVALID_FILE_ATTRIBUTES) { \
            safe_copy(msys2_root, sizeof(msys2_root), _tmp); \
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
                            safe_copy(msys2_root, sizeof(msys2_root), roots[ri]);
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
                        safe_copy(msys2_dir + 2, sizeof(msys2_dir) - 2, pkg_dir + 2);
                        for (char *p = msys2_dir; *p; p++) if (*p == '\\') *p = '/';
                    } else {
                        safe_copy(msys2_dir, sizeof(msys2_dir), pkg_dir);
                        for (char *p = msys2_dir; *p; p++) if (*p == '\\') *p = '/';
                    }

                    const char *make_target = make_args_only[0] ? make_args_only : "";
                    if (win_inc_dir[0]) {
                        char msys2_inc[PACKAGE_MAX_PATH] = {0};
                        if (include_dir[1] == ':') {
                            msys2_inc[0] = '/';
                            msys2_inc[1] = (char)tolower((unsigned char)include_dir[0]);
                            safe_copy(msys2_inc + 2, sizeof(msys2_inc) - 2, include_dir + 2);
                            for (char *p = msys2_inc; *p; p++) if (*p == '\\') *p = '/';
                        } else {
                            safe_copy(msys2_inc, sizeof(msys2_inc), include_dir);
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
                        if (fgets(_t, sizeof(_t), gm)) safe_copy(make_tool, sizeof(make_tool), "make");
                        pclose(gm);
                    }
                    char final_cmd[PACKAGE_MAX_PATH] = {0};
                    if (make_args_only[0])
                        snprintf(final_cmd, sizeof(final_cmd), "%s %s", make_tool, make_args_only);
                    else if (is_make_cmd)
                        safe_copy(final_cmd, sizeof(final_cmd), make_tool);
                    else
                        safe_copy(final_cmd, sizeof(final_cmd), user_cmd);

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
                            if (!write_manifest(manifest_path, &updated_m)) {
                                free(build_log);
                                fprintf(stderr, "エラー: ビルド後のマニフェストを更新できません\n");
                                remove_directory(pkg_dir);
                                return 1;
                            }
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
                fprintf(stderr, "エラー: .hjpファイルもビルド方法も見つかりません\n");
                remove_directory(pkg_dir);
                return 1;
            }

            char final_hjp[PACKAGE_MAX_PATH] = {0};
            if (!find_hjp_recursive(pkg_dir, final_hjp, sizeof(final_hjp), 0)) {
                fprintf(stderr, "エラー: パッケージ '%s' のビルド成果物がありません\n", package_name);
                remove_directory(pkg_dir);
                return 1;
            }
        }
    }

    post_install_done:; /* スクリプトパッケージは goto でここにジャンプ */

    // lockfileへ、実際に取得したバージョン・ソース・コミットを固定する。
    PackageLock latest_lock;
    if (!lock_read(&latest_lock)) {
        fprintf(stderr, "エラー: %s を更新できません\n", PACKAGE_LOCK_FILE);
        remove_directory(pkg_dir);
        return 1;
    }
    PackageLockEntry *lock_entry = lock_find(&latest_lock, package_name);
    if (lock_entry == NULL) {
        if (latest_lock.count >= PACKAGE_MAX_DEPS) {
            fprintf(stderr, "エラー: lockfileの上限を超えました\n");
            remove_directory(pkg_dir);
            return 1;
        }
        lock_entry = &latest_lock.entries[latest_lock.count++];
        memset(lock_entry, 0, sizeof(*lock_entry));
        safe_copy(lock_entry->name, sizeof(lock_entry->name), package_name);
    }
    safe_copy(lock_entry->version, sizeof(lock_entry->version), pkg_manifest.version);
    safe_copy(lock_entry->source, sizeof(lock_entry->source), url);
    safe_copy(lock_entry->commit, sizeof(lock_entry->commit), resolved_commit);
    safe_copy(lock_entry->checksum, sizeof(lock_entry->checksum), expected_checksum);
    if (!lock_write(&latest_lock)) {
        fprintf(stderr, "エラー: %s を書き込めません\n", PACKAGE_LOCK_FILE);
        remove_directory(pkg_dir);
        return 1;
    }

    // 明示的に追加した場合だけ、プロジェクトのhajimu.jsonへ依存を記録する。
    PackageManifest project;
    if (add_to_project && package_read_manifest(PACKAGE_MANIFEST_FILE, &project)) {
        if (validate_manifest_data(&project, PACKAGE_MANIFEST_FILE) != 0) {
            fprintf(stderr, "エラー: プロジェクトマニフェストが不正なため依存を追加できません\n");
            lock_write(&lock);
            remove_directory(pkg_dir);
            return 1;
        }
        // 既に存在するかチェック
        bool exists = false;
        for (int i = 0; i < project.dep_count; i++) {
            if (strcmp(project.deps[i].name, package_name) == 0) {
                exists = true;
                break;
            }
        }
        
        if (!exists && project.dep_count >= PACKAGE_MAX_DEPS) {
            fprintf(stderr, "エラー: 依存パッケージの上限を超えました\n");
            lock_write(&lock);
            remove_directory(pkg_dir);
            return 1;
        }
        if (!exists) {
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
            if (strcmp(requirement, "*") == 0) {
                snprintf(project.deps[project.dep_count].requirement,
                         sizeof(project.deps[0].requirement), "^%s", pkg_manifest.version);
            } else {
                safe_copy(project.deps[project.dep_count].requirement,
                          sizeof(project.deps[0].requirement), requirement);
            }
            safe_copy(project.deps[project.dep_count].checksum,
                      sizeof(project.deps[0].checksum), expected_checksum);
            project.dep_count++;
            if (!write_manifest(PACKAGE_MANIFEST_FILE, &project)) {
                fprintf(stderr, "エラー: %sを更新できません\n", PACKAGE_MANIFEST_FILE);
                lock_write(&lock);
                remove_directory(pkg_dir);
                return 1;
            }
            printf("   → %s に依存を追加しました\n", PACKAGE_MANIFEST_FILE);
        }
    }
    
    printf("✓ パッケージ '%s' をインストールしました\n", package_name);
    return 0;
}

/**
 * パッケージをインストール
 */
int package_install(const char *name_or_url) {
    return package_install_spec(name_or_url, NULL, "*", NULL, true);
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
    if (validate_manifest_data(&manifest, PACKAGE_MANIFEST_FILE) != 0) return 1;
    
    if (manifest.dep_count == 0) {
        printf("依存パッケージはありません\n");
        return 0;
    }
    
    printf("📦 %d 個の依存パッケージをインストール中...\n\n", manifest.dep_count);
    
    int failed = 0;
    for (int i = 0; i < manifest.dep_count; i++) {
        if (package_install_spec(manifest.deps[i].name,
                                 manifest.deps[i].source[0]
                                     ? manifest.deps[i].source : NULL,
                                 manifest.deps[i].requirement,
                                 manifest.deps[i].checksum,
                                 false) != 0) {
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

static int package_update_one(const PackageDep *dep) {
    if (!package_name_is_valid(dep->name)) {
        fprintf(stderr, "エラー: 不正なパッケージ名です: %s\n", dep->name);
        return 1;
    }

    char package_dir[PACKAGE_MAX_PATH];
    char backup_dir[PACKAGE_MAX_PATH];
    get_package_path(dep->name, true, package_dir, sizeof(package_dir));
    snprintf(backup_dir, sizeof(backup_dir), "%s/.update-backup-%s", PACKAGE_LOCAL_DIR, dep->name);
    if (dir_exists(backup_dir)) remove_directory(backup_dir);

    PackageLock original_lock;
    if (!lock_read(&original_lock)) {
        fprintf(stderr, "エラー: %sを読み込めません\n", PACKAGE_LOCK_FILE);
        return 1;
    }
    PackageLock unlocked = original_lock;
    for (int i = 0; i < unlocked.count; i++) {
        if (strcmp(unlocked.entries[i].name, dep->name) == 0) {
            for (int j = i; j < unlocked.count - 1; j++) unlocked.entries[j] = unlocked.entries[j + 1];
            unlocked.count--;
            break;
        }
    }

    bool had_installed = dir_exists(package_dir);
    if (had_installed && rename(package_dir, backup_dir) != 0) {
        fprintf(stderr, "エラー: 更新前の%sを退避できません\n", dep->name);
        return 1;
    }
    if (!lock_write(&unlocked)) {
        if (had_installed) rename(backup_dir, package_dir);
        fprintf(stderr, "エラー: lockfileを更新できません\n");
        return 1;
    }

    int result = package_install_spec(dep->name,
                                      dep->source[0] ? dep->source : NULL,
                                      dep->requirement,
                                      dep->checksum,
                                      false);
    if (result == 0) {
        if (had_installed) remove_directory(backup_dir);
        printf("更新完了: %s\n", dep->name);
        return 0;
    }

    if (dir_exists(package_dir)) remove_directory(package_dir);
    if (had_installed) rename(backup_dir, package_dir);
    lock_write(&original_lock);
    fprintf(stderr, "更新に失敗したため%sを元の状態へ戻しました\n", dep->name);
    return 1;
}

int package_update(const char *name) {
    PackageManifest project;
    if (!package_read_manifest(PACKAGE_MANIFEST_FILE, &project)) {
        fprintf(stderr, "エラー: %sを読み込めません\n", PACKAGE_MANIFEST_FILE);
        return 1;
    }
    if (validate_manifest_data(&project, PACKAGE_MANIFEST_FILE) != 0) return 1;
    int matched = 0;
    int failed = 0;
    for (int i = 0; i < project.dep_count; i++) {
        if (name != NULL && name[0] && strcmp(project.deps[i].name, name) != 0) continue;
        matched++;
        if (package_update_one(&project.deps[i]) != 0) failed++;
    }
    if (matched == 0) {
        fprintf(stderr, "エラー: 更新対象の依存パッケージがありません\n");
        return 1;
    }
    return failed == 0 ? 0 : 1;
}

/**
 * パッケージを削除
 */
int package_remove(const char *name) {
    if (!package_name_is_valid(name)) {
        fprintf(stderr, "エラー: 不正なパッケージ名です: %s\n", name != NULL ? name : "");
        return 1;
    }
    char pkg_dir[PACKAGE_MAX_PATH];
    get_package_path(name, true, pkg_dir, sizeof(pkg_dir));
    
    if (!path_exists(pkg_dir)) {
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

    PackageLock lock;
    if (lock_read(&lock)) {
        for (int i = 0; i < lock.count; i++) {
            if (strcmp(lock.entries[i].name, name) == 0) {
                for (int j = i; j < lock.count - 1; j++) lock.entries[j] = lock.entries[j + 1];
                lock.count--;
                lock_write(&lock);
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

static bool package_name_is_valid(const char *name) {
    if (name == NULL || name[0] == '\0' || strlen(name) >= PACKAGE_MAX_NAME) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || name[0] == '.') return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) return false;
    }
    return true;
}

static bool relative_package_path_is_safe(const char *path) {
    if (path == NULL || path[0] == '\0' || path[0] == '/' || path[0] == '\\') return false;
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return false;

    const char *component = path;
    for (const char *p = path;; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch != '\0' && ch < 0x20) return false;
        if (ch == '/' || ch == '\\' || ch == '\0') {
            size_t length = (size_t)(p - component);
            if (length == 0 || (length == 1 && component[0] == '.') ||
                (length == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if (ch == '\0') break;
            component = p + 1;
        }
    }
    return true;
}

static bool requirement_is_valid(const char *requirement) {
    if (requirement == NULL || requirement[0] == '\0' || strcmp(requirement, "*") == 0 ||
        strcasecmp(requirement, "latest") == 0) return true;
    char copy[PACKAGE_MAX_REQUIREMENT];
    safe_copy(copy, sizeof(copy), requirement);
    char *save = NULL;
    for (char *term = strtok_r(copy, " ,", &save); term != NULL;
         term = strtok_r(NULL, " ,", &save)) {
        while (*term == '>' || *term == '<' || *term == '=' || *term == '^' || *term == '~') term++;
        PackageSemVer parsed;
        if (!semver_parse(term, &parsed)) return false;
    }
    return true;
}

static int validate_manifest_data(const PackageManifest *manifest, const char *path) {
    int errors = 0;
    if (manifest->schema_version < 1 || manifest->schema_version > PACKAGE_MANIFEST_SCHEMA_VERSION) {
        fprintf(stderr, "%s: 未対応のschema_versionです: %d\n", path, manifest->schema_version);
        errors++;
    }
    if (!package_name_is_valid(manifest->name)) {
        fprintf(stderr, "%s: nameは英数字、'.'、'_'、'-'のみ使用できます\n", path);
        errors++;
    }
    PackageSemVer version;
    if (!semver_parse(manifest->version, &version)) {
        fprintf(stderr, "%s: versionはSemVer形式で指定してください\n", path);
        errors++;
    }
    if (manifest->main_file[0] == '\0') {
        fprintf(stderr, "%s: entryがありません\n", path);
        errors++;
    } else if (!relative_package_path_is_safe(manifest->main_file)) {
        fprintf(stderr, "%s: entryはパッケージ内の相対パスで指定してください\n", path);
        errors++;
    }
    if (manifest->release_url[0] && strncmp(manifest->release_url, "https://", 8) != 0) {
        fprintf(stderr, "%s: releaseにはHTTPS URLを使用してください\n", path);
        errors++;
    }
    if (manifest->release_checksum[0] && !is_hex_digest(manifest->release_checksum, 64)) {
        fprintf(stderr, "%s: release_sha256は64桁の16進数で指定してください\n", path);
        errors++;
    }
    for (int i = 0; i < manifest->dep_count; i++) {
        const PackageDep *dep = &manifest->deps[i];
        if (!package_name_is_valid(dep->name)) {
            fprintf(stderr, "%s: 不正な依存パッケージ名です: %s\n", path, dep->name);
            errors++;
        }
        if (!requirement_is_valid(dep->requirement)) {
            fprintf(stderr, "%s: %sのバージョン制約が不正です: %s\n",
                    path, dep->name, dep->requirement);
            errors++;
        }
        if (dep->source[0] && classify_source(dep->source) == 1 &&
            strncmp(dep->source, "http://", 7) == 0) {
            fprintf(stderr, "%s: %sのsourceにはHTTPSを使用してください\n", path, dep->name);
            errors++;
        }
        if (dep->checksum[0] && !is_hex_digest(dep->checksum, 64)) {
            fprintf(stderr, "%s: %sのsha256が不正です\n", path, dep->name);
            errors++;
        }
        for (int j = i + 1; j < manifest->dep_count; j++) {
            if (strcmp(dep->name, manifest->deps[j].name) == 0) {
                fprintf(stderr, "%s: 依存パッケージ%sが重複しています\n", path, dep->name);
                errors++;
            }
        }
    }
    return errors;
}

int package_validate(const char *path) {
    const char *target = path != NULL && path[0] ? path : PACKAGE_MANIFEST_FILE;
    PackageManifest manifest;
    if (!package_read_manifest(target, &manifest)) {
        fprintf(stderr, "エラー: %sを読み込めません\n", target);
        return 1;
    }
    int errors = validate_manifest_data(&manifest, target);
    if (errors > 0) {
        fprintf(stderr, "検証失敗: %d件の問題があります\n", errors);
        return 1;
    }
    printf("検証成功: %s (%s v%s)\n", target, manifest.name, manifest.version);
    return 0;
}

static bool contains_ignore_case(const char *text, const char *query) {
    if (query == NULL || query[0] == '\0') return true;
    size_t query_length = strlen(query);
    for (; *text; text++) {
        size_t i = 0;
        while (i < query_length && text[i] &&
               tolower((unsigned char)text[i]) == tolower((unsigned char)query[i])) i++;
        if (i == query_length) return true;
    }
    return false;
}

int package_search(const char *query) {
    PackageRegistry registry;
    if (!registry_load(&registry)) {
        fprintf(stderr, "エラー: 公式パッケージレジストリを読み込めません\n");
        return 1;
    }
    int matches = 0;
    for (int i = 0; i < registry.count; i++) {
        const PackageRegistryEntry *entry = &registry.entries[i];
        if (!contains_ignore_case(entry->name, query) &&
            !contains_ignore_case(entry->description, query)) continue;
        printf("%-24s %10s  %s\n", entry->name, entry->version, entry->description);
        matches++;
    }
    if (matches == 0) printf("一致するパッケージはありません\n");
    return 0;
}

int package_audit(void) {
    PackageManifest project;
    if (!package_read_manifest(PACKAGE_MANIFEST_FILE, &project)) {
        fprintf(stderr, "エラー: %sを読み込めません\n", PACKAGE_MANIFEST_FILE);
        return 1;
    }
    int problems = validate_manifest_data(&project, PACKAGE_MANIFEST_FILE);
    PackageLock lock;
    if (!lock_read(&lock)) {
        fprintf(stderr, "エラー: %sが壊れています\n", PACKAGE_LOCK_FILE);
        return 1;
    }

    for (int i = 0; i < project.dep_count; i++) {
        PackageDep *dep = &project.deps[i];
        PackageLockEntry *locked = lock_find(&lock, dep->name);
        if (locked == NULL) {
            fprintf(stderr, "未固定: %sは%sにありません\n", dep->name, PACKAGE_LOCK_FILE);
            problems++;
            continue;
        }
        if (!semver_satisfies(locked->version, dep->requirement)) {
            fprintf(stderr, "不整合: %s v%sは要求%sを満たしません\n",
                    dep->name, locked->version, dep->requirement);
            problems++;
        }
        if (!is_hex_digest(locked->commit, 40) && !is_hex_digest(locked->commit, 64)) {
            fprintf(stderr, "不整合: %sのコミットIDが不正です\n", dep->name);
            problems++;
        }
        char package_dir[PACKAGE_MAX_PATH];
        get_package_path(dep->name, true, package_dir, sizeof(package_dir));
        if (!dir_exists(package_dir)) {
            fprintf(stderr, "未インストール: %s\n", dep->name);
            problems++;
            continue;
        }
        char manifest_path[PACKAGE_MAX_PATH + 32];
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s", package_dir, PACKAGE_MANIFEST_FILE);
        PackageManifest installed;
        if (!package_read_manifest(manifest_path, &installed) ||
            validate_manifest_data(&installed, manifest_path) != 0 ||
            strcmp(installed.version, locked->version) != 0) {
            fprintf(stderr, "不整合: %sのインストール版とlockfileが一致しません\n", dep->name);
            problems++;
        }
    }

    /* 直接依存から到達する推移依存も監査する。循環は訪問済み配列で打ち切る。 */
    bool reachable[PACKAGE_MAX_DEPS]={false};
    for(int i=0;i<lock.count;i++) for(int j=0;j<project.dep_count;j++)
        if(strcmp(lock.entries[i].name,project.deps[j].name)==0) reachable[i]=true;
    for(int pass=0;pass<lock.count;pass++) {
        bool changed=false;
        for(int i=0;i<lock.count;i++) {
            if(!reachable[i]) continue;
            char manifest_path[PACKAGE_MAX_PATH+PACKAGE_MAX_NAME+32];
            snprintf(manifest_path,sizeof(manifest_path),"%s/%s/%s",PACKAGE_LOCAL_DIR,lock.entries[i].name,PACKAGE_MANIFEST_FILE);
            PackageManifest installed;
            if(!package_read_manifest(manifest_path,&installed)) continue;
            for(int j=0;j<installed.dep_count;j++) for(int k=0;k<lock.count;k++) {
                if(strcmp(installed.deps[j].name,lock.entries[k].name)==0 && !reachable[k]) { reachable[k]=true;changed=true; }
            }
        }
        if(!changed) break;
    }
    for(int i=0;i<lock.count;i++) {
        if(!reachable[i]) { fprintf(stderr,"未宣言: %sは依存グラフから到達できません\n",lock.entries[i].name);problems++;continue; }
        char manifest_path[PACKAGE_MAX_PATH+PACKAGE_MAX_NAME+32];
        snprintf(manifest_path,sizeof(manifest_path),"%s/%s/%s",PACKAGE_LOCAL_DIR,lock.entries[i].name,PACKAGE_MANIFEST_FILE);
        PackageManifest installed;
        if(!package_read_manifest(manifest_path,&installed) || strcmp(installed.version,lock.entries[i].version)!=0) {
            fprintf(stderr,"不整合: %sの推移依存が導入状態と一致しません\n",lock.entries[i].name);problems++;continue;
        }
        for(int j=0;j<installed.dep_count;j++) {
            PackageLockEntry *dependency=lock_find(&lock,installed.deps[j].name);
            if(!dependency || !semver_satisfies(dependency->version,installed.deps[j].requirement)) {
                fprintf(stderr,"依存競合: %sが要求する%sを満たしません\n",installed.name,installed.deps[j].name);problems++;
            }
        }
    }

    if (problems > 0) {
        fprintf(stderr, "監査失敗: %d件の問題があります\n", problems);
        return 1;
    }
    printf("監査成功: %d個の依存は固定され、インストール状態と一致しています\n",
           project.dep_count);
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
    if (!package_name_is_valid(package_name) || resolved_path == NULL || max_len <= 0) {
        return false;
    }
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
        if (package_read_manifest(manifest_path_r, &manifest) &&
            relative_package_path_is_safe(manifest.main_file)) {
            snprintf(resolved_path, max_len, "%s/%s",
                     search_paths[i], manifest.main_file);
            if (file_exists(resolved_path)) return true;
        }

        // ソーススクリプトを試す (.jp / .haj / .hajimu)
        static const char * const source_exts[] = {".jp", ".haj", ".hajimu", NULL};
        for (int ext_i = 0; source_exts[ext_i] != NULL; ext_i++) {
            snprintf(resolved_path, max_len, "%s/main%s",
                     search_paths[i], source_exts[ext_i]);
            if (file_exists(resolved_path)) return true;

            snprintf(resolved_path, max_len, "%s/%s%s",
                     search_paths[i], package_name, source_exts[ext_i]);
            if (file_exists(resolved_path)) return true;
        }

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
