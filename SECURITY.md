# Security Policy

## Supported Versions

Security fixes are handled for the latest released version of Hajimu.
At the time of writing, that is the latest tag published on GitHub Releases.

## Reporting A Vulnerability

Please do not open a public issue for vulnerabilities that could be exploited.

Instead, report the issue privately by contacting the maintainer:

- GitHub: [@ReoShiozawa](https://github.com/ReoShiozawa)

If GitHub private vulnerability reporting is enabled for this repository, please
use that channel first. Otherwise, send a concise report with:

- affected version or commit
- operating system
- steps to reproduce
- expected behavior
- actual behavior
- impact assessment, if known
- proof of concept, if safe to share privately

## Scope

Security-sensitive areas include:

- package installation and repository/download handling
- native plugin loading
- command execution during package builds
- HTTP/JSON parsing
- file path handling
- Windows installer and PATH modification behavior
- memory safety issues in the C runtime

## Response Expectations

This is a small open source project, so response time may vary. The maintainer
will try to:

1. acknowledge the report
2. reproduce and assess the issue
3. prepare a fix or mitigation
4. publish a release note once disclosure is safe

Thank you for helping keep Hajimu safer.

---

# セキュリティポリシー

悪用可能な脆弱性の可能性がある場合は、公開 Issue ではなく、できるだけ非公開の経路で報告してください。

特に次の領域はセキュリティ上重要です。

- パッケージのインストールとダウンロード処理
- ネイティブプラグインの読み込み
- パッケージビルド時のコマンド実行
- HTTP / JSON パーサ
- ファイルパス処理
- Windows インストーラー
- C ランタイムのメモリ安全性

報告には、影響するバージョン、OS、再現手順、期待される動作、実際の動作、影響範囲を含めてください。
