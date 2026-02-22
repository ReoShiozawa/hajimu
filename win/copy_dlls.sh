#!/usr/bin/env bash
# win/copy_dlls.sh
# hajimu.exe が依存する DLL を dist/ にコピーする。
# Usage: copy_dlls.sh <dist_dir> <curl_win64_dir>

set -euo pipefail

DIST_DIR="${1:-win/dist}"
CURL_DIR="${2:-win/curl-win64}"

# ── MinGW-w64 ランタイム DLL の検索パス ─────────────────────
# brew install mingw-w64 でインストールされた場所
MINGW_BIN=$(dirname "$(command -v x86_64-w64-mingw32-gcc)")
# homebrew が用意する DLL は GCC ディレクトリの隣にある場合も
MINGW_SYSROOT=$(x86_64-w64-mingw32-gcc --print-sysroot 2>/dev/null || true)
MINGW_LIB=""
for candidate in \
    "$MINGW_BIN" \
    "$MINGW_SYSROOT/bin" \
    "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/bin" \
    "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/bin" \
    "/usr/lib/gcc/x86_64-w64-mingw32" \
    "$(x86_64-w64-mingw32-gcc --print-file-name=libwinpthread-1.dll 2>/dev/null | xargs dirname 2>/dev/null || true)"; do
    if [ -d "$candidate" ] && ls "$candidate"/*.dll >/dev/null 2>&1; then
        MINGW_LIB="$candidate"
        break
    fi
done

mkdir -p "$DIST_DIR"

copy_dll() {
    local name="$1"
    local found=0
    # curl ディレクトリを先に見る
    for search in "$CURL_DIR/bin" "$MINGW_LIB" "$MINGW_BIN" \
                  "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/bin" \
                  "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/bin"; do
        if [ -f "$search/$name" ]; then
            cp "$search/$name" "$DIST_DIR/"
            echo "  コピー: $search/$name"
            found=1
            break
        fi
    done
    if [ "$found" -eq 0 ]; then
        echo "  警告: $name が見つかりません (Windows マシン上にある場合は不要)"
    fi
}

echo "[copy_dlls] 必要な DLL を $DIST_DIR/ にコピー中..."

# libcurl
copy_dll "libcurl-x64.dll"
# MinGW-w64 ランタイム (静的リンクの場合は不要だが念のため)
for dll in libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll; do
    copy_dll "$dll" || true
done

# curl が依存する zlib / libssl 等 (オプション)
for dll in zlib1.dll libssl-3-x64.dll libcrypto-3-x64.dll; do
    if ls "$CURL_DIR/bin/$dll" >/dev/null 2>&1; then
        copy_dll "$dll"
    fi
done

echo "[copy_dlls] 完了。"
echo ""
echo "配布パッケージ (${DIST_DIR}/):"
ls "$DIST_DIR/"
