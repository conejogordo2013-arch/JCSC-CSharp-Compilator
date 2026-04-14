#ifndef DIAG_H
#define DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include "token.h"

typedef struct Diagnostic {
    Span span;
    char *message;
} Diagnostic;

typedef struct DiagnosticList {
    Diagnostic *items;
    size_t count;
    size_t capacity;
} DiagnosticList;

void diag_init(DiagnosticList *list);
void diag_report(DiagnosticList *list, Span span, const char *fmt, ...);
void diag_print(const DiagnosticList *list, const char *source_path, const char *source_text);
bool diag_has_errors(const DiagnosticList *list);
void diag_free(DiagnosticList *list);

#endif
