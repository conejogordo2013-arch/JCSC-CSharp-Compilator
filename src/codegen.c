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
    RV_LIST,
    RV_NULL,
    RV_VOID,
} RuntimeValueKind;

typedef struct RuntimeValue {
    RuntimeValueKind kind;
    int int_value;
    const char *string_value;
    struct ObjectInstance *object_value;
    struct ArrayInstance *array_value;
    struct ListInstance *list_value;
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

typedef struct ListInstance {
    int *items;
    int count;
    int capacity;
} ListInstance;

typedef struct ExecContext {
    Program *program;
    DiagnosticList *diags;
    int did_return;
    int did_break;
    int did_continue;
    int did_throw;
    RuntimeValue return_value;
    RuntimeValue thrown_value;
    RuntimeValue active_exception;
    int has_active_exception;
    const char *current_class;
    ObjectInstance *current_this;
    int debug_trace;
} ExecContext;

static void frame_init(Frame *f, Frame *parent) { f->count = 0; f->parent = parent; }

static RuntimeValue rv_int(int v) { return (RuntimeValue){.kind = RV_INT, .int_value = v}; }
static RuntimeValue rv_bool(int v) { return (RuntimeValue){.kind = RV_BOOL, .int_value = v ? 1 : 0}; }
static RuntimeValue rv_string(const char *s) { return (RuntimeValue){.kind = RV_STRING, .string_value = s}; }
static RuntimeValue rv_void(void) { return (RuntimeValue){.kind = RV_VOID}; }
static RuntimeValue rv_object(ObjectInstance *obj) { return (RuntimeValue){.kind = RV_OBJECT, .object_value = obj, .int_value = 0}; }
static RuntimeValue rv_array(ArrayInstance *arr) { return (RuntimeValue){.kind = RV_ARRAY, .array_value = arr}; }
static RuntimeValue rv_list(ListInstance *list) { return (RuntimeValue){.kind = RV_LIST, .list_value = list}; }
static RuntimeValue rv_null(void) { return (RuntimeValue){.kind = RV_NULL}; }

static RuntimeValue default_value_for_type(TypeRef t) {
    switch (t.kind) {
        case TYPE_INT: return rv_int(0);
        case TYPE_BOOL: return rv_bool(0);
        case TYPE_STRING: return rv_null();
        case TYPE_CLASS: return rv_null();
        case TYPE_VOID: return rv_void();
        default: return rv_void();
    }
}

static const char *runtime_value_to_cstr(RuntimeValue v, char *buf, size_t buf_size) {
    if (v.kind == RV_STRING) return v.string_value ? v.string_value : "";
    if (v.kind == RV_BOOL) {
        snprintf(buf, buf_size, "%s", v.int_value ? "true" : "false");
        return buf;
    }
    if (v.kind == RV_NULL) return "null";
    if (v.kind == RV_OBJECT) {
        snprintf(buf, buf_size, "<object %s>", v.object_value ? v.object_value->class_name : "null");
        return buf;
    }
    if (v.kind == RV_ARRAY) {
        snprintf(buf, buf_size, "<array len=%d>", v.array_value ? v.array_value->length : 0);
        return buf;
    }
    if (v.kind == RV_LIST) {
        snprintf(buf, buf_size, "<list count=%d>", v.list_value ? v.list_value->count : 0);
        return buf;
    }
    snprintf(buf, buf_size, "%d", v.int_value);
    return buf;
}

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

static ClassDecl *find_class(ExecContext *ctx, const char *class_name) {
    for (size_t ci = 0; ci < ctx->program->class_count; ++ci) {
        ClassDecl *c = &ctx->program->classes[ci];
        if (strcmp(c->name, class_name) == 0) return c;
    }
    return NULL;
}

static int runtime_class_assignable_to(ExecContext *ctx, const char *from_class, const char *to_class, int depth) {
    if (!from_class || !to_class || depth > 32) return 0;
    if (strcmp(from_class, to_class) == 0) return 1;
    ClassDecl *decl = find_class(ctx, from_class);
    if (!decl) return 0;
    for (size_t i = 0; i < decl->base_type_count; ++i) {
        const char *base = decl->base_types[i];
        if (strcmp(base, to_class) == 0) return 1;
        if (find_class(ctx, base) && runtime_class_assignable_to(ctx, base, to_class, depth + 1)) return 1;
    }
    return 0;
}

static int runtime_value_matches_type(ExecContext *ctx, RuntimeValue v, TypeRef t) {
    if (t.kind == TYPE_INT) return v.kind == RV_INT;
    if (t.kind == TYPE_BOOL) return v.kind == RV_BOOL;
    if (t.kind == TYPE_STRING) return v.kind == RV_STRING || v.kind == RV_NULL;
    if (t.kind == TYPE_CLASS) {
        if (v.kind == RV_NULL) return 1;
        if (t.name && strcmp(t.name, "int[]") == 0) return v.kind == RV_ARRAY;
        if (v.kind != RV_OBJECT || !v.object_value) return 0;
        if (!t.name) return 1;
        return runtime_class_assignable_to(ctx, v.object_value->class_name, t.name, 0);
    }
    return 1;
}

static MethodDecl *find_method_in_class(ExecContext *ctx,
                                        ClassDecl *c,
                                        const char *method_name,
                                        RuntimeValue *args,
                                        size_t arg_count) {
    MethodDecl *best = NULL;
    int best_score = -1;
    for (size_t mi = 0; mi < c->method_count; ++mi) {
        MethodDecl *candidate = &c->methods[mi];
        if (strcmp(candidate->name, method_name) != 0) continue;
        if (candidate->param_count != arg_count) continue;
        int score = 0;
        int ok = 1;
        for (size_t pi = 0; pi < arg_count; ++pi) {
            if (!runtime_value_matches_type(ctx, args[pi], candidate->params[pi].type)) {
                ok = 0;
                break;
            }
            score++;
        }
        if (!ok) continue;
        if (score > best_score) {
            best = candidate;
            best_score = score;
        }
    }
    return best;
}

static MethodDecl *find_method(ExecContext *ctx,
                               const char *class_name,
                               const char *method_name,
                               RuntimeValue *args,
                               size_t arg_count) {
    ClassDecl *c = find_class(ctx, class_name);
    if (!c) return NULL;
    MethodDecl *m = find_method_in_class(ctx, c, method_name, args, arg_count);
    if (m) return m;
    for (size_t i = 0; i < c->base_type_count; ++i) {
        ClassDecl *base = find_class(ctx, c->base_types[i]);
        if (!base) continue;
        m = find_method_in_class(ctx, base, method_name, args, arg_count);
        if (m) return m;
    }
    return NULL;
}

static void init_fields_from_class(ObjectInstance *obj, ClassDecl *c) {
    if (!obj || !c) return;
    for (size_t i = 0; i < c->field_count && obj->field_count < 256; ++i) {
        RuntimeValue init = rv_int(0);
        if (c->fields[i].type.kind == TYPE_CLASS) init = rv_null();
        obj->fields[obj->field_count++] = (ObjectField){.name = c->fields[i].name, .value = init};
    }
}

static ObjectInstance *create_object(ExecContext *ctx, const char *class_name) {
    ClassDecl *c = find_class(ctx, class_name);
    if (!c) return NULL;
    ObjectInstance *obj = calloc(1, sizeof(ObjectInstance));
    obj->class_name = class_name;
    for (size_t i = 0; i < c->base_type_count; ++i) {
        ClassDecl *base = find_class(ctx, c->base_types[i]);
        if (base) init_fields_from_class(obj, base);
    }
    init_fields_from_class(obj, c);
    return obj;
}

static RuntimeValue eval_expr(ExecContext *ctx, Frame *frame, Expr *e);
static void exec_stmt(ExecContext *ctx, Frame *frame, Stmt *s);

static int truthy(RuntimeValue v) {
    if (v.kind == RV_INT) return v.int_value != 0;
    if (v.kind == RV_BOOL) return v.int_value != 0;
    if (v.kind == RV_OBJECT) return v.object_value != NULL;
    if (v.kind == RV_ARRAY) return v.array_value != NULL;
    if (v.kind == RV_LIST) return v.list_value != NULL;
    if (v.kind == RV_NULL) return 0;
    if (v.kind == RV_STRING) return v.string_value && v.string_value[0] != '\0';
    return 0;
}

static RuntimeValue eval_binary(TokenKind op, RuntimeValue a, RuntimeValue b) {
    int ai = a.kind == RV_INT ? a.int_value : 0;
    int bi = b.kind == RV_INT ? b.int_value : 0;
    switch (op) {
        case TOK_PLUS:
            if (a.kind == RV_STRING || b.kind == RV_STRING) {
                char abuf[64], bbuf[64];
                const char *as = runtime_value_to_cstr(a, abuf, sizeof(abuf));
                const char *bs = runtime_value_to_cstr(b, bbuf, sizeof(bbuf));
                size_t n1 = strlen(as), n2 = strlen(bs);
                char *out = malloc(n1 + n2 + 1);
                memcpy(out, as, n1);
                memcpy(out + n1, bs, n2);
                out[n1 + n2] = '\0';
                return rv_string(out);
            }
            return rv_int(ai + bi);
        case TOK_MINUS: return rv_int(ai - bi);
        case TOK_STAR: return rv_int(ai * bi);
        case TOK_SLASH: return rv_int(bi == 0 ? 0 : ai / bi);
        case TOK_PERCENT: return rv_int(bi == 0 ? 0 : ai % bi);
        case TOK_EQ:
            if (a.kind == RV_NULL || b.kind == RV_NULL) {
                int an = (a.kind == RV_NULL) || (a.kind == RV_OBJECT && !a.object_value) || (a.kind == RV_ARRAY && !a.array_value) || (a.kind == RV_LIST && !a.list_value);
                int bn = (b.kind == RV_NULL) || (b.kind == RV_OBJECT && !b.object_value) || (b.kind == RV_ARRAY && !b.array_value) || (b.kind == RV_LIST && !b.list_value);
                return rv_bool(an == bn);
            }
            if (a.kind == RV_OBJECT && b.kind == RV_OBJECT) return rv_bool(a.object_value == b.object_value);
            if (a.kind == RV_ARRAY && b.kind == RV_ARRAY) return rv_bool(a.array_value == b.array_value);
            if (a.kind == RV_LIST && b.kind == RV_LIST) return rv_bool(a.list_value == b.list_value);
            if (a.kind == RV_STRING && b.kind == RV_STRING) {
                const char *as = a.string_value ? a.string_value : "";
                const char *bs = b.string_value ? b.string_value : "";
                return rv_bool(strcmp(as, bs) == 0);
            }
            return rv_bool(ai == bi);
        case TOK_NEQ:
            if (a.kind == RV_NULL || b.kind == RV_NULL) {
                int an = (a.kind == RV_NULL) || (a.kind == RV_OBJECT && !a.object_value) || (a.kind == RV_ARRAY && !a.array_value) || (a.kind == RV_LIST && !a.list_value);
                int bn = (b.kind == RV_NULL) || (b.kind == RV_OBJECT && !b.object_value) || (b.kind == RV_ARRAY && !b.array_value) || (b.kind == RV_LIST && !b.list_value);
                return rv_bool(an != bn);
            }
            if (a.kind == RV_OBJECT && b.kind == RV_OBJECT) return rv_bool(a.object_value != b.object_value);
            if (a.kind == RV_ARRAY && b.kind == RV_ARRAY) return rv_bool(a.array_value != b.array_value);
            if (a.kind == RV_LIST && b.kind == RV_LIST) return rv_bool(a.list_value != b.list_value);
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
                                ObjectInstance *this_obj,
                                Span call_span) {
    if (ctx->debug_trace) {
        fprintf(stderr, "[trace] call %s.%s(%zu)\n", class_name, method_name, args.count);
    }
    if (strcmp(class_name, "Console") == 0 && strcmp(method_name, "WriteLine") == 0 && args.count == 1) {
        RuntimeValue v = eval_expr(ctx, caller, args.items[0]);
        if (v.kind == RV_STRING) printf("%s\n", v.string_value ? v.string_value : "");
        else if (v.kind == RV_BOOL) printf("%s\n", v.int_value ? "true" : "false");
        else if (v.kind == RV_NULL) printf("null\n");
        else if (v.kind == RV_OBJECT) printf("<object %s>\n", v.object_value ? v.object_value->class_name : "null");
        else if (v.kind == RV_ARRAY) printf("<array len=%d>\n", v.array_value ? v.array_value->length : 0);
        else if (v.kind == RV_LIST) printf("<list count=%d>\n", v.list_value ? v.list_value->count : 0);
        else printf("%d\n", v.int_value);
        return rv_void();
    }

    RuntimeValue eval_args[64];
    if (args.count > 64) {
        diag_report(ctx->diags, call_span, "demasiados argumentos para llamada a %s.%s", class_name, method_name);
        return rv_void();
    }
    for (size_t i = 0; i < args.count; ++i) eval_args[i] = eval_expr(ctx, caller, args.items[i]);

    MethodDecl *m = find_method(ctx, class_name, method_name, eval_args, args.count);
    if (!m) {
        diag_report(ctx->diags, call_span,
                    "no existe sobrecarga compatible para %s.%s con %zu argumento(s)",
                    class_name, method_name, args.count);
        return rv_void();
    }

    Frame local;
    frame_init(&local, NULL);
    for (size_t i = 0; i < m->param_count && i < args.count; ++i) {
        frame_define(&local, m->params[i].name, eval_args[i]);
    }

    int prev_return = ctx->did_return;
    int prev_break = ctx->did_break;
    int prev_continue = ctx->did_continue;
    int prev_throw = ctx->did_throw;
    int prev_has_active_exception = ctx->has_active_exception;
    RuntimeValue prev_active_exception = ctx->active_exception;
    RuntimeValue prev_value = ctx->return_value;
    RuntimeValue prev_thrown = ctx->thrown_value;
    ctx->did_return = 0;
    ctx->did_break = 0;
    ctx->did_continue = 0;
    ctx->did_throw = 0;
    ctx->return_value = rv_void();
    const char *prev_class = ctx->current_class;
    ObjectInstance *prev_this = ctx->current_this;
    ctx->current_class = class_name;
    ctx->current_this = this_obj;

    exec_stmt(ctx, &local, m->body);

    RuntimeValue out = ctx->return_value;
    if (!ctx->did_return) out = default_value_for_type(m->return_type);
    int bubbled_throw = ctx->did_throw;
    RuntimeValue bubbled_value = ctx->thrown_value;

    ctx->did_return = prev_return;
    ctx->did_break = prev_break;
    ctx->did_continue = prev_continue;
    ctx->did_throw = prev_throw;
    ctx->has_active_exception = prev_has_active_exception;
    ctx->active_exception = prev_active_exception;
    ctx->return_value = prev_value;
    ctx->thrown_value = prev_thrown;
    if (bubbled_throw) {
        ctx->did_throw = 1;
        ctx->thrown_value = bubbled_value;
    }
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
                if (idx.kind != RV_INT) {
                    diag_report(ctx->diags, e->span, "indice de asignacion debe ser int");
                } else if (arr.kind == RV_ARRAY && arr.array_value) {
                    if (idx.int_value >= 0 && idx.int_value < arr.array_value->length) {
                        arr.array_value->items[idx.int_value] = v.int_value;
                    } else {
                        diag_report(ctx->diags, e->span, "asignacion fuera de rango en array");
                    }
                } else if (arr.kind == RV_LIST && arr.list_value) {
                    if (idx.int_value >= 0 && idx.int_value < arr.list_value->count) {
                        arr.list_value->items[idx.int_value] = v.int_value;
                    } else {
                        diag_report(ctx->diags, e->span, "asignacion fuera de rango en List");
                    }
                } else {
                    diag_report(ctx->diags, e->span, "asignacion indexada requiere array o List");
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
            if (e->as.binary.op == TOK_AND) {
                if (!truthy(l)) return rv_bool(0);
                RuntimeValue r = eval_expr(ctx, frame, e->as.binary.right);
                return rv_bool(truthy(r));
            }
            if (e->as.binary.op == TOK_OR) {
                if (truthy(l)) return rv_bool(1);
                RuntimeValue r = eval_expr(ctx, frame, e->as.binary.right);
                return rv_bool(truthy(r));
            }
            if (e->as.binary.op == TOK_COALESCE) {
                if (l.kind != RV_NULL) return l;
                return eval_expr(ctx, frame, e->as.binary.right);
            }
            RuntimeValue r = eval_expr(ctx, frame, e->as.binary.right);
            if (e->as.binary.op == TOK_SLASH && r.kind == RV_INT && r.int_value == 0) {
                diag_report(ctx->diags, e->span, "division por cero");
                return rv_int(0);
            }
            if (e->as.binary.op == TOK_PERCENT && r.kind == RV_INT && r.int_value == 0) {
                diag_report(ctx->diags, e->span, "modulo por cero");
                return rv_int(0);
            }
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
                if (obj.kind == RV_LIST && strcmp(e->as.member.member, "Count") == 0) {
                    return rv_int(obj.list_value ? obj.list_value->count : 0);
                }
                if (obj.object_value) {
                    ObjectField *field = object_find_field(obj.object_value, e->as.member.member);
                    if (field) return field->value;
                }
                if (obj.kind == RV_NULL) {
                    diag_report(ctx->diags, e->span, "acceso a miembro sobre null");
                } else {
                    diag_report(ctx->diags, e->span, "miembro no encontrado: %s", e->as.member.member);
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
                                       obj.object_value,
                                       e->span);
                }
                if (obj.kind == RV_LIST && obj.list_value) {
                    if (strcmp(callee->as.member.member, "Add") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind == RV_INT) {
                            if (obj.list_value->count >= obj.list_value->capacity) {
                                int new_cap = obj.list_value->capacity == 0 ? 4 : obj.list_value->capacity * 2;
                                obj.list_value->items = realloc(obj.list_value->items, sizeof(int) * (size_t)new_cap);
                                obj.list_value->capacity = new_cap;
                            }
                            obj.list_value->items[obj.list_value->count++] = arg.int_value;
                        }
                        return rv_void();
                    }
                    if (strcmp(callee->as.member.member, "Clear") == 0 && e->as.call.args.count == 0) {
                        obj.list_value->count = 0;
                        return rv_void();
                    }
                    if (strcmp(callee->as.member.member, "Where") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Where requiere argumento int");
                            return rv_list(calloc(1, sizeof(ListInstance)));
                        }
                        ListInstance *out = calloc(1, sizeof(ListInstance));
                        for (int i = 0; i < obj.list_value->count; ++i) {
                            if (obj.list_value->items[i] >= arg.int_value) {
                                if (out->count >= out->capacity) {
                                    int new_cap = out->capacity == 0 ? 4 : out->capacity * 2;
                                    out->items = realloc(out->items, sizeof(int) * (size_t)new_cap);
                                    out->capacity = new_cap;
                                }
                                out->items[out->count++] = obj.list_value->items[i];
                            }
                        }
                        return rv_list(out);
                    }
                    if (strcmp(callee->as.member.member, "Select") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Select requiere argumento int");
                            return rv_list(calloc(1, sizeof(ListInstance)));
                        }
                        ListInstance *out = calloc(1, sizeof(ListInstance));
                        out->count = obj.list_value->count;
                        out->capacity = out->count;
                        if (out->count > 0) out->items = calloc((size_t)out->count, sizeof(int));
                        for (int i = 0; i < obj.list_value->count; ++i) out->items[i] = obj.list_value->items[i] + arg.int_value;
                        return rv_list(out);
                    }
                    if (strcmp(callee->as.member.member, "FirstOrDefault") == 0 && e->as.call.args.count == 0) {
                        if (obj.list_value->count == 0) return rv_int(0);
                        return rv_int(obj.list_value->items[0]);
                    }
                    if (strcmp(callee->as.member.member, "LastOrDefault") == 0 && e->as.call.args.count == 0) {
                        if (obj.list_value->count == 0) return rv_int(0);
                        return rv_int(obj.list_value->items[obj.list_value->count - 1]);
                    }
                    if (strcmp(callee->as.member.member, "ElementAtOrDefault") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue idx = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (idx.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "ElementAtOrDefault requiere argumento int");
                            return rv_int(0);
                        }
                        if (idx.int_value < 0 || idx.int_value >= obj.list_value->count) return rv_int(0);
                        return rv_int(obj.list_value->items[idx.int_value]);
                    }
                    if (strcmp(callee->as.member.member, "Sum") == 0 && e->as.call.args.count == 0) {
                        int total = 0;
                        for (int i = 0; i < obj.list_value->count; ++i) total += obj.list_value->items[i];
                        return rv_int(total);
                    }
                    if (strcmp(callee->as.member.member, "Count") == 0 && e->as.call.args.count == 0) {
                        return rv_int(obj.list_value->count);
                    }
                    if (strcmp(callee->as.member.member, "Any") == 0 && e->as.call.args.count == 0) {
                        return rv_bool(obj.list_value->count > 0);
                    }
                    if (strcmp(callee->as.member.member, "All") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "All requiere argumento int");
                            return rv_bool(0);
                        }
                        for (int i = 0; i < obj.list_value->count; ++i) {
                            if (obj.list_value->items[i] < arg.int_value) return rv_bool(0);
                        }
                        return rv_bool(1);
                    }
                    if (strcmp(callee->as.member.member, "Distinct") == 0 && e->as.call.args.count == 0) {
                        ListInstance *out = calloc(1, sizeof(ListInstance));
                        for (int i = 0; i < obj.list_value->count; ++i) {
                            int v = obj.list_value->items[i];
                            int seen = 0;
                            for (int j = 0; j < out->count; ++j) if (out->items[j] == v) { seen = 1; break; }
                            if (seen) continue;
                            if (out->count >= out->capacity) {
                                int new_cap = out->capacity == 0 ? 4 : out->capacity * 2;
                                out->items = realloc(out->items, sizeof(int) * (size_t)new_cap);
                                out->capacity = new_cap;
                            }
                            out->items[out->count++] = v;
                        }
                        return rv_list(out);
                    }
                    if (strcmp(callee->as.member.member, "OrderBy") == 0 && e->as.call.args.count == 0) {
                        ListInstance *out = calloc(1, sizeof(ListInstance));
                        out->count = obj.list_value->count;
                        out->capacity = out->count;
                        if (out->count > 0) out->items = calloc((size_t)out->count, sizeof(int));
                        for (int i = 0; i < out->count; ++i) out->items[i] = obj.list_value->items[i];
                        for (int i = 0; i < out->count; ++i) {
                            for (int j = i + 1; j < out->count; ++j) {
                                if (out->items[j] < out->items[i]) {
                                    int tmp = out->items[i];
                                    out->items[i] = out->items[j];
                                    out->items[j] = tmp;
                                }
                            }
                        }
                        return rv_list(out);
                    }
                    if (strcmp(callee->as.member.member, "Reverse") == 0 && e->as.call.args.count == 0) {
                        ListInstance *out = calloc(1, sizeof(ListInstance));
                        out->count = obj.list_value->count;
                        out->capacity = out->count;
                        if (out->count > 0) out->items = calloc((size_t)out->count, sizeof(int));
                        for (int i = 0; i < out->count; ++i) out->items[i] = obj.list_value->items[out->count - i - 1];
                        return rv_list(out);
                    }
                    if (strcmp(callee->as.member.member, "Contains") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Contains requiere argumento int");
                            return rv_bool(0);
                        }
                        for (int i = 0; i < obj.list_value->count; ++i) {
                            if (obj.list_value->items[i] == arg.int_value) return rv_bool(1);
                        }
                        return rv_bool(0);
                    }
                    if (strcmp(callee->as.member.member, "Remove") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Remove requiere argumento int");
                            return rv_bool(0);
                        }
                        for (int i = 0; i < obj.list_value->count; ++i) {
                            if (obj.list_value->items[i] == arg.int_value) {
                                for (int j = i; j + 1 < obj.list_value->count; ++j) obj.list_value->items[j] = obj.list_value->items[j + 1];
                                obj.list_value->count--;
                                return rv_bool(1);
                            }
                        }
                        return rv_bool(0);
                    }
                    if (strcmp(callee->as.member.member, "RemoveAt") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "RemoveAt requiere argumento int");
                            return rv_void();
                        }
                        if (arg.int_value < 0 || arg.int_value >= obj.list_value->count) {
                            diag_report(ctx->diags, e->span, "RemoveAt fuera de rango");
                            return rv_void();
                        }
                        for (int j = arg.int_value; j + 1 < obj.list_value->count; ++j) obj.list_value->items[j] = obj.list_value->items[j + 1];
                        obj.list_value->count--;
                        return rv_void();
                    }
                    if (strcmp(callee->as.member.member, "Insert") == 0 && e->as.call.args.count == 2) {
                        RuntimeValue idx = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        RuntimeValue val = eval_expr(ctx, frame, e->as.call.args.items[1]);
                        if (idx.kind != RV_INT || val.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Insert requiere (int index, int value)");
                            return rv_void();
                        }
                        if (idx.int_value < 0 || idx.int_value > obj.list_value->count) {
                            diag_report(ctx->diags, e->span, "Insert fuera de rango");
                            return rv_void();
                        }
                        if (obj.list_value->count >= obj.list_value->capacity) {
                            int new_cap = obj.list_value->capacity == 0 ? 4 : obj.list_value->capacity * 2;
                            obj.list_value->items = realloc(obj.list_value->items, sizeof(int) * (size_t)new_cap);
                            obj.list_value->capacity = new_cap;
                        }
                        for (int j = obj.list_value->count; j > idx.int_value; --j) {
                            obj.list_value->items[j] = obj.list_value->items[j - 1];
                        }
                        obj.list_value->items[idx.int_value] = val.int_value;
                        obj.list_value->count++;
                        return rv_void();
                    }
                    if (strcmp(callee->as.member.member, "ToArray") == 0 && e->as.call.args.count == 0) {
                        ArrayInstance *arr = calloc(1, sizeof(ArrayInstance));
                        arr->length = obj.list_value->count;
                        if (arr->length > 0) arr->items = calloc((size_t)arr->length, sizeof(int));
                        for (int i = 0; i < arr->length; ++i) arr->items[i] = obj.list_value->items[i];
                        return rv_array(arr);
                    }
                    if (strcmp(callee->as.member.member, "SequenceEqual") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue other = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (other.kind == RV_LIST && other.list_value) {
                            if (other.list_value->count != obj.list_value->count) return rv_bool(0);
                            for (int i = 0; i < obj.list_value->count; ++i) {
                                if (obj.list_value->items[i] != other.list_value->items[i]) return rv_bool(0);
                            }
                            return rv_bool(1);
                        }
                        if (other.kind == RV_ARRAY && other.array_value) {
                            if (other.array_value->length != obj.list_value->count) return rv_bool(0);
                            for (int i = 0; i < obj.list_value->count; ++i) {
                                if (obj.list_value->items[i] != other.array_value->items[i]) return rv_bool(0);
                            }
                            return rv_bool(1);
                        }
                        diag_report(ctx->diags, e->span, "SequenceEqual requiere List<int> o int[]");
                        return rv_bool(0);
                    }
                    if ((strcmp(callee->as.member.member, "Min") == 0 ||
                         strcmp(callee->as.member.member, "Max") == 0 ||
                         strcmp(callee->as.member.member, "Average") == 0) && e->as.call.args.count == 0) {
                        if (obj.list_value->count == 0) return rv_int(0);
                        int best = obj.list_value->items[0];
                        int total = 0;
                        for (int i = 0; i < obj.list_value->count; ++i) {
                            int v = obj.list_value->items[i];
                            total += v;
                            if (strcmp(callee->as.member.member, "Min") == 0 && v < best) best = v;
                            if (strcmp(callee->as.member.member, "Max") == 0 && v > best) best = v;
                        }
                        if (strcmp(callee->as.member.member, "Average") == 0) return rv_int(total / obj.list_value->count);
                        return rv_int(best);
                    }
                    if ((strcmp(callee->as.member.member, "Take") == 0 || strcmp(callee->as.member.member, "Skip") == 0) &&
                        e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Take/Skip requiere argumento int");
                            return rv_list(calloc(1, sizeof(ListInstance)));
                        }
                        int n = arg.int_value;
                        if (n < 0) n = 0;
                        ListInstance *out = calloc(1, sizeof(ListInstance));
                        int start = strcmp(callee->as.member.member, "Skip") == 0 ? (n > obj.list_value->count ? obj.list_value->count : n) : 0;
                        int end = strcmp(callee->as.member.member, "Take") == 0 ? (n < obj.list_value->count ? n : obj.list_value->count) : obj.list_value->count;
                        for (int i = start; i < end; ++i) {
                            if (out->count >= out->capacity) {
                                int new_cap = out->capacity == 0 ? 4 : out->capacity * 2;
                                out->items = realloc(out->items, sizeof(int) * (size_t)new_cap);
                                out->capacity = new_cap;
                            }
                            out->items[out->count++] = obj.list_value->items[i];
                        }
                        return rv_list(out);
                    }
                    diag_report(ctx->diags, e->span, "metodo de List no soportado: %s", callee->as.member.member);
                    return rv_void();
                }
                if (obj.kind == RV_ARRAY && obj.array_value) {
                    ListInstance *tmp = calloc(1, sizeof(ListInstance));
                    tmp->count = obj.array_value->length;
                    tmp->capacity = tmp->count;
                    if (tmp->count > 0) tmp->items = calloc((size_t)tmp->count, sizeof(int));
                    for (int i = 0; i < tmp->count; ++i) tmp->items[i] = obj.array_value->items[i];
                    RuntimeValue list_obj = rv_list(tmp);
                    Expr fake_callee = *callee;
                    fake_callee.as.member.object = NULL;
                    (void)fake_callee;
                    if (strcmp(callee->as.member.member, "Sum") == 0 && e->as.call.args.count == 0) {
                        int total = 0;
                        for (int i = 0; i < tmp->count; ++i) total += tmp->items[i];
                        return rv_int(total);
                    }
                    if (strcmp(callee->as.member.member, "FirstOrDefault") == 0 && e->as.call.args.count == 0) {
                        return rv_int(tmp->count > 0 ? tmp->items[0] : 0);
                    }
                    if (strcmp(callee->as.member.member, "LastOrDefault") == 0 && e->as.call.args.count == 0) {
                        return rv_int(tmp->count > 0 ? tmp->items[tmp->count - 1] : 0);
                    }
                    if (strcmp(callee->as.member.member, "ElementAtOrDefault") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue idx = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (idx.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "ElementAtOrDefault requiere argumento int");
                            return rv_int(0);
                        }
                        if (idx.int_value < 0 || idx.int_value >= tmp->count) return rv_int(0);
                        return rv_int(tmp->items[idx.int_value]);
                    }
                    if (strcmp(callee->as.member.member, "Count") == 0 && e->as.call.args.count == 0) {
                        return rv_int(tmp->count);
                    }
                    if (strcmp(callee->as.member.member, "Any") == 0 && e->as.call.args.count == 0) {
                        return rv_bool(tmp->count > 0);
                    }
                    if (strcmp(callee->as.member.member, "Where") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Where requiere argumento int");
                            return rv_list(calloc(1, sizeof(ListInstance)));
                        }
                        ListInstance *out = calloc(1, sizeof(ListInstance));
                        for (int i = 0; i < tmp->count; ++i) if (tmp->items[i] >= arg.int_value) {
                            if (out->count >= out->capacity) {
                                int new_cap = out->capacity == 0 ? 4 : out->capacity * 2;
                                out->items = realloc(out->items, sizeof(int) * (size_t)new_cap);
                                out->capacity = new_cap;
                            }
                            out->items[out->count++] = tmp->items[i];
                        }
                        return rv_list(out);
                    }
                    if (strcmp(callee->as.member.member, "Select") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Select requiere argumento int");
                            return list_obj;
                        }
                        for (int i = 0; i < tmp->count; ++i) tmp->items[i] += arg.int_value;
                        return rv_list(tmp);
                    }
                    if (strcmp(callee->as.member.member, "Contains") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue arg = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (arg.kind != RV_INT) {
                            diag_report(ctx->diags, e->span, "Contains requiere argumento int");
                            return rv_bool(0);
                        }
                        for (int i = 0; i < tmp->count; ++i) if (tmp->items[i] == arg.int_value) return rv_bool(1);
                        return rv_bool(0);
                    }
                    if (strcmp(callee->as.member.member, "Reverse") == 0 && e->as.call.args.count == 0) {
                        for (int i = 0; i < tmp->count / 2; ++i) {
                            int j = tmp->count - i - 1;
                            int swap = tmp->items[i];
                            tmp->items[i] = tmp->items[j];
                            tmp->items[j] = swap;
                        }
                        return rv_list(tmp);
                    }
                    if (strcmp(callee->as.member.member, "ToArray") == 0 && e->as.call.args.count == 0) {
                        ArrayInstance *arr = calloc(1, sizeof(ArrayInstance));
                        arr->length = tmp->count;
                        if (arr->length > 0) arr->items = calloc((size_t)arr->length, sizeof(int));
                        for (int i = 0; i < arr->length; ++i) arr->items[i] = tmp->items[i];
                        return rv_array(arr);
                    }
                    if (strcmp(callee->as.member.member, "SequenceEqual") == 0 && e->as.call.args.count == 1) {
                        RuntimeValue other = eval_expr(ctx, frame, e->as.call.args.items[0]);
                        if (other.kind == RV_LIST && other.list_value) {
                            if (other.list_value->count != tmp->count) return rv_bool(0);
                            for (int i = 0; i < tmp->count; ++i) if (tmp->items[i] != other.list_value->items[i]) return rv_bool(0);
                            return rv_bool(1);
                        }
                        if (other.kind == RV_ARRAY && other.array_value) {
                            if (other.array_value->length != tmp->count) return rv_bool(0);
                            for (int i = 0; i < tmp->count; ++i) if (tmp->items[i] != other.array_value->items[i]) return rv_bool(0);
                            return rv_bool(1);
                        }
                        diag_report(ctx->diags, e->span, "SequenceEqual requiere List<int> o int[]");
                        return rv_bool(0);
                    }
                    if ((strcmp(callee->as.member.member, "Min") == 0 ||
                         strcmp(callee->as.member.member, "Max") == 0 ||
                         strcmp(callee->as.member.member, "Average") == 0) && e->as.call.args.count == 0) {
                        if (tmp->count == 0) return rv_int(0);
                        int best = tmp->items[0];
                        int total = 0;
                        for (int i = 0; i < tmp->count; ++i) {
                            int v = tmp->items[i];
                            total += v;
                            if (strcmp(callee->as.member.member, "Min") == 0 && v < best) best = v;
                            if (strcmp(callee->as.member.member, "Max") == 0 && v > best) best = v;
                        }
                        if (strcmp(callee->as.member.member, "Average") == 0) return rv_int(total / tmp->count);
                        return rv_int(best);
                    }
                }
                if (obj.kind == RV_NULL) {
                    diag_report(ctx->diags, e->span, "invocacion sobre null");
                    return rv_void();
                }
                if (callee->as.member.object->kind == EXPR_IDENTIFIER) {
                    return call_method(ctx,
                                       callee->as.member.object->as.identifier,
                                       callee->as.member.member,
                                       e->as.call.args,
                                       frame,
                                       NULL,
                                       e->span);
                }
                diag_report(ctx->diags, e->span, "invocacion de metodo invalida");
            }
            if (callee->kind == EXPR_IDENTIFIER) {
                return call_method(ctx, ctx->current_class, callee->as.identifier, e->as.call.args, frame, ctx->current_this, e->span);
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
            if (strcmp(e->as.new_expr.class_name, "List") == 0) {
                ListInstance *list = calloc(1, sizeof(ListInstance));
                return rv_list(list);
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
            if (idx.kind != RV_INT) {
                diag_report(ctx->diags, e->span, "indice invalido: debe ser int");
                return rv_int(0);
            }
            if (arr.kind == RV_ARRAY && arr.array_value) {
                if (idx.int_value < 0 || idx.int_value >= arr.array_value->length) {
                    diag_report(ctx->diags, e->span, "indice fuera de rango en array");
                    return rv_int(0);
                }
                return rv_int(arr.array_value->items[idx.int_value]);
            }
            if (arr.kind == RV_LIST && arr.list_value) {
                if (idx.int_value < 0 || idx.int_value >= arr.list_value->count) {
                    diag_report(ctx->diags, e->span, "indice fuera de rango en List");
                    return rv_int(0);
                }
                return rv_int(arr.list_value->items[idx.int_value]);
            }
            diag_report(ctx->diags, e->span, "indexacion requiere array o List");
            return rv_int(0);
        }
        case EXPR_CONDITIONAL: {
            RuntimeValue c = eval_expr(ctx, frame, e->as.conditional.condition);
            if (truthy(c)) return eval_expr(ctx, frame, e->as.conditional.when_true);
            return eval_expr(ctx, frame, e->as.conditional.when_false);
        }
    }
    return rv_void();
}

static void exec_stmt(ExecContext *ctx, Frame *frame, Stmt *s) {
    if (ctx->did_return || ctx->did_break || ctx->did_continue || ctx->did_throw) return;
    if (ctx->debug_trace) {
        fprintf(stderr, "[trace] stmt kind=%d line=%d col=%d\n", (int)s->kind, s->span.line, s->span.column);
    }
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
            break;
        }
        case STMT_FOREACH: {
            RuntimeValue iterable = eval_expr(ctx, frame, s->as.foreach_stmt.iterable);
            Frame loop;
            frame_init(&loop, frame);
            if (iterable.kind == RV_ARRAY && iterable.array_value) {
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
            } else if (iterable.kind == RV_LIST && iterable.list_value) {
                for (int i = 0; i < iterable.list_value->count && !ctx->did_return; ++i) {
                    frame_define(&loop, s->as.foreach_stmt.var_name, rv_int(iterable.list_value->items[i]));
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
            } else {
                diag_report(ctx->diags, s->span, "foreach requiere iterable array o List");
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
        case STMT_THROW:
            if (s->as.throw_expr) {
                ctx->thrown_value = eval_expr(ctx, frame, s->as.throw_expr);
            } else if (ctx->has_active_exception) {
                ctx->thrown_value = ctx->active_exception;
            } else {
                diag_report(ctx->diags, s->span, "throw sin excepcion activa");
                ctx->thrown_value = rv_null();
            }
            ctx->did_throw = 1;
            break;
        case STMT_TRY_CATCH: {
            exec_stmt(ctx, frame, s->as.try_catch_stmt.try_block);
            if (ctx->did_throw) {
                RuntimeValue ex = ctx->thrown_value;
                int handled = 0;
                for (size_t i = 0; i < s->as.try_catch_stmt.catch_count; ++i) {
                    CatchClause *cc = &s->as.try_catch_stmt.catches[i];
                    if (cc->catch_name && !runtime_value_matches_type(ctx, ex, cc->catch_type)) continue;
                    ctx->did_throw = 0;
                    Frame catch_frame;
                    frame_init(&catch_frame, frame);
                    if (cc->catch_name) frame_define(&catch_frame, cc->catch_name, ex);
                    int prev_has = ctx->has_active_exception;
                    RuntimeValue prev_ex = ctx->active_exception;
                    ctx->has_active_exception = 1;
                    ctx->active_exception = ex;
                    exec_stmt(ctx, &catch_frame, cc->catch_block);
                    ctx->has_active_exception = prev_has;
                    ctx->active_exception = prev_ex;
                    handled = 1;
                    break;
                }
                if (!handled) ctx->did_throw = 1;
            }
            if (s->as.try_catch_stmt.finally_block) {
                exec_stmt(ctx, frame, s->as.try_catch_stmt.finally_block);
            }
            break;
        }
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

static bool run_program(Program *program, DiagnosticList *diags, const CodegenOptions *options) {
    ExecContext ctx = {
        .program = program,
        .diags = diags,
        .did_return = 0,
        .did_break = 0,
        .did_continue = 0,
        .did_throw = 0,
        .return_value = rv_void(),
        .thrown_value = rv_void(),
        .active_exception = rv_void(),
        .has_active_exception = 0,
        .current_class = "Program",
        .current_this = NULL,
        .debug_trace = options && options->debug_trace ? 1 : 0
    };
    MethodDecl *entry = find_method(&ctx, "Program", "Main", NULL, 0);
    if (!entry) {
        diag_report(diags, (Span){0}, "no se encontro Program.Main() como punto de entrada");
        return false;
    }
    call_method(&ctx, "Program", "Main", (ExprList){0}, NULL, NULL, (Span){0});
    if (ctx.did_throw) {
        char buf[64];
        diag_report(diags, (Span){0}, "excepcion no capturada: %s",
                    runtime_value_to_cstr(ctx.thrown_value, buf, sizeof(buf)));
        return false;
    }
    if (diag_has_errors(diags)) return false;
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
        if (!run_program(program, diags, options)) return false;
    }
    return true;
}
