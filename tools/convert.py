# SPDX-License-Identifier: GPL-3.0-or-later
"""Converts the original X-COM data into engine mission packs (.PAK).

    python tools/convert.py <UFO data dir> <rules dir> <out dir> [--cga | --vga]
        [--terrain CULTA] [--ufo STR_SMALL_SCOUT] [--race STR_SECTOID]

--vga writes only the VGA prototype's battle packs (...V.PAK: 8 bits a
pixel, tools/vgasprite.py; armours, HWPs, races and the crash sites of
--terrain / --ufo; docs/design.md, "VGA prototype").

Without --terrain/--ufo every supported crash site is written. Packs are
named TTnV.PAK: terrain code, UFO number, E (EGA) or C (CGA). UR0V.PAK is
the terror site (city terrain, civilians, no UFO). Race packs RxV.PAK hold
the alien crews, terror packs TxV.PAK the race's terror units. BASEV.PAK
holds the base defence blocks (laid out by the engine from the facilities).

Pack layout (little endian), mirrored by src/pak.c:
    header (64 bytes)
    transports           "XCR1" (craft_section), when header byte 62 is 1: the
                         records, sprites, block and seats of each transport;
                         the engine loads the common records and sprites and
                         those of the landing craft (src/pak.c)
    MCD records          n_mcd x 16 bytes (the common ones, then each
                         transport's: numbered from the first craft record
                         id on, as the landing craft's are in the engine)
    fill / landing / big block ids (u8 each)
    unit set bases       n_unit_sets x u16 (first sprite of each 40-frame set)
    sprite offsets       (n_sprites + 1) x u32, relative to sprite data
    sprite data
    map blocks           per block: u8 sx, sy, sz, flags; sz*sy*sx*4 tile bytes
    UFO route nodes      u16 count; count x (u8 x, y, z, rank)
    spawn list           n_spawn x (u8 rank, u8 count); units come from a race pack
    quantities           "XDQ1", n_spawn x (u8 lowQty, highQty, dQty) for the
                         difficulty levels (optional: older packs end above)
    "XFL2"               the MCD records' byte 13 is the big wall kind << 4 |
                         fuel ("XFL1": flammability nibble << 4 | fuel)
    map script data      only with a script >= 2: "XSC1", u8 digTunnel records
                         (west wall, north wall, corner, floor), u8 special type
                         of objective tiles (0 none), then per block u8 count,
                         count x (u8 x, y, z, rank) route nodes (older packs;
                         newer ones have "XRT1" instead)
    "XRT2"               route nodes of every block for the alien AI (see
                         route_section): crew share outside the UFO, then per
                         block u8 count, count x 11 bytes ("XRT1": 10 bytes,
                         without the base defence target flag)
    trailer              "XCU1" colour use (EGA), "XMM1" minimap pictures,
                         u16 length, "XTR1" (see trailer(); read from the
                         end by src/batui.c, the loaders stop before it)

Header byte 56 is the map script: 0 crash site, 1 city, 2 alien base,
3 Mars surface, 4 Cydonia base. Without a UFO the "UFO block" is the
script's main block (command centre, pyramid, brain). UBASEV.PAK is the
alien base, CYD1V / CYD2V the two stages of Cydonia.

Also writes UFOPAED.PAK, the UFOpaedia texts (tools/pedia.py), and
NAMES.PAK, the soldier names (tools/names.py).
"""

import argparse
import os
import struct

import cgasprite
import egacolor
import egasprite
import gametext
import intro
import mission as missionmod
import names
import occlude
import packver
import pedia
import rules as rulesmod
import ui
import ufodata
import units
import vgasprite

MAGIC = b"XPK2"
VERSION = 2
# The battlescape cursor's frames of CURSOR.PCK (src/render.h CF_*): the
# red and yellow (a unit on the tile) box, back and front halves, the red
# and yellow aim crosshairs, then the psi, waypoint and throw marks drawn
# over the box (OpenXcom Map::drawTerrain; the blinking and the marks'
# second frames are left out).
CURSOR_PICK = (0, 1, 3, 4, 6, 7, 11, 13, 15)
VIDEO_EGA, VIDEO_CGA, VIDEO_VGA = 0, 1, 2     # VGA: mode 13h battle packs (tools/vgasprite.py)
CGA_PALETTE = "0lo"     # green / red / brown on black: best for outdoor maps

WEAPON_PLASMA_PISTOL, WEAPON_PLASMA_RIFLE = 1, 2

TERRAIN_CODES = {"CULTA": "FA", "DESERT": "DE", "FOREST": "FO", "JUNGLE": "JU",
                 "MOUNT": "MO", "POLAR": "PO"}
UFO_CODES = {"STR_SMALL_SCOUT": 1, "STR_MEDIUM_SCOUT": 2, "STR_LARGE_SCOUT": 3,
             "STR_HARVESTER": 4, "STR_ABDUCTOR": 5, "STR_TERROR_SHIP": 6,
             "STR_BATTLESHIP": 7, "STR_SUPPLY_SHIP": 8}

# Bit 6 is the explosive flag (MCD_EXPL: HE_Strength, the strength in the
# trailer's "XEX1"; the engine never read Block_Fire, which had the bit).
FLAG_BITS = (("stop_los", 0), ("no_floor", 1), ("big_wall", 2), ("door", 3),
             ("ufo_door", 4), ("gravlift", 5), ("he_strength", 6),
             ("block_smoke", 7))

RANK_NAMES = ["Commander", "Leader", "Engineer", "Medic", "Navigator", "Soldier",
              "Terrorist", "Terrorist"]


SPECIAL_XBASE = 64      # an X-COM base object (MCD byte 60, isBaseModule)


def mcd_record(rec, sprite, or_sprite):
    # Byte 13: the big wall kind (OpenXcom's bigWall, from its MCD patches:
    # how a big wall blocks blasts by side, TileEngine::blockage, detonate)
    # << 4 | the fuel (Fuel, 0-15). The Flammable byte is in the trailer's
    # "XFM1".
    flags = 0
    for name, bit in FLAG_BITS:
        if rec[name]:
            flags |= 1 << bit
    # The flags byte is full: X-COM base objects (the machines a base
    # defence can wreck, SavedBattleGame::calculateModuleMap) get special
    # type 64; none of them has a special type of its own.
    special = rec["special"] or (SPECIAL_XBASE if rec["xcom_base"] else 0)
    return struct.pack("<HBBbBBBBBBBBBH", sprite, rec["tile_type"], rec["p_level"],
                       rec["t_level"], rec["tu_walk"], rec["tu_slide"], rec["tu_fly"],
                       flags, rec["armor"], rec["die_mcd"], rec["alt_mcd"],
                       special, burn_byte(rec), or_sprite)


def burn_byte(rec):
    """Big wall kind (0-9) << 4 | fuel (0-15)."""
    kind = rec.get("big_wall_kind", rec["big_wall"])
    if kind > 9:
        raise ValueError("big wall kind %d" % kind)
    return kind << 4 | min(rec["fuel"], 15)


RACE_CODES = {"STR_SECTOID": "S", "STR_FLOATER": "F", "STR_SNAKEMAN": "N",
              "STR_MUTON": "M", "STR_ETHEREAL": "E", "STR_MIXED": "X"}
# Personal armour, power suit, flying suit (armors.rul: XCOM_2.PCK with
# forcedTorso 2, the female torso frames 267-274, OpenXcom UnitSprite):
# sheet, torso frames, the hovering legs (drawRoutine0 legsFloat, None =
# none). The flying suit's pack has 8 more frames after the 48: standing
# with the hovering legs (275-282), by direction.
ARMOUR_SHEETS = {"1": ("XCOM_1", 32, None), "2": ("XCOM_2", 32, None),
                 "3": ("XCOM_2", 267, 275)}


def build_armour(root, sheet, video):
    """Unit frames of one soldier armour, in the race pack format (no ranks)."""
    codec, mapping = codec_for(root, video)
    sheet, torso, float_legs = sheet
    pck = ufodata.load_pck(root, "UNITS/" + sheet)
    if torso != 32:
        pck = list(pck)
        pck[32:40] = pck[torso:torso + 8]
    frames = units.unit_frames(pck, 0, kneel=True)
    if float_legs:
        hover = list(pck)
        hover[16:24] = pck[float_legs:float_legs + 8]
        frames += [units.compose(hover, d) for d in range(8)]
    data = bytearray()
    offsets = []
    for fr in frames:
        offsets.append(len(data))
        data += codec.encode(fr, mapping)
    offsets.append(len(data))
    out = bytearray(struct.pack("<4sHHHB", b"XRC1", 1, 1, len(frames), video))
    out += bytes(32 - len(out)) + bytes(8 * 24)
    for o in offsets:
        out += struct.pack("<I", o)
    out += data
    if video == VIDEO_EGA:
        out += trailer(colour_usage(frames, mapping))
    return bytes(out)


# Heavy weapons platforms: HnV.PAK, n = 1 Tank/Cannon, 2 Tank/Rocket
# Launcher, 3 Tank/Laser Cannon, 4 Hovertank/Plasma, 5 Hovertank/Launcher
# (the order of src/items.h): one unit set (units.hwp_frames: 40 frames,
# hovertanks 72) in the race pack format, loaded only for the HWPs on the
# mission (src/hwp.c).
HWP_PACKS = ((False, 0), (False, 1), (False, 2), (True, 3), (True, 4))


def build_hwp(root, kind, video):
    hover, turret = HWP_PACKS[kind]
    codec, mapping = codec_for(root, video)
    frames = units.hwp_frames(ufodata.load_pck(root, "UNITS/TANKS"), hover, turret)
    data = bytearray()
    offsets = []
    for fr in frames:
        offsets.append(len(data))
        data += codec.encode(fr, mapping)
    offsets.append(len(data))
    out = bytearray(struct.pack("<4sHHHB", b"XRC1", 1, 1, len(frames), video))
    out += bytes(32 - len(out)) + bytes(8 * 24)
    for o in offsets:
        out += struct.pack("<I", o)
    out += data
    if video == VIDEO_EGA:
        out += trailer(colour_usage(frames, mapping))
    return bytes(out)


# Items on the battle floor: FLOORV.PAK, the UNITS/FLOOROB pictures the
# battlescape draws for a tile's heaviest item (OpenXcom Tile::getTopItemSprite,
# items.rul floorSprite and weight, 3 when not given) and for the bodies of
# the dead and the unconscious (armors.rul corpseBattle: one item, or four
# for a 2x2 unit, one per quarter). The engine loads only the pictures of
# what the battle's units carry and of their bodies (src/pak.c).
#     "XFO1", u8 video, u8 n_frames, u8 n_items, u8 n_bodies
#     u8 item_frame[n_items]    src/items.h order, 0xFF = never on the floor
#     u8 body_frame[n_bodies]   first frame; 2x2 bodies have 4 in a row
#                               (quarter 0 = the unit's tile, 1 = x+1,
#                               2 = y+1, 3 = both), 0xFF = none
#     u8 weight[n_frames]
#     u16 offsets[n_frames + 1] relative to the sprite data, sprite data
# Bodies: the soldier armours (0-2), male and female civilians (3, 4), the
# Tank and the Hovertank (5, 6), then 7 + i for the live alien type
# LIVE_ALIENS[i] (the unit's UnitXStat.live - 1).
BODY_ARMORS = ["STR_NONE_UC", "STR_PERSONAL_ARMOR_UC", "STR_POWER_SUIT_UC",
               "CIVM_ARMOR", "CIVF_ARMOR", "TANK_ARMOR", "HOVERTANK_ARMOR"]
# Never on the floor: the HWPs' own weapons (the HWP is the item).
FLOOR_SKIP = {"IT_TANK_CANNON", "IT_TANK_ROCKET", "IT_TANK_LASER", "IT_HOVER_PLASMA",
              "IT_HOVER_LAUNCHER"}


def item_enum():
    """IT_* names by their value in src/items.h (assignments such as
    IT_LIVE_LAST = IT_LIVE + 28 leave the values between unnamed: None)."""
    import re
    with open(os.path.join(packver.src_dir(), "items.h"), encoding="ascii") as f:
        text = f.read()
    body = text[text.index("enum {") + 6:text.index("N_ITEMS")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    values, names, v = {}, [], 0
    for entry in (e.strip() for e in body.split(",")):
        if not entry:
            continue
        name, _, expr = (p.strip() for p in entry.partition("="))
        if expr:
            v = eval(expr, {}, values)
        values[name] = v
        names += [None] * (v - len(names)) + [name]
        v += 1
    return names


def build_floor(root, rules, rules_dir, video):
    import yaml
    with open(os.path.join(rules_dir, "items.rul"), encoding="utf-8") as f:
        rul = {it["type"]: it for it in yaml.safe_load(f)["items"]}
    codec, mapping = codec_for(root, video)
    pck = ufodata.load_pck(root, "UNITS/FLOOROB")
    frames, weights, index = [], [], {}

    def add(group):
        """Pack frames for a run of (floorob frame, weight); first index."""
        key = tuple(f for f, _ in group)
        if key in index:
            return index[key]
        first = len(frames)
        for f, w in group:
            frames.append(f)
            weights.append(w)
        index[key] = first
        return first

    def item_entry(t):
        it = rul[t]
        return it.get("floorSprite", -1), it.get("weight", 3)

    item_frame = []
    for name in item_enum():
        t = ui.ITEM_STR.get(name)
        sprite = item_entry(t)[0] if t and name not in FLOOR_SKIP else -1
        item_frame.append(add([item_entry(t)]) if sprite >= 0 else 0xFF)
    body_frame = []
    for armor in BODY_ARMORS + [rules.units[t]["armor"] for t in LIVE_ALIENS]:
        group = [item_entry(t) for t in rules.armors[armor].get("corpseBattle", [])]
        body_frame.append(add(group) if group and all(f >= 0 for f, _ in group) else 0xFF)
    for i, f in enumerate(frames):
        same = [weights[j] for j, g in enumerate(frames) if g == f]
        if len(set(same)) > 1:
            raise ValueError("floorob frame %d has weights %s" % (f, same))
    data = bytearray()
    offsets = []
    for f in frames:
        offsets.append(len(data))
        data += codec.encode(pck[f], mapping)
    offsets.append(len(data))
    out = bytearray(b"XFO1" + bytes((video, len(frames), len(item_frame), len(body_frame))))
    out += bytes(item_frame) + bytes(body_frame) + bytes(min(255, w) for w in weights)
    for o in offsets:
        out += struct.pack("<H", o)
    out += data
    return bytes(out), {"frames": len(frames), "total": len(out)}


# The blast's picture: BLASTV.PAK, read for each blast (src/bview.c):
#     "XBL1" u8 video, u8 0, u16 n, then n bytes: u16 offsets[8] (from these
#     n bytes) and 8 masked sprites of 32 x 40, 4 in a row: 128 x 80.
# DOS piles a blast's frames of UFOGRAPH/X1.PCK (128 x 64) on each other, each
# at its own random place and start (ExplosionBState::init, Map::drawTerrain,
# frame centred on the place): one moment of that, the later frames below the
# hotter ones, with the middle of the blast at (64, BLAST_MID).
BLAST_MID = 44
BLAST_PILE = ((5, -8, 0), (5, 8, -8), (5, 0, -14), (4, -11, -5), (4, 10, 2), (4, 2, -11),
              (3, -15, -2), (3, 14, -6), (3, -2, 4), (2, -20, 2), (2, 20, -3), (2, 0, -8),
              (1, -26, 4), (1, 26, 0), (1, -8, 5), (1, 8, 3))
BLAST_HOT, BLAST_DIM = 205, 130    # CGA: brown from this brightness (of 255) on, red; black below


def blast_picture(root):
    """The pile, as 80 rows of 128 palette indices (0 = transparent)."""
    frames = ufodata.load_pck(root, "UFOGRAPH/X1", 128, 64)
    pic = [[0] * 128 for _ in range(80)]
    for f, sx, sy in sorted(BLAST_PILE, key=lambda p: -p[0]):
        for y, row in enumerate(frames[f]):
            for x, c in enumerate(row):
                px, py = 64 + sx - 64 + x, BLAST_MID + sy - 32 + y
                if c and 0 <= px < 128 and 0 <= py < 80:
                    pic[py][px] = c
    return pic


def build_blast(root, video):
    codec, mapping = codec_for(root, video)
    if video == VIDEO_CGA:
        # Nearest colours make it one brown blob, lost on a brown floor: by
        # brightness, the hottest parts brown (white on Hercules), then red (grey
        # there) and black lines.
        palette = ufodata.load_palette(root, ufodata.PAL_BATTLESCAPE)
        mapping = list(mapping)
        for i, (r, g, b) in enumerate(palette):
            lum = (3 * r + 6 * g + b) // 10
            mapping[i] = (3, 3, 0) if lum >= BLAST_HOT else (2, 2, 0) if lum >= BLAST_DIM else (0, 0, 0)
    pic = blast_picture(root)
    offsets, data = [], bytearray()
    for i in range(8):
        tile = [row[(i & 3) * 32:(i & 3) * 32 + 32] for row in pic[(i >> 2) * 40:(i >> 2) * 40 + 40]]
        offsets.append(16 + len(data))
        data += codec.encode(tile, mapping)
        if len(data) & 1:
            data.append(0)
    body = b"".join(struct.pack("<H", o) for o in offsets) + bytes(data)
    return b"XBL1" + bytes((video, 0)) + struct.pack("<H", len(body)) + body


# DOS's smoke and fire pictures for VGA: SMOKEV.PAK (src/vgainit.c, drawn by
# src/vgaasm.asm op_fx):
#     "XSM1" u8 video (2), u8 n (14), u16 bytes, then those bytes: u16
#     offsets[n] (from these bytes) and n masked sprites of 32 x 40:
#     UFOGRAPH/SMOKE.PCK frames 0-3 (a fire), 8-13 (smoke, three
#     thicknesses of four frames each overlapping) and 4-7 (a burning
#     unit): OpenXcom Map::drawTerrain, drawUnit.
SMOKE_FRAMES = (0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7)


def build_smoke(root):
    codec, mapping = codec_for(root, VIDEO_VGA)
    frames = ufodata.load_pck(root, "UFOGRAPH/SMOKE", 32, 40)
    n = len(SMOKE_FRAMES)
    offsets, data = [], bytearray()
    for f in SMOKE_FRAMES:
        offsets.append(2 * n + len(data))
        data += codec.encode(frames[f], mapping)
        if len(data) & 1:
            data.append(0)
    body = b"".join(struct.pack("<H", o) for o in offsets) + bytes(data)
    return b"XSM1" + bytes((VIDEO_VGA, n)) + struct.pack("<H", len(body)) + body


# Pack trailer, read from the end of the file by the battle screens
# (src/batui.c), after whatever the loaders read:
#     "XCU1" u8 usage[16]       EGA packs: per mille of the sprites' opaque
#                               pixels in each EGA colour (capped at 255): the
#                               night palette frees rarely used registers
#                               for the control panel
#     "XMM1" u16 n, n frames    mission packs: minimap pictures (SCANG.DAT)
#                               of MCD records 0..n_mcd-1, then UNIT_MINI
#                               unit markers; EGA 4 x u16 (a nibble a pixel,
#                               the left one high) + u16 mask (bit 15 = row 0
#                               left, 1 = drawn); CGA 4 x u8 + u16 mask
#     "XHB1" u16 n, u8 he[n]    mission packs: the HE blockage of MCD records
#                               0..n-1 (TileEngine::blockage: an explosion
#                               loses twice it passing the part; src/pak.c)
#     "XLT1" u16 n, u8 scorched, n x (u8 record, u8 light)
#                               mission packs: the scorched earth record
#                               (BLANKS.MCD 1: a ground floor destroyed,
#                               Tile::destroy; 0 none) and the MCD records
#                               that are light sources of 6 or more
#                               (calculateTerrainLighting; src/pak.c)
#     "XEX1" u16 n, n x (u8 record, u8 strength)
#                               mission packs: the common MCD records that
#                               explode when destroyed (HE_Strength; flag
#                               bit 6 of their records; src/pak.c)
#     "XLF1" ...                last (see loft_section)
#     u16 length of the sections above, "XTR1"
UNIT_MINI = 9           # SCANG.DAT 0-8: X-COM, alien, civilian x 3 animation frames
SCANG_TERRAIN = 35      # MCD minimap index n is SCANG.DAT frame n + 35


def colour_usage(frames, mapping):
    """'XCU1' section: EGA colour use of the frames' opaque pixels
    (mapping: one for all frames, or a list of one per frame)."""
    maps = mapping if len(mapping) == len(frames) and isinstance(mapping[0], list)         else [mapping] * len(frames)
    seen = {}
    for frame, m in zip(frames, maps):
        for y, line in enumerate(frame):
            for x, idx in enumerate(line):
                if idx:
                    key = (id(m), idx, x & 1, y & 1)
                    seen[key] = seen.get(key, 0) + 1
    by_id = {id(m): m for m in maps}
    count = [0] * 16
    for (mid, idx, x, y), n in seen.items():
        count[egacolor.pixel_colour(by_id[mid], idx, x, y)] += n
    total = max(1, sum(count))
    return b"XCU1" + bytes(min(255, (c * 1000 + total - 1) // total) for c in count)


def minimap_frames(root, records, video, mapping):
    """'XMM1' section: a 4x4 picture per MCD record, then the unit markers."""
    scang = ufodata.read(root, "GEODATA/SCANG.DAT")
    n_scang = len(scang) // 16
    frames = [(rec.get("scang", 0) + SCANG_TERRAIN) if rec and rec.get("scang") else None
              for rec in records] + list(range(UNIT_MINI))
    out = bytearray(b"XMM1" + struct.pack("<H", len(frames)))
    for f in frames:
        px = scang[f * 16:f * 16 + 16] if f is not None and f < n_scang else bytes(16)
        mask = 0
        rows = []
        for y in range(4):
            v = 0
            for x in range(4):
                idx = px[y * 4 + x]
                if idx:
                    mask |= 0x8000 >> (y * 4 + x)
                    c = egacolor.pixel_colour(mapping, idx, x, y)
                    v |= c << ((3 - x) * (4 if video == VIDEO_EGA else 2))
            rows.append(v)
        if video == VIDEO_EGA:
            out += struct.pack("<4HH", *rows, mask)
        else:
            out += struct.pack("<4BH", *rows, mask)
    return bytes(out)


def minimap_vga(root, records):
    """'XMV1' section (VGA packs): minimap_frames' pictures as palette indices."""
    scang = ufodata.read(root, "GEODATA/SCANG.DAT")
    n_scang = len(scang) // 16
    frames = [(rec.get("scang", 0) + SCANG_TERRAIN) if rec and rec.get("scang") else None
              for rec in records] + list(range(UNIT_MINI))
    out = bytearray(b"XMV1" + struct.pack("<H", len(frames)))
    for f in frames:
        out += scang[f * 16:f * 16 + 16] if f is not None and f < n_scang else bytes(16)
    return bytes(out + struct.pack("<H", len(frames)))


MAX_HE_VALUES = 32      # src/pak.h: different HE blockages a battle can have


def he_blocks(records):
    """'XHB1' section: the MCD records' HE blockage."""
    if len({0} | {r["he_block"] for r in records[1:]}) > MAX_HE_VALUES:
        raise ValueError("more than %d HE blockage values" % MAX_HE_VALUES)
    return (b"XHB1" + struct.pack("<H", len(records))
            + bytes([0] + [r["he_block"] for r in records[1:]]))


def flammability(records):
    """'XFM1' section: the MCD records' Flammable byte (255: never burns)."""
    return (b"XFM1" + struct.pack("<H", len(records))
            + bytes([255] + [r["flammable"] for r in records[1:]]))


MAX_EXPL = 8            # src/pak.h Pack.expl


def explosives(records, n_common):
    """'XEX1' section: the common MCD records that explode when destroyed
    (HE_Strength, Tile::destroy; all of UFO Defense's are HE_Type 0, high
    explosive): record and strength."""
    ex = [(i, r["he_strength"]) for i, r in enumerate(records[:n_common])
          if i and r and r["he_strength"]]
    if len(ex) > MAX_EXPL or any(records[i]["he_type"] for i, _ in ex):
        raise ValueError("explosive MCD records: %s" % ex)
    out = b"XEX1" + struct.pack("<H", len(ex))
    for i, s in ex:
        out += bytes((i, s))
    return out


# Unit shapes (armors.rul loftempsSet) at fixed template ids 1-7 of the
# "XLF1" section: 1x1 units by body width (LOFT 2: 5 voxels, 3: 7, 5: 11),
# then a 2x2 unit's quarters 0-3 (LOFT 92, 89, 90, 91).
UNIT_LOFTS = (2, 3, 5, 92, 89, 90, 91)


def craft_section(n_common, n_common_sprites, craft_info, first_block):
    """'XCR1' after the header (header byte 62 = 1): the transports' maps,
    in src/pak.h CM_* order (Skyranger, Lightning, Avenger).

        "XCR1", u8 n_crafts, u16 first craft record id (n_common), u16 first
        craft sprite (n_common_sprites); per craft: u16 first record and u8
        count in the pack's record table, u16 first sprite and u16 count in
        its sprite list, u16 map block, u8 n_deploy, n_deploy x (u8 x, y, z,
        direction): crafts.rul deployment, the seats in order (none: the
        start tiles in map order, BattlescapeGenerator::addXCOMUnit)."""
    out = bytearray(b"XCR1" + struct.pack("<BHH", len(craft_info), n_common, n_common_sprites))
    for k, (first, count, spr, n_spr, c) in enumerate(craft_info):
        dep = c["deployment"]
        out += struct.pack("<HBHHHB", first, count, spr, n_spr, first_block + k, len(dep))
        for d in dep:
            out += bytes(d)
    return bytes(out)


def loft_section(root, records, alt_index=None):
    """'XLF1' section: the voxel shapes of the MCD records for shots and
    sight (TileEngine::voxelCheck), only the LOFTEMPS.DAT templates the
    pack uses (plus the unit shapes).

        "XLF1", u16 n_mcd, u16 size, then a blob of size bytes (src/vox.c):
        u16 o_tmask, o_tval, o_gen, o_rows   offsets in the blob
        u16 col[n_col][2]   each column's runs (offset) and layers that hold\n                            voxels (bit per layer); column 0: no voxels
        runs                per column (u8 last layer, u8 template) pairs
                            from the bottom layer (2 voxels each) up to 11
        tmask[n_tpl] u16    rows (y) of the template that hold voxels, bit y
        tval[n_tpl] u8      < 0x80: every such row is rows[tval]; else the
                            template's 16 rows are gen[(tval & 0x7f) * 16 + y]
        gen                 16 row indices per general template
        rows[] u16          row bits as LOFTEMPS.DAT: voxel x is bit 15 - x
    Each record's column goes into its tu_slide byte (the engine never uses
    sliding TU), set here in the records before they are written. Open UFO
    doors (a record whose alt is a UFO door and that is none itself) get no
    voxels: OpenXcom skips open UFO doors."""
    with open(os.path.join(root, "GEODATA", "LOFTEMPS.DAT"), "rb") as f:
        data = f.read()
    loft = [struct.unpack_from("<16H", data, i * 32) for i in range(len(data) // 32)]
    tpl_ids, tpls = {}, [None]
    for l in UNIT_LOFTS:
        tpl_ids[l] = len(tpls)
        tpls.append(loft[l])
    cols, col_ids, col_of = [(0,) * 12], {(0,) * 12: 0}, []
    for i, rec in enumerate(records):
        col = (0,) * 12
        if rec and i:
            a = alt_index[i] if alt_index else rec["alt_mcd"]
            alt = records[a] if a else None
            if not (alt and alt["ufo_door"] and not rec["ufo_door"]):
                ids = []
                for l in rec["loft"]:
                    if l >= len(loft) or not any(loft[l]):
                        ids.append(0)
                        continue
                    if l not in tpl_ids:
                        tpl_ids[l] = len(tpls)
                        tpls.append(loft[l])
                    ids.append(tpl_ids[l])
                col = tuple(ids)
        if col not in col_ids:
            col_ids[col] = len(cols)
            cols.append(col)
        col_of.append(col_ids[col])
    if len(cols) > 255 or len(tpls) > 255:
        raise ValueError("too many voxel columns or templates")
    rows, row_ids, gens = [], {}, []

    def row_id(v):
        if v not in row_ids:
            row_ids[v] = len(rows)
            rows.append(v)
        return row_ids[v]
    tmask, tval = [0], [0]
    for t in tpls[1:]:
        tmask.append(sum(1 << y for y in range(16) if t[y]))
        vals = set(t) - {0}
        if len(vals) <= 1:
            tval.append(row_id(vals.pop() if vals else 0))
        else:
            tval.append(0x80 | len(gens))
            gens.append(bytes(row_id(v) for v in t))
    if len(rows) > 127 or len(gens) > 127:
        raise ValueError("too many voxel rows")
    for rec, c in zip(records, col_of):
        if rec:
            rec["tu_slide"] = c
    runs = []
    for c in cols:
        r = bytearray()
        for l in range(12):
            if l == 11 or c[l + 1] != c[l]:
                r += bytes((l, c[l]))
        runs.append(bytes(r))
    o_cols = 8
    o_runs = o_cols + 4 * len(cols)
    offs, pos = [], o_runs
    for r in runs:
        offs.append(pos)
        pos += len(r)
    o_tmask = pos
    o_tval = o_tmask + 2 * len(tpls)
    o_gen = o_tval + len(tpls)
    o_rows = o_gen + 16 * len(gens)
    lmasks = [sum(1 << l for l in range(12) if c[l]) for c in cols]
    blob = (struct.pack("<4H", o_tmask, o_tval, o_gen, o_rows)
            + b"".join(struct.pack("<HH", o, m) for o, m in zip(offs, lmasks))
            + b"".join(runs) + struct.pack("<%dH" % len(tmask), *tmask)
            + bytes(tval) + b"".join(gens) + struct.pack("<%dH" % len(rows), *rows))
    return b"XLF1" + struct.pack("<HH", len(records), len(blob)) + blob


def trailer(*sections):
    body = b"".join(sections)
    return body + struct.pack("<H", len(body)) + b"XTR1"


def spawn_list(spec):
    """(rank, count) per deployment entry; the race pack supplies the units."""
    out = []
    for entry in spec["deployment"]["data"]:
        count = (entry["lowQty"] + entry["highQty"] + 1) // 2
        if count > 0:
            out.append((entry["alienRank"], count))
    return out


def spawn_quantities(spec):
    """(lowQty, highQty, dQty) for each spawn_list entry, same order."""
    out = []
    for entry in spec["deployment"]["data"]:
        if (entry["lowQty"] + entry["highQty"] + 1) // 2 > 0:
            out.append((entry["lowQty"], entry["highQty"], entry.get("dQty", 0)))
    return out


# Alien equipment (alienDeployments.rul itemSets, alienItemLevels.rul):
# "XIS1", u8 n, per spawn_list entry 3 set bytes (item levels 0-2), then
# 10 x (u8 zeros, u8 ones) per month: the month's row of 10 levels is
# sorted, so a roll of 0-9 below zeros is level 0, below zeros + ones 1,
# else 2. Set byte: bits 0-2 weapon (ITEM_WEAPONS, 0 none), 3-4 its clips
# - 1, 5-6 alien grenades, 7 mind probe.
ITEM_WEAPONS = {"STR_PLASMA_PISTOL": ("STR_PLASMA_PISTOL_CLIP", 1),
                "STR_PLASMA_RIFLE": ("STR_PLASMA_RIFLE_CLIP", 2),
                "STR_HEAVY_PLASMA": ("STR_HEAVY_PLASMA_CLIP", 3),
                "STR_SMALL_LAUNCHER": ("STR_STUN_BOMB", 4),
                "STR_BLASTER_LAUNCHER": ("STR_BLASTER_BOMB", 5)}


def item_set_byte(items):
    weapons = [i for i in items if i in ITEM_WEAPONS]
    assert len(weapons) <= 1, items
    b = 0
    if weapons:
        clip, code = ITEM_WEAPONS[weapons[0]]
        n = items.count(clip)
        assert 1 <= n <= 4, items
        b = code | (n - 1) << 3
    g = items.count("STR_ALIEN_GRENADE")
    assert g <= 3, items
    b |= g << 5 | (0x80 if "STR_MIND_PROBE" in items else 0)
    known = set(ITEM_WEAPONS) | {c for c, _ in ITEM_WEAPONS.values()}         | {"STR_ALIEN_GRENADE", "STR_MIND_PROBE"}
    assert not set(items) - known, items
    return b


def item_section(spec, rules):
    entries = [e for e in spec["deployment"]["data"]
               if (e["lowQty"] + e["highQty"] + 1) // 2 > 0]
    out = bytearray(b"XIS1") + bytes((len(entries),))
    for e in entries:
        sets = [list(s) for s in e.get("itemSets", [])][:3] or [[]]
        while len(sets) < 3:            # OpenXcom takes the last set for higher levels
            sets.append(sets[-1])
        out += bytes(item_set_byte(s) for s in sets)
    rows = rules.item_levels[:10]
    while len(rows) < 10:
        rows.append(rows[-1])
    for row in rows:
        assert len(row) == 10 and list(row) == sorted(row) and max(row) <= 2, row
        out += bytes((row.count(0), row.count(1)))
    return bytes(out)


def route_section(root, m, block_names, n_extra, spec):
    """"XRT2": the share of each crew entry placed outside the UFO
    (percentageOutsideUfo), then every block's route nodes in full (RMP):
    per block u8 count, count x 11 bytes: x, y, z (0 = ground), rank | 0x10
    small units only | 0x20 flying units only, patrol desirability | spawn
    priority << 4, 5 links (block node index, 251-254 exits to the east,
    south, west, north neighbour as OpenXcom pairs them, 255 none), 1 for a
    base defence target (Node::isTarget: RMP byte 22 is 5; the aliens go
    there and shoot the base's machinery). Nodes outside their block are
    kept with x = 255 so the links line up."""
    out = bytearray(b"XRT2")
    outside = [e.get("percentageOutsideUfo", 0) for e in spec["deployment"]["data"]
               if (e["lowQty"] + e["highQty"] + 1) // 2 > 0]
    out += bytes((len(outside),)) + bytes(outside)
    for name in block_names:
        sx, sy, sz = m.blocks[name][:3]
        try:
            nodes = ufodata.load_rmp(root, name, sz, sx, sy)
        except FileNotFoundError:
            nodes = []
        nodes = nodes[:250]
        out += bytes((len(nodes),))
        for n in nodes:
            rank = min(n["rank"], 15) | (0x10 if n["unit_type"] & 2 else 0)                 | (0x20 if n["unit_type"] & 1 else 0)
            pf = min(n["flags"], 15) | min(n["priority"], 15) << 4
            out += bytes((255 if n["bad"] else n["x"], n["y"], max(n["z"], 0), rank, pf)
                         + tuple(n["links"]) + (1 if n["reserved"] == 5 else 0,))
    out += bytes(n_extra)
    return bytes(out)


# The terrain mapping has one entry more than the palette: index 256 is black,
# the colour of the door outlines (door_frame).
OUTLINE = 256
UFO_DOOR_SIDE = 195     # the violet side face of the UFO doors' slab (U_EXT02 and the like)


def codec_for(root, video):
    palette = ufodata.load_palette(root, ufodata.PAL_BATTLESCAPE)
    if video == VIDEO_VGA:
        # the original palette index is the pixel (OUTLINE: black, unused)
        return vgasprite, list(range(256)) + [0]
    if video == VIDEO_CGA:
        mapping = egacolor.build_mapping(palette, target=egacolor.CGA_PALETTES[CGA_PALETTE])
        return cgasprite, mapping + [(0, 0, 0)]
    return egasprite, egacolor.build_mapping(palette) + [(0, 0, 0)]


def door_parts(records, alt_index):
    """Table indices of the door parts: the records with the UFO door or the
    door flag (MCD bytes 30 and 35, OpenXcom MapData isUFODoor / isDoor) and the
    records they turn into when opened (alt_index), walls and objects only (a
    floor part with the flag, the Avenger's open ramp, is not outlined)."""
    parts = set()
    for i, rec in enumerate(records):
        if i and rec and rec["tile_type"] and (rec["ufo_door"] or rec["door"]):
            parts.add(i)
            a = alt_index[i]
            if a and records[a]["tile_type"]:
                parts.add(a)
    return parts


def door_frame(frame):
    """The frame of a door part with its outline in black (OUTLINE): every
    opaque pixel that touches a transparent one (the frame's own border
    counts as opaque: sprites that meet their neighbour there stay joined),
    and the pixels beside the violet side face of a UFO door's slab. In 16
    and 4 colours the door's shades merge into the hull's grey (and brown on
    CGA); a black edge keeps the closed door's shape and the open door's thin
    slab visible against it. Colour 0 is black on every output (the
    Hercules colour table leaves it alone)."""
    out = [list(row) for row in frame]
    h = len(frame)
    for y, row in enumerate(frame):
        w = len(row)
        for x, c in enumerate(row):
            if not c:
                continue
            near = [row[x - 1] if x else -1, row[x + 1] if x < w - 1 else -1,
                    frame[y - 1][x] if y else -1, frame[y + 1][x] if y < h - 1 else -1]
            if 0 in near or (c != UFO_DOOR_SIDE and UFO_DOOR_SIDE in near):
                out[y][x] = OUTLINE
    return out


# Unit colours that would vanish on the maps' greys, browns and greens
# (EGA has no warm grey: the sectoids' skin became the terrain's light and
# dark grey checker). Per unit sheet and video, palette index -> (a, b,
# level) as in egacolor.build_mapping; the terrain keeps the plain mapping.
UNIT_TINTS = {
    # Sectoid skin ramp (83 light .. 94 dark): lilac lights, magenta body,
    # dark purple shadows on EGA (no map uses 5 / 13 much); red on CGA.
    "SECTOID": {
        VIDEO_EGA: {83: (13, 15, 2), 84: (13, 15, 1), 85: (7, 13, 2), 86: (7, 13, 2),
                    87: (5, 13, 2), 88: (5, 13, 1), 89: (5, 5, 0), 90: (5, 5, 0),
                    91: (0, 5, 3), 92: (0, 5, 2), 93: (0, 5, 1), 94: (0, 0, 0)},
        VIDEO_CGA: {83: (2, 3, 2), 84: (2, 3, 1), 85: (2, 3, 1), 86: (2, 2, 0),
                    87: (2, 2, 0), 88: (2, 2, 0), 89: (2, 2, 0), 90: (2, 2, 0),
                    91: (0, 2, 3), 92: (0, 2, 2), 93: (0, 2, 1), 94: (0, 0, 0)},
    },
    # Muton suit (69 light .. 76 dark): green and dark grey on EGA, the
    # jungle's own green; lighter, with light green highlights.
    "MUTON": {
        VIDEO_EGA: {69: (10, 15, 1), 70: (10, 10, 0), 71: (2, 10, 2), 72: (2, 10, 1),
                    73: (2, 2, 0), 74: (0, 2, 3), 75: (0, 2, 2), 76: (0, 2, 1)},
    },
}


def unit_mapping(mapping, sheet, video):
    """The mapping for one unit sheet's frames (UNIT_TINTS applied)."""
    tint = UNIT_TINTS.get(sheet, {}).get(video)
    if not tint:
        return mapping
    mapping = list(mapping)
    for idx, mix in tint.items():
        mapping[idx] = mix
    return mapping


# Terror units: (weapon code, AlienType flags) by unit type. Weapon codes
# 1 plasma pistol, 2 plasma rifle, 3 heavy plasma (also the living ranged
# weapons), 0 melee only (power = strength, as in the original).
FLAG_BIG, FLAG_ZOMBIFY, FLAG_TERROR = 1, 2, 4    # FLAG_TERROR: built-in weapon, no item sets
FLAG_FLY = 8            # armors.rul movementType 1 (src/pak.h AT_FLY)
TERROR_WEAPONS = {
    "STR_CYBERDISC_TERRORIST": 3, "STR_SECTOPOD_TERRORIST": 3,
    "STR_CELATID_TERRORIST": 3, "STR_CHRYSSALID_TERRORIST": 0,
    "STR_SILACOID_TERRORIST": 0, "STR_REAPER_TERRORIST": 0,
}
TERROR_NAMES = {
    "STR_CYBERDISC_TERRORIST": "Cyberdisc", "STR_SECTOPOD_TERRORIST": "Sectopod",
    "STR_CELATID_TERRORIST": "Celatid", "STR_CHRYSSALID_TERRORIST": "Chryssalid",
    "STR_SILACOID_TERRORIST": "Silacoid", "STR_REAPER_TERRORIST": "Reaper",
}


def _unit_type(u, name, weapon, flags=0, melee=0):
    s, a = u["stats"], u["armor"]
    if u.get("fly"):
        flags |= FLAG_FLY
    return struct.pack("<12sBBBBBBBBBBBB", name[:11].encode("ascii"), 0, s["tu"],
                       min(255, s["health"]), s["firing"], s["reactions"],
                       min(255, a[0]), min(255, a[1]), min(255, a[2]), weapon, 1,
                       flags, melee)


# Battle rules per rank ("XST1" section after the sprite data, read by
# src/pak.c race_load): u8 strength, stamina, bravery, throwing, under
# armour, energy recovery a turn, stand height, kneel height, float height,
# body width in voxels (LOFTEMPS shape; 0 = a 2x2 unit's full tile), melee
# accuracy, then the armour's damage modifiers in percent for AP, IN, HE,
# laser, plasma, stun, melee, acid, smoke (OpenXcom damage type order).
XST_SIZE = 21


def loft_width(root, entry):
    """Widest row of a LOFTEMPS.DAT shape (16 rows of 16 bits)."""
    with open(os.path.join(root, "GEODATA", "LOFTEMPS.DAT"), "rb") as f:
        data = f.read()
    best = 0
    for r in range(16):
        w = struct.unpack_from("<H", data, (entry * 16 + r) * 2)[0]
        best = max(best, bin(w).count("1"))
    return best


def unit_extra(root, rules, unit_type):
    u = rules.units[unit_type]
    a = rules.armors[u["armor"]]
    s = u["stats"]
    mods = list(a.get("damageModifier", [1.0] * 10))[1:10]
    width = 0 if a.get("size", 1) > 1 else loft_width(root, a.get("loftempsSet", [3])[0])
    return struct.pack("<11B9B", min(255, s.get("strength", 0)), min(255, s.get("stamina", 0)),
                       min(255, s.get("bravery", 0)), min(255, s.get("throwing", 0)),
                       min(255, a.get("underArmor", 0)), min(255, u.get("energyRecovery", 30)),
                       u.get("standHeight", 20), u.get("kneelHeight", 14),
                       u.get("floatHeight", 0), width, min(255, s.get("melee", 0)),
                       *[min(255, int(round(m * 100))) for m in mods])         + bytes((LIVE_ALIENS.index(unit_type) + 1 if unit_type in LIVE_ALIENS else 0,))


# Live aliens in src/items.h order (IT_LIVE + i, interrogation RS_INT + i):
# the race member's own type, so a muton at the commander's rank is a
# Muton Soldier.
LIVE_ALIENS = ["STR_" + n for n in (
    "SECTOID_COMMANDER SECTOID_LEADER SECTOID_ENGINEER SECTOID_MEDIC SECTOID_NAVIGATOR "
    "SECTOID_SOLDIER FLOATER_COMMANDER FLOATER_LEADER FLOATER_ENGINEER FLOATER_MEDIC "
    "FLOATER_NAVIGATOR FLOATER_SOLDIER SNAKEMAN_COMMANDER SNAKEMAN_LEADER SNAKEMAN_ENGINEER "
    "SNAKEMAN_NAVIGATOR SNAKEMAN_SOLDIER MUTON_ENGINEER MUTON_NAVIGATOR MUTON_SOLDIER "
    "ETHEREAL_COMMANDER ETHEREAL_LEADER ETHEREAL_SOLDIER CYBERDISC_TERRORIST "
    "SECTOPOD_TERRORIST CELATID_TERRORIST CHRYSSALID_TERRORIST SILACOID_TERRORIST "
    "REAPER_TERRORIST").split()]


def _unit_pack(root, video, sheets, types, extra=0, extras=None):
    """XRC1 pack from (sheet, routine) sets and 8 type records (set index
    in byte 12 of each record); extras: 8 XST1 records."""
    codec, mapping = codec_for(root, video)
    frames, maps = [], []
    for sheet, routine in sheets:
        fr = units.unit_frames(ufodata.load_pck(root, "UNITS/" + sheet), routine)
        frames.extend(fr)
        maps.extend([unit_mapping(mapping, sheet, video)] * len(fr))
    data = bytearray()
    offsets = []
    for fr, m in zip(frames, maps):
        offsets.append(len(data))
        data += codec.encode(fr, m)
    offsets.append(len(data))
    out = bytearray(struct.pack("<4sHHHBB", b"XRC1", 1, len(sheets), len(frames), video, extra))
    out += bytes(32 - len(out))
    for t in types:
        out += t
    for o in offsets:
        out += struct.pack("<I", o)
    out += data
    if extras:
        out += b"XST1" + b"".join(extras)
    if video == VIDEO_EGA:
        out += trailer(colour_usage(frames, maps))
    return bytes(out)


EMPTY_TYPE = bytes(24)


def build_race(root, rules, race, video, terror=False, zombies=True):
    """Race pack (crew ranks) or terror pack (terror units: drawing routines
    3, 5, 7, 8, 9, plus the zombie set if there are chryssalids).

    header (32 bytes): "XRC1", u16 version, u16 n_sets, u16 n_sprites, u8 video,
                       u8 zombie set + 1 (terror packs; 0 = none)
    types: 8 x 24 bytes by rank (name[12], set, tu, hp, firing, reactions,
           armour x3, weapon, valid, flags, melee power)
    sprite offsets (n_sprites + 1) x u32, sprite data
    "XST1", 8 x 21 bytes of battle rules by rank (see unit_extra; the last
            byte is the live alien: LIVE_ALIENS index + 1, 0 none)
    zombies=False leaves the zombie set out (Cydonia's terror pack: memory).
    """
    members = rules.races[race]["members"]
    race_name = race.replace("STR_", "").capitalize()
    sheets, types, extras, zombie = [], [], [], 0
    for rank in range(8):
        rec = EMPTY_TYPE
        ext = bytes(XST_SIZE)
        if rank < len(members):
            u = rules.battle_unit(members[rank])
            routine = u["routine"]
            wanted = (routine not in units.CREW_ROUTINES) if terror else (routine in units.CREW_ROUTINES)
            if wanted and routine in units.SUPPORTED_ROUTINES:
                key = (u["sheet"], routine)
                if key not in sheets:
                    sheets.append(key)
                if terror:
                    t = members[rank]
                    flags = (FLAG_BIG if routine in units.BIG_ROUTINES else 0) | FLAG_TERROR
                    if t == "STR_CHRYSSALID_TERRORIST":
                        flags |= FLAG_ZOMBIFY
                    weapon = TERROR_WEAPONS[t]
                    rec = _unit_type(u, TERROR_NAMES[t], weapon, flags,
                                     u["stats"]["strength"] if weapon == 0 else 0)
                elif race == "STR_MIXED":
                    # Cydonia's crew: the members' own race and rank
                    # (STR_ETHEREAL_SOLDIER at rank 0, ...): "Ethereal", "Ethereal Lea".
                    mrace, mrank = members[rank][4:].rsplit("_", 1)
                    name = mrace.capitalize()
                    if mrank != "SOLDIER":
                        name += " " + mrank.capitalize()[:3]
                    rec = _unit_type(u, name, WEAPON_PLASMA_RIFLE)
                else:
                    name = race_name if rank == 5 else "%s %s" % (race_name, RANK_NAMES[rank][:3])
                    rec = _unit_type(u, name, WEAPON_PLASMA_PISTOL if rank == 5 else WEAPON_PLASMA_RIFLE)
                rec = rec[:12] + bytes((sheets.index(key),)) + rec[13:]
                ext = unit_extra(root, rules, members[rank])
        types.append(rec)
        extras.append(ext)
    if terror and zombies and any(s == "CHRYS" for s, _ in sheets):
        sheets.append(("ZOMBIE", 4))
        zombie = len(sheets)
    if not sheets:
        return None, None
    out = _unit_pack(root, video, sheets, types, zombie, extras)
    return out, {"race": race_name, "sets": len(sheets), "total": len(out)}

WORLD_W, WORLD_H = 320, 160      # map pixels; longitude 180W..180E, latitude 90N..90S
GRID_W, GRID_H = 80, 40           # terrain lookup cells (4 x 4 pixels)
WORLD_TERRAINS = ["", "FA", "FO", "JU", "MO", "DE", "PO"]   # grid codes 0..6
# Device colours per grid code: sea first.
WORLD_EGA = [1, 10, 2, 2, 6, 14, 15]
WORLD_CGA = [0, 1, 1, 1, 3, 3, 2]
WORLD_OCEAN, WORLD_TEX = 192, 26  # VGA: the sea's palette index, the first texture frame


def run_length(data):
    """PackBits: n < 128 = n + 1 literal bytes, n >= 128 = repeat 257 - n."""
    out = bytearray()
    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and run < 128 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out += bytes((257 - run, data[i]))
            i += run
            continue
        j = i
        while j < len(data) and j - i < 128:
            if j + 2 < len(data) and data[j] == data[j + 1] == data[j + 2]:
                break
            j += 1
        out += bytes((j - i - 1,)) + data[i:j]
        i = j
    return out


def build_world(root, video, rules_dir):
    """World pack: the flat map bitmap in device format, a terrain grid and
    the alien side (regions, countries, missions: world.build_alien).

    header (16 bytes): "XWD1", u16 width, u16 height, u16 grid_w, u16 grid_h,
                       u8 video
    bitmap: EGA 4 planes x height x width/8, CGA height x width/4 (packed)
    grid:   grid_h x grid_w terrain codes (0 sea, 1..6 = WORLD_TERRAINS),
            run-length coded like the bitmap
    alien:  regions, countries, missions (world.build_alien)
    borders: the country border lines (world.build_borders, "XBD1"; a pack
            without them just has no borders)
    """
    import world
    raw = world.rasterise(root, WORLD_W, WORLD_H)
    # Start the map at 180 degrees west, like a wall map.
    half = WORLD_W // 2
    raw = [row[half:] + row[:half] for row in raw]
    codes = [[WORLD_TERRAINS.index(c) if c else 0 for c in row] for row in raw]
    palette = WORLD_EGA if video == VIDEO_EGA else WORLD_CGA
    out = bytearray(struct.pack("<4sHHHHB", b"XWD2", WORLD_W, WORLD_H, GRID_W, GRID_H, video))
    out += bytes(16 - len(out))
    header, out = out, bytearray()
    if video == VIDEO_VGA:
        # A byte a pixel in the geoscape palette, as DOS's globe in full
        # daylight: the ocean colour (globe.rul oceanPalette 12, Globe's
        # OCEAN_COLOR + its lit shade) and each polygon's texture
        # (TEXTURE.DAT, 39 of 32 x 32: the set DOS shows zoomed out,
        # frames 26-38) repeated over the map.
        tex = world.rasterise(root, WORLD_W, WORLD_H, textures=True)
        tex = [row[half:] + row[:half] for row in tex]
        frames = ufodata.read(root, "GEOGRAPH/TEXTURE.DAT")
        for y, row in enumerate(tex):
            for x, t in enumerate(row):
                out.append(WORLD_OCEAN if t is None else
                           frames[(WORLD_TEX + t) * 1024 + (y & 31) * 32 + (x & 31)])
    elif video == VIDEO_EGA:
        for p in range(4):
            for row in codes:
                for bx in range(WORLD_W // 8):
                    b = 0
                    for i in range(8):
                        if palette[row[bx * 8 + i]] & (1 << p):
                            b |= 0x80 >> i
                    out.append(b)
    else:
        for row in codes:
            for bx in range(WORLD_W // 4):
                b = 0
                for i in range(4):
                    b |= palette[row[bx * 4 + i]] << (6 - 2 * i)
                out.append(b)
    out = header + run_length(out)
    sx, sy = WORLD_W // GRID_W, WORLD_H // GRID_H
    grid = bytearray()
    for gy in range(GRID_H):
        for gx in range(GRID_W):
            # Majority land type in the cell, sea if mostly sea.
            cells = [codes[gy * sy + j][gx * sx + i] for j in range(sy) for i in range(sx)]
            land = [c for c in cells if c]
            grid.append(max(set(land), key=land.count) if len(land) * 2 >= len(cells) else 0)
    return bytes(out + run_length(grid)) + world.build_alien(rules_dir) + world.build_borders(rules_dir)


def build(root, rules, terrain, ufo, video, deployment=None):
    spec = missionmod.make_spec(rules, terrain, ufo)
    if deployment and ufo:
        spec["deployment"] = rules.deployments[deployment]
        spec["title"] = "Cydonia"
    return build_spec(root, spec, video, rules)


BASE_TUNNEL_WALLS = (13, 14)    # XBASE1 records: west / north wall beside a tunnel


def build_base(root, rules, video):
    """Base defence pack: the 21 XBASE blocks (one per facility, the hangar's
    four quarters 16-19, earth 20); src/map.c lays them out like the base.
    The 'UFO' is a hangar quarter (aliens come in there), the 'craft' the
    access lift. An extra 1x1x1 block holds the walls the engine puts beside
    the tunnels it digs between facilities (OpenXcom's XBASE map script)."""
    t = rules.terrains["XBASE"]
    dep = rules.deployments["STR_BASE_DEFENSE"]
    blocks = [b["name"] for b in t["mapBlocks"]]
    spec = {
        "title": "Base Defence",
        "terrain": {"sets": t["mapDataSets"], "blocks": blocks},
        "ufo": {"sets": [], "blocks": []},
        "craft": {"sets": [], "blocks": []},
        "fill_blocks": [blocks.index("XBASE_20")],
        "big_blocks": [],
        "landing_blocks": [],
        "map_size": (dep["width"], dep["length"], dep["height"]),
        "deployment": dep,
        "ufo_index": blocks.index("XBASE_16"),
        "craft_index": blocks.index("XBASE_00"),
        "extra_tiles": [("XBASE1", BASE_TUNNEL_WALLS)],
    }
    return build_spec(root, spec, video, rules)


def light_section(m):
    """'XLT1' section: the scorched earth record and the light sources."""
    lights = [(i, min(255, r["light_source"])) for i, r in enumerate(m.mcd)
              if r and r["light_source"] >= 6]
    out = b"XLT1" + struct.pack("<HB", len(lights), m.kept.get("scorched", 0))
    for i, light in lights:
        out += bytes((i, light))
    return out


def build_spec(root, spec, video, rules):
    # Keep the scorched earth record (BLANKS.MCD 1) that Tile::destroy puts
    # where a ground floor is destroyed.
    if any("BLANKS" in spec.get(p, {}).get("sets", []) for p in ("terrain", "ufo", "craft")):
        spec = dict(spec)
        spec["keep"] = dict(spec.get("keep", {}), scorched=("BLANKS", 1))
    m = missionmod.Mission(root, spec)
    codec, mapping = codec_for(root, video)
    spawn = spawn_list(spec)
    n_common = m.n_common if m.groups else len(m.mcd)

    # Terrain sprites actually referenced, in a compact order.
    # A door part's sprite is its own frame, with the outline (door_frame): the
    # key is the sprite and whether a door part uses it.
    doors = door_parts(m.mcd, m.alt_index)

    def terrain_frame(key):
        # VGA keeps the original colours: no outline needed (DOS has none)
        return door_frame(m.sprites[key[0]]) if key[1] and video != VIDEO_VGA else m.sprites[key[0]]

    sprite_frames = []
    remap = {}
    for i, rec in enumerate(m.mcd[1:n_common], 1):
        s = (rec["sprite"], i in doors)
        if s not in remap:
            remap[s] = len(sprite_frames)
            sprite_frames.append(terrain_frame(s))

    unit_bases = []
    # Soldiers (with kneeling frames), then civilians on terror sites.
    for sheet in ["XCOM_0"] + (["CIVM", "CIVF"] if spec.get("civilians") else []):
        unit_bases.append(len(sprite_frames))
        routine = rules.armors[{"CIVM": "CIVM_ARMOR", "CIVF": "CIVF_ARMOR"}[sheet]].get(
            "drawingRoutine", 0) if sheet != "XCOM_0" else 0
        sprite_frames.extend(units.unit_frames(ufodata.load_pck(root, "UNITS/" + sheet),
                                               routine, kneel=sheet == "XCOM_0"))
    cursor_base = len(sprite_frames)
    cursor = ufodata.load_pck(root, "UFOGRAPH/CURSOR")
    sprite_frames.extend(cursor[i] for i in CURSOR_PICK)

    # OR-format copies of ground floors that stay inside their diamond.
    or_frame = [False] * len(sprite_frames)
    or_remap = {}

    def add_or(records, table):
        for rec in records:
            s = rec["sprite"]
            if (rec["tile_type"] == 0 and rec["p_level"] == 0 and s not in table
                    and egasprite.is_simple_floor(m.sprites[s])):
                table[s] = len(sprite_frames)
                sprite_frames.append(m.sprites[s])
                or_frame.append(True)
    add_or(m.mcd[1:n_common], or_remap)
    rec_sprites = [(remap[(r["sprite"], i in doors)], or_remap.get(r["sprite"], 0xFFFF))
                   for i, r in enumerate(m.mcd[1:n_common], 1)]

    # Each transport's own sprites after the common ones (the pack's sprites
    # n_common_sprites on): numbered from there as its records are, the
    # engine loads the landing craft's (src/pak.c).
    n_common_sprites = len(sprite_frames)
    craft_info = []
    for (first, count), c in zip(m.groups, spec.get("crafts", [])):
        start = len(sprite_frames)
        local, local_or = {}, {}
        recs = m.mcd[first:first + count]
        for i, rec in enumerate(recs, first):
            k = (rec["sprite"], i in doors)
            if k not in remap and k not in local:
                local[k] = len(sprite_frames)
                sprite_frames.append(terrain_frame(k))
                or_frame.append(False)
        add_or([r for r in recs if r["sprite"] not in or_remap], local_or)
        shift = n_common_sprites - start
        for i, rec in enumerate(recs, first):
            s = rec["sprite"]
            k = (s, i in doors)
            o = or_remap.get(s, local_or[s] + shift if s in local_or else 0xFFFF)
            rec_sprites.append((remap[k] if k in remap else local[k] + shift, o))
        craft_info.append((first, count, start, len(sprite_frames) - start, c))
    if max([n_common + i[1] for i in craft_info] or [0]) > 256:
        raise ValueError("craft records past 255")

    sprite_data = bytearray()
    offsets = []
    for frame, is_or in zip(sprite_frames, or_frame):
        offsets.append(len(sprite_data))
        if not is_or:
            sprite_data += codec.encode(frame, mapping)
        else:
            sprite_data += codec.encode_or(frame, mapping)
    offsets.append(len(sprite_data))

    block_names = list(spec["terrain"]["blocks"])
    map_script = spec.get("map_script", 0)
    crafts = m.craft_blocks or ([spec["craft"]["blocks"][0]] if spec.get("craft", {}).get("blocks")
                                else [])
    if map_script:              # alien base, Mars, Cydonia: no UFO
        ufo_block, craft_block = spec["main_block"], 0xFFFF
        if crafts:
            craft_block = len(block_names)
            block_names += crafts
    elif "ufo_index" in spec:         # base defence: craft and UFO are terrain blocks
        ufo_block, craft_block = spec["ufo_index"], spec["craft_index"]
    else:
        block_names += spec["ufo"]["blocks"][:1]
        ufo_block = len(spec["terrain"]["blocks"]) if spec["ufo"]["blocks"] else 0xFFFF
        craft_block = len(block_names)
        block_names += crafts
    extra = [m.extra_tile(s, ids) for s, ids in spec.get("extra_tiles", [])]
    mx, my, mz = spec["map_size"]

    out = bytearray()
    out += struct.pack("<4sHHHHHHHBBBBHHHHBB24s", MAGIC, VERSION, len(m.mcd),
                       len(sprite_frames), len(block_names) + len(extra), mx, my, mz,
                       len(spec["fill_blocks"]), len(spec["landing_blocks"]),
                       len(spec["big_blocks"]), video, ufo_block, craft_block,
                       len(unit_bases), cursor_base, 0, len(spawn),
                       spec["title"].encode("ascii")[:23])
    # Map script (0 crash site, 1 city: roads, big blocks, weighted fill),
    # road blocks (east-west, north-south, crossing), civilians, big blocks,
    # 1 if the transports section follows the header.
    roads = spec.get("roads", (0, 0, 0))
    out += bytes((1 if spec.get("script") == "URBAN" else map_script,) + tuple(roads)
                 + (spec.get("civilians", 0), spec.get("big_count", 3), 1 if craft_info else 0))
    out += bytes(63 - len(out)) + b"\x01"    # byte 63: or_sprite holds cover data
    if craft_info:
        out += craft_section(n_common, n_common_sprites, craft_info, craft_block)

    xlf = loft_section(root, m.mcd, m.alt_index)     # (sets the records' tu_slide)
    out += bytes(16)                         # record 0: empty
    for rec, (s, o) in zip(m.mcd[1:], rec_sprites):
        # Cover data for the renderer (tools/occlude.py).
        frame = m.sprites[rec["sprite"]]
        if rec["tile_type"] == 0:
            full = rec["p_level"] == 0 and occlude.full_floor(frame)
            o = (0x7FFF if o == 0xFFFF else o) | (0x8000 if full else 0)
        else:
            o = occlude.cover_bits(frame, rec["p_level"])
        out += mcd_record(rec, s, o)
    out += bytes(spec["fill_blocks"]) + bytes(spec["landing_blocks"]) + bytes(spec["big_blocks"])
    for base in unit_bases:
        out += struct.pack("<H", base)
    for off in offsets:
        out += struct.pack("<I", off)
    out += sprite_data

    for name in block_names:
        sx, sy, sz, tiles = m.blocks[name]
        out += bytes((sx, sy, sz, 0))
        for z in range(sz):
            for y in range(sy):
                for x in range(sx):
                    out += bytes(tiles[z][y][x])
    for parts in extra:
        out += bytes((1, 1, 1, 0)) + bytes(parts)

    nodes = []
    if ufo_block != 0xFFFF and not map_script:
        ufo_name = block_names[ufo_block]
        nodes = ufodata.load_rmp(root, ufo_name, m.blocks[ufo_name][2])
    out += struct.pack("<H", len(nodes))
    for n in nodes:
        out += bytes((n["x"], n["y"], n["z"], n["rank"]))

    for rank, count in spawn:
        out += bytes((rank, count))
    out += b"XDQ1"
    for q in spawn_quantities(spec):
        out += bytes(q)
    out += item_section(spec, rules)
    out += b"XFL2"          # MCD byte 13 is the big wall kind and the fuel
    if map_script:
        kept = getattr(m, "kept", {})
        out += b"XSC1" + bytes((kept.get("west", 0), kept.get("north", 0),
                                kept.get("corner", 0), kept.get("floor", 0),
                                spec.get("objective", 0)))
    # Route nodes of every block, placed with the block by map_generate.
    out += route_section(root, m, block_names, len(extra), spec)
    sections = [colour_usage(sprite_frames, mapping)] if video == VIDEO_EGA else []
    pal = b""
    if video == VIDEO_VGA:
        # The prototype's screens other than the battle view run as on CGA
        # (src/vgainit.c): the minimap in the CGA format; after "XLF1" (the
        # loaders stop there) the battlescape palette for the DAC, "XPL1",
        # u16 768, 256 x (r, g, b) of 6 bits, last: src/vgainit.c finds it
        # 780 bytes before the end.
        _, cga_map = codec_for(root, VIDEO_CGA)
        mini = minimap_frames(root, m.mcd, VIDEO_CGA, cga_map)
        # DOS's battlescape palette: PAL_BATTLESCAPE with a grey ramp at
        # 240-255 (the file has other colours there; OpenXcom Mod.cpp
        # "Correct Battlescape palette", ui.battle_palette): the Skyranger's
        # hull, and every 16-colour ramp shades the DOS way (gfx_night).
        bpal = ui.battle_palette(root)
        # Before it the minimap in 256 colours, "XMV1": u16 n, n x 16
        # palette indices (SCANG.DAT, 0 clear), u16 n again (src/batui.c
        # reads it back from the end).
        pal = (minimap_vga(root, m.mcd)
               + b"XPL1" + struct.pack("<H", 768) + bytes(c // 4 for rgb in bpal for c in rgb))
    else:
        mini = minimap_frames(root, m.mcd, video, mapping)
    out += trailer(*sections, mini, he_blocks(m.mcd),
                   flammability(m.mcd), light_section(m), explosives(m.mcd, n_common), xlf, pal)

    stats = {"title": spec["title"], "mcd": len(m.mcd), "sprites": len(sprite_frames),
             "aliens": sum(c for _, c in spawn), "total": len(out)}
    return bytes(out), stats


def build_vga(args, rules):
    """The VGA prototype's packs (8 bits a pixel, tools/vgasprite.py): only
    what a quick battle needs."""
    video, vcode = VIDEO_VGA, "V"

    def write(name, data, stats=None):
        with open(os.path.join(args.outdir, name), "wb") as f:
            f.write(data)
        print(name, stats if stats is not None else len(data))
    write("WORLD%s.PAK" % vcode, build_world(args.data, video, args.rules))
    if args.world_only:
        return
    if args.specials:
        # The campaign's other battles (as main() builds them for E and C):
        # terror site, base defence, alien base, Cydonia.
        write("UR0%s.PAK" % vcode, *build(args.data, rules, "URBAN", None, video,
                                          "STR_TERROR_MISSION"))
        write("BASE%s.PAK" % vcode, *build_base(args.data, rules, video))
        write("CYD%s.PAK" % vcode, *build(args.data, rules, "MARS", "STR_BATTLESHIP", video,
                                          "STR_BATTLESHIP"))
        for name, spec in (
                ("UBASE", missionmod.make_script_spec(rules, "UBASE", "STR_ALIEN_BASE_ASSAULT",
                                                      missionmod.SCRIPT_ALIEN_BASE, "Alien Base")),
                ("CYD1", missionmod.make_script_spec(rules, "MARS", "STR_MARS_CYDONIA_LANDING",
                                                     missionmod.SCRIPT_MARS, "Cydonia - Mars")),
                ("CYD2", missionmod.make_script_spec(rules, "UBASE", "STR_MARS_THE_FINAL_ASSAULT",
                                                     missionmod.SCRIPT_BOSS, "Cydonia - Base"))):
            write("%s%s.PAK" % (name, vcode), *build_spec(args.data, spec, video, rules))
        return
    for code, sheet in ARMOUR_SHEETS.items():
        write("A%s%s.PAK" % (code, vcode), build_armour(args.data, sheet, video))
    for kind in range(len(HWP_PACKS)):
        write("H%d%s.PAK" % (kind + 1, vcode), build_hwp(args.data, kind, video))
    write("FLOOR%s.PAK" % vcode, *build_floor(args.data, rules, args.rules, video))
    write("BLAST%s.PAK" % vcode, build_blast(args.data, video))
    if video == VIDEO_VGA:
        write("SMOKEV.PAK", build_smoke(args.data))
    for race, code in RACE_CODES.items():
        for prefix, terror in (("R", False), ("T", True)):
            data, stats = build_race(args.data, rules, race, video, terror, zombies=True)
            if data is not None:
                write("%s%s%s.PAK" % (prefix, code, vcode), data, stats)
    for terrain in args.terrain or list(TERRAIN_CODES):
        for ufo in args.ufo or list(UFO_CODES):
            name = "%s%d%s.PAK" % (TERRAIN_CODES[terrain], UFO_CODES[ufo], vcode)
            try:
                data, stats = build(args.data, rules, terrain, ufo, video)
            except ValueError as e:
                print(name, "skipped:", e)
                continue
            write(name, data, stats)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data")
    ap.add_argument("rules")
    ap.add_argument("outdir")
    ap.add_argument("--cga", action="store_true")
    ap.add_argument("--vga", action="store_true",
                    help="VGA battle packs (the prototype: armours, HWPs, FLOORV, BLASTV, races and the "
                    "crash sites of --terrain / --ufo, named ...V.PAK)")
    ap.add_argument("--world-only", action="store_true",
                    help="with --vga: only WORLDV.PAK, the geoscape's map")
    ap.add_argument("--specials", action="store_true",
                    help="with --vga: WORLDV.PAK and the terror, base defence, alien base and "
                    "Cydonia packs only")
    ap.add_argument("--terrain", action="append")
    ap.add_argument("--ufo", action="append")
    ap.add_argument("--lang-yml", metavar="FILE", help="an OpenXcom language file (or an "
                    "OpenXcom folder): texts the game's ENGLISH.DAT lacks come from it")
    ap.add_argument("--prefer-yml", action="store_true",
                    help="take every text from --lang-yml, not from ENGLISH.DAT")
    args = ap.parse_args(argv)
    gametext.configure(args.lang_yml, args.prefer_yml)

    rules = rulesmod.Rules(args.rules)
    ufodata.MCD_PATCHES = rules.mcd_patches
    video = VIDEO_CGA if args.cga else VIDEO_EGA
    vcode = "C" if args.cga else "E"
    if args.vga:
        build_vga(args, rules)
        return
    with open(os.path.join(args.outdir, "WORLD%s.PAK" % vcode), "wb") as f:
        f.write(build_world(args.data, video, args.rules))
    print("WORLD%s.PAK" % vcode)
    data, stats = pedia.build_pedia(args.rules, args.data)
    with open(os.path.join(args.outdir, "UFOPAED.PAK"), "wb") as f:
        f.write(data)
    print("UFOPAED.PAK", stats)
    data, _ = ui.build(args.data, args.rules, args.cga)
    with open(os.path.join(args.outdir, "UI%s.PAK" % vcode), "wb") as f:
        f.write(data)
    print("UI%s.PAK" % vcode, len(data), "bytes")
    # The intro movie and the end slides (tools/intro.py).
    try:
        intro.convert(intro.parser().parse_args(
            [args.data, args.rules, args.outdir] + (["--cga"] if args.cga else [])))
    except (OSError, ValueError) as e:
        print("INTRO / SLIDES packs skipped:", e)
    # Soldier names: OpenXcom's common/SoldierName next to the rules folder
    # (data/common next to data/xcom1) or next to bin/standard/xcom1.
    for name_dir in (os.path.join(args.rules, "..", "common", "SoldierName"),
                     os.path.join(args.rules, "..", "..", "common", "SoldierName")):
        if os.path.isdir(name_dir):
            break
    else:
        name_dir = None
    if name_dir:
        data, n = names.build_names(name_dir)
        with open(os.path.join(args.outdir, "NAMES.PAK"), "wb") as f:
            f.write(data)
        print("NAMES.PAK", n, "nationalities")
    for code, sheet in ARMOUR_SHEETS.items():
        with open(os.path.join(args.outdir, "A%s%s.PAK" % (code, vcode)), "wb") as f:
            f.write(build_armour(args.data, sheet, video))
    for kind in range(len(HWP_PACKS)):
        data = build_hwp(args.data, kind, video)
        with open(os.path.join(args.outdir, "H%d%s.PAK" % (kind + 1, vcode)), "wb") as f:
            f.write(data)
        print("H%d%s.PAK" % (kind + 1, vcode), len(data), "bytes")
    data, stats = build_floor(args.data, rules, args.rules, video)
    with open(os.path.join(args.outdir, "FLOOR%s.PAK" % vcode), "wb") as f:
        f.write(data)
    print("FLOOR%s.PAK" % vcode, stats)
    data = build_blast(args.data, video)
    with open(os.path.join(args.outdir, "BLAST%s.PAK" % vcode), "wb") as f:
        f.write(data)
    print("BLAST%s.PAK" % vcode, len(data), "bytes")
    if video == VIDEO_VGA:
        data = build_smoke(args.data)
        with open(os.path.join(args.outdir, "SMOKEV.PAK"), "wb") as f:
            f.write(data)
        print("SMOKEV.PAK", len(data), "bytes")
    # The final mission: Mars, a battleship's worth of the mixed race.
    data, stats = build(args.data, rules, "MARS", "STR_BATTLESHIP", video, "STR_BATTLESHIP")
    with open(os.path.join(args.outdir, "CYD%s.PAK" % vcode), "wb") as f:
        f.write(data)
    print("CYD%s.PAK" % vcode, stats)
    # Alien base assault and the two stages of Cydonia (Mars surface, then
    # the base with the alien brain).
    for name, spec in (
            ("UBASE", missionmod.make_script_spec(rules, "UBASE", "STR_ALIEN_BASE_ASSAULT",
                                                  missionmod.SCRIPT_ALIEN_BASE, "Alien Base")),
            ("CYD1", missionmod.make_script_spec(rules, "MARS", "STR_MARS_CYDONIA_LANDING",
                                                 missionmod.SCRIPT_MARS, "Cydonia - Mars")),
            ("CYD2", missionmod.make_script_spec(rules, "UBASE", "STR_MARS_THE_FINAL_ASSAULT",
                                                 missionmod.SCRIPT_BOSS, "Cydonia - Base"))):
        data, stats = build_spec(args.data, spec, video, rules)
        with open(os.path.join(args.outdir, "%s%s.PAK" % (name, vcode)), "wb") as f:
            f.write(data)
        print("%s%s.PAK" % (name, vcode), stats)
    data, stats = build_base(args.data, rules, video)
    with open(os.path.join(args.outdir, "BASE%s.PAK" % vcode), "wb") as f:
        f.write(data)
    print("BASE%s.PAK" % vcode, stats)
    for race, code in RACE_CODES.items():
        for prefix, terror in (("R", False), ("T", True)):
            # Cydonia's terror units with the zombie set (13 KB): the
            # second stage keeps about 46 KB free without a Sound Blaster.
            data, stats = build_race(args.data, rules, race, video, terror,
                                     zombies=True)
            if data is None:
                continue
            name = "%s%s%s.PAK" % (prefix, code, vcode)
            with open(os.path.join(args.outdir, name), "wb") as f:
                f.write(data)
            print(name, stats)
    # Terror sites: the city with civilians; any race.
    data, stats = build(args.data, rules, "URBAN", None, video, "STR_TERROR_MISSION")
    with open(os.path.join(args.outdir, "UR0%s.PAK" % vcode), "wb") as f:
        f.write(data)
    print("UR0%s.PAK" % vcode, stats)
    for terrain in args.terrain or list(TERRAIN_CODES):
        for ufo in args.ufo or list(UFO_CODES):
            name = "%s%d%s.PAK" % (TERRAIN_CODES[terrain], UFO_CODES[ufo],
                                   "C" if args.cga else "E")
            try:
                data, stats = build(args.data, rules, terrain, ufo, video)
            except ValueError as e:
                print(name, "skipped:", e)
                continue
            with open(os.path.join(args.outdir, name), "wb") as f:
                f.write(data)
            print(name, stats)
    # The walk phases the unit packs leave out, beside each of them
    # (<name>W.PAK, loaded only with upper or extended memory: walkpak.py).
    import walkpak
    walkpak.write_all(args.data, rules, args.outdir, video)


if __name__ == "__main__":
    main()
