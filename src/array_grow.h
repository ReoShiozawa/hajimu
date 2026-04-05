#ifndef HAJIMU_ARRAY_GROW_H
#define HAJIMU_ARRAY_GROW_H

#include <stdio.h>
#include <stdlib.h>

#define ARRAY_GROW(ptr, count, cap, type, on_oom) \
    do { \
        if ((count) >= (cap)) { \
            size_t _array_grow_new_cap = (cap) ? (size_t)(cap) * 2u : 8u; \
            void *_array_grow_tmp = realloc((ptr), _array_grow_new_cap * sizeof(type)); \
            if (_array_grow_tmp == NULL) { \
                fprintf(stderr, "Out of memory in %s:%d\n", __FILE__, __LINE__); \
                on_oom; \
            } \
            (ptr) = _array_grow_tmp; \
            (cap) = _array_grow_new_cap; \
        } \
    } while (0)

#endif
