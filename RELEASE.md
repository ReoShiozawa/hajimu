# Release Guide

This document describes the current release flow for Hajimu.

## Release Artifacts

Typical release assets:

- `nihongo-macos`
- `hajimu-windows-x64.exe`
- `hajimu_setup-<version>.exe`
- `libcurl-x64.dll`
- `libwinpthread-1.dll`

Tracked Windows artifacts are also kept under `win/dist/`:

- `win/dist/hajimu.exe`
- `win/dist/hajimu_setup.exe`
- required DLLs

`dist/` is a local staging directory for GitHub Release uploads and should not
be committed.

## Version Update Checklist

Update version strings in:

- `src/main.c`
- `src/evaluator.c`
- `src/wasm_api.c`
- `win/installer.nsi`
- `CHANGELOG.md`
- user-facing docs when they show the current version

Then check:

```bash
./nihongo --version
rg "1\\.3\\.3|1\\.4\\.0" src docs README.md README_en.md win/installer.nsi
```

Adjust the `rg` pattern for the version being released.

## Build

macOS/local release build:

```bash
make release
mkdir -p dist
cp nihongo dist/nihongo-macos
```

Windows cross-build and installer:

```bash
make windows-installer
cp win/dist/hajimu.exe dist/hajimu-windows-x64.exe
cp win/dist/hajimu_setup.exe dist/hajimu_setup-<version>.exe
cp win/dist/libcurl-x64.dll dist/libcurl-x64.dll
cp win/dist/libwinpthread-1.dll dist/libwinpthread-1.dll
```

Requirements for Windows artifacts:

- MinGW-w64
- NSIS (`makensis`)
- curl runtime prepared by `win/setup_curl.sh`

## Test

Run the normal build first:

```bash
make
```

Release smoke tests:

```bash
for file in tests/*.jp; do
  [ "$file" = "tests/webhook_test.jp" ] && continue
  ./nihongo "$file"
done

for file in examples/english_*.jp; do
  ./nihongo "$file"
done

tests/english_error_and_bytecode.sh
```

Async stability smoke test:

```bash
for i in $(seq 1 50); do
  ./nihongo examples/english_concurrency_aliases.jp >/tmp/hajimu-async.out
done
```

Diff hygiene:

```bash
git diff --check
git diff --cached --check
```

## Commit And Tag

Stage source, docs, tests, examples, and tracked release artifacts only.
Do not commit:

- `build/`
- `dist/`
- `win/build/`
- `win/curl-win64/`
- local temporary `.hjp` outputs

```bash
git add CHANGELOG.md README.md README_en.md docs src tests examples win/dist win/installer.nsi
git commit -m "release: vX.Y.Z"
git tag -a vX.Y.Z -m "Release vX.Y.Z"
git push origin main
git push origin vX.Y.Z
```

## GitHub Release

```bash
gh release create vX.Y.Z \
  dist/nihongo-macos \
  dist/hajimu-windows-x64.exe \
  dist/hajimu_setup-X.Y.Z.exe \
  dist/libcurl-x64.dll \
  dist/libwinpthread-1.dll \
  --title "はじむ vX.Y.Z" \
  --notes-file /tmp/hajimu-release-notes.md
```

Release notes should include:

- major user-facing changes
- bug/security fixes
- build artifacts
- validation commands
- known limitations, if any

## Homebrew

If the Homebrew formula is updated in this repository or a tap repository:

1. download the GitHub source tarball for the tag
2. compute SHA256
3. update the formula URL and SHA
4. test install locally

```bash
curl -L https://github.com/ReoShiozawa/hajimu/archive/refs/tags/vX.Y.Z.tar.gz -o hajimu-vX.Y.Z.tar.gz
shasum -a 256 hajimu-vX.Y.Z.tar.gz
```

## Notes

`release.sh` exists for convenience, but manual staging is safer when the
working tree contains generated directories. Prefer the checklist above when
preparing public releases.
