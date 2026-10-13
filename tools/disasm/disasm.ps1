# Disassembles live (already decrypted) game code around an address.
#   disasm.ps1 -Address 14352C590            # from the function start (found via int3 padding) to a bit past the address
#   disasm.ps1 -Address 14352C590 -Before 0x60 -After 0x40
param(
    [Parameter(Mandatory)][string]$Address,
    [int]$Before = 0,        # 0 = search back to the function start
    [int]$After = 0x40,
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type -Path "$PSScriptRoot\..\lib\Iced.dll"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Code {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
}
"@

$target = [Convert]::ToInt64($Address, 16)
$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$h = [Code]::OpenProcess(0x0410, $false, $p.Id)

$window = 0x1000
$buf = New-Object byte[] ($window + $After)
$got = [IntPtr]::Zero
[void][Code]::ReadProcessMemory($h, [IntPtr]($target - $window), $buf, [IntPtr]$buf.Length, [ref]$got)

if ($Before -gt 0) {
    $start = $window - $Before
} else {
    # Functions are padded with int3 (CC); the last CC run before the target marks the function start.
    $start = 0
    for ($i = $window - 1; $i -gt 0; $i--) {
        if ($buf[$i] -eq 0xCC -and $buf[$i - 1] -eq 0xCC) { $start = $i + 1; break }
    }
}

if ($got.ToInt64() -ne $buf.Length) { throw "Could not read code at $Address" }
$reader = [Iced.Intel.ByteArrayCodeReader]::new($buf, $start, $buf.Length - $start)
$decoder = [Iced.Intel.Decoder]::Create(64, $reader)
$decoder.IP = [uint64]($target - $window + $start)
$fmt = New-Object Iced.Intel.IntelFormatter
$fmt.Options.FirstOperandCharIndex = 8
$out = New-Object Iced.Intel.StringOutput
$end = [uint64]($target + $After)
while ($decoder.IP -lt $end) {
    $ins = $decoder.Decode()
    if ($ins.Length -eq 0) { break }
    $fmt.Format([ref]$ins, $out)
    $mark = if ($ins.IP -eq [uint64]$target) { "=>" } else { "  " }
    "{0} {1:X}  {2}" -f $mark, $ins.IP, $out.ToStringAndReset()
}
