# Hajimu

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![GitHub release](https://img.shields.io/github/v/release/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/releases)
[![GitHub stars](https://img.shields.io/github/stars/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/stargazers)

> Japanese README: [README.md](README.md)

**Hajimu is a Japanese-first programming language with a deliberate bridge into English-style programming.**

It is not a localization layer on top of another language, and it is not just
English syntax translated word by word. Hajimu has its own lexer, parser, AST,
runtime, package tools, bytecode format, and VS Code support. The core runtime
is written in C, while the language design explores a question that most
mainstream languages do not ask:

> What would a practical programming language look like if Japanese were a
> first-class syntax, while still helping learners and developers connect to the
> wider English-based programming ecosystem?

Hajimu accepts Japanese-native code:

```hajimu
関数 挨拶(名前):
    表示("こんにちは、" + 名前 + "さん")
終わり

挨拶("はじむ")
```

It also accepts English aliases in the same runtime:

```hajimu
function add(a is number, b is number) is number:
    return a + b
end

var values = [1, 2, 3]
append(values, 4)

var total = 0
for value in values:
    total += value
end

if total >= 10 then
    print("total = " + to_string(total))
else:
    print("too small")
end
```

Japanese and English syntax intentionally map into the same AST and evaluator.
That design makes Hajimu useful as a language project, a learning tool, and a
research playground for localized syntax, diagnostics, editor tooling, and
runtime implementation.

## Why Hajimu Exists

Most programming languages quietly assume that core syntax should look like
English. That assumption affects who feels invited, what beginners have to
memorize before they can reason about programs, and how programming concepts
are taught.

Hajimu takes a different stance:

- Japanese should be able to carry real program structure, not only comments or
  variable names.
- English programming terms should remain reachable, because learners eventually
  need to read docs, errors, APIs, and code from the wider ecosystem.
- Error messages should teach. Diagnostics should explain the cause, likely
  typo, concrete fix, and a nearby example whenever possible.
- The implementation should stay small enough that contributors can understand
  the whole language stack.
- Distribution should matter early: source files, bytecode, plugins, editor
  support, Windows installers, and WebAssembly builds are all part of the same
  language experience.

This makes Hajimu less like "a toy Japanese syntax demo" and more like a compact
language laboratory with a strong educational purpose.

## Current Status

Hajimu is young, but it is already more than a sketch.

| Area | Status |
|---|---|
| Runtime | C AST interpreter |
| Source extensions | `.jp`, `.haj`, `.hajimu` |
| Japanese syntax | Primary language surface |
| English aliases | Practical v1.5.0 subset implemented |
| Types | Dynamic values with optional type annotations in syntax |
| Text | UTF-8 aware string helpers |
| Research compute | Initial numeric vector/matrix API with dtype/astype, statistics, metrics, missing-value helpers, train/test splitting, linear algebra, linear/logistic regression, k-means, and optional BLAS builds |
| Functions | Closures, lambdas, higher-order helpers, generators |
| Data structures | Arrays, dictionaries, enums |
| OOP | Classes, constructors, `self`, inheritance, static methods |
| Control flow | if/else, while, for, foreach, switch, match |
| Errors | try/catch/finally, throw, friendlier parser/runtime messages |
| IO / data | File IO, paths, JSON, HTTP client, regex, Base64 |
| Concurrency | async tasks, await, parallel map/run, channels, mutexes, semaphores, atomics |
| Packaging | `hajimu pkg` package manager |
| Distribution | HJPB `.hjp` bytecode, native plugin loading, Windows builds |
| Tooling | VS Code extension, syntax highlighting, completions, diagnostics |
| Web | WASM target for browser-based education tools |

It is suitable today for:

- language design experiments
- Japanese-first programming education
- small scripts and demos
- editor tooling experiments
- diagnostics research
- C runtime hacking
- package/plugin design experiments

It is not yet a production-stable replacement for Python, JavaScript, Ruby, or
Go. The grammar, package ecosystem, and cross-platform release process are still
evolving.

## Install

### macOS / Linux with Homebrew

```bash
brew tap ReoShiozawa/hajimu
brew install hajimu
```

### Windows

Download the latest installer from
[GitHub Releases](https://github.com/ReoShiozawa/hajimu/releases).

For v1.5.0, the main installer asset is:

```text
hajimu_setup-1.5.0.exe
```

Portable Windows usage is also supported. Keep these files in the same folder:

```text
hajimu-windows-x64.exe
libcurl-x64.dll
libwinpthread-1.dll
```

### Build From Source

```bash
git clone https://github.com/ReoShiozawa/hajimu.git
cd hajimu
make
./nihongo examples/english_basic.jp
```

The installed command is usually `hajimu`. Inside a source checkout, the local
interpreter binary is `./nihongo`.

## Try It In Two Minutes

Create `hello.haj`:

```hajimu
function greet(name):
    print("Hello, " + name)
end

greet("Hajimu")
```

Run it:

```bash
hajimu hello.haj
# or from this repository:
./nihongo hello.haj
```

Supported source extensions:

| Extension | Suggested use |
|---|---|
| `.jp` | Japanese-first Hajimu source |
| `.haj` | English-alias examples and international-facing source |
| `.hajimu` | Explicit Hajimu source name |

## Language Examples

### Mixed Japanese And English

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

This is not a translation mode. `function` and `関数`, `return` and `戻す`,
`print` and `表示` are connected to the same runtime concepts.

### Classes

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

### Async And Parallel Helpers

```hajimu
function double(x):
    return x * 2
end

var mapped = parallel_map([1, 2, 3], double)
print(to_string(mapped))  // [2, 4, 6]

var task = async_run(double, 21)
print(to_string(await_task(task, 2)))  // 42
```

### HTTP And JSON

```hajimu
var response = http_get("https://example.com")
print(to_string(response.status))

var data = json_decode("{\"name\":\"Hajimu\"}")
print(data["name"])
```

## English Alias Design

English support is intentionally built as aliases over the Japanese-first
language, not as a forked grammar or separate runtime.

Examples:

| Japanese | English aliases | Runtime meaning |
|---|---|---|
| `関数` | `function`, `fn` | function definition |
| `戻す` / `返す` | `return` | return statement |
| `変数` | `var`, `let` | variable declaration |
| `定数` | `const` | constant declaration |
| `もし` | `if` | conditional |
| `それ以外` | `else` | fallback branch |
| `繰り返す` | `for`, `repeat` | loop |
| `各` / `の中` | `each`, `in` | foreach-style loop |
| `型` | `class`, `type` | class definition |
| `新規` | `new` | instance creation |
| `自分` | `self`, `this` | receiver reference |
| `試行` / `捕獲` | `try`, `catch` | exception handling |
| `表示` | `print`, `println` | output |

More details:

- [English Syntax Roadmap](docs/ENGLISH_SYNTAX_ROADMAP.md)
- [English Alias Naming And Collision Policy](docs/ENGLISH_ALIAS_POLICY.md)
- [English Reference Manual](docs/REFERENCE_en.md)

## Tooling

### VS Code

The VS Code extension lives in a separate repository:

- https://github.com/ReoShiozawa/hajimu-vscode

It supports `.jp`, `.haj`, and `.hajimu` files, including syntax highlighting,
completion, hover information, diagnostics, Japanese romaji expansion, and
English alias highlighting.

### Education And Browser Runtime

Hajimu also has a WebAssembly target used by the education project:

- https://gitlab.com/hajimudev-group/hajimubridge-project

The WASM API is intentionally small and focused on running source code in a
browser-based learning environment.

## Repository Map

```text
src/
├── lexer.c / lexer.h          tokenization and keyword aliases
├── parser.c / parser.h        recursive descent parser
├── ast.c / ast.h              AST nodes
├── evaluator.c / evaluator.h  runtime evaluator and built-ins
├── value.c / value.h          dynamic value model
├── environment.c / .h         scopes, constants, closures
├── gc.c / gc.h                environment cycle collection
├── async.c / async.h          tasks, channels, locks, atomics
├── http.c / http.h            JSON, HTTP client, server helpers
├── package.c / package.h      package manager
├── plugin.c / plugin.h        native plugin loading
└── bytecode.c / bytecode.h    HJPB .hjp format
```

Other useful directories:

```text
examples/    runnable examples
tests/       regression and smoke tests
docs/        reference, tutorial, roadmap, design notes
scripts/     release and maintenance helpers
win/         Windows build and installer files
Formula/     Homebrew formula
```

## Build And Test

Requirements:

- macOS 10.13+, Linux, or Windows 10+
- GCC 9+ or Clang 10+
- `make`
- libcurl development files
- MinGW-w64 and NSIS only if you want to build Windows artifacts
- Emscripten only if you want to build the WASM target

Common commands:

```bash
make                   # build ./nihongo
make release           # optimized local build
make windows           # build win/dist/hajimu.exe
make windows-installer # build win/dist/hajimu_setup.exe
make wasm              # build WebAssembly artifacts
./nihongo --profile tests/english_numeric_vector.jp # show read/parse/evaluate timings
./nihongo --profile-ast tests/english_numeric_vector.jp # show AST-node timings
```

Release smoke tests usually include:

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

`tests/webhook_test.jp` starts a server and waits for an external/manual
request, so it is intentionally skipped in automated smoke tests.

## Documentation

English:

- [Tutorial](docs/TUTORIAL_en.md)
- [Reference Manual](docs/REFERENCE_en.md)
- [Roadmap](docs/ROADMAP_en.md)
- [English Syntax Roadmap](docs/ENGLISH_SYNTAX_ROADMAP.md)
- [English Alias Policy](docs/ENGLISH_ALIAS_POLICY.md)
- [Performance And Research Compute Design](docs/PERFORMANCE_AND_COMPUTE_DESIGN.md)
- [Plugin Development Guide](docs/PLUGIN_DEVELOPMENT.md)
- [Changelog](CHANGELOG.md)

Japanese:

- [日本語 README](README.md)
- [チュートリアル](docs/TUTORIAL.md)
- [リファレンス](docs/REFERENCE.md)
- [ロードマップ](docs/ROADMAP.md)

Project site:

- https://reoshiozawa.github.io/hajimu-document/

## How To Contribute

The most useful contributions right now are practical and small:

- report a crash or confusing error message with a minimal `.jp` / `.haj` file
- add a regression test for parser, runtime, or English alias behavior
- improve diagnostics wording
- improve English documentation so it reads naturally to non-Japanese developers
- add examples that teach one concept clearly
- test Windows installer and portable builds
- explore VS Code extension behavior with real programs
- review C runtime code for memory safety and portability issues

Before opening a pull request, please read:

- [CONTRIBUTING.md](CONTRIBUTING.md)
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)
- [SECURITY.md](SECURITY.md)

Commit message convention:

- `feat:` new feature
- `fix:` bug fix
- `docs:` documentation
- `test:` tests
- `refactor:` internal cleanup
- `perf:` performance improvement
- `chore:` build or release maintenance

## Project Links

- Repository: https://github.com/ReoShiozawa/hajimu
- Releases: https://github.com/ReoShiozawa/hajimu/releases
- Issues: https://github.com/ReoShiozawa/hajimu/issues
- Documentation site: https://reoshiozawa.github.io/hajimu-document/

## License

Hajimu is released under the [MIT License](LICENSE).

## Author

Created and maintained by [Reo Shiozawa](https://github.com/ReoShiozawa).
