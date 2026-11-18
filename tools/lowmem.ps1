# SPDX-License-Identifier: GPL-3.0-or-later
# Does the game stay up when memory runs out? A plain 640 KB XT with less and less free
# (RESERVE.COM, no UMB / EMS / XMS, a mouse driver): the screens of a battle, a battle save and
# the campaign's overlay walk. A run may refuse with "out of memory"; it may not hang.
#   .\tools\lowmem.ps1 [-Levels 540,530,520,510,500] [-Lanes 6] [-MaxMin 45]
# Logs and screenshots: build\lowmem\.
param(
    [int[]]$Levels = @(540, 530, 520, 510, 500),
    [string[]]$Only = @(),
    [int]$Lanes = 6,
    [int]$MaxMin = 45
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\lanes.ps1"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$tests = @()
foreach ($kb in $Levels) {
    $tests += @{ Name = "ju7e$kb"; Machine = 'xt88adl'; Mouse = $true; FreeKB = $kb; Script = 'LOWTOUR.TXT'; Wait = 300
                 Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2'; Expect = @('script finished') }
    $tests += @{ Name = "hwp$kb"; Machine = 'xt88adl'; Mouse = $true; FreeKB = $kb; Script = 'LOWTOUR.TXT'; Wait = 300
                 Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2 HWP 1'; Expect = @('script finished') }
    $tests += @{ Name = "bsave$kb"; Machine = 'xt88adl'; Mouse = $true; FreeKB = $kb; Script = 'BSAVE.TXT'; Wait = 540
                 Opts = 'DEBUG SEEALL SEED 3'; Expect = @('script finished') }
    $tests += @{ Name = "walk$kb"; Machine = 'xt88adl'; Mouse = $true; FreeKB = $kb; Script = 'OVLWALK.TXT'; Wait = 600
                 Opts = 'DEBUG SEEALL SEED 7'; Expect = @('script finished') }
}
$fail = Invoke-TestLanes -Tests $tests -OutDir "$root\build\lowmem" -Only $Only -Lanes $Lanes -MaxMin $MaxMin
exit $fail
