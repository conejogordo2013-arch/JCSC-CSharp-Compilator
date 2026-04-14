#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>

typedef struct Vector {
    void *data;
    size_t item_size;
    size_t count;
    size_t capacity;
} Vector;

void vector_init(Vector *v, size_t item_size);
void vector_push(Vector *v, const void *item);
void *vector_get(Vector *v, size_t index);
void vector_free(Vector *v);

#endif
