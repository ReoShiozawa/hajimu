/**
 * win_compat.h - Windows (MinGW-w64) 互換レイヤー
 *
 * http.c / async.c からインクルードされる。
 * windows.h → winnt.h の TokenType 汚染を main.c に持ち込まないよう、
 * このヘッダーは http.c / async.c のみでインクルードすること。
 *
 * 提供するもの:
 *   - Winsock2 ソケット API の POSIX 風ラッパー
 *   - usleep / gettimeofday / isatty
 *   - strndup
 *   - WSAStartup / WSACleanup (コンストラクタ関数で自動実行)
 */

#ifndef WIN_COMPAT_H
#define WIN_COMPAT_H

#ifdef _WIN32

/* Winsock2 は windows.h より先にインクルードする必要がある */
/* winnt.h に "TokenType" という enum 値があり、lexer.h の typedef enum
 * TokenType と衝突するため、インクルード前後でマクロで保護する */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#define TokenType  _winnt_TokenType_collision_guard_
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#undef TokenType
#include <io.h>
#include <process.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── socket fd 型 ─────────────────────────────────────────── */
/* MinGW では SOCKET は unsigned (UINT_PTR)、fd は int 型が混在する */
/* wincompat では "int fd" を受け取るラッパーをインライン定義する  */

/* close(fd) → closesocket() */
static inline int win_close_socket(int fd)
{
    return closesocket((SOCKET)fd);
}
#define close(fd)   win_close_socket(fd)

/* ── usleep ──────────────────────────────────────────────── */
static inline int usleep(unsigned int usec)
{
    DWORD ms = (usec + 999) / 1000;
    if (ms == 0) ms = 1;
    Sleep(ms);
    return 0;
}

/* ── gettimeofday ────────────────────────────────────────── */
#ifndef _TIMEVAL_DEFINED
#  define _TIMEVAL_DEFINED
/* ws2tcpip.h / winsock2.h が struct timeval を定義している場合がある    */
/* 重複定義を避けるため #ifndef で保護する                               */
#endif

static inline int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (!tv) return 0;
    /* 1601-01-01 から 1970-01-01 までの 100 ns ティック数 */
    static const ULONGLONG EPOCH = 116444736000000000ULL;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t -= EPOCH;
    tv->tv_sec  = (long)(t / 10000000ULL);
    tv->tv_usec = (long)((t % 10000000ULL) / 10);
    return 0;
}

/* ── isatty / fileno ─────────────────────────────────────── */
#define isatty(fd)  _isatty(fd)
#define fileno(f)   _fileno(f)

/* ── SIGPIPE (Windows には存在しない) ───────────────────── */
#ifndef SIGPIPE
#  define SIGPIPE 13
#endif

/* ── strndup ─────────────────────────────────────────────── */
#ifndef strndup
static inline char *win_strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *p = (char *)malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}
#  define strndup(s, n)  win_strndup((s), (n))
#endif

/* ── strcasestr ──────────────────────────────────────────── */
#ifndef strcasestr
static inline char *win_strcasestr(const char *haystack, const char *needle)
{
    if (!needle || !*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (_strnicmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
    }
    return NULL;
}
#  define strcasestr(h, n)  win_strcasestr((h), (n))
#endif

/* ── WSAStartup / WSACleanup ──────────────────────────────── */
/* GCC の constructor 属性を使い、main() より前に自動的に初期化する。   */
/* http.c か async.c がこのヘッダーをインクルードした翻訳ユニットで     */
/* 一度だけ実行される。                                                  */
static void win_wsa_init(void) __attribute__((constructor));
static void win_wsa_init(void)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

static void win_wsa_cleanup(void) __attribute__((destructor));
static void win_wsa_cleanup(void)
{
    WSACleanup();
}

/* ── コンソール UTF-8 設定 ─────────────────────────── */
/* Windows コンソールを UTF-8 モードに設定し、2文化けを防ぐ。         */
/* また ANSI/VT100 エスケープシーケンス（色彩表示）も有効化する。   */
static void win_console_setup(void) __attribute__((constructor));
static void win_console_setup(void)
{
    /* CP_UTF8 = 65001 */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    /* ENABLE_VIRTUAL_TERMINAL_PROCESSING (= 0x0004):
     * Windows Terminal および VT100対応ターミナルで ANSI エスケープを使えるようにする */
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hout, &mode))
            SetConsoleMode(hout, mode | 0x0004);
    }
    HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
    if (herr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(herr, &mode))
            SetConsoleMode(herr, mode | 0x0004);
    }
}

#endif /* _WIN32 */

#endif /* WIN_COMPAT_H */
