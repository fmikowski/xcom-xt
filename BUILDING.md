# xcom-xt

A from-scratch engine for **X-COM: UFO Defense** (1994) aimed at XT-class PCs:
a 10 MHz turbo 8088 or NEC V20, with the isometric battlescape kept like the
original.

- Graphics: **EGA** 320x200x16 (256 KB card), **CGA** 320x200x4 as fallback, **Hercules** (64 KB HGC: the CGA picture as native 640x200 greys).
- Memory: 640 KB (EMS or XMS is optional: a read cache, see "Sound and
  memory"); a hard disk for the full set of missions. The
  game is made for DOS with a mouse driver and nothing else: about 568 KB of
  free conventional memory (what DOS's `MEM` shows as the largest executable
  program size). There every unit comes to every battle; when memory is short
  vehicles and armour may share a picture (the battle's title line says so), and
  with less still the walls and objects are read from disk as drawn: slower, but
  the battle happens (tested down to 490 KB; not on Hercules). `XCOMXT LOG LOG.TXT` logs `DOS free
  at start`. Upper memory blocks (UMBs) are used when DOS 5 (`DOS=UMB`)
  or an XMS UMB driver offers them: with 44 KB or more the biggest battles
  need none of their memory fallbacks, and every KB beyond that means
  fewer code reloads from disk (see "Sound and memory"; `NOUMB` leaves the
  UMB to sound only, for comparing).
- Exact mode: on a 286 or better the game judges sight of units and the
  aliens' lines of fire by the original's voxel lines alone; below a 286
  (8088, 8086, V20) a tile line comes first, because the full set costs an XT
  1.5 to 2.5 minutes an alien turn. `EXACTAI` turns it on on any CPU, `NOEXACTAI`
  off; `XCOMXT LOG LOG.TXT` logs which CPU class was found and which is on.
- Sound: the original AdLib music (OPL2 at 388h: an AdLib, or the FM part of
  any Sound Blaster) with FM sound effects on the OPL2 (`NOFMSFX`: PC
  speaker effects instead), PC speaker effects without one; the original
  digitised effects on a Sound Blaster (8-bit DMA) only where they need no
  conventional memory (S toggles sound; options `NOSB`, `NOMUSIC`,
  `NOSOUND`). See "Sound and memory".

The project ships no game assets. You need your own copy of the original game;
a converter running on a modern PC turns its data files into packs for EGA and
CGA. File formats and rules follow [OpenXcom](https://openxcom.org).

**Status:** a campaign. The geoscape shows the world with time passing.
Alien missions as in the DOS game (research, harvest, abduction,
infiltration, alien base, terror, retaliation, supply) send waves of UFOs by
type and race into the 12 regions, with the mix changing by month; UFOs fly
the original trajectories, land and take off; your base radar detects them;
two Interceptors chase
them and fight them in air combat (stances, Stingray, Avalanche, cannon, laser
plasma and fusion craft weapons), and the Skyranger goes to landed and crashed UFOs,
terror sites and alien bases (found by radar or by flying near them). Each
battle uses the terrain at the site (6 terrains), the UFO type (8) and the
mission's alien race (sectoid, floater, snakeman, muton, ethereal): fog of
war, time units, shooting, reaction fire, an alien turn. 16 countries fund
X-COM and change their funding monthly by the activity over them;
infiltrated countries sign pacts with the aliens. A month-end report shows
the rating and every country's funding; two bad months in a row disband
X-COM. Cydonia is fought in two stages (Mars surface, then the base with the
alien brain). Soldiers keep their stats, kills and experience; the dead are
UFO components and corpses are recovered and can be sold; their research
leads to the Firestorm, Lightning and Avenger, built in the base's hangars
and fuelled with Elerium (the Lightning and Avenger carry more soldiers).
replaced by recruits. The base is built on the DOS game's 6x6 grid
(facilities limit staff, stores and craft; radars set the detection range);
a battleship attacks it when a retaliation mission finds it: defences fire,
then the squad and the base's HWPs fight in the base itself (the
facilities whose machinery the battle wrecks are destroyed, as in DOS).
The debriefing scores what the battle map's tiles hold (UFO components,
alloys, Elerium) and the alien artifacts, and lists the HWP ammunition
that could not be re-armed. Up to 8 bases as in DOS, each
with its own facilities, stores, staff, soldiers, craft, research and
production; transfers between them (DOS times and costs); wounded
soldiers spend days in recovery; the Skyranger carries 14, the Lightning
12, the Avenger 26, each landing with its own craft map and seats as in
DOS (bases hold as many soldiers as their living quarters, up to 100 a base and 250 in all), and a transport whose
squad is lost is lost too; Hyper-wave Decoder, Mind Shield and Fusion
Ball Defence with their research. Research, manufacturing, buying,
equipment, interceptors, a battle inventory, stun weapons and live captures
work. The five DOS heavy weapons platforms (Tank/Cannon, Tank/Rocket
Launcher, Tank/Laser Cannon, Hovertank/Plasma, Hovertank/Launcher) are
bought or manufactured, loaded on a transport (four seats each; Skyranger
3, Avenger 4) with their ammunition from the stores, and fight as 2x2
units with a turret; hovertanks fly; they defend their base and go on
into Cydonia's second stage (see docs/design.md, Heavy weapons
platforms).
A start menu as in DOS: new game with five difficulty levels (Beginner to
Superhuman: alien numbers and stats as in the original), ten named save
slots in DOS's save and load lists (games saved in the middle of a
battle, quick battles too, load back into it, as in DOS), and a UFOpaedia unlocked by research (the game's own texts, in
UFOPAED.PAK).
The screens look like the DOS game's, in 16 or 4 colours: the original
background pictures, fonts, windows and bevelled buttons for the menus, the
base (seen from above, as the basescape), research, manufacture, purchase /
recruit, sell, transfers, equip, stores, hangar, psi training, the UFOpaedia pages with their
pictures, the reports and the debriefing; the geoscape's panel (buttons, clock, time speeds); the
battlescape control panel with the hands, rank, bars and reserve
buttons (in day colours at night too), the minimap, the abort and options
windows; the inventory with the paper doll; the mission briefing; the air
combat window over the globe; UFOpaedia UFO and facility pages (UIE.PAK /
UIC.PAK, see docs/design.md, Interface); the geoscape's windows (a click on
a base, craft, UFO or site) as in DOS. UI packs converted before the
geoscape windows lack BACK12 / BACK15 (the windows then use BACK01), and
those before the terror pop-up's BACK03: rerun `tools\ui.py`. UI packs
converted before the item picture fix (uipics) use the old entry numbers
(the item pictures from the Auto-Cannon on and every entry after the item
pictures in the wrong places): rerun `tools\ui.py` (EGA and `--cga`).
Terror sites: every month a terror mission ends in an attack on a city; the
Skyranger can fly there for a battle in the city (roads, buildings, 16 civilians, the race's
terror units: cyberdiscs, reapers, chryssalids and their zombies, silacoids,
celatids, sectopods). Ignoring a terror site costs 1000 points (the DOS
penalty).
See [docs/design.md](docs/design.md) for the design and measured speeds.

## Layout

| Path | Contents |
|---|---|
| `src/` | DOS engine: C (Open Watcom, 16-bit real mode) plus NASM assembly |
| `tools/` | Python converter, previews and test scripts (run on the modern PC) |
| `tests/` | Scripted key sequences for the engine's KEYS option |
| `docs/` | Design notes |
| `emu/` | 86Box machine configs (disk images and DOS files are not committed) |
| `packs/` | Converted game data, generated locally, never committed |
| `build/` | Build output |

## Building

Requirements on Windows: Open Watcom 2.0 in `C:\WATCOM`, NASM, Python 3.12 with
PyYAML (`pip install pyyaml`), and the original game data (DOS version 1.4, for
example the `UFO` folder of an OpenXcom install, or the Steam / GOG install).

## Converting the data

The project ships no game data: the packs are made from your own copy of
X-COM: UFO Defense. Players use `XCOMCONV.EXE` (a Windows x64 program that
needs no Python: `.\tools\mkconv.ps1` builds it with PyInstaller into
`build\conv\`); from the source tree the same program is
`python tools\xcomconv.py [game folder] [-o output folder]`:

```powershell
python tools\xcomconv.py                       # looks for Steam / GOG installs, then asks
python tools\xcomconv.py C:\Games\OpenXcom\UFO -o packs
```

It takes the folder that holds GEODATA, GEOGRAPH, MAPS, ROUTES, SOUND, TERRAIN,
UFOGRAPH, UFOINTRO and UNITS, or a Steam / GOG install folder around it (found
one or two levels down, names in any case), checks that the data is DOS 1.4
(every file it reads is there; GEODATA's files have DOS 1.4's sizes:
`--skip-check` overrides), and writes the EGA, CGA and VGA packs (three converters
at once; `--sequential` one after the other) to a `PACKS` folder next to the program:
the shared packs (sounds, music, names, UFOpaedia) and `PACKS.VER` in `PACKS`, each
mode's packs in `PACKS\EGA`, `PACKS\CGA` and `PACKS\VGA` (the layout mkdist gives
the private build). The rule files it needs (OpenXcom's, GPL v3) are in `data\xcom1`
and `data\common`, and XCOMCONV carries a copy. The UFOpaedia articles, the
mission briefings and the end slides' captions are read from the game's own
`GEODATA\ENGLISH.DAT`; the three armour articles the DOS game lacks have short
texts of our own. An OpenXcom installation can supply texts too (`--openxcom
<folder or en-US.yml>`; `--prefer-openxcom` takes all of them from there).

**Pack format version.** `src\packver.h` holds `PACK_FORMAT_VERSION`; the
converter writes it to `PACKS.VER` and the engine checks it once at startup
(`src\startup.c`): packs from another version, or without the marker, make it
print "Packs are from another version: run XCOMCONV.EXE" and exit. Raise the
number whenever a change makes older packs wrong.

The single steps (development; `data\xcom1` instead of OpenXcom's folder):

```powershell
python tools\convert.py C:\Games\OpenXcom\UFO data\xcom1 packs         # EGA
python tools\convert.py C:\Games\OpenXcom\UFO data\xcom1 packs --cga   # CGA
python tools\sound.py C:\Games\OpenXcom\UFO packs     # SOUNDS.PAK, MUSIC.PAK
python tools\packver.py packs                          # PACKS.VER (the engine needs it)
python tools\ui.py C:\Games\OpenXcom\UFO data\xcom1 packs [--cga]  # UIx.PAK alone
# convert.py also writes FLOORE.PAK / FLOORC.PAK, the floor items' pictures.
# and <pack>W.PAK beside every unit pack: the walk phases the packs leave out, loaded
# only with upper or extended memory; walkpak.py makes them alone for an existing folder:
python tools\walkpak.py C:\Games\OpenXcom\UFO data\xcom1 packs [--cga]
python tools\intro.py C:\Games\OpenXcom\UFO data\xcom1 packs [--cga]  # INTROx.PAK, SLIDESx.PAK alone
# WORLDE.PAK / WORLDC.PAK made before the borders branch lack the country borders
# ("XBD1"): the map then shows none; reconvert to get them.
# Mission packs converted before battle2 lack the MCD HE blockage ("XHB1"): blasts
# then pass fences and furniture freely; reconvert to get it.
.\build.ps1             # 8086 build (turbo 8088 and up)
.\build.ps1 -Cpu 186    # V20 / 80186 build (XCOMV20.EXE)
.\build.ps1 -Cpu 286    # 286 build (XCOM286.EXE; docs/design.md "The 286 build")
.\tools\mkconv.ps1      # build\conv\XCOMCONV.EXE
.\tools\mkdist.ps1      # build\dist\ + build\XCOMXT.ZIP (private, with your packs) and
                        # build\release\XCOMXT-GPL.ZIP (public: no packs; needs mkconv first)
.\tools\pubshots.ps1 -Install d   # release\screenshots\ (the public README's pictures) from
                        # installed releases in d\EGA, d\CGA, d\VGA, in 86Box (tools\pubshots.py)
# -Private makes one zip (-ZipDir: elsewhere than build\), XCOMXT.ZIP: the three exes, README.TXT,
# PACKS.VER and the shared packs in the root, the mode folders CGA\, EGA\ and VGA\ beside them
# (docs/design.md "Mode folders"; a machine needs only its folder). Before it, packs\ needs the V packs:
# convert.py --vga (all crash sites), --vga --specials, ui.py --vga (with the UFOpaedia
# pictures), intro.py --vga (SLIDESV); mkdist stops naming the packs it misses.
```

Copy `build\dist\` (or only its root files and the one mode folder the machine needs) to the
DOS machine and run `XCOMXT`. The mode is picked at the start (VGA on a 286 with a VGA card,
the VGA folder and 256 KB of XMS; else EGA when present; LOG `mode:`); `XCOMXT CGA`, `EGA`,
`HERC` and `VGA` force one. On a machine with only a mono card the game uses a Hercules card when it finds one (the CGA packs, shown as 640x200 in greys on the 720x348 screen); `XCOMXT HERC` forces it and says why when it cannot (a colour card present, an MDA, a card without the second 32 KB page). Players get the public zip: the three programs,
XCOMCONV.EXE, QUICKSTART.TXT and MANUAL.TXT (`release\`), COPYING and the source of
that commit in SOURCE\ (without docs\, tests\ and emu\).

The intro movie plays first, as in DOS (with its AdLib music); any key or
mouse click skips it, `XCOMXT NOINTRO` leaves it out. Winning or losing
the game shows the original slides with their texts (a key or click: the
next slide, Esc: the start menu). They need INTROE.PAK / INTROC.PAK and
SLIDESE.PAK / SLIDESC.PAK (`tools\intro.py`, also run by convert.py; without
them the game starts at the menu and ends with a message, as before).

## Playing

| Key | Action |
|---|---|
| Arrows, Home, End, PgUp, PgDn | Move the cursor (map directions) |
| + / - | Level up / down |
| Tab | Next soldier |
| Enter | Walk to the cursor |
| F / A / U | Snap / aimed / auto shot at the cursor |
| G | Throw the grenade in the left hand at the cursor (smoke, proximity, high explosive, flare: see below); with the left hand empty, the grenade carried that is quickest to reach is taken into it first (the inventory's TU: 3 from a shoulder, 4 from the belt, 8 from the backpack); one not primed yet is primed with timer 0 (it goes off at the end of the turn) |
| T | Prime the grenade in the left hand (taken into it as for G): the Set Timer window (0-23 turns: a digit, or the arrows and Enter, or a click); G then throws it |
| W | Launch missile (Blaster Launcher, Hovertank/Launcher): waypoints at the cursor with Enter (or clicks, up to 9), L or the Launch button fires, Esc / right button takes the last one back; A launches straight at the cursor |
| R | Reload (15 TU); with several clip types (AP / HE / IN, rockets) press 1-3 to pick one |
| K | Kneel / stand (4 / 8 TU, 115 % accuracy kneeling) |
| M | Medi-kit on yourself or the unit at the cursor, then H heal a fatal wound, S stimulant, P painkiller |
| X | Reserve TU for a snap / aimed / auto shot (cycles; the panel's reserve buttons show and set it) |
| I | Inventory as in DOS: hands, shoulders, legs, belt, backpack and the ground, each item the size it has in DOS, every move at DOS's TU cost. The arrows (Home, End) go through the items, Enter or a click picks one up; then the arrows move over the cells (R right hand, L left hand, B belt, P backpack: the first cell it fits, G ground) and Enter or a click puts it there, Esc puts it back; a clip put on an empty gun loads it (15 TU, any clip the gun takes); D drops the item at once; Tab, < and > (the buttons) the next and previous soldier; Esc, I or OK close |
| N | Motion scanner (if carried): shows unseen aliens that moved |
| P | Psi-Amp (if carried, trained soldiers): then P panic / C mind control the alien at the cursor |
| O | Mind probe (if carried): stats of the unit at the cursor |
| Space | Turn towards the cursor |
| V | Cursor to the next visible alien |
| C | Centre on the selected soldier |
| E | End turn |
| Q | Minimap (the panel's map button) |
| L | Show the levels above the view level too (layers button) |
| Esc | Options (as DOS's battle options key; also F1 and the panel's options button; PauseState): Load Game, Save Game, Abandon Game, Game Options, Cancel (Esc again); only in your turn, as in DOS. Game Options is DOS's Battlescape Options screen: scroll speed 1-5 (R), scroll type trigger / auto (T), fire speed (F), X-COM (X) and alien (A) movement speed 1-6, each a click on its numbered button or the key for the next value; OK (Enter, Esc) back to the battle, SAVE GAME (S); kept in XCOMXT.CFG. The view scrolls when the pointer is at a screen edge (auto) or with the left button down there (trigger) |
| F2 | Abort mission (the abort window; also the panel's abort button. DOS's key A is the aimed shot here) |

Saving in battle works as in DOS: Save Game in the options writes the
campaign and the battle to a slot; loading that slot (from the start
menu, the geoscape's options or the battle's Load Game) goes straight
back into the battle, in your turn as it was (positions, TU, damage to
the map, items on the floor, smoke and fire, primed grenades, wounds,
stun, morale, mind control, HWPs, the aliens' knowledge, the reserve-TU
setting), without the briefing; after it the campaign goes on as if it
had never been saved (see docs/design.md, "Saving in battle"). Quick
battles (`BATTLE`, OpenXcom's New Battle) are saved and loaded the same
way: such a save loads from the start menu, the geoscape or any battle
and goes back into the quick battle (after it, the start menu); Load Game
in a quick battle also loads campaigns, and Abandon Game goes to the start
menu.

The save and load lists are DOS's: ten slots numbered 1-10 (10 is the
file XCOMXT0.SAV; the digit keys 1-9 and 0 pick them), each with its
name, time and date. To save, click a slot (or type its digit): its name
line takes a cursor (a used slot keeps its name), type the name, Enter
saves, Esc leaves the line. To load, click a slot. A right click on a used
slot deletes that save after a question (an OpenXcom extra; DOS cannot
delete saves).

Geoscape with the mouse, as in the DOS game (OpenXcom's windows on the flat
map):

| Click | Opens |
|---|---|
| a base | Launch Interception for its craft (status, base, weapons / crew); the base becomes the current base. Click a ready craft (or one in flight, to redirect it), then its target on the map: a UFO, terror site, alien base, a way point, or an empty spot (a new way point, where it patrols); Confirm Destination: OK. "Go to Base" opens the basescape. Every transport flies its own mission |
| a craft in flight | its window: status, base, speed, altitude, fuel, damage, weapons; Return to Base, Select New Target, Patrol, Cancel |
| a UFO | its window: size, altitude, heading, speed (with a Hyper-wave Decoder: type, race, mission, zone); Intercept (the craft list, then Confirm), Centre on UFO (time 5 seconds), Cancel |
| a terror site, alien base or way point | its window: place, the craft on the way; Intercept, OK (several craft can share a way point) |
| several of them at once | a list of them first |
| the Intercept button | Launch Interception for every base's craft |
| the Graphs button (or G) | the DOS graphs: the icons on top pick UFO / X-COM activity by area or country, income by country, finance; the buttons on the left switch each line and the total on; the globe icon (or Esc) goes back |
| right button on the map | scrolls the map to centre there (DOS turns the globe) |
| the arrows under the time buttons | scroll the map left / right, up / down when zoomed in (DOS rotates the globe; held, they repeat) |
| the big / small globe beside them | zoom in / out: 1:1, 2x, 4x (a right click: all the way) |

In Select Destination time stands still; the window under the map has
Cancel (or Esc) and, for an Avenger once Cydonia or Bust is researched,
Cydonia: with soldiers at its base or HWPs aboard it asks "Are you sure you want to send
this craft on a mission to Cydonia?" (Yes: the final mission starts); Tab
and Enter pick the selected UFO or site from the keyboard, C the Cydonia
button; the arrows, + and - and the panel's rotate and zoom buttons work
there too. The DOS pop-ups stop the clock: a UFO detected (Intercept,
Centre on UFO, Cancel), tracking lost, a craft at its way point (OK,
Redirect Craft), low on fuel, a terror site (Intercept, Centre on Site),
an alien base located by agents, and a transport ready to land (Begin
Mission? Yes: the battle, No: it returns); Esc cancels or answers OK
(the landing: No), Enter is the landing's Yes. Craft without soldiers that reach a terror site, alien base or
crash site turn back, as in DOS; transports land there.

Geoscape keys: arrows + Enter place the base; arrows scroll the map (up /
down when zoomed in), + / - zoom in / out; 1-4 time speed, Space pause, Tab
select a detected UFO, terror site or alien base, A select a site only,
Enter send the troops of the current base (its ready transport with the
most seats; several transports can be out at once), P send it to patrol a
point (finds alien bases), D the minimised air combats back, F funding
by country, I launch the current base's next ready
armed craft at the selected UFO, B base (DOS's basescape: Build New Base,
Base Information with its Stores and Transfers, Soldiers, Equip Craft, Build
Facilities, Research, Manufacture, Transfer, Purchase / Recruit, Sell / Sack,
Geoscape: one click each; the small bases at the top or 1-8 change base; a
click on a facility dismantles it, a right click opens its screen, a right
click on a finished Psionic Laboratory is Psi Training; with the keyboard,
arrows and Enter or the first letters of a button's name), R soldiers of
the current base (wounds, transfers), S save, U UFOpaedia,
C final assault (once researched: the first ready Avenger with its crew,
as its Cydonia button in Select Destination), Esc options (DOS's GAME OPTIONS:
L load, S save, A abandon game: the start menu, where Q quits, C cancel). Hangar: 1 / 2 change the selected craft's
weapons, Enter on the last lines buys an Interceptor or a Skyranger (needs
a free hangar). Purchase / Recruit (soldiers, scientists, engineers, craft
and items in one list), Sell and Transfer: every row has three buttons each way,
like a tape recorder's: one triangle = 1, two = 10, three = all (all there is,
all that fit, or back to none); Enter, + or Right one more, - or Left one less,
. and , ten, End all, Home none, Tab OK (pays and orders, sells, sends: the
order is then on its way and listed under Transfers), Esc cancels. Long lists
have a scroll bar on the right (the arrows at its ends scroll, a click above
or below the thumb scrolls a page). The basescape's EQUIP CRAFT button is DOS's
Equip Craft: the craft list (Interception Craft), then a craft's screen
(damage, fuel, its weapon boxes: a click or the key 1 / 2 opens Select
Armament; a transport has the buttons Crew = Select Squad, a click on a
soldier cycles his craft and none, the Left and Right keys move him up or
down the list, which is the order the squad leaves the craft; a wounded
soldier leaves the crew when his transport is back at base, Equipment and Armour =
Select Armor, a click on a soldier picks Personal Armour or a Power Suit
from the stores). Equipment opens Equip Craft as in DOS: its HWPs and every item
soldiers can take into battle, Stores and Craft columns (Right / + /
Enter loads one, Left / - unloads one; an HWP takes a full load of
ammunition from the stores). The items aboard count in the base's stores
space. Before each battle the soldiers take the craft's items: as they
carried them when the last battle began (each soldier's equipment
layout), else as OpenXcom's automatic equipment does (a rifle or pistol
with a clip, a second clip, one of each other item, as strength allows;
flares at night); the rest lies on the floor of the craft. The battle
starts with DOS's inventory screen (no TU cost; every soldier at the
craft's floor; Esc when done). Afterwards what came back goes into the
stores and the craft takes its list again from them; what the stores
lack is listed ("Not enough equipment to fully re-equip squad"). The
base's Equip Squad screen sets armour (A).
Research and Manufacture work as in DOS: each base runs several projects
at once, with scientists or engineers allocated to each. The Current
Research / Current Production list shows them (N or the New Project / New
Production button adds one, Enter or a click on a line changes it); in
the allocation window + / - (Up / Down, the arrow buttons; a right click:
all / none) add or take staff, PgUp / PgDn ten scientists, A all free
ones; production units with Right / Left (PgUp / PgDn ten, I or a right
click: no end); Enter OK or Start, Esc cancels a new research project,
C cancels a running one, S stops a production run. Completions open the
DOS pop-ups (Research Completed with View Reports, the UFOpaedia article;
We can now research / produce with Allocate; Production or Construction
complete with Allocate Manufacture / Go to Base): Esc or O is OK, Enter
the second button. The arrow buttons (and the one and ten triangles of the
quantity lists, and the scroll bar arrows) repeat while the mouse button is held.
Sell / Sack: A chooses the whole stock; soldiers, scientists and engineers are
sacked there. Build Facilities: pick the facility in the list (click or
Enter), then a free square (click, or arrows and Enter); the list comes back
for the next one, Esc or OK leaves. With the `DEBUG` option: X lands
the selected UFO under the Skyranger, ! sends a battleship at the base, and
the basescape has a keyboard-only debug entry (type D, Enter: stores topped up with materials, UFO parts,
corpses and money; the base's research projects and construction finish
the next midnight, each production run's next unit within the hour, for
projects and runs with staff).

Air combat (starts when an interceptor reaches its UFO): 1 standoff,
2 cautious (longest weapon range), 3 standard (shortest weapon range),
4 aggressive (close in, fastest reload), 5 or Esc disengage (or click the
window's buttons; the UFO button shows the UFO). Weapons fire on their own
when the UFO is inside the marked range lines; 6 / 7 (or a click on the
weapon icon) switch weapon 1 / 2 off or on. Craft that reach their UFOs
together fight at once (up to four windows; a click or Tab picks the
window the keys go to). M (the window's corner) minimises a fight at
standoff range: the craft follows the UFO, an icon at the map's left
edge brings it back (a click, or D for all). Campaign battles start with
the mission briefing (any key).

Options (`BATTLE` skips the geoscape for quick random battles): `XCOMXT [CGA | HERC] [MISSION pak] [RACE pak] [NIGHT] [ARMOUR n] [GEAR] [PSI] [HWP digits] [CRAFT S|L|A] [SQUAD n] [SEED n] [ALIENS n] [DIFF n] [NOSB] [NOMUSIC] [NOFMSFX] [NOSOUND] [NOUMB] [EXACTAI] [NOEXACTAI] [NOINTRO] [DEBUG] [SEEALL] [SHOTLOG] [PACELOG] [TERROR] [KEYS file] [LOG file] [BENCH]`; `EXACTAI` and `NOEXACTAI` turn exact mode on and off whatever the CPU (below); `NOINTRO` starts without the intro movie (test scripts never play it unless they say `INTRO`: tests/README.md); `DIFF n` (0-4) sets the quick-battle difficulty; `HWP 14` puts HWPs in a quick battle by type digit (1 Tank/Cannon, 2 Tank/Rocket Launcher, 3 Tank/Laser Cannon, 4 Hovertank/Plasma, 5 Hovertank/Launcher; up to four, 4 seats each off the craft's seats); `CRAFT L` / `CRAFT A` land a quick battle's squad in the Lightning / Avenger (its own craft map and seats; `S`, the Skyranger, by default) and `SQUAD n` takes n soldiers instead of 8 (at most the craft's seats: 14, 12, 26). `GEAR` gives the first quick-battle soldier a small launcher, stun bombs, a stun rod and a motion scanner, and soldiers 2-5 the heavy weapons with their special ammunition and grenades; `PSI` gives quick-battle soldiers psi skill and Psi-Amps (the first a mind probe too).

Psionics: interrogating a live ethereal or sectoid leader opens Psi Lab research, then the Psionic Laboratory (10 trainees, monthly training), the Psi-Amp and the Mind Probe (research needs a probe found on aliens); Psi-Amps go aboard in Equip Craft like any item.

Battlescape effects as in DOS: smoke grenades leave 7-15 smoke a tile, and explosions smoke where they break walls and floors; it thins by one a round (a grenade's cloud lasts 7-15 rounds), drifts to neighbouring tiles, blocks sight (not shots) while thick and stuns soldiers standing in it; incendiary rounds and rockets set the area burning (5-10 damage to anyone caught, who may catch fire; none through a power suit), big explosions set flammable parts alight, and flammable terrain (crops, trees, furniture) spreads the fire and burns away; proximity grenades go off when a unit steps next to them; high explosive packs go off at the end of the turn; electro-flares light 6 tiles around them at night, where X-COM then sees as far as by day. HE ammunition explodes on impact. Special ammunition goes aboard in Equip Craft like the rest.

Stunned aliens (stun rod, stun bombs from the small launcher) are captured when the battle is won; they are kept as the DOS live aliens of their race and rank (Sectoid Leader, Floater Navigator, Reaper Terrorist, ...; also across transfers and sales), and interrogating one (a topic per race and rank as in DOS; the captive is used up) opens Alien Origins (any), The Martian Solution (a leader or commander), Cydonia or Bust (a commander), the Hyper-wave Decoder (a navigator) and the Psi Lab (a sectoid leader or commander, any ethereal). Soldiers are assigned to craft in the Soldiers list (R; Enter moves one to the next transport with a free seat); the transport takes its crew. Items dropped or left by the dead are recovered from the battlefield after a win. The battle map shows what lies on the ground as DOS does: a tile's heaviest item with its original floor picture, the bodies of the dead and the unconscious (soldiers, civilians, aliens, terror units) and the wrecks of destroyed HWPs (FLOORE.PAK / FLOORC.PAK; without them a plain mark).
`MISSION UR0E.PAK` is the terror site. Debugging: `SEEALL` (no fog of war,
every unit shown), `TERROR` (a terror site appears in the first hour),
`SHOTLOG` (with `LOG`: every shot's tile path and every terrain change in
the log, the map in MAPDUMP.BIN; `python tools\shotcheck.py MAPDUMP.BIN
LOG.TXT` replays them and reports any that cross a wall, `--sweep N` tests
N random sight lines on the map), `BOOMAIM` (DEBUG, and every alien with a
gun first takes snap shots at the nearest explosive terrain part on its
level: tests\BOOMAI.TXT), `PACELOG` (with `LOG`: every paced wait of the battle,
`PACE <what> <least ms> <took ms>`: the Game Options' speeds, docs/design.md,
"Pacing of the alien turn"; tests\PACE.TXT), `HOLDVIEW n` (a minute on a round's view, 1, a soldier down, 2, or a blast, 4, for screenshots: tests\SHOTVIEW.TXT).

Debugging: `ALIENBASE` starts a campaign with a hidden alien base next to
yours, `CYDONIA` with the final mission unlocked (`tests\ALIEN.TXT`,
`tests\CYDONIA.TXT`).

## Sound and memory

The game comes first: sound never takes the conventional memory a battle
needs.

- **Upper memory (UMBs)** (an XT UMB or EMS board with its driver, such
  as USE!UMBS, or EMM386 on a 386): the game takes them at start-up, the
  largest one whole and up to three more (LOG `UMB: ... paragraphs: map
  buffer at ...`, `UMB: another at ...`). Sound keeps the battle track (5
  KB; with a Sound Blaster also 8 KB for each screen's effects, so a few
  digitised effects per screen instead of all 23). The rest:
  - **44 KB or more:** the map buffer (39 KB) goes there, so every battle
    has 39 KB more conventional memory. In every battle measured (the
    tightest: a terror site with a tank and a hovertank) no HWP stayed
    aboard for room, the floor items showed their pictures, the music
    played and the city kept its first layout. From 96 KB two more tables
    (19.5 KB) go there too.
  - **Everything beyond that**, and any other UMB, holds program code
    that would otherwise be read from disk again (overlay areas). Measured
    in 86Box: a 53 KB UMB loads a quarter less code in a battle round; 64
    KB reloads only the inventory and options once; with 123 KB (an XT
    with RAM at D000-EFFF) nothing is reloaded in the battle or on the
    base screens, and a campaign spends about half as long reading code.
  - **Less than 44 KB:** no map buffer (the fallbacks can happen), but
    the code areas still help (a 32 KB UMB: as fast as 64 KB).

  `NOUMB` gives the UMB to sound only (at most 64 KB), as before.
- **EMS or XMS memory** (an EMS board with its LIM 3.2 / 4.0 driver,
  such as the Lo-tech 2 MB board with LTEMM.EXE; or an XMS driver: EXMS86
  on such a board, HIMEM on a 286+): the game keeps up to 2 MB of what it
  reads from disk there (the packs and its own overlay code, in 4 KB
  blocks), so after the first visit a screen, a battle round or a battle
  setup reads almost nothing from the disk (LOG `XMEM: EMS ...` or `XMEM:
  XMS ...`). The first visit costs what it costs without the board. EMS
  is used when both are there, and costs 0.25 KB of conventional memory;
  XMS costs nothing there either when there is a UMB (11 KB of its
  code area go to the cache), else 11 KB. All of it is given back when the game ends, however it ends.
  `NOXMEM` turns it off, `NOEMS` takes XMS even when EMS is there.
  Which boot set-up on an XT with a Lo-tech EMS board and RAM at D000:
  EMS (LTEMM alone, 64 KB UMB at D000) gives the shortest waits; XMS
  (LTEMM + EXMS86 `!!`, 128 KB UMB at D000-EFFF) leaves about 13 KB more
  free in a battle and reloads less code, at slightly longer reads. With both,
  every HWP was placed in the tightest battles measured.

- **8088 / V20:** PC speaker effects and AdLib music. A track (battle 5 KB,
  Mars 1.5 KB, geoscape 15 KB, win / lose after a battle) goes into a free
  hole of the program's heap when there is one, else into a DOS block at
  the top of memory, and only while 16 KB stay free for the game: in the
  tightest battles the music gives way. Measured: the same free memory
  with AdLib music as without sound. A Sound Blaster on an XT
  plays the music through its OPL2; its digitised effects need an upper
  memory block.
- **Digitised effects (Sound Blaster):** loaded only into memory outside
  the 640 KB:
  - an upper memory block (UMB) from DOS 5+ (`DOS=UMB`) or from an XMS
    driver's UMB function (HIMEM + EMM386 without `DOS=UMB`, or an XT UMB
    driver such as USE!UMBS): at least 8 KB; 32 KB holds the screen's
    music and all its effects (battle: 5 KB track + about 19 KB of
    effects; geoscape: 15 + 13 KB); 45 KB holds all 23 effects at once.
    The DMA reads the effects in place, so the UMB must be real memory
    below 1 MB (an XT RAM card, or EMM386, which handles DMA into its
    UMBs itself).
  - or XMS extended memory on a 286+ (HIMEM.SYS): all 23 effects (41 KB),
    each copied into a 2.7 KB DMA buffer when it plays (the buffer goes
    into a UMB when there is one, else it is the only conventional memory
    the Sound Blaster costs).
  - With neither, the LOG says `sound: no digital effects: ...`, the PC
    speaker plays the effects and the music stays on the card's OPL2.
- **16-bit Sound Blasters in an XT:** an SB16 in an 8-bit slot works on
  some models, in 8-bit DMA mode (DMA 1, IRQ 2-7; the game only uses the
  SB 1.x/2.0 commands); jumpered SB16s (CT1740 and the like) are the ones
  to try. Plug-and-play models (CT2940 and later, AWE) are configured by a
  driver that needs a 286+, so on an XT they stay silent or only their FM
  part answers at 388h. Their OPL3 plays the AdLib music either way when
  it answers there. Digitised effects on an XT then still need a UMB, which
  makes a 286+ with 1 MB and HIMEM the easy way to hear them.

## Testing in 86Box

Machines in `emu/`: `xt88ega` and `xt88cga` (8088 at 10 MHz, 640 KB), plus
`v20ega` and `v20cga`; `xt88sb` (Sound Blaster 1.0, 220h IRQ 7), `xt88sb2`
(SB 2.0, 240h IRQ 5) and `xt88adl` (AdLib only) for sound tests (use `-Hdd`). Each boots a 360K floppy built from MS-DOS 5.
`at286` (IBM XT 286: 6 MHz 286, 640 KB with an ISA memory card + 512 KB
extended, EGA, SB 2.0 at 220h IRQ 5; `tools/mkcmos.py` writes its CMOS
setup before each run) tests XMS. `at386` (ASUS 386DX, 2 MB, EGA, SB 2.0)
is meant for UMBs with EMM386; `u386` is a 386SX fallback for the
VGA version (86Box's DataExpert 386SX at 16 MHz, its slowest 386SX, for his
12 MHz one; 2 MB, Oak OTI-077 VGA, AdLib, HIMEM and DOS=HIGH as `u286`).
Their AMI BIOS (05/05/91) stops at "CMOS system options not set" with an
empty CMOS, so `tools/mkcmos.py --ami <ROM>` writes the CMOS from the ROM's
own setup defaults, with its second checksum, before each run. `-Config` writes CONFIG.SYS
lines and copies HIMEM.SYS / EMM386.EXE from `emu/dos5/` (from your own
MS-DOS 5 disks). `-Mouse` loads a mouse driver, `-FreeKB n` a tiny TSR
(`tools/reserve.asm`) that leaves the game about n KB (LOG: `DOS free at
start`).

```powershell
.\tools\emutest.ps1 -Machine at286 -Hdd -Config "DEVICE=A:\HIMEM.SYS" -Pre "SET BLASTER=A220 I5 D1 T3" -Run "..."
.\tools\emutest.ps1 -Machine at386 -Hdd -Config "DEVICE=A:\HIMEM.SYS","DEVICE=A:\EMM386.EXE NOEMS","DOS=UMB" -Run "..."
```

One-time setup: copy the boot files from your own MS-DOS 5.00 360K disk 1 into
`emu/dos5/` (not committed): `IO.SYS`, `MSDOS.SYS`, `COMMAND.COM`, and the first
512 bytes of the image as `bootsect.bin`.

```powershell
.\tools\emutest.ps1 -Machine xt88ega -Run BENCH                 # timings in BENCH.TXT
.\tools\emutest.ps1 -Machine xt88ega -Run "SEED 16 KEYS FIGHT.TXT LOG LOG.TXT" -Extra tests\FIGHT.TXT
```

`.\tools\smoke.ps1` runs the short smoke scripts (geoscape, base screens,
research and manufacture, air combat, a crash-site battle with loot, psi,
save and load, the geoscape's mouse windows, CGA, saving and loading in a battle; about 7 minutes in 4 parallel lanes, each run stopped when its last expected line is in the LOG: `-Lanes 1` runs them one after the other, `-Full` waits the whole time) and prints PASS / FAIL for each by
the LOG lines it expects; tests/README.md lists every script with its
options and expected lines.

The script builds, writes the floppy, boots 86Box, saves a screenshot and
closes the emulator; files the program wrote can be read back from
`emu\boot360.img` with 7-Zip. Use the 8086 build in 86Box: its V20 core (6.0)
computes `IMUL r16, r/m16, imm` wrongly.

## License

xcom-xt, Copyright (C) 2026 the xcom-xt authors.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program (the file [COPYING](COPYING)); if not, see
<https://www.gnu.org/licenses/>.

Every source file in `src/` and `tools/` carries an
`SPDX-License-Identifier: GPL-3.0-or-later` line.

## Credits and game data

**OpenXcom.** The rules the engine follows (terrains, UFOs, craft, alien
races and units, deployments, map scripts, items, facilities, the world's
regions and countries) and the formats of the original game's files come from
[OpenXcom](https://openxcom.org), the open source clone of the game
(Copyright 2010-2016 OpenXcom Developers, GNU General Public License
version 3 or later). OpenXcom's source is our reference for how the
original game works, and its rule files (`*.rul`) and soldier name lists are
included in `data/` (see `data/xcom1/README` and `data/common/README`),
under the same licence.

**X-COM: UFO Defense.** The game, its graphics, maps, music, sound effects,
intro, texts and every other asset of X-COM: UFO Defense (UFO: Enemy Unknown),
(C) 1994 MicroProse and its present owners, are **not included** in this
project and are not covered by its licence. The packs the engine plays from
are made on your own PC, from your own copy of the game, by
`XCOMCONV.EXE` (`tools/xcomconv.py`); they are for your own use and must not
be distributed. X-COM is a trademark of its owners; this project is not
affiliated with them.
