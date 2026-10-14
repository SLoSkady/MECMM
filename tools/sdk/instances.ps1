# Lists live objects of an engine class (see Instances.cs for how).
#   instances.ps1 -Class PamClientReplayPlayerComponent
#   instances.ps1 -Class ClientSoldierEntity -Max 20
# Needs tools/output/sdk/index.csv (run sdkdump.ps1 once).
param(
    [Parameter(Mandatory)][string]$Class,
    [int]$Max = 50,
    [int]$MaxSlotIndex = 0,     # GetType() is the first vtable slot in this engine; raise to search further
    [string]$Index = "$PSScriptRoot\..\output\sdk\index.csv",
    [string]$Process = "MirrorsEdgeCatalyst"
)

Add-Type -Path "$PSScriptRoot\TypeInfo.cs", "$PSScriptRoot\Instances.cs" -ErrorAction Stop

$row = Import-Csv $Index | Where-Object name -eq $Class | Select-Object -First 1
if (-not $row) { throw "$Class not found in $Index" }
$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$img = [Image]::new($p.Id, $p.MainModule.BaseAddress.ToInt64(), $p.MainModule.ModuleMemorySize)
$typeInfo = $img.Base + [Convert]::ToInt64($row.typeinfo.Replace("exe+", ""), 16)
"{0}: TypeInfo {1:X} (size {2})" -f $Class, $typeInfo, $row.size

$functions = [Instances]::GetTypeFunctions($img, $typeInfo)
"GetType functions: " + (($functions | ForEach-Object { "exe+{0:X}" -f ($_ - $img.Base) }) -join ", ")
$slots = [Instances]::Slots($img, $functions)
"vtable slots: " + (($slots | ForEach-Object { "exe+{0:X}" -f ($_ - $img.Base) }) -join ", ")

# The vtable starts up to MaxSlotIndex slots before the GetType() slot.
$vtables = [Collections.Generic.HashSet[long]]::new()
foreach ($s in $slots) { for ($i = 0; $i -le $MaxSlotIndex; $i++) { [void]$vtables.Add($s - 8 * $i) } }
$hits = [Instances]::Find($p.Id, $vtables, $Max)
"Objects: $($hits.Count)"
foreach ($hit in $hits) {
    $slotIndex = ($slots | ForEach-Object { ($_ - $hit[1]) / 8 } | Where-Object { $_ -ge 0 } | Sort-Object | Select-Object -First 1)
    "  {0:X}  vtable exe+{1:X} (GetType at slot {2})" -f $hit[0], ($hit[1] - $img.Base), $slotIndex
}
