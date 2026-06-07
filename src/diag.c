/**
 * 日本語プログラミング言語 - 診断メッセージユーティリティ実装
 */

#include "diag.h"
#include "lexer.h"  /* utf8_char_length */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef _WIN32
#  include <io.h>      /* _isatty, _fileno */
#  define isatty _isatty
#  define fileno _fileno
#else
#  include <unistd.h>  /* isatty */
#endif

// =============================================================================
// ANSI カラー (TTY のみ有効)
// =============================================================================

static bool use_color(void) {
    static int cached = -1;
    if (cached < 0) cached = isatty(fileno(stderr));
    return cached;
}

#define C_RESET   (use_color() ? "\033[0m"  : "")
#define C_BOLD    (use_color() ? "\033[1m"  : "")
#define C_RED     (use_color() ? "\033[1;31m" : "")
#define C_YELLOW  (use_color() ? "\033[1;33m" : "")
#define C_CYAN    (use_color() ? "\033[1;36m" : "")
#define C_BLUE    (use_color() ? "\033[1;34m" : "")
#define C_GREEN   (use_color() ? "\033[1;32m" : "")
#define C_GRAY    (use_color() ? "\033[0;90m" : "")

// =============================================================================
// エラー種別ラベル
// =============================================================================

const char *diag_kind_label(DiagKind kind) {
    switch (kind) {
        case DIAG_SYNTAX:    return "構文エラー";
        case DIAG_RUNTIME:   return "実行時エラー";
        case DIAG_NAME:      return "名前エラー";
        case DIAG_TYPE:      return "型エラー";
        case DIAG_VALUE:     return "値エラー";
        case DIAG_INDEX:     return "インデックスエラー";
        case DIAG_ZERO_DIV:  return "ゼロ除算エラー";
        case DIAG_OVERFLOW:  return "スタックオーバーフロー";
        case DIAG_ATTRIBUTE: return "属性エラー";
        case DIAG_USER:      return "例外";
        default:             return "エラー";
    }
}

// =============================================================================
// ソース行抽出
// =============================================================================

int diag_extract_line(const char *source, int line_num,
                      char *buf, int buf_size) {
    if (!source || line_num <= 0 || !buf || buf_size <= 0) return 0;

    int cur_line = 1;
    const char *p = source;

    /* 目的行の先頭まで進む */
    while (*p && cur_line < line_num) {
        if (*p == '\n') cur_line++;
        p++;
    }
    if (!*p && cur_line < line_num) return 0;  /* 行が存在しない */

    /* 行末 or バッファ末まで書き写す */
    int len = 0;
    while (*p && *p != '\n' && len < buf_size - 1) {
        buf[len++] = *p++;
    }
    buf[len] = '\0';
    return len;
}

// =============================================================================
// UTF-8 文字数カウント (バイト列の文字数 ≒ 表示列幅に近似)
// =============================================================================

int diag_utf8_strlen(const char *s, int byte_len) {
    int chars = 0;
    int i = 0;
    while (i < byte_len && s[i]) {
        int clen = utf8_char_length((unsigned char)s[i]);
        if (clen <= 0) clen = 1;
        i += clen;
        chars++;
    }
    return chars;
}

static bool contains(const char *s, const char *needle) {
    return s && needle && strstr(s, needle) != NULL;
}

static void diag_build_advice(
    DiagKind kind,
    const char *message,
    const char **cause,
    const char **fix,
    const char **example
) {
    *cause = NULL;
    *fix = NULL;
    *example = NULL;

    if (contains(message, "予約語なので名前に使えません")) {
        *cause = "`if`、`class`、`for`、`str` などは、構文や型名として先に解釈される予約語です。";
        *fix = "変数名・関数名には、用途が分かる別名を使ってください。英語なら `name_value`、`class_name`、`items` のような名前が安全です。";
        *example = "var class_name = \"User\"";
        return;
    }
    if (contains(message, "組み込み関数名") && contains(message, "できません")) {
        *cause = "`print`、`len`、`map` などは標準ライブラリの関数名として予約されています。";
        *fix = "標準関数を上書きせず、値の役割が分かる名前に変えてください。例: `output`、`item_count`、`mapped_items`。";
        return;
    }
    if (contains(message, "ランタイム定数") && contains(message, "できません")) {
        *cause = "`pi`、`e`、`システム` などは実行環境が提供する定数です。";
        *fix = "計算結果を入れる場合は `pi_value`、`ratio`、`system_info` など別の名前を使ってください。";
        return;
    }
    if (contains(message, "未定義の変数")) {
        *cause = "この名前は、現在の場所から見える変数・関数としてまだ登録されていません。";
        *fix = "名前の打ち間違いを直すか、使う前に `変数 名前 = ...` または `関数 名前(...)` で定義してください。";
        *example = "変数 合計 = 0";
        return;
    }
    if (contains(message, "未定義") && contains(message, "もしかして:")) {
        *cause = "名前が少しだけ違う、近い定義が見つかっています。";
        *fix = "`もしかして:` の候補に置き換えるか、意図した別名を先に定義してください。";
        return;
    }
    if (contains(message, "'終わり' が必要")) {
        *cause = "`関数/function`、`もし/if`、`繰り返す/for`、`型/class` などのブロックが閉じられていません。";
        *fix = "対応するブロックの最後に `終わり` または `end` を追加してください。途中に `それ以外/else` や `捕獲/catch` がある場合は、最後の節の後に置きます。";
        *example = "if 条件 then\\n    print(\"OK\")\\nend";
        return;
    }
    if (contains(message, "対応する開始文のない '終わり'")) {
        *cause = "`終わり` だけが余っています。対応する開始文がないか、インデントや位置がずれています。";
        *fix = "直前の `関数`、`もし`、`繰り返す` などとの対応を確認し、余分な `終わり` を削除してください。";
        return;
    }
    if (contains(message, "':' が必要")) {
        *cause = "ブロックを開始する文の末尾にコロンがありません。";
        *fix = "`関数 名前(...):`、`試行:`、`型 名前:` のように、本文が続く文の末尾に `:` を付けてください。";
        *example = "関数 挨拶():";
        return;
    }
    if (contains(message, "'なら' が必要")) {
        *cause = "`もし/if` の条件式の後に、条件の終わりを示す `なら` または `then` がありません。";
        *fix = "`もし 条件 なら`、`if condition then`、または `if condition:` の形にしてください。";
        *example = "if score >= 80 then";
        return;
    }
    if (contains(message, "'の間' が必要")) {
        *cause = "`条件/while` ループの条件式の後に `の間`、`do`、または `:` がありません。";
        *fix = "`条件 条件式 の間`、`while condition do`、または `while condition:` の形にしてください。";
        *example = "while i < 10:";
        return;
    }
    if (contains(message, "改行が必要")) {
        *cause = "文の区切りが曖昧です。はじむでは基本的に1文ごとに改行します。";
        *fix = "次の文を新しい行に分けるか、ブロックなら末尾に `:` を付けてインデントしてください。";
        return;
    }
    if (contains(message, "')' が必要")) {
        *cause = "関数呼び出し、関数定義、またはグループ化の `(` が閉じられていません。";
        *fix = "対応する場所に `)` を追加してください。引数の区切りは `,` です。";
        *example = "表示(\"こんにちは\")";
        return;
    }
    if (contains(message, "']' が必要")) {
        *cause = "配列リテラル、分割代入、またはインデックスアクセスの `[` が閉じられていません。";
        *fix = "対応する場所に `]` を追加してください。";
        *example = "変数 最初 = 配列[0]";
        return;
    }
    if (contains(message, "'=' が必要")) {
        *cause = "変数宣言や代入で、名前と値をつなぐ `=` がありません。";
        *fix = "`変数 名前 = 値` の形にしてください。";
        *example = "変数 名前 = \"はじむ\"";
        return;
    }
    if (contains(message, "取り込むファイルパスが必要")) {
        *cause = "`取り込む/import/use` の後に、読み込むファイル名またはパッケージ名の文字列がありません。";
        *fix = "`取り込む \"ファイル.jp\"` または `import \"file.jp\"` のように、パスをダブルクォートで囲んでください。";
        *example = "import \"lib/math.jp\" as math";
        return;
    }
    if (contains(message, "エイリアス名が必要")) {
        *cause = "`として` の後に、取り込んだ内容を入れる名前がありません。";
        *fix = "`取り込む \"lib.jp\" として ライブラリ` の形にしてください。";
        *example = "取り込む \"lib/math.jp\" として 数学";
        return;
    }
    if (contains(message, "'を' が必要")) {
        *cause = "`繰り返す` 文や `各` 文で、対象を示す `を` が抜けています。";
        *fix = "カウンタループなら `i を 0 から 10 繰り返す` の形にしてください。";
        *example = "i を 0 から 10 繰り返す";
        return;
    }
    if (contains(message, "'から' が必要")) {
        *cause = "ループの開始値と終了値をつなぐ `から` が抜けています。";
        *fix = "`i を 開始 から 終了 繰り返す` の形にしてください。";
        *example = "i を 0 から 10 繰り返す";
        return;
    }
    if (contains(message, "'繰り返す' が必要")) {
        *cause = "カウンタループの末尾に `繰り返す/repeat` がありません。";
        *fix = "`i を 0 から 10 繰り返す`、または英語構文なら `for i from 0 to 10:` の形にしてください。";
        *example = "for i from 0 to 10:";
        return;
    }
    if (contains(message, "'の中' が必要")) {
        *cause = "`各/for` 文で、どの配列や辞書を走査するか示す `の中/in` がありません。";
        *fix = "`各 要素 を 配列 の中:`、または `for item in items:` の形にしてください。";
        *example = "for item in items:";
        return;
    }
    if (contains(message, "'=>' が必要")) {
        *cause = "`照合` や分岐パターンで、条件と結果をつなぐ `=>` がありません。";
        *fix = "`場合 値 => 処理` のように、左の条件と右の処理を `=>` で結んでください。";
        *example = "場合 1 => 表示(\"one\")";
        return;
    }
    if (contains(message, "三項演算子に ':' が必要")) {
        *cause = "`条件 ? 真の値 : 偽の値` の `:` がありません。";
        *fix = "`?` の右側に、真のときの値と偽のときの値を `:` で分けて書いてください。";
        *example = "点数 >= 60 ? \"合格\" : \"再挑戦\"";
        return;
    }
    if (contains(message, "変数名が必要") || contains(message, "関数名が必要")
            || contains(message, "パラメータ名が必要")) {
        *cause = "ここには名前が必要ですが、別の記号やキーワードが来ています。";
        *fix = "日本語・英数字・アンダースコアを使った識別子を書いてください。";
        *example = "関数 合計(数値):";
        return;
    }
    if (contains(message, "ゼロ除算")) {
        *cause = "割り算または剰余計算で、右側の値が 0 になっています。";
        *fix = "割る前に 0 かどうかを確認し、0 のときの処理を分けてください。";
        *example = "もし 分母 != 0 なら 結果 = 分子 / 分母 終わり";
        return;
    }
    if (contains(message, "インデックスが範囲外")) {
        *cause = "配列または文字列に、存在しない位置でアクセスしています。";
        *fix = "インデックスは 0 から始まります。`長さ(配列)` より小さい値か確認してください。";
        *example = "もし i < 長さ(配列) なら 表示(配列[i]) 終わり";
        return;
    }
    if (contains(message, "インデックスは整数")) {
        *cause = "配列や文字列の `[]` には、小数ではなく整数の位置を指定します。";
        *fix = "`配列[0]` のように整数を使ってください。小数を計算した結果なら `切り捨て()` などで明示的に整数値へ変換します。";
        *example = "表示(配列[切り捨て(i)])";
        return;
    }
    if (contains(message, "辞書のキーは文字列")) {
        *cause = "辞書の `[]` には文字列キーが必要です。";
        *fix = "`辞書[\"名前\"]` のように、キーを文字列で指定してください。";
        return;
    }
    if (contains(message, "インデックスアクセスは配列")) {
        *cause = "`[]` は配列、文字列、辞書にだけ使えます。いまの左側の値は `[]` に対応していません。";
        *fix = "`値[番号]` または `辞書[\"キー\"]` の形になっているか確認してください。JSON解析後の値なら、途中のキーが存在するかも確認します。";
        *example = "本文[\"headers\"][\"User-Agent\"]";
        return;
    }
    if (contains(message, "呼び出し可能ではありません") || contains(message, "関数ではありません")) {
        *cause = "`(...)` は関数やメソッドにだけ使えます。";
        *fix = "呼び出している名前が関数か確認してください。値を表示したいだけなら `(...)` を外します。";
        *example = "表示(名前)";
        return;
    }
    if (contains(message, "不正な演算")) {
        *cause = "この型の組み合わせでは、その演算子を使えません。";
        *fix = "数値同士、文字列同士など、演算できる型にそろえてください。必要なら `文字列化()` や `数値化()` を使います。";
        return;
    }
    if (contains(message, "定数") && contains(message, "再定義")) {
        *cause = "`定数` は一度決めた値をあとから変えられません。";
        *fix = "値を変えたい場合は `変数` として宣言してください。";
        return;
    }
    if (contains(message, "引数が必要") || contains(message, "引数は最大")
            || contains(message, "個渡されました")) {
        *cause = "関数に渡した引数の数が、定義されている個数と合っていません。";
        *fix = "関数定義のパラメータ数を確認し、足りない引数を追加するか、余分な引数を削除してください。";
        *example = "関数 足す(a, b):";
        return;
    }
    if (contains(message, "可変長引数は最後")) {
        *cause = "`*引数` は残りの引数をすべて受け取るため、その後に別のパラメータを置けません。";
        *fix = "`関数 名前(先頭, *残り):` のように、可変長引数を最後に移動してください。";
        *example = "関数 合計(最初, *残り):";
        return;
    }
    if (contains(message, "スプレッド演算子は配列")) {
        *cause = "`...` は配列の中身を引数として展開するための記法です。";
        *fix = "`...値` の値が配列になっているか確認してください。";
        *example = "表示(...配列)";
        return;
    }
    if (contains(message, "モジュール") && contains(message, "見つかりません")) {
        *cause = "`取り込む` で指定したファイル、パッケージ、またはプラグインが見つかりません。";
        *fix = "パスの打ち間違い、実行中ファイルからの相対位置、パッケージの追加状態を確認してください。";
        *example = "取り込む \"./lib/math.jp\"";
        return;
    }
    if (contains(message, "モジュール") && contains(message, "読み込めません")) {
        *cause = "ファイルは見つかりましたが、読み込み権限やファイル状態のため開けませんでした。";
        *fix = "ファイルの権限、パス、別プロセスによるロックを確認してください。";
        return;
    }
    if (contains(message, "パースに失敗")) {
        *cause = "取り込んだファイルの中で構文エラーが発生しました。";
        *fix = "このエラーの直前に表示されている、取り込んだファイル側の構文エラーを先に直してください。";
        return;
    }
    if (contains(message, "プラグイン") && contains(message, "読み込みに失敗")) {
        *cause = "指定したプラグインファイルを動的ライブラリとして読み込めませんでした。";
        *fix = "プラグインのビルド形式、依存ライブラリ、OSに合う拡張子かを確認してください。";
        return;
    }
    if (contains(message, "バイトコード") && contains(message, "読み込みに失敗")) {
        *cause = "`.hjp` バイトコードの形式が壊れているか、現在の実行環境と合っていません。";
        *fix = "元の `.jp` からもう一度ビルドし直してください。";
        return;
    }
    if (contains(message, "クラスではありません")) {
        *cause = "`新規/new` や継承で使った名前が、クラスとして定義されていません。";
        *fix = "`型 名前:` または `class Name:` でクラスを定義してから使うか、クラス名の打ち間違いを直してください。";
        *example = "class Person:\\n    init():\\n    end\\nend";
        return;
    }
    if (contains(message, "静的メソッド") && contains(message, "ありません")) {
        *cause = "クラスに、その名前の静的メソッドが定義されていません。";
        *fix = "クラス内で `静的 関数 名前(...):` を定義するか、呼び出す名前を確認してください。";
        return;
    }
    if (contains(message, "フィールドまたはメソッドがありません")) {
        *cause = "インスタンスに、その名前のフィールドまたはメソッドが見つかりません。";
        *fix = "`自分.名前 = ...` で初期化されているか、クラス内のメソッド名が合っているか確認してください。";
        return;
    }
    if (contains(message, "辞書に") && contains(message, "キーがありません")) {
        *cause = "辞書に指定したキーが存在しません。";
        *fix = "キーの文字列を確認し、存在チェックが必要なら `辞書キー存在()` や `含む()` 相当の関数で先に確認してください。";
        return;
    }
    if (contains(message, "メンバーアクセスは")) {
        *cause = "`.` はインスタンス、辞書、クラスのメンバーを見るための記法です。";
        *fix = "左側の値がインスタンス、辞書、クラスのどれかになっているか確認してください。";
        *example = "ユーザー.名前";
        return;
    }
    if (contains(message, "非公開フィールド")) {
        *cause = "`_` で始まるフィールドは、外側から直接触れない設計です。";
        *fix = "クラス内のメソッドから操作するか、公開用のメソッドを用意してください。";
        return;
    }
    if (contains(message, "親クラスがありません")) {
        *cause = "`親` を使っていますが、このクラスには継承元がありません。";
        *fix = "`型 子 extends 親:` のように親クラスを指定するか、`親` 呼び出しを削除してください。";
        return;
    }
    if (contains(message, "'親' はメソッド内")) {
        *cause = "`親` はインスタンスメソッドの中でだけ使える特別な名前です。";
        *fix = "クラスのメソッド内から呼ぶか、通常のクラス名・インスタンス名を使ってください。";
        return;
    }
    if (contains(message, "文字列補間") && contains(message, "閉じられていません")) {
        *cause = "文字列の中の `{式}` の `}` がありません。";
        *fix = "補間したい式の終わりに `}` を追加してください。文字として `{` を出したい場合は `\\{` と書きます。";
        *example = "表示(\"こんにちは {名前}\")";
        return;
    }
    if (contains(message, "試行文には")) {
        *cause = "`試行:` ブロックの後に、失敗時または最後に実行する節がありません。";
        *fix = "`捕獲 エラー:` または `最終:` のどちらかを追加してください。";
        *example = "試行:\\n    処理()\\n捕獲 エラー:\\n    表示(エラー)\\n終わり";
        return;
    }

    switch (kind) {
        case DIAG_SYNTAX:
            *cause = "はじむの文法として、この並びを読み取れませんでした。";
            *fix = "エラー位置の直前から、括弧、コロン、改行、`終わり` の対応を確認してください。";
            break;
        case DIAG_TYPE:
            *cause = "値の種類が、この操作で期待されている型と合っていません。";
            *fix = "値の型を確認し、必要なら変換するか処理を分けてください。";
            break;
        case DIAG_NAME:
            *cause = "参照した名前が見つかりませんでした。";
            *fix = "定義漏れ、import 漏れ、名前の打ち間違いを確認してください。";
            break;
        case DIAG_INDEX:
            *cause = "配列、文字列、辞書へのアクセスで、位置・キー・対象のどれかが合っていません。";
            *fix = "アクセス対象の型、インデックスの値、辞書キーの有無を確認してください。";
            break;
        default:
            *cause = "実行中に処理を続けられない状態になりました。";
            *fix = "キャレット位置の式に渡している値を確認してください。";
            break;
    }
}

static void diag_print_advice_line(const char *label, const char *text) {
    if (!text || text[0] == '\0') return;
    fprintf(stderr, "   %s%s:%s %s\n", C_GREEN, label, C_RESET, text);
}

static void diag_print_message(const char *message) {
    if (!message) return;

    const char *p = message;
    bool first = true;
    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') p++;

        if (first) {
            fprintf(stderr, "   %s=%s ", C_GRAY, C_RESET);
            first = false;
        } else {
            fprintf(stderr, "     ");
        }

        fwrite(line_start, 1, (size_t)(p - line_start), stderr);
        fputc('\n', stderr);
        if (*p == '\n') p++;
    }
}

// =============================================================================
// 診断出力
// =============================================================================

void diag_report(
    DiagKind  kind,
    const char *filename,
    const char *source,
    int  line,
    int  col,
    int  col_end,
    const char *message
) {
    const char *label = diag_kind_label(kind);
    const char *color = (kind == DIAG_SYNTAX) ? C_RED : C_RED;
    (void)color;
    if (col < 1) col = 1;

    /* ── 見出し行 ──────────────────────────────────────────── */
    /*  例: [構文エラー] --> test.jp:15:8                        */
    fprintf(stderr, "%s%s%s", C_RED, C_BOLD, label);
    fprintf(stderr, "%s", C_RESET);
    if (filename) {
        fprintf(stderr, " %s-->%s %s%s:%d:%d%s\n",
                C_BLUE, C_RESET,
                C_BOLD, filename, line, col, C_RESET);
    } else {
        fprintf(stderr, "\n");
    }

    /* ── ソース行がない場合 ───────────────────────────────── */
    if (!source || line <= 0) {
        diag_print_message(message);
        const char *cause;
        const char *fix;
        const char *example;
        diag_build_advice(kind, message, &cause, &fix, &example);
        diag_print_advice_line("原因", cause);
        diag_print_advice_line("直し方", fix);
        diag_print_advice_line("例", example);
        fputc('\n', stderr);
        return;
    }

    /* ── ソース行を抽出 ───────────────────────────────────── */
    char line_buf[1024];
    int  line_len = diag_extract_line(source, line, line_buf, sizeof(line_buf));
    if (line_len <= 0) {
        diag_print_message(message);
        const char *cause;
        const char *fix;
        const char *example;
        diag_build_advice(kind, message, &cause, &fix, &example);
        diag_print_advice_line("原因", cause);
        diag_print_advice_line("直し方", fix);
        diag_print_advice_line("例", example);
        fputc('\n', stderr);
        return;
    }

    /* 行番号の表示幅を決める (最大5桁) */
    char line_num_str[16];
    snprintf(line_num_str, sizeof(line_num_str), "%d", line);
    int num_width = (int)strlen(line_num_str);
    if (num_width < 2) num_width = 2;

    /* ── 区切り行 ─────────────────────────────────────────── */
    /*    |                                                       */
    fprintf(stderr, "%s%*s |%s\n", C_BLUE, num_width, "", C_RESET);

    /* ── ソース行 ─────────────────────────────────────────── */
    /*  15 |     変数 x = もし 条件 なら                         */
    fprintf(stderr, "%s%*d |%s %s\n",
            C_BLUE, num_width, line, C_RESET, line_buf);

    /* ── キャレット行 ─────────────────────────────────────── */
    /*     |          ^^^^^^^^^^^                                 */
    fprintf(stderr, "%s%*s |%s ", C_BLUE, num_width, "", C_RESET);

    /* col 列目まで空白を出力 (バイト→文字変換を考慮) */
    /* col_end が col より小さければ 1 文字分のみハイライト */
    if (col_end < col) col_end = col;

    /* col 列 (1-based) の前を空白で埋める */
    /* ソース行を先頭から col-1 文字分スキャンしてバイト数を計算 */
    int skip_bytes = 0;
    {
        const char *p = line_buf;
        int char_count = 0;
        while (*p && char_count < col - 1) {
            int clen = utf8_char_length((unsigned char)*p);
            if (clen <= 0) clen = 1;
            /* 日本語全角は表示幅 2 として空白 2 個 */
            int w = (clen > 1) ? 2 : 1;
            for (int i = 0; i < w; i++) fputc(' ', stderr);
            skip_bytes += clen;
            p += clen;
            char_count++;
        }
    }

    /* ハイライト部分の文字数を計算 */
    {
        const char *p = line_buf + skip_bytes;
        int char_count = 0;
        int target     = col_end - col + 1;  /* ハイライト文字数 */
        if (target < 1) target = 1;

        fprintf(stderr, "%s", C_YELLOW);
        while (*p && char_count < target) {
            int clen = utf8_char_length((unsigned char)*p);
            if (clen <= 0) clen = 1;
            int w = (clen > 1) ? 2 : 1;
            for (int i = 0; i < w; i++) fputc('^', stderr);
            p += clen;
            char_count++;
        }
        if (char_count == 0) fprintf(stderr, "^"); /* 最低1文字 */
        fprintf(stderr, "%s", C_RESET);
    }
    fputc('\n', stderr);

    /* ── メッセージ行 ─────────────────────────────────────── */
    /*     = メッセージ本文                                      */
    fprintf(stderr, "%s%*s |%s\n", C_BLUE, num_width, "", C_RESET);
    diag_print_message(message);

    const char *cause;
    const char *fix;
    const char *example;
    diag_build_advice(kind, message, &cause, &fix, &example);
    diag_print_advice_line("原因", cause);
    diag_print_advice_line("直し方", fix);
    diag_print_advice_line("例", example);
    fputc('\n', stderr);
}
