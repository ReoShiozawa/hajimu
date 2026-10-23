#!/usr/bin/env bash
set -u

# はじむ関連リポジトリを横断し、公開前に必要な最低限の状態を確認する。
# 未コミット変更は利用者の作業として扱い、既定では報告だけを行う。

core_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
workspace_root=$(dirname "$core_root")
strict_clean=0
scan_secrets=0

while (($# > 0)); do
  case "$1" in
    --strict-clean) strict_clean=1 ;;
    --secrets) scan_secrets=1 ;;
    -h|--help)
      printf '%s\n' \
        '使用方法: scripts/check-ecosystem.sh [--strict-clean] [--secrets]' \
        '  --strict-clean  未コミット変更も失敗として扱う' \
        '  --secrets       gitleaksで履歴と作業ツリーを検査する'
      exit 0
      ;;
    *)
      printf '不明なオプション: %s\n' "$1" >&2
      exit 2
      ;;
  esac
  shift
done

repos=()
repo_identities=()
add_repo() {
  local candidate=$1
  [[ -d "$candidate/.git" ]] || return 0
  candidate=$(CDPATH= cd -- "$candidate" && pwd -P)
  local identity
  if [[ $(uname -s) == Darwin ]]; then identity=$(stat -f '%d:%i' "$candidate");
  else identity=$(stat -c '%d:%i' "$candidate"); fi
  local existing_identity
  for existing_identity in "${repo_identities[@]:-}"; do
    [[ "$existing_identity" == "$identity" ]] && return 0
  done
  repos+=("$candidate")
  repo_identities+=("$identity")
}

for name in jp jp-extensions jp-edu jp-study Kaname kaname jp-document jp-examples jp-discord; do
  add_repo "$workspace_root/$name"
done
if [[ -d "$workspace_root/jp-pack" ]]; then
  for candidate in "$workspace_root"/jp-pack/*; do
    [[ -d "$candidate" ]] && add_repo "$candidate"
  done
fi

failures=0
warnings=0
dirty_repos=0
gitleaks_bin=''

if ((scan_secrets)); then
  if command -v gitleaks >/dev/null 2>&1; then
    gitleaks_bin=$(command -v gitleaks)
  elif [[ -x "$HOME/go/bin/gitleaks" ]]; then
    gitleaks_bin="$HOME/go/bin/gitleaks"
  else
    printf 'gitleaksが見つかりません。--secretsを実行できません。\n' >&2
    exit 2
  fi
fi

printf 'はじむエコシステム監査: %d repositories\n\n' "${#repos[@]}"
printf '%-24s %-24s %8s %s\n' 'repository' 'branch' 'changes' 'result'

for repo in "${repos[@]}"; do
  repo_name=${repo#"$workspace_root"/}
  branch=$(git -C "$repo" branch --show-current 2>/dev/null)
  [[ -n "$branch" ]] || branch='(detached)'
  dirty_count=$(git -C "$repo" status --porcelain=v1 --untracked-files=all | wc -l | tr -d ' ')
  repo_result='ok'

  if ((dirty_count > 0)); then
    ((dirty_repos += 1))
    if ((strict_clean)); then
      repo_result='failed: dirty'
      ((failures += 1))
    else
      repo_result='ok: dirty reported'
      ((warnings += 1))
    fi
  fi

  while IFS= read -r remote_url; do
    [[ -n "$remote_url" ]] || continue
    if [[ "$remote_url" =~ ^https?://[^/]*@ ]] ||
       [[ "$remote_url" =~ (ghp_|github_pat_|glpat-|AKIA)[A-Za-z0-9_-]+ ]]; then
      repo_result='failed: credential in remote URL'
      ((failures += 1))
    fi
  done < <(git -C "$repo" config --get-regexp '^remote\..*\.url$' 2>/dev/null | awk '{print $2}')

  if [[ ! -x "$repo/.githooks/pre-commit" ]] || [[ ! -f "$repo/.pre-commit-config.yaml" ]]; then
    repo_result='failed: secret hook missing'
    ((failures += 1))
  fi

  hooks_path=$(git -C "$repo" config --get core.hooksPath 2>/dev/null || true)
  if [[ "$hooks_path" != '.githooks' ]]; then
    ((warnings += 1))
    [[ "$repo_result" == 'ok' ]] && repo_result='ok: hook not activated'
  fi

  if [[ ! -f "$repo/.github/workflows/security.yml" ]]; then
    if [[ ! -f "$repo/.gitlab-ci.yml" ]] ||
       ! grep -q 'Secret-Detection.gitlab-ci.yml' "$repo/.gitlab-ci.yml"; then
      repo_result='failed: CI secret scan missing'
      ((failures += 1))
    fi
  fi

  manifest="$repo/hajimu.json"
  if [[ -f "$manifest" ]]; then
    if ! jq -e '.schema_version == 2' "$manifest" >/dev/null 2>&1; then
      repo_result='failed: manifest schema'
      ((failures += 1))
    elif [[ -x "$core_root/nihongo" ]] &&
         ! "$core_root/nihongo" pkg validate "$manifest" >/dev/null 2>&1; then
      repo_result='failed: manifest validation'
      ((failures += 1))
    fi
  fi

  if ((scan_secrets)); then
    scan_tree=$(mktemp -d "${TMPDIR:-/tmp}/hajimu-secret-source.XXXXXX")
    # ignore済みのローカル秘密鍵や生成物は含めず、公開対象の追跡済み・新規ソースを走査する。
    (cd "$repo" && git ls-files -z --cached --others --exclude-standard | tar --null -T - -cf -) | tar -xf - -C "$scan_tree"
    if ! "$gitleaks_bin" git --redact --no-banner "$repo" >/dev/null 2>&1 ||
       ! "$gitleaks_bin" dir --redact --no-banner "$scan_tree" >/dev/null 2>&1; then
      repo_result='failed: secret detected'
      ((failures += 1))
    fi
    find "$scan_tree" -depth -delete
  fi

  printf '%-24s %-24s %8d %s\n' "$repo_name" "$branch" "$dirty_count" "$repo_result"
done

printf '\nfailures=%d warnings=%d dirty_repositories=%d\n' \
  "$failures" "$warnings" "$dirty_repos"

if ((failures > 0)); then
  exit 1
fi
