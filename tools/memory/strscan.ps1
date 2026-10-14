# Searches the running game's memory for ASCII strings matching a regex (type names,
# asset paths, ...). By default only the .exe image; -AllMemory scans every readable region.
#   strscan.ps1 -Pattern 'Ghost[A-Z]\w+'
#   strscan.ps1 -Pattern 'TimeTrial\w*' -AllMemory -Count
param(
    [Parameter(Mandatory)][string]$Pattern,
    [switch]$AllMemory,
    [switch]$Count,          # also print how many times each string occurs
    [int]$Max = 300,
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
public static class StrScan {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    public static SortedDictionary<string, int> Find(IntPtr h, long lo, long hi, string pattern, int max) {
        var found = new SortedDictionary<string, int>();
        var rx = new Regex(pattern, RegexOptions.Compiled);
        var buf = new byte[16 << 20]; long addr = lo; MBI m;
        while (addr < hi && VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
            long b = m.Base.ToInt64(), size = m.Size.ToInt64();
            bool readable = (m.Protect & 0xEE) != 0, bad = (m.Protect & 0x101) != 0;
            if (m.State == 0x1000 && readable && !bad) {
                for (long off = 0; off < size; off += buf.Length) {
                    long n = Math.Min(buf.Length, size - off); IntPtr got;
                    if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                    string s = Encoding.ASCII.GetString(buf, 0, (int)got.ToInt64());
                    foreach (Match x in rx.Matches(s)) {
                        int c;
                        if (found.TryGetValue(x.Value, out c)) found[x.Value] = c + 1;
                        else if (found.Count < max) found[x.Value] = 1;
                    }
                }
            }
            addr = b + size;
            if (addr <= 0) break;
        }
        return found;
    }
}
"@

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [StrScan]::OpenProcess(0x0410, $false, $p.Id)
$lo = 0; $hi = [long]::MaxValue
if (-not $AllMemory) {
    $lo = $p.MainModule.BaseAddress.ToInt64()
    $hi = $lo + $p.MainModule.ModuleMemorySize
}
$found = [StrScan]::Find($h, $lo, $hi, $Pattern, $Max)
"Found $($found.Count) distinct strings"
foreach ($kv in $found.GetEnumerator()) { if ($Count) { "{0,6}  {1}" -f $kv.Value, $kv.Key } else { $kv.Key } }
