/**
 * 日本語プログラミング言語 - 抽象構文木（AST）ヘッダー
 * 
 * プログラムの構造を表現する木構造
 */

#ifndef AST_H
#define AST_H

#include "lexer.h"
#include "value.h"
#include <stdbool.h>

// =============================================================================
// ノード種別
// =============================================================================

typedef enum {
    // プログラム構造
    NODE_PROGRAM,           // プログラム全体
    NODE_FUNCTION_DEF,      // 関数定義
    NODE_BLOCK,             // ブロック（文のリスト）
    
    // 文
    NODE_VAR_DECL,          // 変数宣言
    NODE_ASSIGN,            // 代入文
    NODE_IF,                // if文
    NODE_WHILE,             // while文
    NODE_FOR,               // for文
    NODE_RETURN,            // return文
    NODE_BREAK,             // break文
    NODE_CONTINUE,          // continue文
    NODE_EXPR_STMT,         // 式文
    
    // 式
    NODE_BINARY,            // 二項演算
    NODE_UNARY,             // 単項演算
    NODE_CALL,              // 関数呼び出し
    NODE_INDEX,             // 配列インデックス
    NODE_MEMBER,            // メンバーアクセス（将来用）
    
    // リテラル・識別子
    NODE_IDENTIFIER,        // 識別子
    NODE_NUMBER,            // 数値リテラル
    NODE_STRING,            // 文字列リテラル
    NODE_BOOL,              // 真偽値リテラル
    NODE_ARRAY,             // 配列リテラル
    NODE_NULL,              // null
    
    NODE_TYPE_COUNT
} NodeType;

// =============================================================================
// 位置情報
// =============================================================================

typedef struct {
    int line;           // 行番号（1始まり）
    int column;         // 列番号（1始まり）
    const char *filename;  // ファイル名
} SourceLocation;

// =============================================================================
// パラメータ
// =============================================================================

typedef struct {
    char *name;             // パラメータ名
    ValueType type;         // 型（オプション）
    bool has_type;          // 型注釈があるか
} Parameter;

// =============================================================================
// ASTノード
// =============================================================================

typedef struct ASTNode ASTNode;

struct ASTNode {
    NodeType type;
    SourceLocation location;
    
    union {
        // NODE_NUMBER
        double number_value;
        
        // NODE_STRING, NODE_IDENTIFIER
        char *string_value;
        
        // NODE_BOOL
        bool bool_value;
        
        // NODE_BINARY
        struct {
            TokenType operator;
            ASTNode *left;
            ASTNode *right;
        } binary;
        
        // NODE_UNARY
        struct {
            TokenType operator;
            ASTNode *operand;
        } unary;
        
        // NODE_CALL
        struct {
            ASTNode *callee;        // 呼び出し対象
            ASTNode **arguments;    // 引数の配列
            int arg_count;          // 引数の数
        } call;
        
        // NODE_INDEX
        struct {
            ASTNode *array;         // 配列
            ASTNode *index;         // インデックス
        } index;
        
        // NODE_MEMBER（将来用）
        struct {
            ASTNode *object;
            char *member_name;
        } member;
        
        // NODE_FUNCTION_DEF
        struct {
            char *name;             // 関数名
            Parameter *params;      // パラメータ配列
            int param_count;        // パラメータ数
            ValueType return_type;  // 戻り値の型
            bool has_return_type;   // 戻り値の型注釈があるか
            ASTNode *body;          // 関数本体（ブロック）
        } function;
        
        // NODE_VAR_DECL
        struct {
            char *name;             // 変数名
            ASTNode *initializer;   // 初期化式
            bool is_const;          // 定数かどうか
        } var_decl;
        
        // NODE_ASSIGN
        struct {
            ASTNode *target;        // 代入先
            TokenType operator;     // 演算子（=, +=, -= など）
            ASTNode *value;         // 値
        } assign;
        
        // NODE_IF
        struct {
            ASTNode *condition;     // 条件式
            ASTNode *then_branch;   // then節
            ASTNode *else_branch;   // else節（NULLの場合あり）
        } if_stmt;
        
        // NODE_WHILE
        struct {
            ASTNode *condition;     // 条件式
            ASTNode *body;          // ループ本体
        } while_stmt;
        
        // NODE_FOR
        struct {
            char *var_name;         // ループ変数名
            ASTNode *start;         // 開始値
            ASTNode *end;           // 終了値
            ASTNode *step;          // ステップ（NULLなら1）
            ASTNode *body;          // ループ本体
        } for_stmt;
        
        // NODE_RETURN
        struct {
            ASTNode *value;         // 戻り値（NULLの場合あり）
        } return_stmt;
        
        // NODE_BLOCK, NODE_PROGRAM, NODE_ARRAY
        struct {
            ASTNode **statements;   // 文の配列
            int count;              // 文の数
            int capacity;           // 配列の容量
        } block;
        
        // NODE_EXPR_STMT
        struct {
            ASTNode *expression;    // 式
        } expr_stmt;
    };
};

// =============================================================================
// ノード作成関数
// =============================================================================

/**
 * 基本ノードを作成
 */
ASTNode *node_new(NodeType type, int line, int column);

/**
 * 数値ノードを作成
 */
ASTNode *node_number(double value, int line, int column);

/**
 * 文字列ノードを作成
 */
ASTNode *node_string(const char *value, int line, int column);

/**
 * 真偽値ノードを作成
 */
ASTNode *node_bool(bool value, int line, int column);

/**
 * 識別子ノードを作成
 */
ASTNode *node_identifier(const char *name, int line, int column);

/**
 * nullノードを作成
 */
ASTNode *node_null(int line, int column);

/**
 * 二項演算ノードを作成
 */
ASTNode *node_binary(TokenType op, ASTNode *left, ASTNode *right, int line, int column);

/**
 * 単項演算ノードを作成
 */
ASTNode *node_unary(TokenType op, ASTNode *operand, int line, int column);

/**
 * 関数呼び出しノードを作成
 */
ASTNode *node_call(ASTNode *callee, ASTNode **args, int arg_count, int line, int column);

/**
 * インデックスアクセスノードを作成
 */
ASTNode *node_index(ASTNode *array, ASTNode *index, int line, int column);

/**
 * 配列リテラルノードを作成
 */
ASTNode *node_array(ASTNode **elements, int count, int line, int column);

/**
 * 関数定義ノードを作成
 */
ASTNode *node_function_def(const char *name, Parameter *params, int param_count,
                           ValueType return_type, bool has_return_type,
                           ASTNode *body, int line, int column);

/**
 * 変数宣言ノードを作成
 */
ASTNode *node_var_decl(const char *name, ASTNode *initializer, bool is_const,
                       int line, int column);

/**
 * 代入ノードを作成
 */
ASTNode *node_assign(ASTNode *target, TokenType op, ASTNode *value, int line, int column);

/**
 * if文ノードを作成
 */
ASTNode *node_if(ASTNode *condition, ASTNode *then_branch, ASTNode *else_branch,
                 int line, int column);

/**
 * while文ノードを作成
 */
ASTNode *node_while(ASTNode *condition, ASTNode *body, int line, int column);

/**
 * for文ノードを作成
 */
ASTNode *node_for(const char *var_name, ASTNode *start, ASTNode *end,
                  ASTNode *step, ASTNode *body, int line, int column);

/**
 * return文ノードを作成
 */
ASTNode *node_return(ASTNode *value, int line, int column);

/**
 * break文ノードを作成
 */
ASTNode *node_break(int line, int column);

/**
 * continue文ノードを作成
 */
ASTNode *node_continue(int line, int column);

/**
 * 式文ノードを作成
 */
ASTNode *node_expr_stmt(ASTNode *expression, int line, int column);

/**
 * ブロックノードを作成
 */
ASTNode *node_block(int line, int column);

/**
 * プログラムノードを作成
 */
ASTNode *node_program(int line, int column);

// =============================================================================
// ブロック操作
// =============================================================================

/**
 * ブロックに文を追加
 */
void block_add_statement(ASTNode *block, ASTNode *stmt);

// =============================================================================
// メモリ管理
// =============================================================================

/**
 * ノードを解放（再帰的）
 */
void node_free(ASTNode *node);

/**
 * パラメータ配列を解放
 */
void params_free(Parameter *params, int count);

// =============================================================================
// デバッグ
// =============================================================================

/**
 * ノード種別の名前を取得
 */
const char *node_type_name(NodeType type);

/**
 * ASTを表示（デバッグ用）
 */
void ast_print(ASTNode *node, int indent);

/**
 * ASTをJSON形式で出力
 */
void ast_to_json(ASTNode *node, int indent);

#endif // AST_H
