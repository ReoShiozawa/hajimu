/**
 * はじむ - C拡張プラグインシステム実装
 * 
 * 統一拡張子 .hjp によるクロスプラットフォーム対応。
 * macOS/Linux: dlopen/dlsym
 * Windows: LoadLibrary/GetProcAddress
 */

#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// =============================================================================
// プラットフォーム抽象化レイヤー
// =============================================================================

#ifdef _WIN32
  #include <windows.h>

  /* UTF-8 パス → UTF-16 変換して LoadLibraryW で読み込む。
   * LoadLibraryA を使うと Windows の ANSI コードページ変換が入り
   * 日本語パス / スペース含むパスで失敗する。 */
  static void *platform_dlopen(const char *utf8_path) {
      WCHAR wpath[4096];
      int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1,
                                     wpath, (int)(sizeof(wpath)/sizeof(wpath[0])));
      if (wlen == 0) {
          /* 変換失敗時はフォールバックとして LoadLibraryA を試みる */
          return (void *)LoadLibraryA(utf8_path);
      }
      return (void *)LoadLibraryW(wpath);
  }

  static void *platform_dlsym(void *handle, const char *symbol) {
      return (void *)GetProcAddress((HMODULE)handle, symbol);
  }

  static void platform_dlclose(void *handle) {
      FreeLibrary((HMODULE)handle);
  }

  static const char *platform_dlerror(void) {
      static char buf[512];
      /* GetLastError() は他の Win32 呼び出しでクリアされるため即座に保存 */
      DWORD err = GetLastError();
      if (err == 0) return NULL;
      DWORD r = FormatMessageA(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          buf, (DWORD)(sizeof(buf) - 1), NULL);
      if (r == 0) {
          snprintf(buf, sizeof(buf), "Win32 error code: %lu", (unsigned long)err);
      } else {
          /* 末尾の改行を除去 */
          size_t blen = strlen(buf);
          while (blen > 0 && (buf[blen-1] == '\r' || buf[blen-1] == '\n')) buf[--blen] = '\0';
      }
      return buf;
  }

  static const char *get_home_dir_plugin(void) {
      const char *home = getenv("USERPROFILE");
      if (home == NULL) home = getenv("HOMEPATH");
      return home;
  }
#else
  #include <dlfcn.h>
  #include <unistd.h>

  static void *platform_dlopen(const char *path) {
      return dlopen(path, RTLD_NOW | RTLD_LOCAL);
  }

  static void *platform_dlsym(void *handle, const char *symbol) {
      return dlsym(handle, symbol);
  }

  static void platform_dlclose(void *handle) {
      dlclose(handle);
  }

  static const char *platform_dlerror(void) {
      return dlerror();
  }

  static const char *get_home_dir_plugin(void) {
      const char *home = getenv("HOME");
      return home;
  }
#endif

// =============================================================================
// ユーティリティ
// =============================================================================

static bool file_exists_plugin(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * パスに .hjp 拡張子が付いているか
 */
static bool has_hjp_extension(const char *path) {
    if (path == NULL) return false;
    size_t len = strlen(path);
    if (len < HJP_EXTENSION_LEN) return false;
    return strcmp(path + len - HJP_EXTENSION_LEN, HJP_EXTENSION) == 0;
}

/**
 * 名前に .hjp を付加したパスを生成
 * 既に .hjp 付きならそのままコピー
 */
static void ensure_hjp_extension(const char *name, char *out, int out_size) {
    if (has_hjp_extension(name)) {
        snprintf(out, out_size, "%s", name);
    } else {
        snprintf(out, out_size, "%s%s", name, HJP_EXTENSION);
    }
}

// =============================================================================
// プラグインマネージャ
// =============================================================================

void plugin_manager_init(PluginManager *mgr) {
    mgr->plugins = NULL;
    mgr->count = 0;
    mgr->capacity = 0;
}

void plugin_manager_free(PluginManager *mgr) {
    if (mgr == NULL) return;
    
    for (int i = 0; i < mgr->count; i++) {
        LoadedPlugin *p = &mgr->plugins[i];
        free(p->path);
        free(p->name);
        if (p->handle) {
            platform_dlclose(p->handle);
        }
    }
    free(mgr->plugins);
    mgr->plugins = NULL;
    mgr->count = 0;
    mgr->capacity = 0;
}

// =============================================================================
// .hjp 判定
// =============================================================================

bool plugin_is_hjp(const char *path) {
    return has_hjp_extension(path);
}

// =============================================================================
// プラグインの検索
// =============================================================================

LoadedPlugin *plugin_find(PluginManager *mgr, const char *name) {
    for (int i = 0; i < mgr->count; i++) {
        if (strcmp(mgr->plugins[i].name, name) == 0) {
            return &mgr->plugins[i];
        }
    }
    return NULL;
}

// =============================================================================
// .hjp パス解決
// =============================================================================

bool plugin_resolve_hjp(const char *name, const char *caller,
                        char *out, int out_size) {
    char hjp_name[1024];
    ensure_hjp_extension(name, hjp_name, sizeof(hjp_name));
    
    char try_path[2048];  /* プレフィックス+パッケージ名で1024超えの可能性があるため2048 */
    
    // 1. 呼び出し元ファイルからの相対パス
    if (caller != NULL) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", caller);
        char *sep = strrchr(dir, '/');
#ifdef _WIN32
        char *sep2 = strrchr(dir, '\\');
        if (sep2 > sep) sep = sep2;
#endif
        if (sep) {
            *(sep + 1) = '\0';
            snprintf(try_path, sizeof(try_path), "%s%s", dir, hjp_name);
            if (file_exists_plugin(try_path)) {
                snprintf(out, out_size, "%s", try_path);
                return true;
            }
        }
    }
    
    // 2. CWD基準
    if (file_exists_plugin(hjp_name)) {
        snprintf(out, out_size, "%s", hjp_name);
        return true;
    }
    
    // 3. hajimu_packages/ 内の .hjp を検索
    //    hajimu_packages/<name>.hjp
    //    hajimu_packages/<name>/<name>.hjp
    snprintf(try_path, sizeof(try_path), "hajimu_packages/%s", hjp_name);
    if (file_exists_plugin(try_path)) {
        snprintf(out, out_size, "%s", try_path);
        return true;
    }
    
    // パッケージ名部分を抽出（拡張子を除く）
    char base_name[256];
    snprintf(base_name, sizeof(base_name), "%s", name);
    // .hjp が付いていたら除去
    char *ext_pos = strstr(base_name, HJP_EXTENSION);
    if (ext_pos) *ext_pos = '\0';
    
    snprintf(try_path, sizeof(try_path), "hajimu_packages/%s/%s", base_name, hjp_name);
    if (file_exists_plugin(try_path)) {
        snprintf(out, out_size, "%s", try_path);
        return true;
    }

    /* ビルド出力サブディレクトリも検索: hajimu_packages/<name>/{build,dist,lib,bin}/<name>.hjp
     * 多くのパッケージは make の出力先としてこれらのサブディレクトリを使う */
    {
        static const char * const build_subdirs[] = {"build", "dist", "lib", "bin", NULL};
        for (int _sd = 0; build_subdirs[_sd] != NULL; _sd++) {
            snprintf(try_path, sizeof(try_path), "hajimu_packages/%s/%s/%s",
                     base_name, build_subdirs[_sd], hjp_name);
            if (file_exists_plugin(try_path)) {
                snprintf(out, out_size, "%s", try_path);
                return true;
            }
        }
    }

    // 4. グローバルプラグインディレクトリ: ~/.hajimu/plugins/
    //    ~/.hajimu/plugins/<name>.hjp
    //    ~/.hajimu/plugins/<name>/<name>.hjp
    const char *home = get_home_dir_plugin();
    if (home != NULL) {
        snprintf(try_path, sizeof(try_path), "%s/.hajimu/plugins/%s", home, hjp_name);
        if (file_exists_plugin(try_path)) {
            snprintf(out, out_size, "%s", try_path);
            return true;
        }
        
        snprintf(try_path, sizeof(try_path), "%s/.hajimu/plugins/%s/%s",
                 home, base_name, hjp_name);
        if (file_exists_plugin(try_path)) {
            snprintf(out, out_size, "%s", try_path);
            return true;
        }
    }
    
    return false;
}

// =============================================================================
// プラグインの読み込み
// =============================================================================

bool plugin_load(PluginManager *mgr, const char *path, HajimuPluginInfo **info_out) {
    if (info_out) *info_out = NULL;
    
    // 既に読み込み済みかチェック
    for (int i = 0; i < mgr->count; i++) {
        if (strcmp(mgr->plugins[i].path, path) == 0) {
            if (info_out) *info_out = mgr->plugins[i].info;
            return true;
        }
    }
    
    // 共有ライブラリを開く
    void *handle = platform_dlopen(path);
    if (handle == NULL) {
        const char *err = platform_dlerror();
        fprintf(stderr, "エラー: プラグインを読み込めません: %s\n", path);
        if (err) fprintf(stderr, "  詳細: %s\n", err);
#ifdef _WIN32
        /* Windows では .hjp は DLL (PE 形式)。macOS/Linux 向けに
         * ビルドされた .hjp を LoadLibraryW しようとすると
         * ERROR_BAD_EXE_FORMAT (193) が返る。その場合は案内を出す。 */
        {
            DWORD _winerr = GetLastError();
            if (_winerr == 193 /* ERROR_BAD_EXE_FORMAT */ ||
                _winerr == 216 /* ERROR_EXE_MACHINE_TYPE_MISMATCH */) {
                fprintf(stderr, "  ヒント: この .hjp は Windows 用にビルドされていません。\n");
                fprintf(stderr, "  Windows 上でパッケージを削除して再インストールしてください:\n");
                fprintf(stderr, "    hajimu pkg remove <パッケージ名>\n");
                fprintf(stderr, "    hajimu pkg add <ユーザー/リポジトリ>\n");
            }
        }
#endif
        return false;
    }
    
    // 初期化関数を検索
#ifndef _WIN32
    dlerror(); // エラーをクリア（POSIX専用）
#endif
    HajimuPluginInitFn init_fn = (HajimuPluginInitFn)platform_dlsym(handle, HAJIMU_PLUGIN_INIT_SYMBOL);
    const char *sym_error = platform_dlerror();
    if (sym_error != NULL || init_fn == NULL) {
        fprintf(stderr, "エラー: プラグインに '%s' 関数が見つかりません: %s\n",
                HAJIMU_PLUGIN_INIT_SYMBOL, path);
        if (sym_error) fprintf(stderr, "  詳細: %s\n", sym_error);
        platform_dlclose(handle);
        return false;
    }
    
    // プラグインを初期化
    HajimuPluginInfo *info = init_fn();
    if (info == NULL) {
        fprintf(stderr, "エラー: プラグインの初期化に失敗しました: %s\n", path);
        platform_dlclose(handle);
        return false;
    }
    
    if (info->name == NULL || info->name[0] == '\0') {
        fprintf(stderr, "エラー: プラグイン名が設定されていません: %s\n", path);
        platform_dlclose(handle);
        return false;
    }
    
    // 配列を拡張
    if (mgr->count >= mgr->capacity) {
        mgr->capacity = mgr->capacity == 0 ? 4 : mgr->capacity * 2;
        mgr->plugins = realloc(mgr->plugins, mgr->capacity * sizeof(LoadedPlugin));
    }
    
    // プラグインを登録
    LoadedPlugin *p = &mgr->plugins[mgr->count++];
    p->path = strdup(path);
    p->name = strdup(info->name);
    p->handle = handle;
    p->info = info;
    
    if (info_out) *info_out = info;
    
    return true;
}

// =============================================================================
// ランタイムコールバック注入
// =============================================================================

void plugin_set_runtime(LoadedPlugin *plugin, HajimuRuntime *rt) {
    if (!plugin || !plugin->handle || !rt) return;
    
    HajimuPluginSetRuntimeFn set_fn = (HajimuPluginSetRuntimeFn)
        platform_dlsym(plugin->handle, HAJIMU_PLUGIN_SET_RUNTIME_SYMBOL);
    if (set_fn) {
        set_fn(rt);
    }
}
