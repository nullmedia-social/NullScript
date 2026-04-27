#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "interp.h"
#include "parser.h"

// ── Helpers ────────────────────────────────────────────────────────────────

static Token *cur_tok(Parser *p) {
    return p->tokens[p->pos];
}
static Token *peek_tok(Parser *p) {
    return p->tokens[p->pos + 1];
}
static Token *advance_tok(Parser *p) {
    Token *t = p->tokens[p->pos];
    if (t->type != TOK_EOF) p->pos++;
    return t;
}
static int check(Parser *p, TokenType t) {
    return cur_tok(p)->type == t;
}
static int match(Parser *p, TokenType t) {
    if (check(p, t)) { advance_tok(p); return 1; }
    return 0;
}
static void skip_newlines(Parser *p) {
    while (check(p, TOK_NEWLINE)) advance_tok(p);
}
static Token *expect(Parser *p, TokenType t, const char *ctx) {
    if (cur_tok(p)->type == t) return advance_tok(p);
    fprintf(stderr,
        "\033[31m[NullScript Error] line %d: expected %s in %s, got '%s' (%s). "
        "you did something deeply wrong.\033[0m\n",
        cur_tok(p)->line, token_type_name(t), ctx,
        cur_tok(p)->value, token_type_name(cur_tok(p)->type));
    p->had_error = 1;
    return cur_tok(p);
}

// Forward declarations
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_block(Parser *p);
static ASTNode *parse_func_decl(Parser *p);

// ── Primary expression ─────────────────────────────────────────────────────

static ASTNode *parse_array_or_dict(Parser *p) {
    int line = cur_tok(p)->line;
    expect(p, TOK_LBRACE, "array/dict literal");
    skip_newlines(p);

    // Peek to decide: if first token is IDENT/STRING followed by :, it's a dict key-value
    // We do a best-effort parse of items and let runtime sort it
    ASTNode *node = ast_node_new(NODE_ARRAY, line); // may become dict

    int has_colon = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        skip_newlines(p);

        // Check for key: value pair
        // key can be an ident or string, followed by :
        int is_kv = 0;
        if ((check(p, TOK_IDENT) || check(p, TOK_STRING)) &&
             peek_tok(p)->type == TOK_COLON) {
            is_kv = 1;
            has_colon = 1;
        }

        if (is_kv) {
            node->type = NODE_DICT;
            Token *key = advance_tok(p); // key
            advance_tok(p);              // :
            ASTNode *val = parse_expr(p);
            ASTNode *kv  = ast_node_new(NODE_NAMED_ARG, key->line);
            kv->sval     = strdup(key->value);
            ast_node_add_child(kv, val);
            ast_node_add_child(node, kv);
        } else {
            // Could be a plain value, optionally with :Float or :Decimal hint
            ASTNode *val = parse_expr(p);
            // type hint after value: value:Float
            if (check(p, TOK_COLON)) {
                advance_tok(p);
                Token *hint = advance_tok(p);
                ASTNode *annot = ast_node_new(NODE_TYPE_ANNOT, hint->line);
                annot->sval    = strdup(hint->value);
                ast_node_add_child(annot, val);
                ast_node_add_child(node, annot);
            } else {
                ast_node_add_child(node, val);
            }
        }

        if (!check(p, TOK_RBRACE)) {
            if (!match(p, TOK_COMMA)) skip_newlines(p);
        }
        skip_newlines(p);
    }
    expect(p, TOK_RBRACE, "array/dict literal end");
    (void)has_colon;
    return node;
}

static ASTNode *parse_primary(Parser *p) {
    Token *t = cur_tok(p);
    int line  = t->line;

    // Null
    if (t->type == TOK_NULL) {
        advance_tok(p);
        return ast_node_new(NODE_LITERAL_NULL, line);
    }
    // Booleans
    if (t->type == TOK_BOOL_TRUE) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_LITERAL_BOOL, line);
        n->ival = 1; return n;
    }
    if (t->type == TOK_BOOL_FALSE) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_LITERAL_BOOL, line);
        n->ival = 0; return n;
    }
    // Integer
    if (t->type == TOK_INTEGER) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_LITERAL_INT, line);
        n->ival = atoll(t->value);
        // Check for range: 1..5
        if (check(p, TOK_DOTDOT)) {
            advance_tok(p);
            ASTNode *end = parse_primary(p);
            ASTNode *range = ast_node_new(NODE_RANGE, line);
            ast_node_add_child(range, n);
            ast_node_add_child(range, end);
            return range;
        }
        return n;
    }
    // Float
    if (t->type == TOK_FLOAT) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_LITERAL_FLOAT, line);
        n->fval = atof(t->value);
        n->sval = strdup(t->value);
        return n;
    }
    // Decimal
    if (t->type == TOK_DECIMAL) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_LITERAL_DECIMAL, line);
        n->sval = strdup(t->value);
        return n;
    }
    // String
    if (t->type == TOK_STRING) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_LITERAL_STRING, line);
        n->sval = strdup(t->value);
        return n;
    }
    // Interpolated string
    if (t->type == TOK_ESTRING) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_LITERAL_ESTRING, line);
        n->sval = strdup(t->value);
        return n;
    }
    // Array / dict literal
    if (t->type == TOK_LBRACE) {
        return parse_array_or_dict(p);
    }
    // Grouped expression
    if (t->type == TOK_LPAREN) {
        advance_tok(p);
        ASTNode *inner = parse_expr(p);
        expect(p, TOK_RPAREN, "grouped expression");
        return inner;
    }
    // not
    if (t->type == TOK_NOT) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_UNOP, line);
        n->sval = strdup("not");
        ast_node_add_child(n, parse_primary(p));
        return n;
    }
    // Unary minus
    if (t->type == TOK_MINUS) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_UNOP, line);
        n->sval = strdup("-");
        ast_node_add_child(n, parse_primary(p));
        return n;
    }
    // Identifier (possibly a function call)
    if (t->type == TOK_IDENT      ||
        t->type == TOK_TYPE_STRING || t->type == TOK_TYPE_INTEGER ||
        t->type == TOK_TYPE_FLOAT  || t->type == TOK_TYPE_DECIMAL ||
        t->type == TOK_TYPE_BOOL   || t->type == TOK_TYPE_ANY     ||
        t->type == TOK_TYPE_ARRAY  || t->type == TOK_TYPE_DICT    ||
        t->type == TOK_TYPE_NULL   || t->type == TOK_TYPE_UNDEFINED ||
        t->type == TOK_SCOPE_GLOBAL || t->type == TOK_SCOPE_FUNCTION) {
        advance_tok(p);
        ASTNode *ident = ast_node_new(NODE_IDENT, line);
        ident->sval    = strdup(t->value);
        return ident;
    }
    // Inline function literal: func name(args) { body } used as an expression
    if (t->type == TOK_FUNC) {
        return parse_func_decl(p);  // reuse func decl parser; result is NODE_FUNC_DECL
    }
    // await
    if (t->type == TOK_AWAIT) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_AWAIT, line);
        ast_node_add_child(n, parse_expr(p));
        return n;
    }
    // spawn
    if (t->type == TOK_SPAWN) {
        advance_tok(p);
        ASTNode *n = ast_node_new(NODE_SPAWN, line);
        ast_node_add_child(n, parse_expr(p));
        return n;
    }

    fprintf(stderr,
        "\033[31m[NullScript Error] line %d: unexpected token '%s' (%s). "
        "i don't know what you were going for here.\033[0m\n",
        line, t->value, token_type_name(t->type));
    p->had_error = 1;
    advance_tok(p);
    return ast_node_new(NODE_LITERAL_NULL, line);
}

// ── Call / index / member postfix ──────────────────────────────────────────

static int is_type_tok(TokenType t) {
    return t == TOK_TYPE_STRING || t == TOK_TYPE_INTEGER ||
           t == TOK_TYPE_FLOAT  || t == TOK_TYPE_DECIMAL ||
           t == TOK_TYPE_BOOL   || t == TOK_TYPE_ANY     ||
           t == TOK_TYPE_ARRAY  || t == TOK_TYPE_DICT    ||
           t == TOK_TYPE_NULL   || t == TOK_TYPE_UNDEFINED;
}

static ASTNode *parse_call_args(Parser *p, ASTNode *callee) {
    int line = cur_tok(p)->line;
    expect(p, TOK_LPAREN, "call");
    ASTNode *call = ast_node_new(NODE_CALL, line);
    ast_node_add_child(call, callee);

    // Check for mixed positional/named: report error if positional comes after named
    int seen_named = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        // Named arg: ident=expr
        if (check(p, TOK_IDENT) && peek_tok(p)->type == TOK_EQ) {
            seen_named = 1;
            Token *name = advance_tok(p);
            advance_tok(p); // =
            ASTNode *val = parse_expr(p);
            ASTNode *na  = ast_node_new(NODE_NAMED_ARG, name->line);
            na->sval     = strdup(name->value);
            ast_node_add_child(na, val);
            ast_node_add_child(call, na);
        } else {
            if (seen_named) {
                fprintf(stderr,
                    "\033[31m[NullScript Error] line %d: positional argument after named argument. "
                    "you knew what you were doing and you did it anyway. "
                    "prepare to be berated.\033[0m\n", cur_tok(p)->line);
                p->had_error = 1;
            }
            ast_node_add_child(call, parse_expr(p));
        }
        if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA, "call args");
    }
    expect(p, TOK_RPAREN, "call end");
    return call;
}

static ASTNode *parse_postfix(Parser *p) {
    ASTNode *left = parse_primary(p);

    while (1) {
        // Function call: ident(...)
        if (check(p, TOK_LPAREN)) {
            left = parse_call_args(p, left);
        }
        // Index: arr{expr} — only valid after ident, call result, or another index
        // NOT after a bare integer/float/string literal (that would eat a repeat body)
        else if (check(p, TOK_LBRACE) &&
                 left->type != NODE_LITERAL_INT   &&
                 left->type != NODE_LITERAL_FLOAT  &&
                 left->type != NODE_LITERAL_DECIMAL &&
                 left->type != NODE_LITERAL_STRING  &&
                 left->type != NODE_LITERAL_BOOL    &&
                 left->type != NODE_LITERAL_NULL    &&
                 left->type != NODE_BINOP           &&
                 left->type != NODE_UNOP            &&
                 left->type != NODE_RANGE) {
            int line = cur_tok(p)->line;
            advance_tok(p);
            ASTNode *idx  = parse_expr(p);
            expect(p, TOK_RBRACE, "index");
            ASTNode *node = ast_node_new(NODE_INDEX, line);
            ast_node_add_child(node, left);
            ast_node_add_child(node, idx);
            left = node;
        }
        // Member access: obj.method or obj.forEach(...)
        else if (check(p, TOK_DOT)) {
            int line = cur_tok(p)->line;
            advance_tok(p);
            Token *mem = expect(p, TOK_IDENT, "member access");
            ASTNode *member = ast_node_new(NODE_MEMBER, line);
            member->sval    = strdup(mem->value);
            ast_node_add_child(member, left);
            left = member;
        }
        else break;
    }
    return left;
}

// ── Binary expressions (PEMDAS) ────────────────────────────────────────────

static int get_precedence(TokenType t) {
    switch (t) {
        case TOK_OR:       return 1;
        case TOK_AND:      return 2;
        case TOK_EQEQ: case TOK_NEQ:
        case TOK_LT:   case TOK_GT:
        case TOK_LTE:  case TOK_GTE:  return 3;
        case TOK_PLUS: case TOK_MINUS: return 4;
        case TOK_STAR: case TOK_SLASH:
        case TOK_PERCENT:              return 5;
        case TOK_STARSTAR:             return 6;
        default: return 0;
    }
}

static const char *op_str(TokenType t) {
    switch (t) {
        case TOK_PLUS:    return "+";   case TOK_MINUS:   return "-";
        case TOK_STAR:    return "*";   case TOK_SLASH:   return "/";
        case TOK_PERCENT: return "%";   case TOK_STARSTAR:return "**";
        case TOK_EQEQ:   return "==";  case TOK_NEQ:     return "!=";
        case TOK_LT:     return "<";   case TOK_GT:      return ">";
        case TOK_LTE:    return "<=";  case TOK_GTE:     return ">=";
        case TOK_AND:    return "and"; case TOK_OR:      return "or";
        default: return "?";
    }
}

static ASTNode *parse_binop(Parser *p, int min_prec) {
    ASTNode *left = parse_postfix(p);

    while (1) {
        int prec = get_precedence(cur_tok(p)->type);
        if (prec < min_prec) break;
        Token *op = advance_tok(p);
        ASTNode *right = parse_binop(p, prec + 1);
        ASTNode *node  = ast_node_new(NODE_BINOP, op->line);
        node->sval     = strdup(op_str(op->type));
        ast_node_add_child(node, left);
        ast_node_add_child(node, right);
        left = node;
    }
    return left;
}

static ASTNode *parse_expr(Parser *p) {
    return parse_binop(p, 1);
}

// ── Statements ─────────────────────────────────────────────────────────────

static ASTNode *parse_block(Parser *p) {
    int line = cur_tok(p)->line;
    expect(p, TOK_LBRACE, "block");
    skip_newlines(p);
    ASTNode *block = ast_node_new(NODE_BLOCK, line);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_RBRACE)) break;
        ASTNode *s = parse_stmt(p);
        if (s) ast_node_add_child(block, s);
        skip_newlines(p);
    }
    expect(p, TOK_RBRACE, "block end");
    return block;
}

// var/const declaration
// Syntax: var name=value [Type=X | TypeToken] [Scope=X | ScopeToken]
// OR positional: var name value TypeToken ScopeToken
static ASTNode *parse_var_decl(Parser *p) {
    Token *kw = advance_tok(p); // var or const
    int is_const = (kw->type == TOK_CONST);
    int line = kw->line;

    Token *name = expect(p, TOK_IDENT, "variable declaration");

    ASTNode *node     = ast_node_new(is_const ? NODE_CONST_DECL : NODE_VAR_DECL, line);
    node->sval        = strdup(name->value);
    node->is_const    = is_const;
    node->var_type    = strdup("Any");
    node->var_scope   = strdup("Function");

    // Optional =value
    if (check(p, TOK_EQ)) {
        advance_tok(p);
        ast_node_add_child(node, parse_expr(p));
    } else {
        ast_node_add_child(node, ast_node_new(NODE_LITERAL_NULL, line));
    }

    // Optional trailing annotations (any order): Type TypeToken, Scope ScopeToken,
    // Type=TypeName, Scope=ScopeName, or bare TypeToken, bare ScopeToken
    while (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF) && !check(p, TOK_RBRACE)) {
        Token *t = cur_tok(p);
        // Bare type token
        if (is_type_tok(t->type)) {
            free(node->var_type);
            node->var_type = strdup(t->value);
            advance_tok(p);
        }
        // Bare scope token
        else if (t->type == TOK_SCOPE_GLOBAL) {
            free(node->var_scope);
            node->var_scope = strdup("Global");
            advance_tok(p);
        }
        else if (t->type == TOK_SCOPE_FUNCTION) {
            free(node->var_scope);
            node->var_scope = strdup("Function");
            advance_tok(p);
        }
        // Type=XXX
        else if (t->type == TOK_IDENT && !strcmp(t->value, "Type")) {
            advance_tok(p);
            expect(p, TOK_EQ, "Type=");
            Token *tv = advance_tok(p);
            free(node->var_type);
            node->var_type = strdup(tv->value);
        }
        // Scope=XXX
        else if (t->type == TOK_IDENT && !strcmp(t->value, "Scope")) {
            advance_tok(p);
            expect(p, TOK_EQ, "Scope=");
            Token *sv = advance_tok(p);
            free(node->var_scope);
            node->var_scope = strdup(sv->value);
        }
        else break;
    }
    return node;
}

// func declaration
static ASTNode *parse_func_decl(Parser *p) {
    int is_async = check(p, TOK_ASYNC);
    if (is_async) advance_tok(p);
    expect(p, TOK_FUNC, "func declaration");
    Token *name = expect(p, TOK_IDENT, "func name");
    int line = name->line;

    ASTNode *node = ast_node_new(NODE_FUNC_DECL, line);
    node->sval    = strdup(name->value);
    node->ival    = is_async;

    expect(p, TOK_LPAREN, "func params");
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        Token *param = expect(p, TOK_IDENT, "param name");
        ASTNode *pn  = ast_node_new(NODE_IDENT, param->line);
        pn->sval     = strdup(param->value);
        ast_node_add_child(node, pn);
        if (!check(p, TOK_RPAREN)) expect(p, TOK_COMMA, "func params");
    }
    expect(p, TOK_RPAREN, "func params end");
    skip_newlines(p);
    ASTNode *body = parse_block(p);
    ast_node_add_child(node, body);
    return node;
}

// if / else if / else
static ASTNode *parse_if(Parser *p) {
    int line = cur_tok(p)->line;
    expect(p, TOK_IF, "if");
    expect(p, TOK_LPAREN, "if condition");
    ASTNode *cond = parse_expr(p);
    expect(p, TOK_RPAREN, "if condition");
    skip_newlines(p);
    ASTNode *body  = parse_block(p);

    ASTNode *node = ast_node_new(NODE_IF, line);
    ast_node_add_child(node, cond);
    ast_node_add_child(node, body);

    skip_newlines(p);
    while (check(p, TOK_ELSE)) {
        advance_tok(p);
        skip_newlines(p);
        if (check(p, TOK_IF)) {
            // else if — recurse
            ASTNode *elif = parse_if(p);
            ast_node_add_child(node, elif);
        } else {
            ASTNode *else_body = parse_block(p);
            ast_node_add_child(node, else_body);
        }
        skip_newlines(p);
    }
    return node;
}

// repeat i=5 { ... }
static ASTNode *parse_repeat(Parser *p) {
    int line = cur_tok(p)->line;
    expect(p, TOK_REPEAT, "repeat");

    ASTNode *node = ast_node_new(NODE_REPEAT, line);

    if (check(p, TOK_IDENT) && peek_tok(p)->type == TOK_EQ) {
        Token *var = advance_tok(p);
        node->sval = strdup(var->value);
        advance_tok(p); // =
    } else {
        node->sval = strdup("_i");
    }

    // Parse count: use parse_primary then allow calls/member but NOT { indexing.
    // { indexing would consume the loop body brace.
    ASTNode *count = parse_primary(p);
    while (check(p, TOK_LPAREN) || check(p, TOK_DOT)) {
        if (check(p, TOK_LPAREN)) {
            count = parse_call_args(p, count);
        } else {
            int ln = cur_tok(p)->line;
            advance_tok(p);
            Token *mem = expect(p, TOK_IDENT, "member in repeat count");
            ASTNode *member = ast_node_new(NODE_MEMBER, ln);
            member->sval    = strdup(mem->value);
            ast_node_add_child(member, count);
            count = member;
        }
    }
    // Binary operators (arithmetic, comparison) but stop before {
    while (get_precedence(cur_tok(p)->type) > 0) {
        Token *op    = advance_tok(p);
        ASTNode *rhs = parse_primary(p);
        while (check(p, TOK_LPAREN)) { rhs = parse_call_args(p, rhs); }
        ASTNode *binop = ast_node_new(NODE_BINOP, op->line);
        binop->sval    = strdup(op_str(op->type));
        ast_node_add_child(binop, count);
        ast_node_add_child(binop, rhs);
        count = binop;
    }
    ast_node_add_child(node, count);

    skip_newlines(p);
    ASTNode *body = parse_block(p);
    ast_node_add_child(node, body);
    return node;
}

// try / catch
static ASTNode *parse_try_catch(Parser *p) {
    int line = cur_tok(p)->line;
    expect(p, TOK_TRY, "try");
    skip_newlines(p);
    ASTNode *try_block = parse_block(p);
    skip_newlines(p);
    expect(p, TOK_CATCH, "catch");

    ASTNode *node = ast_node_new(NODE_TRY_CATCH, line);
    ast_node_add_child(node, try_block);

    // Optional catch(errVar)
    if (check(p, TOK_LPAREN)) {
        advance_tok(p);
        Token *errvar = expect(p, TOK_IDENT, "catch variable");
        node->sval    = strdup(errvar->value);
        expect(p, TOK_RPAREN, "catch variable end");
    } else {
        node->sval = strdup("err");
    }
    skip_newlines(p);
    ASTNode *catch_block = parse_block(p);
    ast_node_add_child(node, catch_block);
    return node;
}

// import
static ASTNode *parse_import(Parser *p) {
    int line = cur_tok(p)->line;
    expect(p, TOK_IMPORT, "import");
    ASTNode *node = ast_node_new(NODE_IMPORT, line);

    // gather everything until newline as the module path
    char buf[256] = "";
    while (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF)) {
        Token *t = advance_tok(p);
        if (strlen(buf) + strlen(t->value) + 2 < sizeof(buf)) {
            if (buf[0]) strcat(buf, "");
            strcat(buf, t->value);
        }
    }
    // Check for .exe — syntax error
    int len = strlen(buf);
    if (len >= 4 && !strcmp(buf + len - 4, ".exe")) {
        fprintf(stderr,
            "\033[31m[NullScript Error] line %d: importing a .exe? really? "
            "what were you thinking. syntax error.\033[0m\n", line);
        p->had_error = 1;
    }
    node->sval = strdup(buf);
    return node;
}

// alias
static ASTNode *parse_alias(Parser *p) {
    int line = cur_tok(p)->line;
    expect(p, TOK_ALIAS, "alias");
    ASTNode *node = ast_node_new(NODE_ALIAS, line);

    // alias <original> <new> — original and new can be keywords (e.g. alias func fn)
    Token *orig = advance_tok(p);  // accept any token as original
    Token *newn = advance_tok(p);  // accept any token as new name

    node->sval = strdup(orig->value);
    ASTNode *n = ast_node_new(NODE_IDENT, line);
    n->sval    = strdup(newn->value);
    ast_node_add_child(node, n);
    return node;
}

// Assignment or compound assignment: lval op= expr
static ASTNode *parse_assign_or_expr_stmt(Parser *p) {
    ASTNode *left = parse_postfix(p);

    static TokenType compound_ops[] = {
        TOK_EQ, TOK_PLUSEQ, TOK_MINUSEQ, TOK_STAREQ,
        TOK_SLASHEQ, TOK_PERCENTEQ, TOK_STARSTAREQ
    };
    static const char *compound_strs[] = {
        "=", "+=", "-=", "*=", "/=", "%=", "**="
    };
    int n_ops = 7;

    for (int i = 0; i < n_ops; i++) {
        if (check(p, compound_ops[i])) {
            int line = cur_tok(p)->line;
            advance_tok(p);
            ASTNode *right = parse_expr(p);
            ASTNode *assign = ast_node_new(NODE_ASSIGN, line);
            assign->sval    = strdup(compound_strs[i]);
            ast_node_add_child(assign, left);
            ast_node_add_child(assign, right);
            return assign;
        }
    }

    // Plain binary continuation (e.g. standalone expression like a function call)
    // Complete into a full binary expression
    int prec = get_precedence(cur_tok(p)->type);
    if (prec > 0) {
        Token *op = advance_tok(p);
        ASTNode *right = parse_binop(p, prec + 1);
        ASTNode *node  = ast_node_new(NODE_BINOP, op->line);
        node->sval     = strdup(op_str(op->type));
        ast_node_add_child(node, left);
        ast_node_add_child(node, right);
        return node;
    }

    return left; // standalone call or ident
}

static ASTNode *parse_stmt(Parser *p) {
    skip_newlines(p);
    Token *t = cur_tok(p);

    // Check if this ident is a known alias for a keyword
    if (t->type == TOK_IDENT) {
        for (int ai = 0; ai < alias_count; ai++) {
            if (!strcmp(aliases[ai].to, t->value)) {
                const char *orig = aliases[ai].from;
                if (!strcmp(orig, "func")) {
                    t->type = TOK_FUNC; break;
                } else if (!strcmp(orig, "var")) {
                    t->type = TOK_VAR; break;
                } else if (!strcmp(orig, "const")) {
                    t->type = TOK_CONST; break;
                }
            }
        }
    }

    switch (t->type) {
        case TOK_VAR:
        case TOK_CONST:     return parse_var_decl(p);
        case TOK_ASYNC:
        case TOK_FUNC:      return parse_func_decl(p);
        case TOK_IF:        return parse_if(p);
        case TOK_REPEAT:    return parse_repeat(p);
        case TOK_IMPORT:    return parse_import(p);
        case TOK_ALIAS:     return parse_alias(p);
        case TOK_TRY:       return parse_try_catch(p);
        case TOK_RETURN: {
            int line = t->line;
            advance_tok(p);
            ASTNode *node = ast_node_new(NODE_RETURN, line);
            if (!check(p, TOK_NEWLINE) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF))
                ast_node_add_child(node, parse_expr(p));
            return node;
        }
        case TOK_EXPORT: {
            int line = t->line;
            advance_tok(p);
            ASTNode *node = ast_node_new(NODE_EXPORT, line);
            ast_node_add_child(node, parse_expr(p));
            return node;
        }
        case TOK_NEWLINE:
            advance_tok(p);
            return NULL;
        case TOK_EOF:
            return NULL;
        default:
            return parse_assign_or_expr_stmt(p);
    }
}

// ── Entry point ────────────────────────────────────────────────────────────

// Pre-scan: collect all alias statements before full parsing so that
// keyword aliases (e.g. "alias func fn") are available during parse_stmt.
static void prescan_aliases(Token **tokens) {
    for (int i = 0; tokens[i] && tokens[i]->type != TOK_EOF; i++) {
        if (tokens[i]->type != TOK_ALIAS) continue;
        i++;
        // skip newlines
        while (tokens[i] && tokens[i]->type == TOK_NEWLINE) i++;
        if (!tokens[i] || tokens[i]->type == TOK_EOF) break;
        Token *orig = tokens[i++];
        while (tokens[i] && tokens[i]->type == TOK_NEWLINE) i++;
        if (!tokens[i] || tokens[i]->type == TOK_EOF) break;
        Token *newn = tokens[i]; // don't advance — the main loop will re-parse this
        if (alias_count < 64) {
            // Check not already aliased
            int found = 0;
            for (int j = 0; j < alias_count; j++) {
                if (!strcmp(aliases[j].to, newn->value)) { found = 1; break; }
            }
            if (!found) {
                aliases[alias_count].from = strdup(orig->value);
                aliases[alias_count].to   = strdup(newn->value);
                alias_count++;
            }
        }
    }
}

ASTNode *parse(Token **tokens) {
    // Pre-scan aliases so keyword aliases work during parsing
    prescan_aliases(tokens);

    Parser *p = parser_new(tokens);
    ASTNode *program = ast_node_new(NODE_PROGRAM, 1);

    skip_newlines(p);
    while (!check(p, TOK_EOF)) {
        ASTNode *s = parse_stmt(p);
        if (s) ast_node_add_child(program, s);
        skip_newlines(p);
    }

    if (p->had_error) {
        ast_node_free(program);
        parser_free(p);
        return NULL;
    }
    int had_err = p->had_error;
    parser_free(p);
    if (had_err) return NULL;
    return program;
}

Parser *parser_new(Token **tokens) {
    Parser *p   = malloc(sizeof(Parser));
    p->tokens   = tokens;
    p->pos      = 0;
    p->had_error = 0;
    return p;
}

void parser_free(Parser *p) { free(p); }

// Parse a single expression (for estring interpolation)
ASTNode *parse_single_expr(Token **tokens) {
    Parser *p = parser_new(tokens);
    skip_newlines(p);
    ASTNode *expr = parse_expr(p);
    parser_free(p);
    return expr;
}
