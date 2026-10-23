#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HAJIMU_BIN=${HAJIMU_BIN:-"$ROOT/nihongo"}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/hajimu-package-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

mkdir -p "$WORK/source" "$WORK/project"
cat > "$WORK/source/hajimu.json" <<'JSON'
{
  "schema_version": 2,
  "name": "sample_pkg",
  "version": "1.2.3",
  "entry": "main.jp",
  "dependencies": {}
}
JSON
printf '表示する("package ok")\n' > "$WORK/source/main.jp"
git -C "$WORK/source" init -q
git -C "$WORK/source" config user.name "Hajimu Test"
git -C "$WORK/source" config user.email "test@hajimu.invalid"
git -C "$WORK/source" add hajimu.json main.jp
git -C "$WORK/source" commit -q -m initial

cat > "$WORK/project/hajimu.json" <<JSON
{
  "schema_version": 2,
  "name": "package_test",
  "version": "1.0.0",
  "entry": "main.jp",
  "dependencies": {
    "sample_pkg": {
      "version": "^1.2.0",
      "source": "$WORK/source"
    }
  }
}
JSON

cd "$WORK/project"
"$HAJIMU_BIN" pkg validate
"$HAJIMU_BIN" pkg install
test -f hajimu.lock
test -f hajimu_packages/sample_pkg/main.jp
"$HAJIMU_BIN" pkg audit
grep -q '"commit"' hajimu.lock

old_commit=$(jq -r '.packages.sample_pkg.commit' hajimu.lock)
sed 's/"1.2.3"/"1.3.0"/' "$WORK/source/hajimu.json" > "$WORK/source/hajimu.json.next"
mv "$WORK/source/hajimu.json.next" "$WORK/source/hajimu.json"
git -C "$WORK/source" add hajimu.json
git -C "$WORK/source" commit -q -m update
"$HAJIMU_BIN" pkg update sample_pkg
"$HAJIMU_BIN" pkg audit
test "$(jq -r '.packages.sample_pkg.version' hajimu.lock)" = "1.3.0"
test "$(jq -r '.packages.sample_pkg.commit' hajimu.lock)" != "$old_commit"

mkdir -p "$WORK/sample_native"
cat > "$WORK/sample_native/hajimu.json" <<'JSON'
{
  "schema_version": 2,
  "name": "sample_native",
  "version": "1.0.0",
  "entry": "sample_native.hjp",
  "build": "printf plugin > sample_native.hjp",
  "dependencies": {}
}
JSON
git -C "$WORK/sample_native" init -q
git -C "$WORK/sample_native" config user.name "Hajimu Test"
git -C "$WORK/sample_native" config user.email "test@hajimu.invalid"
git -C "$WORK/sample_native" add hajimu.json
git -C "$WORK/sample_native" commit -q -m initial

if "$HAJIMU_BIN" pkg add "$WORK/sample_native"; then
  echo "ビルドスクリプトが許可なしで実行されました" >&2
  exit 1
fi
test ! -d hajimu_packages/sample_native
"$HAJIMU_BIN" pkg add "$WORK/sample_native" --allow-scripts
test -f hajimu_packages/sample_native/sample_native.hjp
"$HAJIMU_BIN" pkg audit

cat > "$WORK/unsafe-entry.json" <<'JSON'
{
  "schema_version": 2,
  "name": "unsafe_entry",
  "version": "1.0.0",
  "entry": "../outside.jp",
  "dependencies": {}
}
JSON
if "$HAJIMU_BIN" pkg validate "$WORK/unsafe-entry.json"; then
  echo "パッケージ外を指すentryが受理されました" >&2
  exit 1
fi

cat > "$WORK/insecure-source.json" <<'JSON'
{
  "schema_version": 2,
  "name": "insecure_source",
  "version": "1.0.0",
  "entry": "main.jp",
  "dependencies": {
    "unsafe_dep": {
      "version": "1.0.0",
      "source": "http://example.invalid/unsafe.git"
    }
  }
}
JSON
if "$HAJIMU_BIN" pkg validate "$WORK/insecure-source.json"; then
  echo "HTTP依存元が受理されました" >&2
  exit 1
fi

if "$HAJIMU_BIN" pkg remove ..; then
  echo "不正なパッケージ名で削除できました" >&2
  exit 1
fi
test -f hajimu.json

mkdir -p "$WORK/symlink-target"
printf 'preserve\n' > "$WORK/symlink-target/sentinel.txt"
ln -s "$WORK/symlink-target" hajimu_packages/symlink_pkg
"$HAJIMU_BIN" pkg remove symlink_pkg
test -f "$WORK/symlink-target/sentinel.txt"
test ! -e hajimu_packages/symlink_pkg

cp hajimu.lock "$WORK/hajimu.lock.safe"
mv hajimu_packages/sample_pkg "$WORK/sample_pkg.installed"
jq --arg payload '$(touch '"$WORK"'/lock-command-executed)' \
  '.packages.sample_pkg.commit = $payload' hajimu.lock > "$WORK/hajimu.lock.tampered"
mv "$WORK/hajimu.lock.tampered" hajimu.lock
if "$HAJIMU_BIN" pkg install; then
  echo "不正なlockコミットが受理されました" >&2
  exit 1
fi
test ! -e "$WORK/lock-command-executed"
mv "$WORK/hajimu.lock.safe" hajimu.lock
mv "$WORK/sample_pkg.installed" hajimu_packages/sample_pkg
"$HAJIMU_BIN" pkg audit

mkdir -p "$WORK/sample_parent"
cat > "$WORK/sample_parent/hajimu.json" <<JSON
{"schema_version":2,"name":"sample_parent","version":"1.0.0","entry":"main.jp","dependencies":{"sample_pkg":{"version":"^1.0.0","source":"$WORK/source"}}}
JSON
printf '表示("parent")\n' > "$WORK/sample_parent/main.jp"
git -C "$WORK/sample_parent" init -q
git -C "$WORK/sample_parent" config user.name "Hajimu Test"
git -C "$WORK/sample_parent" config user.email "test@hajimu.invalid"
git -C "$WORK/sample_parent" add .
git -C "$WORK/sample_parent" commit -q -m initial
"$HAJIMU_BIN" pkg add "$WORK/sample_parent"
"$HAJIMU_BIN" pkg audit
cp hajimu_packages/sample_parent/hajimu.json "$WORK/parent.safe"
jq '.dependencies.sample_pkg.version = "^9.0.0"' "$WORK/parent.safe" > hajimu_packages/sample_parent/hajimu.json
if "$HAJIMU_BIN" pkg audit; then
  echo "推移依存の競合が検出されませんでした" >&2
  exit 1
fi
mv "$WORK/parent.safe" hajimu_packages/sample_parent/hajimu.json
"$HAJIMU_BIN" pkg audit

echo "Package manager checks passed"
