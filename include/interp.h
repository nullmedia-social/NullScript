#ifndef INTERP_H
#define INTERP_H

#include "ast.h"

// ─── Runtime value types ────────────────────────────────────────────────────
typedef enum {
    VAL_NULL,
    VAL_UNDEFINED,
    VAL_INT,
    VAL_FLOAT,
    VAL_DECIMAL,    // stored as string to avoid FP error
    VAL_STRING,
    VAL_BOOL,
    VAL_ARRAY,
    VAL_DICT,
    VAL_FUNCTION,
    VAL_BUILTIN,
    VAL_RETURN,     // sentinel for return propagation
} ValType;

typedef struct Value Value;
typedef struct Env   Env;

typedef Value *(*BuiltinFn)(Value **args, int argc, Env *env);

struct Value {
    ValType type;
    int     ref_count;

    union {
        long long   ival;
        double      fval;
        char       *sval;       // string, decimal (as text), error msg
        int         bval;       // bool
        struct {
            Value **items;
            int     len;
            int     cap;
        } array;
        struct {
            char  **keys;
            Value **vals;
            int     len;
            int     cap;
        } dict;
        struct {
            char    **params;   // param names
            int       n_params;
            ASTNode  *body;     // NODE_BLOCK
            Env      *closure;
            int       is_async;
        } func;
        struct {
            BuiltinFn fn;
            char      *name;
        } builtin;
    };
};

// ─── Environment (scope) ─────────────────────────────────────────────────────
typedef struct EnvEntry {
    char   *name;
    Value  *value;
    int     is_const;
} EnvEntry;

struct Env {
    EnvEntry *entries;
    int       len;
    int       cap;
    Env      *parent;
    int       ref_count;  // closures retain their env
};

Env   *env_new(Env *parent);
void   env_retain(Env *env);
void   env_release(Env *env);   // replaces env_free for closure-aware freeing
void   env_free(Env *env);      // kept for non-closure use
Value *env_get(Env *env, const char *name);
int    env_set(Env *env, const char *name, Value *val, int is_const, int global);
int    env_update(Env *env, const char *name, Value *val); // walks up chain

// ─── Value helpers ───────────────────────────────────────────────────────────
Value *val_null(void);
Value *val_undefined(void);
Value *val_int(long long i);
Value *val_float(double f);
Value *val_decimal(const char *s);
Value *val_string(const char *s);
Value *val_bool(int b);
Value *val_array(void);
Value *val_dict(void);
Value *val_builtin(const char *name, BuiltinFn fn);

void   val_retain(Value *v);
void   val_release(Value *v);
Value *val_copy(Value *v);

const char *val_type_name(Value *v);
char       *val_to_string(Value *v);
int         val_is_truthy(Value *v);
int         val_equals(Value *a, Value *b);

void val_array_push(Value *arr, Value *item);
Value *val_array_get(Value *arr, long long idx); // 0-based internally
void val_dict_set(Value *dict, const char *key, Value *val);
Value *val_dict_get(Value *dict, const char *key);

// ─── Alias table (defined in interp.c) ───────────────────────────────────────
typedef struct { char *from; char *to; } Alias;
extern Alias aliases[64];
extern int   alias_count;

// ─── Interpreter entry ───────────────────────────────────────────────────────
typedef struct {
    Env   *globals;
    int    had_error;
    char   error_msg[512];
    int    judged; // how many times the compiler judged the user
} Interpreter;

Interpreter *interp_new(void);
void         interp_free(Interpreter *it);
Value       *interp_run(Interpreter *it, ASTNode *program);

#endif
