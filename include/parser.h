#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "ast.h"

typedef struct {
    Token **tokens;
    int     pos;
    int     had_error;
} Parser;

Parser  *parser_new(Token **tokens);
void     parser_free(Parser *p);
ASTNode *parse(Token **tokens);
ASTNode *parse_single_expr(Token **tokens);

#endif
