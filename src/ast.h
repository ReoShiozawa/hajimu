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
    NODE_IMPORT,            // 取り込み文
    NODE_CLASS_DEF,         // クラス定義
    NODE_METHOD_DEF,        // メソッド定義
    NODE_TRY,               // 試行文（try-catch-finally）
    NODE_THROW,             // 投げる文（throw）
    NODE_LAMBDA,            // 無名関数
    NODE_SWITCH,            // 選択文
    NODE_FOREACH,           // foreach文
    NODE_YIELD,             // 譲渡文（ジェネレータ）
    
    // 式
    NODE_BINARY,            // 二項演算
    NODE_UNARY,             // 単項演算
    NODE_CALL,              // 関数呼び出し
    NODE_INDEX,             // 配列インデックス
    NODE_MEMBER,            // メンバーアクセス
    NODE_NEW,               // インスタンス生成
    NODE_SELF,              // 自分参照
    
    // リテラル・識別子
    NODE_IDENTIFIER,        // 識別子
    NODE_NUMBER,            // 数値リテラル
    NODE_STRING,            // 文字列リテラル
    NODE_BOOL,              // 真偽値リテラル
    NODE_ARRAY,             // 配列リテラル
    NODE_DICT,              // 辞書リテラル
    NODE_NULL,              // null
    NODE_LIST_COMPREHENSION, // リスト内包表記
    
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
    bool is_variadic;       // 可変長引数か（*引数名）
    struct ASTNode *default_value;  // デフォルト値（NULLなら必須）
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
            bool is_generator;      // 生成関数かどうか
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
        
        // NODE_YIELD
        struct {
            ASTNode *value;         // 譲渡する値
        } yield_stmt;
        
        // NODE_BLOCK, NODE_PROGRAM, NODE_ARRAY
        struct {
            ASTNode **statements;   // 文の配列
            int count;              // 文の数
            int capacity;           // 配列の容量
        } block;
        
        // NODE_DICT
        struct {
            char **keys;            // キーの配列
            ASTNode **values;       // 値の配列
            int count;              // 要素数
        } dict;
        
        // NODE_IMPORT
        struct {
            char *module_path;      // インポートするファイルパス
            char *alias;            // 名前空間エイリアス（NULLなら直接取り込み）
        } import_stmt;
        
        // NODE_CLASS_DEF
        struct {
            char *name;             // クラス名
            char *parent_name;      // 親クラス名（NULLなら継承なし）
            ASTNode **methods;      // メソッドの配列
            int method_count;       // メソッド数
            ASTNode **static_methods;  // 静的メソッドの配列
            int static_method_count;   // 静的メソッド数
            ASTNode *init_method;   // 初期化メソッド（NULLの場合あり）
        } class_def;
        
        // NODE_METHOD_DEF（関数と同じ構造だが自分を受け取る）
        struct {
            char *name;             // メソッド名
            Parameter *params;      // パラメータ配列
            int param_count;        // パラメータ数
            ValueType return_type;  // 戻り値の型
            bool has_return_type;   // 戻り値の型注釈があるか
            ASTNode *body;          // メソッド本体（ブロック）
        } method;
        
        // NODE_NEW
        struct {
            char *class_name;       // クラス名
            ASTNode **arguments;    // 引数の配列
            int arg_count;          // 引数の数
        } new_expr;
        
        // NODE_TRY（試行-捕獲-最終）
        struct {
            ASTNode *try_block;     // 試行ブロック
            char *catch_var;        // 捕獲する変数名（エラー）
            ASTNode *catch_block;   // 捕獲ブロック（NULLの場合あり）
            ASTNode *finally_block; // 最終ブロック（NULLの場合あり）
        } try_stmt;
        
        // NODE_THROW（投げる）
        struct {
            ASTNode *expression;    // 投げる値
        } throw_stmt;
        
        // NODE_LAMBDA（無名関数）
        struct {
            Parameter *params;      // パラメータ配列
            int param_count;        // パラメータ数
            ASTNode *body;          // 本体
        } lambda;
        
        // NODE_SWITCH（選択文）
        struct {
            ASTNode *target;        // 選択対象
            ASTNode **case_values;  // 場合の値配列
            ASTNode **case_bodies;  // 場合の本体配列
            int case_count;         // 場合の数
            ASTNode *default_body;  // 既定の本体（NULLの場合あり）
        } switch_stmt;
        
        // NODE_FOREACH
        struct {
            char *var_name;         // ループ変数名（キー名）
            char *value_name;       // 値の変数名（辞書展開時、NULLなら通常foreach）
            ASTNode *iterable;      // 反復対象
            ASTNode *body;          // ループ本体
        } foreach_stmt;
        
        // NODE_EXPR_STMT
        struct {
            ASTNode *expression;    // 式
        } expr_stmt;
        
        // NODE_LIST_COMPREHENSION (リスト内包表記)
        struct {
            ASTNode *expression;    // 生成式（n * 2 など）
            char *var_name;         // ループ変数名
            ASTNode *iterable;      // 反復対象（配列など）
            ASTNode *condition;     // 条件式（NULLの場合は条件なし）
        } list_comp;
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
 * メンバーアクセスノードを作成
 */
ASTNode *node_member(ASTNode *object, const char *member_name, int line, int column);

/**
 * 配列リテラルノードを作成
 */
ASTNode *node_array(ASTNode **elements, int count, int line, int column);

/**
 * 辞書リテラルノードを作成
 */
ASTNode *node_dict(char **keys, ASTNode **values, int count, int line, int column);

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
 * import文ノードを作成
 */
ASTNode *node_import(const char *module_path, const char *alias, int line, int column);

/**
 * クラス定義ノードを作成
 */
ASTNode *node_class_def(const char *name, const char *parent_name, int line, int column);

/**
 * メソッド定義ノードを作成
 */
ASTNode *node_method_def(const char *name, int line, int column);

/**
 * メソッドにパラメータを追加
 */
void method_add_param(ASTNode *method, const char *name, ValueType type, bool has_type);

/**
 * クラスにメソッドを追加
 */
void class_add_method(ASTNode *class_node, ASTNode *method);
void class_add_static_method(ASTNode *class_node, ASTNode *method);

/**
 * new式ノードを作成
 */
ASTNode *node_new_expr(const char *class_name, int line, int column);

/**
 * self参照ノードを作成
 */
ASTNode *node_self(int line, int column);

/**
 * 試行文ノードを作成
 */
ASTNode *node_try(ASTNode *try_block, const char *catch_var, ASTNode *catch_block, ASTNode *finally_block, int line, int column);

/**
 * 投げる文ノードを作成
 */
ASTNode *node_throw(ASTNode *expression, int line, int column);

/**
 * ラムダ（無名関数）ノードを作成
 */
ASTNode *node_lambda(Parameter *params, int param_count, ASTNode *body, int line, int column);

/**
 * 選択文ノードを作成
 */
ASTNode *node_switch(ASTNode *target, int line, int column);

/**
 * 選択文に場合を追加
 */
void switch_add_case(ASTNode *switch_node, ASTNode *value, ASTNode *body);

/**
 * foreach文ノードを作成
 */
ASTNode *node_foreach(const char *var_name, ASTNode *iterable, ASTNode *body, int line, int column);

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

/**
 * リスト内包表記ノードを作成
 */
ASTNode *node_list_comprehension(ASTNode *expression, const char *var_name,
                                  ASTNode *iterable, ASTNode *condition,
                                  int line, int column);

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
