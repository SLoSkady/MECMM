# Decodes Frostbite name hashes (event / field / interface ids in blueprints, e.g. 0xc2311cde)
# by hashing candidate names: every field and type name from the SDK dump plus common event words.
#   fbhash.ps1 -Hash 0xc2311cde,0xa695a747
#   fbhash.ps1 -Name OnSpawned          # print the hash of a name
param(
    [string[]]$Hash,
    [string[]]$Name,
    [string]$Sdk = "$PSScriptRoot\..\output\sdk"
)

# Frostbite string hash (as used by Frosty): h = 5381; h = (h * 33) ^ byte
Add-Type @"
public static class FbHash {
    public static uint Of(string s) {
        uint h = 5381;
        foreach (char c in s) h = unchecked(h * 33) ^ (byte)c;
        return h;
    }
}
"@
function Get-FbHash([string]$s) { [FbHash]::Of($s) }

if ($Name) { foreach ($n in $Name) { "0x{0:x8}  {1}" -f (Get-FbHash $n), $n }; return }

$words = [Collections.Generic.HashSet[string]]::new()
$common = 'Spawn','Spawned','Unspawn','Unspawned','Killed','Despawn','Play','Stop','Pause','Resume','Start','Started',
    'Stopped','Finished','Done','Enable','Disable','Activate','Deactivate','Show','Hide','In','Out','Trigger','Triggered',
    'Interact','Press','Release','HoldDown','Enter','Exit','Preview','Load','Loaded','Unload','Restart','Reset','Abort',
    'Cancel','Ready','Fail','Failed','Success','Completed','Next','Previous','Open','Close','Opened','Closed','Begin','End',
    'Update','Changed','Set','Get','Add','Remove','Clear','Toggle','Select','Selected','Focus','Unfocus','Timeout',
    'ReplayDone','ReplayStarted','ReplayStopped','StartReplay','StopReplay','PlayReplay','StartPreview','StopPreview'
foreach ($w in $common) { [void]$words.Add($w); [void]$words.Add("On$w") }
foreach ($file in "classes.txt", "valuetypes.txt") {
    foreach ($m in [regex]::Matches((Get-Content "$Sdk\$file" -Raw), '(?m)^\s+/\* 0x[0-9A-F]+ \*/ \S+\s+(\w+);|^(?:class|struct) (\w+)')) {
        [void]$words.Add($m.Groups[1].Value + $m.Groups[2].Value)
    }
}
$map = @{}
foreach ($w in $words) { if ($w) { $map[(Get-FbHash $w)] = $w } }
"($($words.Count) candidate names)"
foreach ($h in $Hash) {
    # PowerShell turns unquoted 0x... arguments into (possibly negative) decimal numbers
    $v = if ($h -match '^0x') { [Convert]::ToUInt32($h.Substring(2), 16) } else { [uint32]([int64]$h -band [int64]4294967295) }
    "0x{0:x8}  {1}" -f $v, $(if ($map.ContainsKey($v)) { $map[$v] } else { "?" })
}
