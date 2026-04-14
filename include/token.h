#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>

typedef enum TokenKind {
    TOK_EOF = 0,
    TOK_IDENTIFIER,
    TOK_INT_LITERAL,
    TOK_STRING_LITERAL,

    TOK_KW_CLASS,
    TOK_KW_PUBLIC,
    TOK_KW_PRIVATE,
    TOK_KW_STATIC,
    TOK_KW_VOID,
    TOK_KW_INT,
    TOK_KW_BOOL,
    TOK_KW_STRING,
    TOK_KW_RETURN,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_FOR,
    TOK_KW_WHILE,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_NEW,
    TOK_KW_TRUE,
    TOK_KW_FALSE,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_SEMI,
    TOK_COMMA,
    TOK_DOT,

    TOK_ASSIGN,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
} TokenKind;

typedef struct Span {
    int line;
    int column;
    int offset;
    int length;
} Span;

typedef struct Token {
    TokenKind kind;
    char *lexeme;
    Span span;
} Token;

const char *token_kind_name(TokenKind kind);

#endif
