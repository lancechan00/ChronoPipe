#include "chronopipe/arena.h"

#include <stdint.h>

void cp_arena_init(cp_arena_t *arena, void *memory, size_t bytes) {
    if (arena == NULL) return;
    arena->base = (uint8_t *)memory;
    arena->capacity = memory != NULL ? bytes : 0u;
    arena->used = 0u;
}
void cp_arena_reset(cp_arena_t *arena) {
    if (arena != NULL) arena->used = 0u;
}

void *cp_arena_alloc(cp_arena_t *arena, size_t bytes, size_t alignment) {
    uintptr_t current;
    uintptr_t aligned;
    size_t padding;
    if (arena == NULL || arena->base == NULL || bytes == 0u || alignment == 0u ||
        (alignment & (alignment - 1u)) != 0u) {
        return NULL;
    }
    current = (uintptr_t)(arena->base + arena->used);
    if (current > UINTPTR_MAX - (alignment - 1u)) return NULL;
    aligned = (current + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    padding = (size_t)(aligned - current);
    if (padding > arena->capacity - arena->used) return NULL;
    if (bytes > arena->capacity - arena->used - padding) return NULL;
    arena->used += padding + bytes;
    return (void *)aligned;
}

size_t cp_arena_remaining(const cp_arena_t *arena) {
    if (arena == NULL || arena->used > arena->capacity) return 0u;
    return arena->capacity - arena->used;
}
