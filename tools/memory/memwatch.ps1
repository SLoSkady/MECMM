# Reads float addresses from a Cheat Engine table (.CT) out of the running game
# and records them to CSV over time.
#   -Snapshot      print current values once (plus neighbouring floats at -8..+8)
#   -Seconds N     record for N seconds into -Out (CSV), one row per sample
#   -Extra         extra hex addresses to include besides the table
param(
    [string]$Table = "$PSScriptRoot\..\..\docs\research\tables\height.CT",
    [string]$Process = "MirrorsEdgeCatalyst",
    [switch]$Snapshot,
    [int]$Seconds = 30,
    [int]$IntervalMs = 100,
    [string]$Out = "$PSScriptRoot\..\output\recording.csv",
    [string[]]$Extra = @()   # additional hex addresses to watch, e.g. 30C8BD90
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Mem {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out IntPtr read);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr hwnd, out int pid);
    public static int ForegroundPid() { int pid; GetWindowThreadProcessId(GetForegroundWindow(), out pid); return pid; }
    public static float? ReadFloat(IntPtr h, long addr) {
        var b = new byte[4]; IntPtr n;
        if (!ReadProcessMemory(h, new IntPtr(addr), b, 4, out n) || n.ToInt64() != 4) return null;
        return BitConverter.ToSingle(b, 0);
    }
}
"@

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [Mem]::OpenProcess(0x0410, $false, $p.Id)  # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
if ($h -eq [IntPtr]::Zero) { throw "Cannot open process $($p.Id)" }

$addrs = @()
if ($Table) {
    $addrs = @(([xml](Get-Content $Table -Raw)).CheatTable.CheatEntries.CheatEntry |
        ForEach-Object { [Convert]::ToInt64($_.Address, 16) })
}
$addrs += $Extra | ForEach-Object { [Convert]::ToInt64($_, 16) }

function Fmt($v) { if ($null -eq $v) { "?" } else { $v.ToString("0.###", [Globalization.CultureInfo]::InvariantCulture) } }

if ($Snapshot) {
    foreach ($a in $addrs) {
        $row = foreach ($off in -8, -4, 0, 4, 8) { Fmt ([Mem]::ReadFloat($h, $a + $off)) }
        "{0:X}  [-8]{1}  [-4]{2}  [0]{3}  [+4]{4}  [+8]{5}" -f $a, $row[0], $row[1], $row[2], $row[3], $row[4]
    }
    return
}

$header = "t_ms," + (($addrs | ForEach-Object { "{0:X}" -f $_ }) -join ",")
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add($header)
# Only the time the game window is in focus counts: the game pauses when alt-tabbed.
$sw = [Diagnostics.Stopwatch]::new()
$deadline = (Get-Date).AddMinutes(15)
while ($sw.ElapsedMilliseconds -lt $Seconds * 1000 -and (Get-Date) -lt $deadline) {
    if ([Mem]::ForegroundPid() -ne $p.Id) { $sw.Stop(); Start-Sleep -Milliseconds 200; continue }
    $sw.Start()
    $vals = foreach ($a in $addrs) { Fmt ([Mem]::ReadFloat($h, $a)) }
    $lines.Add("$($sw.ElapsedMilliseconds)," + ($vals -join ","))
    Start-Sleep -Milliseconds $IntervalMs
}
$lines | Set-Content -Encoding utf8 $Out
"Recorded $($lines.Count - 1) samples of $($addrs.Count) addresses to $Out"
