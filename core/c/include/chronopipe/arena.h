#ifndef CHRONOPIPE_ARENA_H
#define CHRONOPIPE_ARENA_H

#include "chronopipe/types.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t *base;
    size_t capacity;
    size_t used;
} cp_arena_t;

void cp_arena_init(cp_arena_t *arena, void *memory, size_t bytes);
void cp_arena_reset(cp_arena_t *arena);
void *cp_arena_alloc(cp_arena_t *arena, size_t bytes, size_t alignment);
size_t cp_arena_remaining(const cp_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif
