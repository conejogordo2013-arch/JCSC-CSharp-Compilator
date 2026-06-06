#include "arena.h"
#include "codegen.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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
    puts("     [--debug-trace] [--diagnostics-json] [--watch]");
    puts("     [--highlight] [--complete prefijo]");
    puts("     [--add-package Nombre@Version] [--list-packages]");
}

static void print_diagnostics_json(const DiagnosticList *diags) {
    printf("[");
    for (size_t i = 0; i < diags->count; ++i) {
        const Diagnostic *d = &diags->items[i];
        if (i) printf(",");
        printf("{\"line\":%d,\"column\":%d,\"message\":\"", d->span.line, d->span.column);
        for (const char *p = d->message; *p; ++p) {
            if (*p == '"' || *p == '\\') putchar('\\');
            putchar(*p);
        }
        printf("\"}");
    }
    printf("]\n");
}

static void syntax_highlight(const char *source, Vector *tokens, DiagnosticList *diags) {
    (void)diags;
    (void)source;
    for (size_t i = 0; i < tokens->count; ++i) {
        Token *t = (Token *)vector_get(tokens, i);
        if (t->kind == TOK_EOF) break;
        const char *color = "\x1b[0m";
        if (t->kind >= TOK_KW_CLASS && t->kind <= TOK_KW_THROW) color = "\x1b[35m";
        else if (t->kind == TOK_STRING_LITERAL) color = "\x1b[32m";
        else if (t->kind == TOK_INT_LITERAL) color = "\x1b[36m";
        else if (t->kind == TOK_IDENTIFIER) color = "\x1b[37m";
        printf("%s%s\x1b[0m ", color, t->lexeme);
    }
    printf("\n");
}

static void print_completions(const char *prefix, Vector *tokens) {
    const char *keywords[] = {"class","struct","interface","using","namespace","public","private","static","void","int","bool","string",
                              "return","if","else","for","foreach","in","while","do","switch","case","default","break","continue",
                              "new","async","await","true","false","null","try","catch","finally","throw"};
    size_t plen = strlen(prefix);
    for (size_t i = 0; i < sizeof(keywords)/sizeof(keywords[0]); ++i) {
        if (strncmp(keywords[i], prefix, plen) == 0) printf("%s\n", keywords[i]);
    }
    for (size_t i = 0; i < tokens->count; ++i) {
        Token *t = (Token *)vector_get(tokens, i);
        if (t->kind == TOK_IDENTIFIER && strncmp(t->lexeme, prefix, plen) == 0) {
            printf("%s\n", t->lexeme);
        }
    }
}

static int compile_once(const char *input, const CodegenOptions *opt, int diagnostics_json) {
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
    int rc = 0;
    if (diag_has_errors(&diags)) {
        diag_print(&diags, input, source);
        if (diagnostics_json) print_diagnostics_json(&diags);
        rc = 2;
    } else if (!codegen_compile_and_run(program, opt, &diags)) {
        diag_print(&diags, input, source);
        if (diagnostics_json) print_diagnostics_json(&diags);
        rc = 3;
    } else {
        printf("Compilacion OK. Artefacto jccsc en %s\n", opt->output_path);
    }
    for (size_t i = 0; i < tokens.count; ++i) free(((Token *)tokens.data)[i].lexeme);
    vector_free(&tokens);
    arena_free(&arena);
    diag_free(&diags);
    free(source);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    const char *input = argv[1];
    const char *output = "a.jccsc";
    bool output_explicit = false;
    CodegenOptions opt = {
        .output_path = output,
        .run_after_compile = false,
        .debug_trace = false,
        .backend = CODEGEN_BACKEND_VM,
        .target = detect_host_target(),
        .native_emit_asm_only = false
    };
    bool diagnostics_json = false;
    bool watch_mode = false;
    bool highlight_mode = false;
    const char *complete_prefix = NULL;
    const char *add_package = NULL;
    bool list_packages = false;

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
        } else if (strcmp(argv[i], "--debug-trace") == 0) {
            opt.debug_trace = true;
        } else if (strcmp(argv[i], "--diagnostics-json") == 0) {
            diagnostics_json = true;
        } else if (strcmp(argv[i], "--watch") == 0) {
            watch_mode = true;
        } else if (strcmp(argv[i], "--highlight") == 0) {
            highlight_mode = true;
        } else if (strcmp(argv[i], "--complete") == 0 && i + 1 < argc) {
            complete_prefix = argv[++i];
        } else if (strcmp(argv[i], "--add-package") == 0 && i + 1 < argc) {
            add_package = argv[++i];
        } else if (strcmp(argv[i], "--list-packages") == 0) {
            list_packages = true;
        } else {
            usage();
            return 1;
        }
    }

    if (opt.backend == CODEGEN_BACKEND_NATIVE && !output_explicit) {
        output = basename_no_ext(input);
        opt.output_path = output;
    }

    if (add_package) {
        FILE *f = fopen("jccsc.packages", "a");
        if (!f) {
            fprintf(stderr, "No se pudo escribir jccsc.packages\n");
            return 1;
        }
        fprintf(f, "%s\n", add_package);
        fclose(f);
        printf("Paquete agregado: %s\n", add_package);
        return 0;
    }
    if (list_packages) {
        FILE *f = fopen("jccsc.packages", "r");
        if (!f) {
            printf("(sin paquetes)\n");
            return 0;
        }
        char line[256];
        while (fgets(line, sizeof(line), f)) printf("%s", line);
        fclose(f);
        return 0;
    }

    char *source = read_file(input);
    if (!source) {
        fprintf(stderr, "No se pudo abrir %s\n", input);
        return 1;
    }
    DiagnosticList quick_diags;
    diag_init(&quick_diags);
    Vector quick_tokens;
    vector_init(&quick_tokens, sizeof(Token));
    lex_source(source, &quick_tokens, &quick_diags);
    if (highlight_mode) {
        syntax_highlight(source, &quick_tokens, &quick_diags);
        for (size_t i = 0; i < quick_tokens.count; ++i) free(((Token *)quick_tokens.data)[i].lexeme);
        vector_free(&quick_tokens);
        diag_free(&quick_diags);
        free(source);
        return 0;
    }
    if (complete_prefix) {
        print_completions(complete_prefix, &quick_tokens);
        for (size_t i = 0; i < quick_tokens.count; ++i) free(((Token *)quick_tokens.data)[i].lexeme);
        vector_free(&quick_tokens);
        diag_free(&quick_diags);
        free(source);
        return 0;
    }
    for (size_t i = 0; i < quick_tokens.count; ++i) free(((Token *)quick_tokens.data)[i].lexeme);
    vector_free(&quick_tokens);
    diag_free(&quick_diags);
    free(source);

    if (watch_mode) {
        struct stat st;
        time_t last_mtime = 0;
        for (;;) {
            if (stat(input, &st) == 0 && st.st_mtime != last_mtime) {
                last_mtime = st.st_mtime;
                (void)compile_once(input, &opt, diagnostics_json);
            }
            sleep(1);
        }
    }
    return compile_once(input, &opt, diagnostics_json);
}
