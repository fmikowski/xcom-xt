# SPDX-License-Identifier: GPL-3.0-or-later
# The lists sized by memory (docs/design.md "Lists sized by memory"): each list is
# built tiny (XT_DEFS: MAX_* small, *_TOP as usual) and run twice, on a 640 KB XT
# (xt88adl -Mouse: the reference, no growth, the old "full" behaviour) and with a
# 64 KB UMB (xt88umba, XTUMB.SYS: the lists grow, nothing is full). A battle saved
# with the UMB's lists does not load where they are smaller (chain bs).
# Three builds, one after the other; logs go to build\limits\. Exit code = failures.
#   .\tools\limits.ps1
#   .\tools\limits.ps1 -Only floor640,floorumb
# Call it inside PowerShell (& .\tools\limits.ps1), not with powershell -File.
param(
    [string[]]$Only = @(),
    [int]$Lanes = 6,
    [int]$MaxMin = 45,
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\lanes.ps1"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$umb = @('DOS=UMB', 'DEVICE=A:\XTUMB.SYS E000 1000 260')
$plain = 'LIM: 230[0-9][0-9] paragraphs free at the start, UMB 0: '
$grown = 'LIM: [0-9]+ paragraphs free at the start, UMB [1-9][0-9]*: '
$loot = @('weapons 6 .*artifacts 26 points 96', 'REEQUIP stores: 128 bought items')

$groups = @(
    @{ Defs = '-dMAX_FLOOR=6 -dFLOOR_TOP=240 -dFX_DEVS=1 -dFX_DEVS_TOP=72'; Tests = @(
        @{ Name = 'floor640'; Machine = 'xt88adl'; Mouse = $true; Opts = 'DEBUG SEEALL SEED 3'; Script = 'LOOT.TXT'; Wait = 300
           Expect = @("$plain.*devices 1 floor 6 ", 'floor full: item') + $loot },
        @{ Name = 'floorumb'; Machine = 'xt88umba'; Mouse = $true; Config = $umb; Opts = 'DEBUG SEEALL SEED 3'; Script = 'LOOT.TXT'; Wait = 300
           Expect = @("$grown.*devices [1-9][0-9]* floor [1-9][0-9]+ ") + $loot
           Absent = @('floor full') },
        @{ Name = 'gren640'; Machine = 'xt88adl'; Mouse = $true; Opts = 'BATTLE SEEALL SEED 16'; Script = 'GREN.TXT'; Wait = 150
           Expect = @("$plain.*devices 1 ", 'T2: Tanaka throws at 15,20,0', 'devices full: 1 at 15,20,0', 'T2: Explosives detonate!') },
        @{ Name = 'grenumb'; Machine = 'xt88umba'; Mouse = $true; Config = $umb; Opts = 'BATTLE SEEALL SEED 16'; Script = 'GREN.TXT'; Wait = 150
           Expect = @("$grown.*devices [1-9][0-9]* ", 'T2: Tanaka throws at 15,20,0', 'T2: Explosives detonate!')
           Absent = @('devices full') },
        # A battle saved with the UMB's lists (four aliens' weapons and bodies down: more
        # than 6 floor entries in use) is refused on the 640 KB XT, and loads again with
        # the UMB. (Saved before the kills, with 6 entries or fewer, it loads on both.)
        @{ Name = 'limsave'; Machine = 'xt88umba'; Mouse = $true; Config = $umb; Opts = 'DEBUG SEEALL SEED 3'; Script = 'LIMSAVE.TXT'; Wait = 200; Chain = 'bs'
           Expect = @("$grown.*floor [1-9][0-9]+ ", 'pack FA3E.PAK', 'DEBUG Z kills', 'killed!', 'save slot 4 "lim".*: ok') },
        @{ Name = 'limload'; Machine = 'xt88adl'; Mouse = $true; Opts = 'BATTLE SEED 16'; Script = 'LIMLOAD.TXT'; Wait = 120; Chain = 'bs'
           Expect = @("$plain.*floor 6 ", 'load slot 4: ok', 'bload: lists too small here: floor ([7-9]|[1-9][0-9])/6', 'its lists need more memory') },
        @{ Name = 'limload2'; Machine = 'xt88umba'; Mouse = $true; Config = $umb; Opts = 'BATTLE SEED 16'; Script = 'LIMLOAD.TXT'; Wait = 120; Chain = 'bs'
           Expect = @('load slot 4: ok', '^bload state:')
           Absent = @('lists too small') }
    ) },
    @{ Defs = '-dMAX_CARRY=16 -dCARRY_TOP=255'; Tests = @(
        @{ Name = 'carry640'; Machine = 'xt88adl'; Mouse = $true; Opts = 'DEBUG SEEALL SEED 3'; Script = 'LOOT.TXT'; Wait = 300
           Expect = @("$plain.*carry 16 ", 'EQUIP carry pool full') + $loot },
        @{ Name = 'carryumb'; Machine = 'xt88umba'; Mouse = $true; Config = $umb; Opts = 'DEBUG SEEALL SEED 3'; Script = 'LOOT.TXT'; Wait = 300
           Expect = @("$grown.*carry [1-9][0-9]+ ") + $loot
           Absent = @('carry pool full') }
    ) },
    # The full carry pool would change BATTLE GEAR's equipment: smoke, fire and blasts on their own.
    @{ Defs = '-dMAX_SMOKE=60 -dSMOKE_TOP=432 -dMAX_FIRE=8 -dFIRE_TOP=384 -dMAX_BOOM=1 -dBOOM_TOP=64'; Tests = @(
        @{ Name = 'fire640'; Machine = 'xt88adl'; Mouse = $true; Opts = 'BATTLE GEAR SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'FIRE.TXT'; Wait = 120
           Expect = @("${plain}smoke 60 fire 8 ", 'incendiary at 15,0,1', 'effects: [0-9]+ smoke \([0-9]+ thick\), [0-8] fire') },
        @{ Name = 'fireumb'; Machine = 'xt88umba'; Mouse = $true; Config = $umb; Opts = 'BATTLE GEAR SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'FIRE.TXT'; Wait = 120
           Expect = @("${grown}smoke [0-9]+ fire [1-9][0-9]+ ", 'incendiary at 15,0,1', 'effects: [0-9]+ smoke \([0-9]+ thick\), (9|[1-9][0-9]) fire') },
        # SMDUR: a smoke grenade's cloud over 12 rounds, 66 tiles at its widest (end of turn 7).
        @{ Name = 'smoke640'; Machine = 'xt88adl'; Mouse = $true; Opts = 'BATTLE GEAR SEED 6 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'SMDUR.TXT'; Wait = 240
           Expect = @("${plain}smoke 60 ", 'Tanaka throws at 4,9,1', 'smoke at 4,9,1: 14 smoke tiles', 'T13: Turn 13')
           Absent = @('effects: 66 smoke') },
        @{ Name = 'smokeumb'; Machine = 'xt88umba'; Mouse = $true; Config = $umb; Opts = 'BATTLE GEAR SEED 6 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'SMDUR.TXT'; Wait = 240
           Expect = @("${grown}smoke [1-9][0-9][0-9] ", 'Tanaka throws at 4,9,1', 'smoke at 4,9,1: 14 smoke tiles', 'effects: 66 smoke \(7 thick\)', 'T13: Turn 13') },
        @{ Name = 'boom640'; Machine = 'xt88adl'; Mouse = $true; Opts = 'BATTLE BOOMAIM SEEALL SEED 2 MISSION JU7E.PAK RACE RME.PAK'; Script = 'BOOMAI.TXT'; Wait = 420
           Expect = @("$plain.*explosions 1$", 'terrain explodes at', 'terrain explosion list full', 'T2: Turn 2') },
        @{ Name = 'boomumb'; Machine = 'xt88umba'; Mouse = $true; Config = $umb; Opts = 'BATTLE BOOMAIM SEEALL SEED 2 MISSION JU7E.PAK RACE RME.PAK'; Script = 'BOOMAI.TXT'; Wait = 420
           Expect = @("$grown.*explosions [1-9][0-9]*$", 'terrain explodes at', 'T2: Turn 2')
           Absent = @('terrain explosion list full') }
    ) }
)

$fails = 0
$saved = $env:XT_DEFS
try {
    foreach ($g in $groups) {
        if ($Only.Count -and -not @($g.Tests | Where-Object { $Only -contains $_.Name }).Count) { continue }
        $env:XT_DEFS = $g.Defs
        Write-Host "XT_DEFS=$($g.Defs)"
        $r = Invoke-TestLanes -Tests $g.Tests -OutDir "$root\build\limits" -Only $Only -Lanes $Lanes -MaxMin $MaxMin -SevenZip $SevenZip -Python $Python
        $fails += [int]@($r)[-1]
    }
} finally {
    $env:XT_DEFS = $saved
    & "$root\build.ps1" *> $null        # the normal build again
}
exit $fails
