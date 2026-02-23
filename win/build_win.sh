#!/usr/bin/env bash
# win/build_win.sh
# はじむ言語 Windows 向けビルドスクリプト
#
# 使い方:
#   bash win/build_win.sh           # hajimu.exe + DLL だけビルド
#   bash win/build_win.sh --installer  # さらに NSIS インストーラーも生成
#
# 必要なツール:
#   brew install mingw-w64
#   brew install nsis  (--installer 指定時)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

INSTALLER=0
for arg in "$@"; do
    case "$arg" in
        --installer|-i) INSTALLER=1 ;;
        --help|-h)
            echo "使い方: $0 [--installer]"
            echo "  --installer  NSIS インストーラー (.exe) も生成する"
            exit 0 ;;
    esac
done

# ── 前提ツールの確認 ──────────────────────────────────────────
echo "=== はじむ言語 Windows ビルド ==="
echo ""

if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "エラー: MinGW-w64 が必要です。"
    echo "  brew install mingw-w64"
    exit 1
fi

if [ "$INSTALLER" -eq 1 ] && ! command -v makensis >/dev/null 2>&1; then
    echo "エラー: NSIS が必要です。"
    echo "  brew install nsis"
    exit 1
fi

echo "コンパイラ: $(x86_64-w64-mingw32-gcc --version | head -1)"
echo ""

# ── curl for Windows のセットアップ ──────────────────────────
echo "[1/4] curl for Windows のセットアップ..."
bash win/setup_curl.sh
CURL_DIR="$REPO_ROOT/win/curl-win64"
echo ""

# ── Windows 向けコンパイル ────────────────────────────────────
echo "[2/4] Windows 向けコンパイル..."
mkdir -p win/build win/dist

CC="x86_64-w64-mingw32-gcc"
CFLAGS="-Wall -Wextra -std=c11 -O2 -D_WIN32_WINNT=0x0601 -I${CURL_DIR}/include"
LDFLAGS="-L${CURL_DIR}/lib -lcurl -lws2_32 -lwsock32 -lpthread -lshell32 -lm -static-libgcc"

SOURCES=(
    src/main.c
    src/lexer.c
    src/ast.c
    src/parser.c
    src/value.c
    src/environment.c
    src/evaluator.c
    src/diag.c
    src/http.c
    src/async.c
    src/package.c
    src/plugin.c
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
    obj="win/build/$(basename "${src%.c}.o")"
    echo "  コンパイル: $src"
    $CC $CFLAGS -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done

echo "  リンク: win/dist/hajimu.exe"
$CC "${OBJECTS[@]}" -o win/dist/hajimu.exe $LDFLAGS
echo ""

# ── DLL コピー ─────────────────────────────────────────────
echo "[3/4] 依存 DLL をコピー..."
bash win/copy_dlls.sh win/dist "$CURL_DIR"
echo ""

# ── インストーラー生成 ────────────────────────────────────
if [ "$INSTALLER" -eq 1 ]; then
    echo "[4/4] NSIS インストーラー生成..."
    makensis -NOCD win/installer.nsi
    echo ""
    echo "=============================="
    echo "完了: win/dist/hajimu_setup.exe"
    echo "=============================="
else
    echo "[4/4] インストーラー生成をスキップ（.exe だけの配布物）"
    echo ""
    echo "=============================="
    echo "完了!"
    echo ""
    echo "配布ファイル (win/dist/):"
    ls win/dist/
    echo ""
    echo "NSIS インストーラーも生成する場合:"
    echo "  brew install nsis && bash win/build_win.sh --installer"
    echo "=============================="
fi
