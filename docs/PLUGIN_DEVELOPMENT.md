# Hajimu External Package Development Guide

This guide covers how to develop and publish external packages for the Hajimu language —
both pure jp source packages and C extension plugins.

---

## Table of Contents

1. [Package Types](#package-types)
2. [Directory Structure](#directory-structure)
3. [Writing hajimu.json](#writing-hajimujson)
4. [jp Source Packages](#jp-source-packages)
5. [C Extension Plugins (.hjp)](#c-extension-plugins-hjp)
6. [Value Type Reference](#value-type-reference)
7. [Platform-Specific Builds](#platform-specific-builds)
8. [Makefile Template](#makefile-template)
9. [Publishing a Package](#publishing-a-package)
10. [Testing and Debugging](#testing-and-debugging)
11. [FAQ](#faq)

---

## Package Types

Hajimu supports two kinds of packages.

| Type | Extension | Characteristics |
|---|---|---|
| **jp Source Package** | `.jp` | Written in Hajimu; interpreted directly |
| **C Extension Plugin** | `.hjp` | Shared library compiled from C/C++; ideal for high performance or native API access |

> **Note:** `.hjp` files are internally the same format as shared libraries
> (macOS: `.dylib`, Linux: `.so`, Windows: `.dll`), but the unified extension
> means users never need to worry about OS differences.

---

## Directory Structure

### C Extension Plugin

```
my_package/              ← package root (= GitHub repository name)
  hajimu.json            ← package manifest (required)
  README.md
  LICENSE
  Makefile               ← for C extensions (recommended)
  src/
    my_plugin.c          ← C source
  include/
    my_plugin.h
  dist/                  ← built .hjp files (recommended placement)
    my_package-macos.hjp
    my_package-linux-x64.hjp
    my_package-windows-x64.hjp
  examples/
    sample.jp
```

### jp Source Package

```
my_package/
  hajimu.json
  README.md
  main.jp                ← entry point (specified by "メイン" in hajimu.json)
  utils.jp
  examples/
    sample.jp
```

---

## Writing hajimu.json

Place `hajimu.json` at the package root.

```json
{
  "名前": "my_package",
  "バージョン": "1.0.0",
  "説明": "Package description",
  "作者": "Author Name",
  "メイン": "main.jp",
  "依存": {
    "other_package": "https://github.com/username/other_package"
  }
}
```

### For C Extension Plugins

```json
{
  "名前": "my_plugin",
  "バージョン": "1.0.0",
  "説明": "C extension plugin example",
  "作者": "Author Name",
  "メイン": "dist/my_plugin.hjp",
  "ビルドコマンド": "make build-all"
}
```

> When `"メイン"` points to a `.hjp` path, Hajimu loads it as a plugin.
> Hajimu automatically selects the correct platform binary
> (`my_plugin-macos.hjp` / `my_plugin-linux-x64.hjp` / `my_plugin-windows-x64.hjp`)
> based on the current OS (see [Platform-Specific Builds](#platform-specific-builds)).

---

## jp Source Packages

### Example

`main.jp`:

```
// Just define the functions you want to export

関数 greet(name):
    戻す "Hello, " + name + "!"
終わり

関数 add(a, b):
    戻す a + b
終わり
```

### Usage (caller side)

```
取り込む "my_package" として pkg

表示(pkg["greet"]("World"))  // → Hello, World!
表示(pkg["add"](3, 4))       // → 7
```

---

## C Extension Plugins (.hjp)

### Minimal Implementation

```c
#include "hajimu_plugin.h"

/* Function called from Hajimu */
static Value square(int argc, Value *argv) {
    double x = argv[0].number;
    return hajimu_number(x * x);
}

/* Another example: shout */
static Value shout(int argc, Value *argv) {
    const char *s = argv[0].string.data;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s!!!", s);
    return hajimu_string(buf);
}

/* Function table */
static HajimuPluginFunc functions[] = {
    {"二乗", square, 1, 1},   /* exactly 1 argument */
    {"叫ぶ", shout,  1, 1},
};

/* Initialization function (required — name must be exact) */
HAJIMU_PLUGIN_EXPORT HajimuPluginInfo *hajimu_plugin_init(void) {
    static HajimuPluginInfo info = {
        .name           = "my_plugin",
        .version        = "1.0.0",
        .author         = "Author Name",
        .description    = "Example plugin",
        .functions      = functions,
        .function_count = 2,
    };
    return &info;
}
```

### Single-Platform Build

```bash
# macOS
gcc -shared -fPIC -O2 \
    -I/path/to/jp/include \
    my_plugin.c -o dist/my_plugin-macos.hjp

# Linux
gcc -shared -fPIC -O2 \
    -I/path/to/jp/include \
    my_plugin.c -o dist/my_plugin-linux-x64.hjp -lm

# Windows (MinGW)
x86_64-w64-mingw32-gcc -shared -O2 \
    -I/path/to/jp/include \
    my_plugin.c -o dist/my_plugin-windows-x64.hjp
```

---

## Value Type Reference

Helpers and types provided by `hajimu_plugin.h`.

### Creating Values

| Function | Description | Example |
|---|---|---|
| `hajimu_null()` | Return null | `return hajimu_null();` |
| `hajimu_number(double n)` | Return a number | `return hajimu_number(3.14);` |
| `hajimu_bool(bool b)` | Return a boolean | `return hajimu_bool(true);` |
| `hajimu_string(const char *s)` | Return a string (copied) | `return hajimu_string("hello");` |

### Reading Values

```c
static Value process(int argc, Value *argv) {
    // Type check
    if (argv[0].type != VALUE_NUMBER) return hajimu_null();

    double  n   = argv[0].number;           // number
    bool    b   = argv[0].boolean;          // boolean
    char   *s   = argv[0].string.data;      // string data
    int     len = argv[0].string.length;    // string length

    // Array elements
    for (int i = 0; i < argv[0].array.length; i++) {
        Value elem = argv[0].array.elements[i];
    }

    // Dictionary value
    Value val = dict_get(&argv[0], "key");

    return hajimu_number(n * 2);
}
```

### ValueType Constants

| Type | Constant |
|---|---|
| null | `VALUE_NULL` |
| number | `VALUE_NUMBER` |
| boolean | `VALUE_BOOL` |
| string | `VALUE_STRING` |
| array | `VALUE_ARRAY` |
| dictionary | `VALUE_DICT` |

### HajimuPluginFunc Structure

```c
typedef struct {
    const char     *name;      // function name exposed to Hajimu (Unicode OK)
    HajimuPluginFn  fn;        // function pointer
    int             min_args;  // minimum argument count
    int             max_args;  // maximum argument count (-1 for variadic)
} HajimuPluginFunc;
```

---

## Platform-Specific Builds

### File Naming Convention

When `取り込む "my_plugin"` is executed, Hajimu searches in this order:

1. `my_plugin-<platform-suffix>.hjp` (platform-specific)
2. `my_plugin.hjp` (generic fallback)

| OS | Architecture | Suffix | Example filename |
|---|---|---|---|
| macOS | Apple Silicon (arm64) | `-macos-arm64` | `my_plugin-macos-arm64.hjp` |
| macOS | Intel (x86-64) | `-macos` | `my_plugin-macos.hjp` |
| Linux | x86-64 | `-linux-x64` | `my_plugin-linux-x64.hjp` |
| Linux | ARM64 | `-linux-arm64` | `my_plugin-linux-arm64.hjp` |
| Windows | x86-64 | `-windows-x64` | `my_plugin-windows-x64.hjp` |
| Windows | ARM64 | `-windows-arm64` | `my_plugin-windows-arm64.hjp` |

> **Recommendation:** Place all platform builds in `dist/` and set `"メイン"` to
> `"dist/my_plugin.hjp"` in `hajimu.json`. Hajimu will automatically pick the right file.

### Platform Macros in hajimu_plugin.h

Use these macros to branch inside your C plugin source.

```c
#include "hajimu_plugin.h"

static Value get_info(int argc, Value *argv) {
#if defined(HAJIMU_OS_MACOS)
    return hajimu_string("Running on macOS");
#elif defined(HAJIMU_OS_LINUX)
    return hajimu_string("Running on Linux");
#elif defined(HAJIMU_OS_WINDOWS)
    return hajimu_string("Running on Windows");
#else
    return hajimu_string("Unknown OS");
#endif
}
```

| Macro | Meaning |
|---|---|
| `HAJIMU_OS_MACOS` | macOS |
| `HAJIMU_OS_LINUX` | Linux |
| `HAJIMU_OS_WINDOWS` | Windows |
| `HAJIMU_OS_NAME` | OS name string literal |
| `HAJIMU_ARCH_ARM64` | ARM64 architecture |
| `HAJIMU_ARCH_X64` | x86-64 architecture |
| `HAJIMU_ARCH_NAME` | Architecture name string literal |
| `HAJIMU_HJP_SUFFIX` | Platform suffix string (e.g. `"-linux-x64"`) |

---

## Makefile Template

Cross-platform build Makefile for a C extension plugin.

```makefile
# =============================================================================
# my_plugin — Hajimu C Extension Plugin
# =============================================================================
PLUGIN_NAME = my_plugin
DIST        = dist
SRCS        = src/my_plugin.c

# Auto-locate jp/include (installed or source repo)
HAJIMU_INC  = $(abspath $(firstword $(wildcard \
    ../../jp/include \
    ../jp/include \
    $(HOME)/.hajimu/include \
)))

# Cross-compilers
LINUX_CC   ?= x86_64-linux-musl-gcc
WIN_CC     ?= x86_64-w64-mingw32-gcc

.PHONY: build-all build-macos build-linux build-windows

build-all: build-macos build-linux build-windows
	@echo "  All platform builds complete: $(DIST)/"

build-macos:
	@mkdir -p $(DIST)
	cc -shared -fPIC -O2 -std=c11 \
	  -I$(HAJIMU_INC) -Iinclude \
	  $(SRCS) \
	  -o $(DIST)/$(PLUGIN_NAME)-macos.hjp
	@echo "  macOS: $(DIST)/$(PLUGIN_NAME)-macos.hjp"

build-linux:
	@mkdir -p $(DIST)
	$(LINUX_CC) -shared -fPIC -O2 -std=gnu11 \
	  -I$(HAJIMU_INC) -Iinclude \
	  $(SRCS) \
	  -Wl,--allow-shlib-undefined \
	  -o $(DIST)/$(PLUGIN_NAME)-linux-x64.hjp
	@echo "  Linux: $(DIST)/$(PLUGIN_NAME)-linux-x64.hjp"

build-windows:
	@mkdir -p $(DIST)
	$(WIN_CC) -shared -O2 -std=gnu11 \
	  -D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN \
	  -I$(HAJIMU_INC) -Iinclude \
	  $(SRCS) \
	  -static-libgcc \
	  -o $(DIST)/$(PLUGIN_NAME)-windows-x64.hjp
	@echo "  Windows: $(DIST)/$(PLUGIN_NAME)-windows-x64.hjp"

clean:
	rm -rf $(DIST)
```

### Installing Cross-Compilers (on macOS)

Build for all platforms from a single macOS machine.

```bash
# Linux target (musl libc, static)
brew install FiloSottile/musl-cross/musl-cross

# Windows target (MinGW-w64)
brew install mingw-w64
```

---

## Publishing a Package

### 1. Push to GitHub

```bash
git init
git add .
git commit -m "initial release"
git remote add origin https://github.com/username/my_package
git push -u origin main
```

### 2. How Users Install It

In the user's project:

```bash
# Install directly from URL
nihongo install https://github.com/username/my_package

# Or add it to hajimu.json and run:
nihongo install
```

User's `hajimu.json`:

```json
{
  "名前": "my_project",
  "依存": {
    "my_package": "https://github.com/username/my_package"
  }
}
```

### 3. Usage (user's Hajimu code)

```
取り込む "my_package" として plugin

変数 result = plugin["二乗"](5)
表示(result)  // → 25
```

---

## Testing and Debugging

### Test the Plugin Directly from Hajimu

```
取り込む "./dist/my_plugin-macos.hjp" として plugin

テスト("square test", 関数():
    期待(plugin["二乗"](4), 16)
    期待(plugin["二乗"](0), 0)
    期待(plugin["二乗"](-3), 9)
終わり)

テスト実行()
```

### Emitting Error Messages from C

```c
#include <stdio.h>
#include "hajimu_plugin.h"

static Value safe_divide(int argc, Value *argv) {
    double divisor = argv[1].number;
    if (divisor == 0) {
        fprintf(stderr, "[my_plugin] Error: division by zero\n");
        return hajimu_null();
    }
    return hajimu_number(argv[0].number / divisor);
}
```

### Debug Build

```bash
CFLAGS="-g -fsanitize=address" make build-macos
```

---

## FAQ

### Q: Where do I get `hajimu_plugin.h`?

From the `jp` source repository at `jp/include/hajimu_plugin.h`.

```bash
git clone https://github.com/ReoShiozawa/hajimu jp
# Use jp/include/hajimu_plugin.h
```

A future `nihongo install-dev` command will place it automatically.

### Q: Where should I define `_GNU_SOURCE` or `_DARWIN_C_SOURCE`?

`hajimu_plugin.h` defines `_POSIX_C_SOURCE 200809L`. If you need GNU extensions
(e.g. `dladdr`), define the macro **before** the include:

```c
#ifdef __linux__
  #define _GNU_SOURCE
#endif
#ifdef __APPLE__
  #define _DARWIN_C_SOURCE
#endif

#include "hajimu_plugin.h"
```

### Q: How do I use large libraries like SDL2 or OpenGL?

For Windows, include the import lib (`.dll.a`) in the `vendor/` directory and use
`-Wl,--allow-shlib-undefined` for Linux builds (the SDL2.dll is resolved at runtime).
See the [engine_render](https://github.com/ReoShiozawa/hajimu_engine_render) repository for a working example.

### Q: Can a plugin call back into Hajimu code?

Yes. Implement `hajimu_plugin_set_runtime` to receive the `HajimuRuntime` pointer,
then call `hajimu_call()` to invoke any Hajimu function (user-defined, lambda, or built-in).
See the `HajimuRuntime` struct in `hajimu_plugin.h` for details.

### Q: Can I use Japanese function names?

Yes. Hajimu identifiers are UTF-8, so Japanese names work directly.

```c
static HajimuPluginFunc functions[] = {
    {"円の面積", circle_area_fn, 1, 1},  // called as plugin["円の面積"](r)
};
```

---

## Related Documentation

- [REFERENCE_en.md](REFERENCE_en.md) — Hajimu language reference (English)
- [TUTORIAL_en.md](TUTORIAL_en.md) — Hajimu beginner tutorial (English)
- [CONTRIBUTING.md](../CONTRIBUTING.md) — Contribution guide
