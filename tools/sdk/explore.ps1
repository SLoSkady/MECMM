# Research helper for the SDK dumper: walks the global TypeInfo list, prints statistics per
# kind and raw dumps of a few type infos and their field arrays, to work out the layouts.
param([string]$Process = "MirrorsEdgeCatalyst",
      [string[]]$Show = @("PamReplayEntityData", "Vec3", "LinearTransform"))

Add-Type -Path "$PSScriptRoot\TypeInfo.cs" -ErrorAction Stop

$p = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$img = [Image]::new($p.Id, $p.MainModule.BaseAddress.ToInt64(), $p.MainModule.ModuleMemorySize)
$nodes = [Types]::Collect($img)
$list = [Types]::Walk($nodes)
"Candidate nodes: $($nodes.Count), longest list: $($list.Count), head exe+{0:X}" -f ($list[0].Address - $img.Base)
$list | Group-Object Kind | Sort-Object { [int]$_.Name } | ForEach-Object {
    $named = $_.Group | Where-Object Name | Select-Object -First 5 | ForEach-Object Name
    "kind {0,2}: {1,5}   e.g. {2}" -f $_.Name, $_.Count, ($named -join ', ')
}

function Raw([long]$a, [int]$len) {
    for ($o = 0; $o -lt $len; $o += 8) {
        $q = $img.Q($a + $o)
        $n1 = if ($img.Contains($q)) { $img.Name($q) }
        $n2 = if ($img.Contains($q) -and -not $n1) { $img.Name($img.Q($q)) }
        $where = if ($img.Contains($q)) { "exe+{0:X}" -f ($q - $img.Base) } else { "" }
        "  +{0:X2} {1:X16} {2,-13} {3}" -f $o, $q, $where, $(if ($n1) { "`"$n1`"" } elseif ($n2) { "*->`"$n2`"" } else { "" })
    }
}

foreach ($n in $Show) {
    $t = $list | Where-Object Name -eq $n | Select-Object -First 1
    if (-not $t) { "--- $n not found"; continue }
    "--- $n  TypeInfo exe+{0:X}  info exe+{1:X}  kind {2}  size 0x{3:X}" -f ($t.Address - $img.Base), ($t.Info - $img.Base), $t.Kind, $t.Size
    Raw $t.Info 0x38
    $fields = $img.Q($t.Info + 0x30)
    if ($img.Contains($fields)) { "  fields at exe+{0:X}:" -f ($fields - $img.Base); Raw $fields 0x80 }
}
