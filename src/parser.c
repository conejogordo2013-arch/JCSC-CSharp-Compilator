#include "parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    Arena *arena;
    Vector *tokens;
    size_t pos;
    DiagnosticList *diags;
    int loop_depth;
} Parser;

static Token *peek(Parser *p) { return (Token *)vector_get(p->tokens, p->pos); }
static Token *prev(Parser *p) { return (Token *)vector_get(p->tokens, p->pos - 1); }
static bool at(Parser *p, TokenKind k) { return peek(p)->kind == k; }
static Token *advance(Parser *p) { if (!at(p, TOK_EOF)) p->pos++; return prev(p); }
static bool match(Parser *p, TokenKind k) { if (at(p, k)) { advance(p); return true; } return false; }

static Token *expect(Parser *p, TokenKind k, const char *msg) {
    if (at(p, k)) return advance(p);
    diag_report(p->diags, peek(p)->span, "%s. token actual: %s", msg, token_kind_name(peek(p)->kind));
    return peek(p);
}

static void *aalloc(Parser *p, size_t size) {
    void *mem = arena_alloc(p->arena, size);
    memset(mem, 0, size);
    return mem;
}

static TypeRef parse_type(Parser *p) {
    Token *t = peek(p);
    if (match(p, TOK_KW_INT)) return (TypeRef){.kind = TYPE_INT, .name = "int"};
    if (match(p, TOK_KW_BOOL)) return (TypeRef){.kind = TYPE_BOOL, .name = "bool"};
    if (match(p, TOK_KW_STRING)) return (TypeRef){.kind = TYPE_STRING, .name = "string"};
    if (match(p, TOK_KW_VOID)) return (TypeRef){.kind = TYPE_VOID, .name = "void"};
    if (match(p, TOK_IDENTIFIER)) return (TypeRef){.kind = TYPE_CLASS, .name = prev(p)->lexeme};
    diag_report(p->diags, t->span, "se esperaba tipo valido");
    return (TypeRef){.kind = TYPE_UNKNOWN, .name = "<error>"};
}

static Expr *parse_expr(Parser *p);

static Expr *new_expr(Parser *p, ExprKind kind, Span span) {
    Expr *e = aalloc(p, sizeof(Expr));
    e->kind = kind;
    e->span = span;
    e->inferred_type = (TypeRef){.kind = TYPE_UNKNOWN, .name = "unknown"};
    return e;
}

static Stmt *new_stmt(Parser *p, StmtKind kind, Span span) {
    Stmt *s = aalloc(p, sizeof(Stmt));
    s->kind = kind;
    s->span = span;
    return s;
}

static Expr *parse_primary(Parser *p) {
    Token *t = peek(p);
    if (match(p, TOK_INT_LITERAL)) {
        Expr *e = new_expr(p, EXPR_INT, prev(p)->span);
        e->as.int_value = atoi(prev(p)->lexeme);
        return e;
    }
    if (match(p, TOK_KW_TRUE)) {
        Expr *e = new_expr(p, EXPR_BOOL, prev(p)->span);
        e->as.bool_value = true;
        return e;
    }
    if (match(p, TOK_KW_FALSE)) {
        Expr *e = new_expr(p, EXPR_BOOL, prev(p)->span);
        e->as.bool_value = false;
        return e;
    }
    if (match(p, TOK_STRING_LITERAL)) {
        Expr *e = new_expr(p, EXPR_STRING, prev(p)->span);
        e->as.string_value = prev(p)->lexeme;
        return e;
    }
    if (match(p, TOK_IDENTIFIER)) {
        Expr *e = new_expr(p, EXPR_IDENTIFIER, prev(p)->span);
        e->as.identifier = prev(p)->lexeme;
        return e;
    }
    if (match(p, TOK_LPAREN)) {
        Expr *e = parse_expr(p);
        expect(p, TOK_RPAREN, "se esperaba ')' al final de expresion");
        return e;
    }
    diag_report(p->diags, t->span, "expresion primaria invalida");
    advance(p);
    return new_expr(p, EXPR_IDENTIFIER, t->span);
}

static Expr *parse_postfix(Parser *p) {
    Expr *expr = parse_primary(p);
    while (true) {
        if (match(p, TOK_DOT)) {
            Token *id = expect(p, TOK_IDENTIFIER, "se esperaba identificador despues de '.'");
            Expr *m = new_expr(p, EXPR_MEMBER, id->span);
            m->as.member.object = expr;
            m->as.member.member = id->lexeme;
            expr = m;
        } else if (match(p, TOK_LPAREN)) {
            Vector args; vector_init(&args, sizeof(Expr *));
            if (!at(p, TOK_RPAREN)) {
                do {
                    Expr *arg = parse_expr(p);
                    vector_push(&args, &arg);
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "se esperaba ')' en llamada");
            Expr *c = new_expr(p, EXPR_CALL, expr->span);
            c->as.call.callee = expr;
            c->as.call.args.count = args.count;
            c->as.call.args.items = aalloc(p, sizeof(Expr *) * args.count);
            memcpy(c->as.call.args.items, args.data, sizeof(Expr *) * args.count);
            vector_free(&args);
            expr = c;
        } else break;
    }
    return expr;
}

static Expr *parse_unary(Parser *p) {
    if (match(p, TOK_NOT) || match(p, TOK_MINUS)) {
        Token *op = prev(p);
        Expr *e = new_expr(p, EXPR_UNARY, op->span);
        e->as.unary.op = op->kind;
        e->as.unary.operand = parse_unary(p);
        return e;
    }
    return parse_postfix(p);
}

static Expr *parse_binary(Parser *p, int prec);
static int precedence(TokenKind k) {
    switch (k) {
        case TOK_OR: return 1;
        case TOK_AND: return 2;
        case TOK_EQ: case TOK_NEQ: return 3;
        case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE: return 4;
        case TOK_PLUS: case TOK_MINUS: return 5;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 6;
        default: return 0;
    }
}

static Expr *parse_binary(Parser *p, int prec) {
    Expr *left = parse_unary(p);
    for (;;) {
        int p2 = precedence(peek(p)->kind);
        if (p2 < prec) break;
        Token *op = advance(p);
        Expr *right = parse_binary(p, p2 + 1);
        Expr *b = new_expr(p, EXPR_BINARY, op->span);
        b->as.binary.op = op->kind;
        b->as.binary.left = left;
        b->as.binary.right = right;
        left = b;
    }
    return left;
}

static Expr *parse_expr(Parser *p) {
    Expr *left = parse_binary(p, 1);
    if (match(p, TOK_ASSIGN)) {
        Expr *value = parse_expr(p);
        Expr *a = new_expr(p, EXPR_ASSIGN, left->span);
        a->as.assign.target = left;
        a->as.assign.value = value;
        return a;
    }
    return left;
}

static Stmt *parse_stmt(Parser *p);

static Stmt *parse_block(Parser *p) {
    Token *open = expect(p, TOK_LBRACE, "se esperaba '{'");
    Vector stmts; vector_init(&stmts, sizeof(Stmt *));
    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
        Stmt *s = parse_stmt(p);
        vector_push(&stmts, &s);
    }
    expect(p, TOK_RBRACE, "se esperaba '}'");
    Stmt *b = new_stmt(p, STMT_BLOCK, open->span);
    b->as.block.count = stmts.count;
    b->as.block.items = aalloc(p, sizeof(Stmt *) * stmts.count);
    memcpy(b->as.block.items, stmts.data, sizeof(Stmt *) * stmts.count);
    vector_free(&stmts);
    return b;
}

static bool is_type_start(Parser *p) {
    return at(p, TOK_KW_INT) || at(p, TOK_KW_BOOL) || at(p, TOK_KW_STRING) || at(p, TOK_IDENTIFIER);
}

static Stmt *parse_stmt(Parser *p) {
    if (at(p, TOK_LBRACE)) return parse_block(p);
    if (match(p, TOK_KW_IF)) {
        Span s = prev(p)->span;
        expect(p, TOK_LPAREN, "se esperaba '('");
        Expr *cond = parse_expr(p);
        expect(p, TOK_RPAREN, "se esperaba ')'");
        Stmt *then_s = parse_stmt(p);
        Stmt *else_s = NULL;
        if (match(p, TOK_KW_ELSE)) else_s = parse_stmt(p);
        Stmt *st = new_stmt(p, STMT_IF, s);
        st->as.if_stmt.condition = cond;
        st->as.if_stmt.then_branch = then_s;
        st->as.if_stmt.else_branch = else_s;
        return st;
    }
    if (match(p, TOK_KW_WHILE)) {
        Span s = prev(p)->span;
        expect(p, TOK_LPAREN, "se esperaba '('");
        Expr *cond = parse_expr(p);
        expect(p, TOK_RPAREN, "se esperaba ')'");
        p->loop_depth++;
        Stmt *body = parse_stmt(p);
        p->loop_depth--;
        Stmt *st = new_stmt(p, STMT_WHILE, s);
        st->as.while_stmt.condition = cond;
        st->as.while_stmt.body = body;
        return st;
    }
    if (match(p, TOK_KW_FOR)) {
        Span s = prev(p)->span;
        expect(p, TOK_LPAREN, "se esperaba '('");
        Stmt *init = NULL;
        if (!at(p, TOK_SEMI)) {
            if (is_type_start(p)) {
                TypeRef t = parse_type(p);
                Token *name = expect(p, TOK_IDENTIFIER, "se esperaba nombre de variable");
                expect(p, TOK_ASSIGN, "se esperaba '=' en for");
                Expr *value = parse_expr(p);
                init = new_stmt(p, STMT_VAR, name->span);
                init->as.var.type = t;
                init->as.var.name = name->lexeme;
                init->as.var.initializer = value;
            } else {
                init = new_stmt(p, STMT_EXPR, peek(p)->span);
                init->as.expr = parse_expr(p);
            }
        }
        expect(p, TOK_SEMI, "se esperaba ';'");
        Expr *cond = at(p, TOK_SEMI) ? NULL : parse_expr(p);
        expect(p, TOK_SEMI, "se esperaba ';'");
        Expr *inc = at(p, TOK_RPAREN) ? NULL : parse_expr(p);
        expect(p, TOK_RPAREN, "se esperaba ')'");
        p->loop_depth++;
        Stmt *body = parse_stmt(p);
        p->loop_depth--;
        Stmt *st = new_stmt(p, STMT_FOR, s);
        st->as.for_stmt.initializer = init;
        st->as.for_stmt.condition = cond;
        st->as.for_stmt.increment = inc;
        st->as.for_stmt.body = body;
        return st;
    }
    if (match(p, TOK_KW_RETURN)) {
        Stmt *st = new_stmt(p, STMT_RETURN, prev(p)->span);
        st->as.return_expr = at(p, TOK_SEMI) ? NULL : parse_expr(p);
        expect(p, TOK_SEMI, "se esperaba ';' despues de return");
        return st;
    }
    if (match(p, TOK_KW_BREAK)) {
        Stmt *st = new_stmt(p, STMT_BREAK, prev(p)->span);
        if (p->loop_depth == 0) diag_report(p->diags, st->span, "'break' solo puede usarse dentro de un bucle");
        expect(p, TOK_SEMI, "se esperaba ';' despues de break");
        return st;
    }
    if (match(p, TOK_KW_CONTINUE)) {
        Stmt *st = new_stmt(p, STMT_CONTINUE, prev(p)->span);
        if (p->loop_depth == 0) diag_report(p->diags, st->span, "'continue' solo puede usarse dentro de un bucle");
        expect(p, TOK_SEMI, "se esperaba ';' despues de continue");
        return st;
    }
    if (is_type_start(p)) {
        size_t save = p->pos;
        TypeRef t = parse_type(p);
        if (at(p, TOK_IDENTIFIER)) {
            Token *name = advance(p);
            if (match(p, TOK_ASSIGN)) {
                Stmt *st = new_stmt(p, STMT_VAR, name->span);
                st->as.var.type = t;
                st->as.var.name = name->lexeme;
                st->as.var.initializer = parse_expr(p);
                expect(p, TOK_SEMI, "se esperaba ';'");
                return st;
            }
            if (match(p, TOK_SEMI)) {
                Stmt *st = new_stmt(p, STMT_VAR, name->span);
                st->as.var.type = t;
                st->as.var.name = name->lexeme;
                st->as.var.initializer = NULL;
                return st;
            }
        }
        p->pos = save;
    }
    Stmt *st = new_stmt(p, STMT_EXPR, peek(p)->span);
    st->as.expr = parse_expr(p);
    expect(p, TOK_SEMI, "se esperaba ';' despues de expresion");
    return st;
}

Program *parse_program(Arena *arena, Vector *tokens, DiagnosticList *diags) {
    Parser p = {.arena = arena, .tokens = tokens, .pos = 0, .diags = diags, .loop_depth = 0};
    Program *prog = arena_alloc(arena, sizeof(Program));
    memset(prog, 0, sizeof(Program));
    Vector classes; vector_init(&classes, sizeof(ClassDecl));

    while (!at(&p, TOK_EOF)) {
        while (match(&p, TOK_KW_PUBLIC) || match(&p, TOK_KW_PRIVATE)) {}
        expect(&p, TOK_KW_CLASS, "se esperaba 'class' al nivel superior");
        Token *cname = expect(&p, TOK_IDENTIFIER, "se esperaba nombre de clase");
        expect(&p, TOK_LBRACE, "se esperaba '{' de clase");
        ClassDecl cls = {.name = cname->lexeme, .span = cname->span};
        Vector methods; vector_init(&methods, sizeof(MethodDecl));
        Vector fields; vector_init(&fields, sizeof(FieldDecl));

        while (!at(&p, TOK_RBRACE) && !at(&p, TOK_EOF)) {
            bool is_static = match(&p, TOK_KW_STATIC);
            TypeRef type = parse_type(&p);
            Token *name = expect(&p, TOK_IDENTIFIER, "se esperaba nombre de miembro");
            if (match(&p, TOK_LPAREN)) {
                Vector params; vector_init(&params, sizeof(Param));
                if (!at(&p, TOK_RPAREN)) {
                    do {
                        TypeRef pt = parse_type(&p);
                        Token *pn = expect(&p, TOK_IDENTIFIER, "se esperaba nombre de parametro");
                        Param param = {.type = pt, .name = pn->lexeme, .span = pn->span};
                        vector_push(&params, &param);
                    } while (match(&p, TOK_COMMA));
                }
                expect(&p, TOK_RPAREN, "se esperaba ')' en firma de metodo");
                Stmt *body = parse_block(&p);
                MethodDecl m = {.name = name->lexeme, .return_type = type, .body = body, .is_static = is_static, .span = name->span};
                m.param_count = params.count;
                m.params = aalloc(&p, sizeof(Param) * params.count);
                memcpy(m.params, params.data, sizeof(Param) * params.count);
                vector_push(&methods, &m);
                vector_free(&params);
            } else {
                expect(&p, TOK_SEMI, "se esperaba ';' en campo");
                FieldDecl f = {.type = type, .name = name->lexeme, .span = name->span};
                vector_push(&fields, &f);
            }
        }
        expect(&p, TOK_RBRACE, "se esperaba cierre de clase");
        cls.method_count = methods.count;
        cls.methods = arena_alloc(arena, sizeof(MethodDecl) * methods.count);
        memcpy(cls.methods, methods.data, sizeof(MethodDecl) * methods.count);
        cls.field_count = fields.count;
        cls.fields = arena_alloc(arena, sizeof(FieldDecl) * fields.count);
        memcpy(cls.fields, fields.data, sizeof(FieldDecl) * fields.count);
        vector_push(&classes, &cls);
        vector_free(&methods);
        vector_free(&fields);
    }

    prog->class_count = classes.count;
    prog->classes = arena_alloc(arena, sizeof(ClassDecl) * classes.count);
    memcpy(prog->classes, classes.data, sizeof(ClassDecl) * classes.count);
    vector_free(&classes);
    return prog;
}
