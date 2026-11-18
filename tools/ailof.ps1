# SPDX-License-Identifier: GPL-3.0-or-later
# Alien turn timings (docs/design.md "Alien AI"): runs built variants of the
# program on the battles of design.md's table, in parallel lanes, and keeps each LOG.
#   .\tools\ailof.ps1 -Exe p0,p1 -Maps FA4E,JU4E,UR0E [-FreeKB n] [-NoMouse] [-Turns 10] [-MaxMin 45]
# With its mouse driver (the default) the game gets 568 KB ("DOS free at start"), without 581 KB.
# A variant is build\v\<name>\xcomxt.exe, made with
#   .\build.ps1 -OutDir <tree>\build\v\<name> (options such as EXACTAI go in -Opts, with -Tag)
# Logs: build\ailof\<tag><name>-<map>.log; tools\ailof.py sums them up. A run that hits
# -MaxMin keeps the LOG it has: the turns it finished still count.
param(
    [string[]]$Exe,
    [string[]]$Maps = @('FA4E', 'JU4E', 'UR0E'),
    [int]$Turns = 10,
    [int]$FreeKB = 0,
    [switch]$NoMouse,
    [string]$Machine = 'xt88ega',
    [string]$Opts = 'BATTLE SEED 3 RACE REE.PAK',
    [string]$Tag = '',
    [int]$MaxMin = 45,
    [int]$Lanes = 6,
    [int]$LaneBase = 10
)
$root = (Resolve-Path "$PSScriptRoot\..").Path
$out = "$root\build\ailof"
New-Item -ItemType Directory -Force $out | Out-Null
$work = [System.Collections.Generic.Queue[object]]::new()
foreach ($e in $Exe) { foreach ($m in $Maps) { $work.Enqueue(@($e, $m)) } }
$sb = {
    param($root, $out, $e, $m, $Turns, $FreeKB, $Mouse, $Machine, $Opts, $Tag, $MaxMin, $slot)
    $emu = "$root\build\lanes\$slot\emu"
    $p = @{
        Machine = $Machine; Hdd = $true; Run = "$Opts MISSION $m.PAK KEYS WAIT.TXT LOG LOG.TXT"
        Extra = @("$root\tests\WAIT.TXT"); WaitSec = 60 * $MaxMin; Out = "$out\$Tag$e-$m.png"; EmuRoot = $emu
        Exe = "$root\build\v\$e\xcomxt.exe"; UntilLog = @("(^T${Turns}: alien turn|^result )"); PollSec = 20
    }
    if ($FreeKB) { $p.FreeKB = $FreeKB }
    if ($Mouse -eq 1) { $p.Mouse = $true }
    $t0 = Get-Date
    & "$root\tools\emutest.ps1" @p *> "$out\$Tag$e-$m.out"
    & 'C:\Program Files\7-Zip\7z.exe' e -y "-o$emu" "$emu\hdd.img" LOG.TXT 2>&1 | Out-Null
    Copy-Item "$emu\LOG.TXT" "$out\$Tag$e-$m.log" -Force
    "$Tag$e-$m $([int]((Get-Date) - $t0).TotalSeconds) s"
}
$running = @{}
while ($work.Count -or $running.Count) {
    for ($s = 1; $s -le $Lanes; $s++) {
        $slot = $LaneBase + $s
        if (-not $running.ContainsKey($slot) -and $work.Count) {
            $w = $work.Dequeue()
            $running[$slot] = Start-Job -ScriptBlock $sb -ArgumentList $root, $out, $w[0], $w[1], $Turns, $FreeKB, $(if ($NoMouse) { 0 } else { 1 }), $Machine, $Opts, $Tag, $MaxMin, $slot
        }
    }
    Start-Sleep 5
    foreach ($k in @($running.Keys)) {
        if ($running[$k].State -ne 'Running') {
            Receive-Job $running[$k] -ErrorAction Continue
            Remove-Job $running[$k] -Force
            $running.Remove($k)
        }
    }
}
