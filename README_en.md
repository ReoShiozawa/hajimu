# Hajimu

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![GitHub release](https://img.shields.io/github/v/release/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/releases)
[![GitHub stars](https://img.shields.io/github/stars/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/stargazers)

> 日本語版 README: [README.md](README.md)

**A Japanese-first programming language with a practical bridge to English syntax.**

Hajimu is an experimental programming language and runtime implemented in C.
It started from a simple question: what would programming feel like if Japanese
were not merely comments, variable names, or a teaching layer, but the primary
syntax of the language?

At the same time, Hajimu is not trying to isolate learners from the wider
programming ecosystem. Current builds also accept English aliases such as
`function`, `var`, `if`, `for`, `return`, `class`, `new`, `print`, `len`,
`http_get`, and `async_run`. This makes Hajimu useful both as a Japanese-first
language and as a research playground for localized syntax, language learning,
runtime design, and tooling.

```hajimu
function add(a is number, b is number) is number:
    return a + b
end

var numbers = [1, 2, 3]
append(numbers, 4)

var total = 0
for value in numbers:
    total += value
end

if total >= 10 then
    print("total = " + to_string(total))
else:
    print("too small")
end
```

The same runtime also supports Japanese-native code:

```hajimu
関数 挨拶(名前):
    表示("こんにちは、" + 名前 + "さん")
終わり

挨拶("はじむ")
```

## Why This Is Interesting

Most programming languages assume English-like keywords and grammar. Hajimu
explores a different design space:

- **Localized syntax as a first-class language design problem**, not a skin.
- **Gradual transition between Japanese and English programming concepts**.
- **Friendly diagnostics** that explain causes, fixes, examples, and likely typos.
- **A compact C interpreter** that is small enough to read and hack on.
- **Practical batteries included**: HTTP, JSON, files, async tasks, channels,
  atomics, classes, enums, pattern matching, generators, and a package manager.
- **Cross-platform distribution work** through HJPB `.hjp` bytecode and Windows
  installer builds.

Hajimu is still young, but it is already substantial enough for language
experiments, educational tools, editor integrations, and small scripts.

## Quick Start

### Install

macOS / Linux with Homebrew:

```bash
brew tap ReoShiozawa/hajimu
brew install hajimu
```

Windows:

Download `hajimu_setup-1.4.0.exe` from
[GitHub Releases](https://github.com/ReoShiozawa/hajimu/releases), run it, and
open a new terminal.

Portable Windows usage is also supported: keep `hajimu-windows-x64.exe`,
`libcurl-x64.dll`, and `libwinpthread-1.dll` in the same directory.

Build from source:

```bash
git clone https://github.com/ReoShiozawa/hajimu.git
cd hajimu
make
./nihongo examples/english_basic.jp
```

The installed command is usually `hajimu`; the source-tree binary is `nihongo`.

### Run A Program

Create `hello.haj`:

```hajimu
function greet(name):
    print("Hello, " + name)
end

greet("Hajimu")
```

Run:

```bash
hajimu hello.haj
# or from a source checkout:
./nihongo hello.haj
```

Supported source extensions are `.jp`, `.haj`, and `.hajimu`.

## Language Snapshot

| Area | Status |
|---|---|
| Japanese syntax | Primary syntax |
| English aliases | Practical subset implemented |
| Runtime | AST interpreter in C |
| Type system | Dynamic |
| Strings | UTF-8 aware |
| OOP | Classes, constructors, `self`, inheritance support |
| Functional features | Lambdas, higher-order functions, list comprehensions |
| Control flow | if/else, while, for, foreach, switch, match |
| Error handling | try/catch/finally, throw |
| Async/concurrency | async tasks, await, parallel map/run, channels, mutexes, semaphores, atomics |
| Data / IO | JSON, HTTP client, file IO, path helpers, Base64, regex |
| Packaging | `hajimu pkg` package manager |
| Distribution | HJPB `.hjp` bytecode and native plugin loading |
| Plugins | C ABI plugin API, suitable for C/C++/Rust-style native extensions |

## Examples

Object-oriented code:

```hajimu
class Character:
    init(name, level):
        self.name = name
        self.level = level
    end

    function describe():
        return self.name + " Lv." + to_string(self.level)
    end
end

var hero = new Character("Aki", 5)
print(hero.describe())
```

Async and parallel helpers:

```hajimu
function double(x):
    return x * 2
end

var mapped = parallel_map([1, 2, 3], double)
print(to_string(mapped))  // [2, 4, 6]

var task = async_run(double, 21)
print(to_string(await_task(task, 2)))  // 42
```

Japanese and English can be mixed intentionally:

```hajimu
function 合計(numbers):
    var total = 0
    for value in numbers:
        total += value
    end
    戻す total
end

print(to_string(合計([1, 2, 3])))
```

More examples live in [examples/](examples/), especially:

- [examples/english_basic.jp](examples/english_basic.jp)
- [examples/english_advanced.jp](examples/english_advanced.jp)
- [examples/english_oop.jp](examples/english_oop.jp)
- [examples/english_concurrency_aliases.jp](examples/english_concurrency_aliases.jp)

## Documentation

Start here:

- [Tutorial](docs/TUTORIAL_en.md)
- [Reference Manual](docs/REFERENCE_en.md)
- [English Syntax Roadmap](docs/ENGLISH_SYNTAX_ROADMAP.md)
- [English Alias Naming And Collision Policy](docs/ENGLISH_ALIAS_POLICY.md)
- [Plugin Development Guide](docs/PLUGIN_DEVELOPMENT.md)
- [Roadmap](docs/ROADMAP_en.md)
- [Changelog](CHANGELOG.md)

Japanese documentation:

- [日本語 README](README.md)
- [チュートリアル](docs/TUTORIAL.md)
- [リファレンス](docs/REFERENCE.md)
- [ロードマップ](docs/ROADMAP.md)

## Build And Test

Requirements:

- macOS 10.13+, Linux, or Windows 10+
- GCC 9+ or Clang 10+
- `make`
- libcurl development files
- MinGW-w64 and NSIS only if you want to build Windows artifacts from macOS/Linux

Common commands:

```bash
make                 # build ./nihongo
make release         # optimized local build
make windows         # cross-build win/dist/hajimu.exe
make windows-installer
make wasm            # build WebAssembly artifacts for jp-edu integration
```

Test commands used for releases:

```bash
# Most tests are standalone .jp files.
for file in tests/*.jp; do
  [ "$file" = "tests/webhook_test.jp" ] && continue
  ./nihongo "$file"
done

for file in examples/english_*.jp; do
  ./nihongo "$file"
done

tests/english_error_and_bytecode.sh
```

`tests/webhook_test.jp` starts a server and waits for an external/manual request,
so it is intentionally skipped in automated release smoke tests.

## Architecture

The core is intentionally approachable:

```text
src/
├── lexer.c / lexer.h          tokenization and keyword aliases
├── parser.c / parser.h        recursive descent parser
├── ast.c / ast.h              AST nodes
├── evaluator.c / evaluator.h  runtime evaluator and built-ins
├── value.c / value.h          dynamic value model
├── environment.c / .h         scopes and closures
├── gc.c / gc.h                environment cycle collection
├── async.c / async.h          async tasks, channels, locks
├── http.c / http.h            JSON, HTTP client, simple server helpers
├── package.c / package.h      package manager
├── plugin.c / plugin.h        native plugin loading
└── bytecode.c / bytecode.h    HJPB .hjp format
```

The parser maps Japanese syntax and English aliases into the same AST. That is
the key design choice: English support is not a second language mode, and
Japanese code does not become a translation artifact.

## Current Maturity

Hajimu is open source and usable, but not yet a production-stable general-purpose
language. Good use cases today:

- language design experiments
- Japanese-first programming education
- editor tooling and diagnostics experiments
- small scripts and demos
- package/plugin architecture experiments
- runtime hacking in C

Areas still evolving:

- grammar consistency between Japanese and English aliases
- package ecosystem maturity
- Windows/macOS/Linux release automation
- long-running async workloads
- formal language specification

## Contributing

Contributions are welcome. The most valuable contributions right now are:

- small reproducible bug reports
- diagnostics improvements
- English and Japanese documentation improvements
- examples that teach one idea clearly
- tests for parser/runtime edge cases
- Windows packaging feedback
- VS Code tooling ideas and integration feedback

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.
For security-sensitive reports, see [SECURITY.md](SECURITY.md).

Commit message convention:

- `feat:` new feature
- `fix:` bug fix
- `docs:` documentation
- `test:` tests
- `refactor:` internal cleanup
- `perf:` performance improvement
- `chore:` build/release maintenance

## Project Links

- Repository: https://github.com/ReoShiozawa/hajimu
- Releases: https://github.com/ReoShiozawa/hajimu/releases
- Issues: https://github.com/ReoShiozawa/hajimu/issues
- Documentation site: https://reoshiozawa.github.io/hajimu-document/

## License

Hajimu is released under the [MIT License](LICENSE).

## Author

Created and maintained by [Reo Shiozawa](https://github.com/ReoShiozawa).
