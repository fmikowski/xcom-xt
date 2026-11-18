# SPDX-License-Identifier: GPL-3.0-or-later
# Screenshot runner for menu work: each entry is a name and a KEYS line; it writes
# tests\Z_<name>.TXT, runs them in lanes (tools\lanes.ps1) and leaves the final
# screen in build\shots\<name>\shot.png.
#   . .\tools\shots.ps1; Run-Shots @{ geo = 'WAIT 5'; base = "'B WAIT 5" } -Opts 'SEED 2' [-Cga]
param()
. "$PSScriptRoot\lanes.ps1"
function Run-Shots([hashtable]$Keys, [string]$Opts = 'DEBUG SEED 2', [int]$Lanes = 4, [switch]$Cga) {
    $root = (Resolve-Path "$PSScriptRoot\..").Path
    $tests = @()
    foreach ($k in $Keys.Keys) {
        $f = "Z_$k.TXT"
        $text = "AUTO N 2 ENTER`r`n" + $Keys[$k] + " WAIT 20`r`n"
        [IO.File]::WriteAllText("$root\tests\$f", $text)
        $tests += @{ Name = $k; Machine = $(if ($Cga) { 'xt88cga' } else { 'xt88ega' }); Opts = $Opts; Script = $f; Wait = 240
                     Expect = @('script finished') }
    }
    Invoke-TestLanes -Tests $tests -OutDir "$root\build\shots" -Lanes $Lanes -MaxMin 40 | Out-Null
}
