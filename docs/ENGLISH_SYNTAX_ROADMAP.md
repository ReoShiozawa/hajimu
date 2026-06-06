# Hajimu 英語構文対応 設計書兼ロードマップ

作成日: 2026-06-06

この文書は、はじむを日本語だけでなく英語でも書けるようにするための設計方針と実装ロードマップです。

## 1. 目的

はじむは、日本語で読み書きできることを中心価値にしている言語です。一方で、学習・共有・海外ユーザー・他言語からの移行を考えると、同じ実行系で英語構文も受け付けられることには大きな利点があります。

英語構文対応の目的は、はじむを英語の別言語へ作り替えることではありません。既存の日本語構文を守りながら、同じ AST、同じ評価器、同じ標準ライブラリへ接続できる英語 alias を追加し、日本語と英語を行き来しやすい言語にすることです。

## 2. 基本方針

### 2.1 日本語構文は維持する

既存の `.jp` コード、テスト、ドキュメント、パッケージは壊さないことを最優先にします。英語構文は追加機能であり、日本語構文の置き換えではありません。

### 2.2 同じ TokenType / AST / Evaluator を使う

英語キーワードは、原則として既存の `TokenType` に直接割り当てます。

例:

| 入力 | TokenType | 意味 |
|---|---|---|
| `関数` | `TOKEN_FUNCTION` | 関数定義 |
| `function` | `TOKEN_FUNCTION` | 関数定義 |
| `戻す` | `TOKEN_RETURN` | 戻り値 |
| `return` | `TOKEN_RETURN` | 戻り値 |

これにより、parser と evaluator の大半をそのまま使えます。

### 2.3 最初は「英語 alias」、次に「英語らしい構文」

初期段階では、日本語構文の形をほぼ保ったまま、キーワードだけ英語でも書けるようにします。

例:

```hajimu
function greet(name):
    print(name)
end
```

次の段階で、英語として自然な構文も追加します。

例:

```hajimu
for item in items:
    print(item)
end
```

既存 parser は `各 item を items の中:` のような日本語寄りの構文を前提にしているため、自然な英語構文は段階的に parser 側で対応します。

### 2.4 混在を許可する

学習用途では、日本語と英語を少しずつ置き換えられることが重要です。そのため、同じファイル内で日本語キーワードと英語キーワードが混在しても動く方針にします。

例:

```hajimu
function 合計(数列):
    変数 total = 0
    for value in 数列:
        total += value
    end
    return total
end
```

ただし、ドキュメントでは読みやすさのため、日本語スタイル・英語スタイル・混在スタイルを分けて説明します。

## 3. 対応する構文

### 3.1 キーワード alias

まずは lexer の `keywords[]` に英語 alias を追加し、既存トークンへ割り当てます。

| 日本語 | 英語候補 | TokenType |
|---|---|---|
| `関数` | `function`, `fn` | `TOKEN_FUNCTION` |
| `生成関数` | `generator`, `generator_function` | `TOKEN_GENERATOR_FUNC` |
| `終わり` | `end` | `TOKEN_END` |
| `戻す`, `返す` | `return` | `TOKEN_RETURN` |
| `変数` | `var`, `let` | `TOKEN_VARIABLE` |
| `定数` | `const` | `TOKEN_CONSTANT` |
| `もし` | `if` | `TOKEN_IF` |
| `それ以外もし` | `else_if`, `elif` | `TOKEN_ELSE_IF` |
| `それ以外` | `else` | `TOKEN_ELSE` |
| `なら` | `then` | `TOKEN_THEN` |
| `条件` | `while` | `TOKEN_WHILE_COND` |
| `の間` | `do` | `TOKEN_WHILE_END` |
| `繰り返す` | `repeat`, `for` | `TOKEN_FOR` |
| `から` | `from` | `TOKEN_FROM` |
| `を` | `to` | `TOKEN_TO` |
| `抜ける` | `break` | `TOKEN_BREAK` |
| `続ける` | `continue` | `TOKEN_CONTINUE` |
| `取り込む` | `import`, `use` | `TOKEN_IMPORT` |
| `型` | `class`, `type` | `TOKEN_CLASS` |
| `新規` | `new` | `TOKEN_NEW` |
| `継承` | `extends` | `TOKEN_EXTENDS` |
| `自分` | `self`, `this` | `TOKEN_SELF` |
| `初期化` | `init`, `constructor` | `TOKEN_INIT` |
| `親` | `super` | `TOKEN_SUPER` |
| `試行` | `try` | `TOKEN_TRY` |
| `捕獲` | `catch`, `except` | `TOKEN_CATCH` |
| `最終` | `finally` | `TOKEN_FINALLY` |
| `投げる` | `throw`, `raise` | `TOKEN_THROW` |
| `列挙` | `enum` | `TOKEN_ENUM` |
| `照合` | `match` | `TOKEN_MATCH` |
| `静的` | `static` | `TOKEN_STATIC` |
| `譲渡` | `yield` | `TOKEN_YIELD` |
| `選択` | `switch` | `TOKEN_SWITCH` |
| `場合` | `case` | `TOKEN_CASE` |
| `既定` | `default` | `TOKEN_DEFAULT` |
| `各` | `each`, `for_each` | `TOKEN_EACH` |
| `の中` | `in` | `TOKEN_IN` |
| `真` | `true` | `TOKEN_TRUE` |
| `偽` | `false` | `TOKEN_FALSE` |
| `無` | `null`, `nil`, `none` | `TOKEN_NULL_LITERAL` |
| `かつ` | `and` | `TOKEN_AND` |
| `または` | `or` | `TOKEN_OR` |
| `でない` | `not` | `TOKEN_NOT` |
| `は` | `is`, `as` | `TOKEN_TYPE_IS` |
| `数値` | `number`, `num` | `TOKEN_TYPE_NUMBER` |
| `文字列` | `string`, `str` | `TOKEN_TYPE_STRING_T` |
| `真偽` | `bool`, `boolean` | `TOKEN_TYPE_BOOL` |
| `配列` | `array`, `list` | `TOKEN_TYPE_ARRAY` |

### 3.2 最初から動かしたい英語構文

既存 parser を大きく変えずに動かせる構文です。

```hajimu
function add(a, b):
    return a + b
end

var score = 80
if score >= 70 then
    print("OK")
else
    print("NG")
end
```

```hajimu
class User:
    init(name):
        self.name = name
    end

    function greet():
        print(self.name)
    end
end
```

### 3.3 parser 拡張が必要な英語構文

英語として自然にするには、既存の日本語語順とは異なる構文を追加する必要があります。

#### while

既存:

```hajimu
条件 x < 10 の間
    表示(x)
終わり
```

初期 alias:

```hajimu
while x < 10 do
    print(x)
end
```

将来の省略形:

```hajimu
while x < 10:
    print(x)
end
```

#### range for

既存:

```hajimu
1 から 5 まで i を 繰り返す
    表示(i)
終わり
```

初期 alias:

```hajimu
1 from 5 to i repeat
    print(i)
end
```

将来の自然形:

```hajimu
for i from 1 to 5:
    print(i)
end
```

#### foreach

既存:

```hajimu
各 item を items の中:
    表示(item)
終わり
```

将来の自然形:

```hajimu
for item in items:
    print(item)
end
```

## 4. 標準ライブラリ / 組み込み関数の英語 alias

構文だけでなく、組み込み関数にも英語 alias が必要です。

優先度の高い alias:

| 日本語 | 英語候補 |
|---|---|
| `表示` | `print`, `println` |
| `入力` | `input` |
| `長さ` | `len`, `length` |
| `追加` | `append`, `push` |
| `末尾削除` | `pop` |
| `部分文字列` | `substring`, `slice` |
| `始まる` | `starts_with`, `startsWith` |
| `終わる` | `ends_with`, `endsWith` |
| `乱数` | `random` |
| `乱数整数` | `random_int`, `randint` |
| `文字列化` | `to_string`, `str` |
| `数値化` | `to_number`, `number` |
| `型判定` | `instanceof`, `is_instance` |
| `表明` | `assert` |
| `テスト` | `test` |
| `テスト実行` | `run_tests` |
| `期待` | `expect` |
| `ファイル読込` | `read_file` |
| `ファイル書込` | `write_file` |
| `追記` | `append_file` |
| `ディレクトリ一覧` | `list_dir` |
| `ディレクトリ作成` | `make_dir`, `mkdir` |

実装では、評価器内の組み込み関数登録時に同じ C 関数へ複数名を登録します。日本語名は互換性のため必ず残します。

## 5. ファイル拡張子とモード

### 5.1 拡張子

当面は既存の `.jp` をそのまま使います。英語構文専用の拡張子は急いで追加しません。

将来的な候補:

- `.jp`: 日本語・英語混在を許可する標準拡張子
- `.haj`: 英語圏向けに見せやすい別名
- `.hajimu`: 教材やドキュメント向けの明示的な別名

### 5.2 CLI オプション

初期実装では、英語構文を常に有効にします。後から必要になった場合にだけ、以下のようなオプションを検討します。

```bash
hajimu --syntax japanese file.jp
hajimu --syntax english file.jp
hajimu --syntax mixed file.jp
```

ただし、最初からモードを分けると学習者が迷うため、初期は mixed 固定が望ましいです。

## 6. エラー表示方針

英語構文を入れても、エラー文は当面日本語を主にします。将来的には、英語エラー文も選べるようにします。

優先する改善:

- `end` が足りない場合も `終わり/end が必要です` と表示する
- `then` が足りない場合も `なら/then が必要です` と表示する
- 英語キーワードで書いたコードには、英語 alias を含む修正候補を出す
- 混在構文の場合も、どちらの表記でも直せるように案内する

例:

```text
エラー: if 文には 'なら' または 'then' が必要です
  例: if score >= 70 then
```

## 7. 実装ロードマップ

### Phase 0: 設計と合意

ステータス: この文書で着手

- 英語構文の目的を整理する
- 日本語構文を残す方針を明文化する
- alias と自然な英語構文を分ける
- 実装優先度を決める

### Phase 1: lexer keyword alias

目的: parser をほぼ変更せず、英語キーワードを受け付ける。

作業:

- `src/lexer.c` の `keywords[]` に英語 alias を追加する
- `KEYWORD_HASH_TABLE_SIZE` を必要に応じて拡張する
- `token_names[]` は日本語のまま維持するか、代表名を別途整理する
- 英語キーワードの lexer テストを追加する

完了条件:

- `function`, `return`, `var`, `const`, `if`, `then`, `else`, `end`, `true`, `false`, `null`, `and`, `or`, `not` が認識される
- 既存日本語コードのテストが壊れない

### Phase 2: 最小英語構文の実行

目的: 英語キーワードだけで基本プログラムを書けるようにする。

作業:

- 関数定義、変数宣言、if、return の英語サンプルを追加する
- class / try / enum / match などの alias を追加する
- parser エラー文に日本語/英語の両方を含める

完了条件:

```hajimu
function main():
    var name = "Hajimu"
    if name != "" then
        print(name)
    else
        print("empty")
    end
end
```

上記に近いコードが実行できる。

### Phase 3: 組み込み関数の英語 alias

目的: `print`, `input`, `len` など、英語だけでも自然に書けるようにする。

作業:

- 組み込み関数登録箇所に英語 alias を追加する
- 既存日本語名と英語名が同じ実装へ向くことを確認する
- alias がユーザー定義関数と衝突した場合の挙動を確認する
- リファレンスに英語 alias 表を追加する

完了条件:

- `print()`, `input()`, `len()`, `append()`, `random_int()` などが動く
- 日本語名も引き続き動く

### Phase 4: 英語らしい制御構文

目的: 英語ユーザーにとって自然な `for item in items` などを受け付ける。

作業:

- `parse_for_statement` に `for i from start to end:` 形式を追加する
- `parse_foreach_statement` に `for item in items:` 形式を追加する
- `parse_while_statement` に `while condition:` 形式を追加する
- 日本語語順の既存構文と衝突しないように分岐する
- それぞれテストを追加する

完了条件:

```hajimu
for item in items:
    print(item)
end

for i from 1 to 10:
    print(i)
end

while count < 10:
    count += 1
end
```

が実行できる。

### Phase 5: ドキュメントと教育導線

目的: 日本語ユーザーにも英語ユーザーにも分かりやすく説明する。

作業:

- `docs/REFERENCE.md` に英語構文セクションを追加する
- `docs/REFERENCE_en.md` に English-first examples を追加する
- `docs/TUTORIAL.md` に日本語/英語混在の学習例を追加する
- `examples/english_basic.jp` を追加する
- `examples/english_oop.jp` を追加する
- `examples/english_mixed.jp` を追加する

完了条件:

- 初めて読む人が、英語構文で hello world、if、loop、function、class を書ける
- 日本語構文から英語構文への対応表がある

### Phase 6: VS Code 拡張 / Hajimu Bridge 連携

目的: 本体の英語構文を周辺ツールでも扱えるようにする。

作業:

- VS Code 拡張の syntax highlight に英語キーワードを追加する
- 補完候補に英語 alias を追加する
- ローマ字変換候補と英語 alias の優先順位を整理する
- Hajimu Bridge の parser / generator に英語構文モードを追加する
- 学習ガイドで「日本語から英語へ」「英語から日本語へ」の切り替えを説明する

完了条件:

- 本体で動く英語コードが、VS Code と Hajimu Bridge でも壊れず表示・補完される

## 8. テスト方針

### 8.1 追加するテスト

- `tests/english_keywords.jp`
- `tests/english_basic.jp`
- `tests/english_control_flow.jp`
- `tests/english_builtins.jp`
- `tests/english_mixed_syntax.jp`

### 8.2 確認すること

- 日本語構文が壊れていない
- 英語構文が同じ AST / 評価結果になる
- 日本語と英語の混在が動く
- keyword alias が識別子として使われた場合の扱いが明確である
- エラー文が日本語構文だけを前提にしすぎていない

### 8.3 注意する衝突

英語 keyword alias を増やすと、ユーザーが `print` や `class` を変数名として使えなくなる可能性があります。

方針:

- 制御構文に必要な単語はキーワード化する
- 組み込み関数名はキーワードではなく、通常の識別子として登録する
- `print`, `len`, `append` などは lexer keyword ではなく built-in alias にする
- `class`, `function`, `if`, `else`, `end` などは構文キーワードとして扱う

## 9. 互換性方針

- 既存の日本語コードは壊さない
- 既存の `.jp` 拡張子はそのまま使う
- 日本語名の組み込み関数は削除しない
- 英語 alias は同じ実装へ接続する
- エラー表示やドキュメントでは、日本語と英語の対応をできるだけ併記する

## 10. 最初の実装候補

最初の PR / コミットでは、以下に絞ると安全です。

1. `src/lexer.c` に最小英語キーワードを追加
2. `tests/english_basic.jp` を追加
3. `examples/english_basic.jp` を追加
4. parser エラー文の一部を `終わり/end`、`なら/then` のように併記
5. `docs/REFERENCE.md` に短い英語構文入口を追加

最小英語キーワード:

```text
function, fn, end, return,
var, let, const,
if, else, elif, else_if, then,
while, do,
break, continue,
true, false, null,
and, or, not,
class, new, self, this, init, super
```

## 11. 将来構想

英語構文対応が安定したら、以下も検討します。

- 日本語構文から英語構文への pretty print
- 英語構文から日本語構文への pretty print
- `hajimu format --style japanese`
- `hajimu format --style english`
- エラー文の日本語 / 英語切り替え
- 英語版チュートリアルの整備
- Hajimu Bridge での日本語/英語/ブロック/Python/JavaScript の 5 表現対応

## 12. まとめ

英語構文対応は、はじむの日本語性を弱めるものではなく、むしろ学習者が日本語と本格的なプログラミング言語の間を行き来するための橋を増やす取り組みです。

まずは安全な keyword alias から始め、組み込み関数 alias、自然な英語制御構文、周辺ツール連携の順に進めます。

