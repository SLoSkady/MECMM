# Finds exact byte sequences in the game's memory: an ASCII string (zero-terminated, as
# stored by the engine) or a 64-bit value (e.g. a pointer).
#   findbytes.ps1 -String PamClientReplayEntity
#   findbytes.ps1 -Pointer 1424A3B10            # 8-byte little-endian, 8-aligned
#   findbytes.ps1 -UInt32 0x2734a476 -ImageOnly # 4-byte value, any alignment (e.g. an immediate in code)
#   add -ImageOnly to search only inside the .exe image
param(
    [string]$String,
    [string]$Pointer,
    [string]$UInt32,
    [switch]$ImageOnly,
    [int]$Max = 50,
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Bytes {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    public static List<long> Find(IntPtr h, long lo, long hi, byte[] needle, int align, int max) {
        var hits = new List<long>();
        var buf = new byte[16 << 20]; long addr = lo; MBI m;
        while (addr < hi && hits.Count < max &&
               VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
            long b = m.Base.ToInt64(), size = m.Size.ToInt64();
            bool readable = (m.Protect & 0xEE) != 0, bad = (m.Protect & 0x101) != 0;
            if (m.State == 0x1000 && readable && !bad) {
                // overlap chunks by needle length so matches across chunk borders are found
                for (long off = 0; off < size; off += buf.Length - needle.Length) {
                    long n = Math.Min(buf.Length, size - off); IntPtr got;
                    if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                    int cnt = (int)got.ToInt64();
                    for (int i = 0; i + needle.Length <= cnt; i += align) {
                        if (buf[i] != needle[0]) continue;
                        int k = 1;
                        while (k < needle.Length && buf[i + k] == needle[k]) k++;
                        if (k == needle.Length && !hits.Contains(b + off + i)) { hits.Add(b + off + i); if (hits.Count >= max) break; }
                    }
                    if (n < buf.Length) break;
                }
            }
            addr = b + size;
            if (addr <= 0) break;
        }
        return hits;
    }
}
"@

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [Bytes]::OpenProcess(0x0410, $false, $p.Id)
$modBase = $p.MainModule.BaseAddress.ToInt64(); $modEnd = $modBase + $p.MainModule.ModuleMemorySize
$lo = 0; $hi = [long]::MaxValue
if ($ImageOnly) { $lo = $modBase; $hi = $modEnd }

if ($String) {
    $needle = [Text.Encoding]::ASCII.GetBytes($String) + [byte[]](0)
    $hits = [Bytes]::Find($h, $lo, $hi, $needle, 1, $Max)
} elseif ($UInt32) {
    $needle = [BitConverter]::GetBytes([Convert]::ToUInt32($UInt32.Replace("0x", ""), 16))
    $hits = [Bytes]::Find($h, $lo, $hi, $needle, 1, $Max)
} else {
    $needle = [BitConverter]::GetBytes([Convert]::ToInt64($Pointer, 16))
    $hits = [Bytes]::Find($h, $lo, $hi, $needle, 8, $Max)
}
foreach ($a in $hits) {
    $where = if ($a -ge $modBase -and $a -lt $modEnd) { "exe+{0:X}" -f ($a - $modBase) } else { "heap" }
    "{0:X}  {1}" -f $a, $where
}
"($($hits.Count) found)"
