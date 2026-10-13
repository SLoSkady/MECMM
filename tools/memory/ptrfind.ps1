# Finds 8-byte pointers that point into [Target - MaxOffset, Target], i.e. to the start of
# an object that contains Target. Marks pointers that live inside the game's .exe image
# (static locations that survive restarts).
#   ptrfind.ps1 -Target 1B685330 -MaxOffset 0x400
param(
    [Parameter(Mandatory)][string]$Target,
    [int]$MaxOffset = 0x400,
    [int]$Show = 60,
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Ptr {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    // returns "location value" pairs
    public static List<long[]> Find(IntPtr h, long lo, long hi) {
        var hits = new List<long[]>();
        var buf = new byte[16 << 20]; long addr = 0; MBI m;
        while (VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
            long b = m.Base.ToInt64(), size = m.Size.ToInt64();
            bool readable = (m.Protect & (0x02 | 0x04 | 0x08 | 0x20 | 0x40 | 0x80)) != 0, guard = (m.Protect & 0x101) != 0;
            if (m.State == 0x1000 && readable && !guard) {
                for (long off = 0; off < size; off += buf.Length) {
                    long n = Math.Min(buf.Length, size - off); IntPtr got;
                    if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                    int cnt = (int)got.ToInt64();
                    for (int i = 0; i + 8 <= cnt; i += 8) {
                        long v = BitConverter.ToInt64(buf, i);
                        if (v >= lo && v <= hi) hits.Add(new[] { b + off + i, v });
                    }
                }
            }
            addr = b + size;
            if (addr <= 0) break;
        }
        return hits;
    }
}
"@

$t = [Convert]::ToInt64($Target, 16)
$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$mod = $p.MainModule
$modBase = $mod.BaseAddress.ToInt64(); $modEnd = $modBase + $mod.ModuleMemorySize
$h = [Ptr]::OpenProcess(0x0410, $false, $p.Id)

$hits = [Ptr]::Find($h, $t - $MaxOffset, $t)
"Found $($hits.Count) pointers into [{0:X} .. {1:X}]" -f ($t - $MaxOffset), $t
$hits | Sort-Object { $_[0] -lt $modBase -or $_[0] -ge $modEnd }, { $t - $_[1] } | Select-Object -First $Show | ForEach-Object {
    $static = if ($_[0] -ge $modBase -and $_[0] -lt $modEnd) { "  STATIC exe+{0:X}" -f ($_[0] - $modBase) } else { "" }
    "{0:X} -> {1:X}  (target = pointer + 0x{2:X}){3}" -f $_[0], $_[1], ($t - $_[1]), $static
}
