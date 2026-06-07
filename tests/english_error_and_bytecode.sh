#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NIHONGO="${ROOT_DIR}/nihongo"
TMP_DIR="${TMPDIR:-/tmp}/hajimu-english-regression"

mkdir -p "$TMP_DIR"

reserved_file="${TMP_DIR}/reserved_name.jp"
builtin_file="${TMP_DIR}/builtin_redefine.jp"
bytecode_source="${TMP_DIR}/bytecode_direct.haj"
bytecode_file="${TMP_DIR}/bytecode_direct.hjp"

printf 'var class = 1\n' > "$reserved_file"
printf 'var print = 1\n' > "$builtin_file"
printf 'print("bytecode direct ok")\n' > "$bytecode_source"

reserved_output="$("$NIHONGO" "$reserved_file" 2>&1 || true)"
if ! grep -q "予約語なので名前に使えません" <<<"$reserved_output"; then
    printf '%s\n' "$reserved_output"
    echo "reserved keyword error message was not shown"
    exit 1
fi

builtin_output="$("$NIHONGO" "$builtin_file" 2>&1 || true)"
if ! grep -q "組み込み関数名.*再定義できません" <<<"$builtin_output"; then
    printf '%s\n' "$builtin_output"
    echo "builtin alias redefinition message was not shown"
    exit 1
fi

"$NIHONGO" build "$bytecode_source" "$bytecode_file" >/dev/null
bytecode_output="$("$NIHONGO" "$bytecode_file")"
if [[ "$bytecode_output" != "bytecode direct ok" ]]; then
    printf '%s\n' "$bytecode_output"
    echo "direct .hjp execution failed"
    exit 1
fi

echo "english_error_and_bytecode: passed"
