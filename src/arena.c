#include "arena.h"

#include <stdlib.h>

#define ARENA_BLOCK_SIZE 4096

void arena_init(Arena *arena) {
    arena->head = NULL;
}

void *arena_alloc(Arena *arena, size_t size) {
    size = (size + 7u) & ~7u;
    if (!arena->head || arena->head->used + size > arena->head->capacity) {
        size_t cap = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
        ArenaBlock *block = malloc(sizeof(ArenaBlock) + cap);
        block->next = arena->head;
        block->capacity = cap;
        block->used = 0;
        arena->head = block;
    }
    void *ptr = arena->head->data + arena->head->used;
    arena->head->used += size;
    return ptr;
}

void arena_free(Arena *arena) {
    ArenaBlock *curr = arena->head;
    while (curr) {
        ArenaBlock *next = curr->next;
        free(curr);
        curr = next;
    }
    arena->head = NULL;
}
