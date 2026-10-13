# Finds 4x4 float transform matrices (LinearTransform: right, up, forward, translation rows;
# rotation rows orthonormal, w = 0,0,0,1) whose translation lies near a point, then narrows
# them down by how they change.
#   find:    matscan.ps1 -Find -Center -12.7,66.2,300.2 -Radius 1
#   filter:  matscan.ps1 -Center <new> -Radius 1 [-MinTurnDeg 20] [-MaxTurnDeg 5]
#            keeps matrices that still look like a transform near the new center and whose
#            forward vector turned by at least MinTurnDeg / at most MaxTurnDeg since the last scan
param(
    [switch]$Find,
    [Parameter(Mandatory)][float[]]$Center,
    [float]$Radius = 1.0,
    [float]$MinTurnDeg = 0,
    [float]$MaxTurnDeg = 180,
    [int]$Show = 40,
    [string]$Process = "MirrorsEdgeCatalyst",
    [string]$List = "$PSScriptRoot\..\output\matrices.txt"
)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Mat {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr len);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);

    static float F(byte[] b, int i) { return BitConverter.ToSingle(b, i); }

    // m = 16 floats at offset o. Checks transform shape and translation distance.
    public static bool IsTransformNear(byte[] b, int o, float cx, float cy, float cz, float r) {
        float tx = F(b, o + 48), ty = F(b, o + 52), tz = F(b, o + 56), tw = F(b, o + 60);
        if (!(Math.Abs(tw - 1) < 1e-3f)) return false;
        float dx = tx - cx, dy = ty - cy, dz = tz - cz;
        if (!(dx * dx + dy * dy + dz * dz <= r * r)) return false;
        for (int row = 0; row < 3; row++) {
            int p = o + row * 16;
            float x = F(b, p), y = F(b, p + 4), z = F(b, p + 8), w = F(b, p + 12);
            if (!(Math.Abs(w) < 1e-3f)) return false;
            if (!(Math.Abs(x * x + y * y + z * z - 1) < 0.01f)) return false;
        }
        for (int a = 0; a < 3; a++)
            for (int c = a + 1; c < 3; c++) {
                int pa = o + a * 16, pc = o + c * 16;
                float dot = F(b, pa) * F(b, pc) + F(b, pa + 4) * F(b, pc + 4) + F(b, pa + 8) * F(b, pc + 8);
                if (!(Math.Abs(dot) < 0.01f)) return false;
            }
        return true;
    }

    public static List<long> Find(IntPtr h, float cx, float cy, float cz, float r) {
        var hits = new List<long>();
        var buf = new byte[16 << 20]; long addr = 0; MBI m;
        while (VirtualQueryEx(h, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) != IntPtr.Zero) {
            long b = m.Base.ToInt64(), size = m.Size.ToInt64();
            bool writable = (m.Protect & (0x04 | 0x08 | 0x40 | 0x80)) != 0, guard = (m.Protect & 0x100) != 0;
            if (m.State == 0x1000 && writable && !guard) {
                for (long off = 0; off < size; off += buf.Length) {
                    long n = Math.Min(buf.Length, size - off); IntPtr got;
                    if (!ReadProcessMemory(h, new IntPtr(b + off), buf, new IntPtr(n), out got)) continue;
                    int cnt = (int)got.ToInt64();
                    for (int i = 0; i + 64 <= cnt; i += 16)
                        if (IsTransformNear(buf, i, cx, cy, cz, r)) hits.Add(b + off + i);
                }
            }
            addr = b + size;
            if (addr <= 0) break;
        }
        return hits;
    }

    public static byte[] Read(IntPtr h, long a, int n) {
        var b = new byte[n]; IntPtr got;
        return ReadProcessMemory(h, new IntPtr(a), b, new IntPtr(n), out got) ? b : null;
    }
}
"@

$inv = [Globalization.CultureInfo]::InvariantCulture
function Fmt([float]$v) { $v.ToString("0.###", $inv) }
function Describe([long]$a, [byte[]]$m) {
    $f = 0..15 | ForEach-Object { [BitConverter]::ToSingle($m, $_ * 4) }
    # address  tx ty tz  fx fy fz (forward row)  uy (up.y)
    "{0:X} {1} {2} {3} {4} {5} {6} {7}" -f $a, (Fmt $f[12]), (Fmt $f[13]), (Fmt $f[14]), (Fmt $f[8]), (Fmt $f[9]), (Fmt $f[10]), (Fmt $f[5])
}

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [Mat]::OpenProcess(0x0410, $false, $p.Id)

if ($Find) {
    $hits = [Mat]::Find($h, $Center[0], $Center[1], $Center[2], $Radius)
    $out = foreach ($a in $hits) { $m = [Mat]::Read($h, $a, 64); if ($m) { Describe $a $m } }
} else {
    $out = foreach ($line in Get-Content $List) {
        $f = $line.Split(' ')
        $a = [Convert]::ToInt64($f[0], 16)
        $m = [Mat]::Read($h, $a, 64)
        if (-not $m -or -not [Mat]::IsTransformNear($m, 0, $Center[0], $Center[1], $Center[2], $Radius)) { continue }
        $old = $f[4..6] | ForEach-Object { [double]::Parse($_, $inv) }
        $new = 8, 9, 10 | ForEach-Object { [double][BitConverter]::ToSingle($m, $_ * 4) }
        $dot = [Math]::Max(-1, [Math]::Min(1, $old[0] * $new[0] + $old[1] * $new[1] + $old[2] * $new[2]))
        $turn = [Math]::Acos($dot) * 180 / [Math]::PI
        if ($turn -ge $MinTurnDeg -and $turn -le $MaxTurnDeg) { Describe $a $m }
    }
}
@($out) | Set-Content -Encoding ascii $List
"Kept $(@($out).Count)   (columns: address  pos x y z  forward x y z  up.y)"
@($out) | Select-Object -First $Show
