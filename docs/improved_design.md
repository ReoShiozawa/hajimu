# 日本語プログラミング言語 - 詳細設計書

## 1. 概要

### 1.1 目的
日本語を母語とするプログラマが、自然な日本語表現でプログラミングできる言語を提供する。

### 1.2 設計原則
1. **可読性優先**: コードが日本語として自然に読めること
2. **シンプルさ**: 学習コストを最小限に
3. **実用性**: 実際のプログラムが書けること
4. **安全性**: 型エラーや実行時エラーを分かりやすく

## 2. アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│                     ソースコード (.jp)                        │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    字句解析器 (Lexer)                        │
│  ・UTF-8デコード                                             │
│  ・トークン生成                                              │
│  ・キーワード認識                                            │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   構文解析器 (Parser)                        │
│  ・再帰下降パース                                            │
│  ・AST生成                                                   │
│  ・構文エラー検出                                            │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   抽象構文木 (AST)                           │
│  ・プログラム構造の表現                                      │
│  ・位置情報の保持                                            │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    評価器 (Evaluator)                        │
│  ・AST走査                                                   │
│  ・式評価                                                    │
│  ・関数呼び出し                                              │
│  ・環境管理                                                  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                       実行結果                               │
└─────────────────────────────────────────────────────────────┘
```

## 3. 字句解析 (Lexer)

### 3.1 トークン種別

```c
typedef enum {
    // 特殊トークン
    TOKEN_EOF,              // ファイル終端
    TOKEN_NEWLINE,          // 改行
    TOKEN_INDENT,           // インデント増加
    TOKEN_DEDENT,           // インデント減少
    
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
    TOKEN_THEN,             // なら
    
    // キーワード - 繰り返し
    TOKEN_WHILE_COND,       // 条件
    TOKEN_WHILE_END,        // の間
    TOKEN_FOR,              // 繰り返す
    TOKEN_FROM,             // から
    TOKEN_TO,               // を...から（前置詞）
    
    // キーワード - 制御
    TOKEN_BREAK,            // 抜ける
    TOKEN_CONTINUE,         // 続ける
    
    // キーワード - 真偽値
    TOKEN_TRUE,             // 真
    TOKEN_FALSE,            // 偽
    
    // キーワード - 論理演算
    TOKEN_AND,              // かつ
    TOKEN_OR,               // または
    TOKEN_NOT,              // でない
    
    // キーワード - 型
    TOKEN_TYPE_IS,          // は（型注釈）
    TOKEN_TYPE_NUMBER,      // 数値
    TOKEN_TYPE_STRING,      // 文字列
    TOKEN_TYPE_BOOL,        // 真偽
    TOKEN_TYPE_ARRAY,       // 配列
    
    // 演算子
    TOKEN_PLUS,             // +
    TOKEN_MINUS,            // -
    TOKEN_STAR,             // *
    TOKEN_SLASH,            // /
    TOKEN_PERCENT,          // %
    TOKEN_POWER,            // **（べき乗）
    
    // 比較演算子
    TOKEN_EQ,               // ==
    TOKEN_NE,               // !=
    TOKEN_LT,               // <
    TOKEN_LE,               // <=
    TOKEN_GT,               // >
    TOKEN_GE,               // >=
    
    // 代入
    TOKEN_ASSIGN,           // =
    TOKEN_PLUS_ASSIGN,      // +=
    TOKEN_MINUS_ASSIGN,     // -=
    TOKEN_STAR_ASSIGN,      // *=
    TOKEN_SLASH_ASSIGN,     // /=
    
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
    
    // エラー
    TOKEN_ERROR,            // エラートークン
} TokenType;
```

### 3.2 キーワードテーブル

```c
static const Keyword keywords[] = {
    // 関数定義
    {"関数", TOKEN_FUNCTION},
    {"終わり", TOKEN_END},
    {"戻す", TOKEN_RETURN},
    
    // 変数
    {"変数", TOKEN_VARIABLE},
    {"定数", TOKEN_CONSTANT},
    
    // 条件分岐
    {"もし", TOKEN_IF},
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
    
    // 真偽値
    {"真", TOKEN_TRUE},
    {"偽", TOKEN_FALSE},
    
    // 論理演算
    {"かつ", TOKEN_AND},
    {"または", TOKEN_OR},
    {"でない", TOKEN_NOT},
    
    // 型
    {"は", TOKEN_TYPE_IS},
    {"数値", TOKEN_TYPE_NUMBER},
    {"文字列", TOKEN_TYPE_STRING},
    {"真偽", TOKEN_TYPE_BOOL},
    {"配列", TOKEN_TYPE_ARRAY},
    
    {NULL, TOKEN_EOF}
};
```

### 3.3 UTF-8処理

```c
// UTF-8のバイト数を取得
int utf8_char_length(const char *s) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) return 1;       // ASCII
    if (c < 0xC0) return 0;       // 継続バイト（エラー）
    if (c < 0xE0) return 2;       // 2バイト文字
    if (c < 0xF0) return 3;       // 3バイト文字（日本語）
    if (c < 0xF8) return 4;       // 4バイト文字
    return 0;                      // 不正なバイト
}

// UTF-8文字をUnicodeコードポイントに変換
uint32_t utf8_decode(const char *s, int *len) {
    unsigned char c = (unsigned char)*s;
    *len = utf8_char_length(s);
    
    if (*len == 1) return c;
    if (*len == 2) return ((c & 0x1F) << 6) | (s[1] & 0x3F);
    if (*len == 3) return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    if (*len == 4) return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | 
                          ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    return 0;
}
```

### 3.4 インデント処理

Pythonスタイルのインデントベースのブロック構造：

```c
typedef struct {
    int indent_stack[MAX_INDENT_DEPTH];
    int indent_top;
    int pending_dedents;
    int current_indent;
} IndentState;

// 行頭のスペース数をカウント
int count_leading_spaces(const char *line) {
    int count = 0;
    while (*line == ' ') {
        count++;
        line++;
    }
    // タブは4スペースとして扱う
    while (*line == '\t') {
        count += 4;
        line++;
    }
    return count;
}
```

## 4. 構文解析 (Parser)

### 4.1 文法規則 (EBNF)

```ebnf
(* プログラム *)
program = { function_def } ;

(* 関数定義 *)
function_def = "関数" identifier "(" [ param_list ] ")" [ "は" type ] ":" 
               block 
               "終わり" ;

param_list = param { "," param } ;
param = identifier "は" type ;

(* ブロック *)
block = NEWLINE INDENT { statement } DEDENT ;

(* 文 *)
statement = variable_decl
          | assignment
          | if_statement
          | while_statement
          | for_statement
          | return_statement
          | break_statement
          | continue_statement
          | expression_statement ;

(* 変数宣言 *)
variable_decl = ( "変数" | "定数" ) identifier "=" expression NEWLINE ;

(* 代入 *)
assignment = identifier [ "[" expression "]" ] 
             ( "=" | "+=" | "-=" | "*=" | "/=" ) expression NEWLINE ;

(* 条件分岐 *)
if_statement = "もし" expression "なら" block
               [ "それ以外" block ]
               "終わり" ;

(* while繰り返し *)
while_statement = "条件" expression "の間" block "終わり" ;

(* for繰り返し *)
for_statement = identifier "を" expression "から" expression "繰り返す"
                block "終わり" ;

(* return文 *)
return_statement = "戻す" [ expression ] NEWLINE ;

(* break/continue *)
break_statement = "抜ける" NEWLINE ;
continue_statement = "続ける" NEWLINE ;

(* 式文 *)
expression_statement = expression NEWLINE ;

(* 式（優先順位低い順） *)
expression = or_expr ;

or_expr = and_expr { "または" and_expr } ;
and_expr = not_expr { "かつ" not_expr } ;
not_expr = [ "でない" ] comparison ;
comparison = term { ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) term } ;
term = factor { ( "+" | "-" ) factor } ;
factor = power { ( "*" | "/" | "%" ) power } ;
power = unary { "**" unary } ;
unary = [ "-" ] call ;
call = primary { "(" [ arg_list ] ")" | "[" expression "]" } ;
primary = NUMBER | STRING | "真" | "偽" | identifier 
        | "(" expression ")" | array_literal ;

(* 配列リテラル *)
array_literal = "[" [ expression { "," expression } ] "]" ;

(* 引数リスト *)
arg_list = expression { "," expression } ;

(* 型 *)
type = "数値" | "文字列" | "真偽" | "配列" ;
```

### 4.2 演算子の優先順位

| 優先度 | 演算子 | 結合性 |
|--------|--------|--------|
| 1（低）| または | 左 |
| 2 | かつ | 左 |
| 3 | でない | 右 |
| 4 | == != < <= > >= | 左 |
| 5 | + - | 左 |
| 6 | * / % | 左 |
| 7 | ** | 右 |
| 8 | 単項- | 右 |
| 9（高）| () [] . | 左 |

### 4.3 再帰下降パーサの構造

```c
// パーサ構造体
typedef struct {
    Lexer *lexer;
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
    char error_message[256];
} Parser;

// 主要なパース関数
ASTNode *parse_program(Parser *parser);
ASTNode *parse_function(Parser *parser);
ASTNode *parse_statement(Parser *parser);
ASTNode *parse_expression(Parser *parser);
ASTNode *parse_or(Parser *parser);
ASTNode *parse_and(Parser *parser);
ASTNode *parse_comparison(Parser *parser);
ASTNode *parse_term(Parser *parser);
ASTNode *parse_factor(Parser *parser);
ASTNode *parse_unary(Parser *parser);
ASTNode *parse_call(Parser *parser);
ASTNode *parse_primary(Parser *parser);
```

## 5. 抽象構文木 (AST)

### 5.1 ノード種別

```c
typedef enum {
    // プログラム構造
    NODE_PROGRAM,           // プログラム全体
    NODE_FUNCTION_DEF,      // 関数定義
    NODE_BLOCK,             // ブロック（文のリスト）
    
    // 文
    NODE_VAR_DECL,          // 変数宣言
    NODE_CONST_DECL,        // 定数宣言
    NODE_ASSIGN,            // 代入
    NODE_IF,                // 条件分岐
    NODE_WHILE,             // while繰り返し
    NODE_FOR,               // for繰り返し
    NODE_RETURN,            // return
    NODE_BREAK,             // break
    NODE_CONTINUE,          // continue
    NODE_EXPR_STMT,         // 式文
    
    // 式
    NODE_BINARY,            // 二項演算
    NODE_UNARY,             // 単項演算
    NODE_CALL,              // 関数呼び出し
    NODE_INDEX,             // 配列インデックス
    NODE_IDENTIFIER,        // 識別子
    NODE_NUMBER,            // 数値リテラル
    NODE_STRING,            // 文字列リテラル
    NODE_BOOL,              // 真偽値リテラル
    NODE_ARRAY,             // 配列リテラル
} NodeType;
```

### 5.2 ASTノード構造

```c
// 位置情報
typedef struct {
    int line;
    int column;
    const char *filename;
} SourceLocation;

// 基本ノード構造
typedef struct ASTNode {
    NodeType type;
    SourceLocation location;
    
    union {
        // 数値リテラル
        double number_value;
        
        // 文字列・識別子
        char *string_value;
        
        // 真偽値
        bool bool_value;
        
        // 二項演算
        struct {
            TokenType operator;
            struct ASTNode *left;
            struct ASTNode *right;
        } binary;
        
        // 単項演算
        struct {
            TokenType operator;
            struct ASTNode *operand;
        } unary;
        
        // 関数呼び出し
        struct {
            struct ASTNode *callee;
            struct ASTNode **arguments;
            int arg_count;
        } call;
        
        // 関数定義
        struct {
            char *name;
            char **params;
            ValueType *param_types;
            int param_count;
            ValueType return_type;
            struct ASTNode *body;
        } function;
        
        // 変数宣言
        struct {
            char *name;
            struct ASTNode *initializer;
            bool is_const;
        } var_decl;
        
        // 代入
        struct {
            struct ASTNode *target;
            TokenType operator;  // = += -= など
            struct ASTNode *value;
        } assign;
        
        // if文
        struct {
            struct ASTNode *condition;
            struct ASTNode *then_branch;
            struct ASTNode *else_branch;
        } if_stmt;
        
        // while文
        struct {
            struct ASTNode *condition;
            struct ASTNode *body;
        } while_stmt;
        
        // for文
        struct {
            char *var_name;
            struct ASTNode *start;
            struct ASTNode *end;
            struct ASTNode *body;
        } for_stmt;
        
        // return文
        struct {
            struct ASTNode *value;
        } return_stmt;
        
        // ブロック
        struct {
            struct ASTNode **statements;
            int count;
        } block;
        
        // 配列リテラル
        struct {
            struct ASTNode **elements;
            int count;
        } array;
        
        // インデックスアクセス
        struct {
            struct ASTNode *array;
            struct ASTNode *index;
        } index;
    };
} ASTNode;
```

## 6. 値システム

### 6.1 値の型

```c
typedef enum {
    VALUE_NULL,         // null値
    VALUE_NUMBER,       // 数値（double）
    VALUE_BOOL,         // 真偽値
    VALUE_STRING,       // 文字列
    VALUE_ARRAY,        // 配列
    VALUE_FUNCTION,     // 関数
    VALUE_BUILTIN,      // 組み込み関数
} ValueType;
```

### 6.2 値構造体

```c
typedef struct Value {
    ValueType type;
    bool is_const;      // 定数フラグ
    int ref_count;      // 参照カウント（GC用）
    
    union {
        double number;
        bool boolean;
        
        struct {
            char *data;
            int length;
        } string;
        
        struct {
            struct Value *elements;
            int length;
            int capacity;
        } array;
        
        struct {
            ASTNode *definition;
            struct Environment *closure;
        } function;
        
        struct {
            struct Value (*fn)(int argc, struct Value *argv);
            char *name;
        } builtin;
    };
} Value;
```

## 7. 環境（スコープ）

### 7.1 環境構造

```c
#define HASH_TABLE_SIZE 64

typedef struct EnvEntry {
    char *name;
    Value value;
    struct EnvEntry *next;  // チェイン法
} EnvEntry;

typedef struct Environment {
    EnvEntry *table[HASH_TABLE_SIZE];
    struct Environment *parent;  // 親スコープ
    int depth;                   // ネスト深度
} Environment;
```

### 7.2 環境操作

```c
// 新しい環境を作成
Environment *env_new(Environment *parent);

// 環境を解放
void env_free(Environment *env);

// 変数を定義
bool env_define(Environment *env, const char *name, Value value);

// 変数を取得（親スコープも検索）
Value *env_get(Environment *env, const char *name);

// 変数に代入
bool env_set(Environment *env, const char *name, Value value);

// 変数が存在するか確認
bool env_exists(Environment *env, const char *name);
```

## 8. 評価器

### 8.1 評価器構造

```c
typedef struct {
    Environment *global;        // グローバル環境
    Environment *current;       // 現在の環境
    Value last_result;          // 最後の評価結果
    bool returning;             // return中フラグ
    bool breaking;              // break中フラグ
    bool continuing;            // continue中フラグ
    Value return_value;         // return値
    char error_message[256];    // エラーメッセージ
    bool had_error;             // エラーフラグ
} Evaluator;
```

### 8.2 評価関数

```c
// 初期化
Evaluator *evaluator_new(void);

// プログラム全体を評価
Value evaluator_run(Evaluator *eval, ASTNode *program);

// ノードを評価
Value evaluate(Evaluator *eval, ASTNode *node);

// 各ノード種別の評価
Value eval_number(Evaluator *eval, ASTNode *node);
Value eval_string(Evaluator *eval, ASTNode *node);
Value eval_bool(Evaluator *eval, ASTNode *node);
Value eval_identifier(Evaluator *eval, ASTNode *node);
Value eval_binary(Evaluator *eval, ASTNode *node);
Value eval_unary(Evaluator *eval, ASTNode *node);
Value eval_call(Evaluator *eval, ASTNode *node);
Value eval_var_decl(Evaluator *eval, ASTNode *node);
Value eval_assign(Evaluator *eval, ASTNode *node);
Value eval_if(Evaluator *eval, ASTNode *node);
Value eval_while(Evaluator *eval, ASTNode *node);
Value eval_for(Evaluator *eval, ASTNode *node);
Value eval_function_def(Evaluator *eval, ASTNode *node);
Value eval_return(Evaluator *eval, ASTNode *node);
Value eval_array(Evaluator *eval, ASTNode *node);
Value eval_index(Evaluator *eval, ASTNode *node);
```

### 8.3 二項演算の評価

```c
Value eval_binary(Evaluator *eval, ASTNode *node) {
    Value left = evaluate(eval, node->binary.left);
    if (eval->had_error) return VALUE_NULL;
    
    // 短絡評価
    if (node->binary.operator == TOKEN_AND) {
        if (!is_truthy(left)) return make_bool(false);
        return evaluate(eval, node->binary.right);
    }
    if (node->binary.operator == TOKEN_OR) {
        if (is_truthy(left)) return left;
        return evaluate(eval, node->binary.right);
    }
    
    Value right = evaluate(eval, node->binary.right);
    if (eval->had_error) return VALUE_NULL;
    
    // 数値演算
    if (left.type == VALUE_NUMBER && right.type == VALUE_NUMBER) {
        switch (node->binary.operator) {
            case TOKEN_PLUS:  return make_number(left.number + right.number);
            case TOKEN_MINUS: return make_number(left.number - right.number);
            case TOKEN_STAR:  return make_number(left.number * right.number);
            case TOKEN_SLASH:
                if (right.number == 0) {
                    runtime_error(eval, "ゼロ除算エラー");
                    return VALUE_NULL;
                }
                return make_number(left.number / right.number);
            case TOKEN_PERCENT:
                return make_number(fmod(left.number, right.number));
            case TOKEN_POWER:
                return make_number(pow(left.number, right.number));
            case TOKEN_EQ:    return make_bool(left.number == right.number);
            case TOKEN_NE:    return make_bool(left.number != right.number);
            case TOKEN_LT:    return make_bool(left.number < right.number);
            case TOKEN_LE:    return make_bool(left.number <= right.number);
            case TOKEN_GT:    return make_bool(left.number > right.number);
            case TOKEN_GE:    return make_bool(left.number >= right.number);
        }
    }
    
    // 文字列結合
    if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
        if (node->binary.operator == TOKEN_PLUS) {
            return string_concat(left, right);
        }
    }
    
    runtime_error(eval, "不正な演算: %s", operator_name(node->binary.operator));
    return VALUE_NULL;
}
```

## 9. 組み込み関数

### 9.1 標準組み込み関数

| 関数名 | 引数 | 戻り値 | 説明 |
|--------|------|--------|------|
| `表示` | 任意個 | なし | コンソール出力 |
| `入力` | (プロンプト?) | 文字列 | コンソール入力 |
| `長さ` | 配列/文字列 | 数値 | 長さを取得 |
| `追加` | 配列, 値 | なし | 配列に追加 |
| `削除` | 配列, 位置 | 値 | 配列から削除 |
| `型` | 任意 | 文字列 | 型名を取得 |
| `数値化` | 文字列 | 数値 | 数値に変換 |
| `文字列化` | 任意 | 文字列 | 文字列に変換 |

### 9.2 数学関数

| 関数名 | 引数 | 戻り値 | 説明 |
|--------|------|--------|------|
| `絶対値` | 数値 | 数値 | 絶対値 |
| `平方根` | 数値 | 数値 | 平方根 |
| `切り捨て` | 数値 | 数値 | 小数点以下切り捨て |
| `切り上げ` | 数値 | 数値 | 小数点以下切り上げ |
| `四捨五入` | 数値 | 数値 | 四捨五入 |
| `乱数` | () | 数値 | 0-1の乱数 |
| `最大` | 数値... | 数値 | 最大値 |
| `最小` | 数値... | 数値 | 最小値 |

### 9.3 組み込み関数の実装

```c
typedef struct {
    const char *name;
    Value (*function)(int argc, Value *argv);
    int min_args;
    int max_args;  // -1 = 可変長
} BuiltinDef;

static const BuiltinDef builtins[] = {
    {"表示", builtin_print, 0, -1},
    {"入力", builtin_input, 0, 1},
    {"長さ", builtin_length, 1, 1},
    {"追加", builtin_append, 2, 2},
    {"削除", builtin_remove, 2, 2},
    {"型", builtin_type, 1, 1},
    {"数値化", builtin_to_number, 1, 1},
    {"文字列化", builtin_to_string, 1, 1},
    {"絶対値", builtin_abs, 1, 1},
    {"平方根", builtin_sqrt, 1, 1},
    {"切り捨て", builtin_floor, 1, 1},
    {"切り上げ", builtin_ceil, 1, 1},
    {"四捨五入", builtin_round, 1, 1},
    {"乱数", builtin_random, 0, 0},
    {"最大", builtin_max, 1, -1},
    {"最小", builtin_min, 1, -1},
    {NULL, NULL, 0, 0}
};
```

## 10. エラー処理

### 10.1 エラーの種類

```c
typedef enum {
    ERROR_NONE,
    
    // 字句解析エラー
    ERROR_INVALID_CHARACTER,
    ERROR_UNTERMINATED_STRING,
    ERROR_INVALID_NUMBER,
    ERROR_INVALID_UTF8,
    
    // 構文解析エラー
    ERROR_UNEXPECTED_TOKEN,
    ERROR_EXPECTED_TOKEN,
    ERROR_INVALID_SYNTAX,
    ERROR_UNCLOSED_BLOCK,
    
    // 実行時エラー
    ERROR_UNDEFINED_VARIABLE,
    ERROR_UNDEFINED_FUNCTION,
    ERROR_TYPE_MISMATCH,
    ERROR_DIVISION_BY_ZERO,
    ERROR_INDEX_OUT_OF_RANGE,
    ERROR_ARGUMENT_COUNT,
    ERROR_CONST_REASSIGN,
    ERROR_STACK_OVERFLOW,
} ErrorType;
```

### 10.2 エラーメッセージ形式

```
エラー: [ファイル名]:[行番号]:[列番号]
    |
 42 |     変数 x = 10 / 0
    |                   ^
ゼロ除算エラー: 0で割ることはできません
```

## 11. メモリ管理

### 11.1 参照カウント方式

```c
// 参照カウントを増加
void value_retain(Value *v) {
    if (is_heap_value(v)) {
        v->ref_count++;
    }
}

// 参照カウントを減少
void value_release(Value *v) {
    if (is_heap_value(v)) {
        v->ref_count--;
        if (v->ref_count <= 0) {
            value_free(v);
        }
    }
}

// ヒープに確保される型かチェック
bool is_heap_value(Value *v) {
    return v->type == VALUE_STRING || 
           v->type == VALUE_ARRAY || 
           v->type == VALUE_FUNCTION;
}
```

### 11.2 文字列プール（最適化）

```c
#define STRING_POOL_SIZE 256

typedef struct StringPoolEntry {
    char *string;
    int ref_count;
    struct StringPoolEntry *next;
} StringPoolEntry;

static StringPoolEntry *string_pool[STRING_POOL_SIZE];

// 文字列をプールから取得または追加
char *string_intern(const char *s) {
    uint32_t hash = string_hash(s) % STRING_POOL_SIZE;
    
    // 既存のエントリを検索
    for (StringPoolEntry *e = string_pool[hash]; e; e = e->next) {
        if (strcmp(e->string, s) == 0) {
            e->ref_count++;
            return e->string;
        }
    }
    
    // 新しいエントリを作成
    StringPoolEntry *entry = malloc(sizeof(StringPoolEntry));
    entry->string = strdup(s);
    entry->ref_count = 1;
    entry->next = string_pool[hash];
    string_pool[hash] = entry;
    
    return entry->string;
}
```

## 12. 将来の拡張

### 12.1 クラス・オブジェクト

```
クラス 動物:
    関数 初期化(名前 は 文字列):
        自分.名前 = 名前
    終わり
    
    関数 鳴く():
        表示("...")
    終わり
終わり

クラス 犬 は 動物:
    関数 鳴く():
        表示(自分.名前, "がワンワンと鳴いた")
    終わり
終わり
```

### 12.2 例外処理

```
試す
    変数 結果 = 危険な処理()
例外 e なら
    表示("エラー発生:", e.メッセージ)
終わり
```

### 12.3 モジュール

```
取り込む "数学"
取り込む "ファイル" から { 読む, 書く }

関数 メイン():
    表示(数学.円周率)
終わり
```

## 13. 付録

### 13.1 予約語一覧

```
関数 終わり 戻す
変数 定数
もし それ以外 なら
条件 の間 繰り返す から を
抜ける 続ける
真 偽
かつ または でない
は 数値 文字列 真偽 配列
```

### 13.2 演算子一覧

```
+ - * / % **
== != < <= > >=
= += -= *= /=
かつ または でない
```

### 13.3 特殊文字

```
( ) [ ] { }
, : .
# コメント
"文字列"
```
