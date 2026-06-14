# はじむ

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![GitHub release](https://img.shields.io/github/v/release/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/releases)
[![GitHub stars](https://img.shields.io/github/stars/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/stargazers)

> English README: [README_en.md](README_en.md)

**日本語で書き、日本語で考え、英語圏のプログラミングにも橋をかける言語。**

はじむは、日本語を第一の構文として扱うプログラミング言語です。
単に変数名やコメントを日本語にするのではなく、`関数`、`もし`、`戻す`、`表示` などのキーワードを中心に、日本語でプログラムを組み立てられることを目指しています。

同時に、現在のはじむは `function`、`var`、`if`、`for`、`return`、`class`、`new`、`print`、`len`、`http_get`、`async_run` などの英語 alias も受け付けます。日本語で理解した概念を、英語圏の一般的なプログラミング表現へ少しずつ接続できるようにするためです。

```hajimu
関数 挨拶(名前):
    表示("こんにちは、" + 名前 + "さん")
終わり

挨拶("はじむ")
```

英語 alias でも書けます。

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

print("total = " + to_string(total))
```

## このプロジェクトが目指すもの

はじむは、次のような問いに向き合うための OSS です。

- 日本語を第一言語として、読み書きしやすいプログラミング言語を作れるか
- 初学者が「構文の英語」でつまずく前に、計算・条件・繰り返し・抽象化を学べるか
- 日本語構文と英語構文を同じ AST / 同じ実行系へ自然に接続できるか
- 親切なエラー表示によって、学習とデバッグの体験をどれだけ良くできるか
- 小さな C 実装の言語処理系として、共同開発しやすい形に育てられるか

まだ若い言語ですが、言語設計、教育、エディタ拡張、ランタイム実装、パッケージ配布の実験台として育てています。

## 特徴

- **日本語中心構文**: 日本語のキーワードと表現でコードを書けます
- **英語 alias**: `function` / `if` / `for` / `print` などでも書けます
- **複数ソース拡張子**: `.jp`、`.haj`、`.hajimu` をサポート
- **C 実装**: 小さく読める AST インタープリタ
- **UTF-8 対応**: 日本語文字列の長さ・添字処理を意識した実装
- **親切な診断**: 原因、直し方、例、「もしかして」候補を表示
- **実用的な組み込み機能**: JSON、HTTP、ファイル、正規表現、Base64、パス操作など
- **非同期/並列補助**: async task、await、parallel map/run、channel、mutex、semaphore、atomic
- **オブジェクト指向**: class、constructor、self、継承、静的メソッド
- **HJPB `.hjp`**: ソースをバイトコード形式へまとめて直接実行可能
- **パッケージ管理**: `hajimu pkg` / `hajimu パッケージ` コマンド
- **ネイティブプラグイン**: C ABI ベースのプラグイン API

## インストール

### macOS / Linux

Homebrew:

```bash
brew tap ReoShiozawa/hajimu
brew install hajimu
```

### Windows

[GitHub Releases](https://github.com/ReoShiozawa/hajimu/releases) から `hajimu_setup-1.5.0.exe` をダウンロードして実行してください。インストーラーは `hajimu.exe` と必要な DLL を配置し、PATH に追加します。

ポータブルに使う場合は、`hajimu-windows-x64.exe`、`libcurl-x64.dll`、`libwinpthread-1.dll` を同じフォルダーに置いてください。

### ソースからビルド

```bash
git clone https://github.com/ReoShiozawa/hajimu.git
cd hajimu
make
./nihongo examples/english_basic.jp
```

インストール済みコマンドは通常 `hajimu`、ソースツリー内の実行ファイルは `nihongo` です。

## クイックスタート

`hello.jp` を作成します。

```hajimu
表示("こんにちは、世界！")
```

実行:

```bash
hajimu hello.jp
# または
./nihongo hello.jp
```

英語 alias を使う場合は `hello.haj` のように書けます。

```hajimu
function greet(name):
    print("Hello, " + name)
end

greet("Hajimu")
```

```bash
hajimu hello.haj
```

## コード例

### クラス

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

### 日本語と英語 alias の混在

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

### 非同期/並列

```hajimu
function double(x):
    return x * 2
end

var mapped = parallel_map([1, 2, 3], double)
print(to_string(mapped))  // [2, 4, 6]

var task = async_run(double, 21)
print(to_string(await_task(task, 2)))  // 42
```

より多くの例は [examples/](examples/) にあります。

## ドキュメント

はじめに読むもの:

- [チュートリアル](docs/TUTORIAL.md)
- [リファレンス](docs/REFERENCE.md)
- [英語 README](README_en.md)
- [English Tutorial](docs/TUTORIAL_en.md)
- [English Reference](docs/REFERENCE_en.md)
- [英語構文対応ロードマップ](docs/ENGLISH_SYNTAX_ROADMAP.md)
- [英語 alias 命名・衝突方針](docs/ENGLISH_ALIAS_POLICY.md)
- [高速標準処理・研究計算基盤 設計書](docs/PERFORMANCE_AND_COMPUTE_DESIGN.md)
- [プラグイン開発ガイド](docs/外部パッケージ開発ガイド.md)
- [Plugin Development Guide](docs/PLUGIN_DEVELOPMENT.md)
- [ロードマップ](docs/ROADMAP.md)
- [CHANGELOG](CHANGELOG.md)

公式ドキュメントサイト:

- https://reoshiozawa.github.io/hajimu-document/

## 言語機能の概要

| 分野 | 状態 |
|---|---|
| 日本語構文 | 第一構文 |
| 英語 alias | 実用的なサブセットを実装済み |
| 実行方式 | C 実装の AST インタープリタ |
| 型システム | 動的型 |
| 文字列 | UTF-8 対応 |
| 研究計算 | 数値ベクトル、数値行列、dtype/astype、統計、評価指標、欠損処理、訓練/テスト分割、線形代数、線形/ロジスティック回帰、k-means、任意 BLAS ビルドの初期 API |
| オブジェクト指向 | class / constructor / self / 継承 |
| 関数型要素 | ラムダ、高階関数、リスト内包表記 |
| 制御構文 | if / while / for / foreach / switch / match |
| 例外 | try / catch / finally / throw |
| 非同期 | task / await / channel / mutex / semaphore / atomic |
| データ/IO | JSON / HTTP / ファイル / 正規表現 / Base64 / パス |
| 配布 | `.hjp` バイトコード、Windows インストーラー |
| プラグイン | C ABI ベースのネイティブプラグイン |

## ビルドとテスト

必要なもの:

- macOS 10.13+、Linux、Windows 10+
- GCC 9+ または Clang 10+
- `make`
- libcurl
- Windows 配布物を作る場合は MinGW-w64 と NSIS

よく使うコマンド:

```bash
make                    # ./nihongo をビルド
make release            # 最適化ビルド
make windows            # win/dist/hajimu.exe を生成
make windows-installer  # win/dist/hajimu_setup.exe を生成
make wasm               # jp-edu 連携用 WebAssembly を生成
./nihongo --profile tests/numeric_vector.jp  # 読込・パース・実行時間を表示
./nihongo --profile-ast tests/numeric_vector.jp  # ASTノード単位の評価時間を表示
```

リリース時の主な検証:

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

`tests/webhook_test.jp` はサーバーを起動して外部/手動リクエストを待つため、自動 smoke test では除外します。

## プロジェクト構成

```text
src/
├── lexer.c / lexer.h          字句解析とキーワード alias
├── parser.c / parser.h        再帰下降パーサ
├── ast.c / ast.h              AST
├── evaluator.c / evaluator.h  評価器と組み込み関数
├── value.c / value.h          動的値モデル
├── environment.c / .h         スコープとクロージャ
├── gc.c / gc.h                Environment の循環参照回収
├── async.c / async.h          非同期、channel、lock
├── http.c / http.h            JSON、HTTP、簡易サーバー補助
├── package.c / package.h      パッケージ管理
├── plugin.c / plugin.h        ネイティブプラグイン
└── bytecode.c / bytecode.h    HJPB .hjp 形式
```

日本語構文と英語 alias は、最終的に同じ AST と同じ評価器へ接続されます。英語対応は別モードではなく、はじむ本体の文法表現を広げるものです。

## 現在の成熟度

はじむは OSS として公開され、実験・学習・小規模スクリプトに使える状態です。ただし、まだ production stable な汎用言語ではありません。

向いている用途:

- 日本語中心のプログラミング教育
- 言語設計の実験
- エディタ拡張や診断 UI の実験
- 小さなスクリプトやデモ
- C 実装ランタイムの共同開発
- パッケージ/プラグイン配布の実験

今後さらに育てたい領域:

- 日本語構文と英語 alias の一貫性
- パッケージエコシステム
- Windows/macOS/Linux のリリース自動化
- 長時間動く非同期処理の安定性
- 形式的な言語仕様

## コントリビューション

貢献を歓迎します。特に助かるもの:

- 再現しやすいバグ報告
- エラー表示の改善
- 日本語/英語ドキュメントの改善
- 1つの概念を分かりやすく教えるサンプル
- パーサ・評価器・非同期処理のエッジケーステスト
- Windows 配布物の動作確認
- VS Code 拡張や教材との連携アイデア

詳しくは [CONTRIBUTING.md](CONTRIBUTING.md) をご覧ください。
セキュリティに関する報告は [SECURITY.md](SECURITY.md) を参照してください。

## リンク

- Repository: https://github.com/ReoShiozawa/hajimu
- Releases: https://github.com/ReoShiozawa/hajimu/releases
- Issues: https://github.com/ReoShiozawa/hajimu/issues
- Documentation: https://reoshiozawa.github.io/hajimu-document/

## ライセンス

MIT License で公開しています。詳しくは [LICENSE](LICENSE) を参照してください。

## 作者

[Reo Shiozawa](https://github.com/ReoShiozawa)
