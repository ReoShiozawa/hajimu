/**
 * 日本語プログラミング言語 - 構文解析器ヘッダー
 * 
 * 再帰下降パーサによるAST生成
 */

#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"
#include <stdbool.h>

// =============================================================================
// パーサ構造体
// =============================================================================

typedef struct {
    Lexer *lexer;           // 字句解析器
    Token current;          // 現在のトークン
    Token previous;         // 前のトークン
    bool had_error;         // エラーが発生したか
    bool panic_mode;        // パニックモード（エラー回復中）
    char error_message[512]; // エラーメッセージ
    const char *filename;   // ファイル名
} Parser;

// =============================================================================
// パーサの初期化・解放
// =============================================================================

/**
 * パーサを初期化
 * @param parser パーサ
 * @param source ソースコード
 * @param filename ファイル名
 */
void parser_init(Parser *parser, const char *source, const char *filename);

/**
 * パーサを解放
 * @param parser パーサ
 */
void parser_free(Parser *parser);

// =============================================================================
// パース関数
// =============================================================================

/**
 * プログラム全体をパース
 * @param parser パーサ
 * @return ASTのルートノード（エラー時はNULL）
 */
ASTNode *parse_program(Parser *parser);

/**
 * 単一の式をパース
 * @param parser パーサ
 * @return 式のASTノード
 */
ASTNode *parse_expression(Parser *parser);

/**
 * 単一の文をパース
 * @param parser パーサ
 * @return 文のASTノード
 */
ASTNode *parse_statement(Parser *parser);

// =============================================================================
// エラー処理
// =============================================================================

/**
 * パースエラーが発生したか
 * @param parser パーサ
 * @return エラーがあればtrue
 */
bool parser_had_error(Parser *parser);

/**
 * エラーメッセージを取得
 * @param parser パーサ
 * @return エラーメッセージ
 */
const char *parser_error_message(Parser *parser);

/**
 * エラーをクリア
 * @param parser パーサ
 */
void parser_clear_error(Parser *parser);

#endif // PARSER_H
