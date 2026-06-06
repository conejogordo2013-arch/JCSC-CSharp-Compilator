#include "vector.h"

#include <stdlib.h>
#include <string.h>

void vector_init(Vector *v, size_t item_size) {
    v->data = NULL;
    v->item_size = item_size;
    v->count = 0;
    v->capacity = 0;
}

void vector_push(Vector *v, const void *item) {
    if (v->count == v->capacity) {
        size_t new_cap = v->capacity == 0 ? 16 : v->capacity * 2;
        v->data = realloc(v->data, new_cap * v->item_size);
        v->capacity = new_cap;
    }
    memcpy((char *)v->data + v->count * v->item_size, item, v->item_size);
    v->count++;
}

void *vector_get(Vector *v, size_t index) {
    return (char *)v->data + index * v->item_size;
}

void vector_free(Vector *v) {
    free(v->data);
    v->data = NULL;
    v->count = 0;
    v->capacity = 0;
}
