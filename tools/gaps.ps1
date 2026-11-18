# SPDX-License-Identifier: GPL-3.0-or-later
# Where the screen stands still: runs KEYS scripts in 86Box with the
# XT_RDDIAG=gap build (src/mouse.c gap_mark) and prints, per script, every
# stretch of half a second or more in which nothing on the screen changed
# (GAP lines: the time, what ended it, the file reads in it) with the LOG
# line before it, and the busy pointer's frames (BUSY lines).
#   .\tools\gaps.ps1                  all cases below
#   .\tools\gaps.ps1 -Only terror,wait some of them
#   -MinMs n   only gaps of n ms or more (default 500)
# Logs and screenshots go to build\gaps\<name>\. build\86\xcomxt.exe is
# left as the diagnostic build: the next emutest.ps1 or build.ps1 run (with
# XT_RDDIAG cleared, as this script leaves it) makes the normal one.
param(
    [string[]]$Only = @(),
    [int]$MinMs = 500,
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$out = "$root\build\gaps"
New-Item -ItemType Directory -Force $out | Out-Null

$cases = @(
    @{ Name = 'wait'; Machine = 'xt88ega'; Opts = 'BATTLE SEED 3'; Script = 'WAIT.TXT'; Wait = 300 },
    @{ Name = 'terror'; Machine = 'xt88ega'; Opts = 'BATTLE SEED 16 MISSION UR0E.PAK'; Script = 'TERROR.TXT'; Wait = 420 },
    @{ Name = 'fire'; Machine = 'xt88ega'; Opts = 'BATTLE GEAR SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'FIRE.TXT'; Wait = 120 },
    @{ Name = 'crash'; Machine = 'xt88ega'; Opts = 'DEBUG SEEALL SEED 2'; Script = 'SMCRASH.TXT'; Wait = 200 },
    @{ Name = 'save'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'SMSAVE.TXT'; Wait = 150 },
    @{ Name = 'bsave'; Machine = 'xt88ega'; Opts = 'DEBUG SEEALL SEED 3'; Script = 'BSAVE.TXT'; Wait = 540 },
    @{ Name = 'basedef'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 541'; Script = 'BASEDEF.TXT'; Wait = 300 },
    @{ Name = 'cyd2'; Machine = 'xt88sb'; Opts = 'DEBUG SEEALL CYDONIA SEED 541'; Script = 'CYD2.TXT'; Wait = 240
       Pre = 'SET BLASTER=A220 I7 D1 T1' },
    @{ Name = 'soak'; Machine = 'xt88ega'; Opts = ''; Script = 'SOAK.TXT'; Wait = 420 },
    @{ Name = 'intro'; Machine = 'xt88adl'; Opts = 'INTRO'; Script = 'INTRO.TXT'; Wait = 140 }
)

$env:XT_RDDIAG = 'gap'
try {
    foreach ($c in $cases) {
        if ($Only.Count -and $Only -notcontains $c.Name) { continue }
        $dir = "$out\$($c.Name)"
        New-Item -ItemType Directory -Force $dir | Out-Null
        $a = @{ Machine = $c.Machine; Hdd = $true; Run = "$($c.Opts) KEYS $($c.Script) LOG LOG.TXT"
                Extra = "$root\tests\$($c.Script)"; WaitSec = $c.Wait; Out = "$dir\shot.png" }
        if ($c.Pre) { $a.Pre = $c.Pre }
        & "$PSScriptRoot\emutest.ps1" @a *> $null
        $log = "$dir\LOG.TXT"
        if (Test-Path $log) { Remove-Item $log }
        for ($i = 0; $i -lt 10 -and -not (Test-Path $log); $i++) {
            Start-Sleep 2
            $ErrorActionPreference = 'Continue'
            & $SevenZip e -y "-o$dir" "$root\emu\hdd.img" LOG.TXT 2>&1 | Out-Null
            $ErrorActionPreference = 'Stop'
        }
        Write-Host "== $($c.Name) ($($c.Opts) KEYS $($c.Script))"
        if (-not (Test-Path $log)) { Write-Host '   (no LOG.TXT)'; continue }
        $lines = @(Get-Content $log)
        $prev = ''
        $busy = 0
        foreach ($l in $lines) {
            if ($l -match '^BUSY') { $busy++; continue }
            if ($l -match '^GAP (\d+) ms') {
                if ([int]$Matches[1] -ge $MinMs) { Write-Host "   $l   after: $prev" }
                continue
            }
            $prev = $l
        }
        Write-Host "   busy pointer frames: $busy; last line: $($lines[-1])"
    }
} finally {
    Remove-Item Env:XT_RDDIAG -ErrorAction SilentlyContinue
}
