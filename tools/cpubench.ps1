# SPDX-License-Identifier: GPL-3.0-or-later
# BENCH of several builds on one machine, side by side (b286 branch: the 8086, 186 and 286
# builds on u286; docs/design.md "The 286 build"). Each case x build runs (KEYS ONE.TXT) in lanes (build\lanes\<n>\emu, as
# tools\lanes.ps1), and BENCH.TXT + LOG.TXT land in build\cpubench\<case>-<build>\;
# tools\benchcmp.py prints the table (every BENCH line with a time, one column a build).
#   & .\tools\cpubench.ps1                         all cases, builds 86 186 286 (builds them)
#   & .\tools\cpubench.ps1 -Cases vju7,eju7 -Builds 286
#   & .\tools\cpubench.ps1 -Exe @{ t1 = 'build\t1\xcomxt.exe' } -Cases vfa1   a build of your own
param(
    [string[]]$Cases = @(),
    [string[]]$Builds = @('86', '186', '286'),
    [System.Collections.IDictionary]$Exe = @{},   # more builds: name -> xcomxt.exe (not built here; [ordered] keeps the columns in order)
    [string]$Machine = 'u286',
    [int]$Lanes = 6,
    [int]$Wait = 360,
    [string]$OutDir = '',
    [switch]$NoBuild,
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
if (-not $OutDir) { $OutDir = "$root\build\cpubench" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path     # the jobs run elsewhere
# Options, the BENCH.TXT line that ends the run (or LOG.TXT's for the a* cases: an alien turn
# without BENCH, which leaves its battle waiting for a key); a 4th item: the KEYS script.
$all = [ordered]@{
    efa1  = @('BENCH BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK', 'BENCH.TXT', '^night light: off')
    eju7  = @('BENCH BATTLE SEED 8 MISSION JU7E.PAK RACE RME.PAK', 'BENCH.TXT', '^night light: off')
    vfa1  = @('BENCH BATTLE SEED 8 MISSION FA1V.PAK RACE RSV.PAK VGA', 'BENCH.TXT', "^VGA pictures' step")
    vju7  = @('BENCH BATTLE SEED 8 MISSION JU7V.PAK RACE RMV.PAK VGA', 'BENCH.TXT', "^VGA pictures' step")
    vfa1n = @('BENCH BATTLE NIGHT SEED 8 MISSION FA1V.PAK RACE RSV.PAK VGA', 'BENCH.TXT', '^VGA lights')
    aju7e = @('BATTLE SEED 8 MISSION JU7E.PAK RACE RME.PAK', 'LOG.TXT', 'T1: alien turn')
    aju7v = @('BATTLE SEED 8 MISSION JU7V.PAK RACE RMV.PAK VGA', 'LOG.TXT', 'T1: alien turn')
}
$more = [ordered]@{     # not in the default set
    vintro = @('VGA DEBUG SEED 2 INTROTIME', 'LOG.TXT', 'INTRO \d+ of \d+ frames shown', 'VGSINT.TXT')   # the intro in VGA
    eintro = @('EGA DEBUG SEED 2 INTROTIME', 'LOG.TXT', 'INTRO \d+ of \d+ frames shown', 'VGSINT.TXT')   # the same in EGA
    eheld  = @('BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK', 'LOG.TXT', 'held item: \d+ moves', 'HELDPIC.TXT')   # the inventory's held item
}
if (-not $Cases.Count) { $Cases = @($all.Keys) }
foreach ($k in $more.Keys) { $all[$k] = $more[$k] }
$exes = [ordered]@{}
foreach ($b in $Builds) {
    if (-not $NoBuild) {
        & "$root\build.ps1" -Cpu $b *> "$OutDir\build-$b.log"
        if ($LASTEXITCODE -or -not (Test-Path "$root\build\$b\xcomxt.exe")) { throw "build -Cpu $b failed ($OutDir\build-$b.log)" }
    }
    # A copy: a later build (smoke, memcheck) must not change the program under test.
    Copy-Item "$root\build\$b\xcomxt.exe" "$OutDir\x$b.exe"
    $exes[$b] = "$OutDir\x$b.exe"
}
foreach ($k in $Exe.Keys) { Copy-Item (Resolve-Path $Exe[$k]).Path "$OutDir\x$k.exe"; $exes[$k] = "$OutDir\x$k.exe" }

$queue = [System.Collections.Generic.Queue[object]]::new()
foreach ($c in $Cases) { foreach ($b in $exes.Keys) { $queue.Enqueue(@{ Case = $c; Build = $b; Opts = $all[$c][0]; Log = $all[$c][1]; Until = $all[$c][2]
                                                                   Keys = $(if ($all[$c].Count -gt 3) { $all[$c][3] } else { 'ONE.TXT' }) }) } }
$sb = {
    param($root, $emu, $exePath, $opts, $dir, $machine, $wait, $sevenZip, $python, $log, $until, $keys)
    New-Item -ItemType Directory -Force $dir | Out-Null
    Remove-Item "$dir\*.TXT" -ErrorAction SilentlyContinue
    & "$root\tools\emutest.ps1" -Machine $machine -Hdd -Mouse -FreeKB 557 -Exe $exePath -EmuRoot $emu `
        -Run "$opts KEYS $keys LOG LOG.TXT" -Extra "$root\tests\$keys" -WaitSec $wait `
        -LogName $log -UntilLog $until -Out "$dir\shot.png" -SevenZip $sevenZip -Python $python *>&1 | ForEach-Object { "$_" } |
        Where-Object { $_ -match '^Log ' }
    & $sevenZip e -y "-o$dir" "$emu\hdd.img" BENCH.TXT LOG.TXT 2>&1 | Out-Null
}
$clock = [Diagnostics.Stopwatch]::StartNew()
$running = @{}
while ($queue.Count -or $running.Count) {
    for ($s = 1; $s -le $Lanes; $s++) {
        if (-not $running.ContainsKey($s) -and $queue.Count) {
            $u = $queue.Dequeue()
            $dir = "$OutDir\$($u.Case)-$($u.Build)"
            $running[$s] = @{ Job = (Start-Job -ScriptBlock $sb -ArgumentList $root, "$root\build\lanes\$s\emu", $exes[$u.Build],
                                     $u.Opts, $dir, $Machine, $Wait, $SevenZip, $Python, $u.Log, $u.Until, $u.Keys); Name = "$($u.Case)-$($u.Build)" }
        }
    }
    Start-Sleep 3
    foreach ($s in @($running.Keys)) {
        $j = $running[$s].Job
        if ($j.State -ne 'Running') {
            $o = @(Receive-Job $j -ErrorAction Continue)
            Write-Host ("{0,-12} lane {1}: {2}" -f $running[$s].Name, $s, ($o -join ' '))
            Remove-Job $j -Force
            $running.Remove($s)
        }
    }
}
Write-Host ("done in {0:N1} minutes" -f $clock.Elapsed.TotalMinutes)
& $Python -I "$root\tools\benchcmp.py" $OutDir @($Cases) -- @($exes.Keys)
