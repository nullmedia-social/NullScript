#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
    // Literals
    TOK_STRING,
    TOK_INTEGER,
    TOK_FLOAT,
    TOK_DECIMAL,
    TOK_BOOL_TRUE,
    TOK_BOOL_FALSE,
    TOK_NULL,

    // Identifiers & Keywords
    TOK_IDENT,
    TOK_VAR,
    TOK_CONST,
    TOK_FUNC,
    TOK_IF,
    TOK_ELSE,
    TOK_REPEAT,
    TOK_IMPORT,
    TOK_ALIAS,
    TOK_RETURN,
    TOK_EXPORT,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_ASYNC,
    TOK_AWAIT,
    TOK_SPAWN,
    TOK_TRY,
    TOK_CATCH,

    // Types (used as annotations)
    TOK_TYPE_STRING,
    TOK_TYPE_INTEGER,
    TOK_TYPE_FLOAT,
    TOK_TYPE_DECIMAL,
    TOK_TYPE_BOOL,
    TOK_TYPE_ANY,
    TOK_TYPE_ARRAY,
    TOK_TYPE_DICT,
    TOK_TYPE_NULL,
    TOK_TYPE_UNDEFINED,

    // Scope keywords
    TOK_SCOPE_GLOBAL,
    TOK_SCOPE_FUNCTION,

    // Operators
    TOK_PLUS,       // +
    TOK_MINUS,      // -
    TOK_STAR,       // *
    TOK_SLASH,      // /
    TOK_PERCENT,    // %
    TOK_STARSTAR,   // **
    TOK_DOTDOT,     // ..

    TOK_EQ,         // =
    TOK_PLUSEQ,     // +=
    TOK_MINUSEQ,    // -=
    TOK_STAREQ,     // *=
    TOK_SLASHEQ,    // /=
    TOK_PERCENTEQ,  // %=
    TOK_STARSTAREQ, // **=

    TOK_EQEQ,       // ==
    TOK_NEQ,        // !=
    TOK_LT,         // <
    TOK_GT,         // >
    TOK_LTE,        // <=
    TOK_GTE,        // >=

    // Delimiters
    TOK_LPAREN,     // (
    TOK_RPAREN,     // )
    TOK_LBRACE,     // {
    TOK_RBRACE,     // }
    TOK_LBRACKET,   // [  (reserved, not used yet)
    TOK_RBRACKET,   // ]
    TOK_COMMA,      // ,
    TOK_COLON,      // :
    TOK_DOT,        // .
    TOK_HASH,       // # (cli comment, not used in language)

    // String prefix
    TOK_ESTRING,    // e"..." interpolated string

    // Special
    TOK_NEWLINE,
    TOK_EOF,
    TOK_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char     *value;   // heap-allocated string copy of the raw token text
    int       line;
    int       col;
} Token;

const char *token_type_name(TokenType t);
void        token_free(Token *tok);

#endif
