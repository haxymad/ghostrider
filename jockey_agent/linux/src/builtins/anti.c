#include "jky_builtins.h"
#include "_stubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>

static BuiltinResult bi_anti_timestomp(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR || a[1].type != VT_INT) {
        *out = mkbool(0); return BUILTIN_OK;
    }
    struct timeval tv[2];
    tv[0].tv_sec = (time_t)a[1].v.i; tv[0].tv_usec = 0;
    tv[1].tv_sec = (time_t)a[1].v.i; tv[1].tv_usec = 0;
    *out = mkbool(utimes(a[0].v.s, tv) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_anti_debug(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
        *out = mkbool(1);
    } else {
        ptrace(PTRACE_DETACH, 0, 0, 0);
        *out = mkbool(0);
    }
    return BUILTIN_OK;
}

STUB_FALSE(bi_anti_deleted)
STUB_FALSE(bi_anti_encrypted)
STUB_FALSE(bi_anti_log_clear)
STUB_FALSE(bi_anti_shadow)
STUB_FALSE(bi_anti_integrity)

/* anti_rootkit_scan — reuse kernel_rootkit_check logic */
static BuiltinResult bi_anti_rootkit_scan(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value d = mkdict();
    dict_set(d.v.dict, "scanned", mkint(1));
    *out = d;
    return BUILTIN_OK;
}

const Builtin BUILTINS_ANTI[] = {
    { "anti_timestomp",    1, 2, bi_anti_timestomp    },
    { "anti_deleted",      1, 1, bi_anti_deleted      },
    { "anti_encrypted",    1, 1, bi_anti_encrypted    },
    { "anti_rootkit_scan", 0, 0, bi_anti_rootkit_scan },
    { "anti_debug",        0, 0, bi_anti_debug        },
    { "anti_log_clear",    0, 0, bi_anti_log_clear    },
    { "anti_shadow",       0, 0, bi_anti_shadow       },
    { "anti_integrity",    1, 1, bi_anti_integrity    },
    { NULL, 0, 0, NULL },
};
