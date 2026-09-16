#include "jky_value.h"
#include "jky_opcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* ============================================================
 * error slot
 * ============================================================ */

static char g_err[256];
static int  g_has_err = 0;

void value_clear_error(void)      { g_has_err = 0; g_err[0] = 0; }
int  value_has_error(void)        { return g_has_err; }
const char *value_get_error(void) { return g_err; }

void value_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
    g_has_err = 1;
}

/* ============================================================
 * constructors
 * ============================================================ */

Value mkint(int64_t i)  { Value v; v.type = VT_INT;   v.rc = 0; v.v.i = i; return v; }
Value mkfloat(double f) { Value v; v.type = VT_FLOAT; v.rc = 0; v.v.f = f; return v; }
Value mkbool(int b)     { return mkint(b ? 1 : 0); }
Value mknone(void)      { Value v; v.type = VT_NONE; v.rc = 0; v.v.i = 0; return v; }

Value mkstrn(const char *s, size_t n) {
    Value v;
    v.type = VT_STR;
    v.rc   = 0;
    v.v.s  = malloc(n + 1);
    memcpy(v.v.s, s, n);
    v.v.s[n] = 0;
    return v;
}

Value mkstr(const char *s) {
    if (!s) s = "";
    return mkstrn(s, strlen(s));
}

Value mkarray(void) {
    Value v;
    v.type = VT_ARRAY;
    v.rc   = 0;
    v.v.arr = calloc(1, sizeof(Array));
    v.v.arr->rc = 1;
    return v;
}

Value mkdict(void) {
    Value v;
    v.type = VT_DICT;
    v.rc   = 0;
    v.v.dict = calloc(1, sizeof(Dict));
    v.v.dict->rc = 1;
    return v;
}

/* ============================================================
 * lifetime
 * ============================================================ */

void vfree(Value *v) {
    if (!v) return;
    switch (v->type) {
        case VT_STR:
            free(v->v.s);
            break;
        case VT_ARRAY:
            if (v->v.arr) {
                if (--v->v.arr->rc <= 0) {
                    for (int i = 0; i < v->v.arr->len; i++)
                        vfree(&v->v.arr->items[i]);
                    free(v->v.arr->items);
                    free(v->v.arr);
                }
            }
            break;
        case VT_DICT:
            if (v->v.dict) {
                if (--v->v.dict->rc <= 0) {
                    DictEntry *e = v->v.dict->head;
                    while (e) {
                        DictEntry *n = e->next;
                        free(e->key);
                        if (e->value) { vfree(e->value); free(e->value); }
                        free(e);
                        e = n;
                    }
                    free(v->v.dict);
                }
            }
            break;
        case VT_BYTES:
            free(v->v.bytes.data);
            break;
        default:
            break;
    }
    v->type = VT_NONE;
    v->rc   = 0;
    v->v.i  = 0;
}

Value vcopy(Value v) {
    switch (v.type) {
        case VT_INT:   return mkint(v.v.i);
        case VT_FLOAT: return mkfloat(v.v.f);
        case VT_NONE:  return mknone();
        case VT_STR:   return mkstr(v.v.s);
        case VT_ARRAY: {
            Value out = mkarray();
            for (int i = 0; i < v.v.arr->len; i++)
                arr_push(out.v.arr, vcopy(v.v.arr->items[i]));
            return out;
        }
        case VT_DICT: {
            Value out = mkdict();
            for (DictEntry *e = v.v.dict->head; e; e = e->next)
                dict_set(out.v.dict, e->key, vcopy(*e->value));
            return out;
        }
        case VT_BYTES: {
            Value out;
            out.type = VT_BYTES;
            out.rc   = 0;
            out.v.bytes.len  = v.v.bytes.len;
            out.v.bytes.data = malloc(v.v.bytes.len ? v.v.bytes.len : 1);
            if (v.v.bytes.len)
                memcpy(out.v.bytes.data, v.v.bytes.data, v.v.bytes.len);
            return out;
        }
        default: return mknone();
    }
}

Value vshare(Value v) {
    switch (v.type) {
        case VT_ARRAY:
            if (v.v.arr) v.v.arr->rc++;
            return v;
        case VT_DICT:
            if (v.v.dict) v.v.dict->rc++;
            return v;
        case VT_STR:
            return mkstr(v.v.s ? v.v.s : "");
        case VT_BYTES: {
            Value out;
            out.type = VT_BYTES;
            out.rc   = 0;
            out.v.bytes.len  = v.v.bytes.len;
            out.v.bytes.data = malloc(v.v.bytes.len ? v.v.bytes.len : 1);
            if (v.v.bytes.len)
                memcpy(out.v.bytes.data, v.v.bytes.data, v.v.bytes.len);
            return out;
        }
        default:
            return v;
    }
}

/* ============================================================
 * containers
 * ============================================================ */

void arr_push(Array *a, Value v) {
    if (a->len >= a->cap) {
        a->cap   = a->cap ? a->cap * 2 : 4;
        a->items = realloc(a->items, a->cap * sizeof(Value));
    }
    a->items[a->len++] = v;
}

void dict_set(Dict *d, const char *key, Value v) {
    for (DictEntry *e = d->head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            vfree(e->value);
            *e->value = v;
            return;
        }
    }
    DictEntry *ne = malloc(sizeof(DictEntry));
    ne->key    = strdup(key);
    ne->value  = malloc(sizeof(Value));
    *ne->value = v;
    ne->next   = d->head;
    d->head    = ne;
}

Value dict_get(Dict *d, const char *key) {
    for (DictEntry *e = d->head; e; e = e->next)
        if (strcmp(e->key, key) == 0)
            return vshare(*e->value);
    return mknone();
}

/* ============================================================
 * semantic helpers
 * ============================================================ */

static int is_num(Value v) {
    return v.type == VT_INT || v.type == VT_FLOAT;
}
static double as_double(Value v) {
    return v.type == VT_FLOAT ? v.v.f : (double)v.v.i;
}

int value_truthy(Value v) {
    switch (v.type) {
        case VT_NONE:  return 0;
        case VT_INT:   return v.v.i != 0;
        case VT_FLOAT: return v.v.f != 0.0;
        case VT_STR:   return v.v.s && v.v.s[0] != 0;
        case VT_ARRAY: return v.v.arr && v.v.arr->len > 0;
        case VT_DICT:  return v.v.dict && v.v.dict->head != NULL;
        case VT_BYTES: return v.v.bytes.len > 0;
        default:       return 1;
    }
}

char *value_to_str(Value v) {
    char buf[64];
    switch (v.type) {
        case VT_NONE:  return strdup("none");
        case VT_INT:
            snprintf(buf, sizeof(buf), "%lld", (long long)v.v.i);
            return strdup(buf);
        case VT_FLOAT: {
            double f = v.v.f;
            if (f == (double)(int64_t)f && fabs(f) < 1e16)
                snprintf(buf, sizeof(buf), "%.1f", f);
            else
                snprintf(buf, sizeof(buf), "%g", f);
            return strdup(buf);
        }
        case VT_STR:
            return strdup(v.v.s ? v.v.s : "");
        case VT_ARRAY: {
            size_t cap = 32, n = 0;
            char  *b   = malloc(cap);
            b[n++] = '[';
            for (int i = 0; i < v.v.arr->len; i++) {
                char  *e  = value_to_str(v.v.arr->items[i]);
                size_t el = strlen(e);
                while (n + el + 4 > cap) { cap *= 2; b = realloc(b, cap); }
                if (i) { b[n++] = ','; b[n++] = ' '; }
                memcpy(b + n, e, el); n += el;
                free(e);
            }
            while (n + 2 > cap) { cap *= 2; b = realloc(b, cap); }
            b[n++] = ']'; b[n] = 0;
            return b;
        }
        case VT_DICT: {
            size_t cap = 32, n = 0;
            char  *b   = malloc(cap);
            b[n++] = '{';
            int first = 1;
            for (DictEntry *e = v.v.dict->head; e; e = e->next) {
                char  *val  = value_to_str(*e->value);
                size_t need = strlen(e->key) + strlen(val) + 8;
                while (n + need > cap) { cap *= 2; b = realloc(b, cap); }
                if (!first) { b[n++] = ','; b[n++] = ' '; }
                b[n++] = '\'';
                memcpy(b + n, e->key, strlen(e->key)); n += strlen(e->key);
                b[n++] = '\''; b[n++] = ':'; b[n++] = ' ';
                memcpy(b + n, val, strlen(val)); n += strlen(val);
                free(val);
                first = 0;
            }
            while (n + 2 > cap) { cap *= 2; b = realloc(b, cap); }
            b[n++] = '}'; b[n] = 0;
            return b;
        }
        case VT_BYTES: {
            size_t cap = v.v.bytes.len * 4 + 16, n = 0;
            char  *b   = malloc(cap);
            n += snprintf(b + n, cap - n, "b\"");
            for (size_t i = 0; i < v.v.bytes.len; i++)
                n += snprintf(b + n, cap - n, "\\x%02x", v.v.bytes.data[i]);
            snprintf(b + n, cap - n, "\"");
            return b;
        }
        default: return strdup("?");
    }
}

/* ============================================================
 * arithmetic
 * ============================================================ */

Value value_add(Value a, Value b) {
    if (is_num(a) && is_num(b)) {
        if (a.type == VT_INT && b.type == VT_INT) return mkint(a.v.i + b.v.i);
        return mkfloat(as_double(a) + as_double(b));
    }
    if (a.type == VT_STR || b.type == VT_STR) {
        char  *sa = value_to_str(a);
        char  *sb = value_to_str(b);
        size_t la = strlen(sa), lb = strlen(sb);
        char  *r  = malloc(la + lb + 1);
        memcpy(r, sa, la);
        memcpy(r + la, sb, lb);
        r[la + lb] = 0;
        Value out = mkstr(r);
        free(sa); free(sb); free(r);
        return out;
    }
    value_set_error("cannot add type %d and type %d", a.type, b.type);
    return mknone();
}

Value value_sub(Value a, Value b) {
    if (a.type == VT_INT && b.type == VT_INT) return mkint(a.v.i - b.v.i);
    if (is_num(a) && is_num(b)) return mkfloat(as_double(a) - as_double(b));
    value_set_error("subtraction requires numeric operands");
    return mknone();
}

Value value_mul(Value a, Value b) {
    if (a.type == VT_INT && b.type == VT_INT) return mkint(a.v.i * b.v.i);
    if (is_num(a) && is_num(b)) return mkfloat(as_double(a) * as_double(b));
    value_set_error("multiplication requires numeric operands");
    return mknone();
}

Value value_div(Value a, Value b) {
    if (!is_num(a) || !is_num(b)) {
        value_set_error("division requires numeric operands");
        return mknone();
    }
    double db = as_double(b);
    if (db == 0.0) { value_set_error("division by zero"); return mknone(); }
    return mkfloat(as_double(a) / db);
}

Value value_mod(Value a, Value b) {
    if (a.type == VT_INT && b.type == VT_INT) {
        if (b.v.i == 0) { value_set_error("modulo by zero"); return mknone(); }
        int64_t q = a.v.i / b.v.i;
        return mkint(a.v.i - q * b.v.i);
    }
    if (is_num(a) && is_num(b)) {
        double db = as_double(b);
        if (db == 0.0) { value_set_error("modulo by zero"); return mknone(); }
        return mkfloat(fmod(as_double(a), db));
    }
    value_set_error("modulo requires numeric operands");
    return mknone();
}

/* ============================================================
 * comparison
 * ============================================================ */

Value value_eq(Value a, Value b) {
    if (is_num(a) && is_num(b))
        return mkbool(as_double(a) == as_double(b));
    if (a.type == VT_NONE || b.type == VT_NONE)
        return mkbool(a.type == VT_NONE && b.type == VT_NONE);
    if (a.type == VT_STR && b.type == VT_STR)
        return mkbool(strcmp(a.v.s, b.v.s) == 0);
    if (a.type == VT_ARRAY && b.type == VT_ARRAY)
        return mkbool(a.v.arr == b.v.arr);
    if (a.type == VT_DICT && b.type == VT_DICT)
        return mkbool(a.v.dict == b.v.dict);
    return mkbool(0);
}

Value value_ne(Value a, Value b) {
    Value e = value_eq(a, b);
    return mkbool(!e.v.i);
}

Value value_cmp(Value a, Value b, int op) {
    if (is_num(a) && is_num(b)) {
        double da = as_double(a), db = as_double(b);
        switch (op) {
            case OP_LT: return mkbool(da <  db);
            case OP_GT: return mkbool(da >  db);
            case OP_LE: return mkbool(da <= db);
            case OP_GE: return mkbool(da >= db);
        }
    }
    if (a.type == VT_STR && b.type == VT_STR) {
        int c = strcmp(a.v.s, b.v.s);
        switch (op) {
            case OP_LT: return mkbool(c <  0);
            case OP_GT: return mkbool(c >  0);
            case OP_LE: return mkbool(c <= 0);
            case OP_GE: return mkbool(c >= 0);
        }
    }
    value_set_error("cannot compare type %d and type %d", a.type, b.type);
    return mknone();
}

Value value_not(Value v) { return mkbool(!value_truthy(v)); }

Value value_neg(Value v) {
    if (v.type == VT_INT)   return mkint(-v.v.i);
    if (v.type == VT_FLOAT) return mkfloat(-v.v.f);
    value_set_error("unary minus requires numeric operand");
    return mknone();
}

Value value_bitop(Value a, Value b, int op) {
    if (a.type != VT_INT || b.type != VT_INT) {
        value_set_error("bitwise operators require int operands");
        return mknone();
    }
    switch (op) {
        case OP_BIT_OR:  return mkint(a.v.i | b.v.i);
        case OP_BIT_AND: return mkint(a.v.i & b.v.i);
        case OP_BIT_XOR: return mkint(a.v.i ^ b.v.i);
    }
    value_set_error("unknown bitop");
    return mknone();
}

/* ============================================================
 * len / index / set_index
 * ============================================================ */

Value value_len(Value v) {
    switch (v.type) {
        case VT_STR:
            return mkint(v.v.s ? (int64_t)strlen(v.v.s) : 0);
        case VT_ARRAY:
            return mkint(v.v.arr ? v.v.arr->len : 0);
        case VT_DICT: {
            int64_t n = 0;
            if (v.v.dict) {
                for (DictEntry *e = v.v.dict->head; e; e = e->next) n++;
            }
            return mkint(n);
        }
        case VT_BYTES:
            return mkint((int64_t)v.v.bytes.len);
        default:
            return mkint(0);
    }
}

Value value_index(Value obj, Value idx) {
    if (obj.type == VT_ARRAY && idx.type == VT_INT) {
        int64_t i = idx.v.i;
        if (i < 0 || i >= obj.v.arr->len) return mknone();
        return vshare(obj.v.arr->items[i]);
    }
    if (obj.type == VT_STR && idx.type == VT_INT) {
        int64_t i = idx.v.i;
        size_t n  = strlen(obj.v.s);
        if (i < 0 || (size_t)i >= n) return mknone();
        char b[2] = { obj.v.s[i], 0 };
        return mkstr(b);
    }
    if (obj.type == VT_BYTES && idx.type == VT_INT) {
        int64_t i = idx.v.i;
        if (i < 0 || (size_t)i >= obj.v.bytes.len) return mknone();
        return mkint(obj.v.bytes.data[i]);
    }
    if (obj.type == VT_DICT && idx.type == VT_STR) {
        return dict_get(obj.v.dict, idx.v.s);
    }
    return mknone();
}

int value_set_index(Value obj, Value idx, Value val) {
    if (obj.type == VT_ARRAY && idx.type == VT_INT) {
        int64_t i = idx.v.i;
        if (i < 0 || i >= obj.v.arr->len) return 0;
        vfree(&obj.v.arr->items[i]);
        obj.v.arr->items[i] = val;
        return 0;
    }
    if (obj.type == VT_DICT && idx.type == VT_STR) {
        dict_set(obj.v.dict, idx.v.s, val);
        return 0;
    }
    value_set_error("cannot assign into type %d", obj.type);
    return -1;
}
