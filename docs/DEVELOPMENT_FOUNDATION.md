# 開発基盤と配布

更新日: 2026-10-24

## パーサー共有とエディタ

`hajimu --analyze filename.haj < source.haj`はソースを実行せず、通常実行と同じ字句解析器・パーサーで診断、宣言、識別子位置、組み込み名をJSONで返します。位置は1始まりのUnicode文字単位です。LSP側でUTF-16位置へ変換します。

`node tools/lsp.cjs /path/to/hajimu`でLSPを標準入出力上に起動します。対応要求はinitialize、shutdown、文書同期、診断、補完、hover、documentSymbol、definition、references、prepareRename、rename、formattingです。現在の名前解決は開いている文書内です。動的import、別ファイルへの参照、文字列補間を含む名前変更は自動変換の保証範囲外です。整形はインデントだけを対象とし、再解析と字句列一致を確認します。三重引用符を含む文書は変更しません。

VS Code拡張は`hajimu.languageServer.enabled`を既定で有効にし、旧独自診断・定義移動・フォーマッターとの二重登録を避けます。`hajimu.executablePath`にはこの解析APIに対応した本体を設定してください。拡張に含まれる`tools/`は本体の同名ディレクトリから同期します。

## DAP

`node tools/dap.cjs /path/to/hajimu`は標準DAPでlaunch、setBreakpoints、configurationDone、threads、stackTrace、scopes、variables、evaluate、continue、next、stepIn、stepOut、disconnectを受け付けます。本体の`--debug-protocol`へ接続し、評価器の文実行直前に停止します。条件付きブレークポイントも同じ評価器を使用します。

停止中の評価は識別子、リテラル、単項・二項式、添字に限定し、関数呼出し、代入、文字列補間を拒否します。標準出力はDAPのoutputイベントへ転送し、制御イベントは本体のstderr上の専用接頭辞で分離します。停止対象はメイン実行スレッドです。変数は現在フレームと呼出元フレームの環境を表示し、評価式は現在フレームを対象にします。実行中に要求したブレークポイント変更は次の停止で反映します。

## ネイティブABI

公開ヘッダーは`hajimu_plugin.h`と`hajimu_abi.h`です。プラグインのエントリポイントがある翻訳単位で`HAJIMU_PLUGIN_ABI_EXPORT()`を一度展開してください。ABI版、descriptorサイズ、Valueサイズ、plugin情報サイズ、runtimeサイズ、文字列長フィールドの位置、必要機能フラグを初期化前に検証します。

descriptorのない旧プラグインは既定で拒否します。移行時のみ、信頼しているプラグインに限って`HAJIMU_ALLOW_LEGACY_ABI=1`を指定できます。この指定は互換性の証明にはなりません。公式パッケージは同時に再ビルドしてください。

埋め込みホストは`include/hajimu_embed.h`の文字列ベースAPIを使用できます。`make embed`で共有ライブラリを構築し、`hajimu_embed_analyze`／`hajimu_embed_run`が返すJSONを`hajimu_embed_free`で解放します。実行はプロセス内で直列化され、同時実行要求はNULLを返します。実行するソースはホストと同じ権限を持ち、サンドボックスではありません。

## 署名と由来証明

公式レジストリはRSA-3072/SHA-256の署名を`registry/index.json.sig`に保存します。本体へ公開鍵を組み込んでおり、署名が不正な索引は拒否します。秘密鍵はリポジトリ外で管理し、`HAJIMU_REGISTRY_SIGNING_KEY`で指定できます。更新時は`bash scripts/sign-registry.sh`を実行してください。公開鍵の変更には本体に組み込む鍵と配布鍵の両方をレビューします。

独自HTTPSレジストリには`HAJIMU_REGISTRY_PUBLIC_KEY`が必須です。明示的に指定したローカルレジストリは開発用の信頼入力として扱い、公開鍵指定時は署名検証します。署名検証にはOpenSSL CLIが必要です。

各配布CIはSPDX SBOMを作成し、GitHub OIDCでビルド来歴とSBOMを署名します。[GitHub公式の検証方法](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations)に従い、ダウンロードした成果物を次で検証できます。

```sh
gh attestation verify ./artifact -R ReoShiozawa/hajimu
gh attestation verify ./artifact -R ReoShiozawa/hajimu --predicate-type https://spdx.dev/Document/v2.3
```

GitHub来歴証明はOSのコード署名とは別です。Developer ID、notarization、Authenticodeに必要な証明書はこの環境にありません。これらのOS署名済み成果物はまだ発行していません。Windows ARM64配布とパッケージマネージャー公式カタログへの登録も運用上の残項目です。`scripts/generate-install-manifests.cjs`は実ビルドのSHA-256からHomebrew/Scoop/Winget用ファイルを生成し、架空のハッシュを埋めません。

## 試験

`make test`、`make test-package`、`make test-conformance`、`make test-tooling`、`make test-abi`、`make test-utf8`、`make test-embed`を使用します。Linux CIではASan/UBSanとlibFuzzer、macOS/Linuxのx64/ARM64で開発基盤試験を実行します。ファズテストで見つかった途切れたUTF-8の境界外読み取りを修正し、不正な開始バイト・継続バイト・過長表現・サロゲート・Unicode範囲外も回帰試験します。失敗時のファズ入力はCIアーティファクトに保存します。ローカルでのコンパイル成功、ホスティング側CI成功、Windows実機起動は区別して記録します。
