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
// 値の型（value.h と同一定義）
// =============================================================================

typedef enum {
    VALUE_NULL      = 0,
    VALUE_NUMBER    = 1,
    VALUE_BOOL      = 2,
    VALUE_STRING    = 3,
    VALUE_ARRAY     = 4,
    VALUE_DICT      = 5,
    VALUE_FUNCTION  = 6,
    VALUE_BUILTIN   = 7,
    VALUE_CLASS     = 8,
    VALUE_INSTANCE  = 9,
    VALUE_GENERATOR = 10,
} ValueType;

// 前方宣言
struct ASTNode;
struct Environment;
struct GeneratorState;

typedef struct Value Value;
typedef Value (*BuiltinFn)(int argc, Value *argv);

struct Value {
    ValueType type;
    bool is_const;
    int ref_count;
    
    union {
        double number;
        bool boolean;
        
        struct {
            char *data;
            int length;
            int capacity;
        } string;
        
        struct {
            Value *elements;
            int length;
            int capacity;
        } array;
        
        struct {
            char **keys;
            Value *values;
            int length;
            int capacity;
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
    v.ref_count = 0;
    return v;
}

/** 数値を作成 */
static inline Value hajimu_number(double n) {
    Value v;
    v.type = VALUE_NUMBER;
    v.is_const = false;
    v.ref_count = 0;
    v.number = n;
    return v;
}

/** 真偽値を作成 */
static inline Value hajimu_bool(bool b) {
    Value v;
    v.type = VALUE_BOOL;
    v.is_const = false;
    v.ref_count = 0;
    v.boolean = b;
    return v;
}

/** 文字列を作成（コピー） */
static inline Value hajimu_string(const char *s) {
    Value v;
    v.type = VALUE_STRING;
    v.is_const = false;
    v.ref_count = 0;
    if (s == NULL) {
        v.string.data = strdup("");
        v.string.length = 0;
    } else {
        v.string.data = strdup(s);
        v.string.length = (int)strlen(s);
    }
    v.string.capacity = v.string.length + 1;
    return v;
}

/** 空の配列を作成 */
static inline Value hajimu_array(void) {
    Value v;
    v.type = VALUE_ARRAY;
    v.is_const = false;
    v.ref_count = 0;
    v.array.length = 0;
    v.array.capacity = 4;
    v.array.elements = (Value *)calloc(4, sizeof(Value));
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
