/**
 * win_compat2.h — evaluator.c 向け Windows 互換レイヤー
 *
 * windows.h を直接 include せず、dirent・usleep・realpath・setenv
 * など evaluator.c に必要なPOSIX APIのみをエミュレートする。
 *
 * このヘッダーを lexer.h（TokenType 定義）の「後」に include すれば
 * winnt.h の TokenType との衝突が起きない。
 *
 * ポイント:
 *   - FindFirstFile / HANDLE が必要な箇所のみ <fileapi.h> を使う
 *   - WIN32_LEAN_AND_MEAN + windows.h は include しない
 */

#ifndef WIN_COMPAT2_H
#define WIN_COMPAT2_H

#ifdef _WIN32

/* MinGW-w64 最小ヘッダーセット (windows.h を避ける) */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
/* TokenType の衝突を事前に防ぐ: winnt.h の enum は無視させる */
/* winnt.h は TOKEN_INFORMATION_CLASS を enum で定義しているが
 * _TOKEN_INFORMATION_CLASS の forward declaration では回避できない。
 * そこで winnt.h を include する前に TokenType を typedef しておく
 * 方法が確実。しかし evaluator.h → lexer.h 経由で定義済みのため
 * winnt.h を後から include するとエラーになる。
 *
 * 解決策: windows.h 系を include しない / fileapi.h 等を直接使う。 */

#include <direct.h>      /* _mkdir, _getcwd */
#include <io.h>          /* _access, _findfirst など (旧API) */
#include <sys/stat.h>    /* _stat */

/* ── mkdir の互換マクロ ─────────────────────────────────── */
#ifndef mkdir
#  define mkdir(path, mode)  _mkdir(path)
#endif

/* ── usleep ─────────────────────────────────────────────── */
#ifndef usleep
/* windows.h を include せず WinBase.h だけ取り込む */
#  ifndef _WINBASE_
extern __declspec(dllimport) void __stdcall Sleep(unsigned long dwMilliseconds);
#  endif
static inline void win_usleep2(unsigned int us) {
    Sleep((us + 999u) / 1000u);
}
#  define usleep(us)  win_usleep2(us)
#endif

/* ── realpath のエミュレーション ───────────────────────────
 * _fullpath(resolved, path, MAX_PATH) が相当する。  */
#ifndef realpath
#  include <stdlib.h>  /* _fullpath */
static inline char *win_realpath(const char *path, char *resolved) {
    static char buf[4096];
    char *out = resolved ? resolved : buf;
    return _fullpath(out, path, resolved ? 4096 : sizeof(buf));
}
#  define realpath(path, resolved)  win_realpath(path, resolved)
#endif

/* ── setenv のエミュレーション ─────────────────────────────
 * Windows には setenv() がないので _putenv_s() を使う。     */
#ifndef setenv
static inline int win_setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name) != NULL) return 0;
    return _putenv_s(name, value);
}
#  define setenv(name, value, overwrite)  win_setenv(name, value, overwrite)
#endif

/* ── dirent エミュレーション ────────────────────────────────
 * windows.h の FindFirstFile / FindNextFile を使わず
 * MinGW-w64 の <io.h> にある _findfirst / _findnext を使う。  */
#include <io.h>  /* struct _finddata_t, _findfirst, _findnext, _findclose */

struct hajimu_dirent { char d_name[260]; };

typedef struct {
    intptr_t               handle;
    struct _finddata_t     fd;
    int                    first;
    struct hajimu_dirent   entry;
} HAJIMU_DIR;

static inline HAJIMU_DIR *win_opendir2(const char *path) {
    char pat[1024];
    snprintf(pat, sizeof(pat), "%s\\*", path);
    HAJIMU_DIR *d = (HAJIMU_DIR*)calloc(1, sizeof(HAJIMU_DIR));
    if (!d) return NULL;
    d->handle = _findfirst(pat, &d->fd);
    if (d->handle == (intptr_t)-1) { free(d); return NULL; }
    d->first = 1;
    return d;
}

static inline struct hajimu_dirent *win_readdir2(HAJIMU_DIR *d) {
    if (!d || d->handle == (intptr_t)-1) return NULL;
    if (d->first) {
        d->first = 0;
    } else if (_findnext(d->handle, &d->fd) != 0) {
        return NULL;
    }
    strncpy(d->entry.d_name, d->fd.name, sizeof(d->entry.d_name) - 1);
    d->entry.d_name[sizeof(d->entry.d_name) - 1] = '\0';
    return &d->entry;
}

static inline void win_closedir2(HAJIMU_DIR *d) {
    if (d) {
        if (d->handle != (intptr_t)-1) _findclose(d->handle);
        free(d);
    }
}

/* 既存の POSIX 名をエミュレーション版に置き換え */
#define DIR          HAJIMU_DIR
#define dirent       hajimu_dirent
#define opendir(p)   win_opendir2(p)
#define readdir(d)   win_readdir2(d)
#define closedir(d)  win_closedir2(d)

#endif /* _WIN32 */
#endif /* WIN_COMPAT2_H */
