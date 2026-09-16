# unhide_notepad.ps1 — restore notepad visibility via jockey driver
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

    public static bool UnhidePid(uint pid) {
        IntPtr h = CreateFile(@"\\.\Jockey", 0xC0000000 | 0x40000000,
            0x00000001 | 0x00000002, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == IntPtr.Zero || h == new IntPtr(-1)) {
            Console.WriteLine("[-] open device failed: " + Marshal.GetLastWin32Error());
            return false;
        }
        byte[] inbuf = new byte[8];
        Buffer.BlockCopy(BitConverter.GetBytes(pid), 0, inbuf, 0, 4);
        int br;
        bool ok = DeviceIoControl(h, 0x80002008, inbuf, 4, null, 0, out br, IntPtr.Zero);
        CloseHandle(h);
        return ok;
    }
}
"@

$np = Get-Process notepad -ErrorAction SilentlyContinue
if (-not $np) {
    Write-Host "[-] notepad not running"
    exit 1
}

Write-Host "[*] unhiding pid $($np.Id)"
if ([JockeyDrv]::UnhidePid([uint32]$np.Id)) {
    Write-Host "[+] restored"
} else {
    Write-Host "[-] failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}
