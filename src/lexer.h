/**
 * 日本語プログラミング言語 - 字句解析器ヘッダー
 * 
 * UTF-8対応のトークナイザ
 */

#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// トークン種別
// =============================================================================

typedef enum {
    // 特殊トークン
    TOKEN_EOF = 0,          // ファイル終端
    TOKEN_NEWLINE,          // 改行
    TOKEN_INDENT,           // インデント増加
    TOKEN_DEDENT,           // インデント減少
    TOKEN_ERROR,            // エラートークン
    
    // リテラル
    TOKEN_NUMBER,           // 数値: 123, 3.14
    TOKEN_STRING,           // 文字列: "こんにちは"
    TOKEN_IDENTIFIER,       // 識別子: 変数名、関数名
    
    // キーワード - 関数定義
    TOKEN_FUNCTION,         // 関数
    TOKEN_END,              // 終わり
    TOKEN_RETURN,           // 戻す
    
    // キーワード - 変数
    TOKEN_VARIABLE,         // 変数
    TOKEN_CONSTANT,         // 定数
    
    // キーワード - 条件分岐
    TOKEN_IF,               // もし
    TOKEN_ELSE,             // それ以外
    TOKEN_ELSE_IF,          // それ以外もし
    TOKEN_THEN,             // なら
    
    // キーワード - 繰り返し
    TOKEN_WHILE_COND,       // 条件
    TOKEN_WHILE_END,        // の間
    TOKEN_FOR,              // 繰り返す
    TOKEN_FROM,             // から
    TOKEN_TO,               // を
    
    // キーワード - 制御
    TOKEN_BREAK,            // 抜ける
    TOKEN_CONTINUE,         // 続ける
    TOKEN_IMPORT,           // 取り込む
    
    // キーワード - クラス/OOP
    TOKEN_CLASS,            // 型
    TOKEN_NEW,              // 新規
    TOKEN_EXTENDS,          // 継承
    TOKEN_SELF,             // 自分
    TOKEN_INIT,             // 初期化
    TOKEN_SUPER,            // 親
    
    // キーワード - 例外処理
    TOKEN_TRY,              // 試行
    TOKEN_CATCH,            // 捕獲
    TOKEN_FINALLY,          // 最終
    TOKEN_THROW,            // 投げる
    
    // キーワード - ジェネレータ
    TOKEN_GENERATOR_FUNC,   // 生成関数
    TOKEN_YIELD,            // 譲渡
    
    // キーワード - 列挙型
    TOKEN_ENUM,             // 列挙
    
    // キーワード - パターンマッチ
    TOKEN_MATCH,            // 照合
    TOKEN_ARROW,            // =>
    TOKEN_STATIC,           // 静的
    
    // キーワード - 選択文
    TOKEN_SWITCH,           // 選択
    TOKEN_CASE,             // 場合
    TOKEN_DEFAULT,          // 既定
    
    // キーワード - foreach
    TOKEN_EACH,             // 各
    TOKEN_IN,               // の中
    
    // キーワード - 真偽値
    TOKEN_TRUE,             // 真
    TOKEN_FALSE,            // 偽
    TOKEN_NULL_LITERAL,     // 無
    
    // キーワード - 論理演算
    TOKEN_AND,              // かつ
    TOKEN_OR,               // または
    TOKEN_NOT,              // でない
    
    // キーワード - 型
    TOKEN_TYPE_IS,          // は
    TOKEN_TYPE_NUMBER,      // 数値（型として）
    TOKEN_TYPE_STRING_T,    // 文字列（型として）
    TOKEN_TYPE_BOOL,        // 真偽
    TOKEN_TYPE_ARRAY,       // 配列
    
    // 演算子 - 算術
    TOKEN_PLUS,             // +
    TOKEN_MINUS,            // -
    TOKEN_STAR,             // *
    TOKEN_SLASH,            // /
    TOKEN_PERCENT,          // %
    TOKEN_POWER,            // **
    
    // 演算子 - 比較
    TOKEN_EQ,               // ==
    TOKEN_NE,               // !=
    TOKEN_LT,               // <
    TOKEN_LE,               // <=
    TOKEN_GT,               // >
    TOKEN_GE,               // >=
    
    // 演算子 - 代入
    TOKEN_ASSIGN,           // =
    TOKEN_PLUS_ASSIGN,      // +=
    TOKEN_MINUS_ASSIGN,     // -=
    TOKEN_STAR_ASSIGN,      // *=
    TOKEN_SLASH_ASSIGN,     // /=
    TOKEN_PERCENT_ASSIGN,   // %=
    TOKEN_POWER_ASSIGN,     // **=
    
    // 区切り記号
    TOKEN_LPAREN,           // (
    TOKEN_RPAREN,           // )
    TOKEN_LBRACKET,         // [
    TOKEN_RBRACKET,         // ]
    TOKEN_LBRACE,           // {
    TOKEN_RBRACE,           // }
    TOKEN_COMMA,            // ,
    TOKEN_COLON,            // :
    TOKEN_DOT,              // .
    TOKEN_SPREAD,           // ...
    TOKEN_PIPE,             // |>
    TOKEN_QUESTION,         // ?
    TOKEN_NULL_COALESCE,    // ??
    
    TOKEN_COUNT             // トークン種別の数
} TokenType;

// =============================================================================
// トークン構造体
// =============================================================================

typedef struct {
    TokenType type;         // トークンの種類
    const char *start;      // ソース内の開始位置
    int length;             // トークンの長さ（バイト数）
    int line;               // 行番号（1始まり）
    int column;             // 列番号（1始まり）
    
    // リテラル値（該当する場合）
    union {
        double number;      // 数値リテラルの値
        char *string;       // 文字列リテラルの値（エスケープ処理済み）
    } value;
} Token;

// =============================================================================
// Lexer構造体
// =============================================================================

#define MAX_INDENT_DEPTH 64

typedef struct {
    const char *source;         // ソースコード全体
    const char *start;          // 現在のトークンの開始位置
    const char *current;        // 現在のスキャン位置
    const char *filename;       // ファイル名（エラー報告用）
    
    int line;                   // 現在の行番号
    int column;                 // 現在の列番号
    int token_start_column;     // トークン開始時の列番号
    
    // インデント管理
    int indent_stack[MAX_INDENT_DEPTH];  // インデントレベルのスタック
    int indent_top;                       // スタックのトップインデックス
    int pending_dedents;                  // 発行待ちのDEDENTの数
    bool at_line_start;                   // 行頭にいるか
    int current_indent;                   // 現在行のインデントレベル
    
    // エラー情報
    bool had_error;
    char error_message[256];
} Lexer;

// =============================================================================
// 公開関数
// =============================================================================

/**
 * Lexerを初期化
 * @param lexer 初期化するLexer
 * @param source ソースコード（UTF-8）
 * @param filename ファイル名（エラー報告用）
 */
void lexer_init(Lexer *lexer, const char *source, const char *filename);

/**
 * 次のトークンを取得
 * @param lexer Lexer
 * @return 次のトークン
 */
Token lexer_next(Lexer *lexer);

/**
 * 現在のトークンを覗き見（消費しない）
 * @param lexer Lexer
 * @return 現在のトークン
 */
Token lexer_peek(Lexer *lexer);

/**
 * トークン種別の名前を取得
 * @param type トークン種別
 * @return 名前の文字列
 */
const char *token_type_name(TokenType type);

/**
 * トークンの文字列表現を取得
 * @param token トークン
 * @param buffer 出力バッファ
 * @param size バッファサイズ
 */
void token_to_string(Token token, char *buffer, size_t size);

// =============================================================================
// UTF-8ユーティリティ
// =============================================================================

/**
 * UTF-8文字のバイト数を取得
 * @param c 先頭バイト
 * @return バイト数（1-4）、無効な場合は0
 */
int utf8_char_length(unsigned char c);

/**
 * UTF-8をUnicodeコードポイントにデコード
 * @param s UTF-8文字列
 * @param len デコードしたバイト数を格納
 * @return Unicodeコードポイント
 */
uint32_t utf8_decode(const char *s, int *len);

/**
 * 日本語文字かどうか判定
 * @param codepoint Unicodeコードポイント
 * @return 日本語ならtrue
 */
bool is_japanese_char(uint32_t codepoint);

/**
 * 識別子の開始文字として有効か
 * @param codepoint Unicodeコードポイント
 * @return 有効ならtrue
 */
bool is_identifier_start(uint32_t codepoint);

/**
 * 識別子の文字として有効か
 * @param codepoint Unicodeコードポイント
 * @return 有効ならtrue
 */
bool is_identifier_char(uint32_t codepoint);

#endif // LEXER_H
