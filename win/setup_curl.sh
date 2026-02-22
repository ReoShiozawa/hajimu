#!/usr/bin/env bash
# win/setup_curl.sh
# curl for Windows (MinGW-w64 x64) をダウンロードして win/curl-win64/ に配置する。
# make windows を実行すると自動的に呼ばれる。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="$SCRIPT_DIR/curl-win64"

# ── バージョン設定 ────────────────────────────────────────────
# curl.se/windows/ の最新安定版に合わせて更新してください
CURL_VER="8.12.1_1"
CURL_BASE="curl-${CURL_VER}-win64-mingw"
CURL_URL="https://curl.se/windows/dl-${CURL_VER}/${CURL_BASE}.tar.xz"
CURL_ARCHIVE="$SCRIPT_DIR/${CURL_BASE}.tar.xz"

# ── 既にセットアップ済みなら終了 ─────────────────────────────
if [ -f "$DEST/lib/libcurl.dll.a" ] && [ -d "$DEST/include/curl" ]; then
    echo "[setup_curl] curl-win64/ はすでに存在します。スキップします。"
    exit 0
fi

# ── ダウンロード ────────────────────────────────────────────
echo "[setup_curl] curl for Windows をダウンロード中..."
echo "  URL: $CURL_URL"

if command -v curl >/dev/null 2>&1; then
    curl -L --fail --progress-bar -o "$CURL_ARCHIVE" "$CURL_URL"
elif command -v wget >/dev/null 2>&1; then
    wget -q --show-progress -O "$CURL_ARCHIVE" "$CURL_URL"
else
    echo "エラー: curl または wget が必要です。" >&2
    exit 1
fi

# ── 展開 ───────────────────────────────────────────────────
echo "[setup_curl] 展開中..."
TMP_DIR="$SCRIPT_DIR/_curl_tmp"
mkdir -p "$TMP_DIR"
tar -xf "$CURL_ARCHIVE" -C "$TMP_DIR"
rm -f "$CURL_ARCHIVE"

# アーカイブ内のディレクトリを探す（バージョンにかかわらず動く）
EXTRACTED=$(find "$TMP_DIR" -maxdepth 1 -type d | grep -v "^$TMP_DIR$" | head -1)
if [ -z "$EXTRACTED" ]; then
    echo "エラー: 展開されたディレクトリが見つかりません。" >&2
    exit 1
fi

# ── 必要なファイルだけ curl-win64/ にコピー ────────────────
mkdir -p "$DEST/include" "$DEST/lib" "$DEST/bin"

# ヘッダー
cp -r "$EXTRACTED/include/curl" "$DEST/include/"

# ライブラリ (import lib)
if [ -f "$EXTRACTED/lib/libcurl.dll.a" ]; then
    cp "$EXTRACTED/lib/libcurl.dll.a" "$DEST/lib/"
fi
# DLL (配布時に同梱)
if [ -f "$EXTRACTED/bin/libcurl-x64.dll" ]; then
    cp "$EXTRACTED/bin/libcurl-x64.dll" "$DEST/bin/"
elif ls "$EXTRACTED/bin/"libcurl*.dll >/dev/null 2>&1; then
    cp "$EXTRACTED/bin/"libcurl*.dll "$DEST/bin/"
fi

# クリーンアップ
rm -rf "$TMP_DIR"

echo "[setup_curl] 完了: $DEST/"
echo "  include/curl/*.h"
echo "  lib/libcurl.dll.a"
echo "  bin/libcurl-x64.dll  (配布時に hajimu.exe と同梱)"
