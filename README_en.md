# Hajimu (はじむ)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![GitHub release](https://img.shields.io/github/release/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/releases)
[![GitHub stars](https://img.shields.io/github/stars/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/stargazers)

> 🇯🇵 **日本語版 README はこちら:** [README.md](README.md)

**Write in Japanese. Think in Japanese. Bridge to English syntax too.**

Hajimu is a programming language centered on Japanese keywords and natural Japanese grammar.
It now also accepts practical English aliases such as `function`, `if`, `for`, and `print`,
so learners can move between Japanese understanding and English-based programming concepts.

## ✨ Features

- 🇯🇵 **Japanese-centered syntax**: Code can be written with Japanese keywords and natural word order
- 🌐 **English aliases**: Practical aliases such as `function`, `if`, `for`, and `print`
- 🧭 **Multiple source extensions**: `.jp`, `.haj`, and `.hajimu` source files are supported
- 📖 **Intuitive grammar**: Move gradually between Japanese syntax and English-style code
- ⚡ **Fast**: High-performance interpreter implemented in C
- 🔧 **Practical**: 143 built-in functions, HTTP client, async/parallel support
- 📦 **Package manager**: Built-in manager for external packages
- 🔌 **C extension plugins**: Cross-platform `.hjp` plugin format — write plugins in C, C++, Rust, Go, or Zig
- 📦 **HJPB bytecode**: Build `.jp/.haj/.hajimu` into directly executable `.hjp` files
- 🧭 **Friendly diagnostics**: Shows causes, fixes, examples, and "did you mean" suggestions
- 🎓 **Beginner-friendly**: Designed to be easy to learn even for first-time programmers
- 🚀 **Modern**: Lambda expressions, list comprehensions, async/await, and more

## 📦 Installation

### Homebrew (macOS / Linux)

```bash
# Add the tap
brew tap ReoShiozawa/hajimu

# Install
brew install hajimu
```

### Windows

Download `hajimu_setup.exe` from GitHub Releases and run it. The installer places `hajimu.exe` and required DLLs, then adds Hajimu to PATH.

For portable use, keep `hajimu.exe`, `libcurl-x64.dll`, and `libwinpthread-1.dll` in the same folder.

### Build from Source

```bash
# Clone the repository
git clone https://github.com/ReoShiozawa/hajimu.git
cd hajimu

# Build
make

# Install (optional)
sudo make install
```

### Requirements

- **OS**: macOS 10.13+, Ubuntu 18.04+, Windows 10+ / WSL2
- **Compiler**: GCC 9.0+ or Clang 10.0+
- **Memory**: 256 MB minimum

## 🚀 Quick Start

### Hello, World!

```
表示("こんにちは、世界！")
```

Run it:
```bash
hajimu hello.jp
# or
./nihongo hello.jp
```

### More Examples

```
// Recursive factorial function
関数 階乗(n は 数値) は 数値:
    もし n <= 1 なら
        戻す 1
    それ以外
        戻す n * 階乗(n - 1)
    終わり
終わり

// List comprehension
変数 数列 = [1, 2, 3, 4, 5]
変数 二倍 = [n * 2 を n から 数列]
表示(二倍)  // → [2, 4, 6, 8, 10]

// Class
型 人:
    初期化(名前, 年齢):
        自分.名前 = 名前
        自分.年齢 = 年齢
    終わり

    関数 挨拶():
        表示("Hello, I'm " + 自分.名前)
    終わり
終わり

変数 太郎 = 新規 人("Taro", 25)
太郎.挨拶()
```

> **Keyword reference:** `表示` = print, `関数` = function, `戻す` = return,
> `もし/それ以外/終わり` = if/else/end, `変数` = variable, `型` = class,
> `初期化` = constructor, `自分` = self, `新規` = new

## 📚 Documentation

### 🇺🇸 English

- **[Tutorial](docs/TUTORIAL_en.md)** — Step-by-step beginner guide
- **[Reference Manual](docs/REFERENCE_en.md)** — Complete language reference (all 37 sections)
- **[Plugin Development Guide](docs/PLUGIN_DEVELOPMENT.md)** — Build C extension plugins
- **[Roadmap](docs/ROADMAP_en.md)** — Development roadmap

### 🇯🇵 Japanese (日本語)

- **[Official Website](https://reoshiozawa.github.io/hajimu-document/)** — Full docs and tutorials
- **[Language Reference](docs/REFERENCE.md)** — Syntax and built-in function details
- **[Tutorial](docs/TUTORIAL.md)** — Step-by-step guide
- **[Code Examples](examples/)** — Practical sample programs
- **[Roadmap](docs/ROADMAP.md)** — Development plan

## 🎯 Language Specification

### Keywords

| Category | Keywords |
|---|---|
| Control flow | `もし`, `それ以外`, `なら`, `繰り返す`, `条件`, `の間` |
| Functions | `関数`, `戻す`, `終わり` |
| Variables | `変数`, `定数` |
| Classes | `型`, `初期化`, `自分`, `新規`, `継承` |
| Logic | `真`, `偽`, `かつ`, `または`, `でない` |
| Loop control | `抜ける`, `続ける` |
| Async | `非同期実行`, `待機`, `全待機` |
| Exceptions | `試行`, `捕獲`, `最終`, `投げる` |
| Modules | `取り込む`, `として` |

### Data Types

| Type | Description | Example |
|---|---|---|
| 数値 (number) | 64-bit floating point | `42`, `3.14` |
| 真偽 (boolean) | Boolean | `真` (true), `偽` (false) |
| 文字列 (string) | UTF-8 string | `"hello"` |
| 配列 (array) | Dynamic array | `[1, 2, 3]` |
| 辞書 (dictionary) | Hash map | `{"key": "value"}` |
| 関数 (function) | First-class function | lambda / named function |
| 型 (class) | Object-oriented class | `型 MyClass:` |
| 無 (null) | No value | `無` |

### Built-in Functions (143 total)

| Category | Functions |
|---|---|
| I/O | `表示()`, `入力()`, `読み込む()`, `書き込む()` |
| String | `長さ()`, `分割()`, `結合()`, `置換()`, `検索()` |
| Array | `追加()`, `削除()`, `ソート()`, `変換()`, `抽出()` |
| Math | `合計()`, `平均()`, `最大()`, `最小()`, `平方根()` |
| HTTP | `HTTP取得()`, `HTTP送信()`, `JSON解析()` |
| File | `読み込む()`, `書き込む()`, `ファイル存在()` |
| Type | `数値化()`, `文字列化()`, `型判定()` |

See the [Reference Manual](docs/REFERENCE_en.md) for the complete list.

## 📦 Package Management

Hajimu has a built-in package manager.

```bash
# Initialize a project
hajimu パッケージ 初期化      # Japanese
hajimu pkg init               # English alias

# Add a package from GitHub
hajimu パッケージ 追加 user/repo
hajimu pkg add user/repo

# List installed packages
hajimu パッケージ 一覧
hajimu pkg list

# Install all dependencies from hajimu.json
hajimu パッケージ インストール
hajimu pkg install
```

Use installed packages with `取り込む` (import):

```
取り込む "my-library" として lib
表示(lib["someFunction"](arg))
```

## 💻 Development

### Build from Source

```bash
git clone https://github.com/ReoShiozawa/hajimu.git
cd hajimu

# Compile
make

# Run tests (170 tests)
make test

# Install
sudo make install

# Clean
make clean
```

### Project Structure

```
hajimu/
├── src/                    # Source code
│   ├── main.c             # Entry point
│   ├── lexer.c/h          # Lexer
│   ├── parser.c/h         # Parser
│   ├── ast.c/h            # Abstract Syntax Tree
│   ├── evaluator.c/h      # Evaluator
│   ├── value.c/h          # Value type system
│   ├── environment.c/h    # Environment (scope management)
│   ├── async.c/h          # Async execution
│   ├── http.c/h           # HTTP client
│   ├── package.c/h        # Package manager
│   └── plugin.c/h         # C extension plugins
├── include/               # Plugin development header
│   └── hajimu_plugin.h    # Plugin API
├── examples/              # Sample programs
├── tests/                 # Test suite
├── docs/                  # Documentation
│   ├── REFERENCE.md       # Reference manual (Japanese)
│   ├── REFERENCE_en.md    # Reference manual (English)
│   ├── TUTORIAL.md        # Tutorial (Japanese)
│   ├── TUTORIAL_en.md     # Tutorial (English)
│   ├── PLUGIN_DEVELOPMENT.md  # Plugin dev guide (English)
│   └── ROADMAP.md         # Roadmap (Japanese)
└── Makefile               # Build configuration
```

## 🤝 Contributing

Contributions are welcome!

### How to Contribute

1. Fork this repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'feat: add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

See [CONTRIBUTING.md](CONTRIBUTING.md) for details.

### Commit Convention

- `feat:` New feature
- `fix:` Bug fix
- `docs:` Documentation
- `refactor:` Code refactoring
- `test:` Add or fix tests

## 📈 Project Stats

- **Implementation language**: C (C99/C11)
- **Character encoding**: UTF-8
- **Tests**: 170 tests passing
- **Built-in functions**: 143
- **Parser**: Recursive descent (LL(1))
- **Execution**: Direct AST evaluation

## 🔗 Links

- **Documentation**: https://reoshiozawa.github.io/hajimu-document/
- **Repository**: https://github.com/ReoShiozawa/hajimu
- **Issue tracker**: https://github.com/ReoShiozawa/hajimu/issues
- **Tutorial (English)**: [docs/TUTORIAL_en.md](docs/TUTORIAL_en.md)
- **Reference (English)**: [docs/REFERENCE_en.md](docs/REFERENCE_en.md)
- **English alias policy**: [docs/ENGLISH_ALIAS_POLICY.md](docs/ENGLISH_ALIAS_POLICY.md)

## 📝 License

This project is released under the [MIT License](LICENSE).

## 👤 Author

**Reo Shiozawa**

- GitHub: [@ReoShiozawa](https://github.com/ReoShiozawa)

## 📊 Language Comparison

| Feature | Hajimu | Python | Ruby | JavaScript |
|---|---|---|---|---|
| Japanese-centered syntax | ✅ | ❌ | ❌ | ❌ |
| English alias syntax | ✅ | - | - | ✅ |
| Learning difficulty | Low | Medium | Medium | Medium |
| Type system | Dynamic | Dynamic | Dynamic | Dynamic |
| Async support | ✅ | ✅ | ✅ | ✅ |
| C extension plugins | ✅ | ✅ | ✅ | ✅ |
| Built-in package manager | ✅ | ✅ | ✅ | ✅ |
