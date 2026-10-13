# CE-style float scanner over ALL writable memory (private + mapped + image), streaming,
# so it does not need a full memory snapshot. State is kept in a binary file.
#   first:  heightscan.ps1 -First -Lo 30 -Hi 45
#   next:   heightscan.ps1 -Cmp inc|dec|same     (compared to previous scan)
#           heightscan.ps1 -Cmp range -Lo 35.40 -Hi 35.42
param(
    [switch]$First,
    [ValidateSet("inc", "dec", "same", "range")][string]$Cmp,
    [float]$Lo, [float]$Hi,
    [float]$Tol = 0.005,          # "same" tolerance; inc/dec need at least this much change
    [string]$Process = "MirrorsEdgeCatalyst",
    [string]$State = "$PSScriptRoot\..\output\heightscan.bin",
    [int]$Show = 40
)

Add-Type @"
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class HScan {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    // State file: repeated (long address, float value)
    public static long First(IntPtr h, float lo, float hi, string path) {
        long count = 0; var buf = new byte[16 << 20]; long addr = 0; MBI m;
        using (var w = new BinaryWriter(File.Create(path))) {
            while (VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
                long b = m.Base.ToInt64(), size = m.Size.ToInt64();
                bool writable = (m.Protect & (0x04 | 0x08 | 0x40 | 0x80)) != 0, guard = (m.Protect & 0x100) != 0;
                if (m.State == 0x1000 && writable && !guard) {
                    for (long off = 0; off < size; off += buf.Length) {
                        long n = Math.Min(buf.Length, size - off); IntPtr got;
                        if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                        int cnt = (int)got.ToInt64();
                        for (int i = 0; i + 4 <= cnt; i += 4) {
                            float v = BitConverter.ToSingle(buf, i);
                            if (v >= lo && v <= hi) { w.Write(b + off + i); w.Write(v); count++; }
                        }
                    }
                }
                addr = b + size;
                if (addr <= 0) break;
            }
        }
        return count;
    }

    // mode: 0 inc, 1 dec, 2 same, 3 range
    public static string[] Next(IntPtr h, string path, int mode, float lo, float hi, float tol, int show) {
        var tmp = path + ".tmp"; long kept = 0, total = 0;
        var sample = new System.Collections.Generic.List<string>();
        var b = new byte[4];
        using (var r = new BinaryReader(File.OpenRead(path)))
        using (var w = new BinaryWriter(File.Create(tmp))) {
            long len = r.BaseStream.Length;
            while (r.BaseStream.Position < len) {
                long a = r.ReadInt64(); float old = r.ReadSingle(); total++;
                IntPtr got;
                if (!ReadProcessMemory(h, new IntPtr(a), b, new IntPtr(4), out got)) continue;
                float v = BitConverter.ToSingle(b, 0);
                if (float.IsNaN(v)) continue;
                bool ok = mode == 0 ? v > old + tol
                        : mode == 1 ? v < old - tol
                        : mode == 2 ? Math.Abs(v - old) <= tol
                        : v >= lo && v <= hi;
                if (!ok) continue;
                w.Write(a); w.Write(v); kept++;
                if (sample.Count < show) sample.Add(string.Format("{0:X} {1:0.###} (was {2:0.###})", a, v, old));
            }
        }
        File.Delete(path); File.Move(tmp, path);
        sample.Insert(0, "Kept " + kept + " of " + total);
        return sample.ToArray();
    }
}
"@

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [HScan]::OpenProcess(0x0410, $false, $p.Id)
if ($h -eq [IntPtr]::Zero) { throw "Cannot open process $($p.Id)" }
$State = [IO.Path]::GetFullPath($State)

if ($First) {
    "Found " + [HScan]::First($h, $Lo, $Hi, $State)
} else {
    $mode = @{ inc = 0; dec = 1; same = 2; range = 3 }[$Cmp]
    [HScan]::Next($h, $State, $mode, $Lo, $Hi, $Tol, $Show)
}
