#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *slice_dup(const char *src, int start, int len) {
    char *s = malloc((size_t)len + 1);
    memcpy(s, src + start, (size_t)len);
    s[len] = '\0';
    return s;
}

static TokenKind keyword_kind(const char *lexeme) {
    if (strcmp(lexeme, "class") == 0) return TOK_KW_CLASS;
    if (strcmp(lexeme, "using") == 0) return TOK_KW_USING;
    if (strcmp(lexeme, "namespace") == 0) return TOK_KW_NAMESPACE;
    if (strcmp(lexeme, "public") == 0) return TOK_KW_PUBLIC;
    if (strcmp(lexeme, "private") == 0) return TOK_KW_PRIVATE;
    if (strcmp(lexeme, "static") == 0) return TOK_KW_STATIC;
    if (strcmp(lexeme, "void") == 0) return TOK_KW_VOID;
    if (strcmp(lexeme, "int") == 0) return TOK_KW_INT;
    if (strcmp(lexeme, "bool") == 0) return TOK_KW_BOOL;
    if (strcmp(lexeme, "string") == 0) return TOK_KW_STRING;
    if (strcmp(lexeme, "return") == 0) return TOK_KW_RETURN;
    if (strcmp(lexeme, "if") == 0) return TOK_KW_IF;
    if (strcmp(lexeme, "else") == 0) return TOK_KW_ELSE;
    if (strcmp(lexeme, "for") == 0) return TOK_KW_FOR;
    if (strcmp(lexeme, "while") == 0) return TOK_KW_WHILE;
    if (strcmp(lexeme, "break") == 0) return TOK_KW_BREAK;
    if (strcmp(lexeme, "continue") == 0) return TOK_KW_CONTINUE;
    if (strcmp(lexeme, "new") == 0) return TOK_KW_NEW;
    if (strcmp(lexeme, "true") == 0) return TOK_KW_TRUE;
    if (strcmp(lexeme, "false") == 0) return TOK_KW_FALSE;
    return TOK_IDENTIFIER;
}

static void push_token(Vector *tokens, TokenKind kind, char *lexeme, int line, int col, int start, int len) {
    Token t = {.kind = kind, .lexeme = lexeme, .span = {.line = line, .column = col, .offset = start, .length = len}};
    vector_push(tokens, &t);
}

void lex_source(const char *source, Vector *tokens, DiagnosticList *diags) {
    int i = 0, line = 1, col = 1;
    while (source[i]) {
        char c = source[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            i++; col++; continue;
        }
        if (c == '\n') {
            i++; line++; col = 1; continue;
        }
        if (c == '/' && source[i + 1] == '/') {
            while (source[i] && source[i] != '\n') { i++; col++; }
            continue;
        }
        if (c == '/' && source[i + 1] == '*') {
            int sl = line, sc = col;
            i += 2; col += 2;
            while (source[i] && !(source[i] == '*' && source[i + 1] == '/')) {
                if (source[i] == '\n') { line++; col = 1; i++; }
                else { i++; col++; }
            }
            if (!source[i]) {
                diag_report(diags, (Span){.line = sl, .column = sc}, "comentario multilinea sin cerrar");
                break;
            }
            i += 2; col += 2;
            continue;
        }

        int start = i, start_col = col;
        if (isalpha((unsigned char)c) || c == '_') {
            while (isalnum((unsigned char)source[i]) || source[i] == '_') { i++; col++; }
            int len = i - start;
            char *lex = slice_dup(source, start, len);
            push_token(tokens, keyword_kind(lex), lex, line, start_col, start, len);
            continue;
        }
        if (isdigit((unsigned char)c)) {
            while (isdigit((unsigned char)source[i])) { i++; col++; }
            int len = i - start;
            push_token(tokens, TOK_INT_LITERAL, slice_dup(source, start, len), line, start_col, start, len);
            continue;
        }
        if (c == '"') {
            i++; col++;
            while (source[i] && source[i] != '"' && source[i] != '\n') { i++; col++; }
            if (source[i] != '"') {
                diag_report(diags, (Span){.line = line, .column = start_col}, "literal string sin cerrar");
                continue;
            }
            i++; col++;
            int len = i - start;
            push_token(tokens, TOK_STRING_LITERAL, slice_dup(source, start + 1, len - 2), line, start_col, start, len);
            continue;
        }

#define P1(ch, tk) case ch: push_token(tokens, tk, slice_dup(source, start, 1), line, start_col, start, 1); i++; col++; break;
        switch (c) {
            P1('(', TOK_LPAREN)
            P1(')', TOK_RPAREN)
            P1('{', TOK_LBRACE)
            P1('}', TOK_RBRACE)
            P1(';', TOK_SEMI)
            P1(',', TOK_COMMA)
            P1('.', TOK_DOT)
            P1('+', TOK_PLUS)
            P1('-', TOK_MINUS)
            P1('*', TOK_STAR)
            P1('%', TOK_PERCENT)
            case '!':
                if (source[i + 1] == '=') { push_token(tokens, TOK_NEQ, slice_dup(source, start, 2), line, start_col, start, 2); i += 2; col += 2; }
                else { push_token(tokens, TOK_NOT, slice_dup(source, start, 1), line, start_col, start, 1); i++; col++; }
                break;
            case '=':
                if (source[i + 1] == '=') { push_token(tokens, TOK_EQ, slice_dup(source, start, 2), line, start_col, start, 2); i += 2; col += 2; }
                else { push_token(tokens, TOK_ASSIGN, slice_dup(source, start, 1), line, start_col, start, 1); i++; col++; }
                break;
            case '<':
                if (source[i + 1] == '=') { push_token(tokens, TOK_LE, slice_dup(source, start, 2), line, start_col, start, 2); i += 2; col += 2; }
                else { push_token(tokens, TOK_LT, slice_dup(source, start, 1), line, start_col, start, 1); i++; col++; }
                break;
            case '>':
                if (source[i + 1] == '=') { push_token(tokens, TOK_GE, slice_dup(source, start, 2), line, start_col, start, 2); i += 2; col += 2; }
                else { push_token(tokens, TOK_GT, slice_dup(source, start, 1), line, start_col, start, 1); i++; col++; }
                break;
            case '/':
                push_token(tokens, TOK_SLASH, slice_dup(source, start, 1), line, start_col, start, 1); i++; col++; break;
            case '&':
                if (source[i + 1] == '&') { push_token(tokens, TOK_AND, slice_dup(source, start, 2), line, start_col, start, 2); i += 2; col += 2; }
                else diag_report(diags, (Span){.line = line, .column = col}, "se esperaba '&' para operador &&");
                break;
            case '|':
                if (source[i + 1] == '|') { push_token(tokens, TOK_OR, slice_dup(source, start, 2), line, start_col, start, 2); i += 2; col += 2; }
                else diag_report(diags, (Span){.line = line, .column = col}, "se esperaba '|' para operador ||");
                break;
            default:
                diag_report(diags, (Span){.line = line, .column = col}, "caracter no reconocido: '%c'", c);
                i++; col++;
                break;
        }
#undef P1
    }
    push_token(tokens, TOK_EOF, slice_dup("", 0, 0), line, col, i, 0);
}
