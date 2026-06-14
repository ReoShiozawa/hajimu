/**
 * はじむ プラグイン開発用ヘッダー (hajimu_plugin.h)
 * 
 * C拡張プラグインを開発する際にインクルードするヘッダーファイル。
 * プラグインは統一拡張子 .hjp（Hajimu Plugin）でビルドし、
 * Windows/macOS/Linux 共通で使用できます。
 * 
 * === 使い方 ===
 * 
 * 1. このファイルをインクルード:
 *    #include "hajimu_plugin.h"
 * 
 * 2. プラグイン関数を定義:
 *    static Value my_func(int argc, Value *argv) {
 *        double x = argv[0].number;
 *        return value_number(x * x);
 *    }
 * 
 * 3. 関数テーブルを定義:
 *    static HajimuPluginFunc functions[] = {
 *        {"二乗", my_func, 1, 1},
 *    };
 * 
 * 4. プラグイン初期化関数を定義:
 *    HAJIMU_PLUGIN_EXPORT HajimuPluginInfo *hajimu_plugin_init(void) {
 *        static HajimuPluginInfo info = {
 *            .name        = "my_plugin",
 *            .version     = "1.0.0",
 *            .author      = "作者名",
 *            .description = "プラグインの説明",
 *            .functions   = functions,
 *            .function_count = sizeof(functions) / sizeof(functions[0]),
 *        };
 *        return &info;
 *    }
 * 
 * 5. コンパイル（全OS共通で .hjp を出力）:
 *    macOS:  gcc -shared -fPIC -o my_plugin.hjp my_plugin.c
 *    Linux:  gcc -shared -fPIC -o my_plugin.hjp my_plugin.c -lm
 *    Windows(MSVC): cl /LD /Fe:my_plugin.hjp my_plugin.c
 *    Windows(MinGW): gcc -shared -o my_plugin.hjp my_plugin.c
 * 
 * 6. はじむから使用（拡張子不要！）:
 *    取り込む "my_plugin" として プラグイン
 *    表示(プラグイン["二乗"](5))  // → 25
 */

#ifndef HAJIMU_PLUGIN_H
#define HAJIMU_PLUGIN_H

/* POSIX 関数 (strdup 等) を確実に利用可能にする */
#if !defined(_GNU_SOURCE) && !defined(_POSIX_C_SOURCE)
  #define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// =============================================================================
// エクスポートマクロ
// =============================================================================

#ifdef _WIN32
  #define HAJIMU_PLUGIN_EXPORT __declspec(dllexport)
#else
  #define HAJIMU_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// =============================================================================
// プラットフォーム検出マクロ
// =============================================================================

/* OS 判定 */
#if defined(_WIN32)
  #define HAJIMU_OS_WINDOWS 1
  #define HAJIMU_OS_NAME    "Windows"
#elif defined(__APPLE__)
  #define HAJIMU_OS_MACOS   1
  #define HAJIMU_OS_NAME    "macOS"
#elif defined(__linux__)
  #define HAJIMU_OS_LINUX   1
  #define HAJIMU_OS_NAME    "Linux"
#else
  #define HAJIMU_OS_NAME    "不明"
#endif

/* アーキテクチャ判定 */
#if defined(__aarch64__) || defined(__arm64__)
  #define HAJIMU_ARCH_ARM64 1
  #define HAJIMU_ARCH_NAME  "arm64"
#elif defined(__x86_64__) || defined(__amd64__)
  #define HAJIMU_ARCH_X64   1
  #define HAJIMU_ARCH_NAME  "x86-64"
#elif defined(__i386__)
  #define HAJIMU_ARCH_X86   1
  #define HAJIMU_ARCH_NAME  "x86"
#else
  #define HAJIMU_ARCH_NAME  "不明"
#endif

/* プラットフォーム別 .hjp 出力ファイル名サフィックス
 * 例: #define HJPNAME(base) base HAJIMU_HJP_SUFFIX ".hjp"
 *   → "engine_render-linux-x64.hjp"
 */
#if defined(HAJIMU_OS_WINDOWS)
  #if defined(HAJIMU_ARCH_ARM64)
    #define HAJIMU_HJP_SUFFIX "-windows-arm64"
  #else
    #define HAJIMU_HJP_SUFFIX "-windows-x64"
  #endif
#elif defined(HAJIMU_OS_MACOS)
  #if defined(HAJIMU_ARCH_ARM64)
    #define HAJIMU_HJP_SUFFIX "-macos-arm64"
  #else
    #define HAJIMU_HJP_SUFFIX "-macos"
  #endif
#else
  #if defined(HAJIMU_ARCH_ARM64)
    #define HAJIMU_HJP_SUFFIX "-linux-arm64"
  #else
    #define HAJIMU_HJP_SUFFIX "-linux-x64"
  #endif
#endif

// =============================================================================
// 値の型（value.h と同一定義）
// =============================================================================

typedef enum {
    VALUE_NULL          = 0,
    VALUE_NUMBER        = 1,
    VALUE_BOOL          = 2,
    VALUE_STRING        = 3,
    VALUE_ARRAY         = 4,
    VALUE_NUMERIC_ARRAY = 5,
    VALUE_MATRIX        = 6,
    VALUE_DICT          = 7,
    VALUE_FUNCTION      = 8,
    VALUE_BUILTIN       = 9,
    VALUE_CLASS         = 10,
    VALUE_INSTANCE      = 11,
    VALUE_GENERATOR     = 12,
} ValueType;

typedef enum {
    NUMERIC_DTYPE_F64  = 0,
    NUMERIC_DTYPE_F32  = 1,
    NUMERIC_DTYPE_I64  = 2,
    NUMERIC_DTYPE_I32  = 3,
    NUMERIC_DTYPE_BOOL = 4,
} NumericDType;

// 前方宣言
struct ASTNode;
struct Environment;
struct GeneratorState;

typedef struct Value Value;
typedef Value (*BuiltinFn)(int argc, Value *argv);

struct Value {
    ValueType type;
    bool is_const;
    bool is_integer;
    int ref_count;
    
    union {
        double number;
        bool boolean;
        
        struct {
            char *data;
            int byte_length;
            int char_length;
            int capacity;
        } string;
        
        struct {
            Value *elements;
            int length;
            int capacity;
        } array;

        struct {
            NumericDType dtype;
            void *data;
            int length;
            int capacity;
        } numeric_array;

        struct {
            NumericDType dtype;
            void *data;
            int rows;
            int cols;
            int row_stride;
            int col_stride;
            int offset;
            int *ref_count;
        } matrix;
        
        struct {
            char **keys;
            Value *values;
            int length;
            int capacity;
            int *hash_indices;
            int hash_capacity;
            bool hash_valid;
        } dict;
        
        struct {
            struct ASTNode *definition;
            struct Environment *closure;
        } function;
        
        struct {
            BuiltinFn fn;
            const char *name;
            int min_args;
            int max_args;
        } builtin;
        
        struct {
            char *name;
            struct ASTNode *definition;
            struct Value *parent;
        } class_value;
        
        struct {
            struct Value *class_ref;
            char **field_names;
            Value *fields;
            int field_count;
            int field_capacity;
        } instance;
        
        struct {
            struct GeneratorState *state;
        } generator;
    };
};

// =============================================================================
// プラグイン用ヘルパー関数（インライン）
// =============================================================================

/** NULL値を作成 */
static inline Value hajimu_null(void) {
    Value v;
    v.type = VALUE_NULL;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    return v;
}

/** 数値を作成 */
static inline Value hajimu_number(double n) {
    Value v;
    v.type = VALUE_NUMBER;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    v.number = n;
    return v;
}

/** 真偽値を作成 */
static inline Value hajimu_bool(bool b) {
    Value v;
    v.type = VALUE_BOOL;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    v.boolean = b;
    return v;
}

/** 文字列を作成（コピー） */
static inline Value hajimu_string(const char *s) {
    Value v;
    v.type = VALUE_STRING;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    if (s == NULL) {
        v.string.data = strdup("");
        v.string.byte_length = 0;
        v.string.char_length = 0;
    } else {
        v.string.data = strdup(s);
        v.string.byte_length = (int)strlen(s);
        v.string.char_length = v.string.byte_length;
    }
    v.string.capacity = v.string.byte_length + 1;
    return v;
}

/** 空の配列を作成 */
static inline Value hajimu_array(void) {
    Value v;
    v.type = VALUE_ARRAY;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    v.array.length = 0;
    v.array.capacity = 4;
    v.array.elements = (Value *)calloc(4, sizeof(Value));
    return v;
}

/** 数値ベクトルかどうか */
static inline bool hajimu_is_numeric_array(Value *v) {
    return v != NULL && v->type == VALUE_NUMERIC_ARRAY;
}

/** 数値行列かどうか */
static inline bool hajimu_is_matrix(Value *v) {
    return v != NULL && v->type == VALUE_MATRIX;
}

/** 数値ベクトルの f64 データポインタ（借用。保持したい場合はコピーすること） */
static inline double *hajimu_numeric_f64_data(Value *v) {
    return hajimu_is_numeric_array(v) && v->numeric_array.dtype == NUMERIC_DTYPE_F64
        ? (double *)v->numeric_array.data
        : NULL;
}

/** 数値ベクトルの raw データポインタ（dtype を確認してから扱うこと） */
static inline void *hajimu_numeric_raw_data(Value *v) {
    return hajimu_is_numeric_array(v) ? v->numeric_array.data : NULL;
}

/** 数値ベクトルの dtype */
static inline NumericDType hajimu_numeric_dtype(Value *v) {
    return hajimu_is_numeric_array(v) ? v->numeric_array.dtype : NUMERIC_DTYPE_F64;
}

/** 数値ベクトルの長さ */
static inline int hajimu_numeric_length(Value *v) {
    return hajimu_is_numeric_array(v) ? v->numeric_array.length : 0;
}

/** 数値行列の f64 データポインタ（row-major、借用） */
static inline double *hajimu_matrix_f64_data(Value *v) {
    return hajimu_is_matrix(v) && v->matrix.dtype == NUMERIC_DTYPE_F64
        ? (double *)v->matrix.data
        : NULL;
}

/** 数値行列の raw データポインタ（dtype を確認してから扱うこと） */
static inline void *hajimu_matrix_raw_data(Value *v) {
    return hajimu_is_matrix(v) ? v->matrix.data : NULL;
}

/** 数値行列の dtype */
static inline NumericDType hajimu_matrix_dtype(Value *v) {
    return hajimu_is_matrix(v) ? v->matrix.dtype : NUMERIC_DTYPE_F64;
}

/** 数値行列の行数・列数 */
static inline int hajimu_matrix_rows(Value *v) {
    return hajimu_is_matrix(v) ? v->matrix.rows : 0;
}

static inline int hajimu_matrix_cols(Value *v) {
    return hajimu_is_matrix(v) ? v->matrix.cols : 0;
}

static inline int hajimu_matrix_row_stride(Value *v) {
    return hajimu_is_matrix(v) ? v->matrix.row_stride : 0;
}

static inline int hajimu_matrix_col_stride(Value *v) {
    return hajimu_is_matrix(v) ? v->matrix.col_stride : 0;
}

static inline int hajimu_matrix_offset(Value *v) {
    return hajimu_is_matrix(v) ? v->matrix.offset : 0;
}

/** raw double 配列から数値ベクトルを作成（コピー） */
static inline Value hajimu_numeric_from_f64_copy(const double *data, int length) {
    Value v;
    v.type = VALUE_NUMERIC_ARRAY;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    v.numeric_array.dtype = NUMERIC_DTYPE_F64;
    v.numeric_array.length = length > 0 ? length : 0;
    v.numeric_array.capacity = v.numeric_array.length > 0 ? v.numeric_array.length : 1;
    v.numeric_array.data = calloc((size_t)v.numeric_array.capacity, sizeof(double));
    if (data != NULL && length > 0 && v.numeric_array.data != NULL) {
        memcpy(v.numeric_array.data, data, sizeof(double) * (size_t)length);
    }
    return v;
}

/** raw double 配列から数値行列を作成（row-major、コピー） */
static inline Value hajimu_matrix_from_f64_copy(const double *data, int rows, int cols) {
    Value v;
    v.type = VALUE_MATRIX;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    v.matrix.dtype = NUMERIC_DTYPE_F64;
    v.matrix.rows = rows > 0 ? rows : 0;
    v.matrix.cols = cols > 0 ? cols : 0;
    v.matrix.row_stride = v.matrix.cols;
    v.matrix.col_stride = 1;
    v.matrix.offset = 0;
    v.matrix.ref_count = (int *)malloc(sizeof(int));
    if (v.matrix.ref_count != NULL) {
        *v.matrix.ref_count = 1;
    }
    size_t count = (size_t)v.matrix.rows * (size_t)v.matrix.cols;
    v.matrix.data = count > 0 ? calloc(count, sizeof(double)) : NULL;
    if ((count > 0 && v.matrix.data == NULL) || v.matrix.ref_count == NULL) {
        free(v.matrix.data);
        free(v.matrix.ref_count);
        v.matrix.data = NULL;
        v.matrix.ref_count = NULL;
    }
    if (data != NULL && count > 0 && v.matrix.data != NULL) {
        memcpy(v.matrix.data, data, sizeof(double) * count);
    }
    return v;
}

/** 配列に要素を追加 */
static inline void hajimu_array_push(Value *arr, Value elem) {
    if (arr->type != VALUE_ARRAY) return;
    if (arr->array.length >= arr->array.capacity) {
        arr->array.capacity *= 2;
        arr->array.elements = (Value *)realloc(
            arr->array.elements, arr->array.capacity * sizeof(Value));
    }
    arr->array.elements[arr->array.length++] = elem;
}

/** 引数の型チェック */
static inline bool hajimu_check_type(Value *v, ValueType expected) {
    return v->type == expected;
}

/** 引数の数チェック */
static inline bool hajimu_check_argc(int argc, int expected) {
    if (argc != expected) {
        fprintf(stderr, "エラー: 引数の数が正しくありません（期待: %d, 実際: %d）\n",
                expected, argc);
        return false;
    }
    return true;
}

// =============================================================================
// プラグイン登録用の構造体
// =============================================================================

typedef struct {
    const char *name;           // はじむ側に公開する関数名（日本語OK）
    Value (*fn)(int, Value*);   // 関数ポインタ
    int min_args;               // 最小引数数
    int max_args;               // 最大引数数（-1で可変長）
} HajimuPluginFunc;

typedef struct {
    const char *name;           // プラグイン名
    const char *version;        // バージョン
    const char *author;         // 作者
    const char *description;    // 説明
    HajimuPluginFunc *functions;// 関数テーブル
    int function_count;         // 関数の数
} HajimuPluginInfo;

// =============================================================================
// ランタイムコールバック（プラグインからはじむ関数を呼び出す仕組み）
// =============================================================================

/**
 * はじむランタイム — インタプリタが提供するコールバック群。
 * プラグインは hajimu_call() を使って、はじむ側の関数を呼び出せる。
 *
 * 使い方:
 *   // ルートハンドラとして受け取ったはじむ関数を呼び出す
 *   Value result = hajimu_call(&callback_func, 1, &request_value);
 */
typedef struct {
    /** はじむ関数 (VALUE_FUNCTION / VALUE_BUILTIN) を呼び出す */
    Value (*call)(Value *func, int argc, Value *argv);
} HajimuRuntime;

/** グローバルランタイムポインタ（インタプリタが自動設定） */
static HajimuRuntime *__hajimu_runtime = NULL;

/**
 * ランタイム設定（インタプリタ側から呼ばれる）
 * プラグインは hajimu_plugin_set_runtime をエクスポートすることで
 * インタプリタからランタイムを受け取れる。
 */
HAJIMU_PLUGIN_EXPORT void hajimu_plugin_set_runtime(HajimuRuntime *rt);

/** はじむ関数を呼び出す便利マクロ */
static inline Value hajimu_call(Value *func, int argc, Value *argv) {
    if (__hajimu_runtime && __hajimu_runtime->call)
        return __hajimu_runtime->call(func, argc, argv);
    return hajimu_null();
}

/** ランタイムが利用可能かチェック */
static inline bool hajimu_runtime_available(void) {
    return __hajimu_runtime != NULL && __hajimu_runtime->call != NULL;
}

#endif // HAJIMU_PLUGIN_H
