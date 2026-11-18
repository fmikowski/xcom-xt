# SPDX-License-Identifier: GPL-3.0-or-later
# The checks of the fog-of-war fan (src\losasm.asm reveal_fan_asm, opt item reveal-fan),
# on the emulated XT (xt88ega), in the lanes of tools\optlib.ps1.
#
#   & .\tools\fancheck.ps1 -Old build\opt\basel\xcomxt.exe -New build\86\xcomxt.exe
#
# 1. FANTEST (tools\fantest.c): this tree's src\losasm.asm against the ray code it replaced
#    (src\losasm.asm of -OldRev, its C loop is in fantest.c), both in one DOS program, on
#    random maps: -Seeds runs of -Maps maps each, 10 fans a map. The seen tables, the boxes
#    and the results must be the same.
# 2. BENCH SHOTLOG with both programs on the farm (FA1E), the jungle battleship (JU7E, also
#    with 14 soldiers) and the terror site (UR0E): the 'fog fans' line of BENCH.TXT (every
#    soldier in all 8 facings: check sums of the tiles marked, the box and the result) must
#    be the same. -Old needs the line (commit 4d2db6d or later); without -Old / -New only
#    FANTEST runs.
#   -Lanes n     emulators at once (default 6)
#   -OutDir d    default build\opt\fan: fantest_<seed>\, <map>-old\, <map>-new\, fan.txt
# Exit code 0 = nothing differs. Not at the same time as tools\optbench.ps1 or
# tools\optsame.ps1 in the same worktree: all use build\opt\lanes.
param(
    [string]$Old = '',
    [string]$New = '',
    [string]$OldRev = 'bfde1a2',
    [int[]]$Seeds = @(1, 2, 3),
    [int]$Maps = 150,
    [int]$Lanes = 6,
    [string]$OutDir = '',
    [string]$Watcom = 'C:\WATCOM',
    [string]$Nasm = "$env:LOCALAPPDATA\bin\NASM\nasm.exe",
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
. "$PSScriptRoot\optlib.ps1"
if (-not $OutDir) { $OutDir = "$root\build\opt\fan" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# FANTEST.EXE: the old ray code under other names beside the new fan.
$env:WATCOM = $Watcom
$env:INCLUDE = "$Watcom\h"
$env:PATH = "$Watcom\binnt64;$Watcom\binnt;$env:PATH"
$save = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$oldAsm = (& git -C $root show "${OldRev}:src/losasm.asm") -join "`n"
if ($LASTEXITCODE) { throw "git show ${OldRev}:src/losasm.asm failed" }
$oldAsm = $oldAsm -creplace 'LOSASM_TEXT', 'OLDLOS_TEXT' -creplace '_reveal_reset_box', '_old_reset_box' -creplace '_reveal_get_box', '_old_get_box'
Set-Content -Encoding ascii "$OutDir\losold.asm" $oldAsm
& $Nasm -f obj '-dCPU=8086' -o "$OutDir\losold.obj" "$OutDir\losold.asm"
if ($LASTEXITCODE) { throw 'nasm failed on the old losasm.asm' }
& $Nasm -f obj '-dCPU=8086' -o "$OutDir\losasm.obj" "$root\src\losasm.asm"
if ($LASTEXITCODE) { throw 'nasm failed on src\losasm.asm' }
Push-Location $OutDir
try {
    $msg = & wcl '-q' '-bt=dos' '-ml' '-0' '-ox' '-s' '-fe=fantest.exe' "$root\tools\fantest.c" 'losold.obj' 'losasm.obj' 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE) { $msg | Out-Host; throw 'wcl failed on fantest.c' }
} finally { Pop-Location }
$ErrorActionPreference = $save

$runs = @()
foreach ($s in $Seeds) {
    $runs += @{ Case = @{ machine = 'xt88ega'; opts = "$s $Maps"; script = 'ONE.TXT'; log = 'BENCH.TXT'
                          until = @('^fantest: '); wait = 2400 }
                Name = "fantest_$s"; Exe = "$OutDir\fantest.exe"; Dir = "$OutDir\fantest_$s" }
}
$battles = [ordered]@{
    fa1 = 'BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK'
    ju7 = 'BATTLE SEED 8 MISSION JU7E.PAK RACE RME.PAK'
    ju7s = 'BATTLE SEED 8 MISSION JU7E.PAK RACE RME.PAK SQUAD 14'
    ur0 = 'BATTLE SEED 16 MISSION UR0E.PAK RACE RSE.PAK'
}
if ($Old -and $New) {
    Copy-Item (Resolve-Path $Old).Path "$OutDir\xold.exe" -Force
    Copy-Item (Resolve-Path $New).Path "$OutDir\xnew.exe" -Force
    foreach ($b in $battles.Keys) {
        foreach ($n in 'old', 'new') {
            $runs += @{ Case = @{ machine = 'xt88ega'; opts = "BENCH SHOTLOG $($battles[$b])"; script = 'ONE.TXT'
                                  log = 'BENCH.TXT'; until = @('^fog fans: '); wait = 400 }
                        Name = "$b-$n"; Exe = "$OutDir\x$n.exe"; Dir = "$OutDir\$b-$n" }
        }
    }
}
Invoke-OptRuns -Root $root -Runs $runs -Lanes $Lanes -LanesDir "$root\build\opt\lanes" -SevenZip $SevenZip -Python $Python

$bad = 0
$out = @()
foreach ($s in $Seeds) {
    $l = @(Get-Content "$OutDir\fantest_$s\BENCH.TXT" -ErrorAction SilentlyContinue | Where-Object { $_ -match '^(fantest: |DIFFERENT)' })
    $end = @($l | Where-Object { $_ -match '^fantest: ' }) | Select-Object -Last 1
    if (-not $end) { $end = 'did not finish' }
    if ($end -notmatch ', 0 differ$') { $bad++ }
    $out += "seed ${s}: $end"
    $out += @($l | Where-Object { $_ -match '^DIFFERENT' } | Select-Object -First 5 | ForEach-Object { "    $_" })
}
if ($Old -and $New) {
    foreach ($b in $battles.Keys) {
        $a, $z = 'old', 'new' | ForEach-Object {
            (@(Get-Content "$OutDir\$b-$_\BENCH.TXT" -ErrorAction SilentlyContinue | Where-Object { $_ -match '^fog fans: ' }) | Select-Object -Last 1)
        }
        $same = $a -and $a -eq $z
        if (-not $same) { $bad++ }
        $out += "{0,-5} {1}: old '{2}', new '{3}'" -f $b, $(if ($same) { 'SAME' } else { 'DIFFERENT' }), $a, $z
    }
}
$out | Tee-Object -FilePath "$OutDir\fan.txt"
exit $(if ($bad) { 1 } else { 0 })
