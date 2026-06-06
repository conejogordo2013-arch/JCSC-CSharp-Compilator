#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_vprintf(const char *fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    char *buffer = malloc((size_t)needed + 1);
    vsnprintf(buffer, (size_t)needed + 1, fmt, ap);
    return buffer;
}

void diag_init(DiagnosticList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void diag_report(DiagnosticList *list, Span span, const char *fmt, ...) {
    const size_t max_diags = 512;
    if (list->count >= max_diags) return;
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = realloc(list->items, new_cap * sizeof(Diagnostic));
        list->capacity = new_cap;
    }
    va_list ap;
    va_start(ap, fmt);
    char *msg = dup_vprintf(fmt, ap);
    va_end(ap);

    if (list->count > 0) {
        Diagnostic *last = &list->items[list->count - 1];
        if (last->span.line == span.line &&
            last->span.column == span.column &&
            strcmp(last->message, msg) == 0) {
            free(msg);
            return;
        }
    }

    list->items[list->count++] = (Diagnostic){.span = span, .message = msg};
}

void diag_print(const DiagnosticList *list, const char *source_path, const char *source_text) {
    (void)source_text;
    for (size_t i = 0; i < list->count; ++i) {
        const Diagnostic *d = &list->items[i];
        fprintf(stderr, "%s:%d:%d: error: %s\n", source_path, d->span.line, d->span.column, d->message);
    }
}

bool diag_has_errors(const DiagnosticList *list) {
    return list->count > 0;
}

void diag_free(DiagnosticList *list) {
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].message);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}
