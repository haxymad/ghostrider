#include "jky_builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdint.h>

#ifdef __x86_64__
typedef struct user_regs_struct regs_t;
#define REG_IP( r) ((r).rip)
#define REG_SP( r) ((r).rsp)
#define REG_AX( r) ((r).rax)
#define REG_DI( r) ((r).rdi)
#define REG_SI( r) ((r).rsi)
#define REG_DX( r) ((r).rdx)
#define REG_CX( r) ((r).rcx)
#define REG_ORIG_AX(r) ((r).orig_rax)
#else
#error "ptrace.c currently supports x86_64 only"
#endif

/* -------- attach / detach -------- */

static BuiltinResult bi_attach(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    pid_t pid = (pid_t)a[0].v.i;
    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) { *out = mkbool(0); return BUILTIN_OK; }
    int status;
    waitpid(pid, &status, 0);
    *out = mkbool(1);
    return BUILTIN_OK;
}

static BuiltinResult bi_detach(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(ptrace(PTRACE_DETACH, (pid_t)a[0].v.i, 0, 0) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_cont(VM *vm, Value *a, int c, Value *out) {
    (void)vm;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    long sig = (c >= 2 && a[1].type == VT_INT) ? (long)a[1].v.i : 0;
    *out = mkbool(ptrace(PTRACE_CONT, (pid_t)a[0].v.i, 0, sig) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_singlestep(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(ptrace(PTRACE_SINGLESTEP, (pid_t)a[0].v.i, 0, 0) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_syscall(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(ptrace(PTRACE_SYSCALL, (pid_t)a[0].v.i, 0, 0) == 0);
    return BUILTIN_OK;
}

/* -------- wait -------- */

static BuiltinResult bi_wait(VM *vm, Value *a, int c, Value *out) {
    (void)vm;
    if (a[0].type != VT_INT) { *out = mknone(); return BUILTIN_OK; }
    pid_t pid = (pid_t)a[0].v.i;
    int flags = (c >= 2 && a[1].type == VT_INT && a[1].v.i) ? WNOHANG : 0;
    int status = 0;
    pid_t r = waitpid(pid, &status, flags);
    if (r <= 0) { *out = mknone(); return BUILTIN_OK; }

    Value d = mkdict();
    dict_set(d.v.dict, "pid", mkint(r));
    if (WIFSTOPPED(status)) {
        dict_set(d.v.dict, "stopped", mkbool(1));
        dict_set(d.v.dict, "signal",  mkint(WSTOPSIG(status)));
        dict_set(d.v.dict, "event",   mkint(status >> 16));
    } else if (WIFEXITED(status)) {
        dict_set(d.v.dict, "exited", mkbool(1));
        dict_set(d.v.dict, "code",   mkint(WEXITSTATUS(status)));
    } else if (WIFSIGNALED(status)) {
        dict_set(d.v.dict, "signaled", mkbool(1));
        dict_set(d.v.dict, "signal",   mkint(WTERMSIG(status)));
    }
    *out = d;
    return BUILTIN_OK;
}

/* -------- registers -------- */

static BuiltinResult bi_read_regs(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkdict(); return BUILTIN_OK; }
    regs_t r;
    if (ptrace(PTRACE_GETREGS, (pid_t)a[0].v.i, 0, &r) < 0) {
        *out = mkdict();
        return BUILTIN_OK;
    }
    Value d = mkdict();
    dict_set(d.v.dict, "rax", mkint(r.rax));
    dict_set(d.v.dict, "rbx", mkint(r.rbx));
    dict_set(d.v.dict, "rcx", mkint(r.rcx));
    dict_set(d.v.dict, "rdx", mkint(r.rdx));
    dict_set(d.v.dict, "rsi", mkint(r.rsi));
    dict_set(d.v.dict, "rdi", mkint(r.rdi));
    dict_set(d.v.dict, "rbp", mkint(r.rbp));
    dict_set(d.v.dict, "rsp", mkint(r.rsp));
    dict_set(d.v.dict, "r8",  mkint(r.r8));
    dict_set(d.v.dict, "r9",  mkint(r.r9));
    dict_set(d.v.dict, "r10", mkint(r.r10));
    dict_set(d.v.dict, "r11", mkint(r.r11));
    dict_set(d.v.dict, "r12", mkint(r.r12));
    dict_set(d.v.dict, "r13", mkint(r.r13));
    dict_set(d.v.dict, "r14", mkint(r.r14));
    dict_set(d.v.dict, "r15", mkint(r.r15));
    dict_set(d.v.dict, "rip", mkint(r.rip));
    dict_set(d.v.dict, "eflags", mkint(r.eflags));
    dict_set(d.v.dict, "orig_rax", mkint(r.orig_rax));
    *out = d;
    return BUILTIN_OK;
}

static int dict_int(Value d, const char *k, uint64_t *outv, int *found) {
    Value v = dict_get(d.v.dict, k);
    if (v.type == VT_NONE) { *found = 0; return 0; }
    if (v.type != VT_INT) { return -1; }
    *outv = (uint64_t)v.v.i;
    *found = 1;
    return 0;
}

static BuiltinResult bi_write_regs(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT || a[1].type != VT_DICT) {
        *out = mkbool(0); return BUILTIN_OK;
    }
    pid_t pid = (pid_t)a[0].v.i;
    regs_t r;
    if (ptrace(PTRACE_GETREGS, pid, 0, &r) < 0) { *out = mkbool(0); return BUILTIN_OK; }

    Value d = a[1];
    uint64_t v; int found;
    #define SETF(field, key) \
        if (dict_int(d, key, &v, &found) == 0 && found) r.field = v
    SETF(rax, "rax"); SETF(rbx, "rbx"); SETF(rcx, "rcx"); SETF(rdx, "rdx");
    SETF(rsi, "rsi"); SETF(rdi, "rdi"); SETF(rbp, "rbp"); SETF(rsp, "rsp");
    SETF(r8,  "r8");  SETF(r9,  "r9");  SETF(r10, "r10"); SETF(r11, "r11");
    SETF(r12, "r12"); SETF(r13, "r13"); SETF(r14, "r14"); SETF(r15, "r15");
    SETF(rip, "rip"); SETF(eflags, "eflags");
    #undef SETF

    *out = mkbool(ptrace(PTRACE_SETREGS, pid, 0, &r) == 0);
    return BUILTIN_OK;
}

/* -------- memory via ptrace -------- */

static int ptrace_read(pid_t pid, uint64_t addr, uint8_t *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid, addr + i, 0);
        if (errno != 0) return -1;
        size_t take = len - i < sizeof(word) ? len - i : sizeof(word);
        memcpy(buf + i, &word, take);
        i += take;
    }
    return 0;
}

static int ptrace_write(pid_t pid, uint64_t addr, const uint8_t *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t take = len - i < sizeof(long) ? len - i : sizeof(long);
        long word = 0;
        if (take < sizeof(long)) {
            errno = 0;
            word = ptrace(PTRACE_PEEKDATA, pid, addr + i, 0);
            if (errno != 0) return -1;
        }
        memcpy(&word, buf + i, take);
        if (ptrace(PTRACE_POKEDATA, pid, addr + i, word) < 0) return -1;
        i += take;
    }
    return 0;
}

static BuiltinResult bi_read_mem(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT || a[1].type != VT_INT || a[2].type != VT_INT) {
        Value v; v.type = VT_BYTES; v.rc = 0;
        v.v.bytes.data = malloc(1); v.v.bytes.len = 0;
        *out = v;
        return BUILTIN_OK;
    }
    size_t len = (size_t)a[2].v.i;
    if (len > 16 * 1024 * 1024) len = 16 * 1024 * 1024;

    uint8_t *buf = malloc(len ? len : 1);
    if (ptrace_read((pid_t)a[0].v.i, (uint64_t)a[1].v.i, buf, len) < 0) {
        free(buf);
        buf = malloc(1);
        len = 0;
    }
    Value v; v.type = VT_BYTES; v.rc = 0;
    v.v.bytes.data = buf; v.v.bytes.len = len;
    *out = v;
    return BUILTIN_OK;
}

static BuiltinResult bi_write_mem(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT || a[1].type != VT_INT) {
        *out = mkint(-1); return BUILTIN_OK;
    }
    const uint8_t *data = NULL; size_t len = 0;
    if (a[2].type == VT_BYTES) { data = a[2].v.bytes.data; len = a[2].v.bytes.len; }
    else if (a[2].type == VT_STR) { data = (const uint8_t *)a[2].v.s; len = strlen(a[2].v.s); }
    else { *out = mkint(-1); return BUILTIN_OK; }

    *out = mkint(ptrace_write((pid_t)a[0].v.i, (uint64_t)a[1].v.i, data, len) == 0
                 ? (int64_t)len : -1);
    return BUILTIN_OK;
}

/* -------- breakpoints -------- */

static BuiltinResult bi_set_bp(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT || a[1].type != VT_INT) {
        *out = mkint(-1); return BUILTIN_OK;
    }
    pid_t pid = (pid_t)a[0].v.i;
    uint64_t addr = (uint64_t)a[1].v.i;

    errno = 0;
    long orig = ptrace(PTRACE_PEEKDATA, pid, addr, 0);
    if (errno != 0) { *out = mkint(-1); return BUILTIN_OK; }

    long patched = (orig & ~0xffL) | 0xccL;
    if (ptrace(PTRACE_POKEDATA, pid, addr, patched) < 0) {
        *out = mkint(-1); return BUILTIN_OK;
    }
    *out = mkint(orig & 0xff);
    return BUILTIN_OK;
}

static BuiltinResult bi_clear_bp(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT || a[1].type != VT_INT || a[2].type != VT_INT) {
        *out = mkbool(0); return BUILTIN_OK;
    }
    pid_t pid = (pid_t)a[0].v.i;
    uint64_t addr = (uint64_t)a[1].v.i;
    long orig_byte = a[2].v.i & 0xff;

    errno = 0;
    long cur = ptrace(PTRACE_PEEKDATA, pid, addr, 0);
    if (errno != 0) { *out = mkbool(0); return BUILTIN_OK; }
    long patched = (cur & ~0xffL) | orig_byte;
    *out = mkbool(ptrace(PTRACE_POKEDATA, pid, addr, patched) == 0);
    return BUILTIN_OK;
}

/* -------- syscall info -------- */

static BuiltinResult bi_get_syscall(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkdict(); return BUILTIN_OK; }
    regs_t r;
    if (ptrace(PTRACE_GETREGS, (pid_t)a[0].v.i, 0, &r) < 0) {
        *out = mkdict(); return BUILTIN_OK;
    }
    Value d = mkdict();
    dict_set(d.v.dict, "nr",  mkint((int64_t)r.orig_rax));
    dict_set(d.v.dict, "arg0", mkint(r.rdi));
    dict_set(d.v.dict, "arg1", mkint(r.rsi));
    dict_set(d.v.dict, "arg2", mkint(r.rdx));
    dict_set(d.v.dict, "arg3", mkint(r.r10));
    dict_set(d.v.dict, "arg4", mkint(r.r8));
    dict_set(d.v.dict, "arg5", mkint(r.r9));
    dict_set(d.v.dict, "ret",  mkint(r.rax));
    *out = d;
    return BUILTIN_OK;
}

/* -------- table -------- */

const Builtin BUILTINS_PTRACE[] = {
    { "ptrace_attach",     1, 1, bi_attach       },
    { "ptrace_detach",     1, 1, bi_detach       },
    { "ptrace_continue",   1, 2, bi_cont         },
    { "ptrace_singlestep", 1, 1, bi_singlestep   },
    { "ptrace_syscall",    1, 1, bi_syscall      },
    { "ptrace_wait",       1, 2, bi_wait         },
    { "ptrace_read_regs",  1, 1, bi_read_regs    },
    { "ptrace_write_regs", 2, 2, bi_write_regs   },
    { "ptrace_read_mem",   3, 3, bi_read_mem     },
    { "ptrace_write_mem",  3, 3, bi_write_mem    },
    { "ptrace_set_bp",     2, 2, bi_set_bp       },
    { "ptrace_clear_bp",   3, 3, bi_clear_bp     },
    { "ptrace_get_syscall",1, 1, bi_get_syscall  },
    { NULL, 0, 0, NULL },
};
