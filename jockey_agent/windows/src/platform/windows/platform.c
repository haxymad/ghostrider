#include "jky_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <sys/stat.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

const char *jky_platform_name(void) { return "windows"; }

int jky_hostname(char *buf, size_t len) {
    DWORD sz = (DWORD)len;
    if (!GetComputerNameA(buf, &sz)) { buf[0] = 0; return -1; }
    buf[len - 1] = 0;
    return 0;
}

int jky_username(char *buf, size_t len) {
    DWORD sz = (DWORD)len;
    if (!GetUserNameA(buf, &sz)) { buf[0] = 0; return -1; }
    buf[len - 1] = 0;
    return 0;
}

int jky_kernel_version(char *buf, size_t len) {
    OSVERSIONINFOA vi;
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (!GetVersionExA(&vi)) { buf[0] = 0; return -1; }
    snprintf(buf, len, "%lu.%lu.%lu",
             vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    buf[len - 1] = 0;
    return 0;
}

int jky_arch(char *buf, size_t len) {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: strncpy(buf, "x86_64", len - 1); break;
    case PROCESSOR_ARCHITECTURE_INTEL: strncpy(buf, "x86",    len - 1); break;
    case PROCESSOR_ARCHITECTURE_ARM64: strncpy(buf, "arm64",  len - 1); break;
    default:                           strncpy(buf, "unknown", len - 1); break;
    }
    buf[len - 1] = 0;
    return 0;
}

uint64_t jky_uptime_sec(void) { return (uint64_t)(GetTickCount64() / 1000); }

int jky_cpu_count(void) {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}

uint64_t jky_total_mem(void) {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0;
    return (uint64_t)ms.ullTotalPhys;
}

uint64_t jky_free_mem(void) {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0;
    return (uint64_t)ms.ullAvailPhys;
}

int64_t jky_time_sec(void) { return (int64_t)time(NULL); }

void jky_sleep_ms(int ms) { if (ms > 0) Sleep((DWORD)ms); }

int jky_getpid(void)  { return (int)GetCurrentProcessId(); }

int jky_getppid(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    DWORD my_pid = GetCurrentProcessId();
    int ppid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (pe.th32ProcessID == my_pid) { ppid = (int)pe.th32ParentProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return ppid;
}

int jky_kill(int pid, int sig) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (!h) return -1;
    BOOL r = TerminateProcess(h, (UINT)(sig ? sig : 1));
    CloseHandle(h);
    return r ? 0 : -1;
}

int jky_cwd(char *buf, size_t len) {
    DWORD sz = GetCurrentDirectoryA((DWORD)len, buf);
    if (sz == 0 || sz >= len) { buf[0] = 0; return -1; }
    return 0;
}

int jky_chdir(const char *path) { return SetCurrentDirectoryA(path) ? 0 : -1; }

int jky_list_dir(const char *path, char ***out_names, int *out_count) {
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[MAX_PATH];
    int cap = 16, n = 0;
    char **names = NULL;

    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;

    names = (char **)malloc(cap * sizeof(char *));
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (n >= cap) { cap *= 2; names = (char **)realloc(names, cap * sizeof(char *)); }
        names[n++] = _strdup(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    for (int i = 1; i < n; i++) {
        char *k = names[i]; int j = i - 1;
        while (j >= 0 && strcmp(names[j], k) > 0) { names[j + 1] = names[j]; j--; }
        names[j + 1] = k;
    }
    *out_names = names;
    *out_count = n;
    return 0;
}

void jky_free_list(char **names, int count) {
    if (!names) return;
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
}

int jky_read_file(const char *path, uint8_t **out, size_t *out_len) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE) { CloseHandle(h); return -1; }
    uint8_t *buf = (uint8_t *)malloc(sz ? sz : 1);
    DWORD got = 0;
    if (!ReadFile(h, buf, sz, &got, NULL)) { free(buf); CloseHandle(h); return -1; }
    *out = buf;
    *out_len = got;
    CloseHandle(h);
    return 0;
}

int jky_write_file(const char *path, const uint8_t *data, size_t len) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD wrote = 0;
    WriteFile(h, data, (DWORD)len, &wrote, NULL);
    CloseHandle(h);
    return wrote == len ? 0 : -1;
}

int jky_file_exists(const char *path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int64_t jky_file_size(const char *path) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int64_t size = ((int64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    FindClose(h);
    return size;
}