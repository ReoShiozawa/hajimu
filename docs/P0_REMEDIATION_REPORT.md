# P0対応報告

更新日: 2026-10-24

## 対応範囲

この文書は、はじむ本体と主要エコシステムを公開・共同開発できる状態へ近づけるために実施したP0対応を記録します。

## 1. 認証情報と秘密情報

- GitリモートURLに埋め込まれていたGitHub認証情報を失効し、認証情報を含まないURLへ変更
- 主要GitHubリポジトリへgitleaksのCIを追加
- GitLab上のHajimu BridgeへSecret Detectionを追加
- 各Gitリポジトリへgitleaks用pre-commit設定と軽量なローカルフックを追加
- 全履歴を`--redact`付きgitleaksで検査し、追跡済み履歴からの漏えいがないことを確認

秘密情報は履歴へ入れないことが前提です。漏えいを疑う値は、履歴から消すだけでなく発行元で必ず失効してください。

## 2. パッケージ管理

パッケージ管理をマニフェスト仕様v2へ更新しました。

- SemVer制約、公式レジストリ、`hajimu.lock`、Gitコミット固定
- SHA-256付き配布物だけを自動取得
- 外部ビルドスクリプトを既定で拒否し、`--allow-scripts`で明示許可
- `search`、`validate`、`audit`、ロールバック付き`update`
- Git取得キャッシュ
- HTTPS限定、TLS検証、HTTPS以外へのリダイレクト拒否
- 一時ファイルからの原子的なマニフェスト・lockfile・ダウンロード更新
- 不正なパッケージ名、パストラバーサル、シンボリックリンク再帰削除、改ざんlockfileを拒否

詳細は[パッケージ管理v2](PACKAGE_MANAGEMENT_V2.md)を参照してください。

## 3. マニフェストとプラグインABI

- 公式パッケージの`hajimu.json`を仕様v2へ統一
- 公式レジストリへ12パッケージを登録
- マニフェスト、既存Gitタグ、実ロード時のプラグイン版を照合
- `engine_core`と`jp-web`に残っていた旧文字列ABI参照を現行ABIへ修正
- GUI v2のOS別成果物と汎用エントリを同じビルドから生成
- Discordの既定`make`が依存確認だけで終了していた問題を修正

## 4. CIと配布前検査

- 本体はmacOS/Linuxテスト、パッケージ管理試験、Windows成果物、チェックサム生成を自動化
- 拡張機能、公式サイト、Hajimu Bridge、Kaname、ドキュメント、サンプル、Discord、エンジン群へ対象に応じたCIを追加
- 全GitHub Actionsを`actionlint`、GitLab CIを`glab ci lint`で検証
- npm系プロジェクトの本番依存に残っていた既知のhigh脆弱性を解消

## 5. 作業ツリーの扱い

既存の未コミット変更には利用者の作業が含まれるため、P0/P1に関係する差分だけを選別してcommit・公開します。未関連の変更は保持します。`scripts/check-ecosystem.sh`でリポジトリごとのブランチ、変更件数、秘密情報対策、マニフェストを一覧化します。

```sh
# 状態確認。未コミット変更は報告のみ
scripts/check-ecosystem.sh

# 履歴と作業ツリーもgitleaksで検査
scripts/check-ecosystem.sh --secrets

# リリース直前。未コミット変更も失敗にする
scripts/check-ecosystem.sh --strict-clean --secrets
```

共同開発用のcommitへ整理するときは、各リポジトリで差分の所有者と目的を確認してから個別に行います。

## 検証済み項目

- ネイティブ本体の全テスト
- パッケージ管理の正常系、更新、監査、ビルド許可、攻撃回帰試験
- Windows x86-64クロスビルド
- WebAssembly隔離ビルド
- 公式ネイティブプラグインのビルドおよび実ロード版照合
- Hajimu Bridge、公式サイト、VS Code拡張、Kaname、jp-celの対象別ビルド・テスト

## 境界

- Windows成果物はmacOS上のクロスビルド確認であり、Windows実機での起動確認は別途必要です。
- CI設定はローカルで構文検証済みですが、各ホスティングサービス上の次回実行結果までは保証しません。
- P1でLSP/DAP、ネイティブABI検証、準拠試験、SBOM、OIDC来歴証明、レジストリ署名を追加しました。OS署名証明書が必要な項目と公開運用の残項目は[開発基盤と配布](DEVELOPMENT_FOUNDATION.md)に記録します。
