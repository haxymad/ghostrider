#include "jky_builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================
 * print
 * ============================================================ */

static BuiltinResult bi_print(VM *vm, Value *args, int argc, Value *out) {
    FILE *f = vm->out ? vm->out : stdout;
    for (int i = 0; i < argc; i++) {
        if (i) fputc(' ', f);
        char *s = value_to_str(args[i]);
        fputs(s, f);
        free(s);
    }
    fputc('\n', f);
    fflush(f);
    *out = mknone();
    return BUILTIN_OK;
}

/* ============================================================
 * len
 * ============================================================ */

static BuiltinResult bi_len(VM *vm, Value *args, int argc, Value *out) {
    (void)vm; (void)argc;
    *out = value_len(args[0]);
    return BUILTIN_OK;
}

/* ============================================================
 * str
 * ============================================================ */

static BuiltinResult bi_str(VM *vm, Value *args, int argc, Value *out) {
    (void)vm; (void)argc;
    char *s = value_to_str(args[0]);
    *out = mkstr(s);
    free(s);
    return BUILTIN_OK;
}

/* ============================================================
 * int
 * ============================================================ */

static BuiltinResult bi_int(VM *vm, Value *args, int argc, Value *out) {
    (void)vm; (void)argc;
    Value v = args[0];

    if (v.type == VT_INT) {
        *out = mkint(v.v.i);
        return BUILTIN_OK;
    }
    if (v.type == VT_FLOAT) {
        *out = mkint((int64_t)v.v.f);
        return BUILTIN_OK;
    }
    if (v.type == VT_STR) {
        const char *s = v.v.s;
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        if (*s == 0) {
            *out = mkint(0);
            return BUILTIN_OK;
        }
        char *end;
        long long x = strtoll(s, &end, 10);
        if (*end == 0) {
            *out = mkint(x);
            return BUILTIN_OK;
        }
        double d = strtod(s, &end);
        if (*end == 0) {
            *out = mkint((int64_t)d);
            return BUILTIN_OK;
        }
        value_set_error("cannot convert %s to int", v.v.s);
        return BUILTIN_ERROR;
    }
    value_set_error("cannot convert to int");
    return BUILTIN_ERROR;
}

/* ============================================================
 * float
 * ============================================================ */

static BuiltinResult bi_float(VM *vm, Value *args, int argc, Value *out) {
    (void)vm; (void)argc;
    Value v = args[0];

    if (v.type == VT_FLOAT) {
        *out = mkfloat(v.v.f);
        return BUILTIN_OK;
    }
    if (v.type == VT_INT) {
        *out = mkfloat((double)v.v.i);
        return BUILTIN_OK;
    }
    if (v.type == VT_STR) {
        char *end;
        double d = strtod(v.v.s, &end);
        if (*end == 0) {
            *out = mkfloat(d);
            return BUILTIN_OK;
        }
        value_set_error("cannot convert %s to float", v.v.s);
        return BUILTIN_ERROR;
    }
    value_set_error("cannot convert to float");
    return BUILTIN_ERROR;
}

/* ============================================================
 * bool
 * ============================================================ */

static BuiltinResult bi_bool(VM *vm, Value *args, int argc, Value *out) {
    (void)vm; (void)argc;
    *out = mkbool(value_truthy(args[0]));
    return BUILTIN_OK;
}

/* ============================================================
 * list
 * ============================================================ */

static BuiltinResult bi_list(VM *vm, Value *args, int argc, Value *out) {
    (void)vm;

    if (argc == 1 && args[0].type == VT_ARRAY) {
        *out = vcopy(args[0]);
        return BUILTIN_OK;
    }
    if (argc == 1 && args[0].type == VT_STR) {
        Value arr = mkarray();
        for (size_t i = 0; args[0].v.s[i]; i++) {
            char b[2] = { args[0].v.s[i], 0 };
            arr_push(arr.v.arr, mkstr(b));
        }
        *out = arr;
        return BUILTIN_OK;
    }

    Value arr = mkarray();
    for (int i = 0; i < argc; i++) arr_push(arr.v.arr, vcopy(args[i]));
    *out = arr;
    return BUILTIN_OK;
}

/* ============================================================
 * dict
 * ============================================================ */

static BuiltinResult bi_dict(VM *vm, Value *args, int argc, Value *out) {
    (void)vm;

    if (argc == 1 && args[0].type == VT_DICT) {
        *out = vcopy(args[0]);
        return BUILTIN_OK;
    }
    *out = mkdict();
    return BUILTIN_OK;
}

/* ============================================================
 * range
 * ============================================================ */

static BuiltinResult bi_range(VM *vm, Value *args, int argc, Value *out) {
    (void)vm;

    int64_t start = 0, stop = 0, step = 1;

    if (argc == 1) {
        if (args[0].type != VT_INT) {
            value_set_error("range: int required");
            return BUILTIN_ERROR;
        }
        stop = args[0].v.i;
    } else if (argc == 2) {
        if (args[0].type != VT_INT || args[1].type != VT_INT) {
            value_set_error("range: ints required");
            return BUILTIN_ERROR;
        }
        start = args[0].v.i;
        stop  = args[1].v.i;
    } else if (argc == 3) {
        if (args[0].type != VT_INT ||
            args[1].type != VT_INT ||
            args[2].type != VT_INT) {
            value_set_error("range: ints required");
        return BUILTIN_ERROR;
            }
            start = args[0].v.i;
            stop  = args[1].v.i;
            step  = args[2].v.i;
    } else {
        *out = mkarray();
        return BUILTIN_OK;
    }

    if (step == 0) {
        value_set_error("range: step cannot be 0");
        return BUILTIN_ERROR;
    }

    Value arr = mkarray();
    if (step > 0) {
        for (int64_t i = start; i < stop; i += step)
            arr_push(arr.v.arr, mkint(i));
    } else {
        for (int64_t i = start; i > stop; i += step)
            arr_push(arr.v.arr, mkint(i));
    }
    *out = arr;
    return BUILTIN_OK;
}

/* ============================================================
 * abs
 * ============================================================ */

static BuiltinResult bi_abs(VM *vm, Value *args, int argc, Value *out) {
    (void)vm; (void)argc;

    if (args[0].type == VT_INT) {
        int64_t x = args[0].v.i;
        *out = mkint(x < 0 ? -x : x);
        return BUILTIN_OK;
    }
    if (args[0].type == VT_FLOAT) {
        double f = args[0].v.f;
        *out = mkfloat(f < 0 ? -f : f);
        return BUILTIN_OK;
    }
    value_set_error("abs requires numeric operand");
    return BUILTIN_ERROR;
}

/* ============================================================
 * min / max — shared body
 * ============================================================ */

static BuiltinResult minmax(VM *vm, Value *args, int argc, Value *out, int want_max) {
    (void)vm;

    int64_t acc = 0;
    int first = 1;

    if (argc == 1 && args[0].type == VT_ARRAY) {
        if (args[0].v.arr->len == 0) {
            value_set_error("empty array");
            return BUILTIN_ERROR;
        }
        for (int i = 0; i < args[0].v.arr->len; i++) {
            Value e = args[0].v.arr->items[i];
            if (e.type != VT_INT) {
                value_set_error("ints required");
                return BUILTIN_ERROR;
            }
            if (first || (want_max ? e.v.i > acc : e.v.i < acc)) acc = e.v.i;
            first = 0;
        }
    } else {
        for (int i = 0; i < argc; i++) {
            if (args[i].type != VT_INT) {
                value_set_error("ints required");
                return BUILTIN_ERROR;
            }
            if (first || (want_max ? args[i].v.i > acc : args[i].v.i < acc))
                acc = args[i].v.i;
            first = 0;
        }
    }

    *out = mkint(acc);
    return BUILTIN_OK;
}

static BuiltinResult bi_min(VM *vm, Value *args, int argc, Value *out) {
    return minmax(vm, args, argc, out, 0);
}

static BuiltinResult bi_max(VM *vm, Value *args, int argc, Value *out) {
    return minmax(vm, args, argc, out, 1);
}

/* ============================================================
 * type
 * ============================================================ */

static BuiltinResult bi_type(VM *vm, Value *args, int argc, Value *out) {
    (void)vm; (void)argc;

    const char *n = "unknown";
    switch (args[0].type) {
        case VT_INT:   n = "int";    break;
        case VT_FLOAT: n = "float";  break;
        case VT_STR:   n = "string"; break;
        case VT_NONE:  n = "none";   break;
        case VT_ARRAY: n = "array";  break;
        case VT_DICT:  n = "dict";   break;
        case VT_BYTES: n = "bytes";  break;
        default:       n = "unknown"; break;
    }
    *out = mkstr(n);
    return BUILTIN_OK;
}

/* ============================================================
 * supports
 * ============================================================ */

static BuiltinResult bi_supports(VM *vm, Value *args, int argc, Value *out) {
    (void)vm; (void)argc;

    if (args[0].type != VT_STR) {
        *out = mkbool(0);
        return BUILTIN_OK;
    }
    *out = mkbool(builtin_exists(args[0].v.s));
    return BUILTIN_OK;
}

/* ============================================================
 * table — NULL-terminated sentinel
 * ============================================================ */

const Builtin BUILTINS_PURE[] = {
    { "print",    0, -1, bi_print    },
    { "len",      1,  1, bi_len      },
    { "str",      1,  1, bi_str      },
    { "int",      1,  1, bi_int      },
    { "float",    1,  1, bi_float    },
    { "bool",     1,  1, bi_bool     },
    { "list",     0, -1, bi_list     },
    { "dict",     0, -1, bi_dict     },
    { "range",    0,  3, bi_range    },
    { "abs",      1,  1, bi_abs      },
    { "min",      1, -1, bi_min      },
    { "max",      1, -1, bi_max      },
    { "type",     1,  1, bi_type     },
    { "supports", 1,  1, bi_supports },
    { NULL,       0,  0, NULL        },
};
