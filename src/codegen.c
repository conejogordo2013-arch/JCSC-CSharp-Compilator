#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum RuntimeValueKind {
    RV_INT,
    RV_BOOL,
    RV_STRING,
    RV_VOID,
} RuntimeValueKind;

typedef struct RuntimeValue {
    RuntimeValueKind kind;
    int int_value;
    const char *string_value;
} RuntimeValue;

typedef struct VarSlot {
    const char *name;
    RuntimeValue value;
} VarSlot;

typedef struct Frame {
    VarSlot vars[512];
    size_t count;
    struct Frame *parent;
} Frame;

typedef struct ExecContext {
    Program *program;
    DiagnosticList *diags;
    int did_return;
    int did_break;
    int did_continue;
    RuntimeValue return_value;
    const char *current_class;
} ExecContext;

static void frame_init(Frame *f, Frame *parent) { f->count = 0; f->parent = parent; }

static RuntimeValue rv_int(int v) { return (RuntimeValue){.kind = RV_INT, .int_value = v}; }
static RuntimeValue rv_bool(int v) { return (RuntimeValue){.kind = RV_BOOL, .int_value = v ? 1 : 0}; }
static RuntimeValue rv_string(const char *s) { return (RuntimeValue){.kind = RV_STRING, .string_value = s}; }
static RuntimeValue rv_void(void) { return (RuntimeValue){.kind = RV_VOID}; }

static VarSlot *frame_find(Frame *f, const char *name) {
    for (Frame *it = f; it; it = it->parent) {
        for (size_t i = 0; i < it->count; ++i) {
            if (strcmp(it->vars[i].name, name) == 0) return &it->vars[i];
        }
    }
    return NULL;
}

static VarSlot *frame_find_current(Frame *f, const char *name) {
    for (size_t i = 0; i < f->count; ++i) {
        if (strcmp(f->vars[i].name, name) == 0) return &f->vars[i];
    }
    return NULL;
}

static void frame_define(Frame *f, const char *name, RuntimeValue v) {
    VarSlot *slot = frame_find_current(f, name);
    if (slot) {
        slot->value = v;
        return;
    }
    if (f->count < 512) f->vars[f->count++] = (VarSlot){.name = name, .value = v};
}

static void frame_assign(Frame *f, const char *name, RuntimeValue v) {
    VarSlot *slot = frame_find(f, name);
    if (slot) {
        slot->value = v;
        return;
    }
    frame_define(f, name, v);
}

static MethodDecl *find_method(ExecContext *ctx, const char *class_name, const char *method_name) {
    for (size_t ci = 0; ci < ctx->program->class_count; ++ci) {
        ClassDecl *c = &ctx->program->classes[ci];
        if (strcmp(c->name, class_name) != 0) continue;
        for (size_t mi = 0; mi < c->method_count; ++mi) {
            if (strcmp(c->methods[mi].name, method_name) == 0) return &c->methods[mi];
        }
    }
    return NULL;
}

static RuntimeValue eval_expr(ExecContext *ctx, Frame *frame, Expr *e);
static void exec_stmt(ExecContext *ctx, Frame *frame, Stmt *s);

static int truthy(RuntimeValue v) {
    if (v.kind == RV_INT) return v.int_value != 0;
    if (v.kind == RV_BOOL) return v.int_value != 0;
    if (v.kind == RV_STRING) return v.string_value && v.string_value[0] != '\0';
    return 0;
}

static RuntimeValue eval_binary(TokenKind op, RuntimeValue a, RuntimeValue b) {
    int ai = a.kind == RV_INT ? a.int_value : 0;
    int bi = b.kind == RV_INT ? b.int_value : 0;
    switch (op) {
        case TOK_PLUS: return rv_int(ai + bi);
        case TOK_MINUS: return rv_int(ai - bi);
        case TOK_STAR: return rv_int(ai * bi);
        case TOK_SLASH: return rv_int(bi == 0 ? 0 : ai / bi);
        case TOK_PERCENT: return rv_int(bi == 0 ? 0 : ai % bi);
        case TOK_EQ: return rv_bool(ai == bi);
        case TOK_NEQ: return rv_bool(ai != bi);
        case TOK_LT: return rv_bool(ai < bi);
        case TOK_LE: return rv_bool(ai <= bi);
        case TOK_GT: return rv_bool(ai > bi);
        case TOK_GE: return rv_bool(ai >= bi);
        case TOK_AND: return rv_bool(truthy(a) && truthy(b));
        case TOK_OR: return rv_bool(truthy(a) || truthy(b));
        default: return rv_int(0);
    }
}

static RuntimeValue call_method(ExecContext *ctx, const char *class_name, const char *method_name, ExprList args, Frame *caller) {
    if (strcmp(class_name, "Console") == 0 && strcmp(method_name, "WriteLine") == 0 && args.count == 1) {
        RuntimeValue v = eval_expr(ctx, caller, args.items[0]);
        if (v.kind == RV_STRING) printf("%s\n", v.string_value ? v.string_value : "");
        else if (v.kind == RV_BOOL) printf("%s\n", v.int_value ? "true" : "false");
        else printf("%d\n", v.int_value);
        return rv_void();
    }

    MethodDecl *m = find_method(ctx, class_name, method_name);
    if (!m) {
        diag_report(ctx->diags, (Span){0}, "metodo no encontrado: %s.%s", class_name, method_name);
        return rv_void();
    }

    Frame local;
    frame_init(&local, NULL);
    for (size_t i = 0; i < m->param_count && i < args.count; ++i) {
        RuntimeValue arg = eval_expr(ctx, caller, args.items[i]);
        frame_define(&local, m->params[i].name, arg);
    }

    int prev_return = ctx->did_return;
    int prev_break = ctx->did_break;
    int prev_continue = ctx->did_continue;
    RuntimeValue prev_value = ctx->return_value;
    ctx->did_return = 0;
    ctx->did_break = 0;
    ctx->did_continue = 0;
    ctx->return_value = rv_void();
    const char *prev_class = ctx->current_class;
    ctx->current_class = class_name;

    exec_stmt(ctx, &local, m->body);

    RuntimeValue out = ctx->return_value;
    if (!ctx->did_return && m->return_type.kind == TYPE_INT) out = rv_int(0);

    ctx->did_return = prev_return;
    ctx->did_break = prev_break;
    ctx->did_continue = prev_continue;
    ctx->return_value = prev_value;
    ctx->current_class = prev_class;
    return out;
}

static RuntimeValue eval_expr(ExecContext *ctx, Frame *frame, Expr *e) {
    switch (e->kind) {
        case EXPR_INT: return rv_int(e->as.int_value);
        case EXPR_BOOL: return rv_bool(e->as.bool_value);
        case EXPR_STRING: return rv_string(e->as.string_value);
        case EXPR_IDENTIFIER: {
            VarSlot *slot = frame_find(frame, e->as.identifier);
            if (!slot) return rv_int(0);
            return slot->value;
        }
        case EXPR_ASSIGN: {
            if (e->as.assign.target->kind != EXPR_IDENTIFIER) return rv_void();
            RuntimeValue v = eval_expr(ctx, frame, e->as.assign.value);
            frame_assign(frame, e->as.assign.target->as.identifier, v);
            return v;
        }
        case EXPR_UNARY: {
            RuntimeValue v = eval_expr(ctx, frame, e->as.unary.operand);
            if (e->as.unary.op == TOK_NOT) return rv_bool(!truthy(v));
            if (e->as.unary.op == TOK_MINUS) return rv_int(-v.int_value);
            return v;
        }
        case EXPR_BINARY: {
            RuntimeValue l = eval_expr(ctx, frame, e->as.binary.left);
            RuntimeValue r = eval_expr(ctx, frame, e->as.binary.right);
            return eval_binary(e->as.binary.op, l, r);
        }
        case EXPR_MEMBER:
            return rv_void();
        case EXPR_CALL: {
            Expr *callee = e->as.call.callee;
            if (callee->kind == EXPR_MEMBER && callee->as.member.object->kind == EXPR_IDENTIFIER) {
                return call_method(ctx,
                                   callee->as.member.object->as.identifier,
                                   callee->as.member.member,
                                   e->as.call.args,
                                   frame);
            }
            if (callee->kind == EXPR_IDENTIFIER) {
                return call_method(ctx, ctx->current_class, callee->as.identifier, e->as.call.args, frame);
            }
            return rv_void();
        }
    }
    return rv_void();
}

static void exec_stmt(ExecContext *ctx, Frame *frame, Stmt *s) {
    if (ctx->did_return || ctx->did_break || ctx->did_continue) return;
    switch (s->kind) {
        case STMT_BLOCK: {
            Frame inner;
            frame_init(&inner, frame);
            for (size_t i = 0; i < s->as.block.count; ++i) {
                exec_stmt(ctx, &inner, s->as.block.items[i]);
                if (ctx->did_return || ctx->did_break || ctx->did_continue) break;
            }
            break;
        }
        case STMT_VAR: {
            RuntimeValue v = rv_int(0);
            if (s->as.var.initializer) v = eval_expr(ctx, frame, s->as.var.initializer);
            frame_define(frame, s->as.var.name, v);
            break;
        }
        case STMT_EXPR:
            (void)eval_expr(ctx, frame, s->as.expr);
            break;
        case STMT_IF: {
            RuntimeValue c = eval_expr(ctx, frame, s->as.if_stmt.condition);
            if (truthy(c)) exec_stmt(ctx, frame, s->as.if_stmt.then_branch);
            else if (s->as.if_stmt.else_branch) exec_stmt(ctx, frame, s->as.if_stmt.else_branch);
            break;
        }
        case STMT_WHILE:
            while (!ctx->did_return && truthy(eval_expr(ctx, frame, s->as.while_stmt.condition))) {
                exec_stmt(ctx, frame, s->as.while_stmt.body);
                if (ctx->did_break) {
                    ctx->did_break = 0;
                    break;
                }
                if (ctx->did_continue) {
                    ctx->did_continue = 0;
                    continue;
                }
            }
            break;
        case STMT_FOR: {
            Frame loop;
            frame_init(&loop, frame);
            if (s->as.for_stmt.initializer) exec_stmt(ctx, &loop, s->as.for_stmt.initializer);
            while (!ctx->did_return) {
                if (s->as.for_stmt.condition) {
                    RuntimeValue c = eval_expr(ctx, &loop, s->as.for_stmt.condition);
                    if (!truthy(c)) break;
                }
                exec_stmt(ctx, &loop, s->as.for_stmt.body);
                if (ctx->did_return) break;
                if (ctx->did_break) {
                    ctx->did_break = 0;
                    break;
                }
                if (s->as.for_stmt.increment) (void)eval_expr(ctx, &loop, s->as.for_stmt.increment);
                if (ctx->did_continue) {
                    ctx->did_continue = 0;
                    continue;
                }
            }
            break;
        }
        case STMT_RETURN:
            ctx->return_value = s->as.return_expr ? eval_expr(ctx, frame, s->as.return_expr) : rv_void();
            ctx->did_return = 1;
            break;
        case STMT_BREAK:
            ctx->did_break = 1;
            break;
        case STMT_CONTINUE:
            ctx->did_continue = 1;
            break;
    }
}

static bool emit_jccsc_binary(Program *program, const char *path, DiagnosticList *diags) {
    FILE *f = fopen(path, "w");
    if (!f) {
        diag_report(diags, (Span){0}, "no se pudo escribir salida compilada: %s", path);
        return false;
    }
    fputs("JCCSC-BC-1\n", f);
    for (size_t ci = 0; ci < program->class_count; ++ci) {
        ClassDecl *c = &program->classes[ci];
        fprintf(f, "CLASS %s\n", c->name);
        for (size_t mi = 0; mi < c->method_count; ++mi) {
            MethodDecl *m = &c->methods[mi];
            fprintf(f, "  METHOD %s params=%zu\n", m->name, m->param_count);
        }
    }
    fclose(f);
    return true;
}

static bool run_program(Program *program, DiagnosticList *diags) {
    ExecContext ctx = {
        .program = program,
        .diags = diags,
        .did_return = 0,
        .did_break = 0,
        .did_continue = 0,
        .return_value = rv_void(),
        .current_class = "Program"
    };
    MethodDecl *entry = find_method(&ctx, "Program", "Main");
    if (!entry) {
        diag_report(diags, (Span){0}, "no se encontro Program.Main() como punto de entrada");
        return false;
    }
    call_method(&ctx, "Program", "Main", (ExprList){0}, NULL);
    return true;
}

bool codegen_compile_and_run(Program *program,
                             const CodegenOptions *options,
                             DiagnosticList *diags) {
    if (!emit_jccsc_binary(program, options->output_path, diags)) return false;
    if (options->run_after_compile) {
        if (!run_program(program, diags)) return false;
    }
    return true;
}
