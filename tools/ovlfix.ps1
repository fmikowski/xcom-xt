# SPDX-License-Identifier: GPL-3.0-or-later
# Patches a linked program against a bug in Watcom's dynamic overlay manager.
#   .\tools\ovlfix.ps1 build\86\xcomxt.exe build\86\xcomxt.map
# (build.ps1 runs it after every overlay link; build.ps1 -NoOvlFix skips it.)
#
# Watcom's dynamic overlay manager (wovl.lib, novlldr.c, redoRelocs) moves
# sections to compact the area. For a section with relocations to itself
# (far pointers to its own code or constants: the -zc screens' tables) it
# then re-applies the section's relocations, meant to run with the prolog's
# delta (the program's load segment) at 0 so that only the self-references
# change. The compiled library (OW 2.0) restores delta before the call:
#     mov cs:[__OVLTAB__+6],ax      ; delta = 0
#     mov ax,si
#     mov cs:[__OVLTAB__+6],cx      ; delta = saved delta (too early)
#     call __OvlRelocLoad__
# so every far call from the moved section to resident code or overlay
# vectors gets the load segment added twice. The linked program is patched
# to make the call first (the two instructions swap places; the code is the
# library's, only their order changes; __OvlRelocLoad__ keeps CX):
#     mov cs:[__OVLTAB__+6],ax
#     mov ax,si
#     call __OvlRelocLoad__
#     mov cs:[__OVLTAB__+6],cx
# The build fails unless the sequence is found exactly once, so another
# library version cannot slip through unpatched.
param(
    [Parameter(Mandatory)] [string]$exePath,
    [Parameter(Mandatory)] [string]$mapPath
)
$ErrorActionPreference = 'Stop'
$tab = Select-String -Path $mapPath -Pattern '^[0-9a-f]{4}:([0-9a-f]{4})\s+__OVLTAB__$' |
    Select-Object -First 1
if (-not $tab) { throw 'overlay fix: __OVLTAB__ not in the map' }
$delta = [Convert]::ToInt32($tab.Matches[0].Groups[1].Value, 16) + 6
$lo = $delta -band 0xFF
$hi = $delta -shr 8
$b = [IO.File]::ReadAllBytes($exePath)
$hits = @()
for ($i = 0; $i -le $b.Length - 17; $i++) {
    if ($b[$i] -eq 0x2E -and $b[$i + 1] -eq 0xA3 -and $b[$i + 2] -eq $lo -and $b[$i + 3] -eq $hi -and
        $b[$i + 4] -eq 0x89 -and $b[$i + 5] -eq 0xF0 -and
        $b[$i + 6] -eq 0x2E -and $b[$i + 7] -eq 0x89 -and $b[$i + 8] -eq 0x0E -and
        $b[$i + 9] -eq $lo -and $b[$i + 10] -eq $hi -and $b[$i + 11] -eq 0xE8 -and
        $b[$i + 14] -eq 0x89 -and $b[$i + 15] -eq 0x5C -and $b[$i + 16] -eq 0x04) { $hits += $i }
}
if ($hits.Count -ne 1) { throw "overlay fix: redoRelocs sequence found $($hits.Count) times (expected 1)" }
$i = $hits[0]
$rel = ($b[$i + 12] + 256 * $b[$i + 13] + 5) -band 0xFFFF   # the call starts 5 bytes earlier
$b[$i + 6] = 0xE8; $b[$i + 7] = $rel -band 0xFF; $b[$i + 8] = $rel -shr 8
$b[$i + 9] = 0x2E; $b[$i + 10] = 0x89; $b[$i + 11] = 0x0E; $b[$i + 12] = $lo; $b[$i + 13] = $hi
[IO.File]::WriteAllBytes($exePath, $b)
Write-Host ('Overlay manager: redoRelocs fixed at file offset {0:x}' -f $i)
