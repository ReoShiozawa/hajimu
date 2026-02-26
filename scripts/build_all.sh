#!/bin/bash
# =============================================================================
# はじむエコシステム — 一括ビルド & Push スクリプト
# =============================================================================
# 使い方:
#   ./scripts/build_all.sh            全パッケージをローカルビルド + push
#   ./scripts/build_all.sh --no-push  ビルドのみ (push しない)
#   ./scripts/build_all.sh --only web,discord  指定パッケージのみ
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# プロジェクトをグループ II に先に配置し、jp 関連のディレクトリ推定
# jp は $BASE_DIR 自身
JP_DIR="$BASE_DIR"

# jp-pack は隣接ディレクトリまたは jp-pack/ 配下
PACK_DIR="$(cd "$BASE_DIR/../jp-pack" 2>/dev/null && pwd || echo "")"
DISCORD_DIR="$(cd "$BASE_DIR/../jp-discord" 2>/dev/null && pwd || echo "")"

# ---------- パッケージ定義 ----------
# 形式: "名前:ディレクトリパス:ビルドコマンド:成果物パス"
declare -a PACKAGES=(
  "jp:${JP_DIR}:make:nihongo"
  "web:${PACK_DIR}/jp-web:make:hajimu_web.hjp"
  "discord:${DISCORD_DIR}:make:hajimu_discord.hjp"
  "gui:${PACK_DIR}/jp-gui:make:hajimu_gui.hjp"
  "engine_core:${PACK_DIR}/jp-engine_core:make:build/engine_core.hjp"
  "engine_2d:${PACK_DIR}/jp-engine_2d:make:build/engine_2d.hjp"
  "engine_render:${PACK_DIR}/jp-engine_render:make:build/engine_render.hjp"
  "engine_audio:${PACK_DIR}/jp-engine_audio:make:build/engine_audio.hjp"
  "engine_rpg:${PACK_DIR}/jp-engine_rpg:make:build/engine_rpg.hjp"
  "engine_ui:${PACK_DIR}/jp-engine_ui:make:build/engine_ui.hjp"
  "engine_3d:${PACK_DIR}/jp-engine_3d:make:build/engine_3d.hjp"
  "engine_physics:${PACK_DIR}/jp-engine_physics:make:build/engine_physics.hjp"
)

# ---------- 引数解析 ----------
DO_PUSH=true
ONLY_FILTER=""

for arg in "$@"; do
  case "$arg" in
    --no-push) DO_PUSH=false ;;
    --only)    shift; ONLY_FILTER="$1" ;;
    --only=*)  ONLY_FILTER="${arg#--only=}" ;;
  esac
done

# ---------- メイン処理 ----------
TOTAL=0
SUCCESS=0
FAIL=0
FAILED_NAMES=()

for entry in "${PACKAGES[@]}"; do
  IFS=: read -r name dir cmd artifact <<< "$entry"

  # フィルター適用
  if [[ -n "$ONLY_FILTER" ]]; then
    if [[ ! ",$ONLY_FILTER," == *",$name,"* ]]; then
      continue
    fi
  fi

  TOTAL=$((TOTAL + 1))

  # ディレクトリ存在チェック
  if [[ ! -d "$dir" ]]; then
    echo "⚠ [$name] ディレクトリが見つかりません: $dir"
    FAIL=$((FAIL + 1))
    FAILED_NAMES+=("$name")
    continue
  fi

  echo ""
  echo "================================================================"
  echo "  [$name] ビルド開始"
  echo "================================================================"

  (
    cd "$dir"

    # ビルド
    if $cmd; then
      echo "  ✓ [$name] ビルド成功"

      # 成果物の確認
      if [[ -f "$artifact" ]]; then
        echo "  → $artifact ($(du -h "$artifact" | cut -f1))"
      else
        echo "  ⚠ 成果物が見つかりません: $artifact"
      fi

      # Push
      if [[ "$DO_PUSH" == true ]]; then
        git add -A
        if ! git diff --cached --quiet; then
          git commit -m "build: ローカルビルド更新 ($(date +%Y-%m-%d))"
          git push origin HEAD
          echo "  ✓ [$name] push 完了"
        else
          echo "  [$name] 変更なし — push スキップ"
        fi
      fi
    else
      echo "  ✗ [$name] ビルド失敗"
      exit 1
    fi
  ) && SUCCESS=$((SUCCESS + 1)) || { FAIL=$((FAIL + 1)); FAILED_NAMES+=("$name"); }
done

# ---------- サマリー ----------
echo ""
echo "================================================================"
echo "  結果: $SUCCESS/$TOTAL 成功"
if [[ $FAIL -gt 0 ]]; then
  echo "  失敗: ${FAILED_NAMES[*]}"
fi
echo "================================================================"

exit $FAIL
