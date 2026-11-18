# SPDX-License-Identifier: GPL-3.0-or-later
Set-ExecutionPolicy -Scope Process Bypass -Force
# Machine variants for timing questions (boards branch, docs/design.md "Walk steps on v20e"):
# the same walk (tests\WALK4.TXT, PACELOG: a "PACE w" line per step shown) on 86Box machines
# that differ in one thing each, every run in its own EmuRoot under build\speedvar\<name>
# (emu\ is not touched), all at once. Prints each run's walk lines and its steps in ms.
#   & .\tools\speedvar.ps1                  all variants (build first: .\build.ps1)
#   $env:VARONLY = '^(A|E)_'; & .\tools\speedvar.ps1   only those
# A variant: M = the machine folder copied from emu\, Set = 86box.cfg keys replaced, AddEms /
# AddAdlib / NoEms = cards added or taken out, Cfg = CONFIG.SYS lines (none: the machine's
# own), Dos = emu\<Dos>, Pre = an AUTOEXEC line, Opt = game options before the run's.
$Only = $env:VARONLY
$root = (Resolve-Path "$PSScriptRoot\..").Path
$scratch = "$root\build\speedvar"
$exe = "$root\build\86\xcomxt.exe"
$run = 'BATTLE PACELOG SEED 8 MISSION FA1E.PAK RACE RSE.PAK KEYS WALK4.TXT LOG LOG.TXT'
$variants = @(
    @{ N = 'A_xt88ega';       M = 'xt88ega'; Set = @{} },
    @{ N = 'B_v20ega';        M = 'v20ega';  Set = @{} },
    @{ N = 'C_v20xt_8088';    M = 'v20ega';  Set = @{ cpu_family = '8088' } },
    @{ N = 'D_pcxt_v20';      M = 'xt88ega'; Set = @{ cpu_family = 'necv20' } },
    @{ N = 'E_v20e';          M = 'v20e';    Set = @{} },
    @{ N = 'F_v20e_pcxt8088'; M = 'v20e';    Set = @{ cpu_family = '8088'; machine = 'pcxt' } },
    @{ N = 'G_v20e_nodrv';    M = 'v20e';    Set = @{}; Cfg = @('FILES=30') },
    @{ N = 'H_v20e_dos5';     M = 'v20e';    Set = @{}; Dos = 'dos5' },
    @{ N = 'I_v20e_dos5_nodrv'; M = 'v20e';  Set = @{}; Dos = 'dos5'; Cfg = @('FILES=30') },
    @{ N = 'J_v20e_noems_nodrv'; M = 'v20e'; Set = @{}; Dos = 'dos5'; Cfg = @('FILES=30'); NoEms = $true },
    @{ N = 'K_v20ega_ems';    M = 'v20ega';  Set = @{}; AddEms = $true },
    @{ N = 'L_v20ega_adlib';  M = 'v20ega';  Set = @{}; AddAdlib = $true },
    @{ N = 'M_v20e_bare_nomusic'; M = 'v20e'; Set = @{}; Dos = 'dos5'; Cfg = @('FILES=30'); NoEms = $true; Opt = 'NOMUSIC ' },
    @{ N = 'N_v20e_nomusic';  M = 'v20e';    Set = @{}; Opt = 'NOMUSIC ' },
    @{ N = 'O_xt88adl';       M = 'xt88adl'; Set = @{} },
    @{ N = 'P_v20e_nosound';  M = 'v20e';    Set = @{}; Opt = 'NOSOUND ' },
    @{ N = 'Q_v20e_xcvoff';   M = 'v20e';    Set = @{}; Pre = 'SET XCV=OFF' },
    @{ N = 'R_v20e_noumb';    M = 'v20e';    Set = @{}; Opt = 'NOUMB NOXMEM ' },
    @{ N = 'S_v20x';          M = 'v20x';    Set = @{} }
)
if ($Only) { $variants = @($variants | Where-Object { $_.N -match $Only }) }
$jobs = foreach ($v in $variants) {
    $er = "$scratch\$($v.N)"
    if (Test-Path $er) { Remove-Item -Recurse -Force $er }
    New-Item -ItemType Directory -Force $er | Out-Null
    Copy-Item -Recurse "$root\emu\$($v.M)" "$er\$($v.M)"
    $cfg = "$er\$($v.M)\86box.cfg"
    $lines = Get-Content $cfg
    foreach ($k in $v.Set.Keys) { $lines = $lines | ForEach-Object { $_ -creplace "^$k = .*", "$k = $($v.Set[$k])" } }
    if ($v.AddAdlib) { $lines += @('', '[Sound]', 'sndcard = adlib') }
    if ($v.NoEms) { $lines = @($lines | Where-Object { $_ -notmatch '^isamem' }) }
    if ($v.AddEms) { $lines += @('', '[Other peripherals]', 'isamem1_type = lotechems', 'isamem2_type = lotechems', '',
        '[Lo-tech EMS Board #1]', 'base = 0260', 'frame = E0000', 'size = 2048', '', '[Lo-tech EMS Board #2]', 'base = 0250', 'frame = D0000', 'size = 512') }
    Set-Content -Encoding ascii $cfg $lines
    $extra = @{}
    if ($v.Cfg) { $extra.Config = $v.Cfg }
    if ($v.Dos) { $extra.Dos = $v.Dos }
    if ($v.Pre) { $extra.Pre = $v.Pre }
    Start-Job -ArgumentList $root, $v.M, $er, $exe, ("$($v.Opt)" + $run), $v.N, $extra -ScriptBlock {
        param($root, $m, $er, $exe, $run, $n, $extra)
        Set-ExecutionPolicy -Scope Process Bypass -Force
        & "$root\tools\emutest.ps1" -Machine $m -Hdd -Mouse -EmuRoot $er -Exe $exe -Run $run @extra `
            -Extra "$root\tests\WALK4.TXT" -WaitSec 240 -UntilLog 'walked to', 'walked to', 'script finished' `
            -Out "$er\shot.png" *> "$er\emutest.txt"
        & 'C:\Program Files\7-Zip\7z.exe' e -y "-o$er" "$er\hdd.img" LOG.TXT *> $null
        $n
    }
}
$jobs | Wait-Job | Out-Null
foreach ($v in $variants) {
    $log = "$scratch\$($v.N)\LOG.TXT"
    "== $($v.N)"
    if (Test-Path $log) {
        $l = @(Get-Content $log)
        $l | Where-Object { $_ -match 'walked to|DOS free|^video|^canvas|^XMEM|^UMB' }
        $p = @($l | Where-Object { $_ -match '^PACE w' } | ForEach-Object { ($_ -split ' ')[3] })
        "steps: $($p -join ' ')"
    } else { 'no LOG' }
}
