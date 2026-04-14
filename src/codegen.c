#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum RuntimeValueKind {
    RV_INT,
    RV_BOOL,
    RV_STRING,
    RV_OBJECT,
    RV_ARRAY,
    RV_NULL,
    RV_VOID,
} RuntimeValueKind;

typedef struct RuntimeValue {
    RuntimeValueKind kind;
    int int_value;
    const char *string_value;
    struct ObjectInstance *object_value;
    struct ArrayInstance *array_value;
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

typedef struct ObjectField {
    const char *name;
    RuntimeValue value;
} ObjectField;

typedef struct ObjectInstance {
    const char *class_name;
    ObjectField fields[256];
    size_t field_count;
} ObjectInstance;

typedef struct ArrayInstance {
    int *items;
    int length;
} ArrayInstance;

typedef struct ExecContext {
    Program *program;
    DiagnosticList *diags;
    int did_return;
    int did_break;
    int did_continue;
    RuntimeValue return_value;
    const char *current_class;
    ObjectInstance *current_this;
} ExecContext;

static void frame_init(Frame *f, Frame *parent) { f->count = 0; f->parent = parent; }

static RuntimeValue rv_int(int v) { return (RuntimeValue){.kind = RV_INT, .int_value = v}; }
static RuntimeValue rv_bool(int v) { return (RuntimeValue){.kind = RV_BOOL, .int_value = v ? 1 : 0}; }
static RuntimeValue rv_string(const char *s) { return (RuntimeValue){.kind = RV_STRING, .string_value = s}; }
static RuntimeValue rv_void(void) { return (RuntimeValue){.kind = RV_VOID}; }
static RuntimeValue rv_object(ObjectInstance *obj) { return (RuntimeValue){.kind = RV_OBJECT, .object_value = obj, .int_value = 0}; }
static RuntimeValue rv_array(ArrayInstance *arr) { return (RuntimeValue){.kind = RV_ARRAY, .array_value = arr}; }
static RuntimeValue rv_null(void) { return (RuntimeValue){.kind = RV_NULL}; }

static ObjectField *object_find_field(ObjectInstance *obj, const char *name) {
    if (!obj) return NULL;
    for (size_t i = 0; i < obj->field_count; ++i) {
        if (strcmp(obj->fields[i].name, name) == 0) return &obj->fields[i];
    }
    return NULL;
}

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

static ClassDecl *find_class(ExecContext *ctx, const char *class_name) {
    for (size_t ci = 0; ci < ctx->program->class_count; ++ci) {
        ClassDecl *c = &ctx->program->classes[ci];
        if (strcmp(c->name, class_name) == 0) return c;
    }
    return NULL;
}

static ObjectInstance *create_object(ExecContext *ctx, const char *class_name) {
    ClassDecl *c = find_class(ctx, class_name);
    if (!c) return NULL;
    ObjectInstance *obj = calloc(1, sizeof(ObjectInstance));
    obj->class_name = class_name;
    for (size_t i = 0; i < c->field_count && i < 256; ++i) {
        RuntimeValue init = rv_int(0);
        if (c->fields[i].type.kind == TYPE_CLASS) init = rv_null();
        obj->fields[obj->field_count++] = (ObjectField){.name = c->fields[i].name, .value = init};
    }
    return obj;
}

static RuntimeValue eval_expr(ExecContext *ctx, Frame *frame, Expr *e);
static void exec_stmt(ExecContext *ctx, Frame *frame, Stmt *s);

static int truthy(RuntimeValue v) {
    if (v.kind == RV_INT) return v.int_value != 0;
    if (v.kind == RV_BOOL) return v.int_value != 0;
    if (v.kind == RV_OBJECT) return v.object_value != NULL;
    if (v.kind == RV_ARRAY) return v.array_value != NULL;
    if (v.kind == RV_NULL) return 0;
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
        case TOK_EQ:
            if (a.kind == RV_NULL || b.kind == RV_NULL) {
                int an = (a.kind == RV_NULL) || (a.kind == RV_OBJECT && !a.object_value) || (a.kind == RV_ARRAY && !a.array_value);
                int bn = (b.kind == RV_NULL) || (b.kind == RV_OBJECT && !b.object_value) || (b.kind == RV_ARRAY && !b.array_value);
                return rv_bool(an == bn);
            }
            if (a.kind == RV_OBJECT && b.kind == RV_OBJECT) return rv_bool(a.object_value == b.object_value);
            if (a.kind == RV_ARRAY && b.kind == RV_ARRAY) return rv_bool(a.array_value == b.array_value);
            if (a.kind == RV_STRING && b.kind == RV_STRING) {
                const char *as = a.string_value ? a.string_value : "";
                const char *bs = b.string_value ? b.string_value : "";
                return rv_bool(strcmp(as, bs) == 0);
            }
            return rv_bool(ai == bi);
        case TOK_NEQ:
            if (a.kind == RV_NULL || b.kind == RV_NULL) {
                int an = (a.kind == RV_NULL) || (a.kind == RV_OBJECT && !a.object_value) || (a.kind == RV_ARRAY && !a.array_value);
                int bn = (b.kind == RV_NULL) || (b.kind == RV_OBJECT && !b.object_value) || (b.kind == RV_ARRAY && !b.array_value);
                return rv_bool(an != bn);
            }
            if (a.kind == RV_OBJECT && b.kind == RV_OBJECT) return rv_bool(a.object_value != b.object_value);
            if (a.kind == RV_ARRAY && b.kind == RV_ARRAY) return rv_bool(a.array_value != b.array_value);
            if (a.kind == RV_STRING && b.kind == RV_STRING) {
                const char *as = a.string_value ? a.string_value : "";
                const char *bs = b.string_value ? b.string_value : "";
                return rv_bool(strcmp(as, bs) != 0);
            }
            return rv_bool(ai != bi);
        case TOK_LT: return rv_bool(ai < bi);
        case TOK_LE: return rv_bool(ai <= bi);
        case TOK_GT: return rv_bool(ai > bi);
        case TOK_GE: return rv_bool(ai >= bi);
        case TOK_AND: return rv_bool(truthy(a) && truthy(b));
        case TOK_OR: return rv_bool(truthy(a) || truthy(b));
        default: return rv_int(0);
    }
}

static RuntimeValue call_method(ExecContext *ctx,
                                const char *class_name,
                                const char *method_name,
                                ExprList args,
                                Frame *caller,
                                ObjectInstance *this_obj) {
    if (strcmp(class_name, "Console") == 0 && strcmp(method_name, "WriteLine") == 0 && args.count == 1) {
        RuntimeValue v = eval_expr(ctx, caller, args.items[0]);
        if (v.kind == RV_STRING) printf("%s\n", v.string_value ? v.string_value : "");
        else if (v.kind == RV_BOOL) printf("%s\n", v.int_value ? "true" : "false");
        else if (v.kind == RV_NULL) printf("null\n");
        else if (v.kind == RV_OBJECT) printf("<object %s>\n", v.object_value ? v.object_value->class_name : "null");
        else if (v.kind == RV_ARRAY) printf("<array len=%d>\n", v.array_value ? v.array_value->length : 0);
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
    ObjectInstance *prev_this = ctx->current_this;
    ctx->current_class = class_name;
    ctx->current_this = this_obj;

    exec_stmt(ctx, &local, m->body);

    RuntimeValue out = ctx->return_value;
    if (!ctx->did_return && m->return_type.kind == TYPE_INT) out = rv_int(0);

    ctx->did_return = prev_return;
    ctx->did_break = prev_break;
    ctx->did_continue = prev_continue;
    ctx->return_value = prev_value;
    ctx->current_class = prev_class;
    ctx->current_this = prev_this;
    return out;
}

static RuntimeValue eval_expr(ExecContext *ctx, Frame *frame, Expr *e) {
    switch (e->kind) {
        case EXPR_INT: return rv_int(e->as.int_value);
        case EXPR_BOOL: return rv_bool(e->as.bool_value);
        case EXPR_NULL: return rv_null();
        case EXPR_STRING: return rv_string(e->as.string_value);
        case EXPR_IDENTIFIER: {
            VarSlot *slot = frame_find(frame, e->as.identifier);
            if (slot) return slot->value;
            ObjectField *field = object_find_field(ctx->current_this, e->as.identifier);
            if (field) return field->value;
            return rv_int(0);
        }
        case EXPR_ASSIGN: {
            RuntimeValue v = eval_expr(ctx, frame, e->as.assign.value);
            if (e->as.assign.target->kind == EXPR_IDENTIFIER) {
                VarSlot *slot = frame_find(frame, e->as.assign.target->as.identifier);
                if (slot) frame_assign(frame, e->as.assign.target->as.identifier, v);
                else {
                    ObjectField *field = object_find_field(ctx->current_this, e->as.assign.target->as.identifier);
                    if (field) field->value = v;
                    else frame_assign(frame, e->as.assign.target->as.identifier, v);
                }
            } else if (e->as.assign.target->kind == EXPR_MEMBER) {
                RuntimeValue obj = eval_expr(ctx, frame, e->as.assign.target->as.member.object);
                if (obj.object_value) {
                    ObjectField *field = object_find_field(obj.object_value, e->as.assign.target->as.member.member);
                    if (field) field->value = v;
                }
            } else if (e->as.assign.target->kind == EXPR_INDEX) {
                RuntimeValue arr = eval_expr(ctx, frame, e->as.assign.target->as.index.array);
                RuntimeValue idx = eval_expr(ctx, frame, e->as.assign.target->as.index.index);
                if (arr.kind == RV_ARRAY && arr.array_value && idx.kind == RV_INT) {
                    if (idx.int_value >= 0 && idx.int_value < arr.array_value->length) {
                        arr.array_value->items[idx.int_value] = v.int_value;
                    }
                }
            } else return rv_void();
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
            if (e->as.member.object->kind == EXPR_IDENTIFIER &&
                strcmp(e->as.member.object->as.identifier, "this") == 0) {
                ObjectField *field = object_find_field(ctx->current_this, e->as.member.member);
                return field ? field->value : rv_void();
            }
            {
                RuntimeValue obj = eval_expr(ctx, frame, e->as.member.object);
                if (obj.kind == RV_ARRAY && strcmp(e->as.member.member, "Length") == 0) {
                    return rv_int(obj.array_value ? obj.array_value->length : 0);
                }
                if (obj.object_value) {
                    ObjectField *field = object_find_field(obj.object_value, e->as.member.member);
                    if (field) return field->value;
                }
                return rv_void();
            }
        case EXPR_CALL: {
            Expr *callee = e->as.call.callee;
            if (callee->kind == EXPR_MEMBER) {
                RuntimeValue obj = eval_expr(ctx, frame, callee->as.member.object);
                if (obj.object_value) {
                    return call_method(ctx,
                                       obj.object_value->class_name,
                                       callee->as.member.member,
                                       e->as.call.args,
                                       frame,
                                       obj.object_value);
                }
                if (callee->as.member.object->kind == EXPR_IDENTIFIER) {
                    return call_method(ctx,
                                       callee->as.member.object->as.identifier,
                                       callee->as.member.member,
                                       e->as.call.args,
                                       frame,
                                       NULL);
                }
            }
            if (callee->kind == EXPR_IDENTIFIER) {
                return call_method(ctx, ctx->current_class, callee->as.identifier, e->as.call.args, frame, ctx->current_this);
            }
            return rv_void();
        }
        case EXPR_NEW: {
            if (e->as.new_expr.is_int_array) {
                RuntimeValue n = eval_expr(ctx, frame, e->as.new_expr.array_size_expr);
                int len = n.kind == RV_INT ? n.int_value : 0;
                if (len < 0) len = 0;
                ArrayInstance *arr = calloc(1, sizeof(ArrayInstance));
                arr->length = len;
                arr->items = calloc((size_t)len, sizeof(int));
                return rv_array(arr);
            }
            ObjectInstance *obj = create_object(ctx, e->as.new_expr.class_name);
            if (!obj) {
                diag_report(ctx->diags, e->span, "clase no encontrada para new: %s", e->as.new_expr.class_name);
                return rv_void();
            }
            return rv_object(obj);
        }
        case EXPR_INDEX: {
            RuntimeValue arr = eval_expr(ctx, frame, e->as.index.array);
            RuntimeValue idx = eval_expr(ctx, frame, e->as.index.index);
            if (arr.kind == RV_ARRAY && arr.array_value && idx.kind == RV_INT &&
                idx.int_value >= 0 && idx.int_value < arr.array_value->length) {
                return rv_int(arr.array_value->items[idx.int_value]);
            }
            return rv_int(0);
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
            if (s->as.var.type.kind == TYPE_CLASS) v = rv_null();
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
        case STMT_DO_WHILE:
            do {
                exec_stmt(ctx, frame, s->as.do_while_stmt.body);
                if (ctx->did_break) {
                    ctx->did_break = 0;
                    break;
                }
                if (ctx->did_continue) ctx->did_continue = 0;
                if (ctx->did_return) break;
            } while (truthy(eval_expr(ctx, frame, s->as.do_while_stmt.condition)));
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
            if (!matched && s->as.switch_stmt.default_body) exec_stmt(ctx, frame, s->as.switch_stmt.default_body);
            if (ctx->did_break) ctx->did_break = 0;
            break;
        }
        case STMT_FOREACH: {
            RuntimeValue iterable = eval_expr(ctx, frame, s->as.foreach_stmt.iterable);
            if (iterable.kind != RV_ARRAY || !iterable.array_value) break;
            Frame loop;
            frame_init(&loop, frame);
            for (int i = 0; i < iterable.array_value->length && !ctx->did_return; ++i) {
                frame_define(&loop, s->as.foreach_stmt.var_name, rv_int(iterable.array_value->items[i]));
                exec_stmt(ctx, &loop, s->as.foreach_stmt.body);
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
        }
        case STMT_SWITCH: {
            RuntimeValue v = eval_expr(ctx, frame, s->as.switch_stmt.expr);
            int matched = 0;
            for (size_t i = 0; i < s->as.switch_stmt.case_count; ++i) {
                if (v.int_value == s->as.switch_stmt.cases[i].value) {
                    exec_stmt(ctx, frame, s->as.switch_stmt.cases[i].body);
                    matched = 1;
                    break;
                }
            }
            if (!matched && s->as.switch_stmt.default_body) exec_stmt(ctx, frame, s->as.switch_stmt.default_body);
            if (ctx->did_break) ctx->did_break = 0;
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

static bool emit_native_stub(TargetArch target, const char *path, DiagnosticList *diags) {
    FILE *f = fopen(path, "w");
    if (!f) {
        diag_report(diags, (Span){0}, "no se pudo escribir salida nativa: %s", path);
        return false;
    }

    switch (target) {
        case TARGET_ARCH_X86_64:
            fputs(".text\n.global _start\n_start:\n  mov $60, %rax\n  xor %rdi, %rdi\n  syscall\n", f);
            break;
        case TARGET_ARCH_X86_32:
            fputs(".text\n.global _start\n_start:\n  mov $1, %eax\n  xor %ebx, %ebx\n  int $0x80\n", f);
            break;
        case TARGET_ARCH_ARM64:
            fputs(".text\n.global _start\n_start:\n  mov x8, #93\n  mov x0, #0\n  svc #0\n", f);
            break;
        case TARGET_ARCH_ARM32:
            fputs(".text\n.global _start\n_start:\n  mov r7, #1\n  mov r0, #0\n  svc #0\n", f);
            break;
    }
    fclose(f);
    return true;
}

static TargetArch host_target(void) {
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

static bool build_native_executable(TargetArch target, const char *exe_path, DiagnosticList *diags) {
    if (target != host_target()) {
        diag_report(diags, (Span){0}, "cross-target nativo no disponible en este host. usa --emit-asm");
        return false;
    }

    char asm_path[512];
    snprintf(asm_path, sizeof(asm_path), "%s.s", exe_path);
    if (!emit_native_stub(target, asm_path, diags)) return false;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cc -nostdlib -Wl,-e,_start '%s' -o '%s'", asm_path, exe_path);
    int rc = system(cmd);
    if (rc != 0) {
        diag_report(diags, (Span){0}, "fallo al enlazar binario nativo con comando: %s", cmd);
        return false;
    }
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
        .current_class = "Program",
        .current_this = NULL
    };
    MethodDecl *entry = find_method(&ctx, "Program", "Main");
    if (!entry) {
        diag_report(diags, (Span){0}, "no se encontro Program.Main() como punto de entrada");
        return false;
    }
    call_method(&ctx, "Program", "Main", (ExprList){0}, NULL, NULL);
    return true;
}

bool codegen_compile_and_run(Program *program,
                             const CodegenOptions *options,
                             DiagnosticList *diags) {
    if (options->backend == CODEGEN_BACKEND_NATIVE) {
        if (options->run_after_compile) {
            diag_report(diags, (Span){0}, "--run no esta soportado en backend native");
            return false;
        }
        if (options->native_emit_asm_only) {
            return emit_native_stub(options->target, options->output_path, diags);
        }
        return build_native_executable(options->target, options->output_path, diags);
    }

    if (!emit_jccsc_binary(program, options->output_path, diags)) return false;
    if (options->run_after_compile) {
        if (!run_program(program, diags)) return false;
    }
    return true;
}
