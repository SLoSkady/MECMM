# Shows a live engine object with pointers resolved to class names.
#   inspect.ps1 -Address 1502E3130 -Size 0x310
# Needs tools/output/sdk/index.csv (run sdkdump.ps1 once).
param(
    [Parameter(Mandatory)][string[]]$Address,
    [int]$Size = 0x100,
    [string]$Index = "$PSScriptRoot\..\output\sdk\index.csv",
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type -Path "$PSScriptRoot\TypeInfo.cs", "$PSScriptRoot\Inspect.cs" -ErrorAction Stop

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$img = [Image]::new($p.Id, $p.MainModule.BaseAddress.ToInt64(), $p.MainModule.ModuleMemorySize)
foreach ($row in Import-Csv $Index) {
    [Inspect]::Names[$img.Base + [Convert]::ToInt64($row.typeinfo.Replace("exe+", ""), 16)] = $row.name
}
foreach ($a in $Address) { [Inspect]::Dump($img, [Convert]::ToInt64($a, 16), $Size) }
