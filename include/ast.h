#ifndef AST_H
#define AST_H

#include "token.h"

typedef enum {
    NODE_PROGRAM,
    NODE_VAR_DECL,
    NODE_CONST_DECL,
    NODE_FUNC_DECL,
    NODE_CALL,
    NODE_ASSIGN,
    NODE_BINOP,
    NODE_UNOP,
    NODE_IF,
    NODE_REPEAT,
    NODE_RETURN,
    NODE_EXPORT,
    NODE_IMPORT,
    NODE_ALIAS,
    NODE_BLOCK,
    NODE_IDENT,
    NODE_LITERAL_INT,
    NODE_LITERAL_FLOAT,
    NODE_LITERAL_DECIMAL,
    NODE_LITERAL_STRING,
    NODE_LITERAL_ESTRING,  // interpolated
    NODE_LITERAL_BOOL,
    NODE_LITERAL_NULL,
    NODE_ARRAY,
    NODE_DICT,
    NODE_INDEX,       // arr{expr} / dict{"key"}
    NODE_RANGE,       // 1..5
    NODE_MEMBER,      // obj.method
    NODE_NAMED_ARG,   // key=value in call
    NODE_TRY_CATCH,
    NODE_ASYNC_FUNC,
    NODE_AWAIT,
    NODE_SPAWN,
    NODE_FOREACH,     // .forEach()
    NODE_TYPE_ANNOT,  // Float/Decimal hint on literal
} NodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    NodeType   type;
    int        line;

    // Universal value slot (literal text, identifier name, op character, etc.)
    char      *sval;
    long long  ival;
    double     fval;

    // Children (used differently per node type — see parser.c comments)
    ASTNode  **children;
    int        num_children;

    // Optional metadata
    char      *var_type;   // type annotation string
    char      *var_scope;  // "Global" / "Function"
    int        is_const;
};

ASTNode *ast_node_new(NodeType type, int line);
void     ast_node_add_child(ASTNode *parent, ASTNode *child);
void     ast_node_free(ASTNode *node);
void     ast_print(ASTNode *node, int indent); // debug

#endif
