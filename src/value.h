/**
 * 日本語プログラミング言語 - 値システムヘッダー
 * 
 * 実行時の値を表現する型システム
 */

#ifndef VALUE_H
#define VALUE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// 前方宣言
struct ASTNode;
struct Environment;

// ジェネレータの共有状態
//
// ジェネレータ Value はコピー時にこの state を共有します。
// そのため、生成済みの値列・読み取り位置・完了状態の所有権は
// Value.ref_count ではなく GeneratorState.ref_count が管理します。
typedef struct GeneratorState {
    struct Value *values;       // yield された値の配列
    int length;                 // 値の数
    int capacity;               // 容量
    int index;                  // 現在の読み取り位置
    bool done;                  // 完了フラグ
    int ref_count;              // state を共有するジェネレータ Value 数
} GeneratorState;

// =============================================================================
// 値の型
// =============================================================================

typedef enum {
    VALUE_NULL,         // null値
    VALUE_NUMBER,       // 数値（double）
    VALUE_BOOL,         // 真偽値
    VALUE_STRING,       // 文字列
    VALUE_ARRAY,        // 配列
    VALUE_DICT,         // 辞書（ハッシュマップ）
    VALUE_FUNCTION,     // ユーザー定義関数
    VALUE_BUILTIN,      // 組み込み関数
    VALUE_CLASS,        // クラス定義
    VALUE_INSTANCE,     // クラスインスタンス
    VALUE_GENERATOR,    // ジェネレータ
} ValueType;

// =============================================================================
// 値構造体
// =============================================================================

typedef struct Value Value;

// 組み込み関数の型
typedef Value (*BuiltinFn)(int argc, Value *argv);

struct Value {
    ValueType type;
    bool is_const;      // 定数フラグ
    bool is_integer;    // VALUE_NUMBER が整数値として扱えるか
    int ref_count;      // 参照カウント
    
    union {
        // 数値
        double number;
        
        // 真偽値
        bool boolean;
        
        // 文字列
        struct {
            char *data;       // UTF-8 バイト列
            int byte_length;   // バイト数（メモリ管理・I/O 用）
            int char_length;   // Unicode 文字数（長さ()・添字用）
            int capacity;      // バッファ容量（バイト）
        } string;
        
        // 配列
        struct {
            Value *elements;
            int length;
            int capacity;
        } array;
        
        // 辞書（ハッシュマップ）
        struct {
            char **keys;
            Value *values;
            int length;
            int capacity;
        } dict;
        
        // ユーザー定義関数
        struct {
            struct ASTNode *definition;     // 関数定義AST
            struct Environment *closure;     // クロージャ環境
        } function;
        
        // 組み込み関数
        struct {
            BuiltinFn fn;
            const char *name;
            int min_args;
            int max_args;  // -1 = 可変長
        } builtin;
        
        // クラス定義
        struct {
            char *name;                     // クラス名
            struct ASTNode *definition;     // クラス定義AST
            struct Value *parent;           // 親クラス（NULLなら継承なし）
        } class_value;
        
        // クラスインスタンス
        struct {
            struct Value *class_ref;        // クラスへの参照
            char **field_names;             // フィールド名
            Value *fields;                  // フィールド値
            int field_count;                // フィールド数
            int field_capacity;             // 容量
        } instance;
        
        // ジェネレータ
        struct {
            struct GeneratorState *state;   // 共有状態へのポインタ
        } generator;
    };
};

// =============================================================================
// 値の作成
// =============================================================================

/**
 * NULL値を作成
 */
Value value_null(void);

/**
 * 数値を作成
 */
Value value_number(double n);

/**
 * 真偽値を作成
 */
Value value_bool(bool b);

/**
 * 文字列を作成（コピーする）
 */
Value value_string(const char *s);

/**
 * 文字列を作成（長さ指定）
 */
Value value_string_n(const char *s, int length);

/**
 * 空の配列を作成
 */
Value value_array(void);

/**
 * 配列を作成（初期容量指定）
 */
Value value_array_with_capacity(int capacity);

/**
 * ユーザー定義関数を作成
 */
Value value_function(struct ASTNode *definition, struct Environment *closure);

/**
 * 組み込み関数を作成
 */
Value value_builtin(BuiltinFn fn, const char *name, int min_args, int max_args);

/**
 * クラス値を作成
 */
Value value_class(const char *name, struct ASTNode *definition, Value *parent);

/**
 * インスタンス値を作成
 */
Value value_instance(Value *class_ref);

/**
 * ジェネレータ値を作成
 */
Value value_generator(void);

/**
 * ジェネレータに値を追加
 */
void generator_add_value(Value *gen, Value val);

/**
 * インスタンスにフィールドを設定
 */
void instance_set_field(Value *instance, const char *name, Value value);

/**
 * インスタンスからフィールドを取得
 */
Value *instance_get_field(Value *instance, const char *name);

/**
 * 空の辞書を作成
 */
Value value_dict(void);

/**
 * 辞書を作成（初期容量指定）
 */
Value value_dict_with_capacity(int capacity);

// =============================================================================
// 値の操作
// =============================================================================

/**
 * 値をコピー
 *
 * 文字列・配列・辞書・インスタンスは独立した値としてコピーします。
 * ジェネレータは state を共有し、GeneratorState.ref_count を増やします。
 */
Value value_copy(Value v);

/**
 * 値を即時解放します。
 *
 * ref_count を確認せず、呼び出し元が所有している 1 つの Value ハンドルを
 * 破棄します。通常の参照カウント管理が必要な場合は value_release() を
 * 使用してください。
 *
 * ジェネレータの場合は共有 state の参照数だけを減らし、最後のハンドルで
 * state 本体を解放します。
 */
void value_free(Value *v);

/**
 * 参照カウントを増加
 */
void value_retain(Value *v);

/**
 * 参照カウントを減少し、0 になったら value_free() で解放します。
 *
 * value_retain() した Value はこちらで対応する release を行います。
 */
void value_release(Value *v);

// =============================================================================
// 配列操作
// =============================================================================

/**
 * 配列に要素を追加
 */
void array_push(Value *array, Value element);

/**
 * 配列から要素を取得
 */
Value array_get(Value *array, int index);

/**
 * 配列の要素を設定
 */
bool array_set(Value *array, int index, Value element);

/**
 * 配列から最後の要素を削除して返す
 */
Value array_pop(Value *array);

/**
 * 配列の長さを取得
 */
int array_length(Value *array);

// =============================================================================
// 辞書操作
// =============================================================================

/**
 * 辞書に要素を設定
 */
bool dict_set(Value *dict, const char *key, Value value);

/**
 * 辞書から要素を取得
 */
Value dict_get(Value *dict, const char *key);

/**
 * 辞書から要素を削除
 */
bool dict_delete(Value *dict, const char *key);

/**
 * 辞書にキーが存在するか
 */
bool dict_has(Value *dict, const char *key);

/**
 * 辞書のキー一覧を取得
 */
Value dict_keys(Value *dict);

/**
 * 辞書の値一覧を取得
 */
Value dict_values(Value *dict);

/**
 * 辞書の長さを取得
 */
int dict_length(Value *dict);

// =============================================================================
// 文字列操作
// =============================================================================

/**
 * 文字列を連結
 */
Value string_concat(Value a, Value b);

/**
 * 文字列の長さを取得（文字数）
 */
int string_length(Value *s);

/**
 * 部分文字列を取得
 */
Value string_substring(Value *s, int start, int end);

// =============================================================================
// 型変換・判定
// =============================================================================

/**
 * 真偽値として評価
 */
bool value_is_truthy(Value v);

/**
 * 型名を取得
 */
const char *value_type_name(ValueType type);

/**
 * 値の実行時型名を取得。
 * VALUE_NUMBER は is_integer に応じて「整数」/「数値」を返す。
 */
const char *value_runtime_type_name(Value v);

/**
 * 値を文字列に変換
 */
char *value_to_string(Value v);

/**
 * 値を数値に変換
 */
Value value_to_number(Value v);

/**
 * 値が等しいか比較
 */
bool value_equals(Value a, Value b);

/**
 * 値を比較（<0, 0, >0）
 */
int value_compare(Value a, Value b);

// =============================================================================
// デバッグ
// =============================================================================

/**
 * 値をデバッグ出力
 */
void value_print(Value v);

/**
 * 値を詳細にデバッグ出力
 */
void value_debug(Value v);

#endif // VALUE_H
