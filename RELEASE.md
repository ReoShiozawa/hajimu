# リリース手順

このドキュメントでは、はじむをGitHubで公開し、Homebrewでインストール可能にする手順を説明します。

## ✅ 完了済み

以下の準備は既に完了しています：

- [x] README.md を公開用に更新（バッジ、インストール手順、機能一覧）
- [x] LICENSE ファイル（MIT License）を追加
- [x] CONTRIBUTING.md を作成
- [x] GitHub Issue/PR テンプレートを作成
- [x] Homebrew Formula（Formula/hajimu.rb）を作成
- [x] すべてのドキュメントで「はじむ」に統一
- [x] GitHub URL を ReoShiozawa/hajimu に更新
- [x] CHANGELOG.md に v1.0.0 リリース情報を追加
- [x] すべての変更をコミット

## 🚀 次のステップ

### 1. GitHubリポジトリの作成

1. **新しいリポジトリを作成**
   ```bash
   # GitHubで新しいリポジトリを作成
   # 名前: hajimu
   # 説明: 完全日本語プログラミング言語「はじむ」
   # 公開: Public
   # README, LICENSE, .gitignore: 追加しない（既存のものを使用）
   ```

2. **リモートを設定してプッシュ**
   ```bash
   cd /Users/kinoko/Documents/c/jp
   
   # 既存のリモートを確認
   git remote -v
   
   # 新しいリモートを追加（または変更）
   git remote add origin https://github.com/ReoShiozawa/hajimu.git
   # または既存のリモートを変更する場合:
   # git remote set-url origin https://github.com/ReoShiozawa/hajimu.git
   
   # プッシュ
   git push -u origin main
   ```

3. **ドキュメントリポジトリも作成**
   ```bash
   # GitHubで新しいリポジトリを作成
   # 名前: hajimu-document
   # 説明: はじむプログラミング言語の公式ドキュメント
   # 公開: Public
   
   cd /Users/kinoko/Documents/c/jp-document
   git remote add origin https://github.com/ReoShiozawa/hajimu-document.git
   git push -u origin main
   ```

### 2. GitHub Pagesの設定

1. hajimu-document リポジトリの Settings → Pages に移動
2. Source を「Deploy from a branch」に設定
3. Branch を「main」、フォルダを「/ (root)」に設定
4. Save をクリック
5. 数分後、https://reoshiozawa.github.io/hajimu-document/ でアクセス可能になります

### 3. GitHubリリースの作成

1. **タグを作成**
   ```bash
   cd /Users/kinoko/Documents/c/jp
   
   # タグを作成（v1.0.0）
   git tag -a v1.0.0 -m "はじむ v1.0.0 - 初回公開リリース"
   
   # タグをプッシュ
   git push origin v1.0.0
   ```

2. **GitHubでリリースを作成**
   - https://github.com/ReoShiozawa/hajimu/releases/new にアクセス
   - Tag: `v1.0.0`
   - Title: `はじむ v1.0.0 - 初回公開リリース`
   - Description:
     ```markdown
     # はじむ v1.0.0 🎉
     
     完全日本語プログラミング言語「**はじむ**」の初回公開リリースです！
     
     ## ✨ 主な機能
     
     - 🇯🇵 完全日本語構文
     - 📖 充実したドキュメント
     - ⚡ C言語実装による高速実行
     - 🔧 156個のテストをすべて合格
     - 🎓 初心者にも優しい設計
     - 🚀 クラス、非同期、HTTP対応
     
     ## 📦 インストール
     
     ### Homebrew（推奨）
     ```bash
     brew tap ReoShiozawa/hajimu
     brew install hajimu
     ```
     
     ### ソースからビルド
     ```bash
     git clone https://github.com/ReoShiozawa/hajimu.git
     cd hajimu
     make
     sudo make install
     ```
     
     ## 🚀 クイックスタート
     
     ```jp
     表示("はじむへようこそ！")
     
     変数 数値 = 10
     もし 数値 > 5 なら
         表示("大きい数です")
     終わり
     ```
     
     ## 📚 ドキュメント
     
     - [公式サイト](https://reoshiozawa.github.io/hajimu-document/)
     - [チュートリアル](https://github.com/ReoShiozawa/hajimu/blob/main/docs/TUTORIAL.md)
     - [リファレンス](https://github.com/ReoShiozawa/hajimu/blob/main/docs/REFERENCE.md)
     
     ## 🤝 コントリビューション
     
     [CONTRIBUTING.md](https://github.com/ReoShiozawa/hajimu/blob/main/CONTRIBUTING.md)をご覧ください。
     
     ---
     
     詳細は[CHANGELOG](https://github.com/ReoShiozawa/hajimu/blob/main/CHANGELOG.md)をご覧ください。
     ```
   - 「Publish release」をクリック

3. **SHA256を取得**
   ```bash
   # リリースのtarballをダウンロード
   curl -L https://github.com/ReoShiozawa/hajimu/archive/refs/tags/v1.0.0.tar.gz -o hajimu-1.0.0.tar.gz
   
   # SHA256を計算
   shasum -a 256 hajimu-1.0.0.tar.gz
   ```

### 4. Homebrew Formulaの更新

1. **SHA256を Formula/hajimu.rb に追加**
   ```bash
   cd /Users/kinoko/Documents/c/jp
   
   # 取得したSHA256を Formula/hajimu.rb の sha256 行に追加
   # 例: sha256 "abc123..."
   ```

2. **Formula をコミット**
   ```bash
   git add Formula/hajimu.rb
   git commit -m "feat: add SHA256 to Homebrew formula"
   git push origin main
   ```

### 5. Homebrew Tapリポジトリの作成（オプション - 推奨）

専用のHomebrew Tapリポジトリを作成すると、管理が容易になります：

1. **新しいリポジトリを作成**
   - 名前: `homebrew-hajimu`
   - 説明: Homebrew tap for はじむ
   - 公開: Public

2. **Formula を移動**
   ```bash
   # 新しいリポジトリを作成
   mkdir homebrew-hajimu
   cd homebrew-hajimu
   git init
   
   # Formula をコピー
   mkdir Formula
   cp /Users/kinoko/Documents/c/jp/Formula/hajimu.rb Formula/
   
   # README を作成
   cat > README.md << 'EOF'
   # Homebrew Tap for はじむ
   
   はじむプログラミング言語のHomebrew Tap
   
   ## インストール
   
   ```bash
   brew tap ReoShiozawa/hajimu
   brew install hajimu
   ```
   
   ## アップデート
   
   ```bash
   brew update
   brew upgrade hajimu
   ```
   EOF
   
   # コミットしてプッシュ
   git add .
   git commit -m "feat: initial Homebrew tap for はじむ"
   git remote add origin https://github.com/ReoShiozawa/homebrew-hajimu.git
   git push -u origin main
   ```

### 6. Homebrewでインストールテスト

```bash
# Tap を追加
brew tap ReoShiozawa/hajimu

# インストール
brew install hajimu

# テスト
hajimu --version
hajimu examples/hello.jp
```

### 7. リポジトリの設定

1. **GitHub リポジトリの Description を設定**
   - hajimu: "完全日本語プログラミング言語「はじむ」- 日本語で書く、日本語で考える"
   - hajimu-document: "はじむプログラミング言語の公式ドキュメント"

2. **Topics を追加**
   - hajimu: `programming-language`, `japanese`, `interpreter`, `c`, `education`, `beginner-friendly`
   - hajimu-document: `documentation`, `japanese`, `github-pages`

3. **README のバッジを有効化**
   - License バッジ: 自動的に機能します
   - Release バッジ: v1.0.0 リリース後に機能します
   - Stars バッジ: 自動的に機能します

## 📣 公開のお知らせ

リリース後、以下のコミュニティで公開をお知らせすることを検討してください：

- Twitter/X
- Reddit (r/ProgrammingLanguages, r/programming_ja など)
- Hacker News
- Qiita（技術記事）
- Zenn（技術記事）
- 日本語プログラミング言語コミュニティ

## 🎉 完了！

おめでとうございます！はじむが一般公開されました。

次のステップ：
- ユーザーからのフィードバックを収集
- Issue や Pull Request に対応
- ドキュメントを継続的に改善
- 新機能の開発（ROADMAP.md を参照）

---

質問がある場合は、[GitHub Discussions](https://github.com/ReoShiozawa/hajimu/discussions) で質問してください。
