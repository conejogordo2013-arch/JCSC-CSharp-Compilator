#include "arena.h"
#include "codegen.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TargetArch detect_host_target(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return TARGET_ARCH_X86_64;
#elif defined(__i386__) || defined(_M_IX86)
    return TARGET_ARCH_X86_32;
#elif defined(__aarch64__)
    return TARGET_ARCH_ARM64;
#elif defined(__arm__) || defined(_M_ARM)
    return TARGET_ARCH_ARM32;
#else
    return TARGET_ARCH_X86_64;
#endif
}

static const char *basename_no_ext(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    static char out[256];
    size_t n = strlen(base);
    if (n >= sizeof(out)) n = sizeof(out) - 1;
    memcpy(out, base, n);
    out[n] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    if (out[0] == '\0') strcpy(out, "a.out");
    return out;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t read = fread(buf, 1, (size_t)sz, f);
    if (read != (size_t)sz) { fclose(f); free(buf); return NULL; }
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static void usage(void) {
    puts("Uso: jccsc <archivo.cs|archivo.cb> [-o salida] [--run] [--backend vm|native] [--target x86_64|x86_32|arm64|arm32] [--emit-asm]");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    const char *input = argv[1];
    const char *output = "a.jccsc";
    bool output_explicit = false;
    CodegenOptions opt = {
        .output_path = output,
        .run_after_compile = false,
        .backend = CODEGEN_BACKEND_VM,
        .target = detect_host_target(),
        .native_emit_asm_only = false
    };

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
            opt.output_path = output;
            output_explicit = true;
        } else if (strcmp(argv[i], "--run") == 0) {
            opt.run_after_compile = true;
        } else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            const char *backend = argv[++i];
            if (strcmp(backend, "vm") == 0) opt.backend = CODEGEN_BACKEND_VM;
            else if (strcmp(backend, "native") == 0) opt.backend = CODEGEN_BACKEND_NATIVE;
            else { usage(); return 1; }
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            const char *target = argv[++i];
            if (strcmp(target, "x86_64") == 0) opt.target = TARGET_ARCH_X86_64;
            else if (strcmp(target, "x86_32") == 0) opt.target = TARGET_ARCH_X86_32;
            else if (strcmp(target, "arm64") == 0) opt.target = TARGET_ARCH_ARM64;
            else if (strcmp(target, "arm32") == 0) opt.target = TARGET_ARCH_ARM32;
            else { usage(); return 1; }
        } else if (strcmp(argv[i], "--emit-asm") == 0) {
            opt.native_emit_asm_only = true;
        } else {
            usage();
            return 1;
        }
    }

    if (opt.backend == CODEGEN_BACKEND_NATIVE && !output_explicit) {
        output = basename_no_ext(input);
        opt.output_path = output;
    }

    char *source = read_file(input);
    if (!source) {
        fprintf(stderr, "No se pudo abrir %s\n", input);
        return 1;
    }

    DiagnosticList diags;
    diag_init(&diags);

    Vector tokens;
    vector_init(&tokens, sizeof(Token));
    lex_source(source, &tokens, &diags);

    Arena arena;
    arena_init(&arena);
    Program *program = parse_program(&arena, &tokens, &diags);
    semantic_analyze(program, &diags);

    if (diag_has_errors(&diags)) {
        diag_print(&diags, input, source);
        free(source);
        for (size_t i = 0; i < tokens.count; ++i) free(((Token *)tokens.data)[i].lexeme);
        vector_free(&tokens);
        arena_free(&arena);
        diag_free(&diags);
        return 2;
    }

    if (!codegen_compile_and_run(program, &opt, &diags)) {
        diag_print(&diags, input, source);
        free(source);
        for (size_t i = 0; i < tokens.count; ++i) free(((Token *)tokens.data)[i].lexeme);
        vector_free(&tokens);
        arena_free(&arena);
        diag_free(&diags);
        return 3;
    }

    printf("Compilacion OK. Artefacto jccsc en %s\n", output);

    free(source);
    for (size_t i = 0; i < tokens.count; ++i) free(((Token *)tokens.data)[i].lexeme);
    vector_free(&tokens);
    arena_free(&arena);
    diag_free(&diags);
    return 0;
}
