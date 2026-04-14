#include "semantic.h"

#include <string.h>

typedef struct Symbol {
    const char *name;
    TypeRef type;
} Symbol;

typedef struct Scope {
    Symbol symbols[256];
    size_t count;
    struct Scope *parent;
} Scope;

static void scope_init(Scope *s, Scope *parent) { s->count = 0; s->parent = parent; }

static void scope_add(Scope *s, const char *name, TypeRef type) {
    if (s->count < 256) s->symbols[s->count++] = (Symbol){.name = name, .type = type};
}

static bool scope_find(Scope *s, const char *name, TypeRef *type) {
    for (Scope *it = s; it; it = it->parent) {
        for (size_t i = 0; i < it->count; ++i) {
            if (strcmp(it->symbols[i].name, name) == 0) {
                *type = it->symbols[i].type;
                return true;
            }
        }
    }
    return false;
}

static TypeRef analyze_expr(Expr *e, Scope *scope, DiagnosticList *diags);
static int is_truthy_compatible(TypeRef t) {
    return t.kind == TYPE_BOOL || t.kind == TYPE_INT || t.kind == TYPE_UNKNOWN;
}
static int is_int_array_type(TypeRef t) {
    return t.kind == TYPE_CLASS && t.name && strcmp(t.name, "int[]") == 0;
}
static int types_compatible(TypeRef expected, TypeRef found) {
    if (expected.kind == TYPE_UNKNOWN || found.kind == TYPE_UNKNOWN) return 1;
    if (expected.kind == found.kind) return 1;
    if (expected.kind == TYPE_CLASS && found.kind == TYPE_NULL) return 1;
    return 0;
}

static void analyze_stmt(Stmt *s, Scope *scope, TypeRef ret_type, DiagnosticList *diags, int loop_depth, int switch_depth) {
    switch (s->kind) {
        case STMT_BLOCK: {
            Scope child; scope_init(&child, scope);
            for (size_t i = 0; i < s->as.block.count; ++i) analyze_stmt(s->as.block.items[i], &child, ret_type, diags, loop_depth, switch_depth);
            break;
        }
        case STMT_VAR: {
            if (s->as.var.initializer) {
                TypeRef init = analyze_expr(s->as.var.initializer, scope, diags);
                if (!types_compatible(s->as.var.type, init)) {
                    diag_report(diags, s->span, "tipo incompatible en inicializacion de '%s'", s->as.var.name);
                }
            }
            scope_add(scope, s->as.var.name, s->as.var.type);
            break;
        }
        case STMT_EXPR:
            analyze_expr(s->as.expr, scope, diags);
            break;
        case STMT_IF:
            if (!is_truthy_compatible(analyze_expr(s->as.if_stmt.condition, scope, diags))) {
                diag_report(diags, s->span, "la condicion de if debe ser bool/int");
            }
            analyze_stmt(s->as.if_stmt.then_branch, scope, ret_type, diags, loop_depth, switch_depth);
            if (s->as.if_stmt.else_branch) analyze_stmt(s->as.if_stmt.else_branch, scope, ret_type, diags, loop_depth, switch_depth);
            break;
        case STMT_WHILE:
            if (!is_truthy_compatible(analyze_expr(s->as.while_stmt.condition, scope, diags))) {
                diag_report(diags, s->span, "la condicion de while debe ser bool/int");
            }
            analyze_stmt(s->as.while_stmt.body, scope, ret_type, diags, loop_depth + 1, switch_depth);
            break;
        case STMT_DO_WHILE:
            analyze_stmt(s->as.do_while_stmt.body, scope, ret_type, diags, loop_depth + 1, switch_depth);
            if (!is_truthy_compatible(analyze_expr(s->as.do_while_stmt.condition, scope, diags))) {
                diag_report(diags, s->span, "la condicion de do-while debe ser bool/int");
            }
            break;
        case STMT_FOR: {
            Scope child; scope_init(&child, scope);
            if (s->as.for_stmt.initializer) analyze_stmt(s->as.for_stmt.initializer, &child, ret_type, diags, loop_depth + 1, switch_depth);
            if (s->as.for_stmt.condition && !is_truthy_compatible(analyze_expr(s->as.for_stmt.condition, &child, diags))) {
                diag_report(diags, s->span, "la condicion de for debe ser bool/int");
            }
            if (s->as.for_stmt.increment) analyze_expr(s->as.for_stmt.increment, &child, diags);
            analyze_stmt(s->as.for_stmt.body, &child, ret_type, diags, loop_depth + 1, switch_depth);
            break;
        }
        case STMT_FOREACH: {
            Scope child; scope_init(&child, scope);
            TypeRef iterable_type = analyze_expr(s->as.foreach_stmt.iterable, &child, diags);
            if (!is_int_array_type(iterable_type) && iterable_type.kind != TYPE_UNKNOWN) {
                diag_report(diags, s->span, "foreach requiere iterable int[]");
            }
            scope_add(&child, s->as.foreach_stmt.var_name, s->as.foreach_stmt.var_type);
            analyze_stmt(s->as.foreach_stmt.body, &child, ret_type, diags, loop_depth + 1, switch_depth);
            break;
        }
        case STMT_SWITCH:
            analyze_expr(s->as.switch_stmt.expr, scope, diags);
            for (size_t i = 0; i < s->as.switch_stmt.case_count; ++i) {
                analyze_stmt(s->as.switch_stmt.cases[i].body, scope, ret_type, diags, loop_depth, switch_depth + 1);
            }
            if (s->as.switch_stmt.default_body) {
                analyze_stmt(s->as.switch_stmt.default_body, scope, ret_type, diags, loop_depth, switch_depth + 1);
            }
            break;
        case STMT_RETURN: {
            TypeRef found = s->as.return_expr ? analyze_expr(s->as.return_expr, scope, diags) : (TypeRef){.kind = TYPE_VOID, .name = "void"};
            if (!types_compatible(ret_type, found)) {
                diag_report(diags, s->span, "tipo de retorno incompatible. esperado '%s'", ret_type.name);
            }
            break;
        }
        case STMT_BREAK:
        case STMT_CONTINUE:
            if (s->kind == STMT_BREAK) {
                if (loop_depth == 0 && switch_depth == 0) diag_report(diags, s->span, "'break' fuera de bucle/switch");
            } else if (loop_depth == 0) {
                diag_report(diags, s->span, "'continue' fuera de bucle");
            }
            break;
    }
}

static TypeRef analyze_expr(Expr *e, Scope *scope, DiagnosticList *diags) {
    switch (e->kind) {
        case EXPR_INT: return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
        case EXPR_BOOL: return (e->inferred_type = (TypeRef){.kind = TYPE_BOOL, .name = "bool"});
        case EXPR_NULL: return (e->inferred_type = (TypeRef){.kind = TYPE_NULL, .name = "null"});
        case EXPR_STRING: return (e->inferred_type = (TypeRef){.kind = TYPE_STRING, .name = "string"});
        case EXPR_IDENTIFIER: {
            TypeRef t;
            if (!scope_find(scope, e->as.identifier, &t)) {
                diag_report(diags, e->span, "simbolo no definido: %s", e->as.identifier);
                return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
            }
            return (e->inferred_type = t);
        }
        case EXPR_ASSIGN: {
            TypeRef a = analyze_expr(e->as.assign.target, scope, diags);
            TypeRef b = analyze_expr(e->as.assign.value, scope, diags);
            if (!types_compatible(a, b)) {
                diag_report(diags, e->span, "asignacion con tipos incompatibles");
            }
            return (e->inferred_type = a);
        }
        case EXPR_BINARY: {
            TypeRef l = analyze_expr(e->as.binary.left, scope, diags);
            TypeRef r = analyze_expr(e->as.binary.right, scope, diags);
            if ((e->as.binary.op == TOK_PLUS || e->as.binary.op == TOK_MINUS || e->as.binary.op == TOK_STAR || e->as.binary.op == TOK_SLASH || e->as.binary.op == TOK_PERCENT) &&
                l.kind != TYPE_UNKNOWN && r.kind != TYPE_UNKNOWN &&
                (l.kind != TYPE_INT || r.kind != TYPE_INT)) {
                diag_report(diags, e->span, "operacion aritmetica requiere enteros");
            }
            if (e->as.binary.op == TOK_EQ || e->as.binary.op == TOK_NEQ ||
                e->as.binary.op == TOK_LT || e->as.binary.op == TOK_LE ||
                e->as.binary.op == TOK_GT || e->as.binary.op == TOK_GE ||
                e->as.binary.op == TOK_AND || e->as.binary.op == TOK_OR) {
                return (e->inferred_type = (TypeRef){.kind = TYPE_BOOL, .name = "bool"});
            }
            return (e->inferred_type = l);
        }
        case EXPR_UNARY:
            return (e->inferred_type = analyze_expr(e->as.unary.operand, scope, diags));
        case EXPR_MEMBER:
            {
                TypeRef obj = analyze_expr(e->as.member.object, scope, diags);
                if (is_int_array_type(obj) && strcmp(e->as.member.member, "Length") == 0) {
                    return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
                }
                return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
            }
        case EXPR_CALL:
            for (size_t i = 0; i < e->as.call.args.count; ++i) analyze_expr(e->as.call.args.items[i], scope, diags);
            return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
        case EXPR_NEW:
            return (e->inferred_type = (TypeRef){.kind = TYPE_CLASS, .name = e->as.new_expr.class_name});
        case EXPR_INDEX: {
            analyze_expr(e->as.index.array, scope, diags);
            TypeRef idx = analyze_expr(e->as.index.index, scope, diags);
            if (idx.kind != TYPE_INT && idx.kind != TYPE_UNKNOWN) {
                diag_report(diags, e->span, "indice de array debe ser int");
            }
            return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
        }
    }
    return (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"};
}

void semantic_analyze(Program *program, DiagnosticList *diags) {
    for (size_t ci = 0; ci < program->class_count; ++ci) {
        ClassDecl *c = &program->classes[ci];
        for (size_t mi = 0; mi < c->method_count; ++mi) {
            MethodDecl *m = &c->methods[mi];
            Scope root; scope_init(&root, NULL);
            if (!m->is_static) {
                for (size_t fi = 0; fi < c->field_count; ++fi) {
                    scope_add(&root, c->fields[fi].name, c->fields[fi].type);
                }
            }
            for (size_t pi = 0; pi < m->param_count; ++pi) scope_add(&root, m->params[pi].name, m->params[pi].type);
            analyze_stmt(m->body, &root, m->return_type, diags, 0, 0);
        }
    }
}
