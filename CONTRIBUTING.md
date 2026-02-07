# はじむへの貢献ガイド

はじむプロジェクトへの貢献に興味を持っていただき、ありがとうございます！
このドキュメントでは、貢献の方法と開発ガイドラインについて説明します。

## 🤝 貢献の方法

### Issue の報告

バグや機能リクエストがある場合は、[Issue](https://github.com/ReoShiozawa/hajimu/issues)を作成してください。

- バグ報告には以下を含めてください：
  - 問題の説明
  - 再現手順
  - 期待される動作
  - 実際の動作
  - 環境情報（OS、コンパイラバージョンなど）

### プルリクエストの作成

1. **リポジトリをフォーク**
   ```bash
   # GitHubでフォークボタンをクリック後
   git clone https://github.com/YOUR_USERNAME/hajimu.git
   cd hajimu
   ```

2. **開発環境のセットアップ**
   ```bash
   # 依存関係の確認
   # macOS: Xcode Command Line Tools
   # Linux: gcc, make
   
   # ビルド
   make
   
   # テスト実行
   make test
   ```

3. **ブランチを作成**
   ```bash
   git checkout -b feature/your-feature-name
   # または
   git checkout -b fix/your-bug-fix
   ```

4. **変更を加える**
   - コードを書く
   - テストを追加
   - ドキュメントを更新

5. **テストを実行**
   ```bash
   make test
   # 全てのテストが合格することを確認
   ```

6. **コミット**
   ```bash
   git add .
   git commit -m "feat: 新機能の説明"
   ```

7. **プッシュして PR を作成**
   ```bash
   git push origin feature/your-feature-name
   # GitHubでプルリクエストを作成
   ```

## 📝 コーディング規約

### C コードスタイル

```c
// 関数名: snake_case
void parse_expression() {
    // インデント: 4スペース
    if (condition) {
        do_something();
    }
}

// 変数名: snake_case
int token_count = 0;

// 構造体名: PascalCase
typedef struct {
    TokenType type;
    char* value;
} Token;
```

### コメント

- 日本語または英語でOK
- 複雑なロジックには説明を追加
- 関数の目的を簡潔に記述

```c
// 式をパースする
// Returns: 式のASTノード
ASTNode* parse_expression() {
    // ...
}
```

### はじむコード(.jp ファイル)

```
# 関数名: 日本語（全角スペースでインデント）
関数 合計を計算(配列):
    変数 合計 = 0
    配列 を 要素 で繰り返す
        合計 = 合計 + 要素
    終わり
    戻す 合計
終わり
```

## 🧪 テストガイドライン

### 新機能のテスト

新機能を追加する場合は、必ずテストを追加してください：

1. `tests/` ディレクトリにテストファイルを作成
2. `test_all.jp` に追加
3. `make test` で動作確認

```
# tests/your_feature.jp
表示("テスト: 新機能")

変数 結果 = あなたの新機能()
もし 結果 == 期待値 なら
    表示("✓ 合格")
そうでなければ
    表示("✗ 失敗")
終わり
```

### テストのカバレッジ

- 正常系のテスト
- エラーケースのテスト
- エッジケースのテスト

## 📋 コミット規約

コミットメッセージは以下の形式で記述してください：

```
<type>: <subject>

<body>
```

### Type

- `feat`: 新機能
- `fix`: バグ修正
- `docs`: ドキュメントのみの変更
- `style`: コードの意味に影響しない変更（空白、フォーマットなど）
- `refactor`: バグ修正や機能追加を含まないコードの変更
- `perf`: パフォーマンス改善
- `test`: テストの追加や修正
- `chore`: ビルドプロセスやツールの変更

### 例

```
feat: パイプ演算子の実装

|> 演算子を追加し、関数チェーンを可能にしました。

例:
変数 結果 = 5 |> 二倍 |> 三倍
```

## 🔍 プルリクエストのレビュー基準

以下の点を確認してください：

- [ ] コードが意図通りに動作する
- [ ] 既存のテストが全て合格する
- [ ] 新しいテストが追加されている
- [ ] ドキュメントが更新されている
- [ ] コーディング規約に従っている
- [ ] コミットメッセージが規約に従っている
- [ ] メモリリークがない（必要に応じて valgrind で確認）

## 💡 開発のヒント

### デバッグ

```bash
# デバッグビルド
make debug

# AddressSanitizer 付きでビルド
make asan

# Valgrind でメモリリークチェック
valgrind --leak-check=full ./nihongo examples/hello.jp
```

### 新しいキーワードの追加

1. `lexer.c` にトークンタイプを追加
2. `parser.c` にパース処理を追加
3. `evaluator.c` に評価処理を追加
4. テストを追加
5. ドキュメント（REFERENCE.md）を更新

### 新しい組み込み関数の追加

1. `evaluator.c` の `register_builtins()` に登録
2. 関数の実装を追加
3. テストを追加
4. リファレンスに追加

## 📚 参考資料

- [docs/REFERENCE.md](docs/REFERENCE.md) - 言語仕様
- [docs/TUTORIAL.md](docs/TUTORIAL.md) - チュートリアル
- [docs/implementation_guide.md](docs/implementation_guide.md) - 実装ガイド
- [docs/improved_design.md](docs/improved_design.md) - 設計ドキュメント

## 🌟 良い最初の Issue

初めての貢献を検討している方は、以下のラベルが付いた Issue をご覧ください：

- `good first issue` - 初心者向けの Issue
- `help wanted` - 助けを求めている Issue
- `documentation` - ドキュメントの改善

## ❓ 質問がある場合

- [Issue](https://github.com/ReoShiozawa/hajimu/issues) で質問を作成
- または [Discussion](https://github.com/ReoShiozawa/hajimu/discussions) で議論

## 👥 コミュニティ

はじむプロジェクトは、オープンで歓迎的なコミュニティを目指しています。
全ての貢献者が敬意を持って接するようお願いします。

---

**ありがとうございます！** あなたの貢献がはじむをより良いものにします。🚀
