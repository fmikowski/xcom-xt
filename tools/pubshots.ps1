# SPDX-License-Identifier: GPL-3.0-or-later
# The public screenshots (release\screenshots\, shown by the repo's README): five screens in
# each video mode, run in 86Box lanes (tools\lanes.ps1) from an installed release, then
# cut out of the 86Box window by tools\pubshots.py.
#   .\tools\pubshots.ps1 -Install build\reltest      EGA\ CGA\ VGA\ under it, each an installed
#                                                    release with that mode folder only
#   .\tools\pubshots.ps1 -Install build\reltest ega_*   some of them
param(
    [Parameter(Mandatory)] [string]$Install,
    [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Only = @(),
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
Set-ExecutionPolicy -Scope Process Bypass -Force
$root = (Resolve-Path "$PSScriptRoot\..").Path
. "$root\tools\lanes.ps1"
$inst = (Resolve-Path $Install).Path
$modes = @(
    @{ M = 'ega';  Machine = 'xt88ega';  Dir = 'EGA'; L = 'E' }
    @{ M = 'cga';  Machine = 'xt88cga';  Dir = 'CGA'; L = 'C' }
    @{ M = 'herc'; Machine = 'xt88herc'; Dir = 'CGA'; L = 'C' }
    @{ M = 'vga';  Machine = 'u286';     Dir = 'VGA'; L = 'V'; Exe = 'XCOM286' }
)
# geoscape: zoom in and out once, so the clock box has drawn the funds
$bat = 'BATTLE SEEALL SEED 8 MISSION FA1#.PAK RACE RS#.PAK'
$scenes = @(
    @{ S = 'geoscape';  Script = 'SHOTGEO.TXT'; Opts = 'SEED 2' }
    @{ S = 'base';      Script = 'VGSBASE.TXT'; Opts = 'SEED 2' }
    @{ S = 'battle';    Script = 'SHOTBAT.TXT'; Opts = $bat }
    @{ S = 'alien';     Script = 'SHOTALN.TXT'; Opts = $bat }
    @{ S = 'inventory'; Script = 'SHOTINV.TXT'; Opts = $bat }
)
$tests = @()
foreach ($m in $modes) { foreach ($s in $scenes) {
    $n = "$($m.M)_$($s.S)"
    if ($Only.Count -and -not ($Only | Where-Object { $n -like $_ })) { continue }
    $t = @{ Name = $n; Machine = $m.Machine; HddDir = "$inst\$($m.Dir)"; Opts = $s.Opts.Replace('#', $m.L)
            Script = $s.Script; Wait = 240; Expect = @('script finished') }
    if ($m.Exe) { $t.ExeName = $m.Exe }
    $tests += $t } }
$out = "$root\build\pubshots"
$fail = Invoke-TestLanes -Tests $tests -OutDir $out -Lanes 5 -MaxMin 30
$pairs = @($tests | Where-Object { Test-Path "$out\$($_.Name)\shot.png" } |
    ForEach-Object { "$out\$($_.Name)\shot.png=$($_.Name.Replace('_', '-')).png" })
& $Python -I "$root\tools\pubshots.py" "$root\release\screenshots" @pairs
"failed: $fail"
