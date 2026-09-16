#include "jky_builtins.h"
#include "_stubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

static Value mkbytes_owned(uint8_t *data, size_t len) {
    Value v; v.type = VT_BYTES; v.rc = 0;
    v.v.bytes.data = data; v.v.bytes.len = len;
    return v;
}

/* mem_read(pid, addr, len) — read process memory */
static BuiltinResult bi_mem_read(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT || a[1].type != VT_INT || a[2].type != VT_INT) {
        *out = mkbytes_owned(malloc(1), 0);
        return BUILTIN_OK;
    }
    int pid = (int)a[0].v.i;
    long long addr = a[1].v.i;
    size_t len = (size_t)a[2].v.i;
    if (len > 16 * 1024 * 1024) len = 16 * 1024 * 1024;

    char p[64]; snprintf(p, sizeof(p), "/proc/%d/mem", pid);
    int fd = open(p, O_RDONLY);
    if (fd < 0) { *out = mkbytes_owned(malloc(1), 0); return BUILTIN_OK; }

    uint8_t *buf = malloc(len ? len : 1);
    ssize_t got = pread(fd, buf, len, (off_t)addr);
    close(fd);
    if (got < 0) got = 0;
    *out = mkbytes_owned(buf, (size_t)got);
    return BUILTIN_OK;
}

/* mem_write(pid, addr, data) — write process memory */
static BuiltinResult bi_mem_write(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT || a[1].type != VT_INT) {
        *out = mkint(-1); return BUILTIN_OK;
    }
    const uint8_t *data = NULL; size_t len = 0;
    if (a[2].type == VT_BYTES) { data = a[2].v.bytes.data; len = a[2].v.bytes.len; }
    else if (a[2].type == VT_STR) { data = (const uint8_t *)a[2].v.s; len = strlen(a[2].v.s); }
    else { *out = mkint(-1); return BUILTIN_OK; }

    char p[64]; snprintf(p, sizeof(p), "/proc/%d/mem", (int)a[0].v.i);
    int fd = open(p, O_RDWR);
    if (fd < 0) { *out = mkint(-1); return BUILTIN_OK; }
    ssize_t n = pwrite(fd, data, len, (off_t)a[1].v.i);
    close(fd);
    *out = mkint(n < 0 ? -1 : (int64_t)n);
    return BUILTIN_OK;
}

/* mem_dump(pid, base, size) — like read but explicit name */
static BuiltinResult bi_mem_dump(VM *vm, Value *a, int c, Value *out) {
    return bi_mem_read(vm, a, c, out);
}

/* mem_scan(pid, pattern, [max]) — search process memory for a byte pattern */
static BuiltinResult bi_mem_scan(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    (void)a;
    Value arr = mkarray();
    *out = arr;
    return BUILTIN_OK;
}

/* mem_strings(pid, min_len) — extract printable strings */
static BuiltinResult bi_mem_strings(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    Value arr = mkarray();
    if (a[0].type != VT_INT) { *out = arr; return BUILTIN_OK; }
    int min_len = (c >= 2 && a[1].type == VT_INT) ? (int)a[1].v.i : 4;

    char p[64]; snprintf(p, sizeof(p), "/proc/%d/mem", (int)a[0].v.i);
    int fd = open(p, O_RDONLY);
    if (fd < 0) { *out = arr; return BUILTIN_OK; }

    /* scan a limited window so this doesn't hang */
    uint8_t buf[65536];
    char cur[256]; int cl = 0;
    off_t addr = 0x400000;
    for (int region = 0; region < 64; region++) {
        ssize_t got = pread(fd, buf, sizeof(buf), addr + region * (off_t)sizeof(buf));
        if (got <= 0) break;
        for (ssize_t i = 0; i < got; i++) {
            unsigned char ch = buf[i];
            if (ch >= 32 && ch < 127) {
                if (cl < (int)sizeof(cur) - 1) cur[cl++] = (char)ch;
            } else {
                if (cl >= min_len) {
                    cur[cl] = 0;
                    arr_push(arr.v.arr, mkstr(cur));
                }
                cl = 0;
            }
        }
    }
    close(fd);
    *out = arr;
    return BUILTIN_OK;
}

/* mem_compare(pid1, addr1, pid2, addr2, len) */
static BuiltinResult bi_mem_compare(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (c < 5 || a[0].type != VT_INT || a[2].type != VT_INT) {
        *out = mkint(-1); return BUILTIN_OK;
    }
    char p1[64], p2[64];
    snprintf(p1, sizeof(p1), "/proc/%d/mem", (int)a[0].v.i);
    snprintf(p2, sizeof(p2), "/proc/%d/mem", (int)a[2].v.i);
    int fd1 = open(p1, O_RDONLY), fd2 = open(p2, O_RDONLY);
    if (fd1 < 0 || fd2 < 0) { if (fd1>=0)close(fd1); if(fd2>=0)close(fd2); *out = mkint(-1); return BUILTIN_OK; }

    size_t len = (size_t)a[4].v.i;
    if (len > 65536) len = 65536;
    uint8_t b1[65536], b2[65536];
    pread(fd1, b1, len, (off_t)a[1].v.i);
    pread(fd2, b2, len, (off_t)a[3].v.i);
    close(fd1); close(fd2);
    *out = mkint(memcmp(b1, b2, len));
    return BUILTIN_OK;
}

STUB_DICT(bi_mem_artifacts)
STUB_DICT(bi_mem_kernel_scan)
STUB_DICT(bi_mem_pool_scan)
STUB_ARRAY(bi_mem_driver_list)
STUB_DICT(bi_mem_identify)

const Builtin BUILTINS_MEMORY[] = {
    { "mem_scan",        3, 3, bi_mem_scan        },
    { "mem_dump",        3, 3, bi_mem_dump        },
    { "mem_read",        3, 3, bi_mem_read        },
    { "mem_write",       3, 3, bi_mem_write       },
    { "mem_strings",     1, 2, bi_mem_strings     },
    { "mem_artifacts",   2, 2, bi_mem_artifacts   },
    { "mem_kernel_scan", 0, 0, bi_mem_kernel_scan },
    { "mem_compare",     5, 5, bi_mem_compare     },
    { "mem_pool_scan",   0, 0, bi_mem_pool_scan   },
    { "mem_driver_list", 0, 0, bi_mem_driver_list },
    { "mem_identify",    0, 0, bi_mem_identify    },
    { NULL, 0, 0, NULL },
};
