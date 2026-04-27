#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

ASTNode *ast_node_new(NodeType type, int line) {
    ASTNode *n    = calloc(1, sizeof(ASTNode));
    n->type       = type;
    n->line       = line;
    n->children   = NULL;
    n->num_children = 0;
    return n;
}

void ast_node_add_child(ASTNode *parent, ASTNode *child) {
    if (!parent || !child) return;
    parent->num_children++;
    parent->children = realloc(parent->children,
                               sizeof(ASTNode*) * parent->num_children);
    parent->children[parent->num_children - 1] = child;
}

void ast_node_free(ASTNode *node) {
    if (!node) return;
    for (int i = 0; i < node->num_children; i++)
        ast_node_free(node->children[i]);
    free(node->children);
    free(node->sval);
    free(node->var_type);
    free(node->var_scope);
    free(node);
}

static const char *node_type_name(NodeType t) {
    switch (t) {
        case NODE_PROGRAM:        return "PROGRAM";
        case NODE_VAR_DECL:       return "VAR_DECL";
        case NODE_CONST_DECL:     return "CONST_DECL";
        case NODE_FUNC_DECL:      return "FUNC_DECL";
        case NODE_CALL:           return "CALL";
        case NODE_ASSIGN:         return "ASSIGN";
        case NODE_BINOP:          return "BINOP";
        case NODE_UNOP:           return "UNOP";
        case NODE_IF:             return "IF";
        case NODE_REPEAT:         return "REPEAT";
        case NODE_RETURN:         return "RETURN";
        case NODE_EXPORT:         return "EXPORT";
        case NODE_IMPORT:         return "IMPORT";
        case NODE_ALIAS:          return "ALIAS";
        case NODE_BLOCK:          return "BLOCK";
        case NODE_IDENT:          return "IDENT";
        case NODE_LITERAL_INT:    return "INT";
        case NODE_LITERAL_FLOAT:  return "FLOAT";
        case NODE_LITERAL_DECIMAL:return "DECIMAL";
        case NODE_LITERAL_STRING: return "STRING";
        case NODE_LITERAL_ESTRING:return "ESTRING";
        case NODE_LITERAL_BOOL:   return "BOOL";
        case NODE_LITERAL_NULL:   return "NULL";
        case NODE_ARRAY:          return "ARRAY";
        case NODE_DICT:           return "DICT";
        case NODE_INDEX:          return "INDEX";
        case NODE_RANGE:          return "RANGE";
        case NODE_MEMBER:         return "MEMBER";
        case NODE_NAMED_ARG:      return "NAMED_ARG";
        case NODE_TRY_CATCH:      return "TRY_CATCH";
        case NODE_ASYNC_FUNC:     return "ASYNC_FUNC";
        case NODE_AWAIT:          return "AWAIT";
        case NODE_SPAWN:          return "SPAWN";
        case NODE_FOREACH:        return "FOREACH";
        case NODE_TYPE_ANNOT:     return "TYPE_ANNOT";
        default:                  return "?";
    }
}

void ast_print(ASTNode *node, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; i++) printf("  ");
    printf("%s", node_type_name(node->type));
    if (node->sval)      printf(" sval=%s", node->sval);
    if (node->var_type)  printf(" type=%s", node->var_type);
    if (node->var_scope) printf(" scope=%s", node->var_scope);
    printf("\n");
    for (int i = 0; i < node->num_children; i++)
        ast_print(node->children[i], indent + 1);
}
