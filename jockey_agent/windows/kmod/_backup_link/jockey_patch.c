#include "jockey_common.h"

/*
 * jockey_ssdt.c ??? SSDT unhooking and ntdll patching.
 *
 * Restores original SSDT entries to bypass EDR hooks.
 * Patches in-memory ntdll to remove userland hooks.
 */

#include "jockey.h"

/* SSDT structure (varies by Windows version) */
typedef struct _SERVICE_DESCRIPTOR_TABLE {
    PVOID *ServiceTableBase;
    PVOID *ServiceCounterTableBase;
    ULONG NumberOfServices;
    PUCHAR ArgumentTable;
} SERVICE_DESCRIPTOR_TABLE, *PSERVICE_DESCRIPTOR_TABLE;

extern PSERVICE_DESCRIPTOR_TABLE KeServiceDescriptorTable;

/*
 * jockey_ioctl_unhook_ssdt:
 *   Restores original SSDT entries.
 *   Requires reading clean SSDT from disk (ntoskrnl.exe) or backup.
 */
NTSTATUS jockey_ioctl_unhook_ssdt(PIRP irp, ULONG in_len, ULONG out_len)
{
    uint32_t *out;

    UNREFERENCED_PARAMETER(in_len);
    UNREFERENCED_PARAMETER(out_len);

    /* SSDT unhooking requires knowing original addresses */
    /* This is a placeholder ??? full implementation needs:
     * 1. Map clean ntoskrnl.exe from disk
     * 2. Read original SSDT
     * 3. Compare with current SSDT
     * 4. Restore hooked entries
     */

    DbgPrint("[jockey] SSDT unhook requested (requires clean kernel backup)\n");

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 0;
    irp->IoStatus.Information = sizeof(uint32_t);
    return STATUS_NOT_SUPPORTED;
}

/*
 * jockey_ioctl_patch_ntdll:
 *   Patches in-memory ntdll to remove EDR hooks.
 *   Maps clean ntdll from disk, copies original sections over hooked ones.
 */
NTSTATUS jockey_ioctl_patch_ntdll(PIRP irp, ULONG in_len, ULONG out_len)
{
    uint32_t *out;

    UNREFERENCED_PARAMETER(in_len);
    UNREFERENCED_PARAMETER(out_len);

    /* Ntdll patching in kernel:
     * 1. Find ntdll base in the process section list
     * 2. Map clean ntdll from System32
     * 3. Overwrite .text section with clean bytes
     * 4. Fix imports and relocations
     */

    DbgPrint("[jockey] Ntdll patch requested (userspace loader handles this)\n");

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 0;
    irp->IoStatus.Information = sizeof(uint32_t);
    return STATUS_NOT_SUPPORTED;
}

