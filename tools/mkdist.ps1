# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the DOS releases. The program no longer fits a 360K floppy.
#   .\tools\mkdist.ps1             both builds
#   .\tools\mkdist.ps1 -Private    only the private one
#   .\tools\mkdist.ps1 -Public     only the public one
# Private (for your own test machines), one zip that holds game data: never share it.
#   XCOMXT.ZIP  build\dist\: XCOMXT.EXE, XCOMV20.EXE, XCOM286.EXE, README.TXT, PACKS.VER and the shared
#               packs in the root; the mode folders CGA\ (*C.PAK, *CW.PAK; Hercules too),
#               EGA\ (*E.PAK, *EW.PAK) and VGA\ (*V.PAK) beside them (docs/design.md
#               "Mode folders": any one folder is enough for its mode)
#   in build\, or in -ZipDir (e.g. build\trial). Shared: every pack with no twin under
#   another video letter (SOUNDS, MUSIC, NAMES, UFOPAED) and PACKS.VER.
# Public (GPL): build\release\XCOMXT-<Version>.ZIP: the programs, XCOMCONV.EXE (run
# tools\mkconv.ps1 first), QUICKSTART.TXT and MANUAL.TXT (release\), COPYING and
# SOURCE\ (git archive of HEAD without the internal notes docs\, tests\ and emu\: the
# source of these programs and their build scripts). No packs, no game data: the
# players make the packs with XCOMCONV from their own copy of the game. The build stops
# if a shipped file names this PC's user or the checkout's folder.
param(
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
    [switch]$Private,
    [switch]$Public,
    [switch]$AllowDirty,    # public build although tracked files differ from HEAD
    [string]$ZipDir = '',   # where the private zips go (default build\)
    [string]$Version = 'ALPHA-0.5.1',   # the public zip's name: XCOMXT-<Version>.ZIP
    [string]$RepoUrl = 'https://github.com/fmikowski/xcom-xt'   # the public repository (SOURCE.TXT)
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$dist = "$root\build\dist"
if (-not $Private -and -not $Public) { $Private = $true; $Public = $true }
$sevenZip = 'C:\Program Files\7-Zip\7z.exe'

if ($Public -and -not $AllowDirty) {
    # The source in the zip is HEAD's: the programs must be built from it.
    # (86Box rewrites emu\*\86box.cfg on every run: not source.)
    $dirty = @(git -C $root status --porcelain --untracked-files=no | Where-Object { $_ -notmatch 'emu/[^/]+/86box\.cfg$' })
    if ($dirty.Count) { throw "tracked files differ from HEAD (commit first, or -AllowDirty):`n$($dirty -join "`n")" }
}
if ($Public -and -not (Test-Path "$root\build\conv\XCOMCONV.EXE")) {
    throw 'build\conv\XCOMCONV.EXE is missing: run tools\mkconv.ps1'
}

& "$root\build.ps1"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "$root\build\86\xcomxt.exe" "$dist\XCOMXT.EXE"
& "$root\build.ps1" -Cpu 186
Copy-Item "$root\build\186\xcomxt.exe" "$dist\XCOMV20.EXE"
& "$root\build.ps1" -Cpu 286
Copy-Item "$root\build\286\xcomxt.exe" "$dist\XCOM286.EXE"
Get-ChildItem "$dist\*.PAK", "$dist\PACKS.VER" -ErrorAction SilentlyContinue | Remove-Item
foreach ($d in 'CGA', 'EGA', 'VGA') { Remove-Item -Recurse -Force "$dist\$d" -ErrorAction SilentlyContinue }
Remove-Item -Recurse -Force "$root\build\distvga" -ErrorAction SilentlyContinue   # the old second zip's folder
if ($Private) {
    if (-not (Test-Path "$root\packs\PACKS.VER")) {
        throw 'packs\PACKS.VER is missing: the packs are not from the current converter (tools\xcomconv.py makes it)'
    }
    # V packs: ...V.PAK; the E / C packs and their walk packs (...EW.PAK, ...CW.PAK) for the XT.
    $paks = @(Get-ChildItem "$root\packs\*.PAK")
    $vga = @($paks | Where-Object { $_.Name -match 'V\.PAK$' })
    $xt = @($paks | Where-Object { $_.Name -notmatch 'V\.PAK$' })
    # Shared: no other pack with the same name but another video letter (MUSIC.PAK, NAMES.PAK...).
    $stem = { param($n) if ($n -match '^(.+)[ECV](W?)\.PAK$') { "$($matches[1])|$($matches[2])" } else { $n } }
    $stems = @($paks | ForEach-Object { & $stem $_.Name }) | Group-Object -AsHashTable -AsString
    $shared = @($xt | Where-Object { $stems[(& $stem $_.Name)].Count -eq 1 })
    $need = @{ 'VGA' = @('UIV', 'PALV', 'WORLDV', 'FLOORV', 'BLASTV', 'SMOKEV', 'SLIDESV', 'UR0V', 'BASEV', 'UBASEV',
                         'CYDV', 'CYD1V', 'CYD2V', 'FA1V', 'RSV', 'A1V', 'H1V')
               'shared' = @('SOUNDS', 'MUSIC', 'NAMES', 'UFOPAED')
               'XT' = @('UIE','UIC', 'WORLDE', 'WORLDC', 'FA1E', 'FA1C', 'FA1EW', 'FA1CW') }
    foreach ($k in $need.Keys) {
        $have = switch ($k) { 'VGA' { $vga } 'shared' { $shared } default { $xt } }
        $miss = @($need[$k] | Where-Object { "$_.PAK" -notin $have.Name })
        if ($miss.Count) { throw "packs\ lacks for the $k folder: $(($miss | ForEach-Object { "$_.PAK" }) -join ', ')" }
    }
    $shared | Copy-Item -Destination $dist
    Copy-Item "$root\packs\PACKS.VER" $dist
    # Every other pack in its mode's folder, by the letter before .PAK (or before W.PAK).
    foreach ($m in @(@('CGA', 'C'), @('EGA', 'E'), @('VGA', 'V'))) {
        New-Item -ItemType Directory -Force "$dist\$($m[0])" | Out-Null
        $paks | Where-Object { $_.Name -notin $shared.Name -and $_.Name -match "$($m[1])W?\.PAK$" } |
            Copy-Item -Destination "$dist\$($m[0])"
    }
    $left = @($paks | Where-Object { -not (Test-Path "$dist\$($_.Name)") -and -not (Test-Path "$dist\?GA\$($_.Name)") })
    if ($left.Count) { throw "packs in no folder: $($left.Name -join ', ')" }
}

# The folders' sizes for the README (from packs\, as they go into the zip).
$mb = @{}
if (Test-Path "$root\packs\PACKS.VER") {
    $all = @(Get-ChildItem "$root\packs\*.PAK")
    $st = { param($n) if ($n -match '^(.+)[ECV](W?)\.PAK$') { "$($matches[1])|$($matches[2])" } else { $n } }
    $sg = @($all | ForEach-Object { & $st $_.Name }) | Group-Object -AsHashTable -AsString
    $sh = @($all | Where-Object { $sg[(& $st $_.Name)].Count -eq 1 })
    $mb['shared'] = [Math]::Ceiling((($sh | Measure-Object Length -Sum).Sum + 1MB) / 1MB)   # + the programs
    foreach ($v in 'C', 'E', 'V') {
        $mb[$v] = [Math]::Round((($all | Where-Object { $_.Name -notin $sh.Name -and $_.Name -match "${v}W?\.PAK$" } |
            Measure-Object Length -Sum).Sum / 1MB))
    }
} else { $mb = @{ shared = 'a few'; C = 'a few'; E = 'a few'; V = 'a few' } }

$readme = @"
XCOM-XT - X-COM: UFO Defense for XT-class PCs, and in VGA for a 286 (test build)
================================================================================

Needs: 8088/V20 at 8-10 MHz or better, 640 KB, EGA (256 KB), CGA or Hercules, DOS 3+,
a hard disk (the program alone no longer fits a 360K floppy).
Free conventional memory: at least 540 KB, 560 KB+ recommended ("Largest
executable program size" in MEM; a mouse driver is fine, unload other TSRs). No EMS or XMS
needed. XCOMXT LOG LOG.TXT writes "DOS free at start: n KB".
The VGA version (256 colours): a 286 or better, VGA (256 KB), DOS 5 or later, 560 KB+
free, HIMEM.SYS with 256 KB+ of XMS free (the game keeps pictures and a disk cache there;
DOS=HIGH in CONFIG.SYS helps), a mouse driver, an AdLib or Sound Blaster.

Which program to run (all three play the same game, in every video mode):
XCOMXT.EXE   8086 build: any 8088/8086/V20/V30. Use this one on an 8088 or 8086 XT.
XCOMV20.EXE  80186 build: NEC V20/V30, 186 and up. A bit faster than XCOMXT.EXE; never
             tested on a real V20 (86Box's V20 core is buggy), so it is the real test.
XCOM286.EXE  80286 build: a 286, 386 or better (the VGA version's machines; EGA there
             too). About 5 % quicker than XCOMXT.EXE in the alien turn. Not for an XT.
If one of them does not start or misbehaves, XCOMXT.EXE runs on every machine.

Folders. The game's folder holds XCOMXT.EXE, XCOMV20.EXE, XCOM286.EXE, this file, PACKS.VER and
the packs every video mode shares (MUSIC, SOUNDS, NAMES, UFOPAED.PAK); the saved games
go there too. Each video mode's pictures are in a folder of their own:
  EGA\   the EGA packs (*E.PAK, *EW.PAK), about $($mb['E']) MB
  CGA\   the CGA packs (*C.PAK, *CW.PAK), about $($mb['C']) MB; Hercules shows these
         too, so there is no HERC folder
  VGA\   the VGA packs (*V.PAK), about $($mb['V']) MB
Copy the game folder's files (about $($mb['shared']) MB) to a directory on the hard disk, with only
the folders the machine needs, and run XCOMXT (XCOMV20 on a V20, XCOM286 on a 286 or better) from that directory:
  an XT with EGA:              EGA\
  an XT with CGA or Hercules:  CGA\
  the 286 with VGA:            VGA\ (plus EGA\ only if you want to play it in EGA too)
Any one folder alone is enough to play in its mode. Packs in the game folder itself (an
older install, or XCOMCONV's PACKS folder copied as it is) still work: the game looks
in the mode's folder first, then in its own.

Video mode. With no option the game picks one: VGA when the PC has a VGA card, a 286 or
better (an XT with an 8-bit VGA card plays in EGA), the VGA folder and 256 KB of free
XMS; else EGA when it has an EGA or VGA card and the EGA folder; else Hercules (only a
mono card) or CGA, with the CGA folder. XCOMXT LOG LOG.TXT writes the choice and the
reason ("mode: ..."). XCOMXT EGA, XCOMXT CGA, XCOMXT HERC and XCOMXT VGA force a mode;
when its folder (and its packs in the game folder) or its card is missing, the game
says so and stops. Hercules needs a card with 64 KB (both pages): the CGA packs shown
as 640x200 in greys; HERC says why when it cannot (a colour card present, an MDA, a
card without the second 32 KB page).
Files (the letter before .PAK: E = EGA, C = CGA, V = VGA): ??n?.PAK missions (8 UFO
types; UR0 = terror site), UBASE?.PAK alien base, R??.PAK races, T??.PAK terror
units, A?? armour, H??.PAK heavy weapons platforms (tanks), ...W.PAK their walking
frames, WORLD?.PAK world map, FLOOR?.PAK items on the battle floor, BLAST?.PAK the
blast's picture, SMOKEV.PAK the smoke and fire, CYD1? / CYD2?.PAK Cydonia (CYD?.PAK
one-stage fallback), BASE?.PAK base defence, UI?.PAK the original screens, fonts and
panels (without it the screens use plain text), PALV.PAK the VGA palettes, INTRO?.PAK
the intro movie, SLIDES?.PAK the win / lose slides.

The intro plays at the start as in DOS (with its AdLib music): any key or
click skips it; XCOMXT NOINTRO leaves it out. Winning or losing the game
shows the original slides (a key or click: the next one, Esc: the menu).

Start menu: N new game (then a difficulty), L load, Q quit. Place the base
with arrows + Enter (or the mouse). Geoscape, mouse as in DOS: click a base
for its craft (Launch Interception: click a craft, then click its target
on the map: a UFO, a site or an empty spot as a way point, then OK);
click a craft in flight for Return to Base / Select New Target / Patrol;
click a UFO for its details, Intercept and Centre; click a terror site or
alien base for its details; several things under the click give a list.
The Intercept button lists every base's craft. Right click scrolls the
map there. Pop-ups as in DOS (UFO detected, tracking lost, way point
reached, low fuel, terror site, transport ready to land: Enter Yes, Esc
No) stop the clock. Air combat: 1-5 stances, 6 / 7 weapon off / on, M
minimise (D or the icon brings it back), Tab or a click: the next window
when several craft fight at once. The right-hand panel works with the mouse;
keys 1-4 time speed, Space pause, Tab select a UFO or site,
A a terror site or alien base, I intercept, Enter send the Skyranger,
P patrol (finds alien bases), F funding by country, B base (research, manufacture,
purchase / recruit, sell, transfers, equip, stores, hangar, facilities), U UFOpaedia, R squad, S save,
C final assault once Cydonia is researched, Esc options (save, load, abandon,
quit). Research and Manufacture as in DOS: several projects at a base, N new
project, Enter on one to set its staff: + / - (or Up / Down, the arrow buttons)
one more / less, A all free ones; production units with Right / Left, I for
no end; Enter OK; the arrow buttons repeat while held. Completions pop up
as in DOS (Esc or O: OK; Enter: View Reports / Allocate). Battles: click a tile to move the cursor, click again to
walk; the button rows at the bottom do the actions below.
Tanks (HWPs): buy the Tank/Cannon and Tank/Rocket Launcher with their
shells and rockets, manufacture the others; in the hangar Enter on the
Skyranger or Avenger loads them (4 seats each, ammunition from the
stores, used up in battle; the base's HWPs also defend it). Hovertanks
fly: put the cursor on a higher level (+) and walk there.
Sound: AdLib/OPL2 music (an AdLib or any Sound Blaster's FM) with FM sound
effects on the same chip (NOFMSFX: PC speaker effects instead); without an
OPL2, PC speaker effects. A Sound Blaster (BLASTER variable or 220h/240h probe) plays the
digitised effects only from memory outside the 640 KB: an upper memory
block of 8 KB+ (32 KB: all of a screen's sounds; DOS=UMB, or an XMS driver
with UMBs) or XMS on a 286+ (HIMEM.SYS). Otherwise the speaker plays them
(the LOG says why). 16-bit SB16s work in an XT's 8-bit slot only on some
jumpered models (8-bit DMA); plug-and-play ones need a 286+ to set up.
A serial or bus mouse works through its DOS mouse driver (any version; XCOMXT
LOG LOG.TXT writes a "mouse:" line saying what the driver answered).
XCOMXT BATTLE plays quick random battles instead. More options:
XCOMXT [EGA | CGA | HERC | VGA] [BATTLE] [MISSION pak] [RACE pak] [NIGHT] [ARMOUR n] [SEED n]
       [ALIENS n] [DIFF n] [HWP digits] [NOSB] [NOMUSIC] [NOFMSFX] [NOSOUND]
       [NOINTRO] [LOG file] [BENCH]  (HWP 14: a Tank/Cannon and a Hovertank/Plasma)
       [CRAFT S|L|A] [SQUAD n]  (quick battles: Skyranger, Lightning or Avenger, n soldiers)

Battle keys
  Arrows, Home, End, PgUp, PgDn  move the cursor (map directions)
  + / -     level up / down        Tab    next soldier
  Enter     walk to the cursor     Space  turn towards the cursor
  F / A / U snap / aimed / auto shot at the cursor
  G         throw grenade          R      reload (15 TU)
  K         kneel / stand          M      medi-kit (self or soldier at cursor)
  I         inventory (pick up, drop, arm a backpack weapon)
  N         motion scanner (if carried)
  V         next visible alien     C      centre on soldier
  S         sound on/off           E      end turn
  Esc       options (also F1, the panel's options button): Load Game,
            Save Game, Abandon Game, Game Options (sound), Cancel (Esc);
            in your turn, as in DOS.
  F2        abort mission (also the panel's abort button)
            A game saved in a battle loads straight back into it (start
            menu, geoscape options or the battle's own Load Game).

If something goes wrong: run XCOMXT LOG LOG.TXT and keep LOG.TXT, plus what
you pressed. XCOMXT BENCH writes timings to BENCH.TXT.
Built from your own copy of X-COM: UFO Defense; see the project README.
"@
Set-Content -Encoding ascii "$dist\README.TXT" ($readme -replace "`r?`n", "`r`n")
Copy-Item "$root\release\QUICKSTART.TXT", "$root\release\MANUAL.TXT" $dist

if ($Private) {
    $zd = if ($ZipDir) { $ZipDir } else { "$root\build" }
    New-Item -ItemType Directory -Force $zd | Out-Null
    Remove-Item "$zd\XCOMXT.ZIP", "$zd\XCOMVGA.ZIP" -ErrorAction SilentlyContinue
    & $sevenZip a -tzip -mx=9 -bso0 -bsp0 "$zd\XCOMXT.ZIP" "$dist\*"
    if ($LASTEXITCODE) { throw '7z failed on XCOMXT.ZIP' }
    $n = @(Get-ChildItem -Recurse $dist -File)
    Write-Host ("{0}: {1} files, {2:N1} MB unpacked, {3:N1} MB zipped" -f "$zd\XCOMXT.ZIP", $n.Count,
        (($n | Measure-Object Length -Sum).Sum / 1MB), ((Get-Item "$zd\XCOMXT.ZIP").Length / 1MB))
    foreach ($d in 'CGA', 'EGA', 'VGA') {
        $f = @(Get-ChildItem "$dist\$d" -File)
        Write-Host ("  {0}\: {1} files, {2:N1} MB" -f $d, $f.Count, (($f | Measure-Object Length -Sum).Sum / 1MB))
    }
    $f = @(Get-ChildItem $dist -File)
    Write-Host ("  root: {0} files, {1:N1} MB" -f $f.Count, (($f | Measure-Object Length -Sum).Sum / 1MB))
    Remove-Item "$root\build\XCOMXT.IMG" -ErrorAction SilentlyContinue
}

if ($Public) {
    $rel = "$root\build\release"
    $stage = "$rel\stage"
    $zipName = "XCOMXT-$Version.ZIP"
    Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
    Remove-Item "$rel\$zipName", "$rel\XCOMXT-GPL.ZIP" -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $stage, "$stage\SOURCE" | Out-Null
    # The linker keeps the exe's full build path in the overlay table (DOS 3+ finds the
    # program without it): the shipped copies carry their plain file name instead.
    $ascii = [Text.Encoding]::ASCII
    foreach ($e in @(@('86', 'XCOMXT.EXE'), @('186', 'XCOMV20.EXE'), @('286', 'XCOM286.EXE'))) {
        $b = [IO.File]::ReadAllBytes("$root\build\$($e[0])\xcomxt.exe")
        $at = $ascii.GetString($b).ToLower().IndexOf("$root\build\$($e[0])\xcomxt.exe".ToLower(), [StringComparison]::Ordinal)
        if ($at -lt 0) { throw "build path not found in build\$($e[0])\xcomxt.exe" }
        $len = "$root\build\$($e[0])\xcomxt.exe".Length
        $name = $ascii.GetBytes($e[1])
        for ($k = 0; $k -lt $len; $k++) { $b[$at + $k] = if ($k -lt $name.Length) { $name[$k] } else { 0 } }
        [IO.File]::WriteAllBytes("$stage\$($e[1])", $b)
    }
    Copy-Item "$root\build\conv\XCOMCONV.EXE" $stage
    # The source of exactly this commit, without the internal notes: the same files as
    # the public repository ($RepoUrl, one commit a release): its front page is
    # release\REPO-README.md, the developer's README.md is BUILDING.md there.
    $srcZip = "$rel\source.zip"
    git -C $root archive --format=zip -o $srcZip HEAD -- . ':(exclude)docs' ':(exclude)tests' ':(exclude)emu' ':(exclude)release/REPO-README.md'
    if ($LASTEXITCODE) { throw 'git archive failed' }
    & $sevenZip x -y -bso0 -bsp0 "-o$stage\SOURCE" $srcZip
    if ($LASTEXITCODE) { throw '7z failed on the source archive' }
    Remove-Item $srcZip
    Move-Item "$stage\SOURCE\README.md" "$stage\SOURCE\BUILDING.md"
    Copy-Item "$root\release\REPO-README.md" "$stage\SOURCE\README.md"
    Set-Content -Encoding ascii "$stage\SOURCE.TXT" ("SOURCE\ is the source of these programs (XCOM-XT $Version), the same files as`r`n" +
        "the release's tag in $RepoUrl. The developers' design notes, test scripts`r`n" +
        "and emulator set-ups are not included.")
    Copy-Item "$stage\SOURCE\COPYING" $stage
    Copy-Item "$root\release\QUICKSTART.TXT", "$root\release\MANUAL.TXT" $stage
    # Nothing from the game may be in the download.
    $bad = @(Get-ChildItem -Recurse -File $stage | Where-Object { $_.Extension -match '^\.(PAK|SAV|LBM|FLI|MCD|PCK|MAP|RMP|CAT|SCR|SPK|DAT)$' -or $_.Name -eq 'PACKS.VER' })
    if ($bad.Count) { throw "game data in the public build: $($bad.FullName -join ', ')" }
    # Nor this PC's user name or the checkout's folder (XCOMCONV.EXE's modules are
    # compressed; PyInstaller stores them with relative file names). The public
    # repository's address is meant to be there, and is left out of the check.
    $marks = @($env:USERNAME, $root, ($root -replace '\\', '/')) | Where-Object { $_ } | ForEach-Object { $_.ToLower() }
    $repo = $RepoUrl.ToLower() -replace '^https?://', ''
    $leak = @(Get-ChildItem -Recurse -File $stage | Where-Object {
        $t = $ascii.GetString([IO.File]::ReadAllBytes($_.FullName)).ToLower().Replace($repo, '')
        @($marks | Where-Object { $t.Contains($_) }).Count })
    if ($leak.Count) { throw "the public build names this PC: $($leak.FullName -join ', ')" }
    & $sevenZip a -tzip -mx=9 -bso0 -bsp0 "$rel\$zipName" "$stage\*"
    if ($LASTEXITCODE) { throw '7z failed' }
    Get-ChildItem $stage | Select-Object Name, Length
    $z = Get-Item "$rel\$zipName"
    Write-Host ("{0} {1:N1} MB" -f $z.FullName, ($z.Length / 1MB))
}
