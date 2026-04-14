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
    TYPE_NULL,
    TYPE_UNKNOWN,
} TypeKind;

typedef struct TypeRef {
    TypeKind kind;
    const char *name;
} TypeRef;

typedef enum ExprKind {
    EXPR_INT,
    EXPR_BOOL,
    EXPR_NULL,
    EXPR_STRING,
    EXPR_IDENTIFIER,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL,
    EXPR_MEMBER,
    EXPR_ASSIGN,
    EXPR_NEW,
    EXPR_INDEX,
    EXPR_CONDITIONAL,
} ExprKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct SwitchCase SwitchCase;
typedef struct CatchClause CatchClause;

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
        struct {
            const char *class_name;
            Expr *array_size_expr;
            bool is_int_array;
        } new_expr;
        struct {
            Expr *array;
            Expr *index;
        } index;
        struct {
            Expr *condition;
            Expr *when_true;
            Expr *when_false;
        } conditional;
    } as;
};

typedef enum StmtKind {
    STMT_BLOCK,
    STMT_VAR,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_DO_WHILE,
    STMT_FOR,
    STMT_FOREACH,
    STMT_SWITCH,
    STMT_RETURN,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_TRY_CATCH,
    STMT_THROW,
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
            Stmt *body;
            Expr *condition;
        } do_while_stmt;
        struct {
            Stmt *initializer;
            Expr *condition;
            Expr *increment;
            Stmt *body;
        } for_stmt;
        struct {
            TypeRef var_type;
            const char *var_name;
            Expr *iterable;
            Stmt *body;
        } foreach_stmt;
        struct {
            Expr *expr;
            SwitchCase *cases;
            size_t case_count;
            Stmt *default_body;
        } switch_stmt;
        Expr *return_expr;
        struct {
            Stmt *try_block;
            CatchClause *catches;
            size_t catch_count;
            Stmt *finally_block;
        } try_catch_stmt;
        Expr *throw_expr;
    } as;
};

struct CatchClause {
    TypeRef catch_type;
    const char *catch_name;
    Stmt *catch_block;
};

struct SwitchCase {
    int value;
    Stmt *body;
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
    bool is_async;
    Span span;
} MethodDecl;

typedef struct FieldDecl {
    TypeRef type;
    const char *name;
    Span span;
} FieldDecl;

typedef struct ClassDecl {
    const char *name;
    bool is_struct;
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
