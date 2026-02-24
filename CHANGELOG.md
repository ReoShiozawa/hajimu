# チェンジログ

すべての注目すべき変更はこのプロジェクトに記録されます。

## [v1.2.10] - 2026-02-24

### 🐛 Windows CRLF 改行対応 — 字句解析器の空行検出を修正

#### 修正
- **lexer.c**: インデント処理中の空行判定に `\r`（CR）を追加
  - 従来: `\r\n`（CRLF）の空行を空行として認識できず、DEDENTを誤発生させていた
  - 修正後: `\r` を読み飛ばしてから `\n` を判定するため、CRLF ファイルが正常解析される
  - 影響: Windows 環境で git `autocrlf=true` 等により CRLF になったファイルが正しく動作するように

---

## [v1.2.9] - 2026-02-24

### 🔧 コンパイラ警告ゼロ化 (macOS + Windows MinGW) + インストーラー修正

#### 修正
- **async.c**: Winsock `send`/`recv` の `unsigned char*` → `(const char *)` / `(char *)` キャスト — 符号拡張による文字化けを根本修正
- **async.h**: `AsyncTask.error_message` を 256 → 1024 バイトに拡張
- **parser.c**: 未使用の `check_keyword` 関数を削除
- **plugin.c**: `try_path` バッファを 1024 → 2048 バイトに拡張
- **evaluator.h**: `error_message` を 512 → 1024 バイトに拡張
- **evaluator.c**: `try_path` を 1024 → 2048 バイトに拡張
- **package.c**: `git_dir`, `manifest_path`, `makefile_path` を `PACKAGE_MAX_PATH + 32` に拡張
- **package.c**: `search_paths` を `PACKAGE_MAX_PATH + PACKAGE_MAX_NAME` に拡張
- **win/installer.nsi**: PowerShell 変数 `$p`/`$newPath`/`$_` を NSIS エスケープ `$$p`/`$$newPath`/`$$_` に修正 — インストール/アンインストール時の PATH 操作が正常動作するように修正
- **win/installer.nsi**: 静的リンクで生成されない DLL (`libcurl.dll`, `libgcc_s_seh-1.dll`, `zlib1.dll`, `libssl-3-x64.dll`, `libcrypto-3-x64.dll`) を installer から削除 — NSIS ビルド警告ゼロに

## [v1.2.8] - 2026-02-24

### 🪟 Windows 互換性強化 (package.c — ビルドコマンド引用符修正)

#### 修正
- **package.c**: `cmd /C "cd /D "スペースを含むパス" && make"` で発生していたCMD引用符誤解析を修正
  - **旧**: `cmd /C "cd /D "パス" && コマンド"` — 内側の `"` をCMDが誤解析してビルド失敗
  - **新**: `_chdir(pkg_dir)` でディレクトリ変更 → `popen(user_cmd)` を直接実行（`cd` 不要）
  - `pclose` 後に `_chdir(orig_dir)` で確実に元ディレクトリへ復帰
  - `build_cmd[0]` チェックで `_chdir` 失敗時に `popen` が実行されないよう保護

## [v1.2.7] - 2026-02-11

### 🪟 Windows 互換性強化 (plugin / package / hajimu_discord)

#### 修正
- **plugin.c**: `LoadLibraryA` → `LoadLibraryW` — 日本語などのUnicodeパスで .hjp のロードが失敗する問題を修正（`MultiByteToWideChar(CP_UTF8)` で変換）
- **plugin.c**: `GetLastError()` を`LoadLibrary`直後に保存 — 後続 Win32 呼び出しでエラーコードが失われる問題を修正
- **plugin.c**: `ERROR_BAD_EXE_FORMAT(193)` / `ERROR_EXE_MACHINE_TYPE_MISMATCH(216)` 検出時に「このプラグインは Windows 用にビルドされていません」と明確なヒントを表示
- **package.c**: Windows で `make` が見つからない場合 `mingw32-make` へ自動フォールバック
- **package.c**: Windows でのビルドコマンドを `cmd /C "cd /D \"<dir>\" && <cmd>"` でラップ（日本語ディレクトリ名対応）

## [v1.2.0] - 2026-02-10

### 🔧 プラグインランタイムコールバック & 言語改善

#### 追加
- **プラグインランタイムコールバック**: プラグインからはじむ関数を呼び出せる仕組みを実装
  - `HajimuRuntime` コールバック構造体（`hajimu_call()` ヘルパーマクロ）
  - `hajimu_plugin_set_runtime()` によるランタイム自動注入
  - `VALUE_FUNCTION` / `VALUE_BUILTIN` / ラムダの全関数型に対応
- **`返す` キーワード**: `戻す` の別名として追加（より自然な日本語表現）
- **文字列補間の改善**: JSON文字列 `{"キー":"値"}` を含む文字列が正しくリテラル処理されるように修正
  - 補間式が不完全な場合（全テキスト未消費）はリテラル `{...}` として出力

### 🌐 hajimu_web v3.0

#### 追加
- **コールバック関数ハンドラ**: はじむ関数をルートハンドラとして登録可能（Express スタイル）
  - `ウェブ.GET("/パス", 関数)` — 関数の戻り値がレスポンスボディに
  - 配列 `[ステータス, Content-Type, ボディ]` を返すと全制御可能
  - 数値を返すとステータスコードとして使用
- **Cookie 設定 / 削除**: `Cookie設定(名前, 値, オプション?)` / `Cookie削除(名前)`
- **レスポンス制御**: `ヘッダー設定`、`ステータス設定`、`コンテンツタイプ設定`
- **JSON ユーティリティ**: `JSON解析(文字列)` / `JSON生成(値)` — プラグイン内蔵パーサー/ジェネレーター
- **レートリミッタ**: `ミドルウェア("rate_limit")` / `レート制限設定(最大数, ウィンドウ秒)`
  - IP単位のレート制限、`X-RateLimit-Limit` / `X-RateLimit-Remaining` ヘッダー
  - 429 Too Many Requests レスポンス、`Retry-After` ヘッダー

### 🧩 VS Code 拡張

#### 追加
- `返す` キーワードのシンタックスハイライト対応
- hajimu_web プラグインの全42関数をIntelliSense（補完・ホバー）に追加

---

## [v1.1.0] - 2026-02-08

### 🔌 統一プラグイン形式 `.hjp`

クロスプラットフォーム対応の統一拡張子 `.hjp`（Hajimu Plugin）を導入。

#### 変更
- プラグイン拡張子を OS 固有形式（.so/.dylib/.dll）から統一 `.hjp` 形式に移行
- 拡張子なしインポート対応（`取り込む "名前"` で自動検索）
- 4段階プラグイン検索: 呼出元相対 → CWD → `hajimu_packages/` → `~/.hajimu/plugins/`
- Windows 対応のプラットフォーム抽象化レイヤー（LoadLibrary/GetProcAddress）
- ドキュメント全面更新（REFERENCE.md、README.md、ROADMAP.md）

---

## [v1.0.0] - 2026-02-XX

### 🎉 公開リリース

**はじむ（Hajimu）** として正式に一般公開。

#### 変更
- 言語名を「日本語プログラミング言語」から「**はじむ**」に正式決定
- リポジトリを https://github.com/ReoShiozawa/hajimu に移行
- ドキュメントサイトを https://reoshiozawa.github.io/hajimu-document/ に移行

#### 追加
- **Homebrew サポート**: `brew install ReoShiozawa/hajimu/hajimu`
- **パッケージ管理**: 内蔵パッケージマネージャ（`hajimu パッケージ 初期化/追加/削除/一覧/インストール`）
  - `hajimu.json` マニフェストファイルによる依存管理
  - GitHub リポジトリ / ローカルリポジトリからのパッケージインストール
  - `hajimu_packages/`（ローカル）、`~/.hajimu/packages/`（グローバル）の2層パッケージ格納
  - 再帰的な依存パッケージの自動インストール
- **取り込みの強化**: パッケージ名解決、呼び出し元相対パス解決、重複インポート防止、循環参照検出
- **C拡張プラグイン**: 統一拡張子 `.hjp` によるクロスプラットフォーム（Windows/macOS/Linux）ネイティブプラグイン
  - `hajimu_plugin.h` プラグイン開発用公開ヘッダー
  - 拡張子なしインポート対応（`取り込む "名前"` で自動検索）
  - 4段階検索: 呼出元相対 → CWD → `hajimu_packages/` → `~/.hajimu/plugins/`
  - C/C++/Rust/Go/Zig 等ではじむ用ライブラリを開発可能
  - サンプルプラグイン（examples/plugins/）を同梱
- **VS Code 拡張**: はじむ言語サポート（シンタックスハイライト、スニペット、自動補完）
- **LICENSE**: MIT License を明記
- **CONTRIBUTING.md**: コントリビューションガイドラインを追加
- **GitHub テンプレート**: Issue/PR テンプレートを追加
- **Formula/hajimu.rb**: Homebrew Formula を追加

#### ドキュメント
- README.md を公開用に全面刷新
  - バッジ追加（License, Release, Stars）
  - インストール手順（Homebrew + ソースビルド）
  - 機能リスト、クイックスタート、言語比較表を追加
- すべてのドキュメントで「はじむ」の名称に統一
- GitHub URL を ReoShiozawa/hajimu に更新

## [v0.2.0] - 2026-02-06

### 追加

#### 新機能

1. **文字列補間** (`{式}`)
   - 文字列内に `{}` を使用して式を埋め込み可能
   - 例: `"{名前}は{年齢}歳"` → `"太郎は25歳"`
   - `evaluate_string_interpolation()` で実装

2. **デフォルト引数**
   - 関数のパラメータにデフォルト値を指定可能
   - 例: `関数 foo(甲, 乙=10):`
   - 呼び出し時に引数の省略が可能
   - ast.h の Parameter 構造体に `default_value` フィールド追加

3. **else-if チェーン** (`それ以外もし`)
   - 複数の条件分岐を効率的に記述可能
   - `TOKEN_ELSE_IF` キーワード追加
   - パーサーで `pipe_expr()` → `or_expr()` 方式に統一

4. **型チェック関数**
   - `数値か()`, `文字列か()`, `真偽か()`, `配列か()`, `辞書か()`, `関数か()`, `無か()`
   - すべての型判定が可能
   - `無` リテラル (`TOKEN_NULL_LITERAL`) も追加

5. **複数行文字列** (`"""..."""`)
   - `"""` で囲んだ文字列は改行を含む
   - `scan_multiline_string()` で実装
   - エスケープシーケンスにも対応

6. **範囲関数** (`範囲`)
   - `範囲(終了)` - 0から終了まで
   - `範囲(開始, 終了)` - 開始から終了まで
   - `範囲(開始, 終了, ステップ)` - ステップ指定可能
   - `builtin_range()` で実装

7. **ビット演算関数**
   - `ビット積(a, b)` - AND (`&`)
   - `ビット和(a, b)` - OR (`|`)
   - `ビット排他(a, b)` - XOR (`^`)
   - `ビット否定(a)` - NOT (`~`)
   - `左シフト(a, b)` - 左シフト (`<<`)
   - `右シフト(a, b)` - 右シフト (`>>`)
   - ビルトイン関数として実装（`long long` で処理）

8. **パイプ演算子** (`|>`)
   - `式 |> 関数` を `関数(式)` に変換
   - 関数合成に便利
   - `TOKEN_PIPE` キーワード追加
   - パーサーの `pipe_expr()` で実装

### 変更

- lexer.h: TOKEN 追加 (`TOKEN_ELSE_IF`, `TOKEN_NULL_LITERAL`, `TOKEN_PIPE`)
- parser.c: パイプ演算子の優先度最低として処理
- evaluator.c: ビルトイン関数群を大幅追加（合計8種類）
- ドキュメント更新: REFERENCE.md に新機能の説明を追加

### テスト

- `test_all.jp` で全機能の統合テスト実施
- 29個のテストケースがすべて成功

## [v0.1.0] - 前回のリリース

### 機能

- 基本的な文法 (変数、関数、制御構文)
- 組み込み関数 (表示、長さ、配列操作など)
- UTF-8 サポート
- AST 評価
- エラーハンドリング
