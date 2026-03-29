#include "gc.h"
#include "environment.h"
#include "value.h"
#include <stdio.h>
#include <stdlib.h>

typedef void (*ClosureVisitor)(Environment *env, void *ctx);

static bool gc_is_tracked(GC *gc, Environment *env) {
    (void)gc;
    if (env == NULL) return false;
    return env->gc_node.gc_tracked;
}

static void gc_visit_value_closures(Value *v, ClosureVisitor visitor, void *ctx) {
    if (v == NULL || visitor == NULL) return;

    switch (v->type) {
        case VALUE_FUNCTION:
            if (v->function.closure != NULL) {
                visitor(v->function.closure, ctx);
            }
            break;
        case VALUE_ARRAY:
            for (int i = 0; i < v->array.length; i++) {
                gc_visit_value_closures(&v->array.elements[i], visitor, ctx);
            }
            break;
        case VALUE_DICT:
            for (int i = 0; i < v->dict.length; i++) {
                gc_visit_value_closures(&v->dict.values[i], visitor, ctx);
            }
            break;
        case VALUE_INSTANCE:
            if (v->instance.class_ref != NULL) {
                gc_visit_value_closures(v->instance.class_ref, visitor, ctx);
            }
            for (int i = 0; i < v->instance.field_count; i++) {
                gc_visit_value_closures(&v->instance.fields[i], visitor, ctx);
            }
            break;
        case VALUE_CLASS:
            if (v->class_value.parent != NULL) {
                gc_visit_value_closures(v->class_value.parent, visitor, ctx);
            }
            break;
        case VALUE_GENERATOR:
            if (v->generator.state != NULL) {
                for (int i = 0; i < v->generator.state->length; i++) {
                    gc_visit_value_closures(&v->generator.state->values[i], visitor, ctx);
                }
            }
            break;
        default:
            break;
    }
}

static void gc_clear_function_closures(Value *v) {
    if (v == NULL) return;

    switch (v->type) {
        case VALUE_FUNCTION:
            v->function.closure = NULL;
            break;
        case VALUE_ARRAY:
            for (int i = 0; i < v->array.length; i++) {
                gc_clear_function_closures(&v->array.elements[i]);
            }
            break;
        case VALUE_DICT:
            for (int i = 0; i < v->dict.length; i++) {
                gc_clear_function_closures(&v->dict.values[i]);
            }
            break;
        case VALUE_INSTANCE:
            if (v->instance.class_ref != NULL) {
                gc_clear_function_closures(v->instance.class_ref);
            }
            for (int i = 0; i < v->instance.field_count; i++) {
                gc_clear_function_closures(&v->instance.fields[i]);
            }
            break;
        case VALUE_CLASS:
            if (v->class_value.parent != NULL) {
                gc_clear_function_closures(v->class_value.parent);
            }
            break;
        case VALUE_GENERATOR:
            if (v->generator.state != NULL) {
                for (int i = 0; i < v->generator.state->length; i++) {
                    gc_clear_function_closures(&v->generator.state->values[i]);
                }
            }
            break;
        default:
            break;
    }
}

static void gc_update_refs(GC *gc) {
    for (GCNode *node = gc->head.gc_next; node != &gc->head; node = node->gc_next) {
        Environment *env = (Environment *)node;
        node->gc_refs = env->ref_count;
        node->gc_marked = false;
    }
}

static void gc_subtract_closure(Environment *closure, void *ctx) {
    GC *gc = (GC *)ctx;
    if (!gc_is_tracked(gc, closure)) return;
    closure->gc_node.gc_refs--;
}

static void gc_subtract_internal_refs(GC *gc) {
    for (GCNode *node = gc->head.gc_next; node != &gc->head; node = node->gc_next) {
        Environment *env = (Environment *)node;

        // 親環境への参照も内部参照として減算
        if (env->parent != NULL && gc_is_tracked(gc, env->parent)) {
            env->parent->gc_node.gc_refs--;
        }

        for (int i = 0; i < ENV_HASH_SIZE; i++) {
            EnvEntry *entry = env->table[i];
            while (entry != NULL) {
                gc_visit_value_closures(&entry->value, gc_subtract_closure, gc);
                entry = entry->next;
            }
        }
    }
}

static void gc_mark_env(GC *gc, Environment *env);

static void gc_mark_closure(Environment *closure, void *ctx) {
    GC *gc = (GC *)ctx;
    gc_mark_env(gc, closure);
}

static void gc_mark_env(GC *gc, Environment *env) {
    if (!gc_is_tracked(gc, env)) return;
    if (env->gc_node.gc_marked) return;

    env->gc_node.gc_marked = true;
    for (int i = 0; i < ENV_HASH_SIZE; i++) {
        EnvEntry *entry = env->table[i];
        while (entry != NULL) {
            gc_visit_value_closures(&entry->value, gc_mark_closure, gc);
            entry = entry->next;
        }
    }
}

static void gc_mark_reachable(GC *gc) {
    for (GCNode *node = gc->head.gc_next; node != &gc->head; node = node->gc_next) {
        Environment *env = (Environment *)node;
        if (env->gc_node.gc_refs > 0) {
            gc_mark_env(gc, env);
        }
    }
}

static int gc_sweep(GC *gc) {
    if (gc->tracked_count <= 0) return 0;

    Environment **unreachable = malloc(sizeof(Environment *) * (size_t)gc->tracked_count);
    if (unreachable == NULL) return 0;

    int unreachable_count = 0;
    for (GCNode *node = gc->head.gc_next; node != &gc->head; node = node->gc_next) {
        Environment *env = (Environment *)node;
        if (!env->gc_node.gc_marked) {
            unreachable[unreachable_count++] = env;
        }
    }

    for (int i = 0; i < unreachable_count; i++) {
        Environment *env = unreachable[i];
        gc_untrack(gc, env);

        for (int j = 0; j < ENV_HASH_SIZE; j++) {
            EnvEntry *entry = env->table[j];
            while (entry != NULL) {
                EnvEntry *next = entry->next;
                free(entry->name);
                gc_clear_function_closures(&entry->value);
                value_free(&entry->value);
                free(entry);
                entry = next;
            }
        }
        free(env);
    }

    free(unreachable);
    return unreachable_count;
}

void gc_init(GC *gc) {
    if (gc == NULL) return;
    gc->head.gc_next = &gc->head;
    gc->head.gc_prev = &gc->head;
    gc->head.gc_refs = 0;
    gc->head.gc_marked = false;
    gc->tracked_count = 0;
    gc->threshold = 128;
    gc->collections = 0;
    gc->collected = 0;
}

void gc_shutdown(GC *gc) {
    if (gc == NULL) return;

    // 残存する追跡中Environmentを全て解放
    GCNode *node = gc->head.gc_next;
    while (node != &gc->head) {
        Environment *env = (Environment *)node;
        node = node->gc_next;

        // リストから外す
        env->gc_node.gc_prev->gc_next = env->gc_node.gc_next;
        env->gc_node.gc_next->gc_prev = env->gc_node.gc_prev;
        env->gc_node.gc_next = NULL;
        env->gc_node.gc_prev = NULL;
        env->gc_node.gc_tracked = false;

        // エントリを解放
        for (int j = 0; j < ENV_HASH_SIZE; j++) {
            EnvEntry *entry = env->table[j];
            while (entry != NULL) {
                EnvEntry *next = entry->next;
                free(entry->name);
                gc_clear_function_closures(&entry->value);
                value_free(&entry->value);
                free(entry);
                entry = next;
            }
        }
        free(env);
    }

    gc->head.gc_next = &gc->head;
    gc->head.gc_prev = &gc->head;
    gc->tracked_count = 0;
}

void gc_track(GC *gc, Environment *env) {
    if (gc == NULL || env == NULL) return;
    if (gc_is_tracked(gc, env)) return;

    GCNode *node = &env->gc_node;
    node->gc_next = gc->head.gc_next;
    node->gc_prev = &gc->head;
    gc->head.gc_next->gc_prev = node;
    gc->head.gc_next = node;
    node->gc_refs = 0;
    node->gc_marked = false;
    node->gc_tracked = true;
    gc->tracked_count++;
}

void gc_untrack(GC *gc, Environment *env) {
    if (gc == NULL || env == NULL) return;
    if (!gc_is_tracked(gc, env)) return;

    GCNode *node = &env->gc_node;
    node->gc_prev->gc_next = node->gc_next;
    node->gc_next->gc_prev = node->gc_prev;
    node->gc_next = NULL;
    node->gc_prev = NULL;
    node->gc_refs = 0;
    node->gc_marked = false;
    node->gc_tracked = false;
    if (gc->tracked_count > 0) {
        gc->tracked_count--;
    }
}

int gc_collect(GC *gc) {
    if (gc == NULL || gc->tracked_count <= 0) return 0;

    gc_update_refs(gc);
    gc_subtract_internal_refs(gc);
    gc_mark_reachable(gc);

    int collected = gc_sweep(gc);
    gc->collections++;
    gc->collected += collected;
    return collected;
}

void gc_stats(GC *gc) {
    if (gc == NULL) return;
    printf("[GC] tracked=%d threshold=%d collections=%d collected=%d\n",
           gc->tracked_count, gc->threshold, gc->collections, gc->collected);
}
