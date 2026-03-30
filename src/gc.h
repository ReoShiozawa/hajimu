#ifndef GC_H
#define GC_H

#include <stdbool.h>

struct Environment;

typedef struct GCNode {
    struct GCNode *gc_next;
    struct GCNode *gc_prev;
    int gc_refs;
    bool gc_marked;
    bool gc_tracked;    // GCリストに登録済みかどうか
} GCNode;

typedef struct GC {
    GCNode head;
    int tracked_count;
    int threshold;
    int collections;
    int collected;
} GC;

extern GC *g_gc;

void gc_init(GC *gc);
void gc_shutdown(GC *gc);

void gc_track(GC *gc, struct Environment *env);
void gc_untrack(GC *gc, struct Environment *env);

int gc_collect(GC *gc);
void gc_stats(GC *gc);

#endif // GC_H
