#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdbool.h>
#include "ast.h"
#include "diag.h"

typedef enum CodegenBackend {
    CODEGEN_BACKEND_VM = 0,
    CODEGEN_BACKEND_NATIVE,
} CodegenBackend;

typedef enum TargetArch {
    TARGET_ARCH_X86_64 = 0,
    TARGET_ARCH_X86_32,
    TARGET_ARCH_ARM64,
    TARGET_ARCH_ARM32,
} TargetArch;

typedef struct CodegenOptions {
    const char *output_path;
    bool run_after_compile;
    bool debug_trace;
    CodegenBackend backend;
    TargetArch target;
    bool native_emit_asm_only;
} CodegenOptions;

bool codegen_compile_and_run(Program *program,
                             const CodegenOptions *options,
                             DiagnosticList *diags);

#endif
