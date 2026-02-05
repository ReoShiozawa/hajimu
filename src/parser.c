/**
 * 日本語プログラミング言語 - 構文解析器実装
 * 
 * 再帰下降パーサによるAST生成
 */

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// =============================================================================
// 内部関数プロトタイプ
// =============================================================================

static void advance(Parser *parser);
static bool check(Parser *parser, TokenType type);
static bool match(Parser *parser, TokenType type);
static void consume(Parser *parser, TokenType type, const char *message);
static void error_at(Parser *parser, Token *token, const char *message);
static void error(Parser *parser, const char *format, ...);
static void synchronize(Parser *parser);

// 文のパース
static ASTNode *declaration(Parser *parser);
static ASTNode *function_definition(Parser *parser);
static ASTNode *statement(Parser *parser);
static ASTNode *var_declaration(Parser *parser, bool is_const);
static ASTNode *if_statement(Parser *parser);
static ASTNode *while_statement(Parser *parser);
static ASTNode *for_statement(Parser *parser);
static ASTNode *return_statement(Parser *parser);
static ASTNode *break_statement(Parser *parser);
static ASTNode *continue_statement(Parser *parser);
static ASTNode *expression_statement(Parser *parser);
static ASTNode *block(Parser *parser);

// 式のパース（優先順位低い順）
static ASTNode *expression(Parser *parser);
static ASTNode *or_expr(Parser *parser);
static ASTNode *and_expr(Parser *parser);
static ASTNode *not_expr(Parser *parser);
static ASTNode *comparison(Parser *parser);
static ASTNode *term(Parser *parser);
static ASTNode *factor(Parser *parser);
static ASTNode *power(Parser *parser);
static ASTNode *unary(Parser *parser);
static ASTNode *call(Parser *parser);
static ASTNode *primary(Parser *parser);

// ヘルパー関数
static ASTNode *finish_call(Parser *parser, ASTNode *callee);
static char *copy_token_string(Token *token);
static Parameter *parse_parameters(Parser *parser, int *count);
static ValueType parse_type(Parser *parser);

// =============================================================================
// 初期化・解放
// =============================================================================

void parser_init(Parser *parser, const char *source, const char *filename) {
    parser->lexer = malloc(sizeof(Lexer));
    lexer_init(parser->lexer, source, filename);
    parser->filename = filename;
    parser->had_error = false;
    parser->panic_mode = false;
    parser->error_message[0] = '\0';
    
    // 最初のトークンを読む
    advance(parser);
}

void parser_free(Parser *parser) {
    if (parser->lexer) {
        free(parser->lexer);
        parser->lexer = NULL;
    }
}

// =============================================================================
// トークン操作
// =============================================================================

static void advance(Parser *parser) {
    parser->previous = parser->current;
    
    for (;;) {
        parser->current = lexer_next(parser->lexer);
        
        if (parser->current.type != TOKEN_ERROR) break;
        
        // エラートークンの場合
        error_at(parser, &parser->current, parser->current.start);
    }
}

static bool check(Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

static void consume(Parser *parser, TokenType type, const char *message) {
    if (parser->current.type == type) {
        advance(parser);
        return;
    }
    
    error(parser, "%s（'%s'が必要です）", message, token_type_name(type));
}

// =============================================================================
// エラー処理
// =============================================================================

static void error_at(Parser *parser, Token *token, const char *message) {
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    parser->had_error = true;
    
    snprintf(parser->error_message, sizeof(parser->error_message),
             "[%d行目] エラー", token->line);
    
    if (token->type == TOKEN_EOF) {
        strcat(parser->error_message, " ファイル終端で");
    } else if (token->type != TOKEN_ERROR) {
        char temp[64];
        snprintf(temp, sizeof(temp), " '%.*s' の付近で", token->length, token->start);
        strcat(parser->error_message, temp);
    }
    
    strcat(parser->error_message, ": ");
    strcat(parser->error_message, message);
    
    fprintf(stderr, "%s\n", parser->error_message);
}

static void error(Parser *parser, const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    char message[256];
    vsnprintf(message, sizeof(message), format, args);
    
    va_end(args);
    
    error_at(parser, &parser->current, message);
}

static void synchronize(Parser *parser) {
    parser->panic_mode = false;
    
    while (parser->current.type != TOKEN_EOF) {
        // 改行は文の区切りになりうる
        if (parser->previous.type == TOKEN_NEWLINE) return;
        
        // 新しい文の開始となるキーワード
        switch (parser->current.type) {
            case TOKEN_FUNCTION:
            case TOKEN_VARIABLE:
            case TOKEN_CONSTANT:
            case TOKEN_IF:
            case TOKEN_WHILE_COND:
            case TOKEN_FOR:
            case TOKEN_RETURN:
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
            case TOKEN_END:
                return;
            default:
                break;
        }
        
        advance(parser);
    }
}

bool parser_had_error(Parser *parser) {
    return parser->had_error;
}

const char *parser_error_message(Parser *parser) {
    return parser->error_message;
}

void parser_clear_error(Parser *parser) {
    parser->had_error = false;
    parser->panic_mode = false;
    parser->error_message[0] = '\0';
}

// =============================================================================
// ヘルパー関数
// =============================================================================

static char *copy_token_string(Token *token) {
    char *str = malloc(token->length + 1);
    memcpy(str, token->start, token->length);
    str[token->length] = '\0';
    return str;
}

static void skip_newlines(Parser *parser) {
    while (match(parser, TOKEN_NEWLINE)) {
        // 空行をスキップ
    }
}

static Parameter *parse_parameters(Parser *parser, int *count) {
    *count = 0;
    
    if (check(parser, TOKEN_RPAREN)) {
        return NULL;
    }
    
    int capacity = 8;
    Parameter *params = malloc(sizeof(Parameter) * capacity);
    
    do {
        if (*count >= capacity) {
            capacity *= 2;
            params = realloc(params, sizeof(Parameter) * capacity);
        }
        
        // パラメータ名
        consume(parser, TOKEN_IDENTIFIER, "パラメータ名が必要です");
        params[*count].name = copy_token_string(&parser->previous);
        params[*count].has_type = false;
        params[*count].type = VALUE_NULL;
        
        // 型注釈（オプション）
        if (match(parser, TOKEN_TYPE_IS)) {
            params[*count].has_type = true;
            params[*count].type = parse_type(parser);
        }
        
        (*count)++;
    } while (match(parser, TOKEN_COMMA));
    
    return params;
}

static ValueType parse_type(Parser *parser) {
    if (match(parser, TOKEN_TYPE_NUMBER)) {
        return VALUE_NUMBER;
    } else if (match(parser, TOKEN_TYPE_STRING_T)) {
        return VALUE_STRING;
    } else if (match(parser, TOKEN_TYPE_BOOL)) {
        return VALUE_BOOL;
    } else if (match(parser, TOKEN_TYPE_ARRAY)) {
        return VALUE_ARRAY;
    }
    
    error(parser, "型名が必要です");
    return VALUE_NULL;
}

// =============================================================================
// プログラムのパース
// =============================================================================

ASTNode *parse_program(Parser *parser) {
    ASTNode *program = node_program(1, 1);
    
    skip_newlines(parser);
    
    while (!check(parser, TOKEN_EOF)) {
        ASTNode *decl = declaration(parser);
        if (decl != NULL) {
            block_add_statement(program, decl);
        }
        
        skip_newlines(parser);
        
        if (parser->panic_mode) {
            synchronize(parser);
        }
    }
    
    return program;
}

// =============================================================================
// 宣言のパース
// =============================================================================

static ASTNode *declaration(Parser *parser) {
    if (match(parser, TOKEN_FUNCTION)) {
        return function_definition(parser);
    }
    
    return statement(parser);
}

static ASTNode *function_definition(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 関数名
    consume(parser, TOKEN_IDENTIFIER, "関数名が必要です");
    char *name = copy_token_string(&parser->previous);
    
    // パラメータリスト
    consume(parser, TOKEN_LPAREN, "'(' が必要です");
    int param_count;
    Parameter *params = parse_parameters(parser, &param_count);
    consume(parser, TOKEN_RPAREN, "')' が必要です");
    
    // 戻り値の型（オプション）
    ValueType return_type = VALUE_NULL;
    bool has_return_type = false;
    if (match(parser, TOKEN_TYPE_IS)) {
        has_return_type = true;
        return_type = parse_type(parser);
    }
    
    // コロン
    consume(parser, TOKEN_COLON, "':' が必要です");
    
    // 関数本体
    ASTNode *body = block(parser);
    
    // 終わり
    consume(parser, TOKEN_END, "'終わり' が必要です");
    
    return node_function_def(name, params, param_count, return_type, has_return_type,
                            body, line, column);
}

// =============================================================================
// 文のパース
// =============================================================================

static ASTNode *statement(Parser *parser) {
    if (match(parser, TOKEN_VARIABLE)) {
        return var_declaration(parser, false);
    }
    if (match(parser, TOKEN_CONSTANT)) {
        return var_declaration(parser, true);
    }
    if (match(parser, TOKEN_IF)) {
        return if_statement(parser);
    }
    if (match(parser, TOKEN_WHILE_COND)) {
        return while_statement(parser);
    }
    if (match(parser, TOKEN_RETURN)) {
        return return_statement(parser);
    }
    if (match(parser, TOKEN_BREAK)) {
        return break_statement(parser);
    }
    if (match(parser, TOKEN_CONTINUE)) {
        return continue_statement(parser);
    }
    
    // for文のチェック（識別子 を ... から ... 繰り返す）
    if (check(parser, TOKEN_IDENTIFIER)) {
        // 先読みして for文かどうかを判定
        // Lexer状態も含めて全て保存する必要がある
        Token saved_current = parser->current;
        Token saved_previous = parser->previous;
        Lexer saved_lexer = *parser->lexer;  // Lexer状態も保存
        
        advance(parser);  // 識別子を消費
        
        if (check(parser, TOKEN_TO)) {
            // for文として解析
            parser->current = saved_current;
            parser->previous = saved_previous;
            *parser->lexer = saved_lexer;  // Lexer状態も復元
            return for_statement(parser);
        }
        
        // for文ではないので戻す
        parser->current = saved_current;
        parser->previous = saved_previous;
        *parser->lexer = saved_lexer;  // Lexer状態も復元
    }
    
    return expression_statement(parser);
}

static ASTNode *var_declaration(Parser *parser, bool is_const) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 変数名
    consume(parser, TOKEN_IDENTIFIER, "変数名が必要です");
    char *name = copy_token_string(&parser->previous);
    
    // 初期化式
    consume(parser, TOKEN_ASSIGN, "'=' が必要です");
    ASTNode *initializer = expression(parser);
    
    // 改行
    if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT) && 
        !check(parser, TOKEN_END)) {
        consume(parser, TOKEN_NEWLINE, "改行が必要です");
    }
    
    return node_var_decl(name, initializer, is_const, line, column);
}

static ASTNode *if_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 条件式
    ASTNode *condition = expression(parser);
    
    // なら
    consume(parser, TOKEN_THEN, "'なら' が必要です");
    
    // then節
    ASTNode *then_branch = block(parser);
    
    // else節（オプション）
    ASTNode *else_branch = NULL;
    if (match(parser, TOKEN_ELSE)) {
        else_branch = block(parser);
    }
    
    // 終わり
    consume(parser, TOKEN_END, "'終わり' が必要です");
    
    return node_if(condition, then_branch, else_branch, line, column);
}

static ASTNode *while_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 条件式
    ASTNode *condition = expression(parser);
    
    // の間
    consume(parser, TOKEN_WHILE_END, "'の間' が必要です");
    
    // ループ本体
    ASTNode *body = block(parser);
    
    // 終わり
    consume(parser, TOKEN_END, "'終わり' が必要です");
    
    return node_while(condition, body, line, column);
}

static ASTNode *for_statement(Parser *parser) {
    int line = parser->current.line;
    int column = parser->current.column;
    
    // ループ変数
    consume(parser, TOKEN_IDENTIFIER, "ループ変数名が必要です");
    char *var_name = copy_token_string(&parser->previous);
    
    // を
    consume(parser, TOKEN_TO, "'を' が必要です");
    
    // 開始値
    ASTNode *start = expression(parser);
    
    // から
    consume(parser, TOKEN_FROM, "'から' が必要です");
    
    // 終了値
    ASTNode *end = expression(parser);
    
    // ステップ値（オプション - 将来拡張用）
    ASTNode *step = NULL;
    
    // 繰り返す
    consume(parser, TOKEN_FOR, "'繰り返す' が必要です");
    
    // ループ本体
    ASTNode *body = block(parser);
    
    // 終わり
    consume(parser, TOKEN_END, "'終わり' が必要です");
    
    return node_for(var_name, start, end, step, body, line, column);
}

static ASTNode *return_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 戻り値（オプション）
    ASTNode *value = NULL;
    if (!check(parser, TOKEN_NEWLINE) && !check(parser, TOKEN_EOF)) {
        value = expression(parser);
    }
    
    // 改行
    if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT)) {
        match(parser, TOKEN_NEWLINE);
    }
    
    return node_return(value, line, column);
}

static ASTNode *break_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT)) {
        match(parser, TOKEN_NEWLINE);
    }
    
    return node_break(line, column);
}

static ASTNode *continue_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT)) {
        match(parser, TOKEN_NEWLINE);
    }
    
    return node_continue(line, column);
}

static ASTNode *expression_statement(Parser *parser) {
    int line = parser->current.line;
    int column = parser->current.column;
    
    ASTNode *expr = expression(parser);
    
    // 代入演算子のチェック
    if (check(parser, TOKEN_ASSIGN) || check(parser, TOKEN_PLUS_ASSIGN) ||
        check(parser, TOKEN_MINUS_ASSIGN) || check(parser, TOKEN_STAR_ASSIGN) ||
        check(parser, TOKEN_SLASH_ASSIGN)) {
        
        TokenType op = parser->current.type;
        advance(parser);
        
        ASTNode *value = expression(parser);
        
        if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT) &&
            !check(parser, TOKEN_END)) {
            match(parser, TOKEN_NEWLINE);
        }
        
        return node_assign(expr, op, value, line, column);
    }
    
    // 改行
    if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT) &&
        !check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE)) {
        match(parser, TOKEN_NEWLINE);
    }
    
    return node_expr_stmt(expr, line, column);
}

static ASTNode *block(Parser *parser) {
    int line = parser->current.line;
    int column = parser->current.column;
    
    ASTNode *blk = node_block(line, column);
    
    // 改行を期待
    if (!match(parser, TOKEN_NEWLINE)) {
        // インラインブロック（単一の文）
        ASTNode *stmt = statement(parser);
        block_add_statement(blk, stmt);
        return blk;
    }
    
    // INDENTを期待
    if (!match(parser, TOKEN_INDENT)) {
        // 空のブロック
        return blk;
    }
    
    // 文を読み込む
    while (!check(parser, TOKEN_DEDENT) && !check(parser, TOKEN_EOF) &&
           !check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE)) {
        
        skip_newlines(parser);
        
        if (check(parser, TOKEN_DEDENT) || check(parser, TOKEN_EOF) ||
            check(parser, TOKEN_END) || check(parser, TOKEN_ELSE)) {
            break;
        }
        
        ASTNode *stmt = statement(parser);
        if (stmt != NULL) {
            block_add_statement(blk, stmt);
        }
        
        if (parser->panic_mode) {
            synchronize(parser);
        }
    }
    
    // DEDENT
    match(parser, TOKEN_DEDENT);
    
    return blk;
}

// =============================================================================
// 式のパース
// =============================================================================

ASTNode *parse_expression(Parser *parser) {
    return expression(parser);
}

static ASTNode *expression(Parser *parser) {
    return or_expr(parser);
}

// または
static ASTNode *or_expr(Parser *parser) {
    ASTNode *left = and_expr(parser);
    
    while (match(parser, TOKEN_OR)) {
        int line = parser->previous.line;
        int column = parser->previous.column;
        ASTNode *right = and_expr(parser);
        left = node_binary(TOKEN_OR, left, right, line, column);
    }
    
    return left;
}

// かつ
static ASTNode *and_expr(Parser *parser) {
    ASTNode *left = not_expr(parser);
    
    while (match(parser, TOKEN_AND)) {
        int line = parser->previous.line;
        int column = parser->previous.column;
        ASTNode *right = not_expr(parser);
        left = node_binary(TOKEN_AND, left, right, line, column);
    }
    
    return left;
}

// でない
static ASTNode *not_expr(Parser *parser) {
    if (match(parser, TOKEN_NOT)) {
        int line = parser->previous.line;
        int column = parser->previous.column;
        ASTNode *operand = not_expr(parser);
        return node_unary(TOKEN_NOT, operand, line, column);
    }
    
    return comparison(parser);
}

// 比較演算子
static ASTNode *comparison(Parser *parser) {
    ASTNode *left = term(parser);
    
    while (check(parser, TOKEN_EQ) || check(parser, TOKEN_NE) ||
           check(parser, TOKEN_LT) || check(parser, TOKEN_LE) ||
           check(parser, TOKEN_GT) || check(parser, TOKEN_GE)) {
        
        TokenType op = parser->current.type;
        int line = parser->current.line;
        int column = parser->current.column;
        advance(parser);
        
        ASTNode *right = term(parser);
        left = node_binary(op, left, right, line, column);
    }
    
    return left;
}

// 加減算
static ASTNode *term(Parser *parser) {
    ASTNode *left = factor(parser);
    
    while (check(parser, TOKEN_PLUS) || check(parser, TOKEN_MINUS)) {
        TokenType op = parser->current.type;
        int line = parser->current.line;
        int column = parser->current.column;
        advance(parser);
        
        ASTNode *right = factor(parser);
        left = node_binary(op, left, right, line, column);
    }
    
    return left;
}

// 乗除算
static ASTNode *factor(Parser *parser) {
    ASTNode *left = power(parser);
    
    while (check(parser, TOKEN_STAR) || check(parser, TOKEN_SLASH) ||
           check(parser, TOKEN_PERCENT)) {
        TokenType op = parser->current.type;
        int line = parser->current.line;
        int column = parser->current.column;
        advance(parser);
        
        ASTNode *right = power(parser);
        left = node_binary(op, left, right, line, column);
    }
    
    return left;
}

// べき乗（右結合）
static ASTNode *power(Parser *parser) {
    ASTNode *left = unary(parser);
    
    if (match(parser, TOKEN_POWER)) {
        int line = parser->previous.line;
        int column = parser->previous.column;
        ASTNode *right = power(parser);  // 右結合のため再帰
        return node_binary(TOKEN_POWER, left, right, line, column);
    }
    
    return left;
}

// 単項演算子
static ASTNode *unary(Parser *parser) {
    if (match(parser, TOKEN_MINUS)) {
        int line = parser->previous.line;
        int column = parser->previous.column;
        ASTNode *operand = unary(parser);
        return node_unary(TOKEN_MINUS, operand, line, column);
    }
    
    return call(parser);
}

// 関数呼び出し・インデックスアクセス
static ASTNode *call(Parser *parser) {
    ASTNode *expr = primary(parser);
    
    while (true) {
        if (match(parser, TOKEN_LPAREN)) {
            expr = finish_call(parser, expr);
        } else if (match(parser, TOKEN_LBRACKET)) {
            int line = parser->previous.line;
            int column = parser->previous.column;
            ASTNode *index = expression(parser);
            consume(parser, TOKEN_RBRACKET, "']' が必要です");
            expr = node_index(expr, index, line, column);
        } else {
            break;
        }
    }
    
    return expr;
}

static ASTNode *finish_call(Parser *parser, ASTNode *callee) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 引数をパース
    int capacity = 8;
    int arg_count = 0;
    ASTNode **args = malloc(sizeof(ASTNode *) * capacity);
    
    if (!check(parser, TOKEN_RPAREN)) {
        do {
            if (arg_count >= capacity) {
                capacity *= 2;
                args = realloc(args, sizeof(ASTNode *) * capacity);
            }
            args[arg_count++] = expression(parser);
        } while (match(parser, TOKEN_COMMA));
    }
    
    consume(parser, TOKEN_RPAREN, "')' が必要です");
    
    // 配列をぴったりサイズにする
    if (arg_count > 0) {
        args = realloc(args, sizeof(ASTNode *) * arg_count);
    } else {
        free(args);
        args = NULL;
    }
    
    return node_call(callee, args, arg_count, line, column);
}

// 基本式
static ASTNode *primary(Parser *parser) {
    int line = parser->current.line;
    int column = parser->current.column;
    
    // 数値
    if (match(parser, TOKEN_NUMBER)) {
        return node_number(parser->previous.value.number, line, column);
    }
    
    // 文字列
    if (match(parser, TOKEN_STRING)) {
        ASTNode *node = node_string(parser->previous.value.string, line, column);
        // 文字列メモリの所有権を移動したので、lexerで確保されたメモリは解放しない
        // （node_string内でコピーされる）
        free(parser->previous.value.string);
        return node;
    }
    
    // 真偽値
    if (match(parser, TOKEN_TRUE)) {
        return node_bool(true, line, column);
    }
    if (match(parser, TOKEN_FALSE)) {
        return node_bool(false, line, column);
    }
    
    // 識別子
    if (match(parser, TOKEN_IDENTIFIER)) {
        return node_identifier(copy_token_string(&parser->previous), line, column);
    }
    
    // グループ化 (...)
    if (match(parser, TOKEN_LPAREN)) {
        ASTNode *expr = expression(parser);
        consume(parser, TOKEN_RPAREN, "')' が必要です");
        return expr;
    }
    
    // 配列リテラル [...]
    if (match(parser, TOKEN_LBRACKET)) {
        int capacity = 8;
        int count = 0;
        ASTNode **elements = malloc(sizeof(ASTNode *) * capacity);
        
        if (!check(parser, TOKEN_RBRACKET)) {
            do {
                if (count >= capacity) {
                    capacity *= 2;
                    elements = realloc(elements, sizeof(ASTNode *) * capacity);
                }
                elements[count++] = expression(parser);
            } while (match(parser, TOKEN_COMMA));
        }
        
        consume(parser, TOKEN_RBRACKET, "']' が必要です");
        
        if (count > 0) {
            elements = realloc(elements, sizeof(ASTNode *) * count);
        } else {
            free(elements);
            elements = NULL;
        }
        
        return node_array(elements, count, line, column);
    }
    
    // 辞書リテラル {...}
    if (match(parser, TOKEN_LBRACE)) {
        int capacity = 8;
        int count = 0;
        char **keys = malloc(sizeof(char *) * capacity);
        ASTNode **values = malloc(sizeof(ASTNode *) * capacity);
        
        if (!check(parser, TOKEN_RBRACE)) {
            do {
                if (count >= capacity) {
                    capacity *= 2;
                    keys = realloc(keys, sizeof(char *) * capacity);
                    values = realloc(values, sizeof(ASTNode *) * capacity);
                }
                
                // キー（文字列または識別子）
                if (check(parser, TOKEN_STRING)) {
                    advance(parser);
                    keys[count] = parser->previous.value.string;
                } else if (check(parser, TOKEN_IDENTIFIER)) {
                    advance(parser);
                    keys[count] = copy_token_string(&parser->previous);
                } else {
                    error(parser, "辞書のキーは文字列または識別子でなければなりません");
                    free(keys);
                    free(values);
                    return node_null(line, column);
                }
                
                consume(parser, TOKEN_COLON, "':' が必要です");
                values[count] = expression(parser);
                count++;
            } while (match(parser, TOKEN_COMMA));
        }
        
        consume(parser, TOKEN_RBRACE, "'}' が必要です");
        
        if (count > 0) {
            keys = realloc(keys, sizeof(char *) * count);
            values = realloc(values, sizeof(ASTNode *) * count);
        } else {
            free(keys);
            free(values);
            keys = NULL;
            values = NULL;
        }
        
        return node_dict(keys, values, count, line, column);
    }
    
    error(parser, "式が必要です");
    return node_null(line, column);
}

// =============================================================================
// 文のパース（公開）
// =============================================================================

ASTNode *parse_statement(Parser *parser) {
    return statement(parser);
}
