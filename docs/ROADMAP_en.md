# Hajimu Roadmap

## Planned Features

Last updated: February 7, 2026

---

## 🔴 Top Priority (User Requests)

| # | Feature | Status | Description |
|---|---|---|---|
| 1 | **Async** | ✅ Done | Async function definition and execution via `非同期実行` / `待機` / `全待機` keywords |
| 2 | **WebSocket** | ✅ Done | WebSocket client (`WS接続`, `WS送信`, `WS受信`, `WS切断`) |
| 3 | **Parallel Execution** | ✅ Done | Thread-based parallel execution (`並列実行`, `排他作成`, `排他実行`) |
| 4 | **Scheduler** | ✅ Done | Task scheduling (`定期実行`, `遅延実行`, `スケジュール停止`) |

---

## 🟠 High Priority (Core Language Features)

| # | Feature | Status | Description |
|---|---|---|---|
| 5 | **super calls** | ✅ Done | Call parent class methods via `親.method(args)` |
| 6 | **REPL improvements** | ✅ Done | Multi-line input, history, colored output |
| 7 | **Stack traces** | ✅ Done | Show call stack on error |
| 8 | **Negative indexing** | ✅ Done | `array[-1]` accesses the last element |
| 9 | **Ternary operator** | ✅ Done | `condition ? valueA : valueB` |
| 10 | **Variadic args** | ✅ Done | `関数 sum(*nums):` |
| 11 | **Custom exceptions** | ✅ Done | Structured exceptions via `例外作成(type, message)` |
| 12 | **Destructuring** | ✅ Done | `変数 [a, b] = [1, 2]` |
| 13 | **startsWith / endsWith** | ✅ Done | `始まる` / `終わる` |
| 14 | **substring** | ✅ Done | `部分文字列` built-in |

---

## 🟡 Medium Priority (Usability)

| # | Feature | Status | Description |
|---|---|---|---|
| 15 | **Null coalescing** | ✅ Done | `value ?? "default"` |
| 16 | **every / some** | ✅ Done | `全て()` / `一つでも()` |
| 17 | **Custom-comparator sort** | ✅ Done | `比較ソート(array, comparatorFn)` |
| 18 | **File append** | ✅ Done | `追記(path, content)` |
| 19 | **Directory operations** | ✅ Done | `ディレクトリ一覧()`, `ディレクトリ作成()` |
| 20 | **Trig / log / constants** | ✅ Done | `正弦()`, `余弦()`, `対数()`, `円周率` |
| 21 | **Dict key-value iteration** | ✅ Done | `各 key, value を dict の中:` |
| 22 | **instanceof** | ✅ Done | `型判定(obj, "ClassName")` |
| 23 | **assert** | ✅ Done | `表明()` |
| 24 | **Module namespaces** | ✅ Done | `取り込む "file" として alias` |
| 25 | **`%=` / `**=` compound assign** | ✅ Done | Operator consistency |
| 26 | **Random integer** | ✅ Done | `乱数整数(1, 100)` |
| 27 | **Spread operator** | ✅ Done | `fn(...array)` |
| 28 | **find / zip / unique** | ✅ Done | `探す` / `圧縮` / `一意` |
| 29 | **pop** | ✅ Done | `末尾削除(array)` |

---

## 🟢 Low Priority (Advanced Features)

| # | Feature | Status | Description |
|---|---|---|---|
| 30 | Enumerations (enum) | ✅ Done | `列挙 Color: Red Blue Green 終わり` |
| 31 | Pattern matching | ✅ Done | `照合 value: 場合 pattern => action 終わり` |
| 32 | Generators / yield | ✅ Done | `生成関数` / `譲渡` / `次()` / `完了()` / `全値()` |
| 33 | Decorators | ✅ Done | `@decorator 関数 compute():` |
| 34 | Static methods | ✅ Done | `静的 関数 create():` |
| 35 | Access modifiers | ✅ Done | Private fields via `_` prefix |
| 36 | Operator overloading | ✅ Done | `足す` / `引く` / `等しい` etc. |
| 37 | Set type | ✅ Done | `集合(1, 2, 3)`, union / intersection / difference |
| 38 | Hex / binary literals | ✅ Done | `0xFF`, `0b1010` |
| 39 | Test framework | ✅ Done | `テスト(name, fn)` / `テスト実行()` / `期待()` |
| 40 | toString protocol | ✅ Done | Customize object display via `文字列化` method |
| 41 | Base64 / hashing | ✅ Done | `Base64エンコード()`, `Base64デコード()` |
| 42 | Path utilities | ✅ Done | `パス結合()`, `ファイル名()`, `ディレクトリ名()`, `拡張子()` |
| 43 | Doc comments | ✅ Done | `文書化(name, desc)` / `文書(name)` |
| 44 | Type aliases | ✅ Done | `型別名("int", "数値")` — resolved in `型判定` |
| 45 | Package manager | ✅ Done | Built-in package manager (`hajimu pkg init/add/remove/list/install`), extended `取り込む` resolution |
| 46 | C extension plugins | ✅ Done | Cross-platform `.hjp` plugin format, extension-less import, writable in C/C++/Rust etc. |

---

## Progress Summary

- **Completed:** 46 / 46 (100%) 🎉
- **Tests:** 170 tests passing (20 core + 150 new features)

---

## 🔮 Future Vision

### Performance And Research Compute Foundation

To make Hajimu useful for research, numerical computing, and machine-learning-oriented workflows, the language needs both faster standard operations and a dedicated numeric data model.

See [Performance And Research Compute Design](PERFORMANCE_AND_COMPUTE_DESIGN.md) for the full design.

| Phase | Status | Description |
|---|---|---|
| Benchmark foundation | Initial implementation done | `benchmarks/` and `make bench` for continuous speed tracking |
| `vector` foundation | Initial implementation done | `VALUE_NUMERIC_ARRAY`, vector operations, statistics, and metrics |
| `matrix` foundation | Initial implementation done | `VALUE_MATRIX`, matrix operations, and linear algebra |
| Data processing | Initial implementation done | Numeric CSV loading, column extraction, descriptive statistics, missing helpers |
| Research packages | Initial standard functions done | stats, linalg, linear/logistic regression, and k-means |
| Optional native acceleration | Partially implemented | Plugin numeric-buffer helpers; BLAS remains future work |

### Cross-Platform Package Distribution (implemented in v1.3.0)

| Approach | Status | Description |
|---|---|---|
| HJPB bytecode | ✅ v1.3.0 | Bundle `.jp` scripts in HJPB for cross-platform distribution |
| Pure script packages | ✅ v1.3.0 | `"メイン": main.jp` — works on all OSes as-is |
| GitHub Actions CI distribution | ✅ v1.3.0 | Auto-build C/C++/Rust plugins for macOS/Linux/Windows and upload to Releases. `hajimu pkg add` downloads the correct binary for the user's OS |

### Native SDL2 / OpenGL Built into the Interpreter (Future)

The current plugin approach (C `.hjp` files loaded dynamically) requires
engine-type packages to distribute platform-specific binaries via GitHub Releases.

In the future, **statically linking SDL2, OpenGL, miniaudio, and similar rendering/audio
libraries directly into the Hajimu interpreter** would allow engine packages to be
distributed as pure jp scripts inside HJPB files, achieving true
"one file, all platforms" deployment.

```
Future Hajimu (SDL2 embedded):
  nihongo app.jp          ← single .jp script
  hajimu pkg add jp-gui   ← hajimu_gui.hjp (HJPB = jp script)
  → works on Windows / macOS / Linux
```

**Changes required:**
- Statically link SDL2 / OpenGL / miniaudio into the interpreter
- Rewrite engine packages in jp (large-scale refactor abandoning existing C code)
- Accept a larger interpreter binary (~5–15 MB increase)

**This shares the same design philosophy as Unity and Flutter,
and is one of the long-term goals of Hajimu.**

| Item | Current (v1.3.x) | Future (SDL2 embedded) |
|---|---|---|
| Engine distribution | GitHub Releases (per-OS binaries) | HJPB script (single file) |
| Interpreter size | ~1 MB | ~15 MB |
| GPU access | Native C plugin | Directly from jp script |
| Implementation difficulty | — | Very large |
