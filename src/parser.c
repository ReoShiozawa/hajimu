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
static ASTNode *class_definition(Parser *parser);
static ASTNode *statement(Parser *parser);
static ASTNode *var_declaration(Parser *parser, bool is_const);
static ASTNode *if_statement(Parser *parser);
static ASTNode *while_statement(Parser *parser);
static ASTNode *for_statement(Parser *parser);
static ASTNode *return_statement(Parser *parser);
static ASTNode *break_statement(Parser *parser);
static ASTNode *continue_statement(Parser *parser);
static ASTNode *import_statement(Parser *parser);
static ASTNode *try_statement(Parser *parser);
static ASTNode *throw_statement(Parser *parser);
static ASTNode *switch_statement(Parser *parser);
static ASTNode *foreach_statement(Parser *parser);
static ASTNode *expression_statement(Parser *parser);
static ASTNode *block(Parser *parser);

// 式のパース（優先順位低い順）
static ASTNode *expression(Parser *parser);
static ASTNode *pipe_expr(Parser *parser);
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
        params[*count].default_value = NULL;
        
        // 型注釈（オプション）
        if (match(parser, TOKEN_TYPE_IS)) {
            params[*count].has_type = true;
            params[*count].type = parse_type(parser);
        }
        
        // デフォルト値（オプション）
        if (match(parser, TOKEN_ASSIGN)) {
            params[*count].default_value = expression(parser);
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
        // トップレベルで '終わり' が出現した場合はスキップ
        if (check(parser, TOKEN_END)) {
            error(parser, "対応する開始文のない '終わり' です");
            advance(parser);
            parser->panic_mode = false;
            skip_newlines(parser);
            continue;
        }
        
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
    if (check(parser, TOKEN_FUNCTION)) {
        // 関数定義 vs ラムダ式の判定
        // 関数 名前(...) は関数定義、関数(...) はラムダ式
        Token saved_current = parser->current;
        Token saved_previous = parser->previous;
        Lexer saved_lexer = *parser->lexer;
        
        advance(parser);  // TOKEN_FUNCTIONを消費
        
        if (check(parser, TOKEN_IDENTIFIER)) {
            // 関数定義
            return function_definition(parser);
        }
        
        // ラムダ式 → パーサー状態を戻して式文として処理
        parser->current = saved_current;
        parser->previous = saved_previous;
        *parser->lexer = saved_lexer;
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
    if (match(parser, TOKEN_IMPORT)) {
        return import_statement(parser);
    }
    if (match(parser, TOKEN_CLASS)) {
        return class_definition(parser);
    }
    if (match(parser, TOKEN_TRY)) {
        return try_statement(parser);
    }
    if (match(parser, TOKEN_THROW)) {
        return throw_statement(parser);
    }
    if (match(parser, TOKEN_SWITCH)) {
        return switch_statement(parser);
    }
    if (match(parser, TOKEN_EACH)) {
        return foreach_statement(parser);
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
    if (match(parser, TOKEN_ELSE_IF)) {
        // else if（それ以外もし）: 再帰的にif文をパース
        else_branch = if_statement(parser);
        // else if チェーンの場合、終わりは最後のifで消費される
        return node_if(condition, then_branch, else_branch, line, column);
    } else if (match(parser, TOKEN_ELSE)) {
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

static ASTNode *import_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // ファイルパス（文字列）
    consume(parser, TOKEN_STRING, "取り込むファイルパスが必要です");
    char *module_path = parser->previous.value.string;
    
    if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT)) {
        match(parser, TOKEN_NEWLINE);
    }
    
    return node_import(module_path, line, column);
}

// 試行文（try-catch-finally）のパース
static ASTNode *try_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 試行: の後にコロン
    consume(parser, TOKEN_COLON, "':' が必要です");
    
    // 試行ブロック
    ASTNode *try_block = block(parser);
    
    char *catch_var = NULL;
    ASTNode *catch_block = NULL;
    ASTNode *finally_block = NULL;
    
    // 捕獲句（オプション）
    if (match(parser, TOKEN_CATCH)) {
        // 捕獲 変数名:
        consume(parser, TOKEN_IDENTIFIER, "エラー変数名が必要です");
        catch_var = copy_token_string(&parser->previous);
        consume(parser, TOKEN_COLON, "':' が必要です");
        
        catch_block = block(parser);
    }
    
    // 最終句（オプション）
    if (match(parser, TOKEN_FINALLY)) {
        consume(parser, TOKEN_COLON, "':' が必要です");
        
        finally_block = block(parser);
    }
    
    // 試行-捕獲-最終のいずれか終了後に"終わり"
    consume(parser, TOKEN_END, "'終わり' が必要です");
    
    // 少なくとも捕獲か最終のどちらかが必要
    if (catch_block == NULL && finally_block == NULL) {
        error(parser, "試行文には '捕獲' または '最終' が必要です");
    }
    
    return node_try(try_block, catch_var, catch_block, finally_block, line, column);
}

// 投げる文のパース
static ASTNode *throw_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 投げる 式
    ASTNode *expr = expression(parser);
    
    if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT) &&
        !check(parser, TOKEN_END) && !check(parser, TOKEN_CATCH) &&
        !check(parser, TOKEN_FINALLY)) {
        consume(parser, TOKEN_NEWLINE, "改行が必要です");
    }
    
    return node_throw(expr, line, column);
}

// 選択文（switch）のパース
// 選択 式:
//   場合 値:
//     文...
//   場合 値:
//     文...
//   既定:
//     文...
// 終わり
static ASTNode *switch_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // 選択対象の式
    ASTNode *target = expression(parser);
    
    // コロン
    consume(parser, TOKEN_COLON, "':' が必要です");
    
    ASTNode *node = node_switch(target, line, column);
    
    // 改行とインデント
    skip_newlines(parser);
    match(parser, TOKEN_INDENT);
    
    // 場合句を読み込む
    while (match(parser, TOKEN_CASE)) {
        // 場合の値
        ASTNode *case_value = expression(parser);
        
        // コロン
        consume(parser, TOKEN_COLON, "':' が必要です");
        
        // 場合の本体
        ASTNode *case_body = block(parser);
        
        switch_add_case(node, case_value, case_body);
    }
    
    // 既定句（オプション）
    if (match(parser, TOKEN_DEFAULT)) {
        consume(parser, TOKEN_COLON, "':' が必要です");
        node->switch_stmt.default_body = block(parser);
    }
    
    // デデントと終わり
    match(parser, TOKEN_DEDENT);
    consume(parser, TOKEN_END, "'終わり' が必要です");
    
    return node;
}

// 各要素ループ（foreach）のパース
// 各 変数名 を 配列式 の中:
//   文...
// 終わり
static ASTNode *foreach_statement(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // ループ変数名
    consume(parser, TOKEN_IDENTIFIER, "ループ変数名が必要です");
    char *var_name = copy_token_string(&parser->previous);
    
    // を
    consume(parser, TOKEN_TO, "'を' が必要です");
    
    // 反復対象の式
    ASTNode *iterable = expression(parser);
    
    // の中
    consume(parser, TOKEN_IN, "'の中' が必要です");
    
    // コロン
    consume(parser, TOKEN_COLON, "':' が必要です");
    
    // ループ本体
    ASTNode *body = block(parser);
    
    // 終わり
    consume(parser, TOKEN_END, "'終わり' が必要です");
    
    return node_foreach(var_name, iterable, body, line, column);
}

// メソッド定義のパース（クラス内で使用）
static ASTNode *method_definition(Parser *parser, bool is_init) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // メソッド名を取得（初期化の場合は"初期化"、関数の場合は識別子）
    char *name;
    if (is_init) {
        name = strdup("初期化");
    } else {
        consume(parser, TOKEN_IDENTIFIER, "メソッド名が必要です");
        name = copy_token_string(&parser->previous);
    }
    
    ASTNode *method = node_method_def(name, line, column);
    free(name);
    
    // パラメータリスト
    consume(parser, TOKEN_LPAREN, "'(' が必要です");
    
    if (!check(parser, TOKEN_RPAREN)) {
        do {
            consume(parser, TOKEN_IDENTIFIER, "パラメータ名が必要です");
            char *param_name = copy_token_string(&parser->previous);
            
            ValueType param_type = VALUE_NULL;
            bool has_type = false;
            
            if (match(parser, TOKEN_TYPE_IS)) {
                has_type = true;
                if (match(parser, TOKEN_TYPE_NUMBER)) {
                    param_type = VALUE_NUMBER;
                } else if (match(parser, TOKEN_TYPE_STRING_T)) {
                    param_type = VALUE_STRING;
                } else if (match(parser, TOKEN_TYPE_BOOL)) {
                    param_type = VALUE_BOOL;
                } else if (match(parser, TOKEN_TYPE_ARRAY)) {
                    param_type = VALUE_ARRAY;
                } else {
                    error(parser, "型が必要です");
                }
            }
            
            method_add_param(method, param_name, param_type, has_type);
            free(param_name);
        } while (match(parser, TOKEN_COMMA));
    }
    
    consume(parser, TOKEN_RPAREN, "')' が必要です");
    
    // 戻り値の型（オプション）
    if (match(parser, TOKEN_TYPE_IS)) {
        method->method.has_return_type = true;
        if (match(parser, TOKEN_TYPE_NUMBER)) {
            method->method.return_type = VALUE_NUMBER;
        } else if (match(parser, TOKEN_TYPE_STRING_T)) {
            method->method.return_type = VALUE_STRING;
        } else if (match(parser, TOKEN_TYPE_BOOL)) {
            method->method.return_type = VALUE_BOOL;
        } else if (match(parser, TOKEN_TYPE_ARRAY)) {
            method->method.return_type = VALUE_ARRAY;
        } else {
            error(parser, "戻り値の型が必要です");
        }
    }
    
    // コロン
    consume(parser, TOKEN_COLON, "':' が必要です");
    
    // メソッド本体
    method->method.body = block(parser);
    
    // 終わり
    consume(parser, TOKEN_END, "'終わり' が必要です");
    match(parser, TOKEN_NEWLINE);
    
    return method;
}

static ASTNode *class_definition(Parser *parser) {
    int line = parser->previous.line;
    int column = parser->previous.column;
    
    // クラス名
    consume(parser, TOKEN_IDENTIFIER, "クラス名が必要です");
    char *class_name = copy_token_string(&parser->previous);
    
    // 継承（オプション）
    char *parent_name = NULL;
    if (match(parser, TOKEN_EXTENDS)) {
        consume(parser, TOKEN_IDENTIFIER, "親クラス名が必要です");
        parent_name = copy_token_string(&parser->previous);
    }
    
    ASTNode *class_node = node_class_def(class_name, parent_name, line, column);
    free(class_name);
    if (parent_name) free(parent_name);
    
    // コロン
    consume(parser, TOKEN_COLON, "':' が必要です");
    match(parser, TOKEN_NEWLINE);
    
    // インデント
    consume(parser, TOKEN_INDENT, "クラス本体のインデントが必要です");
    
    // クラス本体（メソッド定義）
    while (!check(parser, TOKEN_END) && !check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT)) {
        if (match(parser, TOKEN_INIT)) {
            // 初期化メソッド
            ASTNode *init = method_definition(parser, true);
            class_node->class_def.init_method = init;
        } else if (match(parser, TOKEN_FUNCTION)) {
            // 通常のメソッド
            ASTNode *method = method_definition(parser, false);
            class_add_method(class_node, method);
        } else if (match(parser, TOKEN_NEWLINE)) {
            // 空行をスキップ
            continue;
        } else {
            error(parser, "クラス内では 関数 または 初期化 のみ定義できます");
            advance(parser);
        }
    }
    
    // デデントとクラス終了の処理
    if (match(parser, TOKEN_DEDENT)) {
        // DEDENTの後に 終わり があればそれも消費
        match(parser, TOKEN_END);
    } else {
        // インデントなしで 終わり で終わる場合
        consume(parser, TOKEN_END, "'終わり' が必要です");
    }
    
    if (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_DEDENT)) {
        match(parser, TOKEN_NEWLINE);
    }
    
    return class_node;
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
           !check(parser, TOKEN_END) && !check(parser, TOKEN_ELSE) &&
           !check(parser, TOKEN_ELSE_IF) &&
           !check(parser, TOKEN_CATCH) && !check(parser, TOKEN_FINALLY) &&
           !check(parser, TOKEN_CASE) && !check(parser, TOKEN_DEFAULT)) {
        
        skip_newlines(parser);
        
        if (check(parser, TOKEN_DEDENT) || check(parser, TOKEN_EOF) ||
            check(parser, TOKEN_END) || check(parser, TOKEN_ELSE) ||
            check(parser, TOKEN_ELSE_IF) ||
            check(parser, TOKEN_CATCH) || check(parser, TOKEN_FINALLY) ||
            check(parser, TOKEN_CASE) || check(parser, TOKEN_DEFAULT)) {
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
    return pipe_expr(parser);
}

// パイプ演算子: 式 |> 関数
static ASTNode *pipe_expr(Parser *parser) {
    ASTNode *left = or_expr(parser);
    
    while (match(parser, TOKEN_PIPE)) {
        int line = parser->previous.line;
        int column = parser->previous.column;
        // 右辺は関数（呼び出し対象）
        ASTNode *func = or_expr(parser);
        // left |> func → func(left) に変換
        ASTNode **args = malloc(sizeof(ASTNode*));
        args[0] = left;
        left = node_call(func, args, 1, line, column);
    }
    
    return left;
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
        } else if (match(parser, TOKEN_DOT)) {
            // メンバーアクセス
            int line = parser->previous.line;
            int column = parser->previous.column;
            consume(parser, TOKEN_IDENTIFIER, "メンバー名が必要です");
            char *member_name = copy_token_string(&parser->previous);
            expr = node_member(expr, member_name, line, column);
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
    
    // 無（null）
    if (match(parser, TOKEN_NULL_LITERAL)) {
        return node_null(line, column);
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
    
    // 新規 クラス名(引数)
    if (match(parser, TOKEN_NEW)) {
        consume(parser, TOKEN_IDENTIFIER, "クラス名が必要です");
        char *class_name = copy_token_string(&parser->previous);
        
        ASTNode *new_node = node_new_expr(class_name, line, column);
        free(class_name);
        
        // 引数リスト
        consume(parser, TOKEN_LPAREN, "'(' が必要です");
        
        int capacity = 8;
        int count = 0;
        ASTNode **args = malloc(sizeof(ASTNode *) * capacity);
        
        if (!check(parser, TOKEN_RPAREN)) {
            do {
                if (count >= capacity) {
                    capacity *= 2;
                    args = realloc(args, sizeof(ASTNode *) * capacity);
                }
                args[count++] = expression(parser);
            } while (match(parser, TOKEN_COMMA));
        }
        
        consume(parser, TOKEN_RPAREN, "')' が必要です");
        
        new_node->new_expr.arguments = args;
        new_node->new_expr.arg_count = count;
        
        return new_node;
    }
    
    // 自分
    if (match(parser, TOKEN_SELF)) {
        return node_self(line, column);
    }
    
    // 無名関数（ラムダ）: 関数(引数): 本体 終わり
    if (match(parser, TOKEN_FUNCTION)) {
        // パラメータリスト
        consume(parser, TOKEN_LPAREN, "'(' が必要です");
        int param_count;
        Parameter *params = parse_parameters(parser, &param_count);
        consume(parser, TOKEN_RPAREN, "')' が必要です");
        
        // コロン
        consume(parser, TOKEN_COLON, "':' が必要です");
        
        // 本体
        ASTNode *body = block(parser);
        
        // 終わり
        consume(parser, TOKEN_END, "'終わり' が必要です");
        
        return node_lambda(params, param_count, body, line, column);
    }
    
    error(parser, "式が必要です");
    advance(parser);  // エラー回復: トークンを進めて無限ループを防止
    return node_null(line, column);
}

// =============================================================================
// 文のパース（公開）
// =============================================================================

ASTNode *parse_statement(Parser *parser) {
    return statement(parser);
}
