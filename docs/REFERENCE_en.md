# Hajimu Reference Manual

## Overview

Hajimu (はじむ) is a programming language designed around natural Japanese expression.
It is executed by a C-language interpreter.

> **Note:** All keywords are Japanese. Code examples are in authentic Hajimu syntax.
> Each section includes a keyword table for reference.

## Table of Contents

1. [Basic Syntax](#basic-syntax)
2. [Data Types](#data-types)
3. [Operators](#operators)
4. [Control Flow](#control-flow)
5. [Functions](#functions)
6. [Lambdas (Anonymous Functions)](#lambdas-anonymous-functions)
7. [Classes](#classes)
8. [Exception Handling](#exception-handling)
9. [Modules](#modules)
10. [Standard Library](#standard-library)
11. [Higher-Order Array Functions](#higher-order-array-functions)
12. [Regular Expressions](#regular-expressions)
13. [System Utilities](#system-utilities)
14. [JSON](#json)
15. [HTTP Client](#http-client)
16. [Webhook / HTTP Server](#webhook--http-server)
17. [Async](#async)
18. [Parallel Execution](#parallel-execution)
19. [Scheduler](#scheduler)
20. [WebSocket](#websocket)
21. [Enumerations](#enumerations)
22. [Pattern Matching](#pattern-matching)
23. [Generators](#generators)
24. [Command-Line Arguments](#command-line-arguments)
25. [Debugging](#debugging)
26. [Sets](#sets)
27. [Operator Overloading](#operator-overloading)
28. [Decorators](#decorators)
29. [Access Modifiers](#access-modifiers)
30. [Test Framework](#test-framework)
31. [Custom Exceptions](#custom-exceptions)
32. [Documentation Comments](#documentation-comments)
33. [Type Aliases](#type-aliases)
34. [Module Namespaces](#module-namespaces)
35. [Package Management](#package-management)
36. [C Extension Plugins](#c-extension-plugins)
37. [List Comprehensions](#list-comprehensions)

---

## Basic Syntax

### Comments

```
// Single-line comment

/* 
   Multi-line comment
*/
```

### Variables and Constants

```
変数 名前 = "Taro"        // mutable variable
定数 円周率 = 3.14159     // immutable constant
```

| Keyword | Meaning |
|---|---|
| `変数` | variable (mutable) |
| `定数` | constant (immutable) |

### Optional Type Annotations

```
変数 数 は 数値 = 42
変数 名前 は 文字列 = "hello"
```

| Annotation | Meaning |
|---|---|
| `は 数値` | : number |
| `は 文字列` | : string |

---

## Data Types

| Type | Description | Examples |
|---|---|---|
| 数値 (number) | Floating-point number | `42`, `3.14`, `-17` |
| 文字列 (string) | Text (UTF-8) | `"こんにちは"` |
| 真偽 (boolean) | Boolean | `真` (true), `偽` (false) |
| 配列 (array) | Ordered collection | `[1, 2, 3]` |
| 辞書 (dictionary) | Key-value pairs | `{"name": "Taro", "age": 25}` |
| 無 (null) | No value | `無` |
| ジェネレータ (generator) | Lazy value sequence | created with `生成関数` |

---

## Operators

### Arithmetic

| Operator | Description |
|---|---|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division |
| `%` | Modulo |
| `**` | Exponentiation |

### Comparison

| Operator | Description |
|---|---|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |

### Logical

| Operator | Meaning |
|---|---|
| `かつ` | AND |
| `または` | OR |
| `でない` | NOT |

### Pipe Operator

Passes the left-hand value as the first argument of the right-hand function.

```
関数 二倍(数):
    戻す 数 * 2
終わり

関数 足す十(数):
    戻す 数 + 10
終わり

変数 結果 = 5 |> 二倍 |> 足す十  // (5 * 2) + 10 = 20
表示(結果)  // 20
```

### Bitwise Functions

| Function | Description | Example |
|---|---|---|
| `ビット積(a, b)` | Bitwise AND | `ビット積(12, 10)` → 8 |
| `ビット和(a, b)` | Bitwise OR | `ビット和(12, 10)` → 14 |
| `ビット排他(a, b)` | Bitwise XOR | `ビット排他(12, 10)` → 6 |
| `ビット否定(a)` | Bitwise NOT | `ビット否定(0)` → -1 |
| `左シフト(a, b)` | Left shift | `左シフト(1, 4)` → 16 |
| `右シフト(a, b)` | Right shift | `右シフト(16, 2)` → 4 |

### Ternary Operator

```
変数 結果 = (x > 0) ? "positive" : "non-positive"
変数 最大値 = (a > b) ? a : b
```

### Null Coalescing Operator

Uses the right-hand value when the left-hand side is `無`.

```
変数 名前 = 無
変数 表示名 = 名前 ?? "Guest"  // "Guest"

変数 値 = "Taro"
変数 結果 = 値 ?? "default"    // "Taro"
```

---

## Control Flow

### If / Else

```
もし 条件 なら
    // when condition is true
それ以外
    // when condition is false
終わり
```

### Else-If Chain

```
関数 成績(点数):
    もし 点数 >= 90 なら
        戻す "A"
    それ以外もし 点数 >= 70 なら
        戻す "B"
    それ以外もし 点数 >= 50 なら
        戻す "C"
    それ以外
        戻す "F"
    終わり
終わり
```

| Keyword | Meaning |
|---|---|
| `もし … なら` | if … then |
| `それ以外もし` | else if |
| `それ以外` | else |
| `終わり` | end |

### Loops

#### While Loop

```
条件 x < 10 の間
    表示(x)
    x = x + 1
終わり
```

#### Range Loop

```
i を 1 から 10 繰り返す
    表示(i)
終わり
```

### Loop Control

```
抜ける    // break — exit the loop
続ける    // continue — skip to next iteration
```

### For-Each Loop

Iterates over arrays, strings, and dictionaries.

```
変数 果物 = ["apple", "orange", "grape"]
各 名前 を 果物 の中:
    表示(名前)
終わり
```

#### Each character of a string

```
各 文字 を "hello" の中:
    表示(文字)
終わり
```

#### Dictionary keys

```
変数 情報 = {"名前": "Taro", "年齢": "25"}
各 項目 を 情報 の中:
    表示(項目)  // prints "名前", "年齢"
終わり
```

### Switch Statement

```
選択 値:
    場合 1:
        表示("one")
    場合 2:
        表示("two")
    場合 3:
        表示("three")
    既定:
        表示("other")
終わり
```

| Keyword | Meaning |
|---|---|
| `選択` | switch |
| `場合` | case |
| `既定` | default |

---

## Functions

### Definition

```
関数 挨拶(名前):
    表示("Hello, " + 名前 + "!")
終わり
```

### Return Value

```
関数 足し算(a, b):
    戻す a + b
終わり

変数 結果 = 足し算(3, 5)
```

> **Note:** `戻す` and `返す` are synonymous — both return a value from a function.

### Type-Annotated Functions

```
関数 掛け算(x は 数値, y は 数値) は 数値:
    戻す x * y
終わり
```

### Default Arguments

```
関数 挨拶(名 = "Guest", 接頭 = "Hello"):
    戻す 接頭 + ", " + 名 + "!"
終わり

表示(挨拶())                       // "Hello, Guest!"
表示(挨拶("Taro"))                 // "Hello, Taro!"
表示(挨拶("Taro", "Hey"))          // "Hey, Taro!"
```

### Variadic Arguments

Prefix an argument with `*` to receive any number of values as an array.

```
関数 合計(*数列):
    変数 結果 = 0
    各 数 を 数列 の中:
        結果 = 結果 + 数
    終わり
    戻す 結果
終わり

表示(合計(1, 2, 3))        // 6
表示(合計(10, 20, 30, 40)) // 100
```

### Spread Operator

Expand an array into individual arguments.

```
関数 足し算(a, b, c):
    戻す a + b + c
終わり

変数 値 = [10, 20, 30]
表示(足し算(...値))  // 60
```

### Destructuring Assignment

Unpack array elements into individual variables.

```
変数 [a, b, c] = [1, 2, 3]
表示(a)  // 1
表示(b)  // 2
表示(c)  // 3
```

---

## Lambdas (Anonymous Functions)

Create a nameless function, assign it to a variable, or pass it as an argument.

### Basic Syntax

```
変数 二倍 = 関数(x):
    戻す x * 2
終わり

表示(二倍(5))  // 10
```

### Passing as an Argument

```
変数 数列 = [1, 2, 3, 4, 5]
変数 結果 = 変換(数列, 関数(x):
    戻す x * 10
終わり)
表示(結果)  // [10, 20, 30, 40, 50]
```

### Closures

Lambdas capture variables from the enclosing scope.

```
関数 掛け算器(倍率):
    戻す 関数(x):
        戻す x * 倍率
    終わり
終わり

変数 三倍 = 掛け算器(3)
表示(三倍(5))  // 15
```

---

## Classes

### Class Definition

```
型 人:
    初期化(名前, 年齢):
        自分.名前 = 名前
        自分.年齢 = 年齢
    終わり

    関数 自己紹介():
        表示("I am " + 自分.名前 + ", " + 文字列化(自分.年齢) + " years old.")
    終わり
終わり
```

| Keyword | Meaning |
|---|---|
| `型` | class |
| `初期化` | constructor |
| `自分` | self / this |

### Creating an Instance

```
変数 太郎 = 新規 人("Taro", 25)
太郎.自己紹介()
```

| Keyword | Meaning |
|---|---|
| `新規` | new |

### Inheritance

```
型 学生 継承 人:
    初期化(名前, 年齢, 学校):
        自分.名前 = 名前
        自分.年齢 = 年齢
        自分.学校 = 学校
    終わり

    関数 通学():
        表示(自分.名前 + " attends " + 自分.学校)
    終わり
終わり
```

| Keyword | Meaning |
|---|---|
| `継承` | extends (inherits from) |

### Calling the Parent Method (super)

Use `親.メソッド名(args)` to call a parent class method.

```
型 動物:
    初期化(名前):
        自分.名前 = 名前
    終わり

    関数 挨拶():
        戻す 自分.名前 + " here"
    終わり
終わり

型 犬 継承 動物:
    初期化(名前, 種類):
        親.初期化(名前)
        自分.種類 = 種類
    終わり

    関数 挨拶():
        変数 基本 = 親.挨拶()
        戻す 基本 + " (" + 自分.種類 + ")"
    終わり
終わり
```

| Keyword | Meaning |
|---|---|
| `親` | super (parent class) |

### Static Methods

Define with `静的 関数`. Call as `ClassName.method()` without an instance.

```
型 数学ツール:
    初期化():
    終わり

    静的 関数 足す(a, b):
        戻す a + b
    終わり

    静的 関数 掛ける(a, b):
        戻す a * b
    終わり
終わり

表示(数学ツール.足す(3, 5))    // 8
表示(数学ツール.掛ける(4, 6))  // 24
```

### isinstance Check

```
変数 太郎 = 新規 人("Taro", 25)
表示(型判定(太郎, "人"))    // 真 (true)
表示(型判定(太郎, "学生"))  // 偽 (false)
```

---

## Exception Handling

### Basic Syntax

```
試行:
    投げる "An error occurred"
捕獲 エラー:
    表示("Error: " + エラー)
最終:
    // always runs
終わり
```

| Keyword | Meaning |
|---|---|
| `試行:` | try |
| `捕獲 エラー:` | catch (binds error to 変数) |
| `最終:` | finally |
| `投げる` | throw |

---

## Modules

### Importing

```
// Import by file path
取り込む "math.jp"
取り込む "utils/helpers.jp"

// Import by package name (works with package manager)
取り込む "my-package"
```

### Resolution Order

1. **Relative to the caller** — directory of the currently executing file
2. **Current working directory**
3. **Package search** — `hajimu_packages/` and `~/.hajimu/packages/`

### Deduplication

The same file is loaded only once. Subsequent `取り込む` for the same path are ignored.

### Circular Import Detection

Circular imports are detected automatically to prevent infinite loops.

---

## Standard Library

### I/O

| Function | Description | Example |
|---|---|---|
| `表示(...)` | Print to stdout | `表示("hello")` |
| `入力(prompt)` | Read a line from stdin | `変数 s = 入力("Name: ")` |

### Collections

| Function | Description |
|---|---|
| `長さ(val)` | Length of array or string |
| `追加(arr, val)` | Append to array |
| `削除(arr, index)` | Remove element at index |
| `ソート(arr)` | Sort array |
| `逆順(arr)` | Reverse array |
| `スライス(arr, start, end)` | Sub-array |
| `位置(arr, val)` | Index of value |
| `存在(arr, val)` | Check if value exists |
| `末尾削除(arr)` | Pop and return last element |
| `探す(arr, fn)` | First element matching predicate |
| `全て(arr, fn)` | All elements satisfy predicate |
| `一つでも(arr, fn)` | Any element satisfies predicate |
| `一意(arr)` | Remove duplicates |
| `圧縮(arr1, arr2)` | Zip two arrays into pairs |
| `平坦化(arr)` | Flatten nested array |
| `挿入(arr, pos, val)` | Insert at position |
| `比較ソート(arr, fn)` | Sort with custom comparator |

### Type Conversion

| Function | Description |
|---|---|
| `数値化(val)` | Convert to number |
| `文字列化(val)` | Convert to string |

### Math Functions

| Function | Description |
|---|---|
| `絶対値(n)` | Absolute value |
| `平方根(n)` | Square root |
| `切り捨て(n)` | Floor |
| `切り上げ(n)` | Ceiling |
| `四捨五入(n)` | Round |
| `最大(...)` | Maximum |
| `最小(...)` | Minimum |
| `乱数()` | Random float [0, 1) |
| `乱数整数(min, max)` | Random integer in range |
| `正弦(n)` | Sine (sin) |
| `余弦(n)` | Cosine (cos) |
| `正接(n)` | Tangent (tan) |
| `対数(n)` | Natural logarithm (ln) |
| `常用対数(n)` | Common logarithm (log10) |

### Math Constants

| Constant | Description | Value |
|---|---|---|
| `円周率` | π | 3.14159265... |
| `自然対数の底` | e | 2.71828182... |

### String Functions

| Function | Description |
|---|---|
| `分割(str, sep)` | Split string |
| `結合(arr, sep)` | Join array into string |
| `置換(str, search, rep)` | Replace all occurrences |
| `大文字(str)` | To uppercase |
| `小文字(str)` | To lowercase |
| `空白除去(str)` | Trim whitespace |
| `検索(str, query)` | Find position |
| `部分文字列(str, start, end)` | Substring |
| `始まる(str, prefix)` | Starts with |
| `終わる(str, suffix)` | Ends with |
| `文字コード(str [, pos])` | Character code |
| `コード文字(code)` | Character from code |
| `繰り返し(str, n)` | Repeat string |

### String Interpolation

Embed expressions inside `{}` in a string.

```
変数 名前 = "Taro"
変数 年齢 = 25
表示("{名前} is {年齢} years old.")  // "Taro is 25 years old."
表示("{1 + 2 + 3}")                  // "6"
```

### Multi-line Strings

Wrap in `"""..."""` to include newlines.

```
変数 複数行 = """This is
a multi-line
string."""

表示(複数行)
```

### Range Function

| Call | Result |
|---|---|
| `範囲(n)` | [0, 1, …, n-1] |
| `範囲(start, end)` | [start, …, end-1] |
| `範囲(start, end, step)` | [start, start+step, …] |

### File I/O

| Function | Description |
|---|---|
| `読み込む(path)` | Read file as string |
| `書き込む(path, text)` | Write text to file |
| `ファイル存在(path)` | Check if file exists |
| `追記(path, text)` | Append text to file |
| `ディレクトリ一覧(path)` | List directory contents |
| `ディレクトリ作成(path)` | Create directory |

### Date / Time

| Function | Description |
|---|---|
| `現在時刻()` | Unix timestamp |
| `日付()` | Current date (YYYY-MM-DD) |
| `時間()` | Current time (HH:MM:SS) |

### Type-Check Functions

All return a boolean.

| Function | Description |
|---|---|
| `数値か(val)` | Is number? |
| `文字列か(val)` | Is string? |
| `真偽か(val)` | Is boolean? |
| `配列か(val)` | Is array? |
| `辞書か(val)` | Is dictionary? |
| `関数か(val)` | Is function? |
| `無か(val)` | Is null? |

### Utilities

| Function | Description |
|---|---|
| `表明(cond [, msg])` | Assert — throws if false |
| `型判定(val, "TypeName")` | isinstance check |

### Negative Indexing

Access elements from the end of an array or string.

```
変数 配列 = [10, 20, 30, 40, 50]
表示(配列[-1])   // 50 (last)
表示(配列[-2])   // 40 (second from last)
```

### Dictionary Functions

| Function | Description |
|---|---|
| `キー(dict)` | Array of keys |
| `値一覧(dict)` | Array of values |
| `含む(dict, key)` | Key existence check |

---

## Higher-Order Array Functions

### map — `変換`

Apply a function to every element, returning a new array.

```
変数 数列 = [1, 2, 3, 4, 5]
変数 二倍 = 変換(数列, 関数(x):
    戻す x * 2
終わり)
表示(二倍)  // [2, 4, 6, 8, 10]
```

### filter — `抽出`

Keep only elements that satisfy a predicate.

```
変数 偶数 = 抽出([1, 2, 3, 4, 5], 関数(x):
    戻す x % 2 == 0
終わり)
表示(偶数)  // [2, 4]
```

### reduce — `集約`

Fold an array into a single value. The third argument is the initial accumulator.

```
変数 合計 = 集約([1, 2, 3, 4, 5], 関数(acc, x):
    戻す acc + x
終わり, 0)
表示(合計)  // 15
```

### forEach — `反復`

Run a function for each element (no return value).

```
反復(["A", "B", "C"], 関数(x):
    表示("item:", x)
終わり)
```

---

## Regular Expressions

Uses POSIX Extended Regular Expressions.

### `正規一致(str, pattern)` — Match

Returns `真` if the string matches the pattern.

```
表示(正規一致("hello123", "[a-z]+[0-9]+"))  // 真
表示(正規一致("hello", "[0-9]+"))            // 偽
```

### `正規検索(str, pattern)` — Search

Returns an array of matched strings. Index 0 is the full match; subsequent indices are capture groups.

```
変数 結果 = 正規検索("Phone: 03-1234-5678", "[0-9]+-[0-9]+-[0-9]+")
表示(結果)  // ["03-1234-5678"]
```

### `正規置換(str, pattern, replacement)` — Replace All

```
変数 結果 = 正規置換("abc 123 def 456", "[0-9]+", "XXX")
表示(結果)  // "abc XXX def XXX"
```

---

## System Utilities

### `待つ(seconds)` — Sleep

```
待つ(1.5)  // pause 1.5 seconds
```

### `実行(command)` — Shell Execute

Runs a shell command and returns stdout as a string.

```
変数 結果 = 実行("ls -la")
表示(結果)
```

### `環境変数(name)` — Get Environment Variable

```
変数 ホーム = 環境変数("HOME")
表示(ホーム)  // /Users/username
```

### `環境変数設定(name, value)` — Set Environment Variable

```
環境変数設定("MY_VAR", "test")
表示(環境変数("MY_VAR"))  // test
```

### `終了([code])` — Exit

```
終了()    // exit with code 0
終了(1)   // exit with code 1
```

### Platform Constants

| Constant | Description | Example |
|---|---|---|
| `システム名` | OS name | `"macOS"` / `"Linux"` / `"Windows"` |
| `アーキテクチャ` | CPU architecture | `"arm64"` / `"x86-64"` |
| `はじむバージョン` | Interpreter version | `"1.3.1"` |
| `システム["OS"]` | Same as `システム名` | |
| `システム["区切り文字"]` | Path separator | `/` (Windows: `\`) |
| `システム["改行"]` | Newline | `\n` (Windows: `\r\n`) |

---

## JSON

### `JSON化(val)` — Serialize

```
変数 データ = {"名前": "Taro", "年齢": 25, "趣味": ["reading", "games"]}
変数 json = JSON化(データ)
表示(json)  // {"名前":"Taro","年齢":25,"趣味":["reading","games"]}
```

### `JSON解析(str)` — Deserialize

```
変数 結果 = JSON解析("{\"name\":\"Taro\",\"age\":25}")
表示(結果["name"])  // Taro
表示(結果["age"])   // 25
```

---

## HTTP Client

Built on libcurl. All functions return a response dictionary.

### Response Dictionary

| Key | Description |
|---|---|
| `"状態"` | HTTP status code |
| `"本文"` | Response body (string) |
| `"ヘッダー"` | Response headers (dict) |
| `"エラー"` | Error message (on failure) |

### `HTTP取得(url [, headers])` — GET

```
変数 応答 = HTTP取得("https://api.example.com/data")
表示(応答["状態"])  // 200
変数 データ = JSON解析(応答["本文"])
```

### `HTTP送信(url, body [, headers])` — POST

If `body` is a dict or array it is automatically serialized to JSON.

```
変数 結果 = HTTP送信("https://api.example.com/users", {"名前": "Taro"})
```

### `HTTP更新(url, body [, headers])` — PUT

Same usage as `HTTP送信`.

### `HTTP削除(url [, headers])` — DELETE

```
変数 結果 = HTTP削除("https://api.example.com/users/1")
```

### `HTTPリクエスト(method, url [, body, headers])` — Custom Method

```
変数 結果 = HTTPリクエスト("PATCH", "https://api.example.com/users/1", {"名前": "Hanako"})
```

### URL Encoding

```
変数 enc = URLエンコード("hello world")
変数 dec = URLデコード(enc)
```

---

## Webhook / HTTP Server

### `サーバー起動(port [, timeout])` — Start Server

Waits for a single incoming HTTP request and returns it as a dictionary.
Default timeout is 60 seconds.

```
変数 リクエスト = サーバー起動(8080)
表示(リクエスト["メソッド"])  // POST
表示(リクエスト["パス"])      // /webhook
表示(リクエスト["本文"])
表示(リクエスト["データ"])    // auto-parsed JSON body
```

### Request Dictionary

| Key | Description |
|---|---|
| `"メソッド"` | HTTP method |
| `"パス"` | Request path |
| `"本文"` | Request body (string) |
| `"ヘッダー"` | Request headers (dict) |
| `"クエリ"` | Query string |
| `"データ"` | Auto-parsed JSON body |

### Webhook Loop Example

```
変数 続行 = 真
条件 続行 の間
    変数 リクエスト = サーバー起動(8080, 300)
    変数 メソッド = リクエスト["メソッド"]
    もし メソッド なら
        表示("Received: " + メソッド + " " + リクエスト["パス"])
    終わり
終わり
```

---

## Async

### `非同期実行(fn [, args...])` — Run Async

Executes a function asynchronously and returns a task ID.

```
関数 重い処理():
    待つ(1)
    戻す "done"
終わり

変数 タスク = 非同期実行(重い処理)
変数 結果 = 待機(タスク)
表示(結果)  // "done"
```

### `待機(taskId)` — Await

Wait for an async task to finish and return its result.

### `全待機(taskArray)` — Await All

```
変数 タスク1 = 非同期実行(処理A)
変数 タスク2 = 非同期実行(処理B)
変数 結果 = 全待機([タスク1, タスク2])
```

### `タスク状態(taskId)` — Task Status

Returns `"実行中"` (running), `"完了"` (done), or `"エラー"` (error).

---

## Parallel Execution

### `並列実行(fnArray)` — Run in Parallel

Runs multiple functions in parallel threads and returns an array of results.

```
変数 結果 = 並列実行([
    関数(): 戻す 1 + 1 終わり,
    関数(): 戻す 2 + 2 終わり,
    関数(): 戻す 3 + 3 終わり
])
表示(結果)  // [2, 4, 6]
```

### Mutex

```
変数 鍵 = 排他作成()
排他実行(鍵, 関数():
    表示("exclusive section")
終わり)
```

### Channels (Thread Communication)

| Function | Description |
|---|---|
| `チャネル作成([capacity])` | Create a channel |
| `チャネル送信(ch, val)` | Send value to channel |
| `チャネル受信(ch)` | Receive value from channel |
| `チャネル閉じる(ch)` | Close channel |

---

## Scheduler

### `定期実行(fn, intervalSec)` — Repeat

Runs a function at the given interval. Returns a schedule ID.

```
変数 id = 定期実行(関数():
    表示("tick")
終わり, 2.0)

待つ(7)
スケジュール停止(id)
```

### `遅延実行(fn, delaySec)` — Delay Once

```
遅延実行(関数():
    表示("runs after 3 s")
終わり, 3.0)
```

### Stop

```
スケジュール停止(id)    // stop one schedule
全スケジュール停止()    // stop all schedules
```

---

## WebSocket

| Function | Description |
|---|---|
| `WS接続(url)` | Connect to WebSocket server |
| `WS送信(conn, msg)` | Send message |
| `WS受信(conn [, timeout])` | Receive message |
| `WS切断(conn)` | Disconnect |
| `WS状態(conn)` | Connection state |

```
変数 接続 = WS接続("ws://echo.websocket.org")
WS送信(接続, "Hello")
変数 応答 = WS受信(接続)
表示(応答)
WS切断(接続)
```

---

## Enumerations

`列挙` assigns sequential integer values starting from 0 to a group of names.

```
列挙 方向:
    北
    南
    東
    西
終わり

表示(方向["北"])  // 0
表示(方向["南"])  // 1
表示(方向["東"])  // 2
表示(方向["西"])  // 3
```

| Keyword | Meaning |
|---|---|
| `列挙` | enum |

---

## Pattern Matching

Use `照合` for pattern matching on a value.

```
関数 分類(値):
    照合 値:
        場合 1 => 戻す "one"
        場合 2 => 戻す "two"
        場合 3, 4, 5 => 戻す "three to five"
        場合 _ => 戻す "other"
    終わり
終わり
```

- Multiple values can be listed after `場合` separated by commas
- `_` is a wildcard that matches anything

| Keyword | Meaning |
|---|---|
| `照合` | match |
| `場合 … =>` | case … => |
| `_` | wildcard |

---

## Generators

Define with `生成関数`; yield values with `譲渡`.

### Basic Usage

```
生成関数 数列():
    譲渡 1
    譲渡 2
    譲渡 3
終わり

変数 gen = 数列()
表示(次(gen))    // 1
表示(次(gen))    // 2
表示(完了(gen))  // 偽 (false — not done yet)
表示(次(gen))    // 3
表示(完了(gen))  // 真 (true — exhausted)
```

| Function | Description |
|---|---|
| `次(gen)` | Get next value |
| `完了(gen)` | Check if exhausted |
| `全値(gen)` | Get all remaining values as an array |

### Fibonacci Example

```
生成関数 フィボナッチ(n):
    変数 a = 0
    変数 b = 1
    i を 0 から n 繰り返す
        譲渡 a
        変数 次の値 = a + b
        a = b
        b = 次の値
    終わり
終わり

変数 fib = フィボナッチ(10)
表示(全値(fib))  // [0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55]
```

| Keyword | Meaning |
|---|---|
| `生成関数` | generator function |
| `譲渡` | yield |

---

## Command-Line Arguments

Arguments passed to the script are available via the `引数` array.

```bash
$ nihongo script.jp arg1 arg2 arg3
```

```
// script.jp
表示("Argument count:", 長さ(引数))
各 値 を 引数 の中:
    表示("arg:", 値)
終わり
```

---

## Debugging

### Debug Mode

```bash
nihongo -d program.jp
```

### Debug Commands

| Key | Action |
|---|---|
| Enter | Step to next statement |
| `v` | Show current variables |
| `c` | Continue (exit step mode) |

### Other CLI Options

```bash
nihongo -h           # help
nihongo -v           # version
nihongo -t file.jp   # print tokens
nihongo -a file.jp   # print AST
```

---

## Sets

Sets hold unique values with no duplicates.

```
変数 s = 集合(1, 2, 3, 2)      // {1, 2, 3}
集合追加(s, 4)                  // {1, 2, 3, 4}
集合削除(s, 2)                  // {1, 3, 4}
表示(集合含む(s, 3))            // 真
```

### Set Operations

```
変数 a = 集合(1, 2, 3)
変数 b = 集合(2, 3, 4)

和集合(a, b)    // {1, 2, 3, 4}  union
積集合(a, b)    // {2, 3}        intersection
差集合(a, b)    // {1}           difference
```

---

## Operator Overloading

Define special methods on a class to override operator behavior.

| Operator | Method name |
|---|---|
| `+` | `足す(other)` |
| `-` | `引く(other)` |
| `*` | `掛ける(other)` |
| `/` | `割る(other)` |
| `%` | `剰余(other)` |
| `==` | `等しい(other)` |
| `!=` | `等しくない(other)` |
| `<` | `小さい(other)` |
| `>` | `大きい(other)` |
| `<=` | `以下(other)` |
| `>=` | `以上(other)` |

```
型 ベクトル:
    初期化(x, y):
        自分.x = x
        自分.y = y
    終わり
    
    関数 足す(他):
        戻す ベクトル(自分.x + 他.x, 自分.y + 他.y)
    終わり
終わり

変数 v1 = 新規 ベクトル(1, 2)
変数 v2 = 新規 ベクトル(3, 4)
変数 v3 = v1 + v2   // ベクトル(4, 6)
```

---

## Decorators

Apply `@decorator` to a function to wrap or extend it.

```
関数 二倍化(f):
    戻す 関数(x):
        戻す f(x) * 2
    終わり
終わり

@二倍化
関数 値取得(x):
    戻す x + 1
終わり

表示(値取得(5))  // 12  — (5+1)*2
```

`@decorator` is syntactic sugar for:

```
値取得 = 二倍化(値取得)
```

---

## Access Modifiers

Fields whose names begin with `_` are private and cannot be accessed outside the class.

```
型 口座:
    初期化(残高):
        自分._残高 = 残高
    終わり
    
    関数 残高取得():
        戻す 自分._残高
    終わり
終わり

変数 a = 新規 口座(1000)
表示(a.残高取得())  // 1000
// a._残高           // Error: cannot access private field
```

**Rules:**
- Fields starting with `_` are private.
- Accessible inside the class via `自分.`.
- External access throws an exception (catchable with `試行/捕獲`).

---

## Test Framework

### Define and Run Tests

```
テスト("addition test", 関数():
    期待(1 + 2, 3, "1+2 should be 3")
終わり)

テスト("string test", 関数():
    期待(長さ("hello"), 5)
終わり)

変数 結果 = テスト実行()
// 結果 = {成功: 2, 失敗: 0, 合計: 2}
```

### Test Functions

| Function | Description |
|---|---|
| `テスト(name, fn)` | Register a test case |
| `テスト実行()` | Run all tests; return results |
| `期待(actual, expected [, msg])` | Assert equality |
| `期待エラー(fn)` | Assert the function throws |

---

## Custom Exceptions

### Throw and Catch a Structured Exception

```
試行:
    投げる 例外作成("MathError", "Division by zero")
捕まえる エラー:
    もし エラー["種類"] == "MathError" なら
        表示("Math error: " + エラー["メッセージ"])
    終わり
終わり
```

### `例外作成(type, message)` — Create Exception

Returns a structured exception dictionary:
```
{種類: "MathError", メッセージ: "Division by zero"}
```

---

## Documentation Comments

Attach documentation to functions or classes, queryable at runtime.

```
文書化("greet", "Takes a name and returns a greeting string.")

関数 挨拶(名前):
    戻す "Hello, " + 名前 + "!"
終わり

表示(文書("greet"))  // "Takes a name and returns a greeting string."
```

---

## Type Aliases

Create alternative names for existing types, usable with `型判定`.

```
型別名("整数", "数値")
型別名("テキスト", "文字列")

表示(型判定(42, "整数"))          // 真
表示(型判定("hello", "テキスト")) // 真
```

---

## Module Namespaces

Import a module under an alias to use it as a namespace.

### Basic Syntax

```
// Direct import (traditional)
取り込む "utils"

// Namespaced import
取り込む "math_lib" として 数学
```

### Using the Namespace

```
// math_lib.jp
関数 足す(a, b):
    戻す a + b
終わり

変数 バージョン = "1.0"
```

```
// main.jp
取り込む "math_lib" として 数学

表示(数学["足す"](3, 4))    // 7
表示(数学["バージョン"])     // "1.0"
```

---

## Package Management

### Manifest File (hajimu.json)

```json
{
    "名前": "my-project",
    "バージョン": "1.0.0",
    "説明": "Project description",
    "作者": "Author Name",
    "メイン": "main.jp",
    "依存": {
        "my-library": "https://github.com/user/my-library.git"
    }
}
```

### CLI Commands

```bash
# Initialize (generates hajimu.json)
hajimu パッケージ 初期化

# Add a package from GitHub
hajimu パッケージ 追加 user/repo
hajimu パッケージ 追加 https://github.com/user/repo.git

# Add a local package
hajimu パッケージ 追加 /path/to/local/repo

# Remove a package
hajimu パッケージ 削除 package-name

# List installed packages
hajimu パッケージ 一覧

# Install all dependencies from hajimu.json
hajimu パッケージ インストール
```

English aliases:

```bash
hajimu pkg init
hajimu pkg add user/repo
hajimu pkg remove package-name
hajimu pkg list
hajimu pkg install
```

### Directory Layout

```
project/
├── hajimu.json
├── hajimu_packages/
│   ├── my-library/
│   │   ├── hajimu.json
│   │   └── main.jp
│   └── another-lib/
│       └── main.jp
└── main.jp
```

Global packages are stored in `~/.hajimu/packages/`.

### Using a Package

```
// Direct import
取り込む "my-library"

// Namespaced import
取り込む "my-library" として ライブラリ
表示(ライブラリ["someFunc"](arg))
```

### Package Resolution Order

1. Local: `./hajimu_packages/<name>/`
2. Global: `~/.hajimu/packages/<name>/`

Entry point search order within a package:

1. File specified by `"メイン"` in `hajimu.json`
2. `main.jp`
3. `<package-name>.jp`

---

## C Extension Plugins

Native plugins written in C (or any language that produces a shared library) can be loaded by Hajimu.
All plugins use the unified `.hjp` extension (shared library internally).

### Using a Plugin

```
// Namespaced import (recommended)
取り込む "math_plugin" として 数学P
表示(数学P["二乗"](5))   // 25

// Direct import
取り込む "math_plugin"
表示(二乗(5))            // 25

// Explicit .hjp
取り込む "math_plugin.hjp" として 数学P
```

### Plugin Search Order (extension-less import)

1. Relative to caller: `<call-dir>/name.hjp`
2. Current directory: `./name.hjp`
3. Local packages: `./hajimu_packages/name.hjp`, `./hajimu_packages/name/name.hjp`
4. Global plugins: `~/.hajimu/plugins/name.hjp`

### Plugin Metadata

```
取り込む "my_plugin" として P
表示(P["__名前__"])         // plugin name
表示(P["__バージョン__"])   // version
表示(P["__作者__"])         // author
表示(P["__説明__"])         // description
```

### Developing a Plugin

#### 1. Include the header

```c
#include "hajimu_plugin.h"
```

#### 2. Implement functions

```c
static Value fn_hello(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == VALUE_STRING) {
        printf("Hello, %s!\n", argv[0].string.data);
    }
    return hajimu_null();
}
```

Helper functions:

| Function | Description |
|---|---|
| `hajimu_null()` | Create NULL value |
| `hajimu_number(n)` | Create number value |
| `hajimu_bool(b)` | Create boolean value |
| `hajimu_string(s)` | Create string value (copied) |
| `hajimu_array()` | Create empty array |
| `hajimu_array_push(arr, elem)` | Append to array |

#### 3. Function table and init

```c
static HajimuPluginFunc functions[] = {
    {"greet", fn_hello, 1, 1},  // {name, fn, min_args, max_args}
};

HAJIMU_PLUGIN_EXPORT HajimuPluginInfo *hajimu_plugin_init(void) {
    static HajimuPluginInfo info = {
        .name           = "my_plugin",
        .version        = "1.0.0",
        .author         = "Author",
        .description    = "Example plugin",
        .functions      = functions,
        .function_count = sizeof(functions) / sizeof(functions[0]),
    };
    return &info;
}
```

#### 4. Compile

```bash
# macOS
gcc -shared -fPIC -I/path/to/jp/include -o my_plugin.hjp my_plugin.c

# Linux
gcc -shared -fPIC -I/path/to/jp/include -o my_plugin.hjp my_plugin.c -lm

# Windows (MinGW)
gcc -shared -I/path/to/jp/include -o my_plugin.hjp my_plugin.c

# Windows (MSVC)
cl /LD /Fe:my_plugin.hjp my_plugin.c
```

### Supported Languages

Any language that can produce a shared library works. Just rename the output to `.hjp`.

| Language | Example |
|---|---|
| C | `gcc -shared -fPIC -o plugin.hjp plugin.c` |
| C++ | `g++ -shared -fPIC -o plugin.hjp plugin.cpp` |
| Rust | `cargo build --release` (`cdylib`), rename to `.hjp` |
| Go | `go build -buildmode=c-shared -o plugin.hjp` |
| Zig | `zig build-lib -dynamic`, rename to `.hjp` |

**Requirement:** Export `hajimu_plugin_init` with C linkage (`extern "C"`) returning `HajimuPluginInfo *`.

### Runtime Callbacks (calling Hajimu from C)

```c
#include "hajimu_plugin.h"

HAJIMU_PLUGIN_EXPORT void hajimu_plugin_set_runtime(HajimuRuntime *rt) {
    __hajimu_runtime = rt;
}

static Value fn_example(int argc, Value *argv) {
    if (argc >= 1 && (argv[0].type == VALUE_FUNCTION || argv[0].type == VALUE_BUILTIN)) {
        Value arg = hajimu_string("Hello");
        return hajimu_call(&argv[0], 1, &arg);
    }
    return hajimu_null();
}
```

---

## List Comprehensions

Concisely generate a new array from an existing iterable.

### Syntax

```
[expression を variable から iterable]
```

### Examples

#### Basic transformation

```
変数 数字 = [1, 2, 3, 4, 5]
変数 倍 = [n * 2 を n から 数字]
表示(倍)  // [2, 4, 6, 8, 10]
```

#### With filter (`もし`)

```
変数 大きい = [n を n から 数字 もし n > 3]
表示(大きい)  // [4, 5]

変数 偶数 = [n * 10 を n から 数字 もし n % 2 == 0]
表示(偶数)  // [20, 40]
```

#### Complex expressions

```
変数 結果 = [n * n + 1 を n から [1, 2, 3]]
表示(結果)  // [2, 5, 10]

変数 dict = {"a": 1, "b": 2, "c": 3}
変数 キー = [k を k から dict]
表示(キー)  // ["a", "b", "c"]
```

**Note:** The iterable must be an array, string, or dictionary.

---

## Running a Program

```
// hello.jp
表示("Hello, World!")
```

```bash
$ nihongo hello.jp
Hello, World!
```

---

## Version

- Version: 1.3.1
- Author: Reo Shiozawa
