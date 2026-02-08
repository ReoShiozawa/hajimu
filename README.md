# はじむ (Hajimu)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![GitHub release](https://img.shields.io/github/release/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/releases)
[![GitHub stars](https://img.shields.io/github/stars/ReoShiozawa/hajimu.svg)](https://github.com/ReoShiozawa/hajimu/stargazers)

**日本語で書く、日本語で考える**プログラミング言語

はじむは、日本語のキーワードと自然な文法を持つプログラミング言語です。教育からプロダクション開発まで、幅広い用途で使える実用的な言語を目指しています。

## ✨ 特徴

- 🇯🇵 **完全日本語**: すべてのキーワードが日本語
- 📖 **直感的な文法**: 自然な日本語の語順でコードが書ける
- ⚡ **高速**: C言語で実装された高速なインタープリタ
- 🔧 **実用的**: 143個の組み込み関数、HTTP通信、非同期処理対応
- 📦 **パッケージ管理**: 内蔵パッケージマネージャで外部パッケージに対応
- 🔌 **C拡張プラグイン**: 統一 `.hjp` 形式でクロスプラットフォーム対応、C/C++/Rust等で開発可能
- 🎓 **学びやすい**: プログラミング初心者でも理解しやすい設計
- 🚀 **モダン**: ラムダ式、リスト内包表記、async/awaitなど

## 📦 インストール

### Homebrew（macOS/Linux）

```bash
# Tap リポジトリを追加
brew tap ReoShiozawa/hajimu

# インストール
brew install hajimu
```

### ソースからビルド

```bash
# リポジトリをクローン
git clone https://github.com/ReoShiozawa/hajimu.git
cd hajimu

# ビルド
make

# インストール（オプション）
sudo make install
```

### 動作要件

- **OS**: macOS 10.13+, Ubuntu 18.04+, Windows (WSL2)
- **コンパイラ**: GCC 9.0+ または Clang 10.0+
- **メモリ**: 最小 256MB

## 🚀 クイックスタート

### Hello World

```hajimu
表示("こんにちは、世界！")
```

実行:
```bash
hajimu hello.jp
# または
./nihongo hello.jp
```

### より詳しい例

```hajimu
# 階乗を計算する関数
関数 階乗(n は 数値) は 数値:
    もし n <= 1 なら
        戻す 1
    それ以外
        戻す n * 階乗(n - 1)
    終わり
終わり

# リスト内包表記
変数 数列 = [1, 2, 3, 4, 5]
変数 二倍 = [n * 2 を n から 数列]
表示(二倍)  // → [2, 4, 6, 8, 10]

# クラス
クラス 人:
    初期化(名前, 年齢):
        自分.名前 = 名前
        自分.年齢 = 年齢
    終わり
    
    挨拶(自分):
        表示("こんにちは、" + 自分.名前 + "です")
    終わり
終わり

変数 太郎 = 新しい 人("太郎", 25)
太郎.挨拶()
```

## 📚 ドキュメント

- **[公式サイト](https://reoshiozawa.github.io/hajimu-document/)** - 完全なドキュメントとチュートリアル
- **[言語リファレンス](docs/REFERENCE.md)** - 文法と組み込み関数の詳細
- **[チュートリアル](docs/TUTORIAL.md)** - ステップバイステップガイド
- **[コード例](examples/)** - 実践的なサンプルコード
- **[ロードマップ](docs/ROADMAP.md)** - 開発計画

## 🎯 言語仕様

### キーワード

- **制御構造**: `もし`, `それ以外`, `なら`, `繰り返す`, `条件`, `の間`
- **関数**: `関数`, `戻す`, `終わり`
- **変数**: `変数`, `定数`
- **クラス**: `クラス`, `初期化`, `自分`, `新しい`
- **論理**: `真`, `偽`, `かつ`, `または`, `でない`
- **ループ制御**: `抜ける`, `続ける`
- **非同期**: `非同期`, `待つ`
- **例外**: `試す`, `捕捉`, `最後に`
- **モジュール**: `取り込む`, `として`

### データ型

- **数値** - 浮動小数点数（64bit）
- **真偽** - `真` / `偽`
- **文字列** - UTF-8文字列
- **配列** - 動的配列 `[1, 2, 3]`
- **辞書** - ハッシュマップ `{"key": "value"}`
- **関数** - 第一級オブジェクト
- **クラス** - オブジェクト指向

### 組み込み関数（143個）

- **I/O**: `表示()`, `入力()`, `読む()`, `書く()`
- **文字列**: `長さ()`, `分割()`, `結合()`, `置換()`, `検索()`
- **配列**: `追加()`, `削除()`, `並べ替え()`, `マップ()`, `フィルタ()`
- **数学**: `合計()`, `平均()`, `最大()`, `最小()`, `累乗()`
- **HTTP**: `HTTP取得()`, `HTTP投稿()`, `JSON解析()`
- **ファイル**: `ファイル読込()`, `ファイル書込()`, `ファイル存在()`
- **型**: `型()`, `整数化()`, `小数化()`, `文字列化()`

完全なリストは[リファレンス](https://github.com/ReoShiozawa/hajimu/blob/main/docs/REFERENCE.md)を参照してください。

## � パッケージ管理

はじむには外部パッケージを管理するパッケージマネージャが内蔵されています。

```bash
# プロジェクトの初期化
hajimu パッケージ 初期化

# パッケージの追加（GitHub から）
hajimu パッケージ 追加 user/repo

# インストール済みパッケージの一覧
hajimu パッケージ 一覧

# 依存パッケージの一括インストール
hajimu パッケージ インストール
```

インストールしたパッケージは `取り込む` でインポートできます：

```hajimu
取り込む "my-library" として ライブラリ
表示(ライブラリ["関数名"](引数))
```

## �💻 開発環境

### ソースからのビルド

```bash
# リポジトリのクローン
git clone https://github.com/ReoShiozawa/hajimu.git
cd hajimu

# コンパイル
make

# テスト実行（156個のテストが実行されます）
make test

# インストール
sudo make install

# クリーンアップ
make clean
```

### プロジェクト構成

```
hajimu/
├── src/                    # ソースコード
│   ├── main.c             # エントリポイント
│   ├── lexer.c/h          # 字句解析器
│   ├── parser.c/h         # 構文解析器
│   ├── ast.c/h            # 抽象構文木
│   ├── evaluator.c/h      # 評価器
│   ├── value.c/h          # 値型システム
│   ├── environment.c/h    # 環境（スコープ管理）
│   ├── async.c/h          # 非同期処理
│   ├── http.c/h           # HTTPクライアント
│   ├── package.c/h        # パッケージ管理
│   └── plugin.c/h         # C拡張プラグイン
├── include/               # プラグイン開発用ヘッダー
│   └── hajimu_plugin.h    # プラグインAPI
├── examples/              # サンプルプログラム
│   ├── hello.jp
│   ├── factorial.jp
│   ├── fibonacci.jp
│   ├── class_test.jp
│   └── async_test.jp
├── tests/                 # テストスイート
├── docs/                  # ドキュメント
│   ├── REFERENCE.md       # リファレンスマニュアル
│   ├── TUTORIAL.md        # チュートリアル
│   └── ROADMAP.md         # 開発ロードマップ
└── Makefile               # ビルド設定
```

## 🤝 コントリビューション

はじむへの貢献を歓迎します！

### 貢献方法

1. このリポジトリをフォーク
2. 機能ブランチを作成 (`git checkout -b feature/amazing-feature`)
3. 変更をコミット (`git commit -m 'feat: 素晴らしい機能を追加'`)
4. ブランチにプッシュ (`git push origin feature/amazing-feature`)
5. プルリクエストを作成

詳細は[CONTRIBUTING.md](CONTRIBUTING.md)をご覧ください。

### コミット規約

- `feat:` 新機能
- `fix:` バグ修正
- `docs:` ドキュメント
- `refactor:` リファクタリング
- `test:` テスト追加・修正

## 📈 プロジェクト統計

- **言語**: C (C99/C11)
- **文字エンコーディング**: UTF-8対応
- **テストカバレッジ**: 156テスト全て合格
- **組み込み関数**: 143個
- **パーサ方式**: 再帰下降（LL(1)）
- **実行方式**: AST直接評価

## 🔗 リンク

- **ドキュメント**: https://reoshiozawa.github.io/hajimu-document/
- **リポジトリ**: https://github.com/ReoShiozawa/hajimu
- **Issue トラッカー**: https://github.com/ReoShiozawa/hajimu/issues
- **チュートリアル**: [docs/TUTORIAL.md](docs/TUTORIAL.md)
- **リファレンス**: [docs/REFERENCE.md](docs/REFERENCE.md)

## 📝 ライセンス

このプロジェクトは[MIT License](LICENSE)の下で公開されています。

## 👤 作者

**Reo Shiozawa**

- GitHub: [@ReoShiozawa](https://github.com/ReoShiozawa)

## 📊 言語比較

| 機能 | はじむ | Python | Ruby | JavaScript |
|------|--------|--------|------|------------|
| 完全日本語構文 | ✅ | ❌ | ❌ | ❌ |
| 学習難易度 | 低 | 中 | 中 | 中 |
| 型システム | 動的 | 動的 | 動的 | 動的 |
| クラスベースOOP | ✅ | ✅ | ✅ | ✅ |
| 非同期処理 | ✅ | ✅ | ✅ | ✅ |
| HTTPクライアント | ✅ | ✅ | ✅ | ✅ |
| 実行速度 | 高速 | 中速 | 中速 | 高速 |

---

**はじむ**で、日本語で書く楽しさを体験してください！ ⭐
