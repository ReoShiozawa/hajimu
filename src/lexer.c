/**
 * 日本語プログラミング言語 - 字句解析器実装
 * 
 * UTF-8対応のトークナイザ
 */

#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================================
// キーワードテーブル
// =============================================================================

typedef struct {
    const char *keyword;
    TokenType type;
} KeywordEntry;

static const KeywordEntry keywords[] = {
    // 関数定義
    {"関数", TOKEN_FUNCTION},
    {"終わり", TOKEN_END},
    {"戻す", TOKEN_RETURN},
    
    // 変数
    {"変数", TOKEN_VARIABLE},
    {"定数", TOKEN_CONSTANT},
    
    // 条件分岐
    {"もし", TOKEN_IF},
    {"それ以外もし", TOKEN_ELSE_IF},
    {"それ以外", TOKEN_ELSE},
    {"なら", TOKEN_THEN},
    
    // 繰り返し
    {"条件", TOKEN_WHILE_COND},
    {"の間", TOKEN_WHILE_END},
    {"繰り返す", TOKEN_FOR},
    {"から", TOKEN_FROM},
    {"を", TOKEN_TO},
    
    // 制御
    {"抜ける", TOKEN_BREAK},
    {"続ける", TOKEN_CONTINUE},
    {"取り込む", TOKEN_IMPORT},
    
    // クラス/OOP
    {"型", TOKEN_CLASS},
    {"新規", TOKEN_NEW},
    {"継承", TOKEN_EXTENDS},
    {"自分", TOKEN_SELF},
    {"初期化", TOKEN_INIT},
    {"親", TOKEN_SUPER},
    
    // 例外処理
    {"試行", TOKEN_TRY},
    {"捕獲", TOKEN_CATCH},
    {"最終", TOKEN_FINALLY},
    {"投げる", TOKEN_THROW},
    {"列挙", TOKEN_ENUM},
    {"照合", TOKEN_MATCH},
    {"譲渡", TOKEN_YIELD},
    {"生成関数", TOKEN_GENERATOR_FUNC},
    
    // 選択文
    {"選択", TOKEN_SWITCH},
    {"場合", TOKEN_CASE},
    {"既定", TOKEN_DEFAULT},
    
    // foreach
    {"各", TOKEN_EACH},
    {"の中", TOKEN_IN},
    
    // 真偽値
    {"真", TOKEN_TRUE},
    {"偽", TOKEN_FALSE},
    {"無", TOKEN_NULL_LITERAL},
    
    // 論理演算
    {"かつ", TOKEN_AND},
    {"または", TOKEN_OR},
    {"でない", TOKEN_NOT},
    
    // 型
    {"は", TOKEN_TYPE_IS},
    {"数値", TOKEN_TYPE_NUMBER},
    {"文字列", TOKEN_TYPE_STRING_T},
    {"真偽", TOKEN_TYPE_BOOL},
    {"配列", TOKEN_TYPE_ARRAY},
    
    // 終端
    {NULL, TOKEN_EOF}
};

// =============================================================================
// トークン種別名テーブル
// =============================================================================

static const char *token_names[] = {
    [TOKEN_EOF] = "EOF",
    [TOKEN_NEWLINE] = "NEWLINE",
    [TOKEN_INDENT] = "INDENT",
    [TOKEN_DEDENT] = "DEDENT",
    [TOKEN_ERROR] = "ERROR",
    
    [TOKEN_NUMBER] = "NUMBER",
    [TOKEN_STRING] = "STRING",
    [TOKEN_IDENTIFIER] = "IDENTIFIER",
    
    [TOKEN_FUNCTION] = "関数",
    [TOKEN_END] = "終わり",
    [TOKEN_RETURN] = "戻す",
    
    [TOKEN_VARIABLE] = "変数",
    [TOKEN_CONSTANT] = "定数",
    
    [TOKEN_IF] = "もし",
    [TOKEN_ELSE] = "それ以外",
    [TOKEN_ELSE_IF] = "それ以外もし",
    [TOKEN_THEN] = "なら",
    
    [TOKEN_WHILE_COND] = "条件",
    [TOKEN_WHILE_END] = "の間",
    [TOKEN_FOR] = "繰り返す",
    [TOKEN_FROM] = "から",
    [TOKEN_TO] = "を",
    
    [TOKEN_BREAK] = "抜ける",
    [TOKEN_CONTINUE] = "続ける",
    
    [TOKEN_TRUE] = "真",
    [TOKEN_FALSE] = "偽",
    [TOKEN_NULL_LITERAL] = "無",
    
    [TOKEN_AND] = "かつ",
    [TOKEN_OR] = "または",
    [TOKEN_NOT] = "でない",
    
    [TOKEN_TYPE_IS] = "は",
    [TOKEN_TYPE_NUMBER] = "数値型",
    [TOKEN_TYPE_STRING_T] = "文字列型",
    [TOKEN_TYPE_BOOL] = "真偽型",
    [TOKEN_TYPE_ARRAY] = "配列型",
    
    [TOKEN_ENUM] = "列挙",
    [TOKEN_MATCH] = "照合",
    [TOKEN_ARROW] = "=>",
    [TOKEN_YIELD] = "譲渡",
    [TOKEN_GENERATOR_FUNC] = "生成関数",
    
    [TOKEN_PLUS] = "+",
    [TOKEN_MINUS] = "-",
    [TOKEN_STAR] = "*",
    [TOKEN_SLASH] = "/",
    [TOKEN_PERCENT] = "%",
    [TOKEN_POWER] = "**",
    
    [TOKEN_EQ] = "==",
    [TOKEN_NE] = "!=",
    [TOKEN_LT] = "<",
    [TOKEN_LE] = "<=",
    [TOKEN_GT] = ">",
    [TOKEN_GE] = ">=",
    
    [TOKEN_ASSIGN] = "=",
    [TOKEN_PLUS_ASSIGN] = "+=",
    [TOKEN_MINUS_ASSIGN] = "-=",
    [TOKEN_STAR_ASSIGN] = "*=",
    [TOKEN_SLASH_ASSIGN] = "/=",
    
    [TOKEN_LPAREN] = "(",
    [TOKEN_RPAREN] = ")",
    [TOKEN_LBRACKET] = "[",
    [TOKEN_RBRACKET] = "]",
    [TOKEN_LBRACE] = "{",
    [TOKEN_RBRACE] = "}",
    [TOKEN_COMMA] = ",",
    [TOKEN_COLON] = ":",
    [TOKEN_DOT] = ".",
    [TOKEN_SPREAD] = "...",
    [TOKEN_PIPE] = "|>",
    [TOKEN_QUESTION] = "?",
    [TOKEN_NULL_COALESCE] = "??",
};

// =============================================================================
// UTF-8ユーティリティ
// =============================================================================

int utf8_char_length(unsigned char c) {
    if (c < 0x80) return 1;       // ASCII (0xxxxxxx)
    if (c < 0xC0) return 0;       // 継続バイト（単独では無効）
    if (c < 0xE0) return 2;       // 2バイト (110xxxxx)
    if (c < 0xF0) return 3;       // 3バイト (1110xxxx) ← 日本語
    if (c < 0xF8) return 4;       // 4バイト (11110xxx)
    return 0;                      // 無効
}

uint32_t utf8_decode(const char *s, int *len) {
    unsigned char c = (unsigned char)*s;
    *len = utf8_char_length(c);
    
    if (*len == 0) {
        *len = 1;  // 無効なバイトをスキップ
        return 0xFFFD;  // 置換文字
    }
    
    if (*len == 1) return c;
    
    uint32_t codepoint = 0;
    
    if (*len == 2) {
        codepoint = (c & 0x1F) << 6;
        codepoint |= (s[1] & 0x3F);
    } else if (*len == 3) {
        codepoint = (c & 0x0F) << 12;
        codepoint |= (s[1] & 0x3F) << 6;
        codepoint |= (s[2] & 0x3F);
    } else if (*len == 4) {
        codepoint = (c & 0x07) << 18;
        codepoint |= (s[1] & 0x3F) << 12;
        codepoint |= (s[2] & 0x3F) << 6;
        codepoint |= (s[3] & 0x3F);
    }
    
    return codepoint;
}

bool is_japanese_char(uint32_t cp) {
    // ひらがな: U+3040 - U+309F
    if (cp >= 0x3040 && cp <= 0x309F) return true;
    // カタカナ: U+30A0 - U+30FF
    if (cp >= 0x30A0 && cp <= 0x30FF) return true;
    // CJK統合漢字: U+4E00 - U+9FFF
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
    // CJK統合漢字拡張A: U+3400 - U+4DBF
    if (cp >= 0x3400 && cp <= 0x4DBF) return true;
    // 全角英数字: U+FF00 - U+FFEF
    if (cp >= 0xFF00 && cp <= 0xFFEF) return true;
    // 半角・全角形: U+FF65 - U+FF9F (半角カナ)
    if (cp >= 0xFF65 && cp <= 0xFF9F) return true;
    
    return false;
}

bool is_identifier_start(uint32_t cp) {
    // 日本語文字
    if (is_japanese_char(cp)) return true;
    // ASCII英字
    if (cp >= 'a' && cp <= 'z') return true;
    if (cp >= 'A' && cp <= 'Z') return true;
    // アンダースコア
    if (cp == '_') return true;
    
    return false;
}

bool is_identifier_char(uint32_t cp) {
    if (is_identifier_start(cp)) return true;
    // ASCII数字
    if (cp >= '0' && cp <= '9') return true;
    
    return false;
}

// =============================================================================
// Lexer内部関数
// =============================================================================

// 現在の文字を取得（進めない）
static char peek(Lexer *lexer) {
    return *lexer->current;
}

// 次の文字を取得（進めない）
static char peek_next(Lexer *lexer) {
    if (*lexer->current == '\0') return '\0';
    return lexer->current[1];
}

// 終端に達したか
static bool is_at_end(Lexer *lexer) {
    return *lexer->current == '\0';
}

// 1文字進める
static char advance(Lexer *lexer) {
    char c = *lexer->current;
    lexer->current++;
    lexer->column++;
    return c;
}

// UTF-8で1文字進める
static uint32_t advance_utf8(Lexer *lexer) {
    int len;
    uint32_t cp = utf8_decode(lexer->current, &len);
    lexer->current += len;
    lexer->column++;  // 列番号は文字単位
    return cp;
}

// 現在のUTF-8文字を取得（進めない）
static uint32_t peek_utf8(Lexer *lexer) {
    int len;
    return utf8_decode(lexer->current, &len);
}

// 期待する文字なら進む
static bool match(Lexer *lexer, char expected) {
    if (is_at_end(lexer)) return false;
    if (*lexer->current != expected) return false;
    lexer->current++;
    lexer->column++;
    return true;
}

// トークンを作成
static Token make_token(Lexer *lexer, TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer->start;
    token.length = (int)(lexer->current - lexer->start);
    token.line = lexer->line;
    token.column = lexer->token_start_column;
    token.value.number = 0;
    return token;
}

// エラートークンを作成
static Token error_token(Lexer *lexer, const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = lexer->line;
    token.column = lexer->token_start_column;
    
    lexer->had_error = true;
    strncpy(lexer->error_message, message, sizeof(lexer->error_message) - 1);
    
    return token;
}

// 空白をスキップ
static void skip_whitespace(Lexer *lexer) {
    for (;;) {
        char c = peek(lexer);
        switch (c) {
            case ' ':
            case '\t':
            case '\r':
                advance(lexer);
                break;
            case '#':  // コメント（#スタイル）
                while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                    advance(lexer);
                }
                break;
            case '/':  // コメント（//スタイル）
                if (peek_next(lexer) == '/') {
                    while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                        advance(lexer);
                    }
                    break;
                }
                return;
            default:
                return;
        }
    }
}

// 行頭のインデントを数える
static int count_indent(Lexer *lexer) {
    int spaces = 0;
    const char *p = lexer->current;
    
    while (*p == ' ' || *p == '\t') {
        if (*p == ' ') {
            spaces++;
        } else {
            // タブは4スペースとして扱う
            spaces += 4 - (spaces % 4);
        }
        p++;
    }
    
    return spaces;
}

// キーワードを検索
static TokenType check_keyword(const char *start, int length) {
    for (int i = 0; keywords[i].keyword != NULL; i++) {
        if ((int)strlen(keywords[i].keyword) == length &&
            memcmp(start, keywords[i].keyword, length) == 0) {
            return keywords[i].type;
        }
    }
    return TOKEN_IDENTIFIER;
}

// 識別子をスキャン
static Token scan_identifier(Lexer *lexer) {
    while (!is_at_end(lexer)) {
        uint32_t cp = peek_utf8(lexer);
        if (!is_identifier_char(cp)) break;
        
        int len;
        utf8_decode(lexer->current, &len);
        lexer->current += len;
        lexer->column++;
    }
    
    int length = (int)(lexer->current - lexer->start);
    TokenType type = check_keyword(lexer->start, length);
    
    return make_token(lexer, type);
}

// 数値をスキャン
static Token scan_number(Lexer *lexer) {
    // 整数部
    while (isdigit(peek(lexer))) {
        advance(lexer);
    }
    
    // 小数部
    if (peek(lexer) == '.' && isdigit(peek_next(lexer))) {
        advance(lexer);  // '.'を消費
        while (isdigit(peek(lexer))) {
            advance(lexer);
        }
    }
    
    // 指数部
    if (peek(lexer) == 'e' || peek(lexer) == 'E') {
        advance(lexer);
        if (peek(lexer) == '+' || peek(lexer) == '-') {
            advance(lexer);
        }
        while (isdigit(peek(lexer))) {
            advance(lexer);
        }
    }
    
    Token token = make_token(lexer, TOKEN_NUMBER);
    
    // 数値を解析
    char *temp = malloc(token.length + 1);
    memcpy(temp, token.start, token.length);
    temp[token.length] = '\0';
    token.value.number = strtod(temp, NULL);
    free(temp);
    
    return token;
}

// 複数行文字列をスキャン（"""..."""）
static Token scan_multiline_string(Lexer *lexer) {
    // 開始の """ は既に消費済み
    
    size_t capacity = 256;
    size_t length = 0;
    char *buffer = malloc(capacity);
    bool closed = false;
    
    while (!is_at_end(lexer)) {
        char c = peek(lexer);
        
        // """ の終端チェック: c == peek == current[0]
        if (c == '"') {
            size_t remaining = (lexer->source + strlen(lexer->source)) - lexer->current;
            if (remaining >= 3 && 
                lexer->current[1] == '"' && 
                lexer->current[2] == '"') {
                // 3つの " を消費して終了
                advance(lexer);  // 1つ目の "
                advance(lexer);  // 2つ目の "
                advance(lexer);  // 3つ目の "
                closed = true;
                break;
            }
        }
        
        c = advance(lexer);
        if (c == '\n') {
            lexer->line++;
            lexer->column = 1;
        }
        
        // エスケープシーケンス処理
        if (c == '\\' && !is_at_end(lexer)) {
            char next = advance(lexer);
            switch (next) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '0': c = '\0'; break;
                default:
                    if (length + 1 >= capacity) {
                        capacity *= 2;
                        buffer = realloc(buffer, capacity);
                    }
                    buffer[length++] = '\\';
                    c = next;
            }
        }
        
        if (length + 1 >= capacity) {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
        }
        buffer[length++] = c;
    }
    
    if (!closed) {
        free(buffer);
        return error_token(lexer, "複数行文字列が閉じられていません");
    }
    
    buffer[length] = '\0';
    
    Token token = make_token(lexer, TOKEN_STRING);
    token.value.string = buffer;
    
    return token;
}

// 文字列をスキャン
static Token scan_string(Lexer *lexer) {
    // 開始の " は既に消費済み
    
    // 文字列の内容を格納するバッファ
    size_t capacity = 64;
    size_t length = 0;
    char *buffer = malloc(capacity);
    
    while (peek(lexer) != '"' && !is_at_end(lexer)) {
        if (peek(lexer) == '\n') {
            lexer->line++;
            lexer->column = 1;
        }
        
        char c = advance(lexer);
        
        // エスケープシーケンス処理
        if (c == '\\' && !is_at_end(lexer)) {
            char next = advance(lexer);
            switch (next) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '0': c = '\0'; break;
                default:
                    // 不明なエスケープはそのまま
                    if (length + 1 >= capacity) {
                        capacity *= 2;
                        buffer = realloc(buffer, capacity);
                    }
                    buffer[length++] = '\\';
                    c = next;
            }
        }
        
        // バッファに追加
        if (length + 1 >= capacity) {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
        }
        buffer[length++] = c;
    }
    
    if (is_at_end(lexer)) {
        free(buffer);
        return error_token(lexer, "文字列が閉じられていません");
    }
    
    // 閉じの " を消費
    advance(lexer);
    
    // NULL終端
    buffer[length] = '\0';
    
    Token token = make_token(lexer, TOKEN_STRING);
    token.value.string = buffer;  // 呼び出し側で解放が必要
    
    return token;
}

// =============================================================================
// 公開関数
// =============================================================================

void lexer_init(Lexer *lexer, const char *source, const char *filename) {
    lexer->source = source;
    lexer->start = source;
    lexer->current = source;
    lexer->filename = filename;
    lexer->line = 1;
    lexer->column = 1;
    lexer->token_start_column = 1;
    
    // インデントスタック初期化
    lexer->indent_stack[0] = 0;
    lexer->indent_top = 0;
    lexer->pending_dedents = 0;
    lexer->at_line_start = true;
    lexer->current_indent = 0;
    
    lexer->had_error = false;
    lexer->error_message[0] = '\0';
}

Token lexer_next(Lexer *lexer) {
    // 保留中のDEDENTがあれば発行
    if (lexer->pending_dedents > 0) {
        lexer->pending_dedents--;
        return make_token(lexer, TOKEN_DEDENT);
    }
    
    // 行頭のインデント処理
    if (lexer->at_line_start) {
        lexer->at_line_start = false;
        
        // 空行・コメントのみの行はスキップ
        const char *line_start = lexer->current;
        int indent = count_indent(lexer);
        
        // インデント分を進める
        while (*lexer->current == ' ' || *lexer->current == '\t') {
            advance(lexer);
        }
        
        // 空行またはコメント行の場合
        if (*lexer->current == '\n' || *lexer->current == '#' || *lexer->current == '\0') {
            if (*lexer->current == '\n') {
                advance(lexer);
                lexer->line++;
                lexer->column = 1;
                lexer->at_line_start = true;
            } else if (*lexer->current == '#') {
                // コメントをスキップ
                while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                    advance(lexer);
                }
                if (peek(lexer) == '\n') {
                    advance(lexer);
                    lexer->line++;
                    lexer->column = 1;
                    lexer->at_line_start = true;
                }
            }
            // 再帰的に次のトークンを取得
            return lexer_next(lexer);
        }
        
        // インデントレベルの変化をチェック
        int current_stack_indent = lexer->indent_stack[lexer->indent_top];
        
        if (indent > current_stack_indent) {
            // インデント増加
            lexer->indent_top++;
            if (lexer->indent_top >= MAX_INDENT_DEPTH) {
                return error_token(lexer, "インデントが深すぎます");
            }
            lexer->indent_stack[lexer->indent_top] = indent;
            lexer->start = line_start;
            return make_token(lexer, TOKEN_INDENT);
        } else if (indent < current_stack_indent) {
            // インデント減少（複数レベル分の可能性あり）
            while (lexer->indent_top > 0 && 
                   indent < lexer->indent_stack[lexer->indent_top]) {
                lexer->indent_top--;
                lexer->pending_dedents++;
            }
            
            // 不整合チェック
            if (indent != lexer->indent_stack[lexer->indent_top]) {
                return error_token(lexer, "インデントが一致しません");
            }
            
            if (lexer->pending_dedents > 0) {
                lexer->pending_dedents--;
                lexer->start = line_start;
                return make_token(lexer, TOKEN_DEDENT);
            }
        }
    }
    
    skip_whitespace(lexer);
    
    lexer->start = lexer->current;
    lexer->token_start_column = lexer->column;
    
    if (is_at_end(lexer)) {
        // ファイル終端で残りのDEDENTを発行
        if (lexer->indent_top > 0) {
            lexer->indent_top--;
            return make_token(lexer, TOKEN_DEDENT);
        }
        return make_token(lexer, TOKEN_EOF);
    }
    
    // UTF-8文字をチェック
    uint32_t cp = peek_utf8(lexer);
    
    // 識別子（日本語または英字で始まる）
    if (is_identifier_start(cp)) {
        return scan_identifier(lexer);
    }
    
    char c = advance(lexer);
    
    // 数字
    if (isdigit(c)) {
        lexer->current--;  // 戻して再スキャン
        lexer->column--;
        return scan_number(lexer);
    }
    
    // 小数点から始まる数値
    if (c == '.' && isdigit(peek(lexer))) {
        lexer->current--;
        lexer->column--;
        return scan_number(lexer);
    }
    
    switch (c) {
        // 改行
        case '\n':
            lexer->line++;
            lexer->column = 1;
            lexer->at_line_start = true;
            return make_token(lexer, TOKEN_NEWLINE);
        
        // 文字列
        case '"':
            // 複数行文字列 """...""" のチェック
            if (peek(lexer) == '"' && lexer->current + 1 < lexer->source + strlen(lexer->source) && *(lexer->current + 1) == '"') {
                // 2つの " を消費
                advance(lexer);
                advance(lexer);
                return scan_multiline_string(lexer);
            }
            return scan_string(lexer);
        
        // 単一文字トークン
        case '(': return make_token(lexer, TOKEN_LPAREN);
        case ')': return make_token(lexer, TOKEN_RPAREN);
        case '[': return make_token(lexer, TOKEN_LBRACKET);
        case ']': return make_token(lexer, TOKEN_RBRACKET);
        case '{': return make_token(lexer, TOKEN_LBRACE);
        case '}': return make_token(lexer, TOKEN_RBRACE);
        case ',': return make_token(lexer, TOKEN_COMMA);
        case ':': return make_token(lexer, TOKEN_COLON);
        case '.':
            if (peek(lexer) == '.' && peek_next(lexer) == '.') {
                advance(lexer);
                advance(lexer);
                return make_token(lexer, TOKEN_SPREAD);
            }
            return make_token(lexer, TOKEN_DOT);
        case '%': return make_token(lexer, TOKEN_PERCENT);
        
        // 複合トークン
        case '+':
            return make_token(lexer, match(lexer, '=') ? TOKEN_PLUS_ASSIGN : TOKEN_PLUS);
        case '-':
            return make_token(lexer, match(lexer, '=') ? TOKEN_MINUS_ASSIGN : TOKEN_MINUS);
        case '*':
            if (match(lexer, '*')) {
                return make_token(lexer, TOKEN_POWER);
            }
            return make_token(lexer, match(lexer, '=') ? TOKEN_STAR_ASSIGN : TOKEN_STAR);
        case '/':
            return make_token(lexer, match(lexer, '=') ? TOKEN_SLASH_ASSIGN : TOKEN_SLASH);
        case '=':
            if (match(lexer, '=')) return make_token(lexer, TOKEN_EQ);
            if (match(lexer, '>')) return make_token(lexer, TOKEN_ARROW);
            return make_token(lexer, TOKEN_ASSIGN);
        case '!':
            if (match(lexer, '=')) {
                return make_token(lexer, TOKEN_NE);
            }
            return error_token(lexer, "予期しない文字 '!'");
        case '<':
            return make_token(lexer, match(lexer, '=') ? TOKEN_LE : TOKEN_LT);
        case '>':
            return make_token(lexer, match(lexer, '=') ? TOKEN_GE : TOKEN_GT);
        case '|':
            if (match(lexer, '>')) {
                return make_token(lexer, TOKEN_PIPE);
            }
            return error_token(lexer, "予期しない文字 '|'");
        
        case '?':
            if (match(lexer, '?')) {
                return make_token(lexer, TOKEN_NULL_COALESCE);
            }
            return make_token(lexer, TOKEN_QUESTION);
    }
    
    return error_token(lexer, "予期しない文字です");
}

const char *token_type_name(TokenType type) {
    if (type >= 0 && type < TOKEN_COUNT) {
        return token_names[type];
    }
    return "UNKNOWN";
}

void token_to_string(Token token, char *buffer, size_t size) {
    if (token.type == TOKEN_ERROR) {
        snprintf(buffer, size, "ERROR: %s", token.start);
    } else if (token.type == TOKEN_NUMBER) {
        snprintf(buffer, size, "NUMBER(%g)", token.value.number);
    } else if (token.type == TOKEN_STRING) {
        snprintf(buffer, size, "STRING(\"%s\")", token.value.string);
    } else if (token.type == TOKEN_IDENTIFIER) {
        snprintf(buffer, size, "IDENTIFIER(%.*s)", token.length, token.start);
    } else if (token.type == TOKEN_NEWLINE) {
        snprintf(buffer, size, "NEWLINE");
    } else if (token.type == TOKEN_INDENT) {
        snprintf(buffer, size, "INDENT");
    } else if (token.type == TOKEN_DEDENT) {
        snprintf(buffer, size, "DEDENT");
    } else if (token.type == TOKEN_EOF) {
        snprintf(buffer, size, "EOF");
    } else {
        snprintf(buffer, size, "%s", token_type_name(token.type));
    }
}
