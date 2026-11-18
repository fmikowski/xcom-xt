# SPDX-License-Identifier: GPL-3.0-or-later
# Do two programs behave the same? The cases of tools\optcases.json (seeded, scripted runs:
# battles in EGA, CGA and Hercules, alien turns, effects, night, a door, a battle save and
# load, a campaign battle, the geoscape and the base screens) run with both, and per case
# the verdict is SAME or DIFFERENT (docs\opt-baseline.md). 8086 program only.
#
#   & .\tools\optsame.ps1 -Old build\opt\base\xcomxt.exe -New build\86\xcomxt.exe
#   -Cases a,b   only these cases
#   -Lanes n     emulators at once (default 12)
#   -OutDir d    default build\opt\same: <case>-old\ and <case>-new\ (LOG.TXT, BENCH.TXT,
#                MAPDUMP.BIN, *.SAV, XCOMXT.CFG, shot.png, run.txt), same.txt, same.json
#   -TableOnly   no runs: the verdicts again from what is in OutDir
#
# Compared (tools\optsame.py): the game's LOG and BENCH.TXT with times, disk reads and free
# memory masked; BENCH's picture check sums; the end screenshot (the emulated screen); every
# save file, XCOMXT.CFG and BENCH's map dump byte for byte. The runs are the plain game: no
# time stamps (tools\lstamp.asm is optbench's). Exit code 0 = every case SAME.
# Not at the same time as tools\optbench.ps1 in the same worktree: both use build\opt\lanes.
param(
    [Parameter(Mandatory)] [string]$Old,
    [Parameter(Mandatory)] [string]$New,
    [string[]]$Cases = @(),
    [int]$Lanes = 12,
    [string]$OutDir = '',
    [switch]$TableOnly,
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
. "$PSScriptRoot\optlib.ps1"
if (-not $OutDir) { $OutDir = "$root\build\opt\same" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$all = Get-OptCases $root
if (-not $Cases.Count) { $Cases = @($all.Keys) }
foreach ($c in $Cases) { if (-not $all.Contains($c)) { throw "no case $c (tools\optcases.json: $(@($all.Keys) -join ', '))" } }

function Get-Verdict {
    $save = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $o = & $Python -I "$root\tools\optsame.py" $OutDir --json "$OutDir\same.json" -- old new
    $script:code = $LASTEXITCODE
    $ErrorActionPreference = $save
    return $o
}
function Invoke-Cases($names) {
    $runs = @()
    foreach ($c in $names) {
        foreach ($n in 'old', 'new') {
            $runs += @{ Case = $all[$c]; Name = "$c-$n"; Exe = "$OutDir\x$n.exe"; Dir = "$OutDir\$c-$n" }
        }
    }
    Invoke-OptRuns -Root $root -Runs $runs -Lanes $Lanes -LanesDir "$root\build\opt\lanes" -SevenZip $SevenZip -Python $Python
}

$code = 0
if (-not $TableOnly) {
    Copy-Item (Resolve-Path $Old).Path "$OutDir\xold.exe" -Force
    Copy-Item (Resolve-Path $New).Path "$OutDir\xnew.exe" -Force
    Get-ChildItem $OutDir -Directory | Where-Object { $_.Name -match '-(old|new)$' } | Remove-Item -Recurse -Force
    Invoke-Cases $Cases
}
$out = Get-Verdict
if (-not $TableOnly -and $code) {
    # A case whose only difference is the screenshot runs once more: the shot is a picture of
    # the emulator's window, and now and then that window has another size (seen once in 60
    # runs, docs\opt-baseline.md). A real difference on the screen is there again.
    $j = Get-Content -Raw "$OutDir\same.json" | ConvertFrom-Json
    $again = @($j.cases | Where-Object { -not $_.same -and @($_.why | Where-Object { $_ -notmatch '^screen: ' }).Count -eq 0 } | ForEach-Object { $_.case })
    if ($again.Count) {
        Write-Host "Only the screenshot differs in $($again -join ', '): run once more (first verdicts and shots: $OutDir\first)"
        $out | Where-Object { $_ -match '^DIFFERENT|^ +screen: ' } | ForEach-Object { Write-Host "  $_" }
        New-Item -ItemType Directory -Force "$OutDir\first" | Out-Null
        $out | Set-Content -Encoding ascii "$OutDir\first\same.txt"
        foreach ($c in $again) { foreach ($n in 'old', 'new') { Copy-Item "$OutDir\$c-$n\shot.png" "$OutDir\first\$c-$n.png" -Force } }
        Invoke-Cases $again
        $out = Get-Verdict
    }
}
$out | Tee-Object -FilePath "$OutDir\same.txt"
Write-Host "Verdicts: $OutDir\same.txt, $OutDir\same.json"
exit $code
