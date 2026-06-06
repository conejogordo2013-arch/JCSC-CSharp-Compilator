#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"
#include "diag.h"

void semantic_analyze(Program *program, DiagnosticList *diags);

#endif
