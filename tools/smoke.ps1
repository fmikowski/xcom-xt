# SPDX-License-Identifier: GPL-3.0-or-later
# Smoke tests: runs a few short KEYS scripts in 86Box (tools\emutest.ps1,
# -Hdd), reads each LOG.TXT back from emu\hdd.img and checks it for the
# expected lines (in order). Prints PASS / FAIL per script; the logs and
# screenshots go to build\smoke\. Exit code = number of failures.
#   .\tools\smoke.ps1                  all scripts, 4 lanes in parallel, each run stopped
#                                      a few seconds after its last expected line (about 10 minutes)
#   .\tools\smoke.ps1 -Only crash,psi  some of them
#   .\tools\smoke.ps1 -Lanes 1         one after the other (the old behaviour, about 25 minutes)
#   .\tools\smoke.ps1 -Full            no early stop: every run waits its whole Wait time
# Call it inside PowerShell (& .\tools\smoke.ps1 -Only a,b), not with powershell -File.
# The lanes (tools\lanes.ps1) are copies of emu\ under build\lanes\<n>\.
# Needs what emutest.ps1 needs (86Box, emu\dos5, packs) and 7-Zip.
param(
    [string[]]$Only = @(),
    [int]$Lanes = 4,
    [int]$MaxMin = 30,     # give up after this many minutes (0 = never)
    [switch]$Full,
    [ValidateSet('', 'EMS', 'XMS')] [string]$Board = '',   # the rows on the EMS board machine (tools\lanes.ps1)
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\lanes.ps1"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$out = "$root\build\smoke"
if ($Board) { $out += '-' + $Board.ToLower() }

# name, machine, options, script, seconds, expected lines (regular
# expressions, matched in this order).
$tests = @(
    @{ Name = 'base'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'SMBASE.TXT'; Wait = 150
       Expect = @('BASE Building Living Quarters', 'BASE research Medi-Kit',
                  'Research complete: Medi-Kit', 'GEO pop-ups done: clock running', 'BASE manufacture Medi-Kit x1',
                  'Production complete: Medi-Kit', '^SELL 1 ', '^CRAFT INTERCEPTOR-1 weapon 1',
                  '^BASE equip ', 'script finished') },
    @{ Name = 'crash'; Machine = 'xt88ega'; Opts = 'DEBUG SEEALL SEED 2'; Script = 'SMCRASH.TXT'; Wait = 200
       Expect = @('UFO-1 detected', 'AIR Intercepting UFO-1', 'AIR UFO CRASH LANDS!',
                  'UFO-1 shot down! Crash site marked', 'at the site',
                  '^BRIEF ', 'pack MO1E.PAK', '^result 1 ', '^LOOT alloys [1-9]', 'Success: 1 aliens killed') },
    @{ Name = 'win'; Machine = 'xt88ega'; Opts = 'BATTLE DEBUG SEEALL SEED 16'; Script = 'WIN.TXT'; Wait = 300
       Expect = @('pack FO2E.PAK', 'Grenade primed', 'Explosives detonate', '^result 1 ')
       Absent = @('floor pictures changed') },
    @{ Name = 'psi'; Machine = 'xt88ega'; Opts = 'BATTLE PSI SEEALL SEED 5 MISSION FA1E.PAK RACE RSE.PAK'
       Script = 'PSIWIN.TXT'; Wait = 100
       Expect = @('psi control by .* succeeded', 'under X-COM control', '^result 1 ') },
    @{ Name = 'psigeo'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'SMPSI.TXT'; Wait = 120
       Expect = @('PSI debug: research and kit granted', 'BASE Building Psionic Laboratory',
                  'Construction finished', '^PSI .* trained: skill', 'script finished') },
    @{ Name = 'save'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 7'; Script = 'SMSAVE.TXT'; Wait = 150
       Expect = @('BASE research', '^save slot 3 .*: ok', '^save state: ', '^load slot 3: ok',
                  '^load state: ', 'Campaign loaded'); Same = @('save state: ', 'load state: ') },
    @{ Name = 'newbase'; Machine = 'xt88ega'; Opts = 'SEED 3'; Script = 'NEWBASE.TXT'; Wait = 120
       Expect = @('BASE North America built at 70,44', 'BASE access lift at C3') },
    @{ Name = 'transfer'; Machine = 'xt88ega'; Opts = 'DEBUG SEED 3'; Script = 'TRANSFER.TXT'; Wait = 180
       Expect = @('^XFER items: 40 41 42 43 3E 3F 00 09 01 0A 02 0B 38 39 3A 3B 0F 60$',
                  'XFER soldier .* to North America, 14h', 'XFER 8 Rifle Clip',
                  'Transfer arrived at North America') },
    @{ Name = 'geoclick'; Machine = 'xt88ega'; Opts = 'SEED 2'; Script = 'GEOCLICK.TXT'; Wait = 200
       Expect = @('WIN TARGET: WAY POINT: OK', 'INTERCEPTOR-2 launched\.',
                  'WIN INTERCEPTOR-2: DESTINATION: WAY POINT', 'INTERCEPTOR-2 returning to base',
                  'WIN TARGET: UFO-1: OK', 'INTERCEPTOR-1 launched to UFO-1', 'AIR UFO CRASH LANDS!',
                  'WIN TARGET: CRASH SITE-1: OK', 'SKYRANGER-1 launched', 'SKYRANGER-1 returning to base',
                  'INTERCEPTOR-2 is patrolling', 'WIN INTERCEPTOR-2: PATROLLING',
                  'INTERCEPTOR-2 back at base') },
    @{ Name = 'cga'; Machine = 'xt88cga'; Opts = 'BATTLE PSI SEEALL SEED 5 MISSION FA1C.PAK RACE RSC.PAK'
       Script = 'PSIWIN.TXT'; Wait = 100
       Expect = @('pack FA1C.PAK', '^result 1 ') },
    @{ Name = 'herc'; Machine = 'xt88herc'; Opts = 'BATTLE PSI SEEALL SEED 5 MISSION FA1C.PAK RACE RSC.PAK'
       Script = 'PSIWIN.TXT'; Wait = 130
       Expect = @('^video: Hercules, 640x200 from the CGA picture', 'pack FA1C.PAK', '^result 1 ') },
    @{ Name = 'bsave'; Machine = 'xt88ega'; Opts = 'DEBUG SEEALL SEED 3'; Script = 'BSAVE.TXT'; Wait = 540
       Expect = @('pack FA3E.PAK', 'T2: options window', '^save slot 5 .*: ok', '^bsave state: ',
                  '^result 1 ', 'Success: ',
                  '^load slot 5: ok, .*in battle', '^bload state: ', '^result 1 ', 'Success: ',
                  '^load slot 5: ok, .*in battle', '^bload state: ', 'T3: load slot 5$',
                  '^load slot 5: ok, .*in battle', '^bload state: ', '^result 1 ', 'Success: ')
       Same = @('bsave state: ', 'bload state: ')
       # The same keys after the save (the battle going on), after loading
       # it on the geoscape and after loading it in the battle: the same LOG.
       Cmp = @(@('^bsave state', 1, '^load slot 5: ok', 1, '^bload state', 1, '^load slot 5: ok', 1),
               @('^bload state', 1, '^load slot 5: ok', 1, '^bload state', 3, 'script finished', 1)) }
)

$fail = Invoke-TestLanes -Tests $tests -OutDir $out -Only $Only -Lanes $Lanes -MaxMin $MaxMin -SevenZip $SevenZip -Python $Python -NoEarlyStop:$Full -Board $Board
exit ([int]@($fail)[-1])
