#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include "interp.h"

// Shared interpreter pointer — set by interp.c before any eval
extern Interpreter *g_interp;
// ── Helpers ────────────────────────────────────────────────────────────────
#define ARG(n) ((argc > (n)) ? args[n] : NULL)
#define REQUIRE(n, name) \
    if (argc < (n)) { \
        fprintf(stderr, "\033[31m[NullScript Error]: %s() requires %d argument(s).\033[0m\n", name, n); \
        return val_null(); \
    }

// ── I/O ───────────────────────────────────────────────────────────────────

Value *builtin_print(Value **args, int argc, Env *env) {
    (void)env;
    const char *msg_type = "log";
    char *out = NULL;

    if (argc >= 1) out = val_to_string(args[0]);
    if (argc >= 2 && args[1] && args[1]->type == VAL_STRING) msg_type = args[1]->sval;

    if (!strcmp(msg_type, "warn"))
        fprintf(stderr, "\033[33m[WARN] %s\033[0m", out ? out : "");
    else if (!strcmp(msg_type, "err"))
        fprintf(stderr, "\033[31m[ERR] %s\033[0m", out ? out : "");
    else
        printf("%s", out ? out : "");

    free(out);
    return val_null();
}

Value *builtin_println(Value **args, int argc, Env *env) {
    (void)env;
    const char *msg_type = "log";
    char *out = NULL;

    if (argc >= 1) out = val_to_string(args[0]);
    if (argc >= 2 && args[1] && args[1]->type == VAL_STRING) msg_type = args[1]->sval;

    if (!strcmp(msg_type, "warn"))
        fprintf(stderr, "\033[33m[WARN] %s\n\033[0m", out ? out : "");
    else if (!strcmp(msg_type, "err"))
        fprintf(stderr, "\033[31m[ERR] %s\n\033[0m", out ? out : "");
    else
        printf("%s\n", out ? out : "");

    free(out);
    return val_null();
}

Value *builtin_readline(Value **args, int argc, Env *env) {
    (void)args; (void)argc; (void)env;
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin)) {
        int len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return val_string(buf);
    }
    return val_null();
}

Value *builtin_clear(Value **args, int argc, Env *env) {
    (void)args; (void)argc; (void)env;
    printf("\033[2J\033[H");
    fflush(stdout);
    return val_null();
}

// ── Type ──────────────────────────────────────────────────────────────────

Value *builtin_type(Value **args, int argc, Env *env) {
    (void)env;
    if (argc < 1) return val_string("Undefined");
    return val_string(val_type_name(args[0]));
}

Value *builtin_convert(Value **args, int argc, Env *env) {
    (void)env;
    REQUIRE(2, "convert");
    Value *from = args[0];
    Value *to   = args[1];  // should be a type token value (string like "String", "Integer", etc.)

    // to might be passed as an identifier whose value is the type name
    const char *tname = "";
    if (to && to->type == VAL_STRING) tname = to->sval;

    char *s = val_to_string(from);
    Value *result = val_null();

    if (!strcmp(tname, "String"))  result = val_string(s);
    else if (!strcmp(tname, "Integer")) result = val_int(atoll(s));
    else if (!strcmp(tname, "Float"))   result = val_float(atof(s));
    else if (!strcmp(tname, "Decimal")) result = val_decimal(s);
    else if (!strcmp(tname, "Bool"))    result = val_bool(atoi(s));

    free(s);
    return result;
}

// ── Math ──────────────────────────────────────────────────────────────────

#define MATH_FN(name, fn) \
Value *builtin_math_##name(Value **args, int argc, Env *env) { \
    (void)env; REQUIRE(1, "Math."#name); \
    double x = (args[0]->type == VAL_INT) ? (double)args[0]->ival : args[0]->fval; \
    return val_float(fn(x)); \
}

MATH_FN(abs,   fabs)
MATH_FN(sqrt,  sqrt)
MATH_FN(floor, floor)
MATH_FN(ceil,  ceil)
MATH_FN(round, round)

Value *builtin_math_min(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "Math.min");
    double a = (args[0]->type == VAL_INT) ? (double)args[0]->ival : args[0]->fval;
    double b = (args[1]->type == VAL_INT) ? (double)args[1]->ival : args[1]->fval;
    return val_float(a < b ? a : b);
}

Value *builtin_math_max(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "Math.max");
    double a = (args[0]->type == VAL_INT) ? (double)args[0]->ival : args[0]->fval;
    double b = (args[1]->type == VAL_INT) ? (double)args[1]->ival : args[1]->fval;
    return val_float(a > b ? a : b);
}

// ── String / Array / Dict ops ─────────────────────────────────────────────

Value *builtin_len(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "len");
    Value *v = args[0];
    if (v->type == VAL_STRING)  return val_int(strlen(v->sval));
    if (v->type == VAL_ARRAY)   return val_int(v->array.len);
    if (v->type == VAL_DICT)    return val_int(v->dict.len);
    return val_int(0);
}

Value *builtin_split(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "split");
    if (args[0]->type != VAL_STRING) return val_array();
    char *s   = strdup(args[0]->sval);
    char *sep = (args[1]->type == VAL_STRING) ? args[1]->sval : " ";
    Value *arr = val_array();
    char *tok = strtok(s, sep);
    while (tok) { val_array_push(arr, val_string(tok)); tok = strtok(NULL, sep); }
    free(s);
    return arr;
}

Value *builtin_join(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "join");
    if (args[0]->type != VAL_ARRAY) return val_string("");
    Value *arr = args[0];
    const char *sep = (args[1]->type == VAL_STRING) ? args[1]->sval : "";
    char buf[8192] = "";
    for (int i = 0; i < arr->array.len; i++) {
        char *s = val_to_string(arr->array.items[i]);
        strcat(buf, s); free(s);
        if (i < arr->array.len - 1) strcat(buf, sep);
    }
    return val_string(buf);
}

Value *builtin_replace(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(3, "replace");
    if (args[0]->type != VAL_STRING) return val_copy(args[0]);
    const char *src  = args[0]->sval;
    const char *from = (args[1]->type == VAL_STRING) ? args[1]->sval : "";
    const char *to   = (args[2]->type == VAL_STRING) ? args[2]->sval : "";
    if (!strlen(from)) return val_string(src);

    char buf[8192] = "";
    const char *p = src;
    int fl = strlen(from);
    while (*p) {
        if (!strncmp(p, from, fl)) { strcat(buf, to); p += fl; }
        else { char tmp[2] = {*p, 0}; strcat(buf, tmp); p++; }
    }
    return val_string(buf);
}

Value *builtin_push(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "push");
    if (args[0]->type != VAL_ARRAY) return val_null();
    val_array_push(args[0], args[1]);
    return val_copy(args[0]);
}

Value *builtin_pop(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "pop");
    if (args[0]->type != VAL_ARRAY || args[0]->array.len == 0) return val_null();
    Value *item = args[0]->array.items[--args[0]->array.len];
    return item; // caller now owns it
}

Value *builtin_append(Value **args, int argc, Env *env) {
    return builtin_push(args, argc, env);
}

Value *builtin_remove(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "remove");
    if (args[0]->type != VAL_ARRAY) return val_null();
    long long idx = (args[1]->type == VAL_INT) ? args[1]->ival - 1 : -1;
    if (idx < 0 || idx >= args[0]->array.len) return val_null();
    Value *removed = args[0]->array.items[idx];
    for (long long i = idx; i < args[0]->array.len - 1; i++)
        args[0]->array.items[i] = args[0]->array.items[i+1];
    args[0]->array.len--;
    return removed;
}

Value *builtin_keys(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "keys");
    Value *out = val_array();
    if (args[0]->type == VAL_DICT) {
        for (int i = 0; i < args[0]->dict.len; i++)
            val_array_push(out, val_string(args[0]->dict.keys[i]));
    }
    return out;
}

Value *builtin_values(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "values");
    Value *out = val_array();
    if (args[0]->type == VAL_DICT) {
        for (int i = 0; i < args[0]->dict.len; i++)
            val_array_push(out, val_copy(args[0]->dict.vals[i]));
    }
    return out;
}

Value *builtin_contains(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "contains");
    if (args[0]->type == VAL_STRING && args[1]->type == VAL_STRING)
        return val_bool(strstr(args[0]->sval, args[1]->sval) != NULL);
    if (args[0]->type == VAL_ARRAY) {
        for (int i = 0; i < args[0]->array.len; i++)
            if (val_equals(args[0]->array.items[i], args[1])) return val_bool(1);
    }
    return val_bool(0);
}

Value *builtin_startsWith(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "startsWith");
    if (args[0]->type != VAL_STRING || args[1]->type != VAL_STRING) return val_bool(0);
    return val_bool(!strncmp(args[0]->sval, args[1]->sval, strlen(args[1]->sval)));
}

Value *builtin_endsWith(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "endsWith");
    if (args[0]->type != VAL_STRING || args[1]->type != VAL_STRING) return val_bool(0);
    int sl = strlen(args[0]->sval), fl = strlen(args[1]->sval);
    if (fl > sl) return val_bool(0);
    return val_bool(!strcmp(args[0]->sval + sl - fl, args[1]->sval));
}

Value *builtin_index(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "index");
    if (args[0]->type == VAL_STRING && args[1]->type == VAL_STRING) {
        char *p = strstr(args[0]->sval, args[1]->sval);
        if (!p) return val_int(-1);
        return val_int((p - args[0]->sval) + 1); // 1-based
    }
    if (args[0]->type == VAL_ARRAY) {
        for (int i = 0; i < args[0]->array.len; i++)
            if (val_equals(args[0]->array.items[i], args[1])) return val_int(i+1);
    }
    return val_int(-1);
}

Value *builtin_charsFrom(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(3, "charsFrom");
    if (args[0]->type != VAL_STRING) return val_string("");
    int len = strlen(args[0]->sval);
    int start = (args[1]->type == VAL_INT) ? (int)args[1]->ival - 1 : 0;
    int end   = (args[2]->type == VAL_INT) ? (int)args[2]->ival     : len;
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return val_string("");
    char *buf = malloc(end - start + 1);
    strncpy(buf, args[0]->sval + start, end - start);
    buf[end - start] = '\0';
    Value *v = val_string(buf);
    free(buf);
    return v;
}

Value *builtin_insert(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(3, "insert");
    if (args[0]->type != VAL_ARRAY) return val_null();
    long long idx = (args[2]->type == VAL_INT) ? args[2]->ival - 1 : args[0]->array.len;
    val_array_push(args[0], val_null()); // grow by one
    for (long long i = args[0]->array.len - 1; i > idx; i--)
        args[0]->array.items[i] = args[0]->array.items[i-1];
    val_retain(args[1]);
    args[0]->array.items[idx] = args[1];
    return val_copy(args[0]);
}

Value *builtin_sort(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "sort");
    if (args[0]->type != VAL_ARRAY) return val_copy(args[0]);
    // Simple insertion sort
    for (int i = 1; i < args[0]->array.len; i++) {
        Value *key = args[0]->array.items[i];
        int j = i - 1;
        while (j >= 0) {
            Value *a = args[0]->array.items[j];
            int cmp = 0;
            if (a->type == VAL_INT && key->type == VAL_INT) cmp = a->ival > key->ival;
            else if (a->type == VAL_STRING && key->type == VAL_STRING) cmp = strcmp(a->sval, key->sval) > 0;
            else break;
            if (!cmp) break;
            args[0]->array.items[j+1] = args[0]->array.items[j];
            j--;
        }
        args[0]->array.items[j+1] = key;
    }
    return val_copy(args[0]);
}

Value *builtin_reverse(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "reverse");
    if (args[0]->type != VAL_ARRAY) return val_copy(args[0]);
    int n = args[0]->array.len;
    for (int i = 0; i < n/2; i++) {
        Value *tmp = args[0]->array.items[i];
        args[0]->array.items[i]     = args[0]->array.items[n-1-i];
        args[0]->array.items[n-1-i] = tmp;
    }
    return val_copy(args[0]);
}

Value *builtin_merge(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "merge");
    Value *out = val_array();
    if (args[0]->type == VAL_ARRAY) {
        for (int i = 0; i < args[0]->array.len; i++) val_array_push(out, val_copy(args[0]->array.items[i]));
    }
    if (args[1]->type == VAL_ARRAY) {
        for (int i = 0; i < args[1]->array.len; i++) val_array_push(out, val_copy(args[1]->array.items[i]));
    }
    return out;
}

Value *builtin_isNull(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "isNull");
    return val_bool(args[0]->type == VAL_NULL || args[0]->type == VAL_UNDEFINED);
}

Value *builtin_hasKey(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(2, "hasKey");
    if (args[0]->type != VAL_DICT || args[1]->type != VAL_STRING) return val_bool(0);
    for (int i = 0; i < args[0]->dict.len; i++)
        if (!strcmp(args[0]->dict.keys[i], args[1]->sval)) return val_bool(1);
    return val_bool(0);
}

Value *builtin_assert(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "assert");
    if (!val_is_truthy(args[0])) {
        const char *msg = (argc >= 2 && args[1] && args[1]->type == VAL_STRING)
                          ? args[1]->sval : "assertion failed";
        if (g_interp) {
            g_interp->had_error = 1;
            snprintf(g_interp->error_msg, sizeof(g_interp->error_msg), "%s", msg);
        } else {
            fprintf(stderr, "\033[31m[NullScript Error]: %s\033[0m\n", msg);
            exit(1);
        }
    }
    return val_null();
}

Value *builtin_die(Value **args, int argc, Env *env) {
    (void)env;
    int code = 0;
    if (argc >= 1 && args[0]->type == VAL_INT) code = (int)args[0]->ival;
    else if (argc >= 1 && args[0]->type == VAL_STRING) {
        fprintf(stderr, "%s\n", args[0]->sval);
    }
    exit(code);
    return val_null();
}

Value *builtin_time(Value **args, int argc, Env *env) {
    (void)env;
    int hr = (argc >= 1 && args[0]->type == VAL_BOOL) ? args[0]->bval : 0;
    time_t t = time(NULL);
    if (hr) {
        char *s = ctime(&t);
        if (s) { s[strlen(s)-1] = '\0'; return val_string(s); }
        return val_string("?");
    }
    return val_int((long long)t);
}

Value *builtin_wait(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "wait");
    int ms = (args[0]->type == VAL_INT) ? (int)args[0]->ival : 0;
    usleep(ms * 1000);
    return val_null();
}

Value *builtin_env(Value **args, int argc, Env *env_scope) {
    (void)env_scope; REQUIRE(1, "env");
    if (args[0]->type != VAL_STRING) return val_null();
    char *v = getenv(args[0]->sval);
    return v ? val_string(v) : val_null();
}

Value *builtin_shell(Value **args, int argc, Env *env) {
    (void)env; REQUIRE(1, "shell");
    if (args[0]->type != VAL_STRING) return val_null();
    FILE *fp = popen(args[0]->sval, "r");
    if (!fp) return val_null();
    char buf[4096] = "";
    char line[512];
    while (fgets(line, sizeof(line), fp)) strcat(buf, line);
    pclose(fp);
    return val_string(buf);
}

Value *builtin_trace(Value **args, int argc, Env *env) {
    (void)env;
    fprintf(stderr, "\033[36m[trace]\033[0m ");
    for (int i = 0; i < argc; i++) {
        char *s = val_to_string(args[i]);
        fprintf(stderr, "%s ", s);
        free(s);
    }
    fprintf(stderr, "\n");
    return val_null();
}

Value *builtin_random(Value **args, int argc, Env *env) {
    (void)env;
    long long mn = (argc >= 1 && args[0]->type == VAL_INT) ? args[0]->ival : 0;
    long long mx = (argc >= 2 && args[1]->type == VAL_INT) ? args[1]->ival : 100;
    const char *kind = (argc >= 3 && args[2]->type == VAL_STRING) ? args[2]->sval : "Integer";
    long long range  = mx - mn + 1;
    if (range <= 0) return val_int(mn);
    long long r = (rand() % range) + mn;
    if (!strcmp(kind, "Float"))   return val_float((double)r + ((double)rand()/RAND_MAX));
    if (!strcmp(kind, "Decimal")) {
        char buf[64]; snprintf(buf, sizeof(buf), "%lld.%02d", r, rand()%100);
        return val_decimal(buf);
    }
    return val_int(r);
}

Value *builtin_map(Value **args, int argc, Env *env);   // defined in interp.c (needs eval)
Value *builtin_filter(Value **args, int argc, Env *env);
Value *builtin_reduce(Value **args, int argc, Env *env);
Value *builtin_forEach(Value **args, int argc, Env *env);

// ── Registration ──────────────────────────────────────────────────────────

void register_builtins(Env *env) {
    srand((unsigned)time(NULL));

#define REG(name, fn) env_set(env, name, val_builtin(name, fn), 0, 1)
    REG("print",        builtin_print);
    REG("println",      builtin_println);
    REG("readline",     builtin_readline);
    REG("clear",        builtin_clear);
    REG("type",         builtin_type);
    REG("convert",      builtin_convert);
    REG("len",          builtin_len);
    REG("split",        builtin_split);
    REG("join",         builtin_join);
    REG("replace",      builtin_replace);
    REG("push",         builtin_push);
    REG("pop",          builtin_pop);
    REG("append",       builtin_append);
    REG("remove",       builtin_remove);
    REG("keys",         builtin_keys);
    REG("values",       builtin_values);
    REG("contains",     builtin_contains);
    REG("startsWith",   builtin_startsWith);
    REG("endsWith",     builtin_endsWith);
    REG("index",        builtin_index);
    REG("charsFrom",    builtin_charsFrom);
    REG("insert",       builtin_insert);
    REG("sort",         builtin_sort);
    REG("reverse",      builtin_reverse);
    REG("merge",        builtin_merge);
    REG("isNull",       builtin_isNull);
    REG("hasKey",       builtin_hasKey);
    REG("assert",       builtin_assert);
    REG("die",          builtin_die);
    REG("time",         builtin_time);
    REG("wait",         builtin_wait);
    REG("env",          builtin_env);
    REG("shell",        builtin_shell);
    REG("trace",        builtin_trace);
    REG("random",       builtin_random);
    REG("map",          builtin_map);
    REG("filter",       builtin_filter);
    REG("reduce",       builtin_reduce);
    REG("forEach",      builtin_forEach);
#undef REG

    // Math namespace as a dict of builtins
    Value *Math = val_dict();
#define MREG(name, fn) val_dict_set(Math, name, val_builtin("Math."name, fn))
    MREG("abs",   builtin_math_abs);
    MREG("sqrt",  builtin_math_sqrt);
    MREG("floor", builtin_math_floor);
    MREG("ceil",  builtin_math_ceil);
    MREG("round", builtin_math_round);
    MREG("min",   builtin_math_min);
    MREG("max",   builtin_math_max);
#undef MREG
    env_set(env, "Math", Math, 1, 1);
    val_release(Math);

    // Type name constants (so convert(x, String) works — String resolves to "String")
    // We store them as strings so the interpreter can pass them to convert()
    env_set(env, "String",    val_string("String"),    1, 1);
    env_set(env, "Integer",   val_string("Integer"),   1, 1);
    env_set(env, "Float",     val_string("Float"),     1, 1);
    env_set(env, "Decimal",   val_string("Decimal"),   1, 1);
    env_set(env, "Bool",      val_string("Bool"),      1, 1);
    env_set(env, "Any",       val_string("Any"),       1, 1);
    env_set(env, "Null",      val_null(),              1, 1);
    env_set(env, "True",      val_bool(1),             1, 1);
    env_set(env, "False",     val_bool(0),             1, 1);
}
