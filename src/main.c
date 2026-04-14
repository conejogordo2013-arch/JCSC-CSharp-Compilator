#include "arena.h"
#include "codegen.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    puts("Uso: jccsc <archivo.cs> [-o salida.jccsc] [--run]");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    const char *input = argv[1];
    const char *output = "a.jccsc";
    CodegenOptions opt = {.output_path = output, .run_after_compile = false};

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { output = argv[++i]; opt.output_path = output; }
        else if (strcmp(argv[i], "--run") == 0) opt.run_after_compile = true;
        else { usage(); return 1; }
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
