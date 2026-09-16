/*
 * anti.c — Anti-debug, anti-VM, anti-sandbox builtins for Windows.
 */

#include "jky_builtins.h"
#include "jky_platform.h"
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

static BuiltinResult bi_anti_debug(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    int detected = 0;

    BOOL debugged = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &debugged);
    if (debugged) detected = 1;

    if (!detected && IsDebuggerPresent())
        detected = 1;

    *out = mkint(detected);
    return BUILTIN_OK;
}

static BuiltinResult bi_anti_vm(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    int detected = 0;

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Services", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char name[256];
        DWORD i, sz = sizeof(name);
        for (i = 0; RegEnumValueA(hKey, i, name, &sz, NULL, NULL, NULL, NULL) == ERROR_SUCCESS; i++) {
            if (strstr(name, "VMWARE") || strstr(name, "VBOX") ||
                strstr(name, "QEMU") || strstr(name, "VIRTUAL") ||
                strstr(name, "HYPERV")) {
                detected = 1;
                break;
            }
            sz = sizeof(name);
        }
        RegCloseKey(hKey);
    }

    if (!detected) {
        int regs[4] = {0};
        __cpuid(regs, 1);
        if (regs[2] & (1 << 31))
            detected = 1;
    }

    *out = mkint(detected);
    return BUILTIN_OK;
}

static BuiltinResult bi_anti_sandbox(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    int detected = 0;
    DWORD t1 = GetTickCount();
    Sleep;
    DWORD t2 = GetTickCount();

    if ((int)(t2 - t1) < 4000)
        detected = 1;

    *out = mkint(detected);
    return BUILTIN_OK;
}

static BuiltinResult bi_anti_edr(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    Value arr = mkarray();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        *out = arr;
        return BUILTIN_OK;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe)) {
        do {
            const char *edr_names[] = {
                "csagent.exe", "cb.exe", "sentinel.exe", "MsMpEng.exe",
                "avp.exe", "bdagent.exe", "rtvscan.exe", "mcshield.exe",
                "kavsvc.exe", "nissrv.exe", "MsSense.exe", "SenseCncProxy.exe",
                "forefront.exe", "fmpdtask.exe", "hips.exe", "hipsmain.exe",
                NULL
            };
            for (int i = 0; edr_names[i]; i++) {
                if (_stricmp(pe.szExeFile, edr_names[i]) == 0) {
                    arr_push(arr.v.arr, mkstr(pe.szExeFile));
                    break;
                }
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    *out = arr;
    return BUILTIN_OK;
}

const Builtin BUILTINS_ANTI[] = {
    { "anti_debug",   0, 0, bi_anti_debug   },
    { "anti_vm",      0, 0, bi_anti_vm      },
    { "anti_sandbox", 0, 0, bi_anti_sandbox },
    { "anti_edr",     0, 0, bi_anti_edr     },
    { NULL, 0, 0, NULL },
};
