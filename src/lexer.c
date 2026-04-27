#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

// ── Judgement messages (compiler personality) ──────────────────────────────
static const char *TILDE_JUDGE[] = {
    "oh, another tilde? very original. absolutely necessary. 10/10.",
    "wow, you put TWO tildes in a comment. groundbreaking. truly.",
    "your tilde game is unmatched. and not in a good way.",
    "the tilde key must be very lonely given how often you visit it.",
    "okay at this point i'm convinced you LIVE on the tilde key.",
};
static const int TILDE_JUDGE_COUNT = 5;

static void judge_tilde(Lexer *l) {
    int idx = l->tilde_count % TILDE_JUDGE_COUNT;
    fprintf(stderr, "\033[35m[NullScript judges you] line %d: %s\033[0m\n",
            l->line, TILDE_JUDGE[idx]);
    l->tilde_count++;
}

// ── Helpers ────────────────────────────────────────────────────────────────
static char cur(Lexer *l)  { return l->src[l->pos]; }
static char peek(Lexer *l) { return l->src[l->pos + 1]; }

static char advance(Lexer *l) {
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else           { l->col++; }
    return c;
}

static Token *make_tok(TokenType t, const char *val, int line, int col) {
    Token *tok = malloc(sizeof(Token));
    tok->type  = t;
    tok->value = strdup(val);
    tok->line  = line;
    tok->col   = col;
    return tok;
}

// ── Keyword lookup ─────────────────────────────────────────────────────────
static TokenType keyword_type(const char *s) {
    if (!strcmp(s,"var"))       return TOK_VAR;
    if (!strcmp(s,"const"))     return TOK_CONST;
    if (!strcmp(s,"func"))      return TOK_FUNC;
    if (!strcmp(s,"if"))        return TOK_IF;
    if (!strcmp(s,"else"))      return TOK_ELSE;
    if (!strcmp(s,"repeat"))    return TOK_REPEAT;
    if (!strcmp(s,"import"))    return TOK_IMPORT;
    if (!strcmp(s,"alias"))     return TOK_ALIAS;
    if (!strcmp(s,"return"))    return TOK_RETURN;
    if (!strcmp(s,"export"))    return TOK_EXPORT;
    if (!strcmp(s,"and"))       return TOK_AND;
    if (!strcmp(s,"or"))        return TOK_OR;
    if (!strcmp(s,"not"))       return TOK_NOT;
    if (!strcmp(s,"async"))     return TOK_ASYNC;
    if (!strcmp(s,"await"))     return TOK_AWAIT;
    if (!strcmp(s,"spawn"))     return TOK_SPAWN;
    if (!strcmp(s,"try"))       return TOK_TRY;
    if (!strcmp(s,"catch"))     return TOK_CATCH;
    if (!strcmp(s,"True"))      return TOK_BOOL_TRUE;
    if (!strcmp(s,"False"))     return TOK_BOOL_FALSE;
    if (!strcmp(s,"Null"))      return TOK_NULL;
    // Type annotations
    if (!strcmp(s,"String"))    return TOK_TYPE_STRING;
    if (!strcmp(s,"Integer"))   return TOK_TYPE_INTEGER;
    if (!strcmp(s,"Float"))     return TOK_TYPE_FLOAT;
    if (!strcmp(s,"Decimal"))   return TOK_TYPE_DECIMAL;
    if (!strcmp(s,"Bool"))      return TOK_TYPE_BOOL;
    if (!strcmp(s,"Any"))       return TOK_TYPE_ANY;
    if (!strcmp(s,"Array"))     return TOK_TYPE_ARRAY;
    if (!strcmp(s,"Dict"))      return TOK_TYPE_DICT;
    if (!strcmp(s,"Undefined")) return TOK_TYPE_UNDEFINED;
    // Scope annotations
    if (!strcmp(s,"Global"))    return TOK_SCOPE_GLOBAL;
    if (!strcmp(s,"Function"))  return TOK_SCOPE_FUNCTION;
    // Type= shorthand handled in parser
    return TOK_IDENT;
}

// ── Single-token scan ──────────────────────────────────────────────────────
Token *lexer_next(Lexer *l) {
    // Skip spaces/tabs (not newlines — newlines are significant as statement ends)
    while (cur(l) == ' ' || cur(l) == '\t' || cur(l) == '\r')
        advance(l);

    int line = l->line, col = l->col;
    char c = cur(l);

    // EOF
    if (c == '\0') return make_tok(TOK_EOF, "", line, col);

    // Newline
    if (c == '\n') { advance(l); return make_tok(TOK_NEWLINE, "\\n", line, col); }

    // ── Comments ──────────────────────────────────────────────────────────
    if (c == '~') {
        // Multiline ~~
        if (peek(l) == '~') {
            advance(l); advance(l); // consume ~~
            // Check for nested tilde pairs — that's the one that actually breaks things
            while (l->src[l->pos] != '\0') {
                if (l->src[l->pos] == '~' && l->src[l->pos+1] == '~') {
                    advance(l); advance(l);
                    break;
                }
                if (l->src[l->pos] == '~') {
                    // nested tilde inside multiline: error
                    fprintf(stderr,
                        "\033[31m[NullScript Error] line %d: tilde inside multiline comment. "
                        "i told you not to do that. everything is broken now. congratulations.\033[0m\n",
                        l->line);
                    return make_tok(TOK_ERROR, "nested tilde in multiline comment", line, col);
                }
                advance(l);
            }
            return lexer_next(l); // recurse to get real next token
        }
        // Single-line ~
        advance(l); // consume the ~
        // Extra tildes get judged
        while (cur(l) == '~') {
            judge_tilde(l);
            advance(l);
        }
        while (cur(l) != '\n' && cur(l) != '\0') advance(l);
        return lexer_next(l);
    }

    // ── Strings ───────────────────────────────────────────────────────────
    // Interpolated: e"..."
    if (c == 'e' && peek(l) == '"') {
        advance(l); advance(l); // consume e"
        char buf[4096]; int bi = 0;
        int brace_depth = 0;  // track ${...} depth so inner " don't close the string
        while (cur(l) != '\0') {
            char ch = cur(l);
            // Track ${ ... } nesting
            if (ch == '$' && peek(l) == '{') {
                buf[bi++] = advance(l); // $
                buf[bi++] = advance(l); // {
                brace_depth++;
                continue;
            }
            if (brace_depth > 0) {
                if (ch == '{') brace_depth++;
                else if (ch == '}') { brace_depth--; }
                buf[bi++] = advance(l);
                continue;
            }
            // At depth 0, a bare " closes the string
            if (ch == '"') break;
            if (ch == '\\') { advance(l); buf[bi++] = advance(l); }
            else            { buf[bi++] = advance(l); }
        }
        if (cur(l) == '"') advance(l);
        buf[bi] = '\0';
        return make_tok(TOK_ESTRING, buf, line, col);
    }

    // Regular string "..."
    if (c == '"') {
        advance(l);
        char buf[4096]; int bi = 0;
        while (cur(l) != '"' && cur(l) != '\0') {
            if (cur(l) == '\\') { advance(l); 
                char esc = advance(l);
                switch (esc) {
                    case 'n':  buf[bi++] = '\n'; break;
                    case 't':  buf[bi++] = '\t'; break;
                    case '\\': buf[bi++] = '\\'; break;
                    case '"':  buf[bi++] = '"';  break;
                    default:   buf[bi++] = esc;  break;
                }
            } else { buf[bi++] = advance(l); }
        }
        if (cur(l) == '"') advance(l);
        buf[bi] = '\0';
        return make_tok(TOK_STRING, buf, line, col);
    }

    // ── Numbers ───────────────────────────────────────────────────────────
    if (isdigit(c) || (c == '-' && isdigit(peek(l)))) {
        char buf[64]; int bi = 0;
        if (c == '-') buf[bi++] = advance(l);
        while (isdigit(cur(l))) buf[bi++] = advance(l);

        // range check: 1..5
        if (cur(l) == '.' && peek(l) == '.') {
            buf[bi] = '\0';
            return make_tok(TOK_INTEGER, buf, line, col); // let parser handle ..
        }

        int is_frac = 0;
        char type_hint[16] = "";
        if (cur(l) == '.') {
            is_frac = 1;
            buf[bi++] = advance(l);
            while (isdigit(cur(l))) buf[bi++] = advance(l);
        }
        buf[bi] = '\0';

        // Check for :Float or :Decimal hint after a fractional number
        if (is_frac && cur(l) == ':') {
            advance(l); // consume :
            int ti = 0;
            while (isalpha(cur(l))) type_hint[ti++] = advance(l);
            type_hint[ti] = '\0';
        }

        if (!is_frac) return make_tok(TOK_INTEGER, buf, line, col);
        if (!strcmp(type_hint,"Decimal")) return make_tok(TOK_DECIMAL, buf, line, col);
        return make_tok(TOK_FLOAT, buf, line, col);
    }

    // ── Identifiers / keywords ────────────────────────────────────────────
    if (isalpha(c) || c == '_') {
        char buf[256]; int bi = 0;
        while (isalnum(cur(l)) || cur(l) == '_') buf[bi++] = advance(l);
        buf[bi] = '\0';
        // Handle Type=XXX and Scope=XXX (parser also handles these but lex them as IDENT=value)
        TokenType kw = keyword_type(buf);
        return make_tok(kw, buf, line, col);
    }

    // ── Two-char operators ────────────────────────────────────────────────
    advance(l);
    char n = cur(l);

#define TWO(a,b,t) if (c==a && n==b) { advance(l); return make_tok(t,"",line,col); }
    TWO('*','*', TOK_STARSTAR)
    TWO('.','.',  TOK_DOTDOT)
    TWO('+','=',  TOK_PLUSEQ)
    TWO('-','=',  TOK_MINUSEQ)
    TWO('*','=',  TOK_STAREQ)
    TWO('/','=',  TOK_SLASHEQ)
    TWO('%','=',  TOK_PERCENTEQ)
    TWO('=','=',  TOK_EQEQ)
    TWO('!','=',  TOK_NEQ)
    TWO('<','=',  TOK_LTE)
    TWO('>','=',  TOK_GTE)
#undef TWO
    // **= (three char)
    if (c=='*' && n=='*' && peek(l)=='=') {
        advance(l); advance(l);
        return make_tok(TOK_STARSTAREQ, "", line, col);
    }

    // ── Single-char operators ─────────────────────────────────────────────
    switch (c) {
        case '+': return make_tok(TOK_PLUS,     "+", line, col);
        case '-': return make_tok(TOK_MINUS,    "-", line, col);
        case '*': return make_tok(TOK_STAR,     "*", line, col);
        case '/': return make_tok(TOK_SLASH,    "/", line, col);
        case '%': return make_tok(TOK_PERCENT,  "%", line, col);
        case '=': return make_tok(TOK_EQ,       "=", line, col);
        case '<': return make_tok(TOK_LT,       "<", line, col);
        case '>': return make_tok(TOK_GT,       ">", line, col);
        case '(': return make_tok(TOK_LPAREN,   "(", line, col);
        case ')': return make_tok(TOK_RPAREN,   ")", line, col);
        case '{': return make_tok(TOK_LBRACE,   "{", line, col);
        case '}': return make_tok(TOK_RBRACE,   "}", line, col);
        case '[': return make_tok(TOK_LBRACKET, "[", line, col);
        case ']': return make_tok(TOK_RBRACKET, "]", line, col);
        case ',': return make_tok(TOK_COMMA,    ",", line, col);
        case ':': return make_tok(TOK_COLON,    ":", line, col);
        case '.': return make_tok(TOK_DOT,      ".", line, col);
        case '#': return make_tok(TOK_HASH,     "#", line, col);
        default: {
            // Skip non-ASCII bytes silently (UTF-8 sequences, BOM, etc.)
            if ((unsigned char)c > 127) return lexer_next(l);
            char err[32]; snprintf(err, 32, "unexpected '%c'", c);
            fprintf(stderr, "\033[31m[NullScript Error] line %d: %s\033[0m\n", line, err);
            return make_tok(TOK_ERROR, err, line, col);
        }
    }
}

Lexer *lexer_new(const char *src) {
    Lexer *l = malloc(sizeof(Lexer));
    l->src   = src;
    l->pos   = 0;
    l->line  = 1;
    l->col   = 1;
    l->tilde_count = 0;
    return l;
}

void lexer_free(Lexer *l) { free(l); }

Token **lexer_tokenise(const char *src) {
    Lexer *l = lexer_new(src);
    int cap = 256, len = 0;
    Token **tokens = malloc(sizeof(Token*) * cap);

    while (1) {
        Token *t = lexer_next(l);
        if (len + 1 >= cap) { cap *= 2; tokens = realloc(tokens, sizeof(Token*) * cap); }
        tokens[len++] = t;
        if (t->type == TOK_EOF || t->type == TOK_ERROR) break;
    }
    tokens[len] = NULL;
    lexer_free(l);
    return tokens;
}

void tokens_free(Token **tokens) {
    if (!tokens) return;
    for (int i = 0; tokens[i]; i++) token_free(tokens[i]);
    free(tokens);
}
