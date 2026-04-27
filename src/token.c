#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include "token.h"

const char *token_type_name(TokenType t) {
    switch (t) {
#define X(n) case n: return #n;
        X(TOK_STRING) X(TOK_INTEGER) X(TOK_FLOAT) X(TOK_DECIMAL)
        X(TOK_BOOL_TRUE) X(TOK_BOOL_FALSE) X(TOK_NULL)
        X(TOK_IDENT) X(TOK_VAR) X(TOK_CONST) X(TOK_FUNC)
        X(TOK_IF) X(TOK_ELSE) X(TOK_REPEAT) X(TOK_IMPORT)
        X(TOK_ALIAS) X(TOK_RETURN) X(TOK_EXPORT)
        X(TOK_AND) X(TOK_OR) X(TOK_NOT)
        X(TOK_ASYNC) X(TOK_AWAIT) X(TOK_SPAWN)
        X(TOK_TRY) X(TOK_CATCH)
        X(TOK_TYPE_STRING) X(TOK_TYPE_INTEGER) X(TOK_TYPE_FLOAT)
        X(TOK_TYPE_DECIMAL) X(TOK_TYPE_BOOL) X(TOK_TYPE_ANY)
        X(TOK_TYPE_ARRAY) X(TOK_TYPE_DICT) X(TOK_TYPE_NULL) X(TOK_TYPE_UNDEFINED)
        X(TOK_SCOPE_GLOBAL) X(TOK_SCOPE_FUNCTION)
        X(TOK_PLUS) X(TOK_MINUS) X(TOK_STAR) X(TOK_SLASH)
        X(TOK_PERCENT) X(TOK_STARSTAR) X(TOK_DOTDOT)
        X(TOK_EQ) X(TOK_PLUSEQ) X(TOK_MINUSEQ) X(TOK_STAREQ)
        X(TOK_SLASHEQ) X(TOK_PERCENTEQ) X(TOK_STARSTAREQ)
        X(TOK_EQEQ) X(TOK_NEQ) X(TOK_LT) X(TOK_GT) X(TOK_LTE) X(TOK_GTE)
        X(TOK_LPAREN) X(TOK_RPAREN) X(TOK_LBRACE) X(TOK_RBRACE)
        X(TOK_LBRACKET) X(TOK_RBRACKET)
        X(TOK_COMMA) X(TOK_COLON) X(TOK_DOT) X(TOK_HASH)
        X(TOK_ESTRING) X(TOK_NEWLINE) X(TOK_EOF) X(TOK_ERROR)
#undef X
        default: return "UNKNOWN";
    }
}

void token_free(Token *tok) {
    if (!tok) return;
    free(tok->value);
    free(tok);
}
