/*
 * jockey_resolve.c
 * Runtime resolution of NT routines not exported by ntoskrnl.lib,
 * plus a stub for jockey_ioctl_disable_callbacks.
 */
#include "jockey_common.h"

/* -----------------------------------------------------------------
 * Runtime resolver
 * ----------------------------------------------------------------- */
typedef NTSTATUS (NTAPI *PFN_SeLookupPrivilegeValue)(
    PUNICODE_STRING SystemName,
    PUNICODE_STRING Name,
    PLUID           Luid);

typedef NTSTATUS (NTAPI *PFN_PsTerminateProcess)(
    PEPROCESS Process,
    NTSTATUS  ExitStatus);

static PFN_SeLookupPrivilegeValue g_pfnSeLookupPrivilegeValue = NULL;
static PFN_PsTerminateProcess    g_pfnPsTerminateProcess    = NULL;

static PVOID
jockey_get_routine(PCWSTR name)
{
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name);
    return MmGetSystemRoutineAddress(&us);
}

/* -----------------------------------------------------------------
 * SeLookupPrivilegeValue - 2-arg wrapper matching jockey_cred.c
 * ----------------------------------------------------------------- */
NTSTATUS NTAPI
SeLookupPrivilegeValue(
    _In_  PCWSTR Name,
    _Out_ PLUID  Luid)
{
    UNICODE_STRING usName;

    if (!Name || !Luid)
        return STATUS_INVALID_PARAMETER;

    if (!g_pfnSeLookupPrivilegeValue)
    {
        g_pfnSeLookupPrivilegeValue = (PFN_SeLookupPrivilegeValue)
            jockey_get_routine(L"SeLookupPrivilegeValue");
        if (!g_pfnSeLookupPrivilegeValue)
            return STATUS_PROCEDURE_NOT_FOUND;
    }

    RtlInitUnicodeString(&usName, Name);
    return g_pfnSeLookupPrivilegeValue(NULL, &usName, Luid);
}

/* -----------------------------------------------------------------
 * PsTerminateProcess - runtime-resolved wrapper
 * ----------------------------------------------------------------- */
NTSTATUS NTAPI
PsTerminateProcess(
    _In_ PEPROCESS Process,
    _In_ NTSTATUS  ExitStatus)
{
    if (!g_pfnPsTerminateProcess)
    {
        g_pfnPsTerminateProcess = (PFN_PsTerminateProcess)
            jockey_get_routine(L"PsTerminateProcess");
        if (!g_pfnPsTerminateProcess)
            return STATUS_PROCEDURE_NOT_FOUND;
    }
    return g_pfnPsTerminateProcess(Process, ExitStatus);
}

/* -----------------------------------------------------------------
 * jockey_ioctl_disable_callbacks - missing definition.
 * Real implementation should unhook ObRegisterCallbacks / registry
 * callbacks. Stub returns success so the link succeeds.
 * ----------------------------------------------------------------- */
NTSTATUS
jockey_ioctl_disable_callbacks(
    _In_ PVOID in,
    _In_ ULONG in_len,
    _Out_ PVOID *out)
{
    UNREFERENCED_PARAMETER(in);
    UNREFERENCED_PARAMETER(in_len);
    if (out)
        *out = NULL;
    return STATUS_SUCCESS;
}
