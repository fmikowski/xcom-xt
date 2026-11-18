# SPDX-License-Identifier: GPL-3.0-or-later
# How long the player waits: a fixed set of cases on the emulated XT machines, one table of
# times in ms per metric (docs\opt-baseline.md). 8086 program only.
#
#   & .\tools\optbench.ps1                                  builds the tree and measures it
#   & .\tools\optbench.ps1 -Exe build\opt\base\xcomxt.exe   a program built earlier
#   & .\tools\optbench.ps1 -Exe old.exe, new.exe            two programs side by side, the second's
#                                                           difference from the first in percent
#   -Names base,new   the columns' names (default: a, b; one program: the file's folder name)
#   -Cases a,b        only these cases (tools\optcases.json has them all)
#   -Repeat n         every run n times: the table shows the medians and their spread
#   -Lanes n          emulators at once (default 12; the times do not depend on it,
#                     docs\opt-baseline.md "Noise")
#   -OutDir d         default build\opt\bench: r<k>\<case>-<name>\ per run (LOG.TXT, BENCH.TXT,
#                     shot.png, run.txt), bench.txt (the table), bench.json, x<name>.exe
#   -TableOnly        no runs: the table again from what is in OutDir
#
# Every case is one run of tools\emutest.ps1 (-Hdd -Mouse, its machine from the case table)
# in a lane of its own under build\opt\lanes, with LSTAMP.COM (tools\lstamp.asm) loaded before
# the game: it stamps every LOG line with its time, so the metrics are read off the unchanged
# game's log (tools\optbench.py). Inside PowerShell; with `powershell -File` lists (-Exe a,b)
# do not arrive as lists.
param(
    [string[]]$Exe = @(),
    [string[]]$Names = @(),
    [string[]]$Cases = @(),
    [int]$Repeat = 1,
    [int]$Lanes = 12,
    [string]$OutDir = '',
    [switch]$TableOnly,
    [switch]$Md,
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
. "$PSScriptRoot\optlib.ps1"
if (-not $OutDir) { $OutDir = "$root\build\opt\bench" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$all = Get-OptCases $root
if (-not $Cases.Count) { $Cases = @($all.Keys | Where-Object { -not $all[$_].same_only }) }   # same_only: optsame's screens, nothing timed
foreach ($c in $Cases) { if (-not $all.Contains($c)) { throw "no case $c (tools\optcases.json: $(@($all.Keys) -join ', '))" } }
if ($Exe.Count -gt 2) { throw 'one or two programs' }

if (-not $TableOnly) {
    if (-not $Exe.Count) {
        & "$root\build.ps1" *> "$OutDir\build.log"
        if ($LASTEXITCODE -or -not (Test-Path "$root\build\86\xcomxt.exe")) { throw "build failed ($OutDir\build.log)" }
        $Exe = @("$root\build\86\xcomxt.exe")
        if (-not $Names.Count) { $Names = @('tree') }
    }
}
if (-not $Names.Count) {
    $Names = if ($Exe.Count -eq 2) { @('a', 'b') } elseif ($Exe.Count) { @((Split-Path -Leaf (Split-Path -Parent (Resolve-Path $Exe[0]).Path))) } else { throw '-TableOnly needs -Names' }
}
if (-not $TableOnly) {
    if ($Names.Count -ne $Exe.Count) { throw 'as many -Names as -Exe' }
    # Copies: a later build must not change the programs under test.
    $exes = [ordered]@{}
    for ($i = 0; $i -lt $Exe.Count; $i++) {
        Copy-Item (Resolve-Path $Exe[$i]).Path "$OutDir\x$($Names[$i]).exe" -Force
        $exes[$Names[$i]] = "$OutDir\x$($Names[$i]).exe"
    }
    Get-ChildItem $OutDir -Directory | Where-Object { $_.Name -match '^r\d+$' } | Remove-Item -Recurse -Force
    $runs = @()
    for ($k = 1; $k -le $Repeat; $k++) {
        foreach ($c in $Cases) {
            foreach ($n in $exes.Keys) {
                $runs += @{ Case = $all[$c]; Name = "$c-$n r$k"; Exe = $exes[$n]; Dir = "$OutDir\r$k\$c-$n" }
            }
        }
    }
    Invoke-OptRuns -Root $root -Runs $runs -Lanes $Lanes -LanesDir "$root\build\opt\lanes" -Stamp -SevenZip $SevenZip -Python $Python
}
$a = @("$root\tools\optbench.py", $OutDir, '--json', "$OutDir\bench.json")
if ($Md) { $a += '--md' }
$save = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $Python -I @a -- @($Names) | Tee-Object -FilePath "$OutDir\bench.txt"
$code = $LASTEXITCODE
$ErrorActionPreference = $save
Write-Host "Table: $OutDir\bench.txt, $OutDir\bench.json"
exit $code
