# SPDX-License-Identifier: GPL-3.0-or-later
# Screenshots of the VGA screens outside the battle (docs/design.md, "VGA outside the
# battle"; tests/README.md): every tests\VGS*.TXT script and the special rows below on
# u286 (-Mouse -FreeKB 557) in lanes (tools\lanes.ps1); the last screen of each run is
# build\vgashots\<name>\shot.png, its LOG next to it. Look at the pictures yourself.
#   .\tools\vgashots.ps1                 all of them (5 lanes, about 6 minutes)
#   .\tools\vgashots.ps1 VGSGEO VGSBASE  some of them
#   .\tools\vgashots.ps1 -Cpu 286 VGSGEO  with the 286 build (XCOM286.EXE's), in build\vgashots-286\
param([ValidateSet('', '86', '186', '286')] [string]$Cpu = '', [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Names = @())
Set-ExecutionPolicy -Scope Process Bypass -Force
$root = (Resolve-Path "$PSScriptRoot\..").Path
. "$root\tools\lanes.ps1"
$crash = 'VGA DEBUG SEEALL SEED 2'
$special = [ordered]@{
    VGSAIR   = @{ Script = 'AIR.TXT'; Opts = 'VGA SEED 2'; Expect = @('AIR Intercepting UFO-1') }
    VGSBAT   = @{ Script = 'SMCRASH.TXT'; Opts = $crash; Expect = @('^BRIEF ', 'craft at') }
    VGSUR    = @{ Script = 'ONE.TXT'; Opts = 'BATTLE MISSION UR0V.PAK RACE RSV.PAK VGA SEED 2'; Expect = @('craft at') }
    VGSUBASE = @{ Script = 'ONE.TXT'; Opts = 'BATTLE MISSION UBASEV.PAK RACE RMV.PAK VGA SEED 2'; Expect = @('craft at') }
    VGSEGA   = @{ Script = 'VGSGEO.TXT'; Opts = 'DEBUG SEED 2' }      # the same geoscape in EGA on u286
    VGSMONTH = @{ Opts = 'VGA SEED 2'; Expect = @('REPORT rating'); Wait = 480 }
    VGSBRIEF = @{ Opts = $crash; Expect = @('^BRIEF ', 'script finished') }
    VGSBDEF  = @{ Script = 'BASEDEF.TXT'; Opts = 'VGA DEBUG SEED 541'; Expect = @('pack BASEV.PAK', 'craft at') }
    VGSCYD   = @{ Script = 'CYD2.TXT'; Opts = 'VGA DEBUG SEEALL CYDONIA SEED 541'; Expect = @('pack CYD2V.PAK', 'craft at'); Wait = 400 }
    VGSRET   = @{ Opts = $crash; Expect = @('^BRIEF ', '^result ', 'script finished') }
    VGSRET2  = @{ Opts = $crash; Expect = @('^BRIEF ', '^result ', 'script finished') }
    VGSEND   = @{ Opts = 'VGA DEBUG SEED 2 ENDTEST 1'; Expect = @('END victory', 'END slide 2') }
    VGSLOSE  = @{ Script = 'VGSEND.TXT'; Opts = 'VGA DEBUG SEED 2 ENDTEST 0'; Expect = @('END defeat', 'END slide 2') }
    VGSINTRO = @{ Script = 'VGSINT.TXT'; Opts = 'VGA DEBUG SEED 2 INTROTIME'; Expect = @('INTRO \d+ of \d+ frames shown'); Wait = 240 }
    VGSINTE  = @{ Script = 'VGSINT.TXT'; Opts = 'DEBUG SEED 2 INTROTIME'; Expect = @('INTRO \d+ of \d+ frames shown'); Wait = 240 }      # the EGA intro, for comparison
    VGSEND2  = @{ Opts = 'VGA DEBUG SEED 2 ENDTEST 1'; Expect = @('END slide 1', 'script finished') }
}
$all = @(Get-ChildItem "$root\tests\VGS*.TXT" | ForEach-Object { $_.BaseName }) + @($special.Keys) |
    Select-Object -Unique
$sel = @($Names)
if ($sel.Count) { $all = $all | Where-Object { $sel -contains $_ } }
$tests = @($all | ForEach-Object {
    $t = @{ Name = $_; Machine = 'u286'; Opts = 'VGA DEBUG SEED 2'; Script = "$_.TXT"; Wait = 300
            Mouse = $true; FreeKB = 557; Expect = @('script finished') }
    if ($special[$_]) { foreach ($k in $special[$_].Keys) { $t[$k] = $special[$_][$k] } }
    $t })
$outDir = if ($Cpu -and $Cpu -ne '86') { "$root\build\vgashots-$Cpu" } else { "$root\build\vgashots" }
Invoke-TestLanes -Tests $tests -OutDir $outDir -Lanes 5 -MaxMin 40 -Cpu $Cpu