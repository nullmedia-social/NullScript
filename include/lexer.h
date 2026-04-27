#ifndef LEXER_H
#define LEXER_H

#include "token.h"

typedef struct {
    const char *src;
    int         pos;
    int         line;
    int         col;
    int         tilde_count; // tracks judging opportunities
} Lexer;

Lexer  *lexer_new(const char *src);
void    lexer_free(Lexer *l);
Token  *lexer_next(Lexer *l);

// Tokenise the entire source into a NULL-terminated array of Token*
Token **lexer_tokenise(const char *src);
void    tokens_free(Token **tokens);

#endif
