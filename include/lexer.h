#ifndef LEXER_H
#define LEXER_H

#include "diag.h"
#include "token.h"
#include "vector.h"

void lex_source(const char *source, Vector *tokens, DiagnosticList *diags);

#endif
