# はじむパッケージ管理 v2

パッケージ管理 v2 は、依存関係を再現可能かつ安全に導入するための仕様です。

## 主な変更

- `hajimu.json`に`schema_version: 2`を追加
- 依存ごとにSemVer制約、取得元、SHA-256を記録可能
- 実際のバージョン、取得元、Gitコミットを`hajimu.lock`へ固定
- 公式レジストリからパッケージ名を解決
- SHA-256を持たない配布バイナリの自動取得を禁止
- 外部ビルドスクリプトは`--allow-scripts`指定時だけ実行
- `validate`、`search`、`audit`コマンドを追加
- HTTPS限定、原子的更新、パストラバーサルとシンボリックリンク削除の防止

## マニフェスト例

```json
{
  "schema_version": 2,
  "name": "sample",
  "version": "1.0.0",
  "entry": "main.jp",
  "dependencies": {
    "hajimu_gui": {
      "version": "^14.5.0",
      "source": "ReoShiozawa/hajimu_gui"
    }
  }
}
```

日本語キーを使用したv1マニフェストは引き続き読み込めます。新規作成および更新時はv2形式で保存されます。

## コマンド

```sh
hajimu pkg search gui
hajimu pkg validate
hajimu pkg install
hajimu pkg install --allow-scripts
hajimu pkg update [パッケージ名]
hajimu pkg audit
```

`HAJIMU_REGISTRY`にローカルファイルまたはHTTPS URLを指定すると、使用するレジストリを切り替えられます。ネットワーク上のレジストリ、ソース、配布物にHTTPは使用できません。

## 信頼境界

Gitリポジトリはlockfileのコミットへ固定されます。配布バイナリはマニフェストまたはレジストリのSHA-256と一致する場合だけ使用します。lockfile、パッケージ名、エントリパスは利用前に検証され、パッケージ外を指すパスは拒否されます。ビルドスクリプトは任意コードを実行できるため、内容を確認してから`--allow-scripts`を指定してください。
