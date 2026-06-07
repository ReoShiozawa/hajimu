# Hajimu 英語 alias 命名・衝突方針

作成日: 2026-06-06

この文書は、はじむ本体に英語 alias を追加するときの命名方針と、ユーザー定義名との衝突をどう扱うかをまとめたものです。

## 1. 基本方針

はじむの中心は日本語構文です。英語 alias は、日本語で理解した内容を英語圏のプログラミング言語へ橋渡しするための追加表記です。

そのため、英語 alias は次の順で判断します。

1. 既存の日本語コードを壊さない
2. 学習者が直感的に意味を推測できる
3. JavaScript / Python / C 系言語で広く見慣れた名前に寄せる
4. 短すぎて一般変数名と衝突しやすい名前は慎重に扱う

## 2. 予約語 alias

lexer で keyword として扱う alias は、識別子として使えません。

代表例:

| 分類 | alias |
|---|---|
| ブロック | `function`, `fn`, `end`, `return` |
| 変数 | `var`, `let`, `const` |
| 条件分岐 | `if`, `else_if`, `elif`, `else`, `then` |
| ループ | `while`, `do`, `for`, `repeat`, `from`, `to`, `each`, `for_each`, `in` |
| インポート | `import`, `use`, `as` |
| クラス | `class`, `type`, `new`, `extends`, `self`, `this`, `init`, `constructor`, `super`, `static` |
| 例外 | `try`, `catch`, `except`, `finally`, `throw`, `raise` |
| 値 | `true`, `false`, `null`, `nil`, `none` |
| 論理 | `and`, `or`, `not` |
| 型名 | `number`, `num`, `string`, `str`, `bool`, `boolean`, `array`, `list` |

予約語 alias はコードの構造を読むための名前なので、ユーザー定義名よりも優先します。

## 3. 組み込み関数 alias

組み込み関数 alias は keyword ではなく、グローバル環境に登録される定数です。

代表例:

| 分類 | alias |
|---|---|
| 入出力 | `print`, `println`, `input` |
| 変換 | `to_string`, `to_number`, `typeof` |
| 配列 | `len`, `length`, `append`, `push`, `pop`, `sort`, `reverse`, `slice` |
| 文字列 | `split`, `join`, `find`, `replace`, `upper`, `lower`, `trim`, `substring` |
| 辞書 | `keys`, `values`, `has` |
| 関数型 | `map`, `filter`, `reduce`, `for_each_value` |
| 数学 | `abs`, `sqrt`, `floor`, `ceil`, `round`, `random`, `random_int`, `max`, `min` |
| ファイル | `read_file`, `write_file`, `file_exists`, `append_file`, `list_dir`, `make_dir` |
| JSON / HTTP | `json_encode`, `json_decode`, `http_get`, `http_post`, `http_request` |
| 非同期 | `async_run`, `await_task`, `await_all`, `parallel_run`, `parallel_map` |
| 排他制御 | `mutex_create`, `mutex_exec`, `semaphore_create`, `semaphore_exec` |
| チャネル | `channel_create`, `channel_try_send`, `channel_try_receive`, `channel_count` |
| テスト | `test`, `run_tests`, `expect`, `expect_error`, `assert` |

これらは定数として定義されるため、同じスコープで再代入できません。学習者向けのドキュメントでは、上の名前を変数名として使わないように案内します。

`pi`、`PI`、`e`、`システム` などのランタイム定数も同じく保護されます。再定義や代入をしようとした場合は、別名候補を含むエラーを表示します。

## 4. 衝突しやすい名前

次の名前は短く便利ですが、一般的な変数名としても使われやすいので注意が必要です。

| 名前 | 理由 | 推奨 |
|---|---|---|
| `type` | クラス定義 alias として予約済み | 型確認は `typeof` を使う |
| `str` | 文字列型 alias として予約済み | 変換は `to_string` を使う |
| `number` | 数値型 alias として予約済み | 変換は `to_number` を使う |
| `list` | 配列型 alias として予約済み | 配列値は `items`, `values` など具体名にする |
| `values` | 辞書組み込み alias | 変数名は `items`, `rows`, `entries` などにする |
| `map` | 関数型組み込み alias | 辞書変数は `dict`, `table`, `lookup` などにする |
| `set` | 集合作成 alias | 代入系の動詞として使いたい場合は `set_value` などにする |
| `date`, `time` | 日付時刻組み込み alias | 変数名は `today`, `timestamp` などにする |

## 5. 命名規則

複数語の alias は原則 `snake_case` にします。

例:

- `to_string`
- `random_int`
- `read_file`
- `path_join`
- `async_run`
- `parallel_map`
- `channel_try_receive`

ただし、JavaScript 由来で広く見慣れているものは camelCase も補助 alias として許可します。

例:

- `startsWith`
- `endsWith`
- `charCodeAt`
- `fromCharCode`

## 6. 追加時のチェックリスト

新しい英語 alias を追加するときは、次を確認します。

1. 既存の日本語名と同じ実装へ接続している
2. keyword にする必要があるか、組み込み定数で十分かを判断した
3. よくある変数名を奪いすぎていない
4. `docs/REFERENCE.md` と `docs/REFERENCE_en.md` に追記した
5. 必要なら `tests/english_*` にテストを追加した
6. エラー文に日本語名と英語 alias の両方が出るか確認した

## 7. 今後の検討

将来的には、alias 衝突をより親切に扱うために次の機能を検討します。

- 予約語を変数名として使ったときの専用エラー（実装済み）
- 組み込み alias を上書きしようとしたときの説明（実装済み）
- `hajimu lint` による衝突しやすい名前の警告
- VS Code 拡張での予約語 / 組み込み alias ハイライト
- 日本語名、英語 alias、ローマ字入力候補の優先順位設定
