# SPDX-License-Identifier: GPL-3.0-or-later
# Shared by tools\optbench.ps1 and tools\optsame.ps1 (dot-source it): the case table
# (tools\optcases.json) and a queue of emulator runs in lanes.
#
#   $cases = Get-OptCases $root            name -> case (ordered), from tools\optcases.json
#   Invoke-OptRuns -Root $root -Runs $runs -Lanes 12 -LanesDir build\opt\lanes [-Stamp]
#
# A case: machine, opts (the game's options), script (a tests\ file), log (the file whose
# lines end the run: LOG.TXT or BENCH.TXT), until (regexes, in this order, on its lines),
# wait (the most seconds), and for optbench its metrics.
# A run: @{ Case = <case>; Name = <a name for the progress line>; Exe = <xcomxt.exe>;
# Dir = <folder for its files> }.
# Each run is tools\emutest.ps1 -Hdd -Mouse in its own lane (a copy of emu\ under
# <LanesDir>\<n>\emu, as tools\lanes.ps1), stopped a few seconds after its last line; LOG.TXT,
# BENCH.TXT, MAPDUMP.BIN, every *.SAV, XCOMXT.CFG and the screenshot (shot.png) land in Dir, with run.txt
# (the seconds it took and emutest's last words). -Stamp loads LSTAMP.COM (tools\lstamp.asm)
# before the game: every LOG line then starts with its time.

function Get-OptCases([string]$root) {
    $j = Get-Content -Raw "$root\tools\optcases.json" | ConvertFrom-Json
    $cases = [ordered]@{}
    foreach ($c in $j.cases) { $cases[$c.name] = $c }
    return $cases
}

function Invoke-OptRuns {
    param(
        [Parameter(Mandatory)] [string]$Root,
        [Parameter(Mandatory)] $Runs,
        [int]$Lanes = 12,
        [Parameter(Mandatory)] [string]$LanesDir,
        [switch]$Stamp,
        [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
        [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
        [string]$Nasm = "$env:LOCALAPPDATA\bin\NASM\nasm.exe"
    )
    New-Item -ItemType Directory -Force $LanesDir | Out-Null
    $LanesDir = (Resolve-Path $LanesDir).Path
    $lstamp = ''
    if ($Stamp) {
        $lstamp = "$LanesDir\LSTAMP.COM"
        & $Nasm -f bin -o $lstamp "$Root\tools\lstamp.asm"
        if ($LASTEXITCODE) { throw 'nasm failed on lstamp.asm' }
    }
    $sb = {
        param($root, $emu, $exe, $c, $dir, $lstamp, $sevenZip, $python)
        Set-ExecutionPolicy -Scope Process Bypass -Force
        New-Item -ItemType Directory -Force $dir | Out-Null
        Remove-Item "$dir\*" -Include *.TXT, *.SAV, *.CFG, *.BIN, *.png -ErrorAction SilentlyContinue
        $p = @{ Machine = $c.machine; Hdd = $true; Mouse = $true; Exe = $exe; EmuRoot = $emu
                Run = "$($c.opts) KEYS $($c.script) LOG LOG.TXT"; Extra = "$root\tests\$($c.script)"
                WaitSec = [int]$c.wait; LogName = $c.log; UntilLog = @($c.until); Out = "$dir\shot.png"
                SevenZip = $sevenZip; Python = $python }
        if ($lstamp) {
            $p.Pre = 'A:\LSTAMP.COM'; $p.ExtraA = $lstamp
            # the LOG's lines start with their stamp
            if ($c.log -eq 'LOG.TXT') { $p.UntilLog = @($c.until | ForEach-Object { $_ -replace '^\^', '^(?:@[0-9A-F]{8} )?' }) }
        }
        # One slot of the machine for this emulator: several worktrees run these tools at the
        # same time, and more emulators than cores would run the cases into their time limits
        # (never change the numbers). A slot is a lock file held open; Windows frees it when
        # this job ends, however it ends. XT_EMU_SLOTS (default 20) is the machine's limit.
        $slots = 20
        if ($env:XT_EMU_SLOTS) { $slots = [int]$env:XT_EMU_SLOTS }
        # One folder for every checkout and worktree of this PC (XT_EMU_SLOTDIR overrides).
        $slotDir = if ($env:XT_EMU_SLOTDIR) { $env:XT_EMU_SLOTDIR } else { Join-Path $env:LOCALAPPDATA 'xcomxt\emuslots' }
        New-Item -ItemType Directory -Force $slotDir | Out-Null
        $slot = $null
        while (-not $slot) {
            for ($k = 1; $k -le $slots -and -not $slot; $k++) {
                try { $slot = [IO.File]::Open("$slotDir\slot$k.lock", 'OpenOrCreate', 'ReadWrite', 'None') } catch { }
            }
            if (-not $slot) { Start-Sleep -Milliseconds (1500 + (Get-Random -Maximum 1500)) }
        }
        $t0 = Get-Date
        $note = ''
        try {
            $msg = & "$root\tools\emutest.ps1" @p *>&1 | ForEach-Object { "$_" }
            $note = (@($msg | Where-Object { $_ -match '^Log (matched|did not)' }) | Select-Object -Last 1)
        } catch { $note = "emutest failed: $($_.Exception.Message)" }
        finally { $slot.Close() }
        for ($i = 0; $i -lt 6 -and -not (Test-Path "$dir\LOG.TXT"); $i++) {
            if ($i) { Start-Sleep 2 }
            & $sevenZip e -y "-o$dir" "$emu\hdd.img" LOG.TXT BENCH.TXT MAPDUMP.BIN '*.SAV' XCOMXT.CFG 2>&1 | Out-Null
        }
        $secs = [int]((Get-Date) - $t0).TotalSeconds
        Set-Content -Encoding ascii "$dir\run.txt" "$secs s; $note"
        "$secs s; $note"
    }
    $queue = [System.Collections.Generic.Queue[object]]::new()
    foreach ($r in @($Runs | Sort-Object { -[int]$_.Case.wait })) { $queue.Enqueue($r) }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $running = @{}
    try {
        while ($queue.Count -or $running.Count) {
            for ($s = 1; $s -le $Lanes; $s++) {
                if (-not $running.ContainsKey($s) -and $queue.Count) {
                    $u = $queue.Dequeue()
                    $c = @{}
                    foreach ($k in 'machine', 'opts', 'script', 'log', 'until', 'wait') { $c[$k] = $u.Case.$k }
                    $running[$s] = @{ Job = (Start-Job -ScriptBlock $sb -ArgumentList $Root, "$LanesDir\$s\emu", $u.Exe,
                                             $c, $u.Dir, $lstamp, $SevenZip, $Python); Name = $u.Name }
                }
            }
            Start-Sleep 3
            foreach ($s in @($running.Keys)) {
                $j = $running[$s].Job
                if ($j.State -ne 'Running') {
                    $o = @(Receive-Job $j -ErrorAction Continue)
                    Write-Host ("  {0,-22} lane {1,2}: {2}" -f $running[$s].Name, $s, ($o -join ' '))
                    Remove-Job $j -Force
                    $running.Remove($s)
                }
            }
        }
    } finally {
        foreach ($r in $running.Values) { Stop-Job $r.Job; Remove-Job $r.Job -Force }
    }
    Write-Host ("  {0} runs in {1} lanes: {2:N1} minutes" -f @($Runs).Count, $Lanes, $clock.Elapsed.TotalMinutes)
}
