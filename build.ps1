# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the DOS engine with Open Watcom and NASM.
#   .\build.ps1             8086 build (turbo 8088 and up)
#   .\build.ps1 -Cpu 186    80186 build (NEC V20 and up)
#   .\build.ps1 -Cpu 286    80286 build (286 and up, shipped as XCOM286.EXE; the
#                           assembly's 286 paths are %ifdef CPU286)
#   .\build.ps1 -NoOverlay  everything resident (no overlay manager)
#   .\build.ps1 -OvlArea 45056 an overlay area of that many bytes (stress tests:
#                           a small area makes the manager move sections)
#   .\build.ps1 -NoOvlFix   leave the overlay manager's redoRelocs bug in
#                           (to reproduce it; see docs/design.md, Memory)
# The geoscape and the battlescape code are dynamic overlays sharing one
# area: only one of them is in memory at a time.
param(
    [ValidateSet('86', '186', '286')] [string]$Cpu = '86',
    [switch]$NoOverlay,
    [int]$OvlArea = 0,
    [switch]$NoOvlFix,
    [string]$OutDir = '',   # build into this folder (parallel test lanes) instead of build\<Cpu>
    [string]$Watcom = 'C:\WATCOM',
    [string]$Nasm = "$env:LOCALAPPDATA\bin\NASM\nasm.exe"
)
$ErrorActionPreference = 'Continue'   # compiler warnings go to stderr

$env:WATCOM = $Watcom
$env:INCLUDE = "$Watcom\h"
$env:PATH = "$Watcom\binnt64;$Watcom\binnt;$env:PATH"

# Modules by overlay section; every other module stays resident.
$overlays = @(
    @('geo', 'base', 'craft', 'alien', 'facil'),    # geoscape: what runs every game hour
    @('menu'), @('pedia'), @('airfight', 'afwin', 'cbay'), @('report', 'graphs'), @('mreport'), @('brief'),  # geoscape screens (the funding screen and graphs, the month report)
    @('geowin'),                                     # geoscape windows: map clicks, interception
    @('battle', 'render', 'rendasm', 'los', 'path', 'psibat', 'effects', 'panel', 'rules', 'aihook', 'floor', 'vox'),   # battlescape core
    @('ai'),                                         # alien turn (next to the core)
    @('aiway', 'wayasm'),                                      # a blaster bomb's waypoints (beside the core and the AI)
    @('voxx'),                                       # exact mode's sight and lines of fire (next to the core and any companion; not loaded by default on an XT)
    @('bplayer'),                                    # player turn (next to the core)
    @('bturn'),                                      # between turns: TU, panic, fuses, smoke, fire
    @('bact', 'voxtile'),                            # hand menu, grenade timer, aiming at terrain (next to bplayer)
    @('bdebug'),                                     # the DEBUG option's battle keys Z and Y (next to bplayer)
    @('bmedi'),                                      # the medi-kit (next to bplayer)
    @('bdoor'),                                      # doors: a walk's, a right click's (next to the core and any companion)
    @('bview'),                                      # the view on what X-COM must see outside its turn, the blast's picture (next to the core and any companion)
    @('pak', 'mapgen'),                              # battle setup: packs, map generation
    @('hwp', 'spawn'),                               # battle setup: HWPs, units (with pak, then the core)
    @('picplan'),                                    # battle setup: which unit pictures fit (with pak)
    @('floorld'),                                    # battle setup: floor items' pictures (next to the core)
    @('invscr', 'heldasm'),                          # battle inventory (heldasm.asm: the held item's picture)
    @('batui'),                                      # battle minimap and abort window
    @('batuiv'),                                     # the minimap in 256 colours (VGA; beside batui)
    @('bsave'),                                      # battle options window, saving and loading in battle
    @('saves'),                                      # the DOS save and load lists (geoscape and battle)
    @('basescr', 'hangar'), @('crewscr'), @('facilscr'), @('xfer'), @('soldier'),   # basescape with the craft screens, crew and armour, facilities, transfers / lists, the Soldiers button's screens
    @('trade'), @('qlist'),                          # Purchase, Sell, Stores, Transfers; scroll bars and quantity arrows of their lists (and Equip Craft)
    @('labscr', 'baseinfo'),                         # research and manufacture screens, Base Information
    @('hwpscr', 'psi'),                              # Equip Craft for HWPs, HWPs around a battle; psi training
    @('contain'),                                    # Manage Alien Containment (next to the geoscape and qlist)
    @('gpop', 'hwpdeb', 'postbat'),                  # research / production / construction pop-ups, HWPs and soldiers after a battle (postbat here, not on its own:
                                                     # left over next to basescr + qlist it hung Equip Craft after a battle)
    @('cut', 'cutasm'),                              # intro movie, end slides (cutasm.asm too)
    @('vbench'),                                     # BENCH: renderer, logic, voxel lines
    @('canvas', 'cvasm'),                            # the battle view's canvas in EMS / XMS (runs from a UMB overlay area)
    @('nlit', 'litasm'),                             # the night light (dark battles, next to the canvas in a UMB overlay area)
    @('cvinit'),                                     # the canvas's start and end (twice a battle)
    @('startup'),                                    # the command line (main.c, once)
    @('xmem', 'xmemres', 'xmemhma'),                 # the EMS/XMS read cache's set-up (once; kept apart from startup: a bigger startup
                                                     # section changed where later sections land and slowed the campaign tests)
    @('hgcinit', 'hgcasm'),                          # Hercules: the card's set-up (once, mono machines only; it copies hgcasm's converter to the heap)
    @('vgainit', 'vgaasm', 'vgageo', 'vgxasm')                           # the VGA prototype's set-up (once, option VGA only; it copies vgaasm's battle view code to the heap)
)
# Sections that work together must fit in the overlay area at the same time
# (each named by one of its modules), or every call between them would load
# code from disk: the alien turn runs the core and the AI, the player's turn
# the core, bplayer and the battle windows (batui), the hand menu (bact) or the
# medi-kit (bmedi), the
# inventory (invscr) the core alone (player_turn hands it to battle_run), the
# options window with saving and loading (bsave) the core alone, the
# setup the core and pak/mapgen; a geoscape screen runs next to the
# geoscape (the options open the UFOpaedia, the briefing its text flow;
# the basescape hands the research, manufacture, facility, transfer, base
# list and psi screens to the geoscape, so that only one of them sits next
# to it).
# The dynamic overlay manager keeps sections with a frame on the stack
# (battle_run is in the core) and drops others to load one. A terrain
# explosion (bturn.c) in the player's turn runs the core, bplayer and bturn;
# in the alien turn the manager drops the AI for bturn and reloads it after
# (battle + ai + bturn need not fit: tests\BOOMAI.TXT).
$together = @(@('battle', 'ai'), @('battle', 'invscr'), @('battle', 'bplayer', 'batui'), @('battle', 'bplayer', 'batui', 'batuiv'),
              @('battle', 'bturn'), @('battle', 'bplayer', 'bturn'), @('battle', 'bplayer', 'bact'), @('battle', 'bplayer', 'bdebug'), @('battle', 'bplayer', 'bmedi'), @('battle', 'bplayer', 'bact', 'bdoor'), @('battle', 'bturn', 'bdoor'), @('battle', 'bsave', 'saves'),
              @('battle', 'pak'), @('battle', 'hwp'), @('pak', 'hwp'), @('pak', 'picplan'), @('battle', 'floorld'),
              @('geo', 'menu', 'pedia'), @('geo', 'menu', 'saves'), @('geo', 'airfight'), @('geo', 'report'), @('geo', 'mreport'),
              @('geo', 'brief', 'pedia'), @('geo', 'geowin'), @('geo', 'postbat'),
              @('geo', 'basescr', 'qlist'), @('geo', 'crewscr', 'qlist'), @('geo', 'trade', 'qlist'), @('geo', 'facilscr'), @('geo', 'xfer', 'qlist'), @('geo', 'soldier', 'qlist'), @('geo', 'contain', 'qlist'), @('geo', 'labscr'),
              @('geo', 'hwpscr', 'qlist'), @('geo', 'gpop'), @('geo', 'cut'),
              # what may be left next to them: qlist after a quantity screen
              # when Build Facilities opens, the UFOpaedia when the basescape does
              @('geo', 'facilscr', 'qlist'), @('geo', 'basescr', 'pedia'))
# Sets that may be loaded at once (a section left over from the screen
# before) but need not fit: each must either not fit (the manager drops
# one) or leave 1 KB, never fit with less (such a layout hung the machine:
# docs/design.md, "The craft screens"; a rule of thumb, not the whole
# story: "Sections left over"). The build grows the area for one that
# would fit with less and fails if one still does.
$apart = @(@('geo', 'basescr', 'trade'), @('geo', 'basescr', 'xfer'), @('geo', 'basescr', 'hwpscr'),
           @('geo', 'basescr', 'crewscr', 'qlist'), @('geo', 'crewscr', 'qlist', 'xfer'), @('geo', 'crewscr', 'qlist', 'hwpscr'),
           @('geo', 'trade', 'qlist', 'labscr'), @('geo', 'basescr', 'crewscr', 'labscr'), @('geo', 'qlist', 'labscr'),
           @('geo', 'crewscr', 'labscr'), @('geo', 'crewscr', 'facilscr'), @('geo', 'facilscr', 'pedia'),
           @('geo', 'basescr', 'menu'), @('geo', 'basescr', 'crewscr', 'menu'), @('geo', 'labscr', 'saves'),
           @('geo', 'labscr', 'postbat'))
# Sets that must fit in the area at all (left >= 0, not the 1 KB): a small
# section loaded beside a full set, in its free kilobyte, that loads no
# other section while it runs, so no return trap is needed meanwhile; the
# manager drops it first when it needs the room later. bdoor (doors, every
# step of a walk) beside the alien turn's core + AI (docs/design.md, "Doors").
$beside = @(@('battle', 'ai', 'bdoor'), @('battle', 'ai', 'bview'), @('battle', 'ai', 'voxx'), @('battle', 'ai', 'aiway'),
            @('battle', 'bplayer', 'bturn', 'voxx'), @('battle', 'bplayer', 'batui', 'voxx'))
# Not yet: the geoscape's small sections left over (save lists, after-battle,
# intro movie, UFOpaedia) next to the basescape's: geo + basescr + cut, + saves +
# postbat, geo + crewscr + qlist + cut, + pedia, + saves + postbat, geo + xfer +
# qlist + saves (docs/design.md).

# Screens and the geoscape are compiled for size (-os), the rest for speed.
# (crewscr is deliberately not in the list: its sections' sums with qlist and
# xfer / hwpscr must not nearly fit the area, see docs/design.md, craft screens;
# labscr neither: with Base Information it must not fit next to postbat.)
# The pack loader (pak) reads files: small, so that the setup (battle + pak,
# the transports' maps) fits the overlay area the player's turn needs.
# The AI decides rather than computes: small beats fast there (ai), as in
# the player's commands, the psi rolls and the panel. The battle core's
# rules in battle.c and aihook.c run per step and per unit: fast. The path
# search is pathasm.asm; path.c builds its tables and walks the result.
$forSize = @('voxx', 'geo', 'base', 'craft', 'alien', 'facil', 'menu', 'pedia', 'psi', 'airfight', 'cbay', 'report', 'mreport', 'uik', 'invscr',
             'main', 'inv', 'ai', 'panel', 'psibat', 'input', 'hud', 'unit', 'rules', 'effects', 'path',
             'batui', 'brief', 'afwin', 'bplayer', 'basescr', 'trade', 'qlist', 'facilscr', 'xfer', 'soldier', 'contain', 'geowin', 'graphs',
             'hwp', 'picplan', 'sprcache', 'spawn', 'hwpscr', 'hwpdeb', 'hangar', 'gpop', 'bturn', 'bact', 'bdebug', 'bmedi', 'bdoor', 'bview', 'aiway', 'bsave', 'saves', 'floor', 'floorld', 'cut',
             'vox', 'voxtile', 'vbench', 'pak', 'postbat', 'startup', 'xmem', 'rcursor', 'canvas', 'nlit', 'cvinit', 'hgcinit', 'vgainit', 'batuiv')
# Overlay screens whose strings and tables stay in their section's code
# (-zc) instead of the resident data group.
$constInCode = @('picplan', 'voxx', 'batui', 'brief', 'afwin', 'graphs', 'menu', 'pedia', 'psi', 'invscr', 'geowin', 'labscr', 'hwp',
                 'hwpscr', 'hwpdeb', 'hangar', 'crewscr', 'gpop', 'bturn', 'bact', 'bdebug', 'bmedi', 'bdoor', 'bview', 'aiway', 'bsave', 'saves', 'floorld', 'cut',
                 'facilscr', 'xfer', 'soldier', 'trade', 'qlist', 'contain', 'report', 'mreport', 'airfight', 'cbay', 'ai',
                 'voxtile', 'vbench', 'base', 'startup', 'xmem', 'baseinfo', 'canvas', 'cvinit', 'hgcinit', 'vgainit')
$forSize += 'vgageo'; $constInCode += 'vgageo'                  # VGA outside the battle (its own line: the vga branch edits the lists)

$root = $PSScriptRoot

# The overlay manager's redoRelocs bug is patched in the linked program by
# tools\ovlfix.ps1 (see there and docs/design.md, Memory).
$out = if ($OutDir) { $OutDir } else { Join-Path $root "build\$Cpu" }
New-Item -ItemType Directory -Force $out | Out-Null

$cpuFlag = @{ '86' = '-0'; '186' = '-1'; '286' = '-2' }[$Cpu]
# Overlay stress builds log the manager's section moves (src/rdcount.asm).
# XT_RDDIAG=gap: the LOG says where the screen stood still (GAP lines, src/mouse.c).
$watch = if ($OvlArea -gt 0) { '-dOVLWATCH' } elseif ($env:XT_RDDIAG -eq 'time') { '-dRDTIME' } elseif ($env:XT_RDDIAG -eq 'gap') { '-dGAPDIAG' } elseif ($env:XT_RDDIAG) { '-dRDDIAG' } else { '-dNOWATCH' }
# The 286 build's own code paths: %ifdef CPU286 (8086 and 186 objects stay byte-identical).
$nasm286 = @(if ($Cpu -eq '286') { '-dCPU286' })
$nasmCpu = @{ '86' = '8086'; '186' = '186'; '286' = '286' }[$Cpu]   # NASM: -dCPU=8086/186/286 (cpu CPU)
# Test builds: $env:XT_DEFS = '-dMAX_FLOOR=6 -dMAX_CARRY=16 -dFX_DEVS=1' (smaller lists,
# tests\README.md); clear it for normal builds.
$defs = @(if ($env:XT_DEFS) { $env:XT_DEFS -split ' ' | Where-Object { $_ } })

$objs = @()
foreach ($asm in Get-ChildItem "$root\src\*.asm") {
    $obj = Join-Path $out ($asm.BaseName + '.obj')
    & $Nasm -f obj "-dCPU=$nasmCpu" @nasm286 $watch "-i$root\src\" -o $obj $asm.FullName
    if ($LASTEXITCODE) { throw "nasm failed on $($asm.Name)" }
    $objs += $obj
}

$sources = @(Get-ChildItem "$root\src\*.c")
$exe = Join-Path $out 'xcomxt.exe'
Push-Location $out
try {
    foreach ($c in $sources) {
        $opt = if ($forSize -contains $c.BaseName) { '-os' } else { '-ox' }
        $zc = if ($constInCode -contains $c.BaseName) { '-zc' } else { '-zq' }
        & wcc -q -bt=dos -ml $cpuFlag $opt -s -of -zq $zc $watch @defs "-fo=$out\$($c.BaseName).obj" $c.FullName
        if ($LASTEXITCODE) { throw "wcc failed on $($c.Name)" }
    }
    $names = @($sources | ForEach-Object { $_.BaseName })
    $lnk = @('system dos', "name '$exe'", 'option map, quiet')
    $resident = @($names | Where-Object { $n = $_; $NoOverlay -or -not ($overlays | Where-Object { $_ -contains $n }) })
    $lnk += @($resident | ForEach-Object { "file '$out\$_.obj'" })
    # Assembly modules named in an overlay section (cutasm) go into it.
    $asmNames = @($objs | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) })
    $lnk += @($objs | Where-Object { $n = [IO.Path]::GetFileNameWithoutExtension($_)
                                     $NoOverlay -or -not ($overlays | Where-Object { $_ -contains $n }) } |
              ForEach-Object { "file '$_'" })
    if (-not $NoOverlay) {
        $lnk += 'option dynamic, area=AREA'
        $lnk += 'begin'
        foreach ($sec in $overlays) {
            $present = @($sec | Where-Object { $names -contains $_ -or $asmNames -contains $_ })
            if ($present.Count) {
                $lnk += '  section file ' + (($present | ForEach-Object { "'$out\$_.obj'" }) -join ', ')
            }
        }
        $lnk += 'end'
    }
    # Link with a tiny overlay area: the linker then says how large the area
    # must be (the largest section), and the second link uses that. The map
    # then gives the section sizes: if sections that work together need
    # more, a third link makes the area that large.
    $area = 16
    foreach ($pass in 1, 2, 3, 4) {
        Set-Content -Encoding ascii "$out\xcomxt.lnk" ($lnk -creplace 'AREA', $area)
        $msgs = & wlink "@$out\xcomxt.lnk" 2>&1 | ForEach-Object { "$_" }
        if ($LASTEXITCODE) { $msgs | Out-Host; throw 'wlink failed' }
        if ($NoOverlay) { break }
        $need = $msgs | Select-String 'overlay area must be at least (\d+)'
        if ($need) {
            $area = [int]$need.Matches[0].Groups[1].Value
            continue
        }
        # Section sizes rounded to paragraphs, plus the manager's block headers.
        $size = @{}
        $sec = -1
        foreach ($line in Get-Content "$out\xcomxt.map") {
            if ($line -match '^Overlay section (\d+) ') {
                $sec = [int]$Matches[1] - 1
            } elseif ($sec -ge 0 -and $line -match '^\S+\s+CODE\s+AUTO\s+\S+\s+([0-9a-f]+)$') {
                $size[$sec] += [Convert]::ToInt32($Matches[1], 16)
            }
        }
        function Get-SetSum($set) {
            $sum = 32
            foreach ($m in $set) {
                $i = 0
                while (-not ($overlays[$i] -contains $m)) { $i++ }
                $sum += [math]::Ceiling($size[$i] / 16) * 16 + 16
            }
            return $sum
        }
        $want = 0
        foreach ($set in $together) {
            # The area's own header, plus room for the return traps the
            # manager allocates in the area when it drops a section that has
            # a frame on the stack (the geoscape during a battle): with an
            # exact fit, loading the AI during a campaign battle's setup
            # never found room and hung.
            $sum = 32 + 1024
            foreach ($m in $set) {
                $i = 0
                while (-not ($overlays[$i] -contains $m)) { $i++ }
                $sum += [math]::Ceiling($size[$i] / 16) * 16 + 16
            }
            if ($sum -gt $want) { $want = $sum }
        }
        # A set of $apart that would fit with less than 1 KB left gets its
        # 1 KB (growing the area can make another one fit: again).
        $want = [math]::Max($want, $area)
        # A set of $beside must fit, without the kilobyte (the 186 build's
        # code is laid out differently: there battle+ai+bdoor grows it).
        foreach ($set in $beside) { $want = [math]::Max($want, (Get-SetSum $set)) }
        for ($grow = $true; $grow; ) {
            $grow = $false
            foreach ($set in $apart) {
                $sum = Get-SetSum $set
                if ($want -ge $sum -and $want - $sum -lt 1024) { $want = $sum + 1024; $grow = $true }
            }
        }
        if ($OvlArea -gt 0 -and $area -ne $OvlArea) {
            # A fixed area for stress tests (at least the largest section).
            if ($OvlArea -lt $area) { throw "-OvlArea $OvlArea is below the largest section ($area bytes)" }
            $area = $OvlArea
            continue
        }
        if ($OvlArea -gt 0 -or $want -le $area) { break }
        $area = $want
    }
    if (-not $NoOverlay) {
        Write-Host "Overlay area $area bytes"
        if ($OvlArea -le 0) {
            # The sets with less than 2 KB left (the manager needs 1 KB).
            $tight = @()
            foreach ($set in @($together) + @($apart)) {
                $left = $area - (Get-SetSum $set)
                if ($left -ge 0 -and $left -lt 2048) { $tight += "$($set -join '+') $left" }
                if ($left -ge 0 -and $left -lt 1024) {
                    Remove-Item $exe -ErrorAction SilentlyContinue      # no stale program for the tests
                    throw "overlay sections $($set -join ' + ') fit in the area with only $left bytes left (need 1024 or no fit: build.ps1 `$apart, docs/design.md)"
                }
            }
            if ($tight.Count) { Write-Host "Overlay sets with under 2 KB left: $($tight -join ', ')" }
            foreach ($set in $beside) {
                $left = $area - (Get-SetSum $set)
                if ($left -lt 0) {
                    Remove-Item $exe -ErrorAction SilentlyContinue
                    throw "overlay sections $($set -join ' + ') do not fit in the area ($left bytes left; build.ps1 `$beside)"
                }
                Write-Host "Beside: $($set -join '+') $left bytes left"
            }
        }
        if (-not $NoOvlFix) { & "$root\tools\ovlfix.ps1" $exe "$out\xcomxt.map" }
    }
} finally {
    Pop-Location
}
Write-Host "Built $exe"
