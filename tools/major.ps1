# SPDX-License-Identifier: GPL-3.0-or-later
# The "major round": the longer scripts that smoke.ps1 leaves out, run in parallel lanes
# (tools\lanes.ps1: each lane a copy of emu\ under build\lanes\<n>\, each run stopped a
# few seconds after its last expected line). Same table format as smoke.ps1; logs and
# screenshots go to build\major\. Exit code = number of failures.
#   .\tools\major.ps1                   all of them, 6 lanes (about 20 minutes)
#   .\tools\major.ps1 -Only terror,sound
#   .\tools\major.ps1 -Lanes 1          one after the other
#   .\tools\major.ps1 -Full             no early stop
#   .\tools\major.ps1 -Cpu 286 -Only cpu286,m286e   the rows with the 286 build (XCOM286.EXE's;
#                                       results in build\major-286\)
# Call it inside PowerShell (& .\tools\major.ps1 -Only a,b), not with powershell -File.
#   Chain: scripts that hand each other their saves (CAMP1 -> CAMP2 -> CAMP3,
#          QSAVE -> QLOAD) run in one lane in order; the *.SAV files each leaves on
#          its disk go onto the disk of the next one (-Extra).
#   Timing: INTRO's frame times: runs last, alone, so that no other 86Box slows the
#          host; its "frames shown, n late" line is printed with the result.
# Options, waits and machines are the rows of tests\README.md.
param(
    [string[]]$Only = @(),
    [int]$Lanes = 6,
    [int]$MaxMin = 45,     # give up after this many minutes (0 = never)
    [switch]$Full,
    [ValidateSet('', 'EMS', 'XMS')] [string]$Board = '',   # the rows on the EMS board machine (tools\lanes.ps1)
    [switch]$WithSB,       # also the Sound Blaster tests (SB rows)
    [ValidateSet('', '86', '186', '286')] [string]$Cpu = '',   # the build every row without its own Cpu runs (default 86)
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\lanes.ps1"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$out = "$root\build\major"
if ($Board) { $out += '-' + $Board.ToLower() }
if ($Cpu -and $Cpu -ne '86') { $out += "-$Cpu" }

# ramp* rows: a unit that left the cabin for the ground below its click (bplayer.c)
$rampOut = 'walked to [0-9]+,[0-9]+,0 \(target [0-9]+,[0-9]+,0\)'

$tests = @(
    @{ Name = 'ovlwalk'; Machine = 'xt88ega'; Opts = 'DEBUG SEEALL SEED 7'; Script = 'OVLWALK.TXT'; Wait = 600
       Expect = @('BRIEF', 'minimap level', 'result -2', 'load slot 3: ok', 'BRIEF', 'result -2', 'script finished') },
    @{ Name = 'camp1'; Machine = 'xt88ega'; Opts = 'DEBUG SEEALL SEED 2'; Script = 'CAMP1.TXT'; Wait = 360; Chain = 'camp'
       Expect = @('BASE Building Alien Containment', 'AIR UFO CRASH LANDS!', 'UFO-1 shot down! Crash site marked',
                  'BRIEF', 'pack MO1E.PAK', 'is unconscious!', 'result 1 ', 'LOOT .* live 1',
                  'BASE research Sectoid autopsy', 'Research complete: Sectoid autopsy',
                  'Research complete: Sectoid Soldier', 'Research complete: Alien Origins',
                  'save slot 1 ', 'load slot 1: ok') },
    @{ Name = 'camp2'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 2'; Script = 'CAMP2.TXT'; Wait = 240; Chain = 'camp'
       Expect = @('load slot 1: ok', 'PSI debug: research and kit granted', 'BASE Building Psionic Laboratory',
                  'PSI .* trained: skill', 'REPORT rating', 'save slot 2 .*: ok') },
    @{ Name = 'camp3'; Machine = 'xt88ega'; Opts = 'DEBUG ALIENS 1 SEED 3 RACE RSE.PAK'; Script = 'CAMP3.TXT'; Wait = 240; Chain = 'camp'
       Expect = @('load slot 2: ok', 'battleship UFO-.* sent against ', 'squad [0-9]+ soldiers', 'pack BASEE.PAK', 'result ') },
    @{ Name = 'geo3'; Machine = 'xt88ega'; Opts = 'SEED 541'; Script = 'GEO3.TXT'; Wait = 200
       Expect = @('at the site', 'pack FA2E.PAK', 'result -2', 'Retreat: ') },
    @{ Name = 'geo4'; Machine = 'xt88ega'; Opts = 'SEED 541'; Script = 'GEO4.TXT'; Wait = 200
       Expect = @('at the site', 'result -2', 'save slot 0 .*: ok') },
    @{ Name = 'qsave'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG SEEALL SEED 16'; Script = 'QSAVE.TXT'; Wait = 600; Chain = 'quick'
       Expect = @('^save slot 4 "quick" .*: ok', '^bsave state:', '^result 1 kills 5 lost 3',
                  '^load slot 4: ok, quick battle', '^bload state:', 'game abandoned',
                  '^load slot 4: ok, quick battle', '^load slot 4: ok, quick battle',
                  '^load slot 3: ok', '^load state:', 'Campaign loaded')
       # The same keys after the save and after loading it: the same LOG.
       Cmp = @(@('^bsave state', 1, '^result ', 1, '^bload state', 1, '^result ', 1),
               @('^bload state', 1, '^result ', 1, '^bload state', 2, '^result ', 1)) },
    @{ Name = 'qload'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG SEEALL SEED 16'; Script = 'QLOAD.TXT'; Wait = 300; Chain = 'quick'
       Expect = @('^load slot 4: ok, quick battle', '^bload state:', '^result 1 kills 5 lost 3',
                  '^load slot 3: ok', 'Campaign loaded', 'delete slot 4: ok', 'quit to DOS') },
    # What X-COM knows at the start (mapgen.c map_reveal): the craft's block and a ring, a screenshot of it on turn 1;
    # with the craft against the map's edge the ring is cut; the alien base's lifts, level 1.
    @{ Name = 'reveal'; Machine = 'xt88ega'; Opts = 'BATTLE SEED 3 RACE RSE.PAK MISSION JU4E.PAK'; Script = 'REVEAL.TXT'; Wait = 120
       Expect = @('^known at the start: 1056 tiles', 'script finished') },
    @{ Name = 'revealfa'; Machine = 'xt88ega'; Opts = 'BATTLE SEED 3 RACE RSE.PAK MISSION FA1E.PAK'; Script = 'REVEAL.TXT'; Wait = 120
       Expect = @('^known at the start: 968 tiles', 'script finished') },
    @{ Name = 'revealub'; Machine = 'xt88ega'; Opts = 'BATTLE SEED 3 RACE RSE.PAK MISSION UBASEE.PAK'; Script = 'REVEAL.TXT'; Wait = 120
       Expect = @('^known at the start: 200 tiles', 'script finished') },
    # XS24 campaign saves (tests\xs24, made by the build before the limits branch) load, are
    # converted and saved as XS25 (a second time too); tools\xs24check.py compares the files.
    @{ Name = 'xs24'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'XS24LOAD.TXT'; Wait = 300; Chain = 'xs24'
       Extra = @(Get-ChildItem "$root\tests\xs24\*.SAV" | ForEach-Object { $_.FullName })
       Expect = @('^load slot 1: ok', '^load slot 0: ok', '^load slot 4: ok', '^load slot 3: ok', '^load slot 5: ok',
                  '^load slot 6: ok', '^save slot 9 ') },
    # The converted XS24 campaign goes on: the research and the manufacturing of PROJECTS.TXT
    # (XCOMXT4.SAV, saved by that script in the build before) finish as they did there.
    @{ Name = 'xs24run'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'XS24RUN.TXT'; Wait = 300
       Extra = @("$root\tests\xs24\XCOMXT4.SAV")
       Expect = @('^load slot 4: ok', 'Production complete: Motion Scanner', 'Research complete: Laser Weapons',
                  'Research complete: Alien Food', 'Production complete: Medi-Kit') },
    @{ Name = 'troops'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'TROOPS.TXT'; Wait = 300
       Expect = @('CRAFT SKYRANGER-2 bought', 'SKYRANGER-2 has arrived', 'SKYRANGER-1 launched\.', 'SKYRANGER-2 launched\.',
                  'GEO SKYRANGER-1 carries 4 of 8', 'result -2', 'GEO SKYRANGER-2 carries 4 of 8', 'result -2') },
    @{ Name = 'endgame'; Machine = 'xt88ega'; Opts = 'DEBUG SEEALL SEED 7'; Script = 'ENDGAME.TXT'; Wait = 480
       Expect = @('Research complete: Sectoid Navigator', 'Research complete: Alien Origins',
                  'Research complete: Sectoid Leader', 'Research complete: The Martian Solution',
                  'Research complete: Sectoid Commander', 'Research complete: Cydonia or Bust',
                  'Cydonia needs a ready Avenger', 'Research complete: Ultimate Craft',
                  'CRAFT AVENGER-1 added to the hangar', 'AVENGER-1 carries 8 of 8 soldiers to Cydonia',
                  'The alien brain is destroyed!', 'Victory!') },
    @{ Name = 'psigeo'; Machine = 'xt88ega'; Opts = 'DEBUG SEEALL SEED 7'; Script = 'PSIGEO.TXT'; Wait = 300
       Expect = @('PSI debug: research and kit granted', 'BASE Building Psionic Laboratory', 'PSI .* trained: skill',
                  'BRIEF', 'mind probe on', 'psi panic by', 'save slot 3', 'load slot 3: ok', 'PSI debug: Psi training') },
    @{ Name = 'craftui'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'CRAFTUI.TXT'; Wait = 180
       Expect = @('BASE debug: armour granted', 'CREW .* unassigned', 'CREW .* in SKYRANGER-1', 'CREW .* moved to place 1$',
                  'CREW .* unassigned', 'CREW .* moved to place 5$', 'BASE equip .*: PERSONAL ARMOUR',
                  'BASE equip .*: POWER SUIT', 'CRAFT SKYRANGER-1 Pistol 4 aboard', 'CRAFT SKYRANGER-1 Pistol 5 aboard',
                  'CRAFT INTERCEPTOR-1 weapon 1: AVALANCHE', 'CRAFT INTERCEPTOR-1 weapon 2: -', 'script finished') },
    @{ Name = 'craftloss'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'CRAFTLOS.TXT'; Wait = 60
       Expect = @('^CRAFT SKYRANGER-1 lost, 4 killed', 'GEO debug: SKYRANGER-1 lost, soldiers 8 -> 4', 'script finished') },
    @{ Name = 'craftarm'; Machine = 'xt88ega'; Opts = 'SEED 7'; Script = 'CRAFTARM.TXT'; Wait = 160
       Expect = @('CRAFT INTERCEPTOR-1 weapon 1: CANNON', 'CRAFT INTERCEPTOR-1 weapon 1: no Cannon Rounds\(x50\) left, ammo 50',
                  'WIN Not enough Cannon Rounds\(x50\) to rearm INTERCEPTOR-1 at .*: OK', 'script finished') },
    @{ Name = 'eqcraft'; Machine = 'xt88ega'; Opts = 'SEED 541'; Script = 'EQCRAFT.TXT'; Wait = 75
       Expect = @('CRAFT SKYRANGER-1 Smoke Grenade 1 aboard', 'Smoke Grenade 0 aboard') },
    @{ Name = 'itemord'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'ITEMORD.TXT'; Wait = 120
       Expect = @('^BUY items: 40 41 42 43 3E 3F 63 68 64 69 00 09 01 0A 02 0B 5A 5B 38 39 5C 5D 3A 3B 5E 5F 0F 60 61 11 17$',
                  '^SELL items: 40 41 42 43 3E 3F 00 09 01 0A 02 0B 38 39 3A 3B 0F 60$',
                  '^EQUIPLIST items: 00 09 01 0A 02 0B 5A 38 39 3A 3B 0F 60$',
                  '^BASE research Laser Weapons: 10 scientists$', '^BASE research Laser Weapons: 10 scientists$',
                  'script finished') },
    @{ Name = 'equip'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'EQUIP.TXT'; Wait = 420
       Expect = @('CRAFT SKYRANGER-1 Smoke Grenade 5 aboard', 'EQUIP Tenagne Dabir: Rifle Rifle Clip Grenade Smoke Grenade',
                  'WIN Inventory before the battle', 'LAYOUT Ruslan Pavlov: 4 items', 'result -2',
                  'REEQUIP Smoke Grenade 1 SKYRANGER-1', 'EQUIP Ruslan Pavlov: Heavy Cannon .*Grenade', 'result -2') },
    @{ Name = 'crafts'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'CRAFT.TXT'; Wait = 480
       Expect = @('BASE research UFO Construction', 'Research complete: Fusion Missile',
                  'CRAFT LIGHTNING-1 added to the hangar', 'CRAFT LIGHTNING-1 weapon 1: FUSION BALL',
                  'SELL 1 UFO Power Source', 'GEO .* carries 8 of 10 soldiers', 'squad 8 soldiers', 'result -2') },
    @{ Name = 'tankcamp'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'TANKCAMP.TXT'; Wait = 300
       Expect = @('CRAFT SKYRANGER-1 HWP Tank/Cannon aboard \(1 of 3\)', 'GEO SKYRANGER-1 carries 1 HWPs', 'result -2',
                  'HWP Tank/Cannon back with 28 of 30 rounds', 'CRAFT SKYRANGER-1 HWP Tank/Cannon: 1 of 1 re-armed') },
    @{ Name = 'sellcrft'; Machine = 'xt88ega'; Opts = 'SEED 3'; Script = 'SELLCRFT.TXT'; Wait = 90
       Expect = @('SELL craft SKYRANGER-1', 'SELL craft INTERCEPTOR-1', 'SELL craft INTERCEPTOR-2',
                  'SELL 3 Stingray Launcher', 'SELL 8 Rifle') },
    @{ Name = 'baseui'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'BASEUI.TXT'; Wait = 150
       Expect = @('BASE menu: 11 buttons$', 'BASE buy 1 Soldier for \$40000, due in 72h',
                  'BASE buy 9 Scientist for \$540000, due in 72h', 'BASE buy 1 Rifle for \$3000, due in 24h',
                  'BASE buy 10 Heavy Cannon for \$64000, due in 24h',
                  'TRANSFERS Rifle 1 24', 'TRANSFERS Heavy Cannon 10 24', 'TRANSFERS Soldiers \(recruits\) 1 72',
                  'TRANSFERS Scientists 9 72', 'CRAFT SKYRANGER-1 Pistol 4 aboard', 'Pistol 3 aboard',
                  'Pistol [4-9] aboard', 'Pistol 0 aboard', 'SELL craft SKYRANGER-1', 'SELL craft INTERCEPTOR-1',
                  'script finished') },
    @{ Name = 'baseui2'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'BASEUI2.TXT'; Wait = 200
       Expect = @('XFER soldier .* to North America, 14h', 'XFER soldier .* to North America, 14h',
                  'XFER soldier .* to North America, 14h', 'XFER 4 Grenade', 'XFER 8 Rifle Clip',
                  'BASE North America selected', 'TRANSFERS Grenade 4 14', 'TRANSFERS Rifle Clip 8 14',
                  'TRANSFERS Soldier .* 1 14', 'script finished') },
    @{ Name = 'baseuicga'; Machine = 'xt88cga'; Opts = 'DEBUG SEED 7'; Script = 'BASEUI.TXT'; Wait = 150
       Expect = @('BASE buy 10 Heavy Cannon for \$64000, due in 24h', 'TRANSFERS Scientists 9 72',
                  'SELL craft INTERCEPTOR-1', 'script finished') },
    @{ Name = 'baseuiherc'; Machine = 'xt88herc'; Opts = 'DEBUG SEED 7'; Script = 'BASEUI.TXT'; Wait = 200
       Expect = @('^video: Hercules, 640x200 from the CGA picture', 'BASE buy 10 Heavy Cannon for \$64000, due in 24h', 'TRANSFERS Scientists 9 72',
                  'SELL craft INTERCEPTOR-1', 'script finished') },
    @{ Name = 'crewwnd'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CREWWND.TXT'; Wait = 90
       Expect = @('GEO debug: .* wounded', 'CREW .* wounded, unassigned', 'CREW .* wounded', 'CREW .* unassigned') },
    @{ Name = 'crewseat'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CREWSEAT.TXT'; Wait = 200
       Expect = @('GEO debug: .* wounded', 'CREW SKYRANGER-1: 8 seated, 7 fit', 'GEO SKYRANGER-1 carries 7 of 7 soldiers',
                  'squad 7 soldiers', 'result -2') },
    @{ Name = 'woundlnd'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'WOUNDLND.TXT'; Wait = 300
       Expect = @('GEO debug: .* wounded', 'result -2', 'CREW .* wounded, off SKYRANGER-1', 'SKYRANGER-1 back at base') },
    @{ Name = 'crewuns'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CREWUNS.TXT'; Wait = 120
       Expect = @('GEO debug: .* wounded', 'CREW SKYRANGER-1: 8 seated, 7 fit', 'CREW .* wounded, unassigned',
                  'CREW SKYRANGER-1: 7 seated, 7 fit', 'script finished') },
    @{ Name = 'contain'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CONTAIN.TXT'; Wait = 150
       Expect = @('CONTAIN screen: 3 kinds, 3 of 10', 'CONTAIN removed 1 Sectoid Commander: 3 Sectoid Corpse', 'CONTAIN removed 1 Sectoid Leader: 4 Sectoid Corpse', 'CONTAIN screen: 1 kinds, 1 of 10', 'script finished') },
    @{ Name = 'contover'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CONTOVER.TXT'; Wait = 240
       Expect = @('DEBRIEF TOTAL', 'CONTAIN screen: 3 kinds, 3 of 0', 'CONTAIN removed 1 Sectoid Commander', 'CONTAIN removed 1 Sectoid Leader', 'CONTAIN removed 1 Sectoid Navigator', 'script finished') },
    # The new-production list (DEBUG '!' researches everything): whole category names, Heavy
    # Weapons Platform for every tank (screenshot).
    @{ Name = 'manuflst'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'MANUFLST.TXT'; Wait = 120
       Expect = @('MANUF new production list: [0-9]+ items', 'MANUF new production list: [0-9][0-9] items', 'script finished') },
    # The clock after a pop-up's OK (GeoscapeState::popup breaks the step, timerReset is the
    # buttons'): Production complete at 1 Hr speed, OK, and the clock is still running.
    @{ Name = 'clock'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CLOCK.TXT'; Wait = 240
       Expect = @('BASE manufacture Laser Pistol x1, 10 engineers', 'Production complete: Laser Pistol',
                  'WIN Production of Laser Pistol at .*: OK', 'GEO pop-ups done: clock running', 'script finished')
       Absent = @('clock paused') },
    # OpenXcom's SELL toggle on the production screen: every unit made is sold at its sale price
    # (Production::step), none goes to the stores.
    @{ Name = 'sellprod'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'SELLPROD.TXT'; Wait = 240
       Expect = @('BASE manufacture Laser Pistol sell on', 'BASE manufacture Laser Pistol xinf, 10 engineers',
                  'BASE sold Laser Pistol for \$20000', 'BASE sold Laser Pistol for \$20000', 'script finished') },
    # An interrogation holds its alien's containment space (Base::getUsedContainment): the
    # containment screen counts the studied alien until the project ends.
    @{ Name = 'contstudy'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CONTSTD.TXT'; Wait = 300
       Expect = @('CONTAIN screen: 3 kinds, 3 of 10', 'CONTAIN studied: 0', 'BASE research Sectoid Commander',
                  'CONTAIN screen: 3 kinds, 3 of 10', 'CONTAIN studied: 1', 'Research complete: Sectoid Commander',
                  'CONTAIN screen: 2 kinds, 2 of 10', 'CONTAIN studied: 0') },
    # Items Arriving (ItemsArrivingState: the pop-up when an order or transfer comes in; AUTO
    # answers OK, then NOAUTO leaves the second one open for the screenshot) and Tab opening the
    # selected UFO's window (TargetInfoState / UfoDetectedState) instead of a line under the map.
    @{ Name = 'arrive'; Machine = 'xt88ega'; Opts = 'SEED 3'; Script = 'ARRIVE.TXT'; Wait = 400
       Expect = @('BASE buy 2 Rifle', 'Transfer arrived at', 'WIN Items Arriving: 1 rows', 'WIN Items Arriving: OK',
                  'GEO pop-ups done: clock running', 'BASE buy 1 Rifle', 'WIN Items Arriving: 1 rows', 'script finished') },
    @{ Name = 'tabwin'; Machine = 'xt88ega'; Opts = 'SEED 2'; Script = 'TABWIN.TXT'; Wait = 200
       Expect = @('detected: ', 'WIN [A-Z -]+[0-9]+: Cancel', 'WIN [A-Z -]+[0-9]+: [A-Z ]+, [A-Z ]+, [0-9]+ knots', 'script finished') },
    # Manage Alien Containment in DOS's column order (Alien, Live, Rejected with the arrows, Being
    # Studied): one interrogation running, one alien rejected (screenshot).
    @{ Name = 'contcol'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CONTCOL.TXT'; Wait = 150
       Expect = @('CONTAIN screen: 3 kinds, 3 of 10', 'CONTAIN studied: 1', 'script finished') },
    @{ Name = 'craftfig'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'CRAFTFIG.TXT'; Wait = 240
       Expect = @('FIGURES 8 0 [0-9]+', 'CRAFT SKYRANGER-1 HWP Tank/Cannon aboard',
                  'FIGURES 8 1 [0-9]+', 'script finished') },
    @{ Name = 'craftfigc'; Machine = 'xt88cga'; Opts = 'DEBUG SEED 3'; Script = 'CRAFTFIG.TXT'; Wait = 240
       Expect = @('FIGURES 8 0 [0-9]+', 'CRAFT SKYRANGER-1 HWP Tank/Cannon aboard', 'FIGURES 8 1 [0-9]+', 'script finished') },
    @{ Name = 'pediapg'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'PEDIAPG.TXT'; Wait = 120
       Expect = @('X-COM base operational', 'script finished') },
    @{ Name = 'qlpedia'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'QLPEDIA.TXT'; Wait = 150
       Expect = @('script finished') },
    @{ Name = 'menus'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'MENUS.TXT'; Wait = 200
       Expect = @('BASE menu: 11 buttons', '^SELL sacked ', 'BASE menu: 11 buttons', 'script finished') },
    @{ Name = 'flare'; Machine = 'xt88ega'; Opts = 'BATTLE GEAR NIGHT SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'FLARE.TXT'; Wait = 110
       Expect = @('T1: No aliens in sight', 'Silva walked to 15,16,0', 'Silva throws at 16,5,0, lands 16,5,0',
                  'Electro-flare lights the area', 'T1: in sight: Sectoid at 15,0,1') },
    @{ Name = 'longwalk'; Machine = 'xt88ega'; Opts = 'BATTLE NIGHT SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'LONGWALK.TXT'; Wait = 200
       Expect = @('T1: Kowalski walked to 14,19,1 \(target 14,19,1\) TU 59', 'T1: Not enough time units',
                  'walked to 19,10,0 \(target 14,0,0\) TU 1', 'T1: Not enough time units',
                  'walked to 19,10,0 \(target 14,0,0\) TU 1', 'Hidden movement', 'T2: Turn 2') },
    # Out of each transport down its ramp (or the Lightning's door) on a farm and a mountain
    # map, the cursor on the cabin's level: a click beside the craft means the ground below
    # (RAMP*.TXT; the tank, front, back and side seats; one alien, seeds where it does not
    # shoot at the ramp).
    @{ Name = 'rampsky'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 8 MISSION FA1E.PAK RACE RSE.PAK ALIENS 1 CRAFT S HWP 1'; Script = 'RAMPN.TXT'; Wait = 240
       Expect = (@($rampOut) * 9) + 'script finished'; Absent = @('Cannot get there') },
    @{ Name = 'rampskyh'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 8 MISSION MO1E.PAK RACE RSE.PAK ALIENS 1 CRAFT S HWP 1'; Script = 'RAMPN.TXT'; Wait = 240
       Expect = (@($rampOut) * 9) + 'script finished'; Absent = @('Cannot get there') },
    @{ Name = 'ramplit'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 5 MISSION FA1E.PAK RACE RSE.PAK ALIENS 1 CRAFT L SQUAD 10'; Script = 'RAMPE.TXT'; Wait = 240
       Expect = (@($rampOut) * 10) + 'script finished'; Absent = @('Cannot get there') },
    @{ Name = 'ramplith'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 5 MISSION MO1E.PAK RACE RSE.PAK ALIENS 1 CRAFT L SQUAD 10'; Script = 'RAMPE.TXT'; Wait = 240
       Expect = (@($rampOut) * 10) + 'script finished'; Absent = @('Cannot get there') },
    @{ Name = 'rampave'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 8 MISSION FA1E.PAK RACE RSE.PAK ALIENS 1 CRAFT A SQUAD 26 HWP 1'; Script = 'RAMPA.TXT'; Wait = 360
       Expect = (@($rampOut) * 9) + 'script finished'; Absent = @('Cannot get there') },
    @{ Name = 'rampaveh'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 3 MISSION MO1E.PAK RACE RSE.PAK ALIENS 1 CRAFT A SQUAD 26 HWP 1'; Script = 'RAMPA.TXT'; Wait = 360
       Expect = (@($rampOut) * 9) + 'script finished'; Absent = @('Cannot get there') },
    # Off the side of a craft's ramp (Pathfinding::getTUCost bars only a climb of more than
    # 8): the front seat to the ground beside the ramp's middle (Skyranger, Avenger) or beside
    # the Lightning's upper step, not round the front (before: TU 28, 32, 29 left).
    @{ Name = 'rampside'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 8 MISSION FA1E.PAK RACE RSE.PAK ALIENS 1 CRAFT S'; Script = 'RAMPSIDE.TXT'; Wait = 200
       Expect = @('Kowalski walked to 13,17,0 \(target 13,17,0\) TU 44,', 'script finished') },
    @{ Name = 'rampsidea'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 8 MISSION FA1E.PAK RACE RSE.PAK ALIENS 1 CRAFT A SQUAD 26'; Script = 'RAMPSIDE.TXT'; Wait = 200
       Expect = @('Kowalski walked to 13,17,0 \(target 13,17,0\) TU 46,', 'script finished') },
    @{ Name = 'rampsidel'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 5 MISSION FA1E.PAK RACE RSE.PAK ALIENS 1 CRAFT L SQUAD 10'; Script = 'RAMPSL.TXT'; Wait = 200
       Expect = @('Kowalski walked to 22,6,0 \(target 22,6,0\) TU 39,', 'script finished') },
    @{ Name = 'flysuit'; Machine = 'xt88ega'; Opts = 'BATTLE ARMOUR 3 SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'FLYSUIT.TXT'; Wait = 200
       Expect = @('flying suit: the hovering legs, 4524 bytes', 'walked to 14,19,1',
                  'walked to 14,16,1 \(target 14,16,1\) TU 47', 'script finished') },
    # Kowalski off the ramp's side to the shop door (TU 17 left; round the front before the
    # step rule: TU 5, facing away); he arrives facing it, so the first right click opens it
    # (unitOpensDoor with rClick) and the second does nothing. Paced turns: the pace row.
    @{ Name = 'door'; Machine = 'xt88ega'; Opts = 'BATTLE ALIENS 1 DIFF 0 SEED 16 MISSION UR0E.PAK PACELOG'; Script = 'DOOR.TXT'; Wait = 200
       Expect = @('Kowalski walked to 38,16,0 \(target 38,16,0\) TU 17', 'RCLICK 176 96',
                  'Kowalski opened the door at 38,16,0 \(4 TU\), TU 13', 'RCLICK 176 96', 'script finished') },
    # The same UFO-type door (URBAN's sliding door, two parts along x = 38): its other part
    # opens with it (TileEngine::checkAdjacentDoors), one TU charge; both close at the end
    # of the turn although Kowalski stands in the doorway (closeUfoDoors); in turn 2 a
    # diagonal step north-west through it opens both again (the walk's unitOpensDoor, 4 TU +
    # 6). SHOTLOG: TILE lines.
    @{ Name = 'doorufo'; Machine = 'xt88ega'; Opts = 'BATTLE ALIENS 1 DIFF 0 SEED 16 MISSION UR0E.PAK SHOTLOG'; Script = 'DOORUFO.TXT'; Wait = 240
       Expect = @('Kowalski walked to 38,16,0 \(target 38,16,0\) TU 17', 'TILE 38,16,0 [0-9]+ 107 ', 'TILE 38,15,0 [0-9]+ 107 ',
                  'Kowalski opened the door at 38,16,0 \(4 TU\), TU 13', 'TILE 38,15,0 [0-9]+ 106 ', 'TILE 38,16,0 [0-9]+ 106 ',
                  'T2: Turn 2', 'TILE 38,16,0 [0-9]+ 107 ', 'TILE 38,15,0 [0-9]+ 107 ',
                  'T2: Kowalski walked to 37,15,0 \(target 37,15,0\) TU 43') },
    # The tank walks west through that door (UR0E seed 16, HWP 1): the walk opens both parts
    # (unitOpensDoor without rClick for a 2x2 unit), and standing across it at 37,15 the tank
    # keeps them open at the end of the turn (closeUfoDoors).
    @{ Name = 'doorhwp'; Machine = 'xt88ega'; Opts = 'BATTLE ALIENS 1 DIFF 0 SEED 16 MISSION UR0E.PAK SHOTLOG HWP 1'; Script = 'DOORHWP.TXT'; Wait = 300
       Expect = @('TILE 38,15,0 [0-9]+ 107 ', 'TILE 38,16,0 [0-9]+ 107 ', 'T1: Tank/Cannon walked to 37,15,0 \(target 37,15,0\) TU 13',
                  'T2: Turn 2')
       Absent = @('TILE 38,1[56],0 [0-9]+ 106 ') },
    # A tank at a swinging door (FA1 seed 1, HWP 1: the barn door on the west wall of 10,21). Walls beside it first:
    # no way in. With the wall at 10,20 gone (DEBUG Y with SHOTLOG) the footprint 9,20 would fit, but its last quarter
    # has the door (Pathfinding::getTUCost: `don't let tanks phase through doors`): still no way in. With the wall at
    # 10,22 gone the door lies on another quarter: the tank crosses it shut (Tile::openDoor: not for a 2x2 unit) and
    # pays the average of its four parts: (4 x 4 + 8) / 4 = 6 TU a step.
    @{ Name = 'doortank'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG ALIENS 1 DIFF 0 SEED 1 MISSION FA1E.PAK RACE RSE.PAK SHOTLOG HWP 1'; Script = 'DOORTANK.TXT'; Wait = 200
       Expect = @('T1: Tank/Cannon walked to 8,21,0 \(target 8,21,0\) TU 21', 'T2: Cannot get there', 'DEBUG Y clears the walls at 10,20,0',
                  'T2: Cannot get there', 'DEBUG Y clears the walls at 10,22,0', 'T2: Tank/Cannon walked to 9,21,0 \(target 9,21,0\) TU 64',
                  'T2: Tank/Cannon walked to 10,21,0 \(target 10,21,0\) TU 58', 'T3: Turn 3')
       Absent = @('TILE 10,21,0') },
    # A soldier through that door: 4 + 8 TU with the door shut (Tile::getTUCost), charged with the step
    # (UnitWalkBState: tu before unitOpensDoor); back through the opening it left, 4. The screenshot: the next door's cost.
    @{ Name = 'doortu'; Machine = 'xt88ega'; Opts = 'BATTLE ALIENS 1 DIFF 0 SEED 1 MISSION FA1E.PAK RACE RSE.PAK SHOTLOG'; Script = 'DOORTU.TXT'; Wait = 200
       Expect = @('T2: Kowalski walked to 9,21,0 \(target 9,21,0\) TU 40', 'TILE 10,21,0 52 0 51 0', 'T2: Kowalski walked to 10,21,0 \(target 10,21,0\) TU 28',
                  'T2: Kowalski walked to 9,21,0 \(target 9,21,0\) TU 24', 'T3: Turn 3', 'script finished') },
    # Special clips in the left hand's gun (FA1 seed 5, GEAR: Tanaka's Heavy Cannon): by Reload, by a clip put on it in
    # the inventory; each time the shot blasts (power 52: HE).
    @{ Name = 'lhclip'; Machine = 'xt88ega'; Opts = 'BATTLE GEAR SHOTLOG ALIENS 1 DIFF 0 SEED 5 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'LHCLIP.TXT'; Wait = 300
       Expect = @('Tanaka moved Heavy Cannon to the left hand', 'Reloaded: HC-HE Ammo', 'SHOT T2', 'blast at [0-9,]+ power 52', 'Tanaka unloaded Heavy Cannon',
                  'Tanaka moved Heavy Cannon to the left hand', 'Tanaka loaded Heavy Cannon HE', 'SHOT T4', 'blast at [0-9,]+ power 52', 'T5: Turn 5') },
    # The left hand's gun keeps its special clip in the layout (EquipmentLayoutItem's ammoItem): the next battle's first soldier
    # has it there with HE.
    @{ Name = 'eqleft'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'EQLEFT.TXT'; Wait = 420
       Expect = @('Ruslan Pavlov moved Heavy Cannon to the left hand', 'Ruslan Pavlov loaded Heavy Cannon HE', 'LAYOUT Ruslan Pavlov: 4 items', 'result -2',
                  'EQUIP Ruslan Pavlov: Heavy Cannon', 'Ruslan Pavlov armed Heavy Cannon HE', 'script finished') },
    # A landed medium scout's door seen from outside (FA2 seed 8: the UFO at 20,0, its door
    # on 25,9,0; the cursor goes there, then off to the side): the door part is drawn with a
    # black outline on every video (tools\convert.py door_frame), screenshot only.
    @{ Name = 'doorlook'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 8 MISSION FA2E.PAK RACE RSE.PAK'; Script = 'DOORLOOK.TXT'; Wait = 240
       Expect = @('ufo at 20,0', 'script finished') },
    @{ Name = 'doorlookc'; Machine = 'xt88cga'; Opts = 'BATTLE SEEALL SEED 8 MISSION FA2C.PAK RACE RSC.PAK'; Script = 'DOORLOOK.TXT'; Wait = 240
       Expect = @('ufo at 20,0', 'script finished') },
    @{ Name = 'doorlookh'; Machine = 'xt88herc'; Opts = 'BATTLE SEEALL SEED 8 MISSION FA2C.PAK RACE RSC.PAK'; Script = 'DOORLOOK.TXT'; Wait = 240
       Expect = @('ufo at 20,0', 'script finished') },
    # A farmhouse's upper floor (FA1E seed 1): Kowalski goes through its door at 11,24,1 (the
    # walk opens it, 12 TU), turns it back with a right click from 11,24 (8 TU), climbs to the
    # stair top 12,24,1 (terrain level -16) and in turn 8 right-clicks the same door north-west of
    # him twice: on stairs DOS looks one level up (unitOpensDoor), so nothing opens and no TU
    # go (before: "opened the door at 11,24,1 (8 TU), TU 37", then TU 25 at 12,25,1).
    @{ Name = 'doorstr'; Machine = 'xt88ega'; Opts = 'BATTLE ALIENS 1 DIFF 0 SEED 1 MISSION FA1E.PAK RACE RSE.PAK SHOTLOG'; Script = 'DOORSTR.TXT'; Wait = 900
       Expect = @('T5: Kowalski walked to 11,23,1', 'TILE 11,24,1 33 50 0 0', 'T6: Kowalski walked to 11,24,1 \(target 11,24,1\) TU 34',
                  'TILE 11,24,1 33 0 51 0', 'T6: Kowalski opened the door at 11,24,1 \(8 TU\), TU 24',
                  'T7: Kowalski walked to 12,24,1 \(target 12,24,1\) TU', 'T8: Kowalski walked to 12,25,1 \(target 12,25,1\) TU 33',
                  'T9: Turn 9')
       Absent = @('T8: Kowalski opened the door', 'T8: Not enough time units') },
    @{ Name = 'mousebat'; Machine = 'xt88ega'; Mouse = $true; Opts = 'BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'MOUSEBAT.TXT'; Wait = 200
       Expect = @('mouse: driver found', 'MARKS u0 1 8', 'CLICK 306 133', 'T1: view on Sectoid at 15,0,1: -96,-24',
                  'MOVE 96 72', 'CLICK 96 72', 'Kowalski walked to 10,19,0 \(target 10,19,0\) TU 30') },
    # The selected unit's arrow (bplayer.c arrow_to, src/arrow.asm): gone while Kowalski walks
    # out of the Skyranger, over him again, lower when he kneels, then on the next soldier (Tab)
    # with no copy left over Kowalski; the screenshot shows it (yellow / brown / white).
    @{ Name = 'arrow'; Machine = 'xt88ega'; Mouse = $true; Opts = 'BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'ARROW.TXT'; Wait = 200
       Expect = @('ARROW u0 -68,233,1$', 'ARROW u-1 0,0,0$', 'Kowalski walked to 10,19,0 \(target 10,19,0\) TU 30', 'ARROW u0 -132,225,0$', 'ARROW u0 -132,231,0$', 'ARROW u1 -52,241,1$', 'script finished') },
    @{ Name = 'arrowcga'; Machine = 'xt88cga'; Mouse = $true; Opts = 'BATTLE SEED 8 MISSION FA1C.PAK RACE RSC.PAK'; Script = 'ARROW.TXT'; Wait = 200
       Expect = @('ARROW u0 -68,233,1$', 'ARROW u-1 0,0,0$', 'Kowalski walked to 10,19,0 \(target 10,19,0\) TU 30', 'ARROW u0 -132,225,0$', 'ARROW u0 -132,231,0$', 'ARROW u1 -52,241,1$', 'script finished') },
    @{ Name = 'arrowherc'; Machine = 'xt88herc'; Mouse = $true; Opts = 'BATTLE SEED 8 MISSION FA1C.PAK RACE RSC.PAK'; Script = 'ARROW.TXT'; Wait = 240
       Expect = @('^video: Hercules, 640x200 from the CGA picture', 'ARROW u0 -68,233,1$', 'ARROW u-1 0,0,0$', 'Kowalski walked to 10,19,0 \(target 10,19,0\) TU 30', 'ARROW u0 -132,225,0$', 'ARROW u0 -132,231,0$',
                  'ARROW u1 -52,241,1$', 'script finished') },
    # The medi-kit (bmedi.c bt_medikit, its own overlay section next to bplayer): Kowalski, the
    # quick battle's first soldier, uses his on himself (M: S, P, H).
    @{ Name = 'medikit'; Machine = 'xt88ega'; Opts = 'BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'MEDI.TXT'; Wait = 150
       Expect = @('Kowalski: H heal 10  S stim 10  P pain 10', 'Kowalski: stimulant given', 'Kowalski: H heal 10  S stim 9  P pain 10',
                  'Kowalski: painkiller given, morale', 'No fatal wounds to treat', 'script finished') },
    @{ Name = 'hullbl'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG SEEALL SHOTLOG SEED 1 MISSION FA2E.PAK RACE RSE.PAK'; Script = 'HULLBL.TXT'; Wait = 150
       Expect = @('DEBUG Z blasts 24,12,0', 'TILE 24,11,0 2 0 0 83', 'blast at 24,12,0 power 200',
                  'DEBUG Z blasts 20,15,0', 'TILE 21,15,0 3 0 0 61', 'T2: Turn 2') },
    # The night light (src/nlit.c): on where a UMB overlay area holds it, the night palette
    # elsewhere. nightlit: fires and a walk change the cells; nightplain: the same keys on a
    # plain XT; nightbench: every present compared with the back buffer through the cells.
    @{ Name = 'nightlit'; Machine = 'v20e'; Opts = 'BATTLE GEAR NIGHT SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'NIGHTLIT.TXT'; Wait = 240
       Expect = @('^night light: on', 'T1: No aliens in sight', 'walked to', 'incendiary at', 'T1: Mission aborted',
                  '^night light: [0-9]+ times made, [1-9][0-9]* changes, [1-9][0-9]* cells', '^result -2')
       Absent = @('^night: panel colours') },
    @{ Name = 'nightx'; Machine = 'v20x'; Opts = 'BATTLE GEAR NIGHT SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'NIGHTLIT.TXT'; Wait = 240
       Expect = @('^night light: on', 'incendiary at', '^night light: [0-9]+ times made, [1-9][0-9]* changes', '^result -2') },
    # The walk phases the packs leave out, from <pack>W.PAK with upper or extended memory
    # (src/floorld.c walk_load, tools/walkpak.py): XWALK=1 / 3 shows walk slot 1 / 3 in place of
    # the standing frames (look at the screenshot: the soldiers' legs in mid-stride); a plain XT
    # loads none.
    @{ Name = 'walk4e'; Machine = 'v20e'; Mouse = $true; Opts = 'BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'WALK4.TXT'; Wait = 200
       Pre = 'SET XWALK=1'
       Expect = @('^walk frames: [1-9] sets', 'walked to', 'script finished') },
    @{ Name = 'walk4x'; Machine = 'v20x'; Mouse = $true; Opts = 'BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'WALK4.TXT'; Wait = 200
       Pre = 'SET XWALK=3'
       Expect = @('^walk frames: [1-9] sets', 'walked to', 'script finished') },
    # The same walk's pacing (PACE w: ms a step shown), with the walk frames and without.
    @{ Name = 'walkpace'; Machine = 'v20e'; Mouse = $true; Opts = 'BATTLE PACELOG SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'WALK4.TXT'; Wait = 200
       Expect = @('^walk frames: [1-9] sets', 'walked to', 'walked to', 'script finished') },
    @{ Name = 'walkpaceoff'; Machine = 'v20e'; Mouse = $true; Opts = 'BATTLE PACELOG SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'WALK4.TXT'; Wait = 200
       Pre = 'SET XWALK=OFF'
       Expect = @('^walk frames: XWALK=OFF', 'walked to', 'walked to', 'script finished') },
    @{ Name = 'walkplain'; Machine = 'xt88ega'; Mouse = $true; Opts = 'BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'WALK4.TXT'; Wait = 200
       Expect = @('walked to', 'script finished')
       Absent = @('^walk frames') },
    @{ Name = 'nightplain'; Machine = 'xt88ega'; Opts = 'BATTLE GEAR NIGHT SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'NIGHTLIT.TXT'; Wait = 200
       Expect = @('^night light: off \(no overlay area in a UMB', '^night: panel colours in registers', 'incendiary at', '^result -2')
       Absent = @('^night light: on') },
    @{ Name = 'nightlamp'; Machine = 'v20e'; Opts = 'BATTLE DEBUG NIGHT SHOTLOG SEED 1 MISSION UR0E.PAK RACE RSE.PAK'; Script = 'LAMPOUT.TXT'; Wait = 300
       Expect = @('terrain light: 4 sources', '^night light: on', 'DEBUG Z blasts 34,13,0', 'terrain light: 3 sources') },
    @{ Name = 'nightsave'; Machine = 'v20e'; Opts = 'BATTLE NIGHT SEED 16'; Script = 'NIGHTSAV.TXT'; Wait = 300
       Expect = @('^night light: on', '^save slot 4', '^night light: [0-9]+ times made', '^load slot 4: ok', '^night light: [0-9]+ times made', '^result ')
       Absent = @('^night: panel colours') },
    @{ Name = 'nightbench'; Machine = 'v20e'; Opts = 'BENCH BATTLE SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'ONE.TXT'; Wait = 500
       Expect = @('^night light: on', '^night light check: 0 cells of lit tiles dark, 0 screen bytes differ') },
    @{ Name = 'lampout'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG NIGHT SHOTLOG SEED 1 MISSION UR0E.PAK RACE RSE.PAK'; Script = 'LAMPOUT.TXT'; Wait = 150
       Expect = @('terrain light: 4 sources', 'DEBUG Z blasts 34,13,0', 'TILE 34,13,0 72 0 0 0',
                  'minimap level 0 centre 34,13', 'terrain light: 3 sources') },
    @{ Name = 'crashbl'; Machine = 'xt88ega'; Opts = 'BATTLE CRASHED DIFF 0 SEEALL SEED 5 MISSION FA5E.PAK RACE RSE.PAK'; Script = 'CRASHBL.TXT'; Wait = 240
       Expect = @('crash: 2 of 2 UFO power sources blown up', 'rank 5: 4 aliens \(Beginner\)',
                  'blast at 33,39,0 power 207', 'Sectoid Medic died of wounds', 'blast at 45,39,0 power 243',
                  'crash: 0 Elerium-115 left', 'T2: Turn 2') },
    @{ Name = 'crashel'; Machine = 'xt88ega'; Opts = 'BATTLE CRASHED DIFF 0 SEEALL SEED 7 MISSION FA6E.PAK RACE RSE.PAK'; Script = 'CRASHBL.TXT'; Wait = 240
       Expect = @('crash: 3 of 4 UFO power sources blown up', 'blast at 14,6,0 power 192', 'crash: 0 Elerium-115 left', 'T2: Turn 2') },
    @{ Name = 'harvcga'; Machine = 'xt88cga'; Opts = 'BATTLE CRASHED DIFF 0 SEEALL SEED 5 MISSION FA4C.PAK RACE RSC.PAK'; Script = 'CRASHBL.TXT'; Wait = 240
       Expect = @('UFO power sources blown up', 'crash: [0-9]+ Elerium-115 left', 'T2: Turn 2') },
    @{ Name = 'geoclickherc'; Machine = 'xt88herc'; Opts = 'SEED 2'; Script = 'GEOCLICK.TXT'; Wait = 300
       Expect = @('^video: Hercules, 640x200 from the CGA picture', 'WIN TARGET: WAY POINT: OK', 'INTERCEPTOR-2 launched\.',
                  'WIN INTERCEPTOR-2: DESTINATION: WAY POINT', 'INTERCEPTOR-2 returning to base',
                  'WIN TARGET: UFO-1: OK', 'INTERCEPTOR-1 launched to UFO-1', 'AIR UFO CRASH LANDS!',
                  'WIN TARGET: CRASH SITE-1: OK', 'SKYRANGER-1 launched', 'SKYRANGER-1 returning to base',
                  'INTERCEPTOR-2 is patrolling', 'WIN INTERCEPTOR-2: PATROLLING',
                  'INTERCEPTOR-2 back at base') },
    @{ Name = 'harvherc'; Machine = 'xt88herc'; Opts = 'BATTLE CRASHED DIFF 0 SEEALL SEED 5 MISSION FA4C.PAK RACE RSC.PAK'; Script = 'CRASHBL.TXT'; Wait = 300
       Expect = @('^video: Hercules, 640x200 from the CGA picture', 'UFO power sources blown up', 'crash: [0-9]+ Elerium-115 left', 'T2: Turn 2') },
    @{ Name = 'terrbl'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG SEEALL SEED 2 MISSION JU7E.PAK RACE RME.PAK'; Script = 'TERRBL.TXT'; Wait = 200
       Expect = @('DEBUG Z breaks the object at 19,42,1', 'terrain explodes at 19,42,1 power 70', 'blast at 19,42,1 power 70',
                  'terrain explodes at 19,40,1 power 60', 'T2: Turn 2') },
    # The aliens' own shots set off explosive terrain in the alien turn (BOOMAIM: the overlay
    # manager drops the AI for bturn's bt_boom_run and brings it back).
    @{ Name = 'boomai'; Machine = 'xt88ega'; Opts = 'BATTLE BOOMAIM SEEALL SEED 2 MISSION JU7E.PAK RACE RME.PAK'; Script = 'BOOMAI.TXT'; Wait = 420
       Expect = @('DEBUG alien [0-9]+ aims at', 'terrain explodes at', 'alien turn [0-9]+ ms', 'T[0-9]+: Turn [0-9]+', 'alien turn [0-9]+ ms') },
    # Blaster bombs (DOS BA_LAUNCH, AIModule::wayPointAction): a soldier's by the keys and by
    # the mouse around the Skyranger's hull, the same target straight into the hull, a left-hand
    # launcher, the hovertank's, an alien's along its own waypoints.
    @{ Name = 'bombk'; Machine = 'xt88ega'; Mouse = $true; Opts = 'BATTLE BOMBGEAR SEEALL SHOTLOG SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'BOMBK.TXT'; Wait = 200
       Expect = @('waypoint 1 at 14,15,0', 'waypoint 9 at 19,22,0', 'RCLICK', 'waypoint 9 at 19,22,0', 'aim 232,249,16 acc 60 => -1',
                  'missile at waypoint 1 \(14,15,0\)', 'missile at waypoint 4 \(23,18,0\)', 'missile at waypoint 8 \(19,25,0\)', 'blast at', 'script finished')
       Absent = @('waypoint 10', 'Kowalski killed') },
    @{ Name = 'bombm'; Machine = 'xt88ega'; Mouse = $true; Opts = 'BATTLE BOMBGEAR SEEALL SHOTLOG SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'BOMBM.TXT'; Wait = 300
       Expect = @('waypoint 1 at 14,15,0', 'waypoint 2 at 18,14,0', 'view scrolled to 128,-192', 'waypoint 3 at 22,15,0', 'RCLICK', 'waypoint 3 at 22,15,0',
                  'view scrolled to 128,-256', 'waypoint 6 at 19,24,0', 'missile at waypoint 1 \(14,15,0\)', 'missile at waypoint 5 \(23,21,0\)', 'blast at',
                  'waypoint 1 at 14,15,1', 'SHOT T1 u1 15,19,1', 'script finished') },
    @{ Name = 'bombs'; Machine = 'xt88ega'; Mouse = $true; Opts = 'BATTLE BOMBGEAR SEEALL SHOTLOG SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'BOMBS.TXT'; Wait = 200
       Expect = @('SHOT T1 u0 14,19,1 .*=> 4 at', 'T1: Kowalski killed!', 'blast at 15,20,1', 'Squad lost')
       Absent = @('missile at waypoint') },
    @{ Name = 'mwaypt'; Machine = 'xt88ega'; Mouse = $true; Opts = 'BATTLE HWP 15 SEEALL SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'MWAYPT.TXT'; Wait = 200
       Expect = @('Hover/Launch walked to 14,15,2', 'waypoint 1 at 12,15,2', 'waypoint 2 at 14,12,0', 'missile at waypoint 1', 'Tank/Cannon killed!', 'result -2') },
    @{ Name = 'bombai'; Machine = 'xt88ega'; Opts = 'BATTLE MONTH 9 BOMBAI SHOTLOG SEED 13 RACE RSE.PAK MISSION JU7E.PAK'; Script = 'BOMBAI.TXT'; Wait = 500
       Expect = @('T3: alien 18 launches', 'SHOT T3 u18 13,25,0 .*acc 55 => -1', 'missile at waypoint 1 \(16,26,0\)',
                  'missile at waypoint 2 \(14,23,0\)', 'blast at 14,23,0', 'T3: alien 19 launches', 'missile at waypoint 2 \(4,35,1\)',
                  'missile at waypoint 6 \(31,39,0\)', 'SHOT T3 u19 .* => 4 at', 'blast at', 'T3: alien turn [0-9]+ ms', 'T4: Turn 4') },
    @{ Name = 'gren'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 16'; Script = 'GREN.TXT'; Wait = 150
       Expect = @('T1: Kowalski primes Grenade, timer 2', 'T1: Grenade primed \(2 turns\)',
                  'T1: Tanaka primes Grenade, timer 2', 'T2: Explosives detonate!') },
    @{ Name = 'terror'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 16 MISSION UR0E.PAK'; Script = 'TERROR.TXT'; Wait = 600
       Expect = @('pack UR0E.PAK', 'civilian turn', 'T8: Turn 8') },
    # The alien turn's pacing at the slowest Game Options (speed branch, PACE.TXT sets fire,
    # X-COM and alien speed 1; PACELOG logs every paced wait, "PACE <what> <least ms> <took
    # ms>"): DOS's least times (docs/design.md, "Pacing of the alien turn"): a step 8 frames
    # of 40 ms (16 on a diagonal, one more an eighth turned), a turn 40 ms an eighth, a
    # round 267 ms a tile, its hit 200 ms; none shorter (Absent).
    @{ Name = 'pace'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL PACELOG SEED 16 MISSION UR0E.PAK'; Script = 'PACE.TXT'; Wait = 600
       Expect = @('game options: scroll 5 auto, fire 1, xcom 1, alien 1', 'Hidden movement', '^PACE w (3[2-9]|[4-9][0-9])[0-9] ',
                  '^PACE t ', '^PACE f ', '^PACE h 200 ', 'T1: alien turn [0-9]+ ms')
       Absent = @('^PACE w ([0-9]{1,2}|[12][0-9]{2}|3[01][0-9]) ', '^PACE t ([0-9]|[1-3][0-9]) ',
                  '^PACE f ([0-9]{1,2}|1[0-9]{2}|2[0-5][0-9]|26[0-6]) ', '^PACE h (?!200 )') },
    # An unseen alien (night, seed 8: 8 at 15,2,1) shoots soldiers far off the view (the script
    # moves the cursor away each turn): the view leaves Hidden Movement and goes to the round, then to
    # the soldier, who falls and is seen for UnitDieBState's time (src/bview.c; before: nothing).
    @{ Name = 'shotview'; Machine = 'xt88ega'; Opts = 'BATTLE NIGHT SHOTLOG PACELOG SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'SHOTVIEW.TXT'; Wait = 300
       Expect = @('^SHOT T2 u8 15,2,1', '^T2: view to 16,9,1', '^SHOT T2 u8 15,2,1', '^T2: view to 15,19,1', '^T2: Tanaka killed!', '^PACE d 600 ', '^T2: alien 8 at 14,2,1 .* seen 0', 'T3: Turn 3') },
    # The screenshots (HOLDVIEW: a minute on the picture): the fallen soldier's view; the blast's
    # picture on the three videos, the map restored in hullbl's; with too little memory only the LOG line.
    @{ Name = 'shotdead'; Machine = 'xt88ega'; Opts = 'BATTLE NIGHT SHOTLOG HOLDVIEW 2 SEED 8 MISSION FA1E.PAK RACE RSE.PAK'; Script = 'SHOTVIEW.TXT'; Wait = 300
       Expect = @('^T2: view to 15,19,1', '^T2: Tanaka killed!', '^VIEW HOLD') },
    @{ Name = 'blastpic'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG SEEALL HOLDVIEW 4 SEED 1 MISSION FA2E.PAK RACE RSE.PAK'; Script = 'HULLBL.TXT'; Wait = 150
       Expect = @('^T1: blast picture', '^VIEW HOLD') },
    @{ Name = 'blastcga'; Machine = 'xt88cga'; Opts = 'BATTLE DEBUG SEEALL HOLDVIEW 4 SEED 1 MISSION FA2C.PAK RACE RSC.PAK'; Script = 'HULLBL.TXT'; Wait = 150
       Expect = @('^T1: blast picture', '^VIEW HOLD') },
    @{ Name = 'blastherc'; Machine = 'xt88herc'; Opts = 'BATTLE DEBUG SEEALL HOLDVIEW 4 SEED 1 MISSION FA2C.PAK RACE RSC.PAK'; Script = 'HULLBL.TXT'; Wait = 150
       Expect = @('^T1: blast picture', '^VIEW HOLD') },
    # Exact mode (DOS's voxel lines alone for sight and the AI's lines of fire): the CPU check
    # picks it, EXACTAI and NOEXACTAI overrule it (design.md "Alien AI").
    @{ Name = 'cpu88'; Machine = 'xt88ega'; Opts = 'BATTLE SEED 3 MISSION FA1E.PAK'; Script = 'ONE.TXT'; Wait = 180
       Expect = @('^CPU: below 286, exact AI and sight off', 'T1: alien turn') },
    @{ Name = 'cpuv20'; Machine = 'v20ega'; Opts = 'BATTLE SEED 3 MISSION FA1E.PAK'; Script = 'ONE.TXT'; Wait = 180
       Expect = @('^CPU: below 286, exact AI and sight off', 'T1: alien turn') },
    @{ Name = 'cpuv20186'; Machine = 'v20ega'; Cpu = '186'; Opts = 'BATTLE SEED 3 MISSION FA1E.PAK'; Script = 'ONE.TXT'; Wait = 180
       Expect = @('^CPU: below 286, exact AI and sight off') },
    @{ Name = 'cpu286'; Machine = 'at286'; Opts = 'BATTLE SEED 3 MISSION FA1E.PAK'; Script = 'ONE.TXT'; Wait = 240
       Expect = @('^CPU: 286 or better, exact AI and sight on', 'T1: alien turn') },
    @{ Name = 'cpu286off'; Machine = 'at286'; Opts = 'BATTLE NOEXACTAI SEED 3 MISSION FA1E.PAK'; Script = 'ONE.TXT'; Wait = 240
       Expect = @('^CPU: 286 or better, exact AI and sight off', 'T1: alien turn') },
    @{ Name = 'cpu88on'; Machine = 'xt88ega'; Opts = 'BATTLE EXACTAI SEED 3 MISSION FA1E.PAK'; Script = 'ONE.TXT'; Wait = 300
       Expect = @('^CPU: below 286, exact AI and sight on', 'T1: alien turn') },
    @{ Name = 'blastmem'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG SEEALL PACELOG SEED 1 MISSION FA2E.PAK RACE RSE.PAK'; Script = 'HULLBL.TXT'; Wait = 150; FreeKB = 478
       Expect = @('^blast picture: none \(3[0-9]+ bytes\)', '^blast at 24,12,0 power 200', '^PACE x 400 ', 'T2: Turn 2'); Absent = @('^T1: blast picture') },
    # A chryssalid's blow at a soldier: the view on him, the hit's 200 ms, then his fall (src/bview.c).
    @{ Name = 'shotmelee'; Machine = 'xt88ega'; Opts = 'BATTLE NIGHT SHOTLOG PACELOG ALIENS 1 SEED 3 MISSION UR0E.PAK RACE RNE.PAK'; Script = 'MELEE.TXT'; Wait = 420
       Expect = @('^PACE h 200 ', '^T8: view to ', '^T8: Kowalski killed!', '^PACE d 600 ') },
    @{ Name = 'psi'; Machine = 'xt88ega'; Opts = 'BATTLE SEEALL SEED 4 ALIENS 1 MISSION FA2E.PAK RACE REE.PAK'; Script = 'PSI.TXT'; Wait = 300
       Expect = @('pack FA2E.PAK', 'psi panic by Ethereal', 'T13: Turn 13') },
    # SB: Sound Blaster tests, left out (even with -Only) until the 286 version's work starts;
    # -WithSB runs them.
    @{ Name = 'sound'; Machine = 'xt88sb'; SB = $true; Opts = 'BATTLE SEEALL SEED 16'; Script = 'SOUND.TXT'; Wait = 200
       Pre = 'SET BLASTER=A220 I7 D1 T1'
       Expect = @('sound: Sound Blaster at 220', 'pack FO2E.PAK', 'result -2', 'sound: [0-9]+ effects started') },
    @{ Name = 'soundxms'; Machine = 'at286'; SB = $true; Opts = 'BATTLE SEEALL SEED 16'; Script = 'SOUND.TXT'; Wait = 230
       Pre = 'SET BLASTER=A220 I5 D1 T3'; Config = @('DEVICE=A:\HIMEM.SYS')
       Expect = @('all 23 effects in XMS', 'pack FO2E.PAK', 'result -2', 'sound: [1-9][0-9]* effects started') },
    # A 64 KB UMB (xt88umba: AdLib, Lo-tech EMS board, XTUMB.SYS): the map buffer goes there,
    # so the tight maps' fallbacks are not needed (docs/design.md, "Upper memory").
    @{ Name = 'umbhwp'; Machine = 'xt88umba'; Mouse = $true; Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2 HWP 14'; Script = 'ONE.TXT'; Wait = 240
       Config = @('DOS=UMB', 'DEVICE=A:\XTUMB.SYS E000 1000 260')
       Expect = @('UMB: 4095 paragraphs: map buffer', 'HWP Tank/Cannon at', 'HWP Hover/Plasma at', 'floor pictures: [0-9]+ of',
                  'music: track 2 in the UMB', 'script finished')
       Absent = @('left aboard', 'do not fit', 'left out') },
    @{ Name = 'umbterror'; Machine = 'xt88umba'; Mouse = $true; Opts = 'BATTLE MISSION UR0E.PAK RACE RNE.PAK SEED 3 HWP 14'; Script = 'ONE.TXT'; Wait = 300
       Config = @('DOS=UMB', 'DEVICE=A:\XTUMB.SYS E000 1000 260')
       Expect = @('UMB: 4095 paragraphs: map buffer', 'HWP Hover/Plasma at', 'floor pictures: [0-9]+ of', 'music: track 2 in the UMB',
                  'pack UR0E.PAK', 'script finished')
       Absent = @('another layout', 'left aboard', 'do not fit', 'left out') },
    # The reference XT's two boot profiles: a 53 KB UMB (the map buffer, the track, 9 KB of
    # overlay area) and a 123 KB one (xt88umb2: two EMS boards' frames at D000 and E000,
    # 79 KB of overlay area): no fallback either.
    @{ Name = 'umb53'; Machine = 'xt88umba'; Mouse = $true; Opts = 'BATTLE MISSION UR0E.PAK RACE RNE.PAK SEED 3 HWP 14'; Script = 'ONE.TXT'; Wait = 300
       Config = @('DOS=UMB', 'DEVICE=A:\XTUMB.SYS E000 0D40 260')
       Expect = @('UMB: 3391 paragraphs: map buffer', 'HWP Hover/Plasma at', 'floor pictures: [0-9]+ of', 'music: track 2 in the UMB',
                  'pack UR0E.PAK', 'script finished')
       Absent = @('another layout', 'left aboard', 'do not fit', 'left out') },
    @{ Name = 'umb123'; Machine = 'xt88umb2'; Mouse = $true; Opts = 'BATTLE MISSION UR0E.PAK RACE RNE.PAK SEED 3 HWP 14'; Script = 'ONE.TXT'; Wait = 300
       Config = @('DOS=UMB', 'DEVICE=A:\XTUMB.SYS D000 1EC0 268 260')
       Expect = @('UMB: 7871 paragraphs: map buffer', 'HWP Hover/Plasma at', 'floor pictures: [0-9]+ of', 'music: track 2 in the UMB',
                  'pack UR0E.PAK', 'script finished')
       Absent = @('another layout', 'left aboard', 'do not fit', 'left out') },
    # The same on the reference XT's own layout (v20e: profile E, LTEMM, 53 KB UMB; v20x: profile X,
    # EXMS86, 123 KB UMB; their emu\<machine>\CONFIG.SYS, MS-DOS 6.22 from emu\dos6).
    @{ Name = 'v20eter'; Machine = 'v20e'; Mouse = $true; Opts = 'BATTLE MISSION UR0E.PAK RACE RNE.PAK SEED 3 HWP 14'; Script = 'ONE.TXT'; Wait = 300
       Expect = @('UMB: [0-9]+ paragraphs: map buffer', 'HWP Hover/Plasma at', 'floor pictures: [0-9]+ of', 'music: track 2 in the UMB',
                  'pack UR0E.PAK', 'script finished')
       Absent = @('another layout', 'left aboard', 'do not fit', 'left out') },
    @{ Name = 'v20xter'; Machine = 'v20x'; Mouse = $true; Opts = 'BATTLE MISSION UR0E.PAK RACE RNE.PAK SEED 3 HWP 14'; Script = 'ONE.TXT'; Wait = 300
       Expect = @('UMB: [0-9]+ paragraphs: map buffer', 'HWP Hover/Plasma at', 'floor pictures: [0-9]+ of', 'music: track 2 in the UMB',
                  'pack UR0E.PAK', 'script finished')
       Absent = @('another layout', 'left aboard', 'do not fit', 'left out') },
    # No upper memory on a 286 (docs/design.md "The HMA"): the XMS read cache's code, table and
    # buffer in the HMA. u286 (the reference 286: MS-DOS 5, HIMEM, DOS=HIGH, mouse, 557 KB): the part
    # of the HMA DOS left free; at286 (HIMEM, DOS low): the whole HMA from HIMEM, and XMEMA20 turns
    # A20 off behind the cache's back (the stub turns it on for each call it passes on).
    @{ Name = 'hmau286'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2'; Script = 'UMBPLAY.TXT'; Wait = 420
       Expect = @('^XMEM: XMS, [0-9]+ KB: cache .* in the HMA at FFFF:DA00 \(DOS=HIGH left: ', '^canvas: XMS, [0-9]+ KB lent by the read cache', 'script finished')
       Absent = @('no read cache', 'UMB 65535') },
    @{ Name = 'hmaat286'; Machine = 'at286'; Mouse = $true; Opts = 'XMEMA20 BATTLE SEED 3 MISSION FA1E.PAK'; Script = 'ONE.TXT'; Wait = 300
       Config = @('DEVICE=A:\HIMEM.SYS')
       Expect = @('^XMEM: XMS, [0-9]+ KB: cache .* in the HMA at FFFF:DA00 \(the XMS driver''s: 65520 bytes', '^XMEM: XMEMA20 \(A20 off: 1\)',
                  'T1: alien turn')
       Absent = @('no read cache') },
    # Tight battles with EMS or XMS (the picture store, docs/design.md "Tight battles"): the
    # Avenger in the jungle with flying suits and four vehicle types gets every unit's own
    # picture and no disk cache on v20e (EMS), u286 (XMS, no UMB, 557 KB like the reference 286);
    # the city on u286 puts a vehicle's own set into the store; a battle save reloads it.
    @{ Name = 'xpice'; Machine = 'v20e'; Mouse = $true; Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2 ARMOUR 3 SQUAD 10 HWP 1234 CRAFT A GEAR'
       Script = 'LOWSAVE.TXT'; Wait = 400
       Expect = @('^picture store: EMS', '^picture store: walls and objects', 'HWP Hover/Plasma at', '^load slot 4: ok',
                  '^picture store: EMS', 'script finished')
       Absent = @('the picture of', 'disk cache', 'Low memory', 'left aboard', 'failed') },
    @{ Name = 'xpicu'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2 ARMOUR 3 SQUAD 10 HWP 1234 CRAFT A GEAR'
       Script = 'LOWSAVE.TXT'; Wait = 400
       Expect = @('^picture store: XMS', '^picture store: walls and objects', 'HWP Hover/Plasma at', '^load slot 4: ok',
                  '^picture store: XMS', 'script finished')
       Absent = @('the picture of', 'disk cache', 'Low memory', 'left aboard', 'failed') },
    @{ Name = 'xpicuc'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Opts = 'BATTLE MISSION UR0E.PAK RACE RNE.PAK SEED 2 ARMOUR 3 SQUAD 10 HWP 1234 CRAFT A GEAR'
       Script = 'LOWTOUR.TXT'; Wait = 600
       Expect = @('^picture store: XMS', 'its pictures in the store', '^picture store: walls and objects', 'HWP Hover/Plasma at', 'script finished')
       Absent = @('the picture of', 'disk cache', 'Low memory', 'left aboard', 'failed') },
    # The 286's memory (mem286 branch, docs/design.md "The 286: XMS budget, HMA track, canvas"):
    # the store borrows the read cache's XMS, the battle track plays from the HMA, the floor
    # pictures and the hovering legs come in the worst jungle battle (EGA and VGA), the city takes
    # its first layout, and a battle with room runs the canvas on XMS the cache lends.
    @{ Name = 'm286e'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2 ARMOUR 3 SQUAD 10 HWP 1234 CRAFT A GEAR'
       Script = 'ONE.TXT'; Wait = 300
       Expect = @('^XMEM: the battle track \([0-9]+ bytes\) in the HMA', '^picture store: XMS, [0-9]+ KB lent by the read cache',
                  '^picture store: the hovering legs', '^floor pictures: [0-9]+ of', '^picture store: buffer', '^canvas: XMS, [0-9]+ KB lent', 'T1: alien turn')
       Absent = @('do not fit', 'left out', 'the picture of', 'disk cache', 'Low memory', 'left aboard', 'failed') },
    @{ Name = 'm286v'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Opts = 'BATTLE MISSION JU7V.PAK RACE RMV.PAK SEED 2 ARMOUR 3 SQUAD 10 HWP 1234 CRAFT A GEAR VGA'
       Script = 'ONE.TXT'; Wait = 300
       Expect = @('^XMEM: the battle track \([0-9]+ bytes\) in the HMA', '^picture store: XMS, [0-9]+ KB lent by the read cache',
                  '^picture store: the hovering legs', '^floor pictures: [0-9]+ of', '^picture store: buffer', 'T1: alien turn')
       Absent = @('do not fit', 'left out', 'the picture of', 'disk cache', 'Low memory', 'left aboard', 'failed') },
    @{ Name = 'm286c'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Opts = 'BATTLE MISSION UR0E.PAK RACE RNE.PAK SEED 2 ARMOUR 3 SQUAD 10 HWP 1234 CRAFT A GEAR'
       Script = 'ONE.TXT'; Wait = 500
       Expect = @('^picture store: XMS, [0-9]+ KB lent by the read cache', '^picture store: the hovering legs', '^floor pictures: [0-9]+ of',
                  '^canvas: XMS, [0-9]+ KB lent', 'T1: alien turn')
       Absent = @('another layout', 'left out', 'the picture of', 'disk cache', 'Low memory', 'left aboard', 'failed') },
    @{ Name = 'm286cv'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2'; Script = 'ONE.TXT'; Wait = 300
       Expect = @('^canvas: overlay area at', '^picture store: XMS, [0-9]+ KB lent by the read cache', '^floor pictures: [0-9]+ of',
                  '^canvas: XMS, [0-9]+ KB lent by the read cache', 'T1: alien turn')
       Absent = @('do not fit', 'left out', 'ERRORS', 'failed') },
    # XMS on an XT (v20x, EXMS86) takes the picture store in the worst jungle battle: floor
    # pictures, the hovering legs, the walk frames (in the store) and the canvas come back.
    @{ Name = 'v20xw'; Machine = 'v20x'; Mouse = $true; Opts = 'BATTLE MISSION JU7E.PAK RACE RME.PAK SEED 2 ARMOUR 3 SQUAD 10 HWP 1234 CRAFT A GEAR'
       Script = 'ONE.TXT'; Wait = 300
       Expect = @('^picture store: XMS, [0-9]+ KB', '^floor pictures: [0-9]+ of', '^flying suit: the hovering legs, ',
                  '^walk frames: [0-9]+ sets, [0-9]+ bytes in the picture store', '^canvas: XMS', 'T1: alien turn')
       Absent = @('do not fit', 'left out', 'the picture of', 'disk cache', 'Low memory', 'left aboard', 'failed') },
    # The canvas in VGA (8 bits a pixel) on a walk, every copy checked against the drawing (XCV=CHECK).
    @{ Name = 'm286vc'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Opts = 'BATTLE SEED 8 MISSION FA1V.PAK RACE RSV.PAK VGA'; Script = 'WALK4.TXT'; Wait = 300
       Pre = 'SET XCV=CHECK'
       Expect = @('^canvas: overlay area at', '^canvas: XMS, [0-9]+ KB lent by the read cache', 'walked to', 'walked to', 'script finished')
       Absent = @('do not fit', 'left out', 'ERRORS', 'CHECK cell', 'failed') },
    # Mode folders (docs/design.md "Mode folders"): the release's layout with ONE mode folder
    # (emutest -Folders): the shared packs and PACKS.VER in C:\, the mode's packs in CGA\, EGA\
    # or VGA\. The mode is picked without an option; a forced mode whose folder or card is
    # missing stops with its message.
    @{ Name = 'fcga'; Machine = 'xt88cga'; Folders = 'CGA'; Opts = 'BATTLE PSI SEEALL SEED 5 MISSION FA1C.PAK RACE RSC.PAK'
       Script = 'PSIWIN.TXT'; Wait = 130
       Expect = @('^mode: CGA \(auto; no VGA card; 0 KB XMS\), packs in CGA\\$', 'pack FA1C.PAK', '^result 1 ') },
    @{ Name = 'fcgageo'; Machine = 'xt88cga'; Folders = 'CGA'; Opts = 'DEBUG SEEALL SEED 2'; Script = 'SMCRASH.TXT'; Wait = 300
       Expect = @('^mode: CGA \(auto; .*\), packs in CGA\\$', 'AIR UFO CRASH LANDS!', '^BRIEF ', 'pack MO1C.PAK', '^result 1 ',
                  'Success: 1 aliens killed') },
    @{ Name = 'fherc'; Machine = 'xt88herc'; Folders = 'CGA'; Opts = 'BATTLE PSI SEEALL SEED 5 MISSION FA1C.PAK RACE RSC.PAK'
       Script = 'PSIWIN.TXT'; Wait = 160
       Expect = @('^mode: HERC \(mono card\) \(auto; .*\), packs in CGA\\$', '^video: Hercules, 640x200 from the CGA picture',
                  'pack FA1C.PAK', '^result 1 ') },
    @{ Name = 'fega'; Machine = 'xt88ega'; Folders = 'EGA'; Opts = 'DEBUG SEEALL SEED 2'; Script = 'SMCRASH.TXT'; Wait = 260
       Expect = @('^mode: EGA \(auto; no VGA card; 0 KB XMS\), packs in EGA\\$', 'UFO-1 detected', 'AIR UFO CRASH LANDS!',
                  '^BRIEF ', 'pack MO1E.PAK', '^result 1 ', '^LOOT alloys [1-9]', 'Success: 1 aliens killed') },
    @{ Name = 'fegasave'; Machine = 'xt88ega'; Folders = 'EGA'; Opts = 'DEBUG SEED 7'; Script = 'SMSAVE.TXT'; Wait = 200
       Expect = @('^mode: EGA \(auto', 'BASE research', '^save slot 3 .*: ok', '^save state: ', '^load slot 3: ok',
                  '^load state: ', 'Campaign loaded'); Same = @('save state: ', 'load state: ') },
    @{ Name = 'fegaq'; Machine = 'xt88ega'; Folders = 'EGA'; Opts = 'BATTLE SEED 3'; Script = 'ONE.TXT'; Wait = 300
       Expect = @('^mode: EGA \(auto', '^seed 3 pack [A-Z]+[0-9]E\.PAK .* race R[A-Z]E\.PAK', 'T1: alien turn') },
    @{ Name = 'fvga'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Folders = 'VGA'; Opts = 'DEBUG SEED 2'; Script = 'VGSGEO.TXT'; Wait = 300
       Expect = @('^mode: VGA \(auto; VGA card, 286 or better, VGA folder; [0-9]+ KB XMS\), packs in VGA\\$', '^video: VGA prototype',
                  'script finished') },
    # The same with the 286 build under its release name (b286 branch: XCOM286.EXE).
    @{ Name = 'fvga286'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Folders = 'VGA'; Cpu = '286'; ExeName = 'XCOM286'; Opts = 'DEBUG SEED 2'; Script = 'VGSGEO.TXT'; Wait = 300
       Expect = @('^mode: VGA \(auto; VGA card, 286 or better, VGA folder; [0-9]+ KB XMS\), packs in VGA\\$', '^video: VGA prototype',
                  'script finished') },
    # The quantity lists' arrow buttons in VGA (uk_restore reads the picture again there:
    # qlist.c draws a row's buttons after it): BASEUI's clicks on them buy, transfer and sell.
    @{ Name = 'vgaqty'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Folders = 'VGA'; Cpu = '286'; ExeName = 'XCOM286'; Opts = 'DEBUG SEED 7'; Script = 'BASEUI.TXT'; Wait = 240
       Expect = @('^video: VGA prototype', 'BASE buy 1 Soldier for \$40000, due in 72h', 'BASE buy 9 Scientist for \$540000, due in 72h',
                  'BASE buy 1 Rifle for \$3000, due in 24h', 'CRAFT SKYRANGER-1 Pistol 4 aboard', 'Pistol 0 aboard', 'SELL craft SKYRANGER-1') },
    @{ Name = 'fvgabat'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Folders = 'VGA'; Opts = 'DEBUG SEEALL SEED 2'; Script = 'SMCRASH.TXT'; Wait = 400
       Expect = @('^mode: VGA \(auto', '^video: VGA prototype', '^BRIEF ', 'pack [A-Z]+[0-9]V\.PAK', 'craft at') },
    @{ Name = 'fvgaega'; Machine = 'u286'; Mouse = $true; FreeKB = 557; Folders = 'EGA'; Opts = 'DEBUG SEED 2'; Script = 'VGSGEO.TXT'; Wait = 300
       Expect = @('^mode: EGA \(auto; VGA card, no VGA folder; [0-9]+ KB XMS\), packs in EGA\\$', 'script finished')
       Absent = @('^video: VGA') },
    @{ Name = 'fmiss'; Machine = 'xt88ega'; Folders = 'CGA'; Opts = 'EGA'; Script = 'ONE.TXT'; Wait = 90
       Expect = @('^EGA: the EGA folder is missing \(EGA\\UIE\.PAK\)\.$'); Absent = @('^mode:', '^packs format') },
    @{ Name = 'fmissv'; Machine = 'u286'; Folders = 'EGA'; Opts = 'VGA'; Script = 'ONE.TXT'; Wait = 90
       Expect = @('^VGA: the VGA folder is missing \(VGA\\UIV\.PAK\)\.$'); Absent = @('^mode:', '^packs format') },
    @{ Name = 'fnoega'; Machine = 'xt88cga'; Folders = 'CGA'; Opts = 'EGA'; Script = 'ONE.TXT'; Wait = 90
       Expect = @('^EGA: no EGA or VGA card found\.$'); Absent = @('^mode:', '^packs format') },
    @{ Name = 'intro'; Machine = 'xt88adl'; Opts = 'INTROTIME'; Script = 'INTRO.TXT'; Wait = 140; Timing = $true
       Show = '^INTRO .*frames shown'
       Expect = @('INTRO 793 of 793 frames shown, [0-9]+ late', 'INTRO played to the end', 'quit to DOS') },
    @{ Name = 'introherc'; Machine = 'xt88herc'; Opts = 'INTROTIME'; Script = 'INTRO.TXT'; Wait = 160; Timing = $true
       Show = '^INTRO .*frames shown'
       Expect = @('^video: Hercules, 640x200 from the CGA picture', 'INTRO ([0-9]+) of \1 frames shown, [0-9]+ late', 'INTRO played to the end', 'quit to DOS') }
)
if (-not $WithSB) { $tests = @($tests | Where-Object { -not $_.SB }) }

$fail = Invoke-TestLanes -Tests $tests -OutDir $out -Only $Only -Lanes $Lanes -MaxMin $MaxMin -SevenZip $SevenZip -Python $Python -NoEarlyStop:$Full -Board $Board -Cpu $Cpu
exit ([int]@($fail)[-1])
