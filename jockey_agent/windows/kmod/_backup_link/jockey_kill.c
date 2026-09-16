#include "jockey_common.h"

/*
 * jockey_kill.c ??? Process termination via kernel.
 */

#include "jockey.h"

NTSTATUS jockey_ioctl_kill_pid(PIRP irp, ULONG in_len, ULONG out_len)
{
    struct jky_req_kill req;
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

    /* Use PsTerminateProcess for clean kernel-level kill */
    status = PsTerminateProcess(target, req.exit_code);

    ObDereferenceObject(target);

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = NT_SUCCESS(status) ? 1 : 0;
    irp->IoStatus.Information = sizeof(uint32_t);

    DbgPrint("[jockey] Kill PID %u: 0x%X\n", req.pid, status);
    return STATUS_SUCCESS;
}

