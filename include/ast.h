#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdbool.h>
#include "token.h"

typedef enum TypeKind {
    TYPE_INT,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_VOID,
    TYPE_CLASS,
    TYPE_UNKNOWN,
} TypeKind;

typedef struct TypeRef {
    TypeKind kind;
    const char *name;
} TypeRef;

typedef enum ExprKind {
    EXPR_INT,
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_IDENTIFIER,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL,
    EXPR_MEMBER,
    EXPR_ASSIGN,
} ExprKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef struct ExprList {
    Expr **items;
    size_t count;
} ExprList;

struct Expr {
    ExprKind kind;
    Span span;
    TypeRef inferred_type;
    union {
        int int_value;
        bool bool_value;
        const char *string_value;
        const char *identifier;
        struct {
            TokenKind op;
            Expr *left;
            Expr *right;
        } binary;
        struct {
            TokenKind op;
            Expr *operand;
        } unary;
        struct {
            Expr *callee;
            ExprList args;
        } call;
        struct {
            Expr *object;
            const char *member;
        } member;
        struct {
            Expr *target;
            Expr *value;
        } assign;
    } as;
};

typedef enum StmtKind {
    STMT_BLOCK,
    STMT_VAR,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_RETURN,
    STMT_BREAK,
    STMT_CONTINUE,
} StmtKind;

typedef struct StmtList {
    Stmt **items;
    size_t count;
} StmtList;

struct Stmt {
    StmtKind kind;
    Span span;
    union {
        StmtList block;
        struct {
            TypeRef type;
            const char *name;
            Expr *initializer;
        } var;
        Expr *expr;
        struct {
            Expr *condition;
            Stmt *then_branch;
            Stmt *else_branch;
        } if_stmt;
        struct {
            Expr *condition;
            Stmt *body;
        } while_stmt;
        struct {
            Stmt *initializer;
            Expr *condition;
            Expr *increment;
            Stmt *body;
        } for_stmt;
        Expr *return_expr;
    } as;
};

typedef struct Param {
    TypeRef type;
    const char *name;
    Span span;
} Param;

typedef struct MethodDecl {
    const char *name;
    TypeRef return_type;
    Param *params;
    size_t param_count;
    Stmt *body;
    bool is_static;
    Span span;
} MethodDecl;

typedef struct FieldDecl {
    TypeRef type;
    const char *name;
    Span span;
} FieldDecl;

typedef struct ClassDecl {
    const char *name;
    MethodDecl *methods;
    size_t method_count;
    FieldDecl *fields;
    size_t field_count;
    Span span;
} ClassDecl;

typedef struct Program {
    ClassDecl *classes;
    size_t class_count;
} Program;

#endif
