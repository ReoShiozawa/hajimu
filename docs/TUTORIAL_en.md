# Hajimu Tutorial

## Introduction

Hajimu (はじむ) is a programming language designed to express code naturally using Japanese syntax.
Even beginners can write programs intuitively.

> **Note:** Hajimu's keywords are Japanese. Code examples in this tutorial use the actual Japanese keywords.
> Refer to the keyword glossary at the end of each section for translation help.

---

## Chapter 1: Your First Program

### Hello, World!

Let's write the simplest possible program.

```
// hello.jp
表示("こんにちは、世界！")
```

Run it:
```bash
$ nihongo hello.jp
こんにちは、世界！
```

`表示` (hyōji) is the built-in print function.

---

## Chapter 2: Variables and Arithmetic

### Using Variables

A variable is a named container that holds a value.

```
// variables.jp
変数 名前 = "Taro"
変数 年齢 = 25

表示("My name is " + 名前)
表示("Age: " + 文字列化(年齢))
```

| Keyword | Meaning |
|---|---|
| `変数` | variable (mutable) |
| `文字列化` | convert to string |
| `表示` | print / display |

### Arithmetic

```
// calculator.jp
変数 a = 10
変数 b = 3

表示("Add:      " + 文字列化(a + b))   // 13
表示("Subtract: " + 文字列化(a - b))   // 7
表示("Multiply: " + 文字列化(a * b))   // 30
表示("Divide:   " + 文字列化(a / b))   // 3.333...
表示("Modulo:   " + 文字列化(a % b))   // 1
```

### Constants

Use `定数` to define a value that cannot be changed.

```
定数 消費税率 = 0.1
変数 価格 = 1000
変数 税込価格 = 価格 * (1 + 消費税率)
表示("Total: " + 文字列化(税込価格))
```

| Keyword | Meaning |
|---|---|
| `定数` | constant (immutable) |

---

## Chapter 3: Conditionals

### If / Else

Use `もし〜なら` to branch on a condition.

```
// if.jp
変数 点数 = 75

もし 点数 >= 80 なら
    表示("Excellent!")
それ以外
    もし 点数 >= 60 なら
        表示("Pass")
    それ以外
        表示("Fail")
    終わり
終わり
```

| Keyword | Meaning |
|---|---|
| `もし … なら` | if … then |
| `それ以外` | else |
| `それ以外もし` | else if |
| `終わり` | end (block terminator) |

### Logical Operators

```
変数 年齢 = 20
変数 学生 = 真

もし 年齢 >= 18 かつ 学生 なら
    表示("Adult student")
終わり

もし 年齢 < 18 または 学生 でない なら
    表示("General ticket")
終わり
```

| Keyword | Meaning |
|---|---|
| `かつ` | and (&&) |
| `または` | or (\|\|) |
| `でない` | not (!) |
| `真` | true |
| `偽` | false |

---

## Chapter 4: Loops

### While Loop

Runs as long as the condition is true.

```
// countdown.jp
変数 カウント = 5

条件 カウント > 0 の間
    表示(カウント)
    カウント = カウント - 1
終わり

表示("Launch!")
```

Output:
```
5
4
3
2
1
Launch!
```

| Keyword | Meaning |
|---|---|
| `条件 … の間` | while … |

### Range Loop

Iterates over a numeric range.

```
// loop.jp
i を 1 から 5 繰り返す
    表示("Count: " + 文字列化(i))
終わり
```

| Syntax | Meaning |
|---|---|
| `i を A から B 繰り返す` | for i from A to B (inclusive) |

### Multiplication Table

```
// kuku.jp
i を 1 から 9 繰り返す
    j を 1 から 9 繰り返す
        変数 結果 = i * j
        表示(文字列化(i) + " x " + 文字列化(j) + " = " + 文字列化(結果))
    終わり
終わり
```

---

## Chapter 5: Arrays

Arrays hold multiple values in order.

```
// array.jp
変数 果物 = ["apple", "orange", "grape"]

表示("First: " + 果物[0])
表示("Count: " + 文字列化(長さ(果物)))

// Add an element
追加(果物, "banana")
表示("After add: " + 文字列化(果物))
```

| Function | Meaning |
|---|---|
| `長さ(arr)` | length of array / string |
| `追加(arr, val)` | append value to array |

### Looping over an Array

```
変数 数列 = [1, 2, 3, 4, 5]

変数 合計 = 0
i を 0 から 長さ(数列) - 1 繰り返す
    合計 = 合計 + 数列[i]
終わり

表示("Sum: " + 文字列化(合計))
```

---

## Chapter 6: Functions

Functions are reusable blocks of code.

```
// function.jp
関数 挨拶(名前):
    表示("Hello, " + 名前 + "!")
終わり

挨拶("Taro")
挨拶("Hanako")
```

| Keyword | Meaning |
|---|---|
| `関数` | function |
| `戻す` / `返す` | return |

### Functions with Return Values

```
関数 面積(幅, 高さ):
    戻す 幅 * 高さ
終わり

変数 結果 = 面積(5, 3)
表示("Area: " + 文字列化(結果))  // 15
```

### Recursive Functions

A function can call itself.

```
関数 階乗(n):
    もし n <= 1 なら
        戻す 1
    それ以外
        戻す n * 階乗(n - 1)
    終わり
終わり

表示("5! = " + 文字列化(階乗(5)))  // 120
```

---

## Chapter 7: Classes and Objects

Classes bundle data together with the operations on that data.

```
// class.jp
型 犬:
    初期化(名前, 年齢):
        自分.名前 = 名前
        自分.年齢 = 年齢
    終わり

    関数 吠える():
        表示(自分.名前 + " says: Woof!")
    終わり

    関数 情報():
        表示(自分.名前 + " is " + 文字列化(自分.年齢) + " years old.")
    終わり
終わり

変数 ポチ = 新規 犬("Pochi", 3)
ポチ.情報()
ポチ.吠える()
```

Output:
```
Pochi is 3 years old.
Pochi says: Woof!
```

| Keyword | Meaning |
|---|---|
| `型` | class / type |
| `初期化` | constructor (initializer) |
| `自分` | self / this |
| `新規` | new (creates an instance) |
| `継承` | extends (inheritance) |

### Inheritance

```
型 動物:
    初期化(名前):
        自分.名前 = 名前
    終わり

    関数 鳴く():
        表示(自分.名前 + " makes a sound.")
    終わり
終わり

型 猫 継承 動物:
    関数 にゃー():
        表示(自分.名前 + " says: Meow!")
    終わり
終わり

変数 タマ = 新規 猫("Tama")
タマ.鳴く()    // parent method
タマ.にゃー()  // child method
```

---

## Chapter 8: Exception Handling

Handle errors gracefully so the program doesn't crash.

```
// exception.jp
関数 割り算(a, b):
    もし b == 0 なら
        投げる "Cannot divide by zero"
    終わり
    戻す a / b
終わり

試行:
    変数 結果 = 割り算(10, 0)
    表示("Result: " + 文字列化(結果))
捕獲 エラー:
    表示("Error: " + エラー)
最終:
    表示("Done.")
終わり
```

Output:
```
Error: Cannot divide by zero
Done.
```

| Keyword | Meaning |
|---|---|
| `試行:` | try |
| `捕獲 エラー:` | catch (error) |
| `最終:` | finally |
| `投げる` | throw |

---

## Chapter 9: File I/O

Read and write files easily.

```
// file.jp

// Write to a file
書き込む("memo.txt", "This is a memo.\nHajimu supports Unicode.")

// Check if the file exists
もし ファイル存在("memo.txt") なら
    表示("File found.")
終わり

// Read the file
変数 内容 = 読み込む("memo.txt")
表示("Contents:")
表示(内容)
```

| Function | Meaning |
|---|---|
| `書き込む(path, text)` | write text to file |
| `読み込む(path)` | read text from file |
| `ファイル存在(path)` | check if file exists |
| `追記(path, text)` | append text to file |

---

## Chapter 10: Practical Programs

### Rock-Paper-Scissors

```
// janken.jp
関数 じゃんけん():
    変数 選択肢 = ["Rock", "Scissors", "Paper"]

    表示("Rock-Paper-Scissors!")
    表示("0: Rock  1: Scissors  2: Paper")

    変数 プレイヤー入力 = 入力("Your choice: ")
    変数 プレイヤー = 数値化(プレイヤー入力)

    変数 コンピュータ = 切り捨て(乱数() * 3)

    表示("You:      " + 選択肢[プレイヤー])
    表示("Computer: " + 選択肢[コンピュータ])

    もし プレイヤー == コンピュータ なら
        表示("Draw!")
    それ以外
        変数 勝ち = (プレイヤー == 0 かつ コンピュータ == 1) または
                   (プレイヤー == 1 かつ コンピュータ == 2) または
                   (プレイヤー == 2 かつ コンピュータ == 0)
        もし 勝ち なら
            表示("You win!")
        それ以外
            表示("You lose...")
        終わり
    終わり
終わり

じゃんけん()
```

### Simple Calculator

```
// dentaku.jp
関数 電卓():
    表示("=== Calculator ===")

    変数 数1 = 数値化(入力("First number: "))
    変数 演算子 = 入力("Operator (+, -, *, /): ")
    変数 数2 = 数値化(入力("Second number: "))

    変数 結果 = 0

    もし 演算子 == "+" なら
        結果 = 数1 + 数2
    それ以外
        もし 演算子 == "-" なら
            結果 = 数1 - 数2
        それ以外
            もし 演算子 == "*" なら
                結果 = 数1 * 数2
            それ以外
                もし 演算子 == "/" なら
                    もし 数2 == 0 なら
                        表示("Error: Division by zero")
                        戻す
                    終わり
                    結果 = 数1 / 数2
                それ以外
                    表示("Unknown operator")
                    戻す
                終わり
            終わり
        終わり
    終わり

    表示("Result: " + 文字列化(結果))
終わり

電卓()
```

---

## Modules and Packages

### Importing a Module

Use `取り込む` to load functions and variables from another file.

Create a utility file:

```
// utils.jp
関数 二乗(n):
    戻す n * n
終わり

関数 挨拶(名前):
    表示("Hello, " + 名前 + "!")
終わり
```

Then import it:

```
// main.jp
取り込む "utils.jp"

挨拶("Taro")        // → Hello, Taro!
表示(二乗(5))       // → 25
```

### Namespace Import

Use `として` to import a module under a namespace, avoiding name collisions.

```
取り込む "utils.jp" として ツール

ツール["挨拶"]("Hanako")   // → Hello, Hanako!
表示(ツール["二乗"](3))    // → 9
```

| Keyword | Meaning |
|---|---|
| `取り込む` | import |
| `として` | as (namespace alias) |

### Package Management

Hajimu has a built-in package manager.

#### Initialize a project

```bash
hajimu パッケージ 初期化     # Japanese
hajimu pkg init              # English alias
```

This creates `hajimu.json` (the manifest file).

#### Add a package from GitHub

```bash
hajimu パッケージ 追加 user/repo
hajimu pkg add user/repo
```

#### Use an installed package

```
取り込む "my-library"

取り込む "my-library" として ライブラリ
```

#### Other commands

```bash
hajimu パッケージ 一覧          # list packages
hajimu パッケージ 削除 pkg-name # remove a package
hajimu パッケージ インストール   # install all dependencies

# English aliases
hajimu pkg list
hajimu pkg remove pkg-name
hajimu pkg install
```

See the [Reference Manual](REFERENCE_en.md) "Package Management" section for details.

---

## Platform Detection

Hajimu provides built-in constants to detect the current OS and architecture at runtime.

```jp
表示(システム名)           // → "macOS" / "Linux" / "Windows"
表示(アーキテクチャ)       // → "arm64" / "x86-64"
表示(はじむバージョン)     // → "1.3.1"

// Detailed info via the システム dictionary
表示(システム["OS"])        // → "macOS"
表示(システム["区切り文字"]) // → "/" (Windows: "\\")
表示(システム["改行"])      // → "\n" (Windows: "\r\n")
```

| Constant | Meaning |
|---|---|
| `システム名` | OS name |
| `アーキテクチャ` | CPU architecture |
| `はじむバージョン` | Hajimu interpreter version |
| `システム` | dictionary with extended info |

Branch on platform:

```jp
もし システム名 == "Windows" なら
    表示("Running on Windows")
それ以外もし システム名 == "macOS" なら
    表示("Running on macOS")
それ以外
    表示("Running on Linux")
終わり
```

### Automatic .hjp Plugin Selection

When you write `取り込む "engine_render"`, Hajimu automatically picks the correct platform binary:

| OS | File selected |
|---|---|
| macOS (Apple Silicon) | `engine_render-macos-arm64.hjp` |
| macOS (Intel) | `engine_render-macos.hjp` |
| Linux (x86-64) | `engine_render-linux-x64.hjp` |
| Windows | `engine_render-windows-x64.hjp` |

See the [Plugin Development Guide](PLUGIN_DEVELOPMENT.md) for how to build cross-platform packages.

---

## Next Steps

Congratulations! You have learned the fundamentals of Hajimu.

To go further:
- Read the [Reference Manual](REFERENCE_en.md) for a complete language reference
- Try the sample programs in the `tests/` folder
- Run the debugger with `nihongo -d yourprogram.jp` to step through execution

Happy coding!
