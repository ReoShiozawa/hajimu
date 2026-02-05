/**
 * 日本語プログラミング言語 - 環境（スコープ）実装
 */

#include "environment.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// ハッシュ関数
// =============================================================================

static uint32_t hash_string(const char *s) {
    uint32_t hash = 2166136261u;  // FNV-1a
    while (*s) {
        hash ^= (unsigned char)*s++;
        hash *= 16777619;
    }
    return hash;
}

// =============================================================================
// 環境の作成・解放
// =============================================================================

Environment *env_new(Environment *parent) {
    Environment *env = calloc(1, sizeof(Environment));
    env->parent = parent;
    env->depth = parent ? parent->depth + 1 : 0;
    
    for (int i = 0; i < ENV_HASH_SIZE; i++) {
        env->table[i] = NULL;
    }
    
    return env;
}

void env_free(Environment *env) {
    if (env == NULL) return;
    
    // エントリを解放
    for (int i = 0; i < ENV_HASH_SIZE; i++) {
        EnvEntry *entry = env->table[i];
        while (entry != NULL) {
            EnvEntry *next = entry->next;
            free(entry->name);
            value_free(&entry->value);
            free(entry);
            entry = next;
        }
    }
    
    free(env);
}

// =============================================================================
// 内部関数
// =============================================================================

static EnvEntry *find_entry_local(Environment *env, const char *name) {
    uint32_t index = hash_string(name) % ENV_HASH_SIZE;
    EnvEntry *entry = env->table[index];
    
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

static EnvEntry *find_entry(Environment *env, const char *name) {
    while (env != NULL) {
        EnvEntry *entry = find_entry_local(env, name);
        if (entry != NULL) {
            return entry;
        }
        env = env->parent;
    }
    
    return NULL;
}

// =============================================================================
// 変数操作
// =============================================================================

bool env_define(Environment *env, const char *name, Value value, bool is_const) {
    if (env == NULL || name == NULL) return false;
    
    // 既に同じスコープに存在する場合は更新
    EnvEntry *existing = find_entry_local(env, name);
    if (existing != NULL) {
        if (existing->is_const) {
            return false;  // 定数は再定義不可
        }
        value_free(&existing->value);
        existing->value = value;
        existing->is_const = is_const;
        return true;
    }
    
    // 新しいエントリを作成
    EnvEntry *entry = malloc(sizeof(EnvEntry));
    entry->name = strdup(name);
    entry->value = value;
    entry->is_const = is_const;
    
    // ハッシュテーブルに挿入
    uint32_t index = hash_string(name) % ENV_HASH_SIZE;
    entry->next = env->table[index];
    env->table[index] = entry;
    
    return true;
}

Value *env_get(Environment *env, const char *name) {
    EnvEntry *entry = find_entry(env, name);
    if (entry != NULL) {
        return &entry->value;
    }
    return NULL;
}

bool env_set(Environment *env, const char *name, Value value) {
    EnvEntry *entry = find_entry(env, name);
    
    if (entry == NULL) {
        return false;  // 変数が存在しない
    }
    
    if (entry->is_const) {
        return false;  // 定数には代入できない
    }
    
    value_free(&entry->value);
    entry->value = value;
    return true;
}

bool env_exists(Environment *env, const char *name) {
    return find_entry(env, name) != NULL;
}

bool env_is_const(Environment *env, const char *name) {
    EnvEntry *entry = find_entry(env, name);
    return entry != NULL && entry->is_const;
}

bool env_exists_local(Environment *env, const char *name) {
    return find_entry_local(env, name) != NULL;
}

// =============================================================================
// デバッグ
// =============================================================================

void env_print(Environment *env) {
    printf("=== Environment (depth=%d) ===\n", env->depth);
    
    for (int i = 0; i < ENV_HASH_SIZE; i++) {
        EnvEntry *entry = env->table[i];
        while (entry != NULL) {
            printf("  %s%s = ", 
                   entry->is_const ? "定数 " : "",
                   entry->name);
            value_print(entry->value);
            printf("\n");
            entry = entry->next;
        }
    }
    
    if (env->parent != NULL) {
        printf("--- Parent ---\n");
        env_print(env->parent);
    }
}
