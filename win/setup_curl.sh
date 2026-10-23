#!/usr/bin/env bash
# win/setup_curl.sh
# Windows用curlを、公式配布物の固定ハッシュを検証して導入する。
# make windows を実行すると自動的に呼ばれる。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="$SCRIPT_DIR/curl-win64"

# ── バージョン設定 ────────────────────────────────────────────
CURL_VER="8.23.0_5"
CURL_BASE="curl-${CURL_VER}-win64-mingw"
CURL_URL="https://curl.se/windows/dl-${CURL_VER}/${CURL_BASE}.zip"
# https://curl.se/windows/dl-8.23.0_5/curl-8.23.0_5-win64-mingw.zip.txt
CURL_SHA256="b0af3896433242213f6d1388a104115e6dfdd3b16727e5b00c2ab0f48fbc904a"

# ── 既にセットアップ済みなら終了 ─────────────────────────────
if [[ -z "${HAJIMU_CURL_ARCHIVE:-}" && -f "$DEST/.archive-sha256" ]] &&
   [[ "$(<"$DEST/.archive-sha256")" == "$CURL_SHA256" ]] &&
   [[ -f "$DEST/lib/libcurl.dll.a" && -f "$DEST/include/curl/curl.h" && -f "$DEST/bin/libcurl-x64.dll" ]]; then
    echo "[setup_curl] curl ${CURL_VER} は導入済みです。"
    exit 0
fi

# 作業用ディレクトリ内で取得・検証を完了するまで、既存環境には触らない。
TMP_DIR=$(mktemp -d "$SCRIPT_DIR/.curl-setup.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT
ARCHIVE="$TMP_DIR/curl.zip"
if [[ -n "${HAJIMU_CURL_ARCHIVE:-}" ]]; then
    # オフライン入力も同じ固定ハッシュで検証する。
    cp "$HAJIMU_CURL_ARCHIVE" "$ARCHIVE"
elif command -v curl >/dev/null 2>&1; then
    curl --proto '=https' --proto-redir '=https' --tlsv1.2 \
        --connect-timeout 20 --max-time 180 --retry 2 \
        --fail --location --silent --show-error -o "$ARCHIVE" "$CURL_URL"
elif command -v wget >/dev/null 2>&1; then
    wget --https-only --timeout=60 --tries=3 -q -O "$ARCHIVE" "$CURL_URL"
else
    echo "エラー: curl または wget が必要です。" >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    ACTUAL=$(sha256sum "$ARCHIVE" | awk '{print $1}')
else
    ACTUAL=$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')
fi
if [[ "$ACTUAL" != "$CURL_SHA256" ]]; then
    echo "エラー: curl配布物のSHA-256が一致しません。展開せず中止します。" >&2
    exit 1
fi

unzip -q "$ARCHIVE" -d "$TMP_DIR/extracted"
EXTRACTED="$TMP_DIR/extracted/$CURL_BASE"
for file in include/curl/curl.h lib/libcurl.dll.a bin/libcurl-x64.dll COPYING.txt; do
    if [[ ! -f "$EXTRACTED/$file" ]]; then
        echo "エラー: curl配布物に必須ファイルがありません: $file" >&2
        exit 1
    fi
done

NEW="$TMP_DIR/install"
mkdir -p "$NEW/include" "$NEW/lib" "$NEW/bin" "$NEW/licenses"
cp -R "$EXTRACTED/include/curl" "$NEW/include/"
cp "$EXTRACTED/lib/libcurl.dll.a" "$NEW/lib/"
cp "$EXTRACTED/bin/libcurl-x64.dll" "$NEW/bin/"
cp "$EXTRACTED/COPYING.txt" "$NEW/licenses/curl.txt"
cp -R "$EXTRACTED/dep" "$NEW/licenses/dependencies"
printf '%s\n' "$CURL_SHA256" > "$NEW/.archive-sha256"

BACKUP=''
if [[ -e "$DEST" || -L "$DEST" ]]; then
    # 既存環境は削除せず退避し、導入失敗時は元へ戻す。
    BACKUP=$(mktemp -d "$SCRIPT_DIR/.curl-backup.XXXXXX")
    mv "$DEST" "$BACKUP/curl-win64"
fi
if ! mv "$NEW" "$DEST"; then
    [[ -z "$BACKUP" ]] || mv "$BACKUP/curl-win64" "$DEST"
    exit 1
fi
echo "[setup_curl] curl ${CURL_VER} のハッシュ検証と導入が完了しました。"
