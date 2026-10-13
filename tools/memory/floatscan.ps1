# Streaming float scanner for the running game (no full memory snapshot, unlike CE).
#   -Lo/-Hi          value range to match
#   new scan:        floatscan.ps1 -Lo 35.40 -Hi 35.42
#   filter results:  floatscan.ps1 -Lo 36.40 -Hi 36.42 -Next
# Results are kept in -List (one hex address per line).
param(
    [Parameter(Mandatory)][float]$Lo,
    [Parameter(Mandatory)][float]$Hi,
    [switch]$Next,
    [string]$Process = "MirrorsEdgeCatalyst",
    [string]$List = "$PSScriptRoot\..\output\scan.txt"
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Scan {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    const uint MEM_COMMIT = 0x1000, WRITABLE = 0x04 | 0x08 | 0x40 | 0x80, GUARD = 0x100;

    public static List<long> First(IntPtr h, float lo, float hi) {
        var hits = new List<long>();
        var buf = new byte[16 << 20];
        long addr = 0; MBI m;
        while (VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
            long b = m.Base.ToInt64(), size = m.Size.ToInt64();
            if (m.State == MEM_COMMIT && (m.Protect & WRITABLE) != 0 && (m.Protect & GUARD) == 0) {
                for (long off = 0; off < size; off += buf.Length) {
                    long n = Math.Min(buf.Length, size - off); IntPtr got;
                    if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                    int cnt = (int)got.ToInt64();
                    for (int i = 0; i + 4 <= cnt; i += 4) {
                        float v = BitConverter.ToSingle(buf, i);
                        if (v >= lo && v <= hi) hits.Add(b + off + i);
                    }
                }
            }
            addr = b + size;
            if (addr <= 0) break;
        }
        return hits;
    }

    public static List<long> Filter(IntPtr h, long[] addrs, float lo, float hi) {
        var keep = new List<long>(); var b = new byte[4]; IntPtr got;
        foreach (var a in addrs) {
            if (!ReadProcessMemory(h, new IntPtr(a), b, new IntPtr(4), out got)) continue;
            float v = BitConverter.ToSingle(b, 0);
            if (v >= lo && v <= hi) keep.Add(a);
        }
        return keep;
    }
}
"@

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [Scan]::OpenProcess(0x0410, $false, $p.Id)
if ($h -eq [IntPtr]::Zero) { throw "Cannot open process $($p.Id)" }

if ($Next) {
    $prev = [long[]](Get-Content $List | ForEach-Object { [Convert]::ToInt64($_, 16) })
    $res = [Scan]::Filter($h, $prev, $Lo, $Hi)
    "Kept $($res.Count) of $($prev.Count)"
} else {
    $res = [Scan]::First($h, $Lo, $Hi)
    "Found $($res.Count)"
}
$res | ForEach-Object { "{0:X}" -f $_ } | Set-Content -Encoding ascii $List
