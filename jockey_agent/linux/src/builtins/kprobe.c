#include "jky_builtins.h"
#include "jky_kernel_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct event_rec {
    uint64_t ts_ns;
    int32_t  pid;
    int32_t  id;
    uint64_t arg0, arg1, arg2, arg3;
    char     name[64];
};

static BuiltinResult bi_kprobe_add(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkint(-1); return BUILTIN_OK; }
    *out = mkint(jky_kmod_kprobe_add(a[0].v.s));
    return BUILTIN_OK;
}

static BuiltinResult bi_kprobe_remove(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_kprobe_remove((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_kprobe_list(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[4096];
    int n = jky_kmod_trace_list(buf, sizeof(buf) - 1);
    if (n < 0) { *out = mkstr(""); return BUILTIN_OK; }
    buf[n] = 0;
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_read(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    int max = (a[0].type == VT_INT) ? (int)a[0].v.i : 128;
    if (max <= 0 || max > 1024) max = 128;

    struct event_rec *evs = calloc(max, sizeof(struct event_rec));
    int n = jky_kmod_trace_read(evs, max);
    Value arr = mkarray();
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            Value d = mkdict();
            dict_set(d.v.dict, "ts_ns", mkint((int64_t)evs[i].ts_ns));
            dict_set(d.v.dict, "pid",   mkint(evs[i].pid));
            dict_set(d.v.dict, "id",    mkint(evs[i].id));
            dict_set(d.v.dict, "name",  mkstr(evs[i].name));
            dict_set(d.v.dict, "arg0",  mkint((int64_t)evs[i].arg0));
            dict_set(d.v.dict, "arg1",  mkint((int64_t)evs[i].arg1));
            dict_set(d.v.dict, "arg2",  mkint((int64_t)evs[i].arg2));
            dict_set(d.v.dict, "arg3",  mkint((int64_t)evs[i].arg3));
            arr_push(arr.v.arr, d);
        }
    }
    free(evs);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_clear(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(jky_kmod_trace_clear() == 0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_KPROBE[] = {
    { "kprobe_add",        1, 1, bi_kprobe_add    },
    { "kprobe_remove",     1, 1, bi_kprobe_remove },
    { "kprobe_list",       0, 0, bi_kprobe_list   },
    { "kprobe_events",     0, 1, bi_trace_read    },
    { "kprobe_clear",      0, 0, bi_trace_clear   },
    { NULL, 0, 0, NULL },
};
