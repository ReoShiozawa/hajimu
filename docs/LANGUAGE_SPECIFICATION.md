# はじむ言語仕様 1.5

仕様日: 2026-10-24。対象は1.5系のAST実行系です。日本語構文と英語aliasは同一のトークン・AST・評価器を利用します。言語仕様の変更はこの文書と`tests/conformance`を同じ変更で更新します。

## 字句と構文

ソースはUTF-8です。改行は文区切りです。`#`と`//`は文字列外で行コメントを開始します。識別子は字句解析器が認めるUnicode文字と数字、アンダースコアからなり、先頭に数字を置けません。予約語は名前に使用できません。文字列は引用符で囲み、三重引用符では複数行を保持します。複数行ブロックの本体はインデントし、字句解析器のINDENT/DEDENTで範囲を決定します。終端に`終わり`または`end`を記述します。

次のEBNFは共通構文を表します。キーワードaliasの完全な対応は`src/lexer.c`のキーワード表を正本とし、変更時に準拠テストを更新します。型、列挙、照合、生成関数、内包表記などの拡張構文は[リファレンス](REFERENCE.md)の該当節を併用します。

```ebnf
program      = { statement, newline } ;
statement    = declaration | assignment | function | if | while | for
             | foreach | return | import | try | throw | expression
             | "break" | "continue" ;
declaration  = ("変数" | "var" | "let" | "定数" | "const"), identifier, "=", expression ;
assignment   = target, ("=" | "+=" | "-=" | "*=" | "/="), expression ;
function     = ("関数" | "function" | "fn"), identifier, "(", [parameters], ")", ":", newline, block, end ;
parameters   = parameter, {",", parameter} ;
parameter    = ["*"], identifier, ["=", expression] ;
if           = ("もし" | "if"), expression, ["なら" | "then"], [":"], newline,
               block, { ("それ以外もし" | "elif"), expression, [":"], newline, block },
               [("それ以外" | "else"), [":"], newline, block], end ;
while        = ("条件" | "while"), expression, ["の間"], [":"], newline, block, end ;
for          = "for", identifier, "from", expression, "to", expression, [":"], newline, block, end
             | identifier, "を", expression, "から", expression, "まで", "繰り返す", newline, block, end ;
foreach      = ("各" | "each" | "for"), identifier, ("の中" | "in"), expression, [":"], newline, block, end ;
return       = ("戻す" | "return"), [expression] ;
import       = ("取り込む" | "import"), string, [("として" | "as"), identifier] ;
try          = ("試行" | "try"), ":", newline, block,
               [("捕獲" | "catch"), identifier, ":", newline, block],
               [("最終" | "finally"), ":", newline, block], end ;
throw        = ("投げる" | "throw"), expression ;
expression   = precedence_expression ;
primary      = number | string | boolean | null | identifier | array | dictionary | "(", expression, ")" ;
postfix      = primary, { "(", [arguments], ")" | "[", expression, "]" | ".", identifier } ;
block        = indent, { statement, newline }, dedent ;
end          = "終わり" | "end" ;
```

上記は共通構文の索引であり、パーサーが受理しない組合せを新たに許可するものではありません。`--analyze`による同じパーサーで受理可否を検査できます。

## 値と演算

値は動的型です。数値、文字列、真偽、無、配列、辞書、関数、型、インスタンス、ジェネレーター、数値ベクトル、行列を区別します。数値には整数性の属性があり、配列添字には整数値が必要です。文字列長と添字はUnicode文字単位で、保存領域・ABI・I/Oはバイト長を使用します。配列添字は0始まりです。コピー・変更の挙動は値型ごとの`value_copy`と準拠テストに従い、関数のクロージャ環境とジェネレーター状態は共有されます。

優先順位は低い順に、代入、パイプ、null合体、論理OR、論理AND、比較、加減、乗除剰余、べき乗、単項、後置呼出し・添字・メンバーです。代入・べき乗の結合規則は準拠試験で固定します。`and`／`or`は短絡評価します。無効な演算・ゼロ除算・添字範囲外は実行時エラーです。

## スコープと実行

名前解決は現在の環境から親環境へ辿ります。関数は定義時の環境をクロージャとして保持します。`const`は再代入できません。組み込み関数aliasとランタイム定数は保護されます。トップレベル文を順に実行した後、`メイン`関数が存在すれば呼び出します。通常の関数引数は評価してから渡します。

`return`は現在の関数、`break`／`continue`は現在のループへ作用します。`throw`は値を例外として送出します。捕獲された実行時診断は`message`、`file`、`line`、`column`、`kind`を持ちます。`finally`は正常・例外経路で実行します。

## 互換性

- 1.5系の修正版で、正常なプログラムの構文と評価結果を破壊しません。未定義動作、メモリ破壊、不正入力の拒否は修正対象です。
- 日本語と英語aliasは同じ機能を指します。新しいaliasが既存の識別子と衝突する場合は追加前にRFCで検討します。
- ネイティブABIは言語版とは独立して番号を持ちます。`Value`のサイズ・配置を変更する場合はABI版を更新し、公式プラグインを再構築します。
- 仕様変更には例、移行方法、日英準拠試験を含めます。性能だけの変更でも評価順・例外・副作用を保持します。

## 検証

`make test-conformance`は日英の同等結果、クロージャ、定数、短絡、Unicode、例外を検証します。`make test-tooling`は同じパーサーを使ったLSPと実評価器につながるDAPを検証します。完全な回帰試験は`make test`です。
