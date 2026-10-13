# Double-precision variant of posscan.ps1: finds x,y,z double triples (24 bytes in a row) that look like the player's position,
# then narrows them down by movement.
#   find:   posscan.ps1 -Find -X -30,5 -Y 35.3,38 -Z 285,325
#   filter: posscan.ps1 -DY -2.5,-0.5 -Move 0.5,15   (keep triples whose y changed by DY
#           and horizontal distance moved is within Move, compared to the saved values)
param(
    [switch]$Find,
    [float[]]$X, [float[]]$Y, [float[]]$Z,
    [float[]]$DY = @(-1e9, 1e9),
    [float[]]$Move = @(0, 1e9),
    [string]$Process = "MirrorsEdgeCatalyst",
    [string]$List = "$PSScriptRoot\..\output\triples_d.txt"
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class PosD {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    static bool In(double v, float lo, float hi) { return v >= lo && v <= hi; }  // false for NaN

    public static List<long> Find(IntPtr h, float x0, float x1, float y0, float y1, float z0, float z1) {
        var hits = new List<long>();
        var buf = new byte[16 << 20];
        long addr = 0; MBI m;
        while (VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
            long b = m.Base.ToInt64(), size = m.Size.ToInt64();
            bool writable = (m.Protect & (0x04 | 0x08 | 0x40 | 0x80)) != 0, guard = (m.Protect & 0x100) != 0;
            if (m.State == 0x1000 && writable && !guard) {
                for (long off = 0; off < size; off += buf.Length) {
                    long n = Math.Min(buf.Length, size - off); IntPtr got;
                    if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                    int cnt = (int)got.ToInt64();
                    for (int i = 0; i + 24 <= cnt; i += 8) {
                        if (In(BitConverter.ToDouble(buf, i + 8), y0, y1) &&
                            In(BitConverter.ToDouble(buf, i), x0, x1) &&
                            In(BitConverter.ToDouble(buf, i + 16), z0, z1)) hits.Add(b + off + i);
                    }
                }
            }
            addr = b + size;
            if (addr <= 0) break;
        }
        return hits;
    }

    public static double[] Read3(IntPtr h, long a) {
        var b = new byte[24]; IntPtr got;
        if (!ReadProcessMemory(h, new IntPtr(a), b, new IntPtr(24), out got)) return null;
        return new[] { BitConverter.ToDouble(b, 0), BitConverter.ToDouble(b, 8), BitConverter.ToDouble(b, 16) };
    }
}
"@

$inv = [Globalization.CultureInfo]::InvariantCulture
function Fmt([double]$v) { $v.ToString("0.###", $inv) }

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [PosD]::OpenProcess(0x0410, $false, $p.Id)
if ($h -eq [IntPtr]::Zero) { throw "Cannot open process $($p.Id)" }

if ($Find) {
    $hits = [PosD]::Find($h, $X[0], $X[1], $Y[0], $Y[1], $Z[0], $Z[1])
    $out = foreach ($a in $hits) { $v = [PosD]::Read3($h, $a); "{0:X} {1} {2} {3}" -f $a, (Fmt $v[0]), (Fmt $v[1]), (Fmt $v[2]) }
    $out | Set-Content -Encoding ascii $List
    "Found $($hits.Count)"
    return
}

$keep = foreach ($line in Get-Content $List) {
    $f = $line.Split(' ')
    $a = [Convert]::ToInt64($f[0], 16)
    $old = $f[1..3] | ForEach-Object { [double]::Parse($_, $inv) }
    $v = [PosD]::Read3($h, $a)
    if ($null -eq $v -or [double]::IsNaN($v[1])) { continue }
    $deltaY = $v[1] - $old[1]
    $dist = [Math]::Sqrt(($v[0] - $old[0]) * ($v[0] - $old[0]) + ($v[2] - $old[2]) * ($v[2] - $old[2]))
    if ($deltaY -ge $DY[0] -and $deltaY -le $DY[1] -and $dist -ge $Move[0] -and $dist -le $Move[1]) {
        "{0:X} {1} {2} {3}" -f $a, (Fmt $v[0]), (Fmt $v[1]), (Fmt $v[2])
    }
}
@($keep) | Set-Content -Encoding ascii $List
"Kept $(@($keep).Count)"
@($keep) | Select-Object -First 40
