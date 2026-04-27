#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "interp.h"

// ── Value constructors ─────────────────────────────────────────────────────

static Value *val_new(ValType t) {
    Value *v = calloc(1, sizeof(Value));
    v->type  = t;
    v->ref_count = 1;
    return v;
}

Value *val_null(void)       { return val_new(VAL_NULL); }
Value *val_undefined(void)  { return val_new(VAL_UNDEFINED); }

Value *val_int(long long i) {
    Value *v = val_new(VAL_INT);
    v->ival  = i;
    return v;
}
Value *val_float(double f) {
    Value *v = val_new(VAL_FLOAT);
    v->fval  = f;
    return v;
}
Value *val_decimal(const char *s) {
    Value *v = val_new(VAL_DECIMAL);
    v->sval  = strdup(s);
    return v;
}
Value *val_string(const char *s) {
    Value *v = val_new(VAL_STRING);
    v->sval  = strdup(s ? s : "");
    return v;
}
Value *val_bool(int b) {
    Value *v = val_new(VAL_BOOL);
    v->bval  = b ? 1 : 0;
    return v;
}
Value *val_array(void) { return val_new(VAL_ARRAY); }
Value *val_dict(void)  { return val_new(VAL_DICT); }

Value *val_builtin(const char *name, BuiltinFn fn) {
    Value *v        = val_new(VAL_BUILTIN);
    v->builtin.fn   = fn;
    v->builtin.name = strdup(name);
    return v;
}

// ── Ref counting ───────────────────────────────────────────────────────────

void val_retain(Value *v) { if (v) v->ref_count++; }

void val_release(Value *v) {
    if (!v) return;
    if (--v->ref_count > 0) return;

    switch (v->type) {
        case VAL_STRING:
        case VAL_DECIMAL:
            free(v->sval); break;
        case VAL_ARRAY:
            for (int i = 0; i < v->array.len; i++) val_release(v->array.items[i]);
            free(v->array.items); break;
        case VAL_DICT:
            for (int i = 0; i < v->dict.len; i++) {
                free(v->dict.keys[i]);
                val_release(v->dict.vals[i]);
            }
            free(v->dict.keys);
            free(v->dict.vals); break;
        case VAL_FUNCTION:
            for (int i = 0; i < v->func.n_params; i++) free(v->func.params[i]);
            free(v->func.params);
            if (v->func.closure) env_release(v->func.closure);
            break;
        case VAL_BUILTIN:
            free(v->builtin.name); break;
        default: break;
    }
    free(v);
}

Value *val_copy(Value *v) {
    if (!v) return val_null();
    val_retain(v);
    return v;
}

// ── Type names ────────────────────────────────────────────────────────────

const char *val_type_name(Value *v) {
    if (!v) return "Null";
    switch (v->type) {
        case VAL_NULL:      return "Null";
        case VAL_UNDEFINED: return "Undefined";
        case VAL_INT:       return "Integer";
        case VAL_FLOAT:     return "Float";
        case VAL_DECIMAL:   return "Decimal";
        case VAL_STRING:    return "String";
        case VAL_BOOL:      return "Bool";
        case VAL_ARRAY:     return "Array";
        case VAL_DICT:      return "Dict";
        case VAL_FUNCTION:  return "Function";
        case VAL_BUILTIN:   return "Function";
        case VAL_RETURN:    return "Return";
        default: return "Unknown";
    }
}

// ── To string ────────────────────────────────────────────────────────────

char *val_to_string(Value *v) {
    if (!v) return strdup("Null");
    char buf[256];
    switch (v->type) {
        case VAL_NULL:      return strdup("Null");
        case VAL_UNDEFINED: return strdup("Undefined");
        case VAL_INT:       snprintf(buf, sizeof(buf), "%lld", v->ival); return strdup(buf);
        case VAL_FLOAT:
            // Trim trailing zeros
            snprintf(buf, sizeof(buf), "%g", v->fval);
            return strdup(buf);
        case VAL_DECIMAL:   return strdup(v->sval);
        case VAL_STRING:    return strdup(v->sval);
        case VAL_BOOL:      return strdup(v->bval ? "True" : "False");
        case VAL_ARRAY: {
            // Build a {a, b, c} representation
            char *out = malloc(4096); out[0] = '{'; out[1] = '\0';
            for (int i = 0; i < v->array.len; i++) {
                char *s = val_to_string(v->array.items[i]);
                // Quote strings
                if (v->array.items[i] && v->array.items[i]->type == VAL_STRING) {
                    strcat(out, "\""); strcat(out, s); strcat(out, "\"");
                } else {
                    strcat(out, s);
                }
                free(s);
                if (i < v->array.len - 1) strcat(out, ", ");
            }
            strcat(out, "}");
            return out;
        }
        case VAL_DICT: {
            char *out = malloc(4096); out[0] = '{'; out[1] = '\0';
            for (int i = 0; i < v->dict.len; i++) {
                strcat(out, v->dict.keys[i]);
                strcat(out, ": ");
                char *s = val_to_string(v->dict.vals[i]);
                if (v->dict.vals[i] && v->dict.vals[i]->type == VAL_STRING) {
                    strcat(out, "\""); strcat(out, s); strcat(out, "\"");
                } else {
                    strcat(out, s);
                }
                free(s);
                if (i < v->dict.len - 1) strcat(out, ", ");
            }
            strcat(out, "}");
            return out;
        }
        case VAL_FUNCTION:
        case VAL_BUILTIN:
            return strdup("[Function]");
        default:
            return strdup("?");
    }
}

int val_is_truthy(Value *v) {
    if (!v) return 0;
    switch (v->type) {
        case VAL_NULL:      return 0;
        case VAL_UNDEFINED: return 0;
        case VAL_BOOL:      return v->bval;
        case VAL_INT:       return v->ival != 0;
        case VAL_FLOAT:     return v->fval != 0.0;
        case VAL_STRING:    return v->sval && v->sval[0] != '\0';
        case VAL_ARRAY:     return v->array.len > 0;
        case VAL_DICT:      return v->dict.len > 0;
        default: return 1;
    }
}

int val_equals(Value *a, Value *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->type == VAL_NULL && b->type == VAL_NULL) return 1;
    if (a->type == VAL_BOOL && b->type == VAL_BOOL) return a->bval == b->bval;
    if (a->type == VAL_INT  && b->type == VAL_INT)  return a->ival == b->ival;
    if (a->type == VAL_FLOAT && b->type == VAL_FLOAT) return a->fval == b->fval;
    if (a->type == VAL_INT && b->type == VAL_FLOAT) return (double)a->ival == b->fval;
    if (a->type == VAL_FLOAT && b->type == VAL_INT) return a->fval == (double)b->ival;
    if (a->type == VAL_STRING && b->type == VAL_STRING) return !strcmp(a->sval, b->sval);
    return 0;
}

// ── Array helpers ─────────────────────────────────────────────────────────

void val_array_push(Value *arr, Value *item) {
    if (arr->array.len >= arr->array.cap) {
        arr->array.cap = arr->array.cap ? arr->array.cap * 2 : 8;
        arr->array.items = realloc(arr->array.items, sizeof(Value*) * arr->array.cap);
    }
    val_retain(item);
    arr->array.items[arr->array.len++] = item;
}

Value *val_array_get(Value *arr, long long idx) {
    // 1-based indexing
    long long i = idx - 1;
    if (i < 0 || i >= arr->array.len) return val_null();
    return arr->array.items[i];
}

// ── Dict helpers ──────────────────────────────────────────────────────────

void val_dict_set(Value *dict, const char *key, Value *val) {
    for (int i = 0; i < dict->dict.len; i++) {
        if (!strcmp(dict->dict.keys[i], key)) {
            val_release(dict->dict.vals[i]);
            val_retain(val);
            dict->dict.vals[i] = val;
            return;
        }
    }
    if (dict->dict.len >= dict->dict.cap) {
        dict->dict.cap = dict->dict.cap ? dict->dict.cap * 2 : 8;
        dict->dict.keys = realloc(dict->dict.keys, sizeof(char*)  * dict->dict.cap);
        dict->dict.vals = realloc(dict->dict.vals, sizeof(Value*) * dict->dict.cap);
    }
    dict->dict.keys[dict->dict.len] = strdup(key);
    val_retain(val);
    dict->dict.vals[dict->dict.len] = val;
    dict->dict.len++;
}

Value *val_dict_get(Value *dict, const char *key) {
    for (int i = 0; i < dict->dict.len; i++) {
        if (!strcmp(dict->dict.keys[i], key)) return dict->dict.vals[i];
    }
    return val_null();
}

// ── Environment ───────────────────────────────────────────────────────────

Env *env_new(Env *parent) {
    Env *e    = calloc(1, sizeof(Env));
    e->parent = parent;
    e->ref_count = 1;
    if (parent) parent->ref_count++;  // retain parent so it outlives children
    return e;
}

void env_retain(Env *env) {
    if (env) env->ref_count++;
}

void env_release(Env *env) {
    if (!env) return;
    if (--env->ref_count > 0) return;
    // Release parent ref
    Env *parent = env->parent;
    for (int i = 0; i < env->len; i++) {
        free(env->entries[i].name);
        val_release(env->entries[i].value);
    }
    free(env->entries);
    free(env);
    // Propagate release up
    if (parent) env_release(parent);
}

void env_free(Env *env) {
    env_release(env);
}

Value *env_get(Env *env, const char *name) {
    for (Env *e = env; e; e = e->parent) {
        for (int i = 0; i < e->len; i++) {
            if (!strcmp(e->entries[i].name, name))
                return e->entries[i].value;
        }
    }
    return NULL;
}

int env_set(Env *env, const char *name, Value *val, int is_const, int global) {
    Env *target = global ? env : env;
    // If global, walk to the top
    if (global) {
        while (target->parent) target = target->parent;
    }
    // Check if already defined in this scope
    for (int i = 0; i < target->len; i++) {
        if (!strcmp(target->entries[i].name, name)) {
            if (target->entries[i].is_const) {
                fprintf(stderr,
                    "\033[31m[NullScript Error]: cannot reassign const '%s'. "
                    "that's the whole point of const.\033[0m\n", name);
                return 0;
            }
            val_release(target->entries[i].value);
            val_retain(val);
            target->entries[i].value = val;
            return 1;
        }
    }
    // New entry
    if (target->len >= target->cap) {
        target->cap = target->cap ? target->cap * 2 : 8;
        target->entries = realloc(target->entries, sizeof(EnvEntry) * target->cap);
    }
    target->entries[target->len].name     = strdup(name);
    val_retain(val);
    target->entries[target->len].value    = val;
    target->entries[target->len].is_const = is_const;
    target->len++;
    return 1;
}

int env_update(Env *env, const char *name, Value *val) {
    for (Env *e = env; e; e = e->parent) {
        for (int i = 0; i < e->len; i++) {
            if (!strcmp(e->entries[i].name, name)) {
                if (e->entries[i].is_const) {
                    fprintf(stderr,
                        "\033[31m[NullScript Error]: cannot reassign const '%s'. "
                        "that's literally what const means.\033[0m\n", name);
                    return 0;
                }
                val_release(e->entries[i].value);
                val_retain(val);
                e->entries[i].value = val;
                return 1;
            }
        }
    }
    fprintf(stderr,
        "\033[31m[NullScript Error]: '%s' is not defined. "
        "you can't update something that doesn't exist.\033[0m\n", name);
    return 0;
}
