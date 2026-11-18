# SPDX-License-Identifier: GPL-3.0-or-later
"""Interface pack (UIE.PAK / UIC.PAK / UIV.PAK): the original backgrounds,
fonts, panels and item pictures in EGA or CGA colours, or (--vga, the VGA
prototype) as the original's own palette indices, for src/uik.c.

Entry numbers and formats are defined in src/uipak.h (read from there).

    python tools/ui.py <UFO data dir> <rules dir> <out dir> [--cga | --vga] [--preview dir]

VGA pictures: one byte a pixel, rows of w bytes, no alignment; masked
pictures are clear where the byte is 0. Every picture is in the
battlescape palette (the only one the VGA prototype sets): pictures drawn
in other palettes take the nearest colour of it.
"""

import argparse
import os
import re
import struct

import yaml

import egacolor
import gametext
import packver
import pedia
import png
import ufodata

SRC = packver.src_dir()      # the engine's headers (XCOMCONV.EXE carries copies)


def _defines(header, prefix):
    out = {}
    with open(os.path.join(SRC, header), encoding="ascii") as f:
        for m in re.finditer(r"#define\s+(%s\w+)\s+(0x[0-9A-Fa-f]+|\d+)" % prefix, f.read()):
            out[m.group(1)] = int(m.group(2), 0)
    return out


UI = _defines("uipak.h", "U")       # UI_*, UIF_*
PL = _defines("uipak.h", "PL_")
RT = {**_defines("uipak.h", "RT_"), **_defines("uipak.h", "MSG_")}   # report and message texts


def _item_enum():
    """IT_* names by their value in src/items.h: list index = the C value,
    N_ITEMS entries; None for values without a name of their own (the live
    aliens IT_LIVE + 1 ..)."""
    with open(os.path.join(SRC, "items.h"), encoding="ascii") as f:
        text = f.read()
    body = text[text.index("enum {") + 6:text.index("N_ITEMS")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    names = []
    for part in body.split(","):
        name, _, expr = (s.strip() for s in part.partition("="))
        if not name:
            continue
        v = len(names)
        if expr:
            m = re.fullmatch(r"(\w+)\s*\+\s*(\d+)", expr)
            if not m:
                raise ValueError("items.h: cannot evaluate %s = %s" % (name, expr))
            v = names.index(m.group(1)) + int(m.group(2))
            if v < len(names):
                raise ValueError("items.h: %s repeats a value" % name)
        names += [None] * (v - len(names)) + [name]
    return names


# Engine items -> items.rul types (for BIGOBS.PCK pictures and sizes).
ITEM_STR = {
    "IT_PISTOL": "STR_PISTOL", "IT_RIFLE": "STR_RIFLE", "IT_HCANNON": "STR_HEAVY_CANNON",
    "IT_LPISTOL": "STR_LASER_PISTOL", "IT_LRIFLE": "STR_LASER_RIFLE",
    "IT_HLASER": "STR_HEAVY_LASER", "IT_PPISTOL": "STR_PLASMA_PISTOL",
    "IT_PRIFLE": "STR_PLASMA_RIFLE", "IT_HPLASMA": "STR_HEAVY_PLASMA",
    "IT_CLIP_PISTOL": "STR_PISTOL_CLIP", "IT_CLIP_RIFLE": "STR_RIFLE_CLIP",
    "IT_CLIP_CANNON": "STR_HC_AP_AMMO", "IT_CLIP_PPISTOL": "STR_PLASMA_PISTOL_CLIP",
    "IT_CLIP_PRIFLE": "STR_PLASMA_RIFLE_CLIP", "IT_CLIP_HPLASMA": "STR_HEAVY_PLASMA_CLIP",
    "IT_GRENADE": "STR_GRENADE", "IT_AGRENADE": "STR_ALIEN_GRENADE",
    "IT_HE": "STR_HIGH_EXPLOSIVE", "IT_ELERIUM": "STR_ELERIUM_115",
    "IT_MEDIKIT": "STR_MEDI_KIT", "IT_STUNROD": "STR_STUN_ROD",
    "IT_LAUNCHER": "STR_SMALL_LAUNCHER", "IT_STUNBOMB": "STR_STUN_BOMB",
    "IT_SCANNER": "STR_MOTION_SCANNER", "IT_ACANNON": "STR_AUTO_CANNON",
    "IT_CLIP_ACANNON": "STR_AC_AP_AMMO", "IT_RLAUNCHER": "STR_ROCKET_LAUNCHER",
    "IT_ROCKET": "STR_SMALL_ROCKET", "IT_BLASTER": "STR_BLASTER_LAUNCHER",
    "IT_BLASTERBOMB": "STR_BLASTER_BOMB", "IT_PSIAMP": "STR_PSI_AMP",
    "IT_MINDPROBE": "STR_MIND_PROBE",
    "IT_CORPSE_SECTOID": "STR_SECTOID_CORPSE", "IT_CORPSE_FLOATER": "STR_FLOATER_CORPSE",
    "IT_CORPSE_SNAKEMAN": "STR_SNAKEMAN_CORPSE", "IT_CORPSE_MUTON": "STR_MUTON_CORPSE",
    "IT_CORPSE_ETHEREAL": "STR_ETHEREAL_CORPSE",
    "IT_CLIP_CANNON_HE": "STR_HC_HE_AMMO", "IT_CLIP_CANNON_IN": "STR_HC_I_AMMO",
    "IT_CLIP_ACANNON_HE": "STR_AC_HE_AMMO", "IT_CLIP_ACANNON_IN": "STR_AC_I_AMMO",
    "IT_ROCKET_LARGE": "STR_LARGE_ROCKET", "IT_ROCKET_IN": "STR_INCENDIARY_ROCKET",
    "IT_SMOKE": "STR_SMOKE_GRENADE", "IT_PROXIMITY": "STR_PROXIMITY_GRENADE",
    "IT_FLARE": "STR_ELECTRO_FLARE",
    "IT_TANK_CANNON": "STR_TANK_CANNON", "IT_TANK_ROCKET": "STR_TANK_ROCKET_LAUNCHER",
    "IT_TANK_LASER": "STR_TANK_LASER_CANNON", "IT_HOVER_PLASMA": "STR_HOVERTANK_PLASMA",
    "IT_HOVER_LAUNCHER": "STR_HOVERTANK_LAUNCHER",
}

# HWPs: their pictures are UI_HWP + n.
HWP_ITEMS = ["IT_TANK_CANNON", "IT_TANK_ROCKET", "IT_TANK_LASER", "IT_HOVER_PLASMA",
             "IT_HOVER_LAUNCHER"]

# Paper dolls (UI_DOLL + n): no armour male/female x 4 looks, personal
# armour male/female x 4, power suit.
DOLLS = (["MAN_0M%d" % i for i in range(4)] + ["MAN_0F%d" % i for i in range(4)]
         + ["MAN_1M%d" % i for i in range(4)] + ["MAN_1F%d" % i for i in range(4)]
         + ["MAN_2", "MAN_3"])

CGA_NAMES = ["0lo", "0hi", "1lo", "1hi"]
BATTLE_CGA = 0          # the battlescape's CGA palette (0 low)

# Backgrounds: entry, file, palette (ufodata index), back palette block,
# CGA palette.
BACKGROUNDS = [
    ("UI_BG_MENU", "GEOGRAPH/BACK01.SCR", ufodata.PAL_GEOSCAPE, 0, 0),
    ("UI_BG_PSI", "GEOGRAPH/BACK01.SCR", ufodata.PAL_BASESCAPE, 7, 2),
    ("UI_BG_RESEARCH", "GEOGRAPH/BACK05.SCR", ufodata.PAL_BASESCAPE, 1, 0),
    ("UI_BG_MANUF", "GEOGRAPH/BACK17.SCR", ufodata.PAL_BASESCAPE, 6, 0),
    ("UI_BG_STORES", "GEOGRAPH/BACK13.SCR", ufodata.PAL_BASESCAPE, 0, 2),
    ("UI_BG_REPORT", "GEOGRAPH/BACK13.SCR", ufodata.PAL_GEOSCAPE, 3, 0),
    ("UI_BG_EQUIP", "GEOGRAPH/BACK04.SCR", ufodata.PAL_BASESCAPE, 2, 0),
    ("UI_BG_HANGAR", "GEOGRAPH/BACK14.SCR", ufodata.PAL_BASESCAPE, 3, 0),
    ("UI_BG_SOLDIERS", "GEOGRAPH/BACK02.SCR", ufodata.PAL_BASESCAPE, 2, 0),
    ("UI_BG_PEDIA_ITEM", "GEOGRAPH/BACK08.SCR", 4, None, 2),
    ("UI_BG_PEDIA_TEXT", "GEOGRAPH/BACK10.SCR", ufodata.PAL_UFOPAEDIA, None, 2),
    ("UI_BG_PEDIA_UFO", "GEOGRAPH/BACK11.SCR", ufodata.PAL_GEOSCAPE, None, 2),
    ("UI_BG_BRIEFING", "GEOGRAPH/BACK16.SCR", ufodata.PAL_GEOSCAPE, 0, 0),
    ("UI_BG_ABORT", "UFOGRAPH/TAC00.SCR", ufodata.PAL_BATTLESCAPE, None, BATTLE_CGA),
    ("UI_BG_PEDIA_FACIL", "GEOGRAPH/BACK09.SCR", ufodata.PAL_BASESCAPE, None, 2),
    ("UI_BG_FACIL", "GEOGRAPH/BACK05.SCR", ufodata.PAL_BASESCAPE, 6, 2),
    ("UI_BG_PLACE", "GEOGRAPH/BACK01.SCR", ufodata.PAL_BASESCAPE, 6, 2),
    ("UI_BG_BASEINFO", "GEOGRAPH/BACK07.SCR", ufodata.PAL_BASESCAPE, None, 2),
    ("UI_BG_SOLDIER_INFO", "GEOGRAPH/BACK06.SCR", ufodata.PAL_BASESCAPE, None, 2),
    # Geoscape popups (interfaces.rul: intercept / geoCraft / confirmDestination
    # aqua, UFOInfo / multipleTargets dark blue); CGA palette 0 like the map.
    ("UI_BG_INTERCEPT", "GEOGRAPH/BACK12.SCR", ufodata.PAL_GEOSCAPE, 4, 0),
    ("UI_BG_UFOINFO", "GEOGRAPH/BACK15.SCR", ufodata.PAL_GEOSCAPE, 7, 0),
    # MissionDetectedState: the terror deployment's alert background
    # (BACK03.SCR), interface terrorSite (palette 3, pink).
    ("UI_BG_TERROR", "GEOGRAPH/BACK03.SCR", ufodata.PAL_GEOSCAPE, 3, 0),
    # Briefings (alienDeployments.rul briefing: background, palette).
    ("UI_BG_BRIEF_TERROR", "GEOGRAPH/BACK16.SCR", ufodata.PAL_GEOSCAPE, 2, 0),
    ("UI_BG_BRIEF_MARS", "GEOGRAPH/BACK16.SCR", ufodata.PAL_GEOSCAPE, 6, 0),
    ("UI_BG_BRIEF_FINAL", "GEOGRAPH/BACK01.SCR", ufodata.PAL_GEOSCAPE, 6, 0),
]

# Mission briefings by BRIEF_* (src/uipak.h): deployment, background entry,
# text offset (alienDeployments.rul), title and text strings (gametext).
BRIEFINGS = [
    ("UI_BG_BRIEFING", 0, "STR_UFO_CRASH_RECOVERY"),
    ("UI_BG_BRIEFING", 0, "STR_UFO_GROUND_ASSAULT"),
    ("UI_BG_BRIEF_TERROR", 0, "STR_TERROR_MISSION"),
    ("UI_BG_MENU", -16, "STR_ALIEN_BASE_ASSAULT"),
    ("UI_BG_MENU", 0, "STR_BASE_DEFENSE"),
    ("UI_BG_BRIEF_MARS", -16, "STR_MARS_CYDONIA_LANDING"),
    ("UI_BG_BRIEF_FINAL", -16, "STR_MARS_THE_FINAL_ASSAULT"),
]

# Craft weapons in src/craft.h CW_ order -> INTICON.PCK weapon sprite
# (craftWeapons.rul sprite; the icon is frame sprite + 5), craft types in
# CT_ order -> crafts.rul sprite (the damage picture is frame sprite + 11).
CW_SPRITES = [2, 0, 1, 4, 5, 3]
CT_SPRITES = [3, 4, 1, 2]
DF_BUTTONS = [(83, 4, 36, 15), (120, 4, 36, 15), (83, 20, 36, 15), (120, 20, 36, 15),
              (120, 36, 36, 15)]       # standoff, cautious, standard, aggressive, disengage
DF_X, DF_Y = 80, 52                     # the window on the screen
RESERVE_BUTTONS = [(49, 177, 28, 11, 70), (78, 177, 28, 11, 38), (49, 189, 28, 11, 38),
                   (78, 189, 28, 11, 38)]   # x, y, w, h, middle of the colour ramp


class Pal(list):
    """A palette (256 (r, g, b)) that knows which DOS palette it is (pi,
    ufodata.PAL_*) and its window ramp at 224-239 (back, a BACKPALS.DAT
    block, or None): the VGA pack tags plain pictures with them."""
    pi, back, style = 0, None, 0


def palette(root, index, back=None):
    pal = Pal(ufodata.load_palette(root, index))
    pal.pi, pal.back = index, back
    if back is not None:
        bp = ufodata.read(root, "GEODATA/BACKPALS.DAT")
        for k in range(16):
            o = (back * 16 + k) * 3
            pal[224 + k] = tuple(min(255, c * 4) for c in bp[o:o + 3])
    return pal


def battle_palette(root):
    """PAL_BATTLESCAPE with OpenXcom's grey ramp at 240-255 (Mod.cpp)."""
    pal = Pal(ufodata.load_palette(root, ufodata.PAL_BATTLESCAPE))
    pal.pi = ufodata.PAL_BATTLESCAPE
    grey = [(140, 152, 148), (132, 136, 140), (116, 124, 132), (108, 116, 124),
            (92, 104, 108), (84, 92, 100), (76, 80, 92), (56, 68, 84), (48, 56, 68),
            (40, 48, 56), (32, 36, 48), (24, 28, 32), (16, 20, 24), (8, 12, 16),
            (3, 4, 8), (3, 3, 6)]
    for i, c in enumerate(grey):
        pal[240 + i] = c
    return pal


def load_spk(data, w=320, h=200):
    px = [0] * (w * h)
    pos = i = 0
    while i + 1 < len(data):
        flag = struct.unpack_from("<H", data, i)[0]
        i += 2
        if flag == 0xFFFF:
            pos += struct.unpack_from("<H", data, i)[0] * 2
            i += 2
        elif flag == 0xFFFE:
            n = struct.unpack_from("<H", data, i)[0] * 2
            i += 2
            for k in range(n):
                if pos < len(px):
                    px[pos] = data[i + k]
                pos += 1
            i += n
        else:
            break
    return [px[y * w:(y + 1) * w] for y in range(h)]


# ----------------------------------------------------------------------
# Encoding


class Device:
    def __init__(self, cga, vga=False):
        self.cga = cga
        self.vga = vga
        self.align = 1 if vga else 4 if cga else 8
        self.maps = {}
        self.near = {}
        self.bpal = None            # VGA: the battlescape palette

    def to_battle(self, pal):
        """VGA: palette indices of pal -> the battlescape palette's nearest
        (1-255: 0 stays clear)."""
        if pal[:240] == self.bpal[:240]:    # PAL_BATTLESCAPE as in the file: the same to 239
            return list(range(256))
        key = id(pal)
        if key not in self.near:
            bp = self.bpal
            self.near[key] = [0] + [min(range(1, 256), key=lambda j: egacolor._dist(pal[i], bp[j]))
                                    for i in range(1, 256)]
        return self.near[key]

    def mapping(self, pal, cga_pal, pen=0.35):
        key = (id(pal), cga_pal, pen)
        if key not in self.maps:
            target = egacolor.CGA_PALETTES[CGA_NAMES[cga_pal]] if self.cga else None
            self.maps[key] = (pal, egacolor.build_mapping(pal, noise_penalty=pen, target=target))
        return self.maps[key][1]


# CGA battle panel (ICONS.PCK, rank insignia, reserve buttons): the
# bevelled, engraved buttons in two solid colours each, face and symbol
# (palette 0 low: 0 black, 1 green, 2 red, 3 brown), no bevel shading.
# (palette indices, face colour, symbol indices); symbols are black.
PANEL_CGA = (
    (range(80, 96), 3, range(91, 96)),      # grey buttons: dark symbols
    (range(33, 41), 2, (33,)),              # red reserve buttons: light symbols
    (range(67, 74), 1, (67,)),              # green reserve button
)


def panel_cga(pressed=False):
    """Mapping overrides (index -> (a, b, level)) for the CGA panel;
    pressed: a black button with the symbol in the face colour."""
    out = {}
    for indices, face, symbols in PANEL_CGA:
        for i in indices:
            c = 0 if (i in symbols) != pressed else face
            out[i] = (c, c, 0)
    return out


def flat_buttons(rows):
    """ICONS.PCK rows 144-199 with the 14 grey buttons' engraved inset edge
    (the dark curve left of and the line above each symbol; the layers
    button keeps its symbol's top) turned to the face colour, for the
    two-colour CGA panel."""
    rows = [list(r) for r in rows]
    for y in range(32):
        by = y % 16
        for x in range(48, 272):
            bx = (x - 48) % 32
            top = by == 3 and 3 <= bx < (10 if (x - 48) // 32 == 2 and y < 16 else 20)
            if rows[y][x] >= 88 and (top or (3 <= bx <= 6 and 3 <= by <= 11)):
                rows[y][x] = 86
    return rows


def picture(dev, rows, pal, x, y, masked=False, cga_pal=BATTLE_CGA, screen=False, pen=None,
            override=None):
    """rows: palette indices (0 = clear when masked) placed at screen (x, y).
    Returns the entry bytes; x is aligned down and the width up.
    override: palette index -> (a, b, level) replacing the mapping's."""
    h = len(rows)
    w = len(rows[0]) if h else 0
    a = dev.align
    x0 = x - x % a
    pad = x - x0
    w2 = -(-(w + pad) // a) * a
    grid = [[0] * pad + list(r) + [0] * (w2 - w - pad) for r in rows]
    opaque = [[1 if not masked else (1 if c else 0) for c in r] for r in rows]
    opaque = [[0] * pad + r + [0] * (w2 - w - pad) for r in opaque] if masked else None
    if dev.vga:
        # Every picture in its own DOS palette's indices; plain ones carry
        # the palette in the head's spare byte (vga_tag), which the VGA
        # code sets when it draws them (src/vgageo.asm).
        flags = (UI["UIF_MASKED"] if masked else 0) | (UI["UIF_SCREEN"] if screen else 0)
        out = bytearray(struct.pack("<HHHHBB", x0, y, w2, h, flags,
                                    0 if masked else vga_tag(pal)))
        near = list(range(256)) if isinstance(pal, Pal) else dev.to_battle(pal)
        for ry in range(h):
            out += bytes(near[c] for c in grid[ry])
        return bytes(out)
    # Battle pictures (pen None): flat colours keep the small icons sharp.
    mp = dev.mapping(pal, cga_pal, 1.0 if pen is None else pen)
    if override:
        mp = list(mp)
        for i, mix in override.items():
            mp[i] = mix
    flags = (UI["UIF_MASKED"] if masked else 0) | (cga_pal << 1)
    if screen:
        flags |= UI["UIF_SCREEN"]
    out = bytearray(struct.pack("<HHHHBB", x0, y, w2, h, flags, 0))

    def colour(px, py, c):
        return egacolor.pixel_colour(mp, c, px, py)

    if not dev.cga:
        nb = w2 // 8
        if not masked:
            for p in range(4):
                for ry in range(h):
                    for b in range(nb):
                        v = 0
                        for bit in range(8):
                            cx = b * 8 + bit
                            if colour(x0 + cx, y + ry, grid[ry][cx]) & (1 << p):
                                v |= 0x80 >> bit
                        out.append(v)
        else:
            for ry in range(h):
                for b in range(nb):
                    m = 0
                    pl = [0, 0, 0, 0]
                    for bit in range(8):
                        cx = b * 8 + bit
                        if not opaque[ry][cx]:
                            continue
                        m |= 0x80 >> bit
                        c = colour(x0 + cx, y + ry, grid[ry][cx])
                        for p in range(4):
                            if c & (1 << p):
                                pl[p] |= 0x80 >> bit
                    out += bytes([m] + pl)
    else:
        nb = w2 // 4
        for ry in range(h):
            for b in range(nb):
                m = d = 0
                for k in range(4):
                    cx = b * 4 + k
                    sh = 6 - 2 * k
                    if masked and not opaque[ry][cx]:
                        continue
                    m |= 3 << sh
                    d |= colour(x0 + cx, y + ry, grid[ry][cx]) << sh
                if masked:
                    out += bytes((m, d))
                else:
                    out.append(d)
    return bytes(out)


def vga_tag(pal):
    """The head's spare byte of a plain VGA picture: bits 0-2 the DOS
    palette + 1 (ufodata.PAL_*), bits 3-6 its window ramp + 1 (0: the
    ramp does not matter); 0 for a palette without a tag."""
    if not isinstance(pal, Pal):
        return 0
    return ((pal.pi + 1) | ((pal.back + 1) << 3 if pal.back is not None else 0)
            | pal.style << 7)


# The EGA themes (src/uik.c ega_themes): frame edge, middle, centre; title,
# text, hilite, sel, dim; button light edge, light, fill, dark, dark edge;
# button text, list.
EGA_THEMES = [
    [2, 2, 10, 14, 10, 15, 2, 2, 10, 10, 2, 8, 0, 10, 14],      # green
    [1, 1, 9, 11, 9, 15, 1, 1, 9, 9, 1, 8, 0, 11, 13],          # blue
    [8, 8, 7, 15, 7, 14, 8, 8, 7, 7, 8, 0, 0, 15, 7],           # grey
    [5, 5, 13, 13, 11, 15, 5, 5, 13, 13, 5, 8, 0, 15, 11],      # purple
    [14, 6, 14, 15, 14, 15, 6, 6, 14, 14, 6, 0, 0, 14, 14],     # gold
]
# DOS's interface colours for the VGA themes, from OpenXcom's
# interfaces.rul and Ufopaedia articles: (palette, style) -> per theme
# (window, button, title, text, list, button text) as OpenXcom's colour
# values c (Window: frames c+3, c+2, c+1 from outside; TextButton: c+1,
# c+2, fill c+3, c+4, c+5; Text: body c+1, shadow c+4), None: the EGA
# theme in the palette's nearest colours. Style 1: DOS's menus
# (BACK01.SCR in the geoscape palette, mainMenu: window and buttons 133);
# style 0 the rest (geoscape windows 239, intercept, reports).
VGA_THEMES = {
    (ufodata.PAL_GEOSCAPE, 0): [(239, 133, 138, 239, 239, 133), (246, 246, 244, 244, 244, 244),
                                None, None, None],
    (ufodata.PAL_GEOSCAPE, 1): [(133, 133, 138, 138, 133, 133), (246, 246, 244, 244, 244, 244),
                                None, None, None],
    # buy/sell/stores 218; soldiers 241/218; research/psi 246; basescape 213
    (ufodata.PAL_BASESCAPE, 0): [(241, 218, 218, 241, 218, 218), (218, 218, 218, 218, 218, 218),
                                 None, (246, 246, 241, 241, 246, 246),
                                 (213, 213, 241, 246, 218, 213)],
    # ArticleStateText: buttons 80, title 244, text 239
    (ufodata.PAL_UFOPAEDIA, 0): [(239, 80, 244, 239, 239, 80), None, None, None, None],
    # ArticleStateCraft (the crafts' UP pictures, style 1): buttons, title, text 239
    (ufodata.PAL_UFOPAEDIA, 1): [(239, 239, 239, 239, 239, 239), None, None, None, None],
    # GraphsState: buttons 151 (greys, text 152 on 154)
    (ufodata.PAL_GRAPHS, 0): [None, None, (151, 151, 135, 135, 135, 151), None, None],
    # ArticleStateItem (PAL_BATTLEPEDIA): buttons 144 (gold), title and text
    # 239 (light blue there; OpenXcom's greys in the battlescape's 240-255,
    # so the nearest of the rest). Outside the battle only (vgxasm.asm).
    (ufodata.PAL_BATTLESCAPE, 0): [(144, 144, 239, 239, 239, 144), None, None, None, None],
}
# Single EGA colours a palette's screens name, to DOS's (all its themes):
# the graphs' title (12: interfaces.rul graphs text 135, light red), scale
# and months (10: scale 103, green) and grid (2: graph 160, dark green).
VGA_EXTRA = {
    ufodata.PAL_GRAPHS: {12: 136, 4: 139, 10: 104, 2: 160},
}
VGA_THEME_REC = 15 + 16         # a theme and its VMAP


def vga_themes(pal, pi, style, vmap, raw=None):
    """The 5 VGA themes of a palette and style (src/vgxasm.asm): each the
    UkTheme (frames, the selection bar and buttons as palette indices, 16
    and up; text as EGA colours 1-14) and the VMAP that goes with it: the
    palette's nearest colours, the theme's text colours (and their shadows,
    8 below) moved to DOS's. Colours 0, 12 (red) and 15 (the pointer's)
    keep the nearest."""
    out = bytearray()
    dos = VGA_THEMES.get((pi, style)) or VGA_THEMES.get((pi, 0)) or [None] * 5

    def dist(a, b):
        return sum((a[k] - b[k]) ** 2 for k in range(3))

    def ours(i):        # DOS's colour i in this palette (raw: DOS's own palette)
        if raw is None or raw[i] == pal[i]:
            return i
        return min((k for k in range(1, 240) if raw[k] == pal[k]), key=lambda k: dist(pal[k], raw[i]))

    for t in range(5):
        ega, m = EGA_THEMES[t], list(vmap)
        for k, v in VGA_EXTRA.get(pi, {}).items():
            m[k] = v
        if dos[t] is None:
            out += bytes(ega) + bytes(m)
            continue
        win, btn, title, text, lst, btext = dos[t]
        th = [win + 3, win + 2, win + 1, 0, 0, 15, btn + 3, 0] + [btn + k for k in range(1, 6)] + [0, 0]
        used = {}                               # VMAP slots given a colour
        for field, c in ((3, title), (4, text), (13, btext), (14, lst)):
            b, sh = ours(c + 1), ours(c + 4)
            pool = [ega[field]] + sorted((9, 10, 11, 13, 14),
                                         key=lambda s: dist(egacolor.EGA_RGB[s], pal[b]))
            for s in pool:
                if s < 9 or s in (12, 15) or used.get(s, b) != b or used.get(s - 8, sh) != sh:
                    continue
                used[s], used[s - 8] = b, sh
                m[s], m[s - 8] = b, sh
                th[field] = s
                break
            else:
                raise ValueError("VGA theme %d of palette %d: no colour for %d" % (t, pi, c))
        th[7] = th[4] - 8                       # dim: the text's shadow
        out += bytes(th) + bytes(m)
    return bytes(out)


def vga_palettes(root):
    """PALV.PAK, the palettes the VGA code switches between (src/vgageo.asm):
    "XVP1", u16 5, u16 8; then for each DOS palette (ufodata.PAL_*, the
    battlescape's with OpenXcom's greys at 240-255) 768 bytes of DAC
    values (6 bits) and 16 bytes VMAP (the colours nearest the 16 EGA
    colours the screens name; not in the window ramp 224-239 outside the
    battlescape); then BACKPALS.DAT's 8 ramps of 16 colours (48 bytes);
    then for palettes 0-4, styles 0 and 1, the 5 VGA themes (vga_themes)."""
    out = bytearray(b"XVP1" + struct.pack("<HH", 5, 8))
    for pi in range(5):
        pal = battle_palette(root) if pi == ufodata.PAL_BATTLESCAPE else palette(root, pi)
        out += bytes(c >> 2 for rgb in pal for c in rgb)
        cand = [i for i in range(256) if pi == ufodata.PAL_BATTLESCAPE or not 224 <= i < 240]
        for e in egacolor.EGA_RGB[:16]:
            out.append(min(cand, key=lambda i: sum((pal[i][k] - e[k]) ** 2 for k in range(3))))
    out += ufodata.read(root, "GEODATA/BACKPALS.DAT")[:384]
    for pi in range(5):         # the themes (vga_themes)
        battle = pi == ufodata.PAL_BATTLESCAPE
        pal = battle_palette(root) if battle else palette(root, pi)
        vmap = out[8 + pi * 784 + 768:8 + pi * 784 + 784]
        for style in range(2):
            out += vga_themes(pal, pi, style, vmap,
                              ufodata.load_palette(root, pi) if battle else None)
    return bytes(out)


def font(data, size):
    """SMALLSET.DAT (8x9) or BIGLETS.DAT (16x16): glyphs from '!' on.
    Pixel values 1-3 are the body, 4-5 the shadow. The small font's value 2 is
    the lighter pixel of the original's two-tone letters (the corners of R, P,
    B, which tell it from A): body only for 1, so R is not an A, and drawn in
    the shadow colour."""
    gw, gh = size
    bpr = gw // 8
    per = gw * gh
    widths = []
    glyphs = bytearray()
    for ch in range(32, 128):
        g = ch - 33
        body = [[0] * gw for _ in range(gh)]
        shadow = [[0] * gw for _ in range(gh)]
        if 0 <= g < len(data) // per:
            for yy in range(gh):
                for xx in range(gw):
                    v = data[g * per + yy * gw + xx]
                    if v == 1 or (v in (2, 3) and gw > 8):
                        body[yy][xx] = 1
                    elif v >= 2:
                        shadow[yy][xx] = 1
        cols = [xx for xx in range(gw) if any(body[yy][xx] or shadow[yy][xx] for yy in range(gh))]
        if ch == 32 or not cols:
            widths.append(4 if gw == 8 else 8)
        else:
            widths.append(cols[-1] + 1)
        for plane in (body, shadow):
            for yy in range(gh):
                for b in range(bpr):
                    v = 0
                    for bit in range(8):
                        if plane[yy][b * 8 + bit]:
                            v |= 0x80 >> bit
                    glyphs.append(v)
    return bytes([gh, bpr] + widths) + bytes(glyphs)


def crop_box(rows):
    """Bounding box (x, y, w, h) of the non-zero pixels."""
    ys = [y for y, r in enumerate(rows) if any(r)]
    if not ys:
        return 0, 0, 8, 1
    xs = [x for r in rows for x, c in enumerate(r) if c]
    return min(xs), ys[0], max(xs) - min(xs) + 1, ys[-1] - ys[0] + 1


def sub(rows, x, y, w, h):
    return [r[x:x + w] for r in rows[y:y + h]]


def shrink(rows):
    """Halves a picture: each 2x2 block takes its most common non-zero
    colour (0 only when the block is empty)."""
    out = []
    for y in range(0, len(rows) - 1, 2):
        line = []
        for x in range(0, len(rows[0]) - 1, 2):
            px = [rows[y][x], rows[y][x + 1], rows[y + 1][x], rows[y + 1][x + 1]]
            nz = [c for c in px if c]
            line.append(max(set(nz), key=nz.count) if nz else 0)
        out.append(line)
    return out


def pedia_table(rules_dir, items):
    """16 bytes per UFOPAED.PAK entry: layout, pictures, text and stats
    boxes (OpenXcom's ArticleState* layouts)."""
    with open(os.path.join(rules_dir, "ufopaedia.rul"), encoding="utf-8") as f:
        arts = {a["id"]: a for a in yaml.safe_load(f)["ufopaedia"]}
    with open(os.path.join(rules_dir, "facilities.rul"), encoding="utf-8") as f:
        facils = {a["type"]: a for a in yaml.safe_load(f)["facilities"]}
    with open(os.path.join(rules_dir, "ufos.rul"), encoding="utf-8") as f:
        ufos = {a["type"]: a for a in yaml.safe_load(f)["ufos"]}
    str_item = {v: k for k, v in ITEM_STR.items()}
    out = bytearray()
    for e in pedia.ENTRIES:
        art = arts.get(e[0], {})
        t = art.get("type_id")
        img = art.get("image_id") or ""
        m = re.match(r"UP0*(\d+)\.SPK", img)
        layout, pic, box, stats = PL["PL_TEXT"], 0xFFFF, (10, 56, 296), (0, 0)
        pic2, size, sw = 0xFFFF, 0, 0
        if m and t in (1, 2, 7):
            layout = PL["PL_PICTURE"]
            pic = UI["UI_UP"] + int(m.group(1)) - 1
            if t == 1:
                rt = art.get("rect_text") or {}
                rs = art.get("rect_stats") or {}
                box = (rt.get("x", 5), rt.get("y", 40) + 16, rt.get("width", 140))
                stats = (rs.get("x", 160) // 2, rs.get("y", 5) + (16 if rs.get("x", 160) < 150 else 2))
                sw = rs.get("width", 160)
            elif t == 2:
                box = (5, 160, 310)
                stats = (5 // 2, 80)        # ArticleStateCraftWeapon: list at 5, 80
                sw = 250
            else:
                box = (5, 56, art.get("text_width", 150))
        elif t in (4, 5) and e[0] in str_item:
            layout = PL["PL_ITEM"]
            pic = UI["UI_BIGOB"] + items.index(str_item[e[0]])
            box = (8, 82, 300)
        elif t == 3 and e[0] in str_item:
            # ArticleStateVehicle: here the HWP's picture as an item page
            layout = PL["PL_ITEM"]
            pic = UI["UI_HWP"] + HWP_ITEMS.index(str_item[e[0]])
            box = (8, 82, 300)
        elif t == 9:
            # ArticleStateUfo: the UFO (160 x 52) at 160, 6, stats at 10, 68,
            # text at 10, 140.
            layout = PL["PL_UFO"]
            pic = UI["UI_UFO_MID"] + ufos.get(e[0], {}).get("sprite", 0)
            box = (10, 140, 300)
            stats = (10 // 2, 68)
            sw = 310
        elif t == 6 and e[0] in facils:
            # ArticleStateBaseFacility: BACK09, the facility at 232, 16 (64x64),
            # stats at 10, 42, text at 10, 104.
            f = facils[e[0]]
            layout = PL["PL_FACIL"]
            pic = UI["UI_BASEBITS"] + f.get("spriteShape", 0)
            pic2 = UI["UI_BASEBITS"] + f.get("spriteFacility", 0)
            size = f.get("size", 1)
            box = (10, 104, 300)
            stats = (10 // 2, 42)
            sw = 200
        out += struct.pack("<BBHHHHHHBB", layout, stats[0], pic, box[0], box[1], box[2], stats[1],
                           pic2, size, sw // 2)
    return bytes(out)


def briefing_table(root):
    """UI_BRIEFING: u16 offsets (one more: the end), then per mission u16
    background, s8 text offset, title NUL, text NUL (the game's texts, ASCII)."""
    lang = gametext.lang(root)
    recs = []
    for bg, off, sid in BRIEFINGS:
        title = pedia.ascii_text(lang.get(sid, sid))
        text = pedia.ascii_text(lang.get(sid + "_BRIEFING", ""))
        recs.append(struct.pack("<Hb", UI[bg], off) + title.encode("ascii") + b"\0"
                    + text.encode("ascii") + b"\0")
    out = bytearray()
    pos = 2 * len(recs) + 2
    for r in recs:
        out += struct.pack("<H", pos)
        pos += len(r)
    out += struct.pack("<H", pos)
    for r in recs:
        out += r
    return bytes(out)


# Month report texts by RT_* (src/uipak.h).
REPORT_TEXTS = [
    "STR_XCOM_PROJECT_MONTHLY_REPORT", "STR_COUNCIL_IS_VERY_PLEASED",
    "STR_COUNCIL_IS_GENERALLY_SATISFIED", "STR_COUNCIL_IS_DISSATISFIED",
    "STR_YOU_HAVE_NOT_SUCCEEDED", "STR_COUNCIL_REDUCE_DEBTS",
    "STR_COUNTRY_IS_PARTICULARLY_PLEASED", "STR_COUNTRIES_ARE_PARTICULARLY_HAPPY",
    "STR_COUNTRY_IS_UNHAPPY_WITH_YOUR_ABILITY", "STR_COUNTRIES_ARE_UNHAPPY_WITH_YOUR_ABILITY",
    "STR_COUNTRY_HAS_SIGNED_A_SECRET_PACT", "STR_COUNTRIES_HAVE_SIGNED_A_SECRET_PACT",
]


def report_table(root):
    """UI_REPORT: u16 offsets (one more: the end), then the texts, NUL each
    (ASCII, one space between words). The country texts lose their {0} (the
    names are put in front by the engine); the heading is spelt X-COM."""
    if len(REPORT_TEXTS) != RT["RT_N"]:
        raise ValueError("uipak.h RT_N and ui.py REPORT_TEXTS differ")
    lang = gametext.lang(root)
    texts = []
    for sid in REPORT_TEXTS:
        t = lang.get(sid)
        if t is None:
            raise ValueError("no text for %s" % sid)
        t = pedia.ascii_text(t.replace("{0}", ""))
        if sid == "STR_XCOM_PROJECT_MONTHLY_REPORT":
            t = re.sub(r"\bXCOM\b", "X-COM", t)
        texts.append(t.encode("ascii") + b"\0")
    out = bytearray()
    pos = 2 * len(texts) + 2
    for t in texts:
        out += struct.pack("<H", pos)
        pos += len(t)
    out += struct.pack("<H", pos)
    for t in texts:
        out += t
    return bytes(out)


# Message texts by MSG_* (src/uipak.h): string id, the number of words in
# each line where the text is shown on several lines (the game's own text
# has no line breaks there, or the {NEWLINE}s are kept when it has).
MSG_TEXTS = [
    ("STR_CANNOT_DISMANTLE_FACILITY", None), ("STR_CANNOT_BUILD_HERE", None),
    ("STR_ARE_YOU_SURE_CYDONIA", (5, 6, 3)), ("STR_IS_LOW_ON_FUEL_RETURNING_TO_BASE", None),
    ("STR_CRAFT_READY_TO_LAND_NEAR_DESTINATION", None),
    ("STR_XCOM_AGENTS_HAVE_LOCATED_AN_ALIEN_BASE_IN_REGION", (3, 5)),
    ("STR_NOT_ENOUGH_EQUIPMENT_TO_FULLY_RE_EQUIP_SQUAD", (3, 4)),
    ("STR_CONTAINMENT_FULL_XT", None),
]


NL = "\n"


def msg_text(t, breaks):
    """One message: ASCII, lines kept (LF), the placeholders of OpenXcom's
    language file left out, the break between lines after `breaks` words."""
    for a in ("{NEWLINE}", "{ALT}{NEWLINE}"):
        t = t.replace(a, NL)
    t = t.replace("{SMALLLINE}", " ").replace("{ALT}", "").replace("{0}", "").replace("{1}", "")
    t = t.replace(chr(13), "")
    lines = [pedia.ascii_text(l) for l in t.split(NL)]
    lines = [l for l in lines if l]
    if breaks:
        words = " ".join(lines).split(" ")
        if sum(breaks) != len(words):
            raise ValueError("msg_text: %d words, breaks %s" % (len(words), breaks))
        lines, i = [], 0
        for n in breaks:
            lines.append(" ".join(words[i:i + n]))
            i += n
    return NL.join(lines).replace("XCom ", "X-Com ")


def msg_table(root):
    """UI_MSG: u16 offsets (one more: the end), then the texts, NUL each."""
    if len(MSG_TEXTS) != RT["MSG_N"]:
        raise ValueError("uipak.h MSG_N and ui.py MSG_TEXTS differ")
    lang = gametext.lang(root)
    texts = []
    for sid, breaks in MSG_TEXTS:
        t = lang.get(sid)
        if t is None:
            raise ValueError("no text for %s" % sid)
        texts.append(msg_text(t, breaks).encode("ascii") + b"\0")
    out = bytearray()
    pos = 2 * len(texts) + 2
    for t in texts:
        out += struct.pack("<H", pos)
        pos += len(t)
    out += struct.pack("<H", pos)
    for t in texts:
        out += t
    return bytes(out)


def invert(rows, mid):
    """OpenXcom's Surface::invert: colours mirrored around mid (pressed
    buttons)."""
    return [[c + 2 * (mid - c) if c else 0 for c in r] for r in rows]


def build(root, rules_dir, cga, vga=False, no_pedia=False):
    dev = Device(cga, vga)
    # VGA: the DOS game's battlescape palette, as the mission packs' "XPL1".
    dev.bpal = battle_palette(root)
    entries = {}
    items = _item_enum()

    entries[UI["UI_FONT_SMALL"]] = font(ufodata.read(root, "GEODATA/SMALLSET.DAT"), (8, 9))
    entries[UI["UI_FONT_BIG"]] = font(ufodata.read(root, "GEODATA/BIGLETS.DAT"), (16, 16))

    for name, path, pi, back, cp in BACKGROUNDS:
        pal = battle_palette(root) if pi == ufodata.PAL_BATTLESCAPE else palette(root, pi, back)
        pal.style = int(name == "UI_BG_MENU")   # (VGA: DOS's menus, VGA_THEMES)
        data = ufodata.read(root, path)
        rows = [list(data[y * 320:(y + 1) * 320]) for y in range(200)]
        entries[UI[name]] = picture(dev, rows, pal, 0, 0, cga_pal=cp, screen=True, pen=0.35)

    geo = palette(root, ufodata.PAL_GEOSCAPE)
    data = ufodata.read(root, "GEOGRAPH/GEOBORD.SCR")
    rows = [list(data[y * 320 + 256:(y + 1) * 320]) for y in range(200)]
    entries[UI["UI_GEOBORD"]] = picture(dev, rows, geo, 256, 0, cga_pal=0)

    bpal = battle_palette(root)
    icons = load_spk(ufodata.read(root, "UFOGRAPH/ICONS.PCK"))
    # CGA: four colours turn the panel's greys flat brown; dithered, the
    # buttons' icons and edges stay visible.
    ppen = 0.35 if cga else None
    # CGA: two solid colours a button (PANEL_CGA), not the stamped greys.
    pover = panel_cga() if cga else None
    entries[UI["UI_ICONS"]] = picture(dev, flat_buttons(icons[144:200]) if cga else icons[144:200],
                                      bpal, 0, 144, pen=ppen, override=pover)

    # Rank insignia, pre-drawn onto the panel at (107, 177).
    smoke = ufodata.load_pck(root, "UFOGRAPH/SMOKE")
    for r in range(6):
        panel = [row[104:136] for row in icons[177:200]]
        fr = smoke[20 + r]
        for yy in range(23):
            for xx in range(26):
                if fr[yy][xx]:
                    panel[yy][xx + 3] = fr[yy][xx]
        entries[UI["UI_RANK"] + r] = picture(dev, panel, bpal, 104, 177, pen=ppen,
                                             override=pover)

    tac01 = load_spk(ufodata.read(root, "UFOGRAPH/TAC01.SCR"))
    x, y, w, h = crop_box(tac01)
    entries[UI["UI_TAC01"]] = picture(dev, sub(tac01, x, y, w, h), bpal, x, y, pen=0.35)
    for i, name in enumerate(DOLLS):
        doll = load_spk(ufodata.read(root, "UFOGRAPH/%s.SPK" % name))
        x, y, w, h = crop_box(doll)
        entries[UI["UI_DOLL"] + i] = picture(dev, sub(doll, x, y, w, h), bpal, x, y)

    with open(os.path.join(rules_dir, "items.rul"), encoding="utf-8") as f:
        rul = {it["type"]: it for it in yaml.safe_load(f)["items"]}
    bigobs = ufodata.load_pck(root, "UNITS/BIGOBS", 32, 48)
    # UI_BIGOB + item and UI_ICON + item for every item (src/uipak.h).
    # The flying suit and what follows it have no pictures (items.h N_PICTURED).
    if "IT_FLYINGSUIT" in items:
        items = items[:items.index("IT_FLYINGSUIT")]
    following = min(v for v in UI.values() if v > UI["UI_ICON"])
    if UI["UI_ICON"] - UI["UI_BIGOB"] < len(items) or following - UI["UI_ICON"] < len(items):
        raise SystemExit("src/uipak.h: UI_BIGOB / UI_ICON need room for %d items" % len(items))
    for idx, name in enumerate(items):
        it = rul.get(ITEM_STR.get(name, ""))
        if not it or it.get("bigSprite") is None:
            continue
        fw, fh = it.get("invWidth", 1), it.get("invHeight", 1)
        fr = sub(bigobs[it["bigSprite"]], 0, 0, fw * 16, fh * 16)
        if name in HWP_ITEMS:
            # HWPs: their own entries (UI_HWP), for the battle panel's hand.
            entries[UI["UI_HWP"] + HWP_ITEMS.index(name)] = picture(
                dev, fr, bpal, (2 - fw) * 8, (3 - fh) * 8, masked=True)
            continue
        entries[UI["UI_BIGOB"] + idx] = picture(dev, fr, bpal, (2 - fw) * 8, (3 - fh) * 8,
                                                masked=True)
        icon = fr
        while len(icon) > 16 or len(icon[0]) > 16:
            icon = shrink(icon)
        icon = [r + [0] * (16 - len(r)) for r in icon]
        icon += [[0] * 16 for _ in range(16 - len(icon))]
        entries[UI["UI_ICON"] + idx] = picture(dev, icon, bpal, 0, 0, masked=True)

    # UI_BODY + body (src/inv.h BODY_*: convert.py's BODY_ARMORS, then the
    # live aliens' types): the hand picture of the armour's corpseBattle
    # item; the 2x2 units' corpses (invWidth 0) cannot be picked up.
    from convert import BODY_ARMORS, LIVE_ALIENS
    import rules as rulesmod
    rs = rulesmod.Rules(rules_dir)
    bodies = BODY_ARMORS + [rs.units[t]["armor"] for t in LIVE_ALIENS]
    if UI["UI_COUNT"] - UI["UI_BODY"] < len(bodies):
        raise SystemExit("src/uipak.h: UI_BODY needs room for %d bodies" % len(bodies))
    for b, armor in enumerate(bodies):
        corpse = rs.armors[armor].get("corpseBattle", [])
        it = rul.get(corpse[0]) if corpse else None
        if not it or it.get("bigSprite") is None or not it.get("invWidth", 1):
            continue
        fw, fh = it.get("invWidth", 1), it.get("invHeight", 1)
        fr = sub(bigobs[it["bigSprite"]], 0, 0, fw * 16, fh * 16)
        entries[UI["UI_BODY"] + b] = picture(dev, fr, bpal, (2 - fw) * 8, (3 - fh) * 8,
                                             masked=True)

    base = palette(root, ufodata.PAL_BASESCAPE)
    # SoldierInfoState: the rank insignia (SMOKE.PCK frames 20-25) at 4, 4.
    for r in range(6):
        entries[UI["UI_SOLDIER_RANK"] + r] = picture(dev, smoke[20 + r], base, 4, 4, masked=True,
                                                     cga_pal=2)
    bits = ufodata.load_pck(root, "GEOGRAPH/BASEBITS")
    for i in range(min(40, len(bits))):
        entries[UI["UI_BASEBITS"] + i] = picture(dev, sub(bits[i], 0, 0, 32, 32), base, 0, 0,
                                                 masked=True, cga_pal=2)
    entries[UI["UI_BASE_EARTH"]] = picture(dev, sub(bits[0], 0, 0, 32, 32), base, 0, 0,
                                           cga_pal=2)
    # CraftInfoState: a row of soldier (38), HWP (40) and equipment (39) figures every 10
    # pixels from x 85. Pictures sit on whole bytes (CGA: 4 pixels), so four shifted copies
    # of each: the n-th figure uses copy n % 4 (placed as picture() aligns it), 40 pixels on.
    for t, f in enumerate((38, 40, 39)):
        for v in range(4):
            entries[UI["UI_FIGURE"] + t * 4 + v] = picture(
                dev, sub(bits[f], 0, 0, 11, 18), base, 85 + 10 * v, 96, masked=True, cga_pal=2)

    up = palette(root, ufodata.PAL_UFOPAEDIA)
    # (VGA) the crafts' pictures in style 1: ArticleStateCraft's colours
    upc = palette(root, ufodata.PAL_UFOPAEDIA)
    upc.style = 1
    with open(os.path.join(rules_dir, "ufopaedia.rul"), encoding="utf-8") as f:
        crafts = {int(m.group(1)) for a in yaml.safe_load(f)["ufopaedia"] if a.get("type_id") == 1
                  for m in [re.match(r"UP0*(\d+)\.SPK", a.get("image_id") or "")] if m}
    for n in range(1, 0 if no_pedia else 43):     # (VGA: 64000 bytes each, 2.7 MB)
        try:
            data = ufodata.read(root, "GEOGRAPH/UP%03d.SPK" % n)
        except FileNotFoundError:
            continue
        entries[UI["UI_UP"] + n - 1] = picture(dev, load_spk(data), upc if n in crafts else up,
                                               0, 0, cga_pal=2, screen=True, pen=0.35)

    # The Graphs screen's icon buttons (GRAPHS.SPK holds only them: 7 of
    # 32 x 24 at 96, 0; GraphsState draws the rest), CGA palette 1 (blue).
    gspk = load_spk(ufodata.read(root, "GEOGRAPH/GRAPHS.SPK"))
    entries[UI["UI_GRAPHS"]] = picture(dev, sub(gspk, 96, 0, 224, 24),
                                       palette(root, ufodata.PAL_GRAPHS), 96, 0, cga_pal=1, pen=0.35)

    entries[UI["UI_PEDIA_MAP"]] = pedia_table(rules_dir, items)
    entries[UI["UI_BRIEFING"]] = briefing_table(root)
    entries[UI["UI_REPORT"]] = report_table(root)
    entries[UI["UI_MSG"]] = msg_table(root)

    # Reserve-TU buttons pressed (BattlescapeButton: colours inverted
    # around the button's colour + 3).
    # CGA: the button's two colours swapped instead.
    for i, (x, y, w, h, mid) in enumerate(RESERVE_BUTTONS):
        if cga:
            entries[UI["UI_RESERVE"] + i] = picture(dev, sub(icons, x, y, w, h), bpal, x, y,
                                                    masked=True, pen=0.35,
                                                    override=panel_cga(pressed=True))
            continue
        entries[UI["UI_RESERVE"] + i] = picture(dev, invert(sub(icons, x, y, w, h), mid), bpal,
                                                x, y, masked=True)
    # The minimap frame.
    entries[UI["UI_SCANBORD"]] = picture(dev, load_spk(ufodata.read(root, "UFOGRAPH/SCANBORD.PCK")),
                                         bpal, 0, 0, cga_pal=BATTLE_CGA, screen=True, pen=0.35)

    # Air combat (DogfightState): the window, pressed stance buttons, weapon
    # icons in both slots, the craft with its damage.
    data = ufodata.read(root, "GEODATA/INTERWIN.DAT")
    win = [list(data[y * 160:(y + 1) * 160]) for y in range(len(data) // 160)]
    entries[UI["UI_DF_WINDOW"]] = picture(dev, win[:96], geo, DF_X, DF_Y, pen=0.35)
    for i, (x, y, w, h) in enumerate(DF_BUTTONS):
        entries[UI["UI_DF_PRESSED"] + i] = picture(dev, invert(sub(win, x, y, w, h), 85), geo,
                                                   DF_X + x, DF_Y + y, masked=True, pen=0.35)
    inticon = ufodata.load_pck(root, "GEOGRAPH/INTICON", 32, 40)
    for slot, wx in enumerate((4, 64)):
        for cw, spr in enumerate(CW_SPRITES):
            entries[UI["UI_DF_WEAPON"] + slot * 6 + cw] = picture(
                dev, sub(inticon[spr + 5], 0, 0, 15, 17), geo, DF_X + wx, DF_Y + 52, masked=True)
    red = min(range(1, 256), key=lambda i: egacolor._dist(geo[i], (255, 40, 40)))
    for ct, spr in enumerate(CT_SPRITES + [0]):     # the Skyranger last: UI_DF_SKYRANGER
        craft = sub(inticon[spr + 11], 0, 0, 22, 25)
        rows = [y for y, r in enumerate(craft) if any(r)]
        for q in range(5):
            # DogfightState::drawCraftDamage colours the craft's rows from
            # the top by the damage percentage: here in quarters.
            n = len(rows) * q // 4
            pic = [[red if c and y in rows[:n] else c for c in r] for y, r in enumerate(craft)]
            entries[(UI["UI_DF_CRAFT"] + ct * 5 if ct < len(CT_SPRITES) else UI["UI_DF_SKYRANGER"])
                    + q] = picture(dev, pic, geo, DF_X + 93, DF_Y + 40, masked=True)

    # UFO pictures for the UFOpaedia and the air combat preview: top and
    # bottom of the frame, the UFO by ufos.rul sprite (ArticleStateUfo).
    entries[UI["UI_UFO_TOP"]] = picture(dev, win[96:111], geo, 160, 6, cga_pal=2, pen=0.35)
    entries[UI["UI_UFO_BOTTOM"]] = picture(dev, win[111:140], geo, 160, 73, cga_pal=2, pen=0.35)
    for s in range(8):
        y0 = 140 + 52 * s
        if y0 + 52 <= len(win):
            entries[UI["UI_UFO_MID"] + s] = picture(dev, win[y0:y0 + 52], geo, 160, 21, cga_pal=2,
                                                    pen=0.35)

    count = UI["UI_COUNT"]
    head = bytearray(b"XUI1" + struct.pack("<HH", count, 0))
    offset = len(head) + 4 * count
    # Small entries first: DOS seeks walk the cluster chain from the start
    # of the file, so the pictures drawn by the dozen (base view, items)
    # are found quicker near the front.
    offsets = {}
    body = bytearray()
    for i in sorted(entries, key=lambda k: (len(entries[k]) > 4096, k)):
        offsets[i] = offset + len(body)
        body += entries[i]
    table = bytearray()
    for i in range(count):
        table += struct.pack("<I", offsets.get(i, 0))
    return bytes(head + table + body), entries


# ----------------------------------------------------------------------
# Preview (decodes the packed pictures again)


def decode(entry, cga, vga=False):
    x, y, w, h, flags, _ = struct.unpack_from("<HHHHBB", entry, 0)
    d = entry[10:]
    img = [[None] * w for _ in range(h)]
    if vga:
        for ry in range(h):
            for cx in range(w):
                c = d[ry * w + cx]
                img[ry][cx] = None if flags & 1 and not c else c
    elif not cga:
        nb = w // 8
        if not flags & 1:
            for p in range(4):
                for ry in range(h):
                    for b in range(nb):
                        v = d[(p * h + ry) * nb + b]
                        for bit in range(8):
                            if v & (0x80 >> bit):
                                img[ry][b * 8 + bit] = (img[ry][b * 8 + bit] or 0) | (1 << p)
                            elif img[ry][b * 8 + bit] is None:
                                img[ry][b * 8 + bit] = 0
        else:
            k = 0
            for ry in range(h):
                for b in range(nb):
                    m, *pl = d[k:k + 5]
                    k += 5
                    for bit in range(8):
                        if m & (0x80 >> bit):
                            img[ry][b * 8 + bit] = sum(1 << p for p in range(4)
                                                       if pl[p] & (0x80 >> bit))
    else:
        nb = w // 4
        k = 0
        for ry in range(h):
            for b in range(nb):
                if flags & 1:
                    m, v = d[k], d[k + 1]
                    k += 2
                else:
                    m, v = 0xFF, d[k]
                    k += 1
                for q in range(4):
                    sh = 6 - 2 * q
                    if (m >> sh) & 3:
                        img[ry][b * 4 + q] = (v >> sh) & 3
    return x, y, w, h, flags, img


def preview(entries, cga, outdir, vga_pal=None):
    os.makedirs(outdir, exist_ok=True)
    names = {v: k for k, v in UI.items() if k.startswith("UI_")}
    for i, e in sorted(entries.items()):
        if i in (UI["UI_FONT_SMALL"], UI["UI_FONT_BIG"], UI["UI_PEDIA_MAP"], UI["UI_BRIEFING"],
                 UI["UI_REPORT"], UI["UI_MSG"]):
            continue
        x, y, w, h, flags, img = decode(e, cga, vga_pal is not None)
        if vga_pal is not None:
            dev = vga_pal
        elif cga:
            dev = egacolor.CGA_PALETTES[CGA_NAMES[(flags >> 1) & 3]]
        else:
            dev = egacolor.EGA_RGB
        rows = [[dev[c] if c is not None else (60, 0, 60) for c in r for _ in (0, 1)]
                for r in img for _ in (0, 1)]
        png.write_png(os.path.join(outdir, "%s_%d.png" % (names.get(i, "E"), i)), w * 2, h * 2,
                      rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("data")
    ap.add_argument("rules")
    ap.add_argument("outdir")
    ap.add_argument("--cga", action="store_true")
    ap.add_argument("--vga", action="store_true")
    ap.add_argument("--no-pedia", action="store_true",
                    help="without the UFOpaedia's pictures (UIV.PAK: 2.7 MB less; the test disk)")
    ap.add_argument("--preview")
    args = ap.parse_args()
    data, entries = build(args.data, args.rules, args.cga, args.vga, args.no_pedia)
    if args.vga:
        with open(os.path.join(args.outdir, "PALV.PAK"), "wb") as f:
            f.write(vga_palettes(args.data))

    path = os.path.join(args.outdir, "UIV.PAK" if args.vga else "UIC.PAK" if args.cga else "UIE.PAK")
    with open(path, "wb") as f:
        f.write(data)
    print("wrote", path, len(data), "bytes,", len(entries), "entries")
    if args.preview:
        preview(entries, args.cga, args.preview,
                battle_palette(args.data) if args.vga else None)


if __name__ == "__main__":
    main()
