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

static void analyze_stmt(Stmt *s, Scope *scope, TypeRef ret_type, DiagnosticList *diags) {
    switch (s->kind) {
        case STMT_BLOCK: {
            Scope child; scope_init(&child, scope);
            for (size_t i = 0; i < s->as.block.count; ++i) analyze_stmt(s->as.block.items[i], &child, ret_type, diags);
            break;
        }
        case STMT_VAR: {
            if (s->as.var.initializer) {
                TypeRef init = analyze_expr(s->as.var.initializer, scope, diags);
                if (s->as.var.type.kind != TYPE_UNKNOWN && init.kind != TYPE_UNKNOWN && s->as.var.type.kind != init.kind) {
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
            analyze_expr(s->as.if_stmt.condition, scope, diags);
            analyze_stmt(s->as.if_stmt.then_branch, scope, ret_type, diags);
            if (s->as.if_stmt.else_branch) analyze_stmt(s->as.if_stmt.else_branch, scope, ret_type, diags);
            break;
        case STMT_WHILE:
            analyze_expr(s->as.while_stmt.condition, scope, diags);
            analyze_stmt(s->as.while_stmt.body, scope, ret_type, diags);
            break;
        case STMT_FOR: {
            Scope child; scope_init(&child, scope);
            if (s->as.for_stmt.initializer) analyze_stmt(s->as.for_stmt.initializer, &child, ret_type, diags);
            if (s->as.for_stmt.condition) analyze_expr(s->as.for_stmt.condition, &child, diags);
            if (s->as.for_stmt.increment) analyze_expr(s->as.for_stmt.increment, &child, diags);
            analyze_stmt(s->as.for_stmt.body, &child, ret_type, diags);
            break;
        }
        case STMT_RETURN: {
            TypeRef found = s->as.return_expr ? analyze_expr(s->as.return_expr, scope, diags) : (TypeRef){.kind = TYPE_VOID, .name = "void"};
            if (ret_type.kind != TYPE_UNKNOWN && ret_type.kind != found.kind) {
                diag_report(diags, s->span, "tipo de retorno incompatible. esperado '%s'", ret_type.name);
            }
            break;
        }
    }
}

static TypeRef analyze_expr(Expr *e, Scope *scope, DiagnosticList *diags) {
    switch (e->kind) {
        case EXPR_INT: return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
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
            if (a.kind != TYPE_UNKNOWN && b.kind != TYPE_UNKNOWN && a.kind != b.kind) {
                diag_report(diags, e->span, "asignacion con tipos incompatibles");
            }
            return (e->inferred_type = a);
        }
        case EXPR_BINARY: {
            TypeRef l = analyze_expr(e->as.binary.left, scope, diags);
            TypeRef r = analyze_expr(e->as.binary.right, scope, diags);
            if ((e->as.binary.op == TOK_PLUS || e->as.binary.op == TOK_MINUS || e->as.binary.op == TOK_STAR || e->as.binary.op == TOK_SLASH) &&
                (l.kind != TYPE_INT || r.kind != TYPE_INT)) {
                diag_report(diags, e->span, "operacion aritmetica requiere enteros");
            }
            return (e->inferred_type = l);
        }
        case EXPR_UNARY:
            return (e->inferred_type = analyze_expr(e->as.unary.operand, scope, diags));
        case EXPR_MEMBER:
            analyze_expr(e->as.member.object, scope, diags);
            return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
        case EXPR_CALL:
            for (size_t i = 0; i < e->as.call.args.count; ++i) analyze_expr(e->as.call.args.items[i], scope, diags);
            return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
    }
    return (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"};
}

void semantic_analyze(Program *program, DiagnosticList *diags) {
    for (size_t ci = 0; ci < program->class_count; ++ci) {
        ClassDecl *c = &program->classes[ci];
        for (size_t mi = 0; mi < c->method_count; ++mi) {
            MethodDecl *m = &c->methods[mi];
            Scope root; scope_init(&root, NULL);
            for (size_t pi = 0; pi < m->param_count; ++pi) scope_add(&root, m->params[pi].name, m->params[pi].type);
            analyze_stmt(m->body, &root, m->return_type, diags);
        }
    }
}
