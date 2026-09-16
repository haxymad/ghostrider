/*
 * process.c — Process enumeration and manipulation builtins for Windows.
 */
#include "jky_builtins.h"
#include "jky_platform.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "psapi.lib")

typedef struct {
    NTSTATUS  ExitStatus;
    PVOID     PebBaseAddress;
    ULONG_PTR AffinityMask;
    KPRIORITY BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} MY_PROCESS_BASIC_INFORMATION;

typedef NTSTATUS (NTAPI *NtQueryInfo_t)(HANDLE, int, PVOID, ULONG, PULONG);

static BuiltinResult bi_self_pid(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_getpid());
    return BUILTIN_OK;
}

static BuiltinResult bi_parent_pid(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_getppid());
    return BUILTIN_OK;
}

static BuiltinResult bi_proc_list(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    Value arr = mkarray();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { *out = arr; return BUILTIN_OK; }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe)) {
        do {
            Value item = mkdict();
            dict_set(item.v.dict, "pid",  mkint((int64_t)pe.th32ProcessID));
            dict_set(item.v.dict, "name", mkstr(pe.szExeFile));
            dict_set(item.v.dict, "ppid", mkint((int64_t)pe.th32ParentProcessID));
            arr_push(arr.v.arr, item);
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_proc_kill(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkbool(0); return BUILTIN_OK; }

    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)a[0].v.i);
    if (!h) { *out = mkbool(0); return BUILTIN_OK; }

    BOOL r = TerminateProcess(h, (UINT)(c > 1 && a[1].type == JKY_INT ? a[1].v.i : 1));
    CloseHandle(h);

    *out = mkint(r ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_proc_info(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mknone(); return BUILTIN_OK; }

    DWORD pid = (DWORD)a[0].v.i;
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) { *out = mknone(); return BUILTIN_OK; }

    Value d = mkdict();

    {
        MY_PROCESS_BASIC_INFORMATION pbi;
        ULONG sz = sizeof(pbi);
        NtQueryInfo_t fn = (NtQueryInfo_t)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");
        if (fn && fn(h, 0, &pbi, sz, &sz) == 0) {
            dict_set(d.v.dict, "peb", mkint((int64_t)(uintptr_t)pbi.PebBaseAddress));
            dict_set(d.v.dict, "exit_status", mkint((int64_t)pbi.ExitStatus));
        }
    }

    {
        char path[MAX_PATH] = {0};
        if (GetModuleFileNameExA(h, NULL, path, MAX_PATH))
            dict_set(d.v.dict, "path", mkstr(path));
    }

    CloseHandle(h);
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_proc_modules(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkarray(); return BUILTIN_OK; }

    Value arr = mkarray();
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                           FALSE, (DWORD)a[0].v.i);
    if (!h) { *out = arr; return BUILTIN_OK; }

    HMODULE mods[1024];
    DWORD needed = 0;
    if (EnumProcessModules(h, mods, sizeof(mods), &needed)) {
        DWORD count = needed / sizeof(HMODULE);
        char name[MAX_PATH];
        for (DWORD i = 0; i < count; i++) {
            if (GetModuleFileNameExA(h, mods[i], name, MAX_PATH)) {
                const char *base = strrchr(name, '\\');
                base = base ? base + 1 : name;
                Value item = mkdict();
                dict_set(item.v.dict, "name", mkstr(base));
                dict_set(item.v.dict, "path", mkstr(name));
                arr_push(arr.v.arr, item);
            }
        }
    }

    CloseHandle(h);
    *out = arr;
    return BUILTIN_OK;
}

const Builtin BUILTINS_PROCESS[] = {
    { "getpid",       0, 0, bi_self_pid    },
    { "getppid",      0, 0, bi_parent_pid  },
    { "procs",        0, 0, bi_proc_list   },
    { "proc_list",    0, 0, bi_proc_list   },
    { "kill",         1, 2, bi_proc_kill   },
    { "proc_kill",    1, 2, bi_proc_kill   },
    { "proc_info",    1, 1, bi_proc_info   },
    { "proc_modules", 1, 1, bi_proc_modules},
    { NULL, 0, 0, NULL },
};