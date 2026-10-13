# Reads Faith's position through a pointer path from the game's .exe and prints it.
#   showpos.ps1            print once
#   showpos.ps1 -Watch     print every 0.5 s until Ctrl+C
param(
    [string[]]$Statics = @("23DA020", "2577238"),   # exe offsets holding a pointer to the object
    [int]$PosOffset = 0x730,                          # position x,y,z inside that object
    [switch]$Watch,
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Show {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    public static byte[] Read(IntPtr h, long a, int n) {
        var b = new byte[n]; IntPtr got;
        return ReadProcessMemory(h, new IntPtr(a), b, new IntPtr(n), out got) ? b : null;
    }
}
"@

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [Show]::OpenProcess(0x0410, $false, $p.Id)
$base = $p.MainModule.BaseAddress.ToInt64()

do {
    $line = foreach ($s in $Statics) {
        $ptrBytes = [Show]::Read($h, $base + [Convert]::ToInt64($s, 16), 8)
        $obj = if ($ptrBytes) { [BitConverter]::ToInt64($ptrBytes, 0) } else { 0 }
        $pos = if ($obj) { [Show]::Read($h, $obj + $PosOffset, 12) }
        if ($pos) {
            "exe+{0}: X={1,8:0.00} Y={2,7:0.00} Z={3,8:0.00}" -f $s, [BitConverter]::ToSingle($pos, 0), [BitConverter]::ToSingle($pos, 4), [BitConverter]::ToSingle($pos, 8)
        } else {
            "exe+{0}: (no data)" -f $s
        }
    }
    $line -join "   |   "
    if ($Watch) { Start-Sleep -Milliseconds 500 }
} while ($Watch)
