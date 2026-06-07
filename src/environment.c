/**
 * 日本語プログラミング言語 - 環境（スコープ）実装
 */

#include "environment.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>

extern GC *g_gc;

// 非同期ワーカーが関数クロージャをコピー/解放するため、
// Environment の参照カウント更新はプロセス全体で直列化する。
static pthread_mutex_t g_env_ref_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    if (env == NULL) return NULL;

    env->parent = parent;
    env->depth = parent ? parent->depth + 1 : 0;
    env->ref_count = 1;
    
    for (int i = 0; i < ENV_HASH_SIZE; i++) {
        env->table[i] = NULL;
    }

    if (g_gc != NULL) {
        gc_track(g_gc, env);
    }
    
    return env;
}

void env_free(Environment *env) {
    if (env == NULL) return;

    if (g_gc != NULL) {
        gc_untrack(g_gc, env);
    }
    
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

void env_retain(Environment *env) {
    if (env == NULL) return;

    pthread_mutex_lock(&g_env_ref_mutex);
    env->ref_count++;
    pthread_mutex_unlock(&g_env_ref_mutex);
}

void env_release(Environment *env) {
    if (env == NULL) return;

    bool should_free = false;
    pthread_mutex_lock(&g_env_ref_mutex);
    env->ref_count--;
    if (env->ref_count <= 0) {
        should_free = true;
    }
    pthread_mutex_unlock(&g_env_ref_mutex);

    if (should_free) {
        env_free(env);
    }
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
    if (entry == NULL) return false;

    entry->name = strdup(name);
    if (entry->name == NULL) {
        free(entry);
        return false;
    }
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

static int min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int utf8_char_bytes(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int utf8_to_codepoints(const char *s, unsigned int *out, int max_count) {
    if (!s || !out || max_count <= 0) return 0;

    int count = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && count < max_count) {
        int n = utf8_char_bytes(*p);
        unsigned int cp = 0;

        if (n == 1 || p[1] == '\0') {
            cp = *p;
        } else if (n == 2 && p[1] != '\0') {
            cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
        } else if (n == 3 && p[1] != '\0' && p[2] != '\0') {
            cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        } else if (n == 4 && p[1] != '\0' && p[2] != '\0' && p[3] != '\0') {
            cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12)
                 | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        } else {
            cp = *p;
            n = 1;
        }

        out[count++] = cp;
        p += n;
    }

    return count;
}

static int edit_distance(const char *a, const char *b) {
    if (!a || !b) return INT_MAX;

    unsigned int acp[97];
    unsigned int bcp[97];
    int alen = utf8_to_codepoints(a, acp, 97);
    int blen = utf8_to_codepoints(b, bcp, 97);
    if (alen == 0) return blen;
    if (blen == 0) return alen;
    if (alen > 96 || blen > 96) return INT_MAX;

    int prev[97];
    int curr[97];

    for (int j = 0; j <= blen; j++) {
        prev[j] = j;
    }

    for (int i = 1; i <= alen; i++) {
        curr[0] = i;
        for (int j = 1; j <= blen; j++) {
            int cost = acp[i - 1] == bcp[j - 1] ? 0 : 1;
            curr[j] = min3(prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost);
        }
        for (int j = 0; j <= blen; j++) {
            prev[j] = curr[j];
        }
    }

    return prev[blen];
}

const char *env_find_similar(Environment *env, const char *name) {
    if (!env || !name || name[0] == '\0') return NULL;

    const char *best = NULL;
    int best_score = INT_MAX;

    for (Environment *scope = env; scope != NULL; scope = scope->parent) {
        for (int i = 0; i < ENV_HASH_SIZE; i++) {
            for (EnvEntry *entry = scope->table[i]; entry != NULL; entry = entry->next) {
                int score = edit_distance(name, entry->name);
                if (score < best_score) {
                    best_score = score;
                    best = entry->name;
                }
            }
        }
    }

    unsigned int cp[97];
    int len = utf8_to_codepoints(name, cp, 97);
    int threshold = len <= 4 ? 1 : 2;
    return best_score <= threshold ? best : NULL;
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
