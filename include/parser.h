#ifndef PARSER_H
#define PARSER_H

#include "arena.h"
#include "ast.h"
#include "diag.h"
#include "vector.h"

Program *parse_program(Arena *arena, Vector *tokens, DiagnosticList *diags);

#endif
