#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdbool.h>
#include "ast.h"
#include "diag.h"

typedef struct CodegenOptions {
    const char *output_path;
    bool run_after_compile;
} CodegenOptions;

bool codegen_compile_and_run(Program *program,
                             const CodegenOptions *options,
                             DiagnosticList *diags);

#endif
