# SPDX-License-Identifier: GPL-3.0-or-later
# Parallel test lanes, shared by tools\smoke.ps1 and tools\major.ps1 (dot-source it).
#
#   Invoke-TestLanes -Tests $tests -OutDir build\smoke -Lanes 4
#
# Every test runs tools\emutest.ps1 -Hdd -Run "<Opts> KEYS <Script> LOG LOG.TXT" in
# its own lane: a copy of emu\ under build\lanes\<n>\emu (own 86Box machine folder,
# hdd.img and boot floppy), so lanes do not disturb each other. The program is built
# once up front; tests that need their own build (Cpu, OvlArea, NoOvlFix) build into
# their lane. A lane takes the next unit of work when it is free (longest first).
# Early stop: each test's expected lines are handed to emutest as -UntilLog, so a
# run ends a few seconds after its last expected line, -Wait is the maximum.
#
# A test is a hashtable:
#   Name, Machine, Opts, Script (a tests\ file), Wait (maximum seconds), Expect
#   (regexes, matched in this order on LOG.TXT lines)
#   optional: Same, Cmp (see smoke.ps1), Until (regexes for the early stop; default
#   Expect, plus 'script finished' when Same / Cmp compare up to it), Mouse (switch),
#   Pre, Config, FreeKB, Cpu, ExeName, OvlArea, NoOvlFix, Extra (more files for the disk),
#   Folders (the release's layout with only these mode folders: emutest -Folders),
#   Absent (regexes no LOG line may match),
#   Chain ('name': tests with the same Chain run in a lane one after the other, in
#   table order, and each gets every *.SAV the earlier ones left on the disk),
#   Timing (frame times matter: runs alone, after everything else, no other 86Box
#   running), Show (regex: a LOG line to print with the result, e.g. INTRO's frames).
#
# -Cpu 186|286 (major.ps1 / vgashots.ps1 -Cpu): the shared build is build.ps1 -Cpu n (build\<n>),
# so every row without its own Cpu runs that program (XCOM286.EXE's build: 286).
#
# -Board EMS|XMS (smoke.ps1 / major.ps1 -Board): the EGA rows run on xt88umba (EGA, AdLib,
# Lo-tech EMS board) with the board's EMS driver (LTEMM.EXE), for XMS also EXMS86.SYS !!
# (emu\drivers, README "Extended memory"), and must log `XMEM: EMS` / `XMEM: XMS` (the read
# cache on). The UMB rows (XTUMB.SYS) get EXMS86's UMB (USE!UMBS.SYS E000-EFFF, DOS=UMB)
# with XMS and are left out with EMS; CGA and Sound Blaster rows are left out. With XMS and no
# UMB the cache stays off (`XMEM: no read cache (no upper memory`).

function Get-RunLog([string]$img, [string]$dir, [string]$sevenZip, [string]$logName = 'LOG.TXT') {
    $log = "$dir\$logName"
    if (Test-Path $log) { Remove-Item $log }
    for ($i = 0; $i -lt 6 -and -not (Test-Path $log); $i++) {
        if ($i) { Start-Sleep 2 }
        $save = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $sevenZip e -y "-o$dir" $img $logName 2>&1 | Out-Null
        $ErrorActionPreference = $save
    }
    if (Test-Path $log) { return @(Get-Content $log) }
    return $null
}

# The expected-line checks of one test: returns the list of what is missing.
function Test-RunLog($t, $lines, [string]$dir, [string]$root, [string]$python) {
    $missing = @()
    if ($null -eq $lines) { return @('(no LOG.TXT)') }
    # Expected lines in order: each one after the previous match.
    $at = 0
    foreach ($e in $t.Expect) {
        $found = -1
        for ($k = $at; $k -lt $lines.Count; $k++) {
            if ($lines[$k] -match $e) { $found = $k; break }
        }
        if ($found -lt 0) { $missing += $e } else { $at = $found + 1 }
    }
    # Lines that must say the same after their prefixes (save / load).
    if ($t.Same) {
        $a = @($lines | Where-Object { $_.StartsWith($t.Same[0]) } | Select-Object -Last 1)
        $b = @($lines | Where-Object { $_.StartsWith($t.Same[1]) } | Select-Object -Last 1)
        if ($a.Count -and $b.Count -and
            $a[0].Substring($t.Same[0].Length) -ne $b[0].Substring($t.Same[1].Length)) {
            $missing += "'$($a[0])' and '$($b[0])' differ"
        }
    }
    # Stretches of the LOG that must be the same (tools\logsame.py).
    foreach ($c in @($t.Cmp)) {
        if (-not $c) { continue }
        $save = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $o = & $python -I "$root\tools\logsame.py" "$dir\LOG.TXT" @($c | ForEach-Object { "$_" })
        $code = $LASTEXITCODE
        $ErrorActionPreference = $save
        if ($code) { $missing += "logsame $($c -join ' '): $($o -join ' ')" }
    }
    # Lines that must not be there (a fallback the test rules out).
    foreach ($e in @($t.Absent)) {
        if ($e -and @($lines | Where-Object { $_ -match $e }).Count) { $missing += "(absent) $e" }
    }
    return $missing
}

# One unit of work (a chain, or a single test) in lane $slot. Runs in a job.
# Returns one result object per test.
function Invoke-LaneUnit($ctx, $unit, [int]$slot) {
    $root = $ctx.Root
    $emu = "$($ctx.Lanes)\$slot\emu"
    $saves = @{}      # chain: SAV files left by the earlier tests, name -> path
    foreach ($t in $unit) {
        $dir = "$($ctx.OutDir)\$($t.Name)"
        New-Item -ItemType Directory -Force $dir | Out-Null
        $run = "$($t.Opts) KEYS $($t.Script) LOG LOG.TXT"
        $until = if ($t.Until) { @($t.Until) } else {
            $u = @($t.Expect)
            if (($t.Same -or $t.Cmp) -and $u[-1] -ne 'script finished') { $u += 'script finished' }
            $u
        }
        $extra = @("$root\tests\$($t.Script)") + @($t.Extra | Where-Object { $_ }) + @($saves.Values)
        $p = @{
            Machine = $t.Machine; Hdd = $true; Run = $run; Extra = $extra; WaitSec = [int]$t.Wait
            Out = "$dir\shot.png"; EmuRoot = $emu; SevenZip = $ctx.SevenZip
            Python = $ctx.Python
        }
        if (-not $ctx.NoEarlyStop) { $p.UntilLog = $until }
        if ($t.Mouse) { $p.Mouse = $true }
        if ($t.Pre) { $p.Pre = $t.Pre }
        if ($t.Config) { $p.Config = @($t.Config) }
        if ($t.FreeKB) { $p.FreeKB = [int]$t.FreeKB }
        if ($t.Folders) { $p.Folders = @($t.Folders) }
        if ($t.HddDir) { $p.HddDir = "$($t.HddDir)" }   # C: is that folder (an installed release)
        if ($t.Cpu) { $p.Cpu = "$($t.Cpu)" }
        if ($t.ExeName) { $p.ExeName = "$($t.ExeName)" }
        if ($t.OvlArea) { $p.OvlArea = [int]$t.OvlArea }
        if ($t.NoOvlFix) { $p.NoOvlFix = $true }
        if (-not ($t.Cpu -or $t.OvlArea -or $t.NoOvlFix)) { $p.Exe = $ctx.Exe }
        if ($ctx.Board) {
            $p.Machine = 'xt88umba'
            $drv = @('DEVICE=A:\LTEMM.EXE /p:E000 /i:260')
            if ($ctx.Board -eq 'XMS') { $drv += 'DEVICE=A:\EXMS86.SYS !!' }
            if ($t.Config -and ($t.Config -join ' ') -match 'XTUMB') { $drv += @('DEVICE=A:\USE!UMBS.SYS E000-EFFF', 'DOS=UMB') }
            $p.Config = $drv
        }
        $t0 = Get-Date
        $note = ''
        $info = @()
        try {
            $msg = & "$root\tools\emutest.ps1" @p *>&1 | ForEach-Object { "$_" }
            $m = @($msg | Where-Object { $_ -match '^Log (matched|did not)' })
            if ($m.Count) { $note = $m[-1] }
            $lines = Get-RunLog "$emu\hdd.img" $dir $ctx.SevenZip
            $missing = @(Test-RunLog $t $lines $dir $root $ctx.Python)
            if ($t.Show -and $lines) { $info = @($lines | Where-Object { $_ -match $t.Show }) }
        } catch {
            $missing = @("emutest failed: $($_.Exception.Message)")
        }
        if ($t.Chain) {
            $save = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            & $ctx.SevenZip e -y "-o$dir" "$emu\hdd.img" '*.SAV' 2>&1 | Out-Null
            $ErrorActionPreference = $save
            foreach ($f in Get-ChildItem "$dir\*.SAV" -ErrorAction SilentlyContinue) { $saves[$f.Name] = $f.FullName }
        }
        [pscustomobject]@{
            Name = $t.Name; Script = $t.Script; Run = $run; Missing = $missing; Slot = $slot
            Secs = [int]((Get-Date) - $t0).TotalSeconds; Note = $note; Info = $info
        }
    }
}

function Invoke-TestLanes {
    param(
        [Parameter(Mandatory)] $Tests,
        [Parameter(Mandatory)] [string]$OutDir,
        [string[]]$Only = @(),
        [int]$Lanes = 4,
        [int]$MaxMin = 0,      # stop and report what is left as unfinished after this many minutes
        [switch]$NoEarlyStop,
        [ValidateSet('', 'EMS', 'XMS')] [string]$Board = '',
        [ValidateSet('', '86', '186', '286')] [string]$Cpu = '',   # the shared build's CPU (default 86)
        [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
        [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
    )
    $root = (Resolve-Path "$PSScriptRoot\..").Path
    $clock = [Diagnostics.Stopwatch]::StartNew()
    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $OutDir = (Resolve-Path $OutDir).Path

    # Which tests: -Only names, and for a chained test the ones before it in its chain.
    $sel = @()
    foreach ($t in $Tests) {
        if (-not $Only.Count -or $Only -contains $t.Name) { $sel += $t; continue }
        if ($t.Chain) {
            $later = @($Tests | Where-Object { $_.Chain -eq $t.Chain -and $Only -contains $_.Name -and
                                              [array]::IndexOf($Tests, $_) -gt [array]::IndexOf($Tests, $t) })
            if ($later.Count) { $sel += $t }
        }
    }
    if ($Board) {
        # The board machine has EGA and AdLib; the cache must be on.
        $sel = @($sel | Where-Object { @('xt88ega', 'xt88adl', 'xt88umba') -contains $_.Machine -and
                                       -not ($Board -eq 'EMS' -and (@($_.Config) -join ' ') -match 'XTUMB') } | ForEach-Object {
            $c = $_.Clone()
            $e = @($c.Expect | ForEach-Object { $_ -replace '^UMB: \d+ paragraphs', 'UMB: \d+ paragraphs' })   # EXMS86's UMB is 1 paragraph smaller than XTUMB's
            $k = if ($e.Count -and $e[0] -match '^UMB:') { 1 } else { 0 }   # the UMB plan logs before XMEM
            # XMS without a UMB: no cache (its code never goes low, src/xmem.c).
            $x = if ($Board -eq 'XMS' -and (@($_.Config) -join ' ') -notmatch 'XTUMB') { '^XMEM: no read cache \(no upper memory' } else { "^XMEM: $Board" }
            $c.Expect = @($e | Select-Object -First $k) + @($x) + @($e | Select-Object -Skip $k)
            # EXMS86 sits low (7 KB less conventional memory than XTUMB's machine): with a 64 KB UMB
            # the terror battle's floor pictures fall back to marks, as designed.
            # (NOXMEM there gives the same: tests the cache does not cause it.)
            if ($Board -eq 'XMS') {
                if ($c.Absent) { $c.Absent = @($c.Absent | Where-Object { $_ -ne 'do not fit' }) }
                $c.Expect = @($c.Expect | ForEach-Object { $_ -replace '^floor pictures: \[0-9\]\+ of$', 'floor pictures: ' })
            }
            # The drivers' load at boot (LTEMM, EXMS86): rows timed to the second elsewhere.
            $c.Wait = [int]$c.Wait + 30
            $c
        })
    }
    if (-not $sel.Count) { Write-Host 'No test selected.'; return 1 }

    # Units: a chain in table order, or one test. Timing-sensitive units run last, alone.
    $units = @()
    $seen = @{}
    foreach ($t in $sel) {
        if ($t.Chain) {
            if ($seen[$t.Chain]) { continue }
            $seen[$t.Chain] = $true
            $units += [pscustomobject]@{ Tests = @($sel | Where-Object { $_.Chain -eq $t.Chain }) }
        } else {
            $units += [pscustomobject]@{ Tests = @($t) }
        }
    }
    $timing = @($units | Where-Object { @($_.Tests | Where-Object { $_.Timing }).Count })
    $normal = @($units | Where-Object { @($_.Tests | Where-Object { $_.Timing }).Count -eq 0 })
    $normal = @($normal | Sort-Object { -(@($_.Tests | ForEach-Object { [int]$_.Wait }) | Measure-Object -Sum).Sum })
    $nLanes = [Math]::Max(1, [Math]::Min($Lanes, $normal.Count))

    # One build for everyone (tests with their own Cpu / OvlArea build in their lane).
    $lanesDir = "$root\build\lanes"
    New-Item -ItemType Directory -Force $lanesDir | Out-Null
    $needBuild = @($sel | Where-Object { -not ($_.Cpu -or $_.OvlArea -or $_.NoOvlFix) }).Count
    $bc = if ($Cpu) { $Cpu } else { '86' }
    $exe = "$root\build\$bc\xcomxt.exe"
    if ($needBuild) {
        & "$root\build.ps1" -Cpu $bc *> "$OutDir\build.log"
        if (-not (Test-Path $exe)) { throw "build failed, see $OutDir\build.log" }
    }
    Write-Host ("{0} tests in {1} lanes (build {2:N0} s)" -f $sel.Count, $nLanes, $clock.Elapsed.TotalSeconds)

    $ctx = @{ Root = $root; Lanes = $lanesDir; OutDir = $OutDir; Exe = $exe; SevenZip = $SevenZip; Python = $Python; NoEarlyStop = [bool]$NoEarlyStop; Board = $Board }
    $lib = "$PSScriptRoot\lanes.ps1"
    $sb = { param($lib, $ctx, $unit, $slot) . $lib; Invoke-LaneUnit $ctx $unit $slot }

    $results = @()
    $running = @{}      # slot -> job
    $queue = [System.Collections.Generic.Queue[object]]::new()
    foreach ($u in $normal) { $queue.Enqueue($u) }
    $phase = 'main'
    $aborted = $false
    try {
        while ($true) {
            # Start work in free lanes.
            for ($s = 1; $s -le $nLanes; $s++) {
                if (-not $running.ContainsKey($s) -and $queue.Count) {
                    $u = $queue.Dequeue()
                    $running[$s] = Start-Job -ScriptBlock $sb -ArgumentList $lib, $ctx, $u.Tests, $s
                }
            }
            if (-not $running.Count) {
                if ($phase -eq 'main' -and $timing.Count -and -not $aborted) {
                    Write-Host 'Timing-sensitive tests, alone:'
                    $phase = 'timing'
                    $nLanes = 1
                    foreach ($u in $timing) { $queue.Enqueue($u) }
                    continue
                }
                break
            }
            Start-Sleep 3
            foreach ($s in @($running.Keys)) {
                $j = $running[$s]
                $new = @(Receive-Job $j -ErrorAction Continue)
                foreach ($r in $new) {
                    if ($r.PSObject.Properties['Missing']) { $results += $r; Write-TestResult $r }
                    else { Write-Host "  [lane $s] $r" }
                }
                if ($j.State -ne 'Running') {
                    if ($j.State -eq 'Failed') { Write-Host "  [lane $s] job failed: $($j.ChildJobs[0].JobStateInfo.Reason.Message)" }
                    Remove-Job $j -Force
                    $running.Remove($s)
                }
            }
            if ($MaxMin -gt 0 -and $clock.Elapsed.TotalMinutes -gt $MaxMin) {
                Write-Host "Time limit of $MaxMin minutes reached: stopping."
                $aborted = $true
                foreach ($j in $running.Values) { Stop-Job $j; Remove-Job $j -Force }
                $running.Clear()
                $queue.Clear()
                break
            }
        }
    } finally {
        foreach ($j in $running.Values) { Stop-Job $j; Remove-Job $j -Force }
    }

    $done = @($results | ForEach-Object { $_.Name })
    $notRun = @($sel | Where-Object { $done -notcontains $_.Name } | ForEach-Object { $_.Name })
    foreach ($n in $notRun) { Write-Host ("ABORT {0,-9} not finished (time limit)" -f $n) }
    $fail = @($results | Where-Object { $_.Missing.Count }).Count + $notRun.Count
    Write-Host ("{0} of {1} passed, wall-clock {2:N1} minutes ({3:N0} s)" -f
        ($sel.Count - $fail), $sel.Count, $clock.Elapsed.TotalMinutes, $clock.Elapsed.TotalSeconds)
    return $fail
}

function Write-TestResult($r) {
    $tail = if ($r.Note) { "  ($($r.Secs) s; $($r.Note))" } else { "  ($($r.Secs) s)" }
    if ($r.Missing.Count) {
        Write-Host ("FAIL  {0,-9} {1} ({2})$tail" -f $r.Name, $r.Script, $r.Run)
        foreach ($m in $r.Missing) { Write-Host "        missing: $m" }
    } else {
        Write-Host ("PASS  {0,-9} {1}$tail" -f $r.Name, $r.Script)
    }
    foreach ($i in $r.Info) { Write-Host "        $i" }
}
