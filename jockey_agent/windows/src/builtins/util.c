/*
 * util.c — Utility builtins. Windows mirror of Linux pure.c's function set.
 */
#include "jky_builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static BuiltinResult bi_print(VM *vm, Value *a, int c, Value *out)
{
    for (int i = 0; i < c; i++) {
        char *s = value_to_str(a[i]);
        fputs(s, vm->out);
        free(s);
        if (i < c - 1) fputc(' ', vm->out);
    }
    fputc('\n', vm->out);
    *out = mknone();
    return BUILTIN_OK;
}

static BuiltinResult bi_len(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    *out = value_len(a[0]);
    return BUILTIN_OK;
}

static BuiltinResult bi_str(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    char *s = value_to_str(a[0]);
    *out = mkstr(s);
    free(s);
    return BUILTIN_OK;
}

static BuiltinResult bi_int(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type == JKY_INT) { *out = a[0]; return BUILTIN_OK; }
    if (a[0].type == JKY_FLOAT) { *out = mkint((int64_t)a[0].v.f); return BUILTIN_OK; }
    if (a[0].type == JKY_STR) {
        char *end = NULL;
        long long v = strtoll(a[0].v.s, &end, 10);
        if (end && *end == 0) { *out = mkint(v); return BUILTIN_OK; }
        *out = mkint(0);
    } else {
        *out = mkint(0);
    }
    return BUILTIN_OK;
}

static BuiltinResult bi_float(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type == JKY_FLOAT) { *out = a[0]; return BUILTIN_OK; }
    if (a[0].type == JKY_INT) { *out = mkfloat((double)a[0].v.i); return BUILTIN_OK; }
    if (a[0].type == JKY_STR) *out = mkfloat(atof(a[0].v.s));
    else *out = mkfloat(0.0);
    return BUILTIN_OK;
}

static BuiltinResult bi_bool(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    *out = mkint(value_truthy(a[0]) ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_list(VM *vm, Value *a, int c, Value *out)
{
    (void)vm;
    Value arr = mkarray();
    for (int i = 0; i < c; i++)
        arr_push(arr.v.arr, vshare(a[i]));
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_dict(VM *vm, Value *a, int c, Value *out)
{
    (void)vm;
    Value d = mkdict();
    if (c == 1 && a[0].type == JKY_DICT) {
        for (DictEntry *e = a[0].v.dict->head; e; e = e->next)
            dict_set(d.v.dict, e->key, vshare(*e->value));
    }
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_range(VM *vm, Value *a, int c, Value *out)
{
    (void)vm;
    int start = 0, stop, step = 1;
    if (c == 1)      { stop = (int)a[0].v.i; }
    else if (c == 2) { start = (int)a[0].v.i; stop = (int)a[1].v.i; }
    else             { start = (int)a[0].v.i; stop = (int)a[1].v.i; step = (int)a[2].v.i; }

    Value arr = mkarray();
    if (step > 0) for (int i = start; i < stop; i += step) arr_push(arr.v.arr, mkint(i));
    else if (step < 0) for (int i = start; i > stop; i += step) arr_push(arr.v.arr, mkint(i));
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_abs(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type == JKY_INT) *out = mkint(a[0].v.i < 0 ? -a[0].v.i : a[0].v.i);
    else if (a[0].type == JKY_FLOAT) *out = mkfloat(a[0].v.f < 0.0 ? -a[0].v.f : a[0].v.f);
    else *out = mknone();
    return BUILTIN_OK;
}

static BuiltinResult bi_min(VM *vm, Value *a, int c, Value *out)
{
    (void)vm;
    if (c == 1 && a[0].type == JKY_ARRAY) {
        Value *best = NULL;
        for (int i = 0; i < a[0].v.arr->len; i++) {
            Value *v = &a[0].v.arr->items[i];
            if (!best) best = v;
            else {
                Value r = value_cmp(*v, *best, OP_LT);
                if (r.v.i) best = v;
            }
        }
        *out = best ? vshare(*best) : mknone();
        return BUILTIN_OK;
    }
    Value *best = &a[0];
    for (int i = 1; i < c; i++) {
        Value r = value_cmp(a[i], *best, OP_LT);
        if (r.v.i) best = &a[i];
    }
    *out = vshare(*best);
    return BUILTIN_OK;
}

static BuiltinResult bi_max(VM *vm, Value *a, int c, Value *out)
{
    (void)vm;
    if (c == 1 && a[0].type == JKY_ARRAY) {
        Value *best = NULL;
        for (int i = 0; i < a[0].v.arr->len; i++) {
            Value *v = &a[0].v.arr->items[i];
            if (!best) best = v;
            else {
                Value r = value_cmp(*v, *best, OP_GT);
                if (r.v.i) best = v;
            }
        }
        *out = best ? vshare(*best) : mknone();
        return BUILTIN_OK;
    }
    Value *best = &a[0];
    for (int i = 1; i < c; i++) {
        Value r = value_cmp(a[i], *best, OP_GT);
        if (r.v.i) best = &a[i];
    }
    *out = vshare(*best);
    return BUILTIN_OK;
}

static BuiltinResult bi_type(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    const char *s = "none";
    switch (a[0].type) {
        case JKY_INT:    s = "int";    break;
        case JKY_FLOAT:  s = "float";  break;
        case JKY_STR:    s = "string"; break;
        case JKY_BYTES:  s = "bytes";  break;
        case JKY_ARRAY:  s = "array";  break;
        case JKY_DICT:   s = "dict";   break;
        case JKY_BOOL:   s = "bool";   break;
        default: break;
    }
    *out = mkstr(s);
    return BUILTIN_OK;
}

static BuiltinResult bi_supports(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkint(0); return BUILTIN_OK; }
    *out = mkint(builtin_exists(a[0].v.s) ? 1 : 0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_UTIL[] = {
    { "print",     0, -1, bi_print    },
    { "len",       1,  1, bi_len      },
    { "str",       1,  1, bi_str      },
    { "int",       1,  1, bi_int      },
    { "float",     1,  1, bi_float    },
    { "bool",      1,  1, bi_bool     },
    { "list",      0, -1, bi_list     },
    { "dict",      0, -1, bi_dict     },
    { "range",     1,  3, bi_range    },
    { "abs",       1,  1, bi_abs      },
    { "min",       1, -1, bi_min      },
    { "max",       1, -1, bi_max      },
    { "type",      1,  1, bi_type     },
    { "supports",  1,  1, bi_supports },
    { NULL, 0, 0, NULL },
};