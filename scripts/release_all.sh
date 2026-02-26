#!/bin/bash
# =============================================================================
# はじむエコシステム — 一括リリーススクリプト
# =============================================================================
# 使い方:
#   ./scripts/release_all.sh           全パッケージをリリース (hajimu.json のバージョン使用)
#   ./scripts/release_all.sh --push    ビルドなし、push + Release のみ
#   ./scripts/release_all.sh --only web,gui  指定パッケージのみ
#
# 前提条件:
#   - gh auth login 済み
#   - jq インストール済み (brew install jq)
#   - 各パッケージの hajimu.json にバージョン記載済み
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

JP_DIR="$BASE_DIR"
PACK_DIR="$(cd "$BASE_DIR/../jp-pack" 2>/dev/null && pwd || echo "")"
DISCORD_DIR="$(cd "$BASE_DIR/../jp-discord" 2>/dev/null && pwd || echo "")"

# ---------- パッケージ定義 ----------
# 形式: "名前:ディレクトリパス"
declare -a PACKAGES=(
  "jp:${JP_DIR}"
  "web:${PACK_DIR}/jp-web"
  "discord:${DISCORD_DIR}"
  "gui:${PACK_DIR}/jp-gui"
  "engine_core:${PACK_DIR}/jp-engine_core"
  "engine_2d:${PACK_DIR}/jp-engine_2d"
  "engine_render:${PACK_DIR}/jp-engine_render"
  "engine_audio:${PACK_DIR}/jp-engine_audio"
  "engine_rpg:${PACK_DIR}/jp-engine_rpg"
  "engine_ui:${PACK_DIR}/jp-engine_ui"
  "engine_3d:${PACK_DIR}/jp-engine_3d"
  "engine_physics:${PACK_DIR}/jp-engine_physics"
)

# ---------- 引数解析 ----------
EXTRA_ARGS=()
ONLY_FILTER=""

for arg in "$@"; do
  case "$arg" in
    --only=*)  ONLY_FILTER="${arg#--only=}" ;;
    *)         EXTRA_ARGS+=("$arg") ;;
  esac
done

# ---------- メイン処理 ----------
TOTAL=0
SUCCESS=0
FAIL=0
FAILED_NAMES=()

for entry in "${PACKAGES[@]}"; do
  IFS=: read -r name dir <<< "$entry"

  # フィルター適用
  if [[ -n "$ONLY_FILTER" ]]; then
    if [[ ! ",$ONLY_FILTER," == *",$name,"* ]]; then
      continue
    fi
  fi

  TOTAL=$((TOTAL + 1))

  if [[ ! -d "$dir" ]]; then
    echo "⚠ [$name] ディレクトリが見つかりません: $dir"
    FAIL=$((FAIL + 1))
    FAILED_NAMES+=("$name")
    continue
  fi

  if [[ ! -f "$dir/release.sh" ]]; then
    echo "⚠ [$name] release.sh が見つかりません"
    FAIL=$((FAIL + 1))
    FAILED_NAMES+=("$name")
    continue
  fi

  echo ""
  echo "================================================================"
  echo "  [$name] リリース開始"
  echo "================================================================"

  if (cd "$dir" && bash release.sh "${EXTRA_ARGS[@]}" 2>&1); then
    SUCCESS=$((SUCCESS + 1))
  else
    FAIL=$((FAIL + 1))
    FAILED_NAMES+=("$name")
  fi
done

# ---------- サマリー ----------
echo ""
echo "================================================================"
echo "  リリース結果: $SUCCESS/$TOTAL 成功"
if [[ $FAIL -gt 0 ]]; then
  echo "  失敗: ${FAILED_NAMES[*]}"
fi
echo "================================================================"

exit $FAIL
