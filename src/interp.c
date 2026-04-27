#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "interp.h"
#include "ast.h"

// val_new is static in value.c; redeclare locally for use in interp.c
static Value *val_new(ValType t) {
    Value *v = calloc(1, sizeof(Value));
    v->type  = t;
    v->ref_count = 1;
    return v;
}

// Forward declarations
static Value *eval(Interpreter *it, ASTNode *node, Env *env);
void register_builtins(Env *env);

// ── Error handling ────────────────────────────────────────────────────────

static void set_error(Interpreter *it, const char *msg) {
    it->had_error = 1;
    strncpy(it->error_msg, msg, sizeof(it->error_msg) - 1);
    fprintf(stderr, "\033[31m[NullScript Error]: %s\033[0m\n", msg);
}

// ── String interpolation ──────────────────────────────────────────────────
// Processes e"..." strings containing $var and ${expr} — we re-parse inline exprs

static char *interp_estring(Interpreter *it, const char *raw, Env *env);

// We need the parser/lexer here for inline expressions
#include "lexer.h"
#include "parser.h"

static char *interp_estring(Interpreter *it, const char *raw, Env *env) {
    char *out  = malloc(8192);
    out[0]     = '\0';
    const char *p = raw;

    while (*p) {
        if (*p == '$') {
            p++;
            if (*p == '{') {
                // ${expr}
                p++;
                char expr_buf[1024] = "";
                int depth = 1;
                while (*p && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') { depth--; if (!depth) { p++; break; } }
                    strncat(expr_buf, p, 1);
                    p++;
                }
                // Parse & evaluate expr_buf as a single expression
                Token **toks = lexer_tokenise(expr_buf);
                ASTNode *expr_node = parse_single_expr(toks);
                if (expr_node) {
                    Value *v = eval(it, expr_node, env);
                    char *s  = val_to_string(v);
                    strcat(out, s);
                    free(s);
                    val_release(v);
                    ast_node_free(expr_node);
                }
                tokens_free(toks);
            } else {
                // $varname
                char var[128] = "";
                while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                               (*p >= '0' && *p <= '9') || *p == '_')) {
                    strncat(var, p, 1); p++;
                }
                Value *v = env_get(env, var);
                if (v) {
                    char *s = val_to_string(v);
                    strcat(out, s);
                    free(s);
                } else {
                    strcat(out, "$"); strcat(out, var);
                }
            }
        } else {
            strncat(out, p, 1);
            p++;
        }
    }
    return out;
}

// ── Arithmetic helpers ─────────────────────────────────────────────────────

static Value *numeric_binop(const char *op, Value *a, Value *b) {
    // Promote to float if either is float
    double da, db;
    int both_int = (a->type == VAL_INT && b->type == VAL_INT);

    if (a->type == VAL_INT)   da = (double)a->ival;
    else if (a->type == VAL_FLOAT) da = a->fval;
    else return val_null();

    if (b->type == VAL_INT)   db = (double)b->ival;
    else if (b->type == VAL_FLOAT) db = b->fval;
    else return val_null();

    double result = 0;
    if      (!strcmp(op, "+"))  result = da + db;
    else if (!strcmp(op, "-"))  result = da - db;
    else if (!strcmp(op, "*"))  result = da * db;
    else if (!strcmp(op, "/"))  { if (db == 0) { fprintf(stderr, "\033[31m[NullScript Error]: division by zero. bold move.\033[0m\n"); return val_null(); } result = da / db; }
    else if (!strcmp(op, "%"))  result = fmod(da, db);
    else if (!strcmp(op, "**")) result = pow(da, db);

    if (both_int && strcmp(op, "/")) return val_int((long long)result);
    return val_float(result);
}

// ── Call a function value ──────────────────────────────────────────────────

static Value *call_value(Interpreter *it, Value *fn, Value **args, int argc, Env *call_env) {
    if (fn->type == VAL_BUILTIN) {
        return fn->builtin.fn(args, argc, call_env);
    }
    if (fn->type == VAL_FUNCTION) {
        Env *fn_env = env_new(fn->func.closure);
        // Bind params
        for (int i = 0; i < fn->func.n_params && i < argc; i++) {
            env_set(fn_env, fn->func.params[i], args[i], 0, 0);
        }
        Value *result = eval(it, fn->func.body, fn_env);
        // Unwrap return sentinel
        if (result && result->type == VAL_RETURN) {
            Value *inner = result->array.len > 0 ? val_copy(result->array.items[0]) : val_null();
            val_release(result);
            env_free(fn_env);
            return inner;
        }
        env_free(fn_env);
        return result ? result : val_null();
    }
    fprintf(stderr, "\033[31m[NullScript Error]: tried to call a non-function. embarrassing.\033[0m\n");
    return val_null();
}

// ── map/filter/reduce — need eval, so defined here ─────────────────────────

// Defined in builtins.c header but need access to call_value:
// We handle them via a global interp pointer trick — use a thread-local or param
// For simplicity, we use a static pointer (single-threaded)
static Interpreter *g_it = NULL;
Interpreter *g_interp    = NULL;  // shared with builtins.c
static Env         *g_env = NULL;

Value *builtin_map(Value **args, int argc, Env *env) {
    if (argc < 2 || args[0]->type != VAL_ARRAY) return val_array();
    Value *arr = args[0], *fn = args[1];
    Value *out = val_array();
    for (int i = 0; i < arr->array.len; i++) {
        Value *item = arr->array.items[i];
        Value *mapped = call_value(g_it, fn, &item, 1, env);
        val_array_push(out, mapped);
        val_release(mapped);
    }
    return out;
}

Value *builtin_filter(Value **args, int argc, Env *env) {
    if (argc < 2 || args[0]->type != VAL_ARRAY) return val_array();
    Value *arr = args[0], *fn = args[1];
    Value *out = val_array();
    for (int i = 0; i < arr->array.len; i++) {
        Value *item = arr->array.items[i];
        Value *res  = call_value(g_it, fn, &item, 1, env);
        if (val_is_truthy(res)) val_array_push(out, val_copy(item));
        val_release(res);
    }
    return out;
}

Value *builtin_reduce(Value **args, int argc, Env *env) {
    if (argc < 2 || args[0]->type != VAL_ARRAY) return val_null();
    Value *arr = args[0], *fn = args[1];
    Value *acc = (argc >= 3) ? val_copy(args[2]) : (arr->array.len > 0 ? val_copy(arr->array.items[0]) : val_null());
    int start  = (argc >= 3) ? 0 : 1;
    for (int i = start; i < arr->array.len; i++) {
        Value *pair[2] = { acc, arr->array.items[i] };
        Value *next = call_value(g_it, fn, pair, 2, env);
        val_release(acc);
        acc = next;
    }
    return acc;
}

Value *builtin_forEach(Value **args, int argc, Env *env) {
    if (argc < 2 || args[0]->type != VAL_ARRAY) return val_null();
    Value *arr = args[0], *fn = args[1];
    for (int i = 0; i < arr->array.len; i++) {
        Value *item = arr->array.items[i];
        Value *res  = call_value(g_it, fn, &item, 1, env);
        val_release(res);
    }
    return val_null();
}

// ── Alias table ───────────────────────────────────────────────────────────

Alias aliases[64];
int alias_count = 0;

static const char *resolve_alias(const char *name) {
    // Keyword aliases (e.g. "alias func fn") are parse-time only.
    // At runtime, only resolve non-keyword aliases (e.g. "alias println log").
    static const char *KEYWORDS[] = {
        "func","var","const","if","else","repeat","import","alias",
        "return","export","and","or","not","async","await","spawn",
        "try","catch", NULL
    };
    for (int i = 0; i < alias_count; i++) {
        if (!strcmp(aliases[i].to, name)) {
            // Skip this alias if 'from' is a language keyword
            int from_is_kw = 0;
            for (int k = 0; KEYWORDS[k]; k++) {
                if (!strcmp(KEYWORDS[k], aliases[i].from)) { from_is_kw = 1; break; }
            }
            if (!from_is_kw) return aliases[i].from;
        }
    }
    return name;
}

// ── Main eval ─────────────────────────────────────────────────────────────

static Value *eval(Interpreter *it, ASTNode *node, Env *env) {
    if (!node || it->had_error) return val_null();

    g_it  = it;
    g_env = env;

    switch (node->type) {

    case NODE_PROGRAM:
    case NODE_BLOCK: {
        Value *last = val_null();
        for (int i = 0; i < node->num_children; i++) {
            val_release(last);
            last = eval(it, node->children[i], env);
            if (last && last->type == VAL_RETURN) return last;
        }
        return last;
    }

    case NODE_LITERAL_NULL:    return val_null();
    case NODE_LITERAL_BOOL:    return val_bool((int)node->ival);
    case NODE_LITERAL_INT:     return val_int(node->ival);
    case NODE_LITERAL_FLOAT:   return val_float(node->fval);
    case NODE_LITERAL_DECIMAL: return val_decimal(node->sval);
    case NODE_LITERAL_STRING:  return val_string(node->sval);

    case NODE_LITERAL_ESTRING: {
        char *s = interp_estring(it, node->sval, env);
        Value *v = val_string(s);
        free(s);
        return v;
    }

    case NODE_IDENT: {
        const char *name = resolve_alias(node->sval);
        Value *v = env_get(env, name);
        if (!v) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "undefined variable '%s'. did you forget to declare it? or spell it wrong? or both?",
                name);
            set_error(it, msg);
            it->had_error = 0; // allow continuation
            return val_undefined();
        }
        return val_copy(v);
    }

    case NODE_VAR_DECL:
    case NODE_CONST_DECL: {
        Value *val = eval(it, node->children[0], env);
        int global  = node->var_scope && !strcmp(node->var_scope, "Global");
        env_set(env, node->sval, val, node->is_const, global);
        val_release(val);
        return val_null();
    }

    case NODE_FUNC_DECL: {
        Value *fn         = val_new(VAL_FUNCTION);
        fn->func.n_params = node->num_children - 1; // last child is body
        fn->func.params   = malloc(sizeof(char*) * (fn->func.n_params + 1));
        for (int i = 0; i < fn->func.n_params; i++)
            fn->func.params[i] = strdup(node->children[i]->sval);
        fn->func.body     = node->children[node->num_children - 1];
        fn->func.closure  = env;
        env_retain(env);  // function keeps a reference to its closure env
        fn->func.is_async = (int)node->ival;
        // Register in env AND return the value (so it works as an expression argument)
        env_set(env, node->sval, fn, 0, 0);
        val_retain(fn); // one retain for the caller (val_release in env_set consumed one)
        return fn;
    }

    case NODE_RETURN: {
        // Wrap the return value in a sentinel
        Value *sentinel   = val_new(VAL_RETURN);
        Value *inner      = (node->num_children > 0) ? eval(it, node->children[0], env) : val_null();
        sentinel->array.items = malloc(sizeof(Value*));
        sentinel->array.items[0] = inner;
        sentinel->array.len = 1;
        return sentinel;
    }

    case NODE_ASSIGN: {
        ASTNode *target = node->children[0];
        Value   *rhs    = eval(it, node->children[1], env);
        const char *op  = node->sval;

        // Get existing value for compound ops
        Value *existing = NULL;
        if (strcmp(op, "=")) existing = eval(it, target, env);

        Value *new_val = NULL;
        if (!strcmp(op, "="))   new_val = val_copy(rhs);
        else if (!strcmp(op, "+="))  new_val = numeric_binop("+",  existing, rhs);
        else if (!strcmp(op, "-="))  new_val = numeric_binop("-",  existing, rhs);
        else if (!strcmp(op, "*="))  new_val = numeric_binop("*",  existing, rhs);
        else if (!strcmp(op, "/="))  new_val = numeric_binop("/",  existing, rhs);
        else if (!strcmp(op, "%="))  new_val = numeric_binop("%",  existing, rhs);
        else if (!strcmp(op, "**=")) new_val = numeric_binop("**", existing, rhs);
        else new_val = val_copy(rhs);

        // Handle string += concatenation
        if (!strcmp(op, "+=") && existing && existing->type == VAL_STRING) {
            char *b = val_to_string(rhs);
            char *combined = malloc(strlen(existing->sval) + strlen(b) + 1);
            strcpy(combined, existing->sval);
            strcat(combined, b);
            free(b); free(new_val);
            new_val = val_string(combined);
            free(combined);
        }

        val_release(existing);
        val_release(rhs);

        // Assign to target
        if (target->type == NODE_IDENT) {
            const char *name = resolve_alias(target->sval);
            if (!env_update(env, name, new_val)) {
                // var doesn't exist, create it
                env_set(env, name, new_val, 0, 0);
            }
        } else if (target->type == NODE_INDEX) {
            Value *container = eval(it, target->children[0], env);
            Value *key       = eval(it, target->children[1], env);
            if (container->type == VAL_ARRAY && key->type == VAL_INT) {
                long long idx = key->ival - 1;
                if (idx >= 0 && idx < container->array.len) {
                    val_release(container->array.items[idx]);
                    val_retain(new_val);
                    container->array.items[idx] = new_val;
                }
            } else if (container->type == VAL_DICT && key->type == VAL_STRING) {
                val_dict_set(container, key->sval, new_val);
            }
            val_release(container);
            val_release(key);
        }

        val_release(new_val);
        return val_null();
    }

    case NODE_BINOP: {
        const char *op = node->sval;
        // Short-circuit logic
        if (!strcmp(op, "and")) {
            Value *a = eval(it, node->children[0], env);
            if (!val_is_truthy(a)) { return a; }
            val_release(a);
            return eval(it, node->children[1], env);
        }
        if (!strcmp(op, "or")) {
            Value *a = eval(it, node->children[0], env);
            if (val_is_truthy(a)) return a;
            val_release(a);
            return eval(it, node->children[1], env);
        }

        Value *a = eval(it, node->children[0], env);
        Value *b = eval(it, node->children[1], env);
        Value *res = val_null();

        if (!strcmp(op, "==")) res = val_bool(val_equals(a, b));
        else if (!strcmp(op, "!=")) res = val_bool(!val_equals(a, b));
        else if (!strcmp(op, "<"))  res = val_bool(
            (a->type == VAL_INT   && b->type == VAL_INT)   ? a->ival < b->ival :
            (a->type == VAL_FLOAT || b->type == VAL_FLOAT) ?
              ((a->type==VAL_INT?(double)a->ival:a->fval) < (b->type==VAL_INT?(double)b->ival:b->fval)) : 0);
        else if (!strcmp(op, ">"))  res = val_bool(
            (a->type == VAL_INT   && b->type == VAL_INT)   ? a->ival > b->ival :
            (a->type == VAL_FLOAT || b->type == VAL_FLOAT) ?
              ((a->type==VAL_INT?(double)a->ival:a->fval) > (b->type==VAL_INT?(double)b->ival:b->fval)) : 0);
        else if (!strcmp(op, "<=")) res = val_bool(!val_equals(a,b) ?
            ((a->type==VAL_INT&&b->type==VAL_INT) ? a->ival<=b->ival :
             (a->type==VAL_INT?(double)a->ival:a->fval) <= (b->type==VAL_INT?(double)b->ival:b->fval)) : 1);
        else if (!strcmp(op, ">=")) res = val_bool(!val_equals(a,b) ?
            ((a->type==VAL_INT&&b->type==VAL_INT) ? a->ival>=b->ival :
             (a->type==VAL_INT?(double)a->ival:a->fval) >= (b->type==VAL_INT?(double)b->ival:b->fval)) : 1);
        // String concatenation via +
        else if (!strcmp(op, "+") && a->type == VAL_STRING) {
            char *sb = val_to_string(b);
            char *combined = malloc(strlen(a->sval) + strlen(sb) + 1);
            strcpy(combined, a->sval); strcat(combined, sb);
            free(sb); res = val_string(combined); free(combined);
        }
        else res = numeric_binop(op, a, b);

        val_release(a); val_release(b);
        return res;
    }

    case NODE_UNOP: {
        const char *op = node->sval;
        Value *v = eval(it, node->children[0], env);
        Value *res = val_null();
        if (!strcmp(op, "not")) res = val_bool(!val_is_truthy(v));
        else if (!strcmp(op, "-")) {
            if (v->type == VAL_INT)   res = val_int(-v->ival);
            else if (v->type == VAL_FLOAT) res = val_float(-v->fval);
        }
        val_release(v);
        return res;
    }

    case NODE_IF: {
        // children: cond, body, [elif_NODE_IF | else_NODE_BLOCK] ...
        Value *cond = eval(it, node->children[0], env);
        int truth   = val_is_truthy(cond);
        val_release(cond);
        if (truth) {
            return eval(it, node->children[1], env);
        }
        // Walk else/elif branches — there is at most one: the else-chain node
        for (int i = 2; i < node->num_children; i++) {
            ASTNode *branch = node->children[i];
            if (branch->type == NODE_IF) {
                // This is an "else if" — evaluate it as a full if statement
                // (it carries its own condition, body, and possibly more else-ifs)
                return eval(it, branch, env);
            } else if (branch->type == NODE_BLOCK) {
                // Plain "else" block
                return eval(it, branch, env);
            }
        }
        return val_null();
    }

    case NODE_REPEAT: {
        // children[0] = count expr, children[1] = body
        Value *count_val = eval(it, node->children[0], env);
        long long count  = (count_val->type == VAL_INT) ? count_val->ival : 0;
        val_release(count_val);
        Value *last = val_null();
        for (long long i = 1; i <= count; i++) {
            val_release(last);
            Env *loop_env = env_new(env);
            env_set(loop_env, node->sval, val_int(i), 0, 0);
            last = eval(it, node->children[1], loop_env);
            env_free(loop_env);
            if (last && last->type == VAL_RETURN) return last;
        }
        return last;
    }

    case NODE_RANGE: {
        Value *start_v = eval(it, node->children[0], env);
        Value *end_v   = eval(it, node->children[1], env);
        long long start = (start_v->type == VAL_INT) ? start_v->ival : 0;
        long long end   = (end_v->type   == VAL_INT) ? end_v->ival   : 0;
        val_release(start_v); val_release(end_v);
        Value *arr = val_array();
        for (long long i = start; i <= end; i++) val_array_push(arr, val_int(i));
        return arr;
    }

    case NODE_ARRAY: {
        Value *arr = val_array();
        for (int i = 0; i < node->num_children; i++) {
            Value *item = eval(it, node->children[i], env);
            val_array_push(arr, item);
            val_release(item);
        }
        return arr;
    }

    case NODE_DICT: {
        Value *dict = val_dict();
        for (int i = 0; i < node->num_children; i++) {
            ASTNode *kv = node->children[i];
            if (kv->type == NODE_NAMED_ARG) {
                Value *val = eval(it, kv->children[0], env);
                val_dict_set(dict, kv->sval, val);
                val_release(val);
            }
        }
        return dict;
    }

    case NODE_INDEX: {
        Value *container = eval(it, node->children[0], env);
        Value *key       = eval(it, node->children[1], env);
        Value *result    = val_null();
        if (container->type == VAL_ARRAY && key->type == VAL_INT) {
            result = val_copy(val_array_get(container, key->ival));
        } else if (container->type == VAL_DICT && key->type == VAL_STRING) {
            result = val_copy(val_dict_get(container, key->sval));
        } else if (container->type == VAL_STRING && key->type == VAL_INT) {
            long long idx = key->ival - 1;
            if (idx >= 0 && idx < (long long)strlen(container->sval)) {
                char ch[2] = {container->sval[idx], '\0'};
                result = val_string(ch);
            }
        }
        val_release(container); val_release(key);
        return result;
    }

    case NODE_MEMBER: {
        // obj.method — look up member name in dict, or handle special methods
        Value *obj  = eval(it, node->children[0], env);
        const char *member = node->sval;

        // forEach is a method call that returns a callable wrapper
        // We return the object itself with the member name for call resolution in NODE_CALL
        // Simple: check if obj is dict and member is a key
        if (obj->type == VAL_DICT) {
            Value *v = val_dict_get(obj, member);
            if (v->type != VAL_NULL) { val_release(obj); return val_copy(v); }
        }
        // Built-in method-style: arr.forEach, arr.map, arr.filter, arr.reduce
        // Return a partial application wrapper (builtin bound to obj)
        // For simplicity, we store the object in a thread-local and look up the function globally
        if (obj->type == VAL_ARRAY) {
            Value *fn = env_get(env, member);
            if (!fn) fn = env_get(env, member); // also check globals
            // Create a bound function value that calls the global builtin with obj as first arg
            // We use a special dict to carry the binding
            Value *bound = val_dict();
            val_dict_set(bound, "__bound_fn__",  fn ? fn : val_null());
            val_dict_set(bound, "__bound_self__", obj);
            val_release(obj);
            return bound;
        }

        val_release(obj);
        return val_null();
    }

    case NODE_CALL: {
        // children[0] = callee, children[1..] = args or named args
        ASTNode *callee_node = node->children[0];
        Value   *callee      = eval(it, callee_node, env);

        // Collect args (handle named args by matching param names)
        int argc_raw = node->num_children - 1;
        Value **args = malloc(sizeof(Value*) * (argc_raw + 1));
        int n_named = 0;

        // First pass: count named args
        for (int i = 1; i < node->num_children; i++)
            if (node->children[i]->type == NODE_NAMED_ARG) n_named++;

        // If callee is a user function, we can do named arg resolution
        if (callee && callee->type == VAL_FUNCTION && n_named > 0) {
            // Build positional arg list by matching named args to param positions
            int argc_actual = callee->func.n_params;
            Value **ordered = calloc(argc_actual, sizeof(Value*));
            int pos_idx = 0;

            for (int i = 1; i < node->num_children; i++) {
                ASTNode *child = node->children[i];
                if (child->type == NODE_NAMED_ARG) {
                    // Find param index
                    int found = -1;
                    for (int j = 0; j < callee->func.n_params; j++) {
                        if (!strcmp(callee->func.params[j], child->sval)) { found = j; break; }
                    }
                    if (found >= 0) { ordered[found] = eval(it, child->children[0], env); }
                    else {
                        fprintf(stderr,
                            "\033[31m[NullScript Error] line %d: unknown parameter '%s'. "
                            "that's not a parameter. it has never been a parameter.\033[0m\n",
                            child->line, child->sval);
                        it->had_error = 0;
                    }
                } else {
                    // Positional after check is already done in parser
                    if (pos_idx < argc_actual) ordered[pos_idx++] = eval(it, child, env);
                }
            }
            for (int i = 0; i < argc_actual; i++) if (!ordered[i]) ordered[i] = val_null();

            Value *result = call_value(it, callee, ordered, argc_actual, env);
            for (int i = 0; i < argc_actual; i++) val_release(ordered[i]);
            free(ordered); free(args); val_release(callee);
            return result;
        }

        // Simple positional call
        int argc_actual = 0;
        for (int i = 1; i < node->num_children; i++) {
            ASTNode *child = node->children[i];
            if (child->type == NODE_NAMED_ARG)
                args[argc_actual++] = eval(it, child->children[0], env);
            else
                args[argc_actual++] = eval(it, child, env);
        }

        // Handle bound method (from NODE_MEMBER on array)
        Value *result = val_null();
        if (callee && callee->type == VAL_DICT) {
            Value *bf = val_dict_get(callee, "__bound_fn__");
            Value *bs = val_dict_get(callee, "__bound_self__");
            if (bf && bf->type != VAL_NULL && bs && bs->type != VAL_NULL) {
                Value **bound_args = malloc(sizeof(Value*) * (argc_actual + 1));
                bound_args[0] = bs;
                for (int i = 0; i < argc_actual; i++) bound_args[i+1] = args[i];
                result = call_value(it, bf, bound_args, argc_actual + 1, env);
                free(bound_args);
                goto call_done;
            }
        }

        if (callee) result = call_value(it, callee, args, argc_actual, env);

        call_done:
        for (int i = 0; i < argc_actual; i++) val_release(args[i]);
        free(args);
        val_release(callee);
        return result;
    }

    case NODE_TRY_CATCH: {
        // Simple try/catch — we use longjmp-lite via had_error flag
        Interpreter saved = *it;
        char saved_msg[512];
        strncpy(saved_msg, it->error_msg, sizeof(saved_msg));

        it->had_error = 0;
        Value *try_result = eval(it, node->children[0], env);

        if (it->had_error) {
            it->had_error = 0;
            val_release(try_result);
            Env *catch_env = env_new(env);
            env_set(catch_env, node->sval, val_string(it->error_msg), 0, 0);
            it->error_msg[0] = '\0';
            Value *catch_result = eval(it, node->children[1], catch_env);
            env_free(catch_env);
            return catch_result;
        }
        (void)saved;
        return try_result;
    }

    case NODE_IMPORT: {
        // Basic: just note that the import happened; stdlib modules can be pre-loaded
        // Runtime import of .ns files by re-interpreting them
        const char *path = node->sval;
        // Check for stdlib modules (file, etc.)
        if (!strcmp(path, "file")) {
            // Register file builtins into the env
            // For brevity, add them directly
            // file.read, file.write, file.append, file.exists
            extern void register_file_module(Env *env);
            register_file_module(env);
        }
        // .ns file import
        else if (strlen(path) > 3 && !strcmp(path + strlen(path) - 3, ".ns")) {
            FILE *f = fopen(path, "r");
            if (!f) {
                char msg[256];
                snprintf(msg, sizeof(msg), "cannot import '%s': file not found.", path);
                fprintf(stderr, "\033[31m[NullScript Error]: %s\033[0m\n", msg);
            } else {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f); rewind(f);
                char *src = malloc(sz + 1);
                fread(src, 1, sz, f); fclose(f);
                src[sz] = '\0';
                Token **toks = lexer_tokenise(src);
                ASTNode *ast = parse(toks);
                if (ast) eval(it, ast, env);
                ast_node_free(ast);
                tokens_free(toks);
                free(src);
            }
        }
        return val_null();
    }

    case NODE_ALIAS: {
        if (alias_count < 64) {
            aliases[alias_count].from = strdup(node->sval);
            aliases[alias_count].to   = strdup(node->children[0]->sval);
            alias_count++;
        }
        return val_null();
    }

    case NODE_EXPORT: {
        // Mark variable as exported — for single-file interpreter, no-op beyond eval
        return eval(it, node->children[0], env);
    }

    case NODE_AWAIT:
    case NODE_SPAWN:
        // No real async in this interpreter — just evaluate synchronously
        return eval(it, node->children[0], env);

    case NODE_TYPE_ANNOT:
        // Type annotation wrapper around a literal — just eval the inner value
        return eval(it, node->children[0], env);

    case NODE_NAMED_ARG:
        return eval(it, node->children[0], env);

    default:
        return val_null();
    }
}

// ── File module ────────────────────────────────────────────────────────────

static Value *file_read(Value **args, int argc, Env *env) {
    (void)env;
    if (argc < 1 || args[0]->type != VAL_STRING) return val_null();
    FILE *f = fopen(args[0]->sval, "r");
    if (!f) return val_null();
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f); fclose(f);
    buf[sz] = '\0';
    Value *v = val_string(buf); free(buf);
    return v;
}
static Value *file_write(Value **args, int argc, Env *env) {
    (void)env;
    if (argc < 2) return val_bool(0);
    FILE *f = fopen(args[0]->sval, "w");
    if (!f) return val_bool(0);
    char *s = val_to_string(args[1]); fputs(s, f); free(s); fclose(f);
    return val_bool(1);
}
static Value *file_append_fn(Value **args, int argc, Env *env) {
    (void)env;
    if (argc < 2) return val_bool(0);
    FILE *f = fopen(args[0]->sval, "a");
    if (!f) return val_bool(0);
    char *s = val_to_string(args[1]); fputs(s, f); free(s); fclose(f);
    return val_bool(1);
}
static Value *file_exists_fn(Value **args, int argc, Env *env) {
    (void)env;
    if (argc < 1 || args[0]->type != VAL_STRING) return val_bool(0);
    FILE *f = fopen(args[0]->sval, "r");
    if (f) { fclose(f); return val_bool(1); }
    return val_bool(0);
}

void register_file_module(Env *env) {
    Value *file = val_dict();
    val_dict_set(file, "read",   val_builtin("file.read",   file_read));
    val_dict_set(file, "write",  val_builtin("file.write",  file_write));
    val_dict_set(file, "append", val_builtin("file.append", file_append_fn));
    val_dict_set(file, "exists", val_builtin("file.exists", file_exists_fn));
    env_set(env, "file", file, 0, 1);
    val_release(file);
}

// ── Public API ────────────────────────────────────────────────────────────

Interpreter *interp_new(void) {
    Interpreter *it = calloc(1, sizeof(Interpreter));
    it->globals     = env_new(NULL);
    register_builtins(it->globals);
    return it;
}

void interp_free(Interpreter *it) {
    if (!it) return;
    env_free(it->globals);
    free(it);
}

Value *interp_run(Interpreter *it, ASTNode *program) {
    g_interp = it;
    g_it     = it;
    return eval(it, program, it->globals);
}
