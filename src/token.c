#include "token.h"

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOK_EOF: return "EOF";
        case TOK_IDENTIFIER: return "identifier";
        case TOK_INT_LITERAL: return "int_literal";
        case TOK_STRING_LITERAL: return "string_literal";
        case TOK_KW_CLASS: return "class";
        case TOK_KW_PUBLIC: return "public";
        case TOK_KW_PRIVATE: return "private";
        case TOK_KW_STATIC: return "static";
        case TOK_KW_VOID: return "void";
        case TOK_KW_INT: return "int";
        case TOK_KW_STRING: return "string";
        case TOK_KW_RETURN: return "return";
        case TOK_KW_IF: return "if";
        case TOK_KW_ELSE: return "else";
        case TOK_KW_FOR: return "for";
        case TOK_KW_WHILE: return "while";
        case TOK_KW_NEW: return "new";
        default: return "symbol";
    }
}
