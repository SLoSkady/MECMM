# Dumps the game's runtime type information (all engine classes, structs and enums with their
# fields and offsets) into text files. The game must be running (best: in the open world).
#   sdkdump.ps1                 -> tools/output/sdk/{classes,valuetypes,enums}.txt, index.csv
# The output is generated from the user's own copy of the game and is not committed.
param(
    [string]$Out = "$PSScriptRoot\..\output\sdk",
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type -Path "$PSScriptRoot\TypeInfo.cs" -ErrorAction Stop

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$img = [Image]::new($p.Id, $p.MainModule.BaseAddress.ToInt64(), $p.MainModule.ModuleMemorySize)
$list = [Types]::Walk([Types]::Collect($img))
if ($list.Count -lt 1000) { throw "Only $($list.Count) types found: the layout may differ in this game version." }
[Sdk]::Dump($img, $list, [IO.Path]::GetFullPath($Out))
"Written to $([IO.Path]::GetFullPath($Out))"
