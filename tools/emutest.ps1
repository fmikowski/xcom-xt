# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the engine, makes a bootable 360K DOS floppy, boots it in 86Box and
# saves a screenshot after a delay. Needs emu\dos5 (see README).
#   .\tools\emutest.ps1 -Machine xt88ega -Run BENCH -Out shot.png
#   -Hdd       program and all packs on drive C: (emu\hdd.img), run from C:
#   -DriveB x  run from a release floppy image in drive B:
#   otherwise  program and the farm small-scout pack on the boot floppy
#   xt88sb / xt88sb2 / xt88adl: Sound Blaster 1.0 (220h, IRQ 7), 2.0 (240h, IRQ 5),
#   AdLib only; sound tests need -Hdd (SOUNDS.PAK, MUSIC.PAK)
#   -ExtraA f  copy these files to the boot floppy (A:); -Extra goes to C: with -Hdd, else to A:
#   -Dos d     take IO.SYS, MSDOS.SYS, COMMAND.COM from emu\d (default dos5, on v20e / v20x dos6 =
#              your own MS-DOS 6.22 files, with MEM.EXE and SETVER.EXE; see tests/README.md)
#   -Mouse     load a DOS mouse driver: emu\dos5\MOUSE.COM (8088-safe), else CTMOUSE.EXE (186+)
#   -MouseType msserial (default: Microsoft serial on COM1), logibus (Logitech/Microsoft
#              bus mouse card) or msbus (Microsoft InPort bus mouse)
#   -FreeKB n  load RESERVE.COM (tools\reserve.asm) last, so the game gets about n KB
#              (LOG: "DOS free at start"), e.g. 580 like a real XT with its mouse driver
#   -Config    CONFIG.SYS lines; HIMEM.SYS / EMM386.EXE named there are copied from emu\dos5,
#              the EMS / XMS drivers (LTEMM.EXE, EXMS86.SYS, USE!UMBS.SYS) from emu\drivers
#              (untracked, as emu\dos5: README, "Extended memory"),
#              XTUMB.SYS is assembled from tools\xtumb.asm (an XMS UMB driver for the 8088)
#   xt88umb: xt88sb plus a Lo-tech EMS board (86Box's lotechems: page registers 260h,
#              frame E000); -Config 'DOS=UMB','DEVICE=A:\XTUMB.SYS E000 1000 260' maps EMS
#              pages 0-3 into the frame and hands it out as a 64 KB UMB (DOS 5 links it;
#              without DOS=UMB the game takes it through XMS function 10h). The XT-IDE
#              ROM sits at D000, so the UMB cannot be there.
#   xt88umba: xt88umb with an AdLib card instead of the Sound Blaster (the UMB tests)
#   at286: IBM XT 286 (6 MHz 286), 640 KB + 512 KB extended, EGA, SB 2.0 220h IRQ 5
#   (HIMEM: XMS); at386: 386DX, 2 MB, EGA, SB 2.0 (HIMEM + EMM386 NOEMS: UMBs), AMI BIOS
#   (its CMOS setup comes from tools\mkcmos.py --ami, which reads the defaults from 86Box's ROM)
#   u286: the reference 286 for the VGA version (8 MHz IBM AT, an Oak OTI-077 VGA with 256 KB,
#   1 MB = 640 KB + 384 KB XMS, AdLib; emu\u286\CONFIG.SYS: HIMEM, DOS=HIGH)
#   u386: the fallback for the VGA version, a 386SX (86Box dataexpert386sx, AMI BIOS) at
#   16 MHz, 86Box's slowest 386SX, for a 12 MHz one; 2 MB = 640 KB + 1 MB XMS, the Oak OTI-077
#   VGA with 256 KB as u286, AdLib; emu\u386\CONFIG.SYS as u286 (HIMEM, DOS=HIGH)
#   v20e / v20x: the reference XT (NEC V20 at 10 MHz, 640 KB, EGA, AdLib, 86Box machine v20xt) with its
#              upper memory: a Lo-tech 2MB EMS board (frame E000; I/O 260, as 26C does not map in 86Box) and, where its Lo-tech
#              1MB RAM board is, a second 86Box Lo-tech EMS board (I/O 250, frame D000) whose pages
#              EMSMAP.SYS (tools\emsmap.asm) maps at boot so D000 is plain RAM; the hard disk is an
#              Acculogic sIDE controller (XT-IDE-like BIOS at C800: 86Box's XT-IDE ROM cannot leave D000).
#              Without -Config the machine's own emu\<machine>\CONFIG.SYS is used: v20e = profile E
#              (USE!UMBS.SYS D000-E000, LTEMM.EXE, 64 KB UMB, EMS), v20x = profile X (LTEMM.EXE,
#              EXMS86.SYS !!, USE!UMBS.SYS D000-F000, 128 KB UMB, XMS). The drivers come from
#              emu\drivers (LTEMM.EXE is not in git), MS-DOS 6.22 from emu\dos6 (not in git;
#              tests/README.md, "v20e and v20x").
#   -Cpu 86|186|286  the build (build.ps1 -Cpu; 286 = XCOM286.EXE's)
#   -ExeName n the program's name on the disks and in AUTOEXEC (default XCOMXT; e.g. XCOM286 runs
#              a build under its release name)
#   -OvlArea n / -NoOvlFix  passed to build.ps1 (overlay stress tests)
#   -UntilLog <regex>[,<regex>...]  early stop (needs -Hdd): every -PollSec (10) seconds the
#              game's log (-LogName, default LOG.TXT) is read from a COPY of the running VM's
#              hdd.img (7-Zip); once all the regexes have matched, in this order, on log
#              lines, the run takes its screenshot -GraceSec (4) seconds later and stops.
#              -WaitSec is then only the maximum. The game writes its log line by line
#              (src/input.c log_printf reopens the file each time), so lines reach the
#              image at once.
#   -EmuRoot d run from a copy of emu\ in folder d (86Box machine folders, hdd.img, boot360.img,
#              the floppy staging and variant builds all live there), so that several
#              runs can go on at once (tools\lanes.ps1: build\lanes\<n>\emu). The machine
#              folder is copied from emu\ the first time. emu\ itself is left untouched.
#   -Exe f     use this built xcomxt.exe, skip build.ps1 (the lanes build once)
#   -HddDir d  with -Hdd: drive C: holds every file of folder d (e.g. an unzipped release: its
#              own XCOMXT.EXE and packs) instead of this build and packs\; its subfolders (CGA,
#              EGA, VGA) become folders of C:
#   -Folders f[,f]  with -Hdd: the release's layout instead of every pack in the root: this
#              build, PACKS.VER and the shared packs in C:\, and only the mode folders named
#              (CGA, EGA, VGA) with their packs (docs/design.md "Mode folders")
param(
    [ValidateSet('xt88ega', 'xt88cga', 'v20ega', 'v20cga', 'xt88herc', 'xt88sb', 'xt88sb2', 'xt88adl', 'at286', 'at386', 'xt88umb', 'xt88umba', 'v20e', 'v20x', 'xt88umb2', 'u286', 'u386')] [string]$Machine = 'xt88ega',
    [ValidateSet('86', '186', '286')] [string]$Cpu = '86',
    [ValidatePattern('^[A-Za-z0-9_]{1,8}$')] [string]$ExeName = 'XCOMXT',
    [string]$Run = '',
    [string]$Pre = '',          # extra AUTOEXEC line before the run, e.g. SET BLASTER=...
    [string]$Post = '',         # extra AUTOEXEC line after the run (XMEMCHK: tools\xmemchk.asm)
    [int]$WaitSec = 60,
    [string[]]$Extra = @(),
    [string[]]$ExtraA = @(),    # files for the boot floppy (A:), e.g. emu\dos6\MEM.EXE, build\UMBPROBE.COM
    [string]$Out = "$PSScriptRoot\..\build\$Machine.png",
    [string]$DriveB = '',
    [switch]$Hdd,
    [switch]$Mouse,
    [ValidateSet('msserial', 'logibus', 'msbus')] [string]$MouseType = 'msserial',
    [int]$FreeKB = 0,
    [string[]]$Config = @(),
    [string]$Dos = '',          # emu\<Dos>: IO.SYS, MSDOS.SYS, COMMAND.COM (+ MEM.EXE etc.); default dos5, v20e / v20x: dos6
    [int]$OvlArea = 0,
    [switch]$NoOvlFix,
    [string]$EmuRoot = '',
    [string]$Exe = '',
    [string]$HddDir = '',
    [string[]]$Folders = @(),
    [string[]]$UntilLog = @(),
    [string]$LogName = 'LOG.TXT',
    [int]$PollSec = 10,
    [int]$GraceSec = 4,
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
    [string]$Box = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\86Box.86Box_Microsoft.Winget.Source_8wekyb3d8bbwe\86Box.exe",
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."
if (-not $Dos) { $Dos = if (($Machine -in 'v20e', 'v20x') -and (Test-Path "$root\emu\dos6\IO.SYS")) { 'dos6' } else { 'dos5' } }
$dos = "$root\emu\$Dos"
$cga = $Machine -like '*cga' -or $Machine -like '*herc'   # Hercules runs the CGA packs

if ($EmuRoot) {
    New-Item -ItemType Directory -Force $EmuRoot | Out-Null
    $emu = (Resolve-Path $EmuRoot).Path
    $stage = "$emu\floppy"
    $bin = "$emu\bin"
    if (-not (Test-Path "$emu\$Machine")) { Copy-Item -Recurse "$root\emu\$Machine" "$emu\$Machine" }
} else {
    $emu = "$root\emu"
    $stage = "$root\build\floppy"
    $bin = "$root\build\$Cpu"
}
if ($Exe) {
    $exePath = (Resolve-Path $Exe).Path
} else {
    if ($EmuRoot) { & "$root\build.ps1" -Cpu $Cpu -OvlArea $OvlArea -NoOvlFix:$NoOvlFix -OutDir $bin }
    else { & "$root\build.ps1" -Cpu $Cpu -OvlArea $OvlArea -NoOvlFix:$NoOvlFix }
    $exePath = "$bin\xcomxt.exe"
}

New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exePath "$stage\$ExeName.EXE"
$extraFiles = @($Extra | ForEach-Object { (Resolve-Path $_).Path })
$files = @("$dos\COMMAND.COM", "$stage\AUTOEXEC.BAT")
$files += @($ExtraA | ForEach-Object { (Resolve-Path $_).Path })

if ($Hdd) {
    $drive = "C:`r`n"
    $paks = @(Get-ChildItem "$root\packs\*.PAK")
    $hfiles = @("$stage\$ExeName.EXE", "$root\packs\PACKS.VER") + @($paks | ForEach-Object { $_.FullName })
    $subs = @()
    if ($Folders) {
        # The release's layout (tools\mkdist.ps1): shared packs (no twin with another video
        # letter: MUSIC, SOUNDS, NAMES, UFOPAED) in the root, the rest in CGA\ EGA\ VGA\.
        $stem = { param($n) if ($n -match '^(.+)[ECV](W?)\.PAK$') { "$($matches[1])|$($matches[2])" } else { $n } }
        $stems = @($paks | ForEach-Object { & $stem $_.Name }) | Group-Object -AsHashTable -AsString
        $shared = @($paks | Where-Object { $stems[(& $stem $_.Name)].Count -eq 1 })
        $hfiles = @("$stage\$ExeName.EXE", "$root\packs\PACKS.VER") + @($shared | ForEach-Object { $_.FullName })
        foreach ($f in $Folders) {
            $v = $f.Substring(0, 1).ToUpper()
            $mine = @($paks | Where-Object { $_.Name -notin $shared.Name -and $_.Name -match "${v}W?\.PAK$" })
            $subs += @('--sub', $f.ToUpper()) + @($mine | ForEach-Object { $_.FullName })
        }
    }
    if ($HddDir) {
        $hfiles = @(Get-ChildItem -File $HddDir | ForEach-Object { $_.FullName })
        foreach ($d in @(Get-ChildItem -Directory $HddDir)) {      # one level: the mode folders
            $subs += @('--sub', $d.Name) + @(Get-ChildItem -File $d.FullName | ForEach-Object { $_.FullName })
        }
    }
    & $Python -I "$root\tools\mkhdd.py" --out "$emu\hdd.img" @($hfiles + $extraFiles + $subs)
    if ($LASTEXITCODE) { throw 'mkhdd failed' }
} elseif ($DriveB) {
    $drive = "B:`r`n"
} else {
    $drive = ''
    $v = if ($cga) { 'C' } else { 'E' }
    $files += @("$stage\$ExeName.EXE", "$root\packs\PACKS.VER", "$root\packs\FA1$v.PAK", "$root\packs\RS$v.PAK") + $extraFiles
}
$preLine = if ($Pre) { "$Pre`r`n" } else { '' }
$postLine = if ($Post) { "`r`n$Post" } else { '' }
if (("$Pre $Post") -match 'XMEMCHK') {
    & "$env:LOCALAPPDATA\bin\NASM\nasm.exe" -f bin -o "$stage\XMEMCHK.COM" "$root\tools\xmemchk.asm"
    if ($LASTEXITCODE) { throw 'nasm failed on xmemchk.asm' }
    $files += "$stage\XMEMCHK.COM"
}
$mouseLine = ''
if ($Mouse) {
    $drv = if (Test-Path "$dos\MOUSE.COM") { 'MOUSE.COM' } else { 'CTMOUSE.EXE' }
    $files += "$dos\$drv"
    $mouseLine = "A:\$drv`r`n"
}
$reserveLine = ''
if ($FreeKB) {
    $nasm = "$env:LOCALAPPDATA\bin\NASM\nasm.exe"
    & $nasm -f bin -o "$stage\RESERVE.COM" "$root\tools\reserve.asm"
    if ($LASTEXITCODE) { throw 'nasm failed on reserve.asm' }
    $files += "$stage\RESERVE.COM"
    $reserveLine = "A:\RESERVE $FreeKB`r`n"
}
Set-Content -Encoding ascii "$stage\AUTOEXEC.BAT" "@ECHO OFF`r`n$mouseLine$preLine$reserveLine${drive}$ExeName $Run$postLine"
if (-not $Config -and (Test-Path "$root\emu\$Machine\CONFIG.SYS")) {   # v20e / v20x / u286: the reference profiles
    $Config = @(Get-Content "$root\emu\$Machine\CONFIG.SYS" | Where-Object { $_.Trim() -and $_ -notmatch '^\s*(REM|;)' })
}
if ($Config) {
    foreach ($drv in 'HIMEM.SYS', 'EMM386.EXE') {
        if (($Config -join ' ') -match $drv) { $files += "$dos\$drv" }
    }
    foreach ($asm in 'EMSMAP', 'UMBFILL') {      # our own drivers (tools\<name>.asm), built here
        if (($Config -join ' ') -match "$asm.SYS") {
            $nasm = "$env:LOCALAPPDATA\bin\NASM\nasm.exe"
            & $nasm -f bin -o "$stage\$asm.SYS" "$root\tools\$($asm.ToLower()).asm"
            if ($LASTEXITCODE) { throw "nasm failed on $asm.asm" }
            $files += "$stage\$asm.SYS"
        }
    }
    if (($Config -join ' ') -match 'SETVER.EXE') { $files += "$dos\SETVER.EXE" }
    foreach ($drv in 'LTEMM.EXE', 'EXMS86.SYS', 'USE!UMBS.SYS') {   # emu\drivers (the real drivers, kept out of the repository)
        if (($Config -join ' ') -match [regex]::Escape($drv)) { $files += "$root\emu\drivers\$drv" }
    }
    if (($Config -join ' ') -match 'XTUMB.SYS') {
        $nasm = "$env:LOCALAPPDATA\bin\NASM\nasm.exe"
        & $nasm -f bin -o "$stage\XTUMB.SYS" "$root\tools\xtumb.asm"
        if ($LASTEXITCODE) { throw 'nasm failed on xtumb.asm' }
        $files += "$stage\XTUMB.SYS"
    }
    Set-Content -Encoding ascii "$stage\CONFIG.SYS" (($Config -join "`r`n") + "`r`n")
    $files += "$stage\CONFIG.SYS"
}

# Drive B and the hard disk in the machine config.
$cfgPath = "$emu\$Machine\86box.cfg"
$cfg = @(Get-Content $cfgPath | Where-Object {
    $_ -notmatch '^fdd_02_' -and $_ -notmatch '^(hdc_1|hdd_01_\w+) = ' -and
    $_ -notmatch '^\[(Storage controllers|Hard disks)\]$' } |
    ForEach-Object { $_ -creplace '^mouse_type = .*', "mouse_type = $MouseType" })
if ($EmuRoot) {   # the committed configs say ../../emu/boot360.img, relative to the machine folder
    $cfg = @($cfg | ForEach-Object { $_ -creplace '^fdd_01_fn = .*', "fdd_01_fn = $emu\boot360.img" })
}
$i = [array]::IndexOf($cfg, '[Floppy and CD-ROM drives]')
if ($DriveB) {
    $b = (Resolve-Path $DriveB).Path
    $cfg = $cfg[0..$i] + @('fdd_02_type = 525_2dd', "fdd_02_fn = $b") + $cfg[($i + 1)..($cfg.Count - 1)]
} else {
    $cfg = $cfg[0..$i] + @('fdd_02_type = none') + $cfg[($i + 1)..($cfg.Count - 1)]
}
if ($Hdd) {
    $hdc = if ($Machine -in 'v20e', 'v20x') { 'xtide_acculogic' } else { 'xtide' }   # jride: the XT-IDE BIOS at C800, not D000
    # tools\mkhdd.py's geometry (cylinders, heads, sectors; XT_HDD_GEOM overrides both)
    $geo = if ($env:XT_HDD_GEOM) { $env:XT_HDD_GEOM -split ',' } else { @(200, 16, 63) }
    $cfg += @('', '[Storage controllers]', "hdc_1 = $hdc", '', '[Hard disks]',
              "hdd_01_parameters = $($geo[2]), $($geo[1]), $($geo[0]), 0, ide", "hdd_01_fn = $emu\hdd.img",
              'hdd_01_ide_channel = 0:0')
}
Set-Content -Encoding ascii $cfgPath $cfg

& $Python -I "$root\tools\mkfloppy.py" --boot "$dos\bootsect.bin" `
    --out "$emu\boot360.img" --label XCOMXT `
    --system "$dos\IO.SYS" "$dos\MSDOS.SYS" -- @files
if ($LASTEXITCODE) { throw 'mkfloppy failed' }

# AT-class machines: a CMOS setup, or the BIOS stops at its setup prompt.
if ($Machine -eq 'at286') {
    New-Item -ItemType Directory -Force "$emu\$Machine\nvr" | Out-Null
    & $Python -I "$root\tools\mkcmos.py" "$emu\$Machine\nvr\ibmxt286.nvr" 512
}
if ($Machine -eq 'u286') {
    New-Item -ItemType Directory -Force "$emu\$Machine\nvr" | Out-Null
    & $Python -I "$root\tools\mkcmos.py" "$emu\$Machine\nvr\ibm5170_111585.nvr" 384 64
}
# at386 / u386: AMI BIOS 05/05/91, its setup defaults read from 86Box's ROM (mkcmos --ami)
$amiRom = @{ 'at386' = 'asus386\ASUS_ISA-386C_BIOS.bin'; 'u386' = 'dataexpert386sx\5e9f20e5ef967717086346.BIN' }
if ($amiRom.ContainsKey($Machine)) {
    $rom = $amiRom[$Machine]
    New-Item -ItemType Directory -Force "$emu\$Machine\nvr" | Out-Null
    & $Python -I "$root\tools\mkcmos.py" "$emu\$Machine\nvr\$(Split-Path -Parent $rom).nvr" 1024 128 `
        --ami "$(Split-Path -Parent $Box)\roms\machines\$rom"
    if ($LASTEXITCODE) { throw 'mkcmos failed' }
}

# Early stop: whether the log in a copy of the VM's disk has matched all of $UntilLog.
function Test-LogUntil {
    $img = "$emu\hdd.img"
    $tmpDir = "$emu\poll"
    $tmpImg = "$tmpDir\copy.img"
    $lines = $null
    New-Item -ItemType Directory -Force $tmpDir | Out-Null
    Remove-Item "$tmpDir\$LogName" -ErrorAction SilentlyContinue
    try {
        $in = [IO.File]::Open($img, 'Open', 'Read', 'ReadWrite')
        try {
            $o = [IO.File]::Create($tmpImg)
            try { $in.CopyTo($o) } finally { $o.Dispose() }
        } finally { $in.Dispose() }
        $save = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $SevenZip e -y "-o$tmpDir" $tmpImg $LogName 2>&1 | Out-Null
        $ErrorActionPreference = $save
        if (Test-Path "$tmpDir\$LogName") { $lines = [IO.File]::ReadAllLines("$tmpDir\$LogName") }
    } catch { return $false }    # a torn copy: try again at the next poll
    if ($null -eq $lines) { return $false }
    $at = 0
    foreach ($re in $UntilLog) {
        $found = -1
        for ($k = $at; $k -lt $lines.Count; $k++) {
            if ($lines[$k] -match $re) { $found = $k; break }
        }
        if ($found -lt 0) { return $false }
        $at = $found + 1
    }
    return $true
}

$proc = Start-Process $Box -ArgumentList '--vmpath', "`"$emu\$Machine`"" -PassThru
try {
    $t0 = Get-Date
    if ($UntilLog.Count -and $Hdd) {
        $matched = $false
        while (((Get-Date) - $t0).TotalSeconds -lt $WaitSec) {
            $left = $WaitSec - ((Get-Date) - $t0).TotalSeconds
            Start-Sleep -Milliseconds ([int]([Math]::Min($PollSec, $left) * 1000))
            if ($proc.HasExited) { break }
            if (Test-LogUntil) { $matched = $true; break }
        }
        if ($matched) {
            Start-Sleep $GraceSec
            Write-Host ("Log matched after {0:N0} s (maximum {1} s): stopping" -f ((Get-Date) - $t0).TotalSeconds, $WaitSec)
        } else {
            Write-Host "Log did not match within $WaitSec s"
        }
    } else {
        Start-Sleep $WaitSec
    }
    & "$root\tools\snapwin.ps1" -ProcessId $proc.Id -Out $Out
} finally {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $proc.Id -Timeout 10 -ErrorAction SilentlyContinue
}
