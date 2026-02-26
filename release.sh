#!/bin/bash
# =============================================================================
# はじむ — ローカルビルド + GitHub Release スクリプト
# =============================================================================
# 使い方:
#   ./release.sh v1.3.1          macOS ビルド → タグ → GitHub Release
#   ./release.sh v1.3.1 --win    macOS + Windows クロスコンパイル
#   ./release.sh v1.3.1 --push   ビルドなし、コード push + タグ + Release のみ
# =============================================================================
set -euo pipefail

cd "$(dirname "$0")"

# ---------- 引数解析 ----------
VERSION="${1:-}"
CROSS_WIN=false
PUSH_ONLY=false

for arg in "$@"; do
  case "$arg" in
    --win)  CROSS_WIN=true ;;
    --push) PUSH_ONLY=true ;;
  esac
done

if [[ -z "$VERSION" ]]; then
  echo "使い方: ./release.sh <バージョン> [--win] [--push]"
  echo ""
  echo "  <バージョン>   v1.3.1 形式 (v は自動付与)"
  echo "  --win          Windows クロスコンパイルも行う (mingw 必要)"
  echo "  --push         ビルドせず push + Release 作成のみ"
  exit 1
fi

[[ "$VERSION" == v* ]] || VERSION="v$VERSION"

echo "=== はじむ $VERSION リリース ==="

# ---------- gh CLI チェック ----------
if ! command -v gh >/dev/null 2>&1; then
  echo "⚠ gh CLI が見つかりません (brew install gh)"
  echo "  ビルドとタグは行いますが Release 作成はスキップします"
  HAS_GH=false
else
  HAS_GH=true
fi

# ---------- ビルド ----------
mkdir -p dist

if [[ "$PUSH_ONLY" == false ]]; then
  echo "--- macOS ビルド ---"
  make clean
  make release
  cp nihongo "dist/nihongo-macos"
  echo "  → dist/nihongo-macos"

  if [[ "$CROSS_WIN" == true ]]; then
    if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
      echo "--- Windows クロスコンパイル ---"
      make windows-installer 2>/dev/null || make windows
      cp win/dist/hajimu.exe dist/ 2>/dev/null || true
      cp win/dist/hajimu_setup.exe dist/ 2>/dev/null || true
      echo "  → dist/hajimu.exe, dist/hajimu_setup.exe"
    else
      echo "⚠ mingw が見つかりません — Windows ビルドをスキップ"
    fi
  fi
fi

# ---------- Git タグ & Push ----------
echo "--- Git push ---"
git add -A
git diff --cached --quiet || git commit -m "release: $VERSION"
git push origin HEAD

if git rev-parse "$VERSION" >/dev/null 2>&1; then
  echo "  タグ $VERSION は既に存在します"
else
  git tag -a "$VERSION" -m "Release $VERSION"
  echo "  タグ作成: $VERSION"
fi
git push origin "$VERSION"

# ---------- GitHub Release ----------
if [[ "$HAS_GH" == true ]]; then
  # dist/ 内のファイルをアップロード
  ASSETS=()
  for f in dist/*; do
    [[ -f "$f" ]] && ASSETS+=("$f")
  done

  if [[ ${#ASSETS[@]} -gt 0 ]]; then
    echo "--- GitHub Release 作成 ---"
    gh release create "$VERSION" "${ASSETS[@]}" \
      --title "はじむ $VERSION" \
      --generate-notes 2>/dev/null || \
    gh release upload "$VERSION" "${ASSETS[@]}" --clobber 2>/dev/null || \
    echo "⚠ Release 作成/アップロードに失敗 (手動で対応してください)"
  fi
fi

echo ""
echo "=== リリース完了: $VERSION ==="
