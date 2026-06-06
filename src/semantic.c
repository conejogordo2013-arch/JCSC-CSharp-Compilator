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

typedef struct SemContext {
    Program *program;
    DiagnosticList *diags;
} SemContext;

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

static TypeRef analyze_expr(Expr *e, Scope *scope, SemContext *ctx);

static int is_truthy_compatible(TypeRef t) {
    return t.kind == TYPE_BOOL || t.kind == TYPE_INT || t.kind == TYPE_UNKNOWN;
}

static int is_int_array_type(TypeRef t) {
    return t.kind == TYPE_CLASS && t.name && strcmp(t.name, "int[]") == 0;
}

static int is_list_type(TypeRef t) {
    return t.kind == TYPE_CLASS && t.name && strcmp(t.name, "List") == 0;
}

static ClassDecl *find_class(Program *program, const char *name) {
    for (size_t i = 0; i < program->class_count; ++i) {
        if (strcmp(program->classes[i].name, name) == 0) return &program->classes[i];
    }
    return NULL;
}

static void add_inherited_fields_to_scope(Program *program, Scope *scope, ClassDecl *c, int depth) {
    if (!c || depth > 32) return;
    for (size_t bi = 0; bi < c->base_type_count; ++bi) {
        ClassDecl *base = find_class(program, c->base_types[bi]);
        if (!base) continue;
        add_inherited_fields_to_scope(program, scope, base, depth + 1);
        for (size_t fi = 0; fi < base->field_count; ++fi) {
            scope_add(scope, base->fields[fi].name, base->fields[fi].type);
        }
    }
}

static int is_interface_name(Program *program, const char *name) {
    for (size_t i = 0; i < program->interface_count; ++i) {
        if (strcmp(program->interfaces[i].name, name) == 0) return 1;
    }
    return 0;
}

static int class_assignable_to(Program *program, const char *from_class, const char *to_type, int depth) {
    if (!from_class || !to_type || depth > 32) return 0;
    if (strcmp(from_class, to_type) == 0) return 1;
    ClassDecl *decl = find_class(program, from_class);
    if (!decl) return 0;
    for (size_t i = 0; i < decl->base_type_count; ++i) {
        const char *base = decl->base_types[i];
        if (strcmp(base, to_type) == 0) return 1;
        if (find_class(program, base) && class_assignable_to(program, base, to_type, depth + 1)) return 1;
    }
    return 0;
}

static int types_compatible(TypeRef expected, TypeRef found, SemContext *ctx) {
    if (expected.kind == TYPE_UNKNOWN || found.kind == TYPE_UNKNOWN) return 1;
    if (expected.kind == TYPE_STRING && found.kind == TYPE_STRING) return 1;
    if (expected.kind == TYPE_INT && found.kind == TYPE_INT) return 1;
    if (expected.kind == TYPE_BOOL && found.kind == TYPE_BOOL) return 1;
    if (expected.kind == TYPE_VOID && found.kind == TYPE_VOID) return 1;
    if (expected.kind == TYPE_NULL && found.kind == TYPE_NULL) return 1;
    if (expected.kind == TYPE_CLASS && found.kind == TYPE_CLASS && expected.name && found.name &&
        strcmp(expected.name, found.name) == 0) return 1;
    if ((expected.kind == TYPE_CLASS || expected.kind == TYPE_STRING) && found.kind == TYPE_NULL) return 1;
    if (expected.kind == TYPE_CLASS && found.kind == TYPE_CLASS &&
        class_assignable_to(ctx->program, found.name, expected.name, 0)) {
        return 1;
    }
    return 0;
}

static void analyze_stmt(Stmt *s, Scope *scope, TypeRef ret_type, SemContext *ctx, int loop_depth, int switch_depth) {
    switch (s->kind) {
        case STMT_BLOCK: {
            Scope child; scope_init(&child, scope);
            for (size_t i = 0; i < s->as.block.count; ++i) analyze_stmt(s->as.block.items[i], &child, ret_type, ctx, loop_depth, switch_depth);
            break;
        }
        case STMT_VAR: {
            if (s->as.var.initializer) {
                TypeRef init = analyze_expr(s->as.var.initializer, scope, ctx);
                if (!types_compatible(s->as.var.type, init, ctx)) {
                    diag_report(ctx->diags, s->span, "tipo incompatible en inicializacion de '%s'", s->as.var.name);
                }
            }
            scope_add(scope, s->as.var.name, s->as.var.type);
            break;
        }
        case STMT_EXPR:
            analyze_expr(s->as.expr, scope, ctx);
            break;
        case STMT_IF:
            if (!is_truthy_compatible(analyze_expr(s->as.if_stmt.condition, scope, ctx))) {
                diag_report(ctx->diags, s->span, "la condicion de if debe ser bool/int");
            }
            analyze_stmt(s->as.if_stmt.then_branch, scope, ret_type, ctx, loop_depth, switch_depth);
            if (s->as.if_stmt.else_branch) analyze_stmt(s->as.if_stmt.else_branch, scope, ret_type, ctx, loop_depth, switch_depth);
            break;
        case STMT_WHILE:
            if (!is_truthy_compatible(analyze_expr(s->as.while_stmt.condition, scope, ctx))) {
                diag_report(ctx->diags, s->span, "la condicion de while debe ser bool/int");
            }
            analyze_stmt(s->as.while_stmt.body, scope, ret_type, ctx, loop_depth + 1, switch_depth);
            break;
        case STMT_DO_WHILE:
            analyze_stmt(s->as.do_while_stmt.body, scope, ret_type, ctx, loop_depth + 1, switch_depth);
            if (!is_truthy_compatible(analyze_expr(s->as.do_while_stmt.condition, scope, ctx))) {
                diag_report(ctx->diags, s->span, "la condicion de do-while debe ser bool/int");
            }
            break;
        case STMT_FOR: {
            Scope child; scope_init(&child, scope);
            if (s->as.for_stmt.initializer) analyze_stmt(s->as.for_stmt.initializer, &child, ret_type, ctx, loop_depth + 1, switch_depth);
            if (s->as.for_stmt.condition && !is_truthy_compatible(analyze_expr(s->as.for_stmt.condition, &child, ctx))) {
                diag_report(ctx->diags, s->span, "la condicion de for debe ser bool/int");
            }
            if (s->as.for_stmt.increment) analyze_expr(s->as.for_stmt.increment, &child, ctx);
            analyze_stmt(s->as.for_stmt.body, &child, ret_type, ctx, loop_depth + 1, switch_depth);
            break;
        }
        case STMT_FOREACH: {
            Scope child; scope_init(&child, scope);
            TypeRef iterable_type = analyze_expr(s->as.foreach_stmt.iterable, &child, ctx);
            if (!is_int_array_type(iterable_type) && !is_list_type(iterable_type) && iterable_type.kind != TYPE_UNKNOWN) {
                diag_report(ctx->diags, s->span, "foreach requiere iterable int[] o List<int>");
            }
            scope_add(&child, s->as.foreach_stmt.var_name, s->as.foreach_stmt.var_type);
            analyze_stmt(s->as.foreach_stmt.body, &child, ret_type, ctx, loop_depth + 1, switch_depth);
            break;
        }
        case STMT_SWITCH:
            analyze_expr(s->as.switch_stmt.expr, scope, ctx);
            for (size_t i = 0; i < s->as.switch_stmt.case_count; ++i) {
                analyze_stmt(s->as.switch_stmt.cases[i].body, scope, ret_type, ctx, loop_depth, switch_depth + 1);
            }
            if (s->as.switch_stmt.default_body) {
                analyze_stmt(s->as.switch_stmt.default_body, scope, ret_type, ctx, loop_depth, switch_depth + 1);
            }
            break;
        case STMT_RETURN: {
            TypeRef found = s->as.return_expr ? analyze_expr(s->as.return_expr, scope, ctx) : (TypeRef){.kind = TYPE_VOID, .name = "void"};
            if (!types_compatible(ret_type, found, ctx)) {
                diag_report(ctx->diags, s->span, "tipo de retorno incompatible. esperado '%s'", ret_type.name);
            }
            break;
        }
        case STMT_BREAK:
        case STMT_CONTINUE:
            if (s->kind == STMT_BREAK) {
                if (loop_depth == 0 && switch_depth == 0) diag_report(ctx->diags, s->span, "'break' fuera de bucle/switch");
            } else if (loop_depth == 0) {
                diag_report(ctx->diags, s->span, "'continue' fuera de bucle");
            }
            break;
        case STMT_TRY_CATCH: {
            analyze_stmt(s->as.try_catch_stmt.try_block, scope, ret_type, ctx, loop_depth, switch_depth);
            for (size_t i = 0; i < s->as.try_catch_stmt.catch_count; ++i) {
                CatchClause *cc = &s->as.try_catch_stmt.catches[i];
                Scope catch_scope;
                scope_init(&catch_scope, scope);
                if (cc->catch_name) scope_add(&catch_scope, cc->catch_name, cc->catch_type);
                analyze_stmt(cc->catch_block, &catch_scope, ret_type, ctx, loop_depth, switch_depth);
            }
            if (s->as.try_catch_stmt.finally_block) {
                analyze_stmt(s->as.try_catch_stmt.finally_block, scope, ret_type, ctx, loop_depth, switch_depth);
            }
            break;
        }
        case STMT_THROW:
            if (s->as.throw_expr) (void)analyze_expr(s->as.throw_expr, scope, ctx);
            break;
    }
}

static TypeRef analyze_expr(Expr *e, Scope *scope, SemContext *ctx) {
    switch (e->kind) {
        case EXPR_INT: return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
        case EXPR_BOOL: return (e->inferred_type = (TypeRef){.kind = TYPE_BOOL, .name = "bool"});
        case EXPR_NULL: return (e->inferred_type = (TypeRef){.kind = TYPE_NULL, .name = "null"});
        case EXPR_STRING: return (e->inferred_type = (TypeRef){.kind = TYPE_STRING, .name = "string"});
        case EXPR_IDENTIFIER: {
            TypeRef t;
            if (!scope_find(scope, e->as.identifier, &t)) {
                diag_report(ctx->diags, e->span, "simbolo no definido: %s", e->as.identifier);
                return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
            }
            return (e->inferred_type = t);
        }
        case EXPR_ASSIGN: {
            TypeRef a = analyze_expr(e->as.assign.target, scope, ctx);
            TypeRef b = analyze_expr(e->as.assign.value, scope, ctx);
            if (!types_compatible(a, b, ctx)) {
                diag_report(ctx->diags, e->span, "asignacion con tipos incompatibles");
            }
            return (e->inferred_type = a);
        }
        case EXPR_BINARY: {
            TypeRef l = analyze_expr(e->as.binary.left, scope, ctx);
            TypeRef r = analyze_expr(e->as.binary.right, scope, ctx);
            if (e->as.binary.op == TOK_PLUS &&
                l.kind != TYPE_UNKNOWN && r.kind != TYPE_UNKNOWN) {
                int both_int = (l.kind == TYPE_INT && r.kind == TYPE_INT);
                int has_string = (l.kind == TYPE_STRING || r.kind == TYPE_STRING);
                if (!both_int && !has_string) {
                    diag_report(ctx->diags, e->span, "operador '+' requiere enteros o al menos un string");
                }
            } else if ((e->as.binary.op == TOK_MINUS || e->as.binary.op == TOK_STAR ||
                        e->as.binary.op == TOK_SLASH || e->as.binary.op == TOK_PERCENT) &&
                       l.kind != TYPE_UNKNOWN && r.kind != TYPE_UNKNOWN &&
                       (l.kind != TYPE_INT || r.kind != TYPE_INT)) {
                diag_report(ctx->diags, e->span, "operacion aritmetica requiere enteros");
            }
            if (e->as.binary.op == TOK_EQ || e->as.binary.op == TOK_NEQ ||
                e->as.binary.op == TOK_LT || e->as.binary.op == TOK_LE ||
                e->as.binary.op == TOK_GT || e->as.binary.op == TOK_GE ||
                e->as.binary.op == TOK_AND || e->as.binary.op == TOK_OR) {
                return (e->inferred_type = (TypeRef){.kind = TYPE_BOOL, .name = "bool"});
            }
            if (e->as.binary.op == TOK_COALESCE) {
                if (l.kind == TYPE_NULL) return (e->inferred_type = r);
                if (r.kind == TYPE_NULL) return (e->inferred_type = l);
                return (e->inferred_type = l.kind != TYPE_UNKNOWN ? l : r);
            }
            if (e->as.binary.op == TOK_PLUS && (l.kind == TYPE_STRING || r.kind == TYPE_STRING)) {
                return (e->inferred_type = (TypeRef){.kind = TYPE_STRING, .name = "string"});
            }
            return (e->inferred_type = l);
        }
        case EXPR_UNARY:
            return (e->inferred_type = analyze_expr(e->as.unary.operand, scope, ctx));
        case EXPR_MEMBER: {
            TypeRef obj = analyze_expr(e->as.member.object, scope, ctx);
            if (is_int_array_type(obj) && strcmp(e->as.member.member, "Length") == 0) {
                return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
            }
            if (is_list_type(obj) && strcmp(e->as.member.member, "Count") == 0) {
                return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
            }
            return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
        }
        case EXPR_CALL:
            for (size_t i = 0; i < e->as.call.args.count; ++i) analyze_expr(e->as.call.args.items[i], scope, ctx);
            if (e->as.call.callee->kind == EXPR_MEMBER) {
                Expr *obj_expr = e->as.call.callee->as.member.object;
                TypeRef obj_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"};
                if (obj_expr->kind == EXPR_IDENTIFIER) {
                    TypeRef scoped_type;
                    if (scope_find(scope, obj_expr->as.identifier, &scoped_type)) {
                        obj_type = scoped_type;
                    }
                } else {
                    obj_type = analyze_expr(obj_expr, scope, ctx);
                }
                const char *member = e->as.call.callee->as.member.member;
                if (is_list_type(obj_type) || is_int_array_type(obj_type)) {
                    size_t argc = e->as.call.args.count;
                    TypeRef arg0 = argc > 0 ? e->as.call.args.items[0]->inferred_type : (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"};
                    TypeRef arg1 = argc > 1 ? e->as.call.args.items[1]->inferred_type : (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"};

                    if (strcmp(member, "Add") == 0 || strcmp(member, "Where") == 0 || strcmp(member, "Select") == 0 ||
                        strcmp(member, "Contains") == 0 || strcmp(member, "Remove") == 0 || strcmp(member, "All") == 0 ||
                        strcmp(member, "Take") == 0 || strcmp(member, "Skip") == 0 || strcmp(member, "RemoveAt") == 0 ||
                        strcmp(member, "ElementAtOrDefault") == 0) {
                        if (argc != 1) {
                            diag_report(ctx->diags, e->span, "%s requiere 1 argumento", member);
                        } else if (arg0.kind != TYPE_INT && arg0.kind != TYPE_UNKNOWN) {
                            diag_report(ctx->diags, e->span, "%s requiere argumento int", member);
                        }
                    } else if (strcmp(member, "Insert") == 0) {
                        if (argc != 2) {
                            diag_report(ctx->diags, e->span, "Insert requiere 2 argumentos");
                        } else {
                            if (arg0.kind != TYPE_INT && arg0.kind != TYPE_UNKNOWN) {
                                diag_report(ctx->diags, e->span, "Insert requiere index int");
                            }
                            if (arg1.kind != TYPE_INT && arg1.kind != TYPE_UNKNOWN) {
                                diag_report(ctx->diags, e->span, "Insert requiere value int");
                            }
                        }
                    } else if (strcmp(member, "Clear") == 0 || strcmp(member, "Distinct") == 0 || strcmp(member, "OrderBy") == 0 ||
                               strcmp(member, "Reverse") == 0 || strcmp(member, "ToArray") == 0 || strcmp(member, "FirstOrDefault") == 0 ||
                               strcmp(member, "LastOrDefault") == 0 || strcmp(member, "Sum") == 0 || strcmp(member, "Count") == 0 ||
                               strcmp(member, "Any") == 0 || strcmp(member, "Min") == 0 || strcmp(member, "Max") == 0 ||
                               strcmp(member, "Average") == 0) {
                        if (argc != 0) diag_report(ctx->diags, e->span, "%s no recibe argumentos", member);
                    } else if (strcmp(member, "SequenceEqual") == 0) {
                        if (argc != 1) {
                            diag_report(ctx->diags, e->span, "SequenceEqual requiere 1 argumento");
                        } else if (!is_list_type(arg0) && !is_int_array_type(arg0) && arg0.kind != TYPE_UNKNOWN) {
                            diag_report(ctx->diags, e->span, "SequenceEqual requiere List<int> o int[]");
                        }
                    } else {
                        diag_report(ctx->diags, e->span, "metodo de coleccion no soportado: %s", member);
                    }

                    if (strcmp(member, "Where") == 0 || strcmp(member, "Select") == 0) {
                        return (e->inferred_type = (TypeRef){.kind = TYPE_CLASS, .name = "List"});
                    }
                    if (strcmp(member, "Distinct") == 0 || strcmp(member, "OrderBy") == 0 ||
                        strcmp(member, "Take") == 0 || strcmp(member, "Skip") == 0 ||
                        strcmp(member, "Reverse") == 0) {
                        return (e->inferred_type = (TypeRef){.kind = TYPE_CLASS, .name = "List"});
                    }
                    if (strcmp(member, "ToArray") == 0) {
                        return (e->inferred_type = (TypeRef){.kind = TYPE_CLASS, .name = "int[]"});
                    }
                    if (strcmp(member, "FirstOrDefault") == 0 || strcmp(member, "Sum") == 0 ||
                        strcmp(member, "Count") == 0 || strcmp(member, "Min") == 0 ||
                        strcmp(member, "Max") == 0 || strcmp(member, "Average") == 0 ||
                        strcmp(member, "LastOrDefault") == 0 || strcmp(member, "ElementAtOrDefault") == 0) {
                        return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
                    }
                    if (strcmp(member, "Any") == 0 || strcmp(member, "All") == 0) {
                        return (e->inferred_type = (TypeRef){.kind = TYPE_BOOL, .name = "bool"});
                    }
                    if (strcmp(member, "Contains") == 0 || strcmp(member, "Remove") == 0 ||
                        strcmp(member, "SequenceEqual") == 0) {
                        return (e->inferred_type = (TypeRef){.kind = TYPE_BOOL, .name = "bool"});
                    }
                }
            }
            return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
        case EXPR_NEW:
            return (e->inferred_type = (TypeRef){.kind = TYPE_CLASS, .name = e->as.new_expr.class_name});
        case EXPR_INDEX: {
            analyze_expr(e->as.index.array, scope, ctx);
            TypeRef idx = analyze_expr(e->as.index.index, scope, ctx);
            if (idx.kind != TYPE_INT && idx.kind != TYPE_UNKNOWN) {
                diag_report(ctx->diags, e->span, "indice de array debe ser int");
            }
            return (e->inferred_type = (TypeRef){.kind = TYPE_INT, .name = "int"});
        }
        case EXPR_CONDITIONAL: {
            TypeRef c = analyze_expr(e->as.conditional.condition, scope, ctx);
            TypeRef t = analyze_expr(e->as.conditional.when_true, scope, ctx);
            TypeRef f = analyze_expr(e->as.conditional.when_false, scope, ctx);
            if (!is_truthy_compatible(c)) {
                diag_report(ctx->diags, e->span, "la condicion del operador ternario debe ser bool/int");
            }
            if (types_compatible(t, f, ctx)) return (e->inferred_type = t);
            if (types_compatible(f, t, ctx)) return (e->inferred_type = f);
            diag_report(ctx->diags, e->span, "ramas incompatibles en operador ternario");
            return (e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"});
        }
    }
    return (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"};
}

void semantic_analyze(Program *program, DiagnosticList *diags) {
    SemContext ctx = {.program = program, .diags = diags};
    for (size_t ci = 0; ci < program->class_count; ++ci) {
        ClassDecl *c = &program->classes[ci];
        for (size_t bi = 0; bi < c->base_type_count; ++bi) {
            const char *base = c->base_types[bi];
            if (!find_class(program, base) && !is_interface_name(program, base)) {
                diag_report(diags, c->span, "tipo base/interface no definido: %s", base);
            }
        }
    }

    for (size_t ci = 0; ci < program->class_count; ++ci) {
        ClassDecl *c = &program->classes[ci];
        for (size_t mi = 0; mi < c->method_count; ++mi) {
            MethodDecl *m = &c->methods[mi];
            Scope root; scope_init(&root, NULL);
            if (!m->is_static) {
                add_inherited_fields_to_scope(program, &root, c, 0);
                for (size_t fi = 0; fi < c->field_count; ++fi) {
                    scope_add(&root, c->fields[fi].name, c->fields[fi].type);
                }
            }
            for (size_t pi = 0; pi < m->param_count; ++pi) scope_add(&root, m->params[pi].name, m->params[pi].type);
            analyze_stmt(m->body, &root, m->return_type, &ctx, 0, 0);
        }
    }
}
