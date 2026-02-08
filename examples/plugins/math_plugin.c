/**
 * はじむ サンプルプラグイン — 数学ユーティリティ
 * 
 * コンパイル（全OS共通で .hjp を出力）:
 *   macOS:  gcc -shared -fPIC -I../../include -o math_plugin.hjp math_plugin.c
 *   Linux:  gcc -shared -fPIC -I../../include -o math_plugin.hjp math_plugin.c -lm
 *   Win:    gcc -shared -I../../include -o math_plugin.hjp math_plugin.c
 * 
 * 使い方（はじむ側）— 拡張子不要！:
 *   取り込む "math_plugin" として 数学P
 *   表示(数学P["二乗"](5))       // → 25
 *   表示(数学P["階乗"](6))       // → 720
 */

#include "hajimu_plugin.h"
#include <math.h>

// =============================================================================
// プラグイン関数の実装
// =============================================================================

/**
 * 二乗: n^2
 */
static Value fn_square(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return hajimu_null();
    return hajimu_number(argv[0].number * argv[0].number);
}

/**
 * 立方: n^3
 */
static Value fn_cube(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return hajimu_null();
    double n = argv[0].number;
    return hajimu_number(n * n * n);
}

/**
 * 階乗: n!
 */
static Value fn_factorial(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return hajimu_null();
    int n = (int)argv[0].number;
    if (n < 0) return hajimu_null();
    double result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return hajimu_number(result);
}

/**
 * フィボナッチ: 第n項
 */
static Value fn_fibonacci(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return hajimu_null();
    int n = (int)argv[0].number;
    if (n <= 0) return hajimu_number(0);
    if (n == 1) return hajimu_number(1);
    
    double a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        double tmp = a + b;
        a = b;
        b = tmp;
    }
    return hajimu_number(b);
}

/**
 * 最大公約数: GCD(a, b)
 */
static Value fn_gcd(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER) 
        return hajimu_null();
    
    int a = abs((int)argv[0].number);
    int b = abs((int)argv[1].number);
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return hajimu_number(a);
}

/**
 * 平方根
 */
static Value fn_sqrt(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return hajimu_null();
    return hajimu_number(sqrt(argv[0].number));
}

/**
 * 累乗: base^exp
 */
static Value fn_power(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER || argv[1].type != VALUE_NUMBER)
        return hajimu_null();
    return hajimu_number(pow(argv[0].number, argv[1].number));
}

/**
 * 絶対値
 */
static Value fn_abs(int argc, Value *argv) {
    (void)argc;
    if (argv[0].type != VALUE_NUMBER) return hajimu_null();
    return hajimu_number(fabs(argv[0].number));
}

// =============================================================================
// 関数テーブル
// =============================================================================

static HajimuPluginFunc functions[] = {
    {"二乗",           fn_square,    1, 1},
    {"立方",           fn_cube,      1, 1},
    {"階乗",           fn_factorial, 1, 1},
    {"フィボナッチ",     fn_fibonacci, 1, 1},
    {"GCD",            fn_gcd,       2, 2},
    {"平方根",         fn_sqrt,      1, 1},
    {"累乗",           fn_power,     2, 2},
    {"絶対値",         fn_abs,       1, 1},
};

// =============================================================================
// プラグイン初期化
// =============================================================================

HAJIMU_PLUGIN_EXPORT HajimuPluginInfo *hajimu_plugin_init(void) {
    static HajimuPluginInfo info = {
        .name           = "math_plugin",
        .version        = "1.0.0",
        .author         = "はじむ開発チーム",
        .description    = "数学ユーティリティ関数プラグイン",
        .functions      = functions,
        .function_count = sizeof(functions) / sizeof(functions[0]),
    };
    return &info;
}
