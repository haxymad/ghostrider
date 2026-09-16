/*
 * memory.c — Memory read/write/scan builtins for Windows.
 */

#include "jky_builtins.h"
#include "jky_platform.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static BuiltinResult bi_mem_read(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT || a[1].type != JKY_INT) {
        *out = mkbytes_empty(); return BUILTIN_OK;
    }

    DWORD pid = (DWORD)a[0].v.i;
    uint64_t addr = (uint64_t)a[1].v.i;
    size_t len = (c > 2 && a[2].type == JKY_INT) ? (size_t)a[2].v.i : 4096;
    if (len > 16 * 1024 * 1024) len = 16 * 1024 * 1024;

    HANDLE h = OpenProcess(PROCESS_VM_READ, FALSE, pid);
    if (!h) { *out = mkbytes_empty(); return BUILTIN_OK; }

    uint8_t *buf = (uint8_t *)malloc(64 * 1024);
    SIZE_T got = 0;
    ReadProcessMemory(h, (LPCVOID)addr, buf, len, &got);
    CloseHandle(h);

    Value v;
    v.type = JKY_BYTES;
    v.rc = 0;
    v.v.bytes.data = buf;
    v.v.bytes.len = got;
    *out = v;
    return BUILTIN_OK;
}

static BuiltinResult bi_mem_write(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT || a[1].type != JKY_INT) {
        *out = mkint(0); return BUILTIN_OK;
    }

    DWORD pid = (DWORD)a[0].v.i;
    uint64_t addr = (uint64_t)a[1].v.i;
    const uint8_t *data = NULL;
    size_t len = 0;

    if (a[2].type == JKY_BYTES) {
        data = a[2].v.bytes.data;
        len = a[2].v.bytes.len;
    } else if (a[2].type == JKY_STR) {
        data = (const uint8_t *)a[2].v.s;
        len = strlen(a[2].v.s);
    } else {
        *out = mkint(0);
        return BUILTIN_OK;
    }

    HANDLE h = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                           FALSE, pid);
    if (!h) { *out = mkint(0); return BUILTIN_OK; }

    SIZE_T wrote = 0;
    WriteProcessMemory(h, (LPVOID)addr, data, len, &wrote);
    CloseHandle(h);

    *out = mkint((int64_t)wrote);
    return BUILTIN_OK;
}

static BuiltinResult bi_mem_strings(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkarray(); return BUILTIN_OK; }

    int min_len = 4;
    if (c > 1 && a[1].type == JKY_INT)
        min_len = (int)a[1].v.i;

    Value arr = mkarray();
    DWORD pid = (DWORD)a[0].v.i;
    HANDLE h = OpenProcess(PROCESS_VM_READ, FALSE, pid);
    if (!h) { *out = arr; return BUILTIN_OK; }

    /* Scan first 64MB for printable strings */
uint8_t *buf = (uint8_t *)malloc(64 * 1024);    if (!buf) { CloseHandle(h); *out = arr; return BUILTIN_OK; }

    for (uint64_t addr = 0; addr < 64 * 1024 * 1024; addr += 65536) {
        SIZE_T got = 0;
        ReadProcessMemory(h, (LPCVOID)addr, buf, 65536, &got);
        for (SIZE_T i = 0; i < got; i++) {
            if (buf[i] >= 0x20 && buf[i] < 0x7f) {
                size_t start = i;
                while (i < got && buf[i] >= 0x20 && buf[i] < 0x7f) i++;
                if ((size_t)(i - start) >= (size_t)min_len) {
                    char s[256];
                    size_t n = i - start;
                    if (n > 255) n = 255;
                    memcpy(s, buf + start, n);
                    s[n] = 0;
                    arr_push(arr.v.arr, mkstr(s));
                }
            }
        }
    }

    free(buf);
    CloseHandle(h);
    *out = arr;
    return BUILTIN_OK;
}

const Builtin BUILTINS_MEMORY[] = {
    { "mem_read",     2, 3, bi_mem_read    },
    { "mem_write",    3, 3, bi_mem_write   },
    { "mem_dump",     3, 3, bi_mem_read    },
    { "mem_strings",  1, 2, bi_mem_strings },
    { "mem_compare",  5, 5, bi_mem_read    },
    { "mem_scan",     2, 2, bi_mem_strings },
    { NULL, 0, 0, NULL },
};
