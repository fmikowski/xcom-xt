# SPDX-License-Identifier: GPL-3.0-or-later
"""Readers for the original X-COM: UFO Defense data files.

Formats as documented by OpenXcom and the UFOpaedia: PALETTES.DAT, PCK/TAB
sprite sets, MCD terrain records, MAP blocks and RMP route nodes.
"""

import os
import struct

PAL_GEOSCAPE, PAL_BASESCAPE, PAL_GRAPHS, PAL_UFOPAEDIA, PAL_BATTLESCAPE = range(5)


def find_file(root, rel):
    """Case-insensitive path lookup under root."""
    path = root
    for part in rel.replace("\\", "/").split("/"):
        names = {n.lower(): n for n in os.listdir(path)}
        if part.lower() not in names:
            raise FileNotFoundError(os.path.join(path, part))
        path = os.path.join(path, names[part.lower()])
    return path


def read(root, rel):
    with open(find_file(root, rel), "rb") as f:
        return f.read()


def load_palette(root, index):
    """Returns 256 (r, g, b) tuples scaled to 0-255."""
    data = read(root, "GEODATA/PALETTES.DAT")
    off = index * (768 + 6)
    return [tuple(min(255, c * 4) for c in data[off + i * 3:off + i * 3 + 3])
            for i in range(256)]


def decode_pck_frame(data, width, height):
    """Decodes one PCK frame to a list of rows of colour indices (0 = clear)."""
    pixels = [0] * (width * height)
    pos = data[0] * width          # first byte: blank rows at the top
    i = 1
    while i < len(data):
        b = data[i]
        i += 1
        if b == 0xFF:
            break
        if b == 0xFE:
            pos += data[i]
            i += 1
            continue
        if pos < len(pixels):
            pixels[pos] = b
        pos += 1
    return [pixels[y * width:(y + 1) * width] for y in range(height)]


def load_pck(root, name, width=32, height=40):
    """Loads NAME.PCK/NAME.TAB (name includes the folder, no extension)."""
    pck = read(root, name + ".PCK")
    tab = read(root, name + ".TAB")
    offsets = list(struct.unpack("<%dH" % (len(tab) // 2), tab))
    frames = []
    for i, start in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < len(offsets) else len(pck)
        frames.append(decode_pck_frame(pck[start:end], width, height))
    return frames


MCD_FIELDS = (
    # name, offset
    ("ufo_door", 30), ("stop_los", 31), ("no_floor", 32), ("big_wall", 33),
    ("gravlift", 34), ("door", 35), ("block_fire", 36), ("block_smoke", 37),
    ("tu_walk", 39), ("tu_slide", 40), ("tu_fly", 41), ("armor", 42),
    ("he_block", 43), ("die_mcd", 44), ("flammable", 45), ("alt_mcd", 46),
    ("light_block", 51), ("footstep", 52), ("tile_type", 53), ("he_type", 54),
    ("he_strength", 55), ("smoke_block", 56), ("fuel", 57),
    ("light_source", 58), ("special", 59), ("xcom_base", 60),
)


# OpenXcom's corrections to the MCD files (standard/xcom1/mcdPatches.rul,
# set by the converter from rules.Rules.mcd_patches): data set -> [(index,
# fields)]. Only the fields the blast and fire rules read are applied (the
# path and sight ones are not). "big_wall_kind" is OpenXcom's bigWall (0
# none, 1 block, 2 and 3 the diagonals, 4-7 west, north, east, south, 8 east
# and south, 9 west and north); "big_wall" stays the MCD's own byte (the
# engine's big wall flag for sight and paths).
MCD_PATCHES = {}
PATCH_FIELDS = {"bigWall": "big_wall_kind", "flammability": "flammable", "fuel": "fuel",
                "HEBlock": "he_block", "armor": "armor", "deathTile": "die_mcd"}


def load_mcd(root, name):
    data = read(root, "TERRAIN/%s.MCD" % name)
    records = []
    for off in range(0, len(data) - 61, 62):
        r = data[off:off + 62]
        rec = {
            "frames": list(r[0:8]),
            "loft": list(r[8:20]),
            "scang": struct.unpack_from("<H", r, 20)[0],
            "t_level": struct.unpack_from("<b", r, 48)[0],
            "p_level": r[49],
        }
        for field, o in MCD_FIELDS:
            rec[field] = r[o]
        rec["big_wall_kind"] = rec["big_wall"]
        records.append(rec)
    for index, fields in MCD_PATCHES.get(name.upper(), ()):
        if index < len(records):
            for key, field in PATCH_FIELDS.items():
                if key in fields:
                    records[index][field] = fields[key]
    return records


def load_map(root, name):
    """Returns (size_x, size_y, size_z, tiles) with tiles[z][y][x] = 4 parts.

    z = 0 is the ground level. Parts are raw indices into the block's
    terrain data set list (0 = empty).
    """
    data = read(root, "MAPS/%s.MAP" % name)
    size_y, size_x, size_z = data[0], data[1], data[2]
    tiles = [[[None] * size_x for _ in range(size_y)] for _ in range(size_z)]
    i = 3
    for z in range(size_z - 1, -1, -1):
        for y in range(size_y):
            for x in range(size_x):
                tiles[z][y][x] = tuple(data[i:i + 4])
                i += 4
    return size_x, size_y, size_z, tiles


def load_rmp(root, name, size_z, size_x=None, size_y=None):
    """Route nodes: list of dicts with x, y, z (z = 0 ground), rank, links.
    With the block size, nodes outside the block get "bad": True (OpenXcom
    keeps them as dummies so the links still line up)."""
    data = read(root, "ROUTES/%s.RMP" % name)
    nodes = []
    for off in range(0, len(data) - 23, 24):
        r = data[off:off + 24]
        nodes.append({
            "x": r[1], "y": r[0], "z": size_z - 1 - r[2],
            "links": [r[4 + i * 3] for i in range(5)],
            "unit_type": r[19], "rank": r[20], "flags": r[21],
            "reserved": r[22], "priority": r[23],
            "bad": size_x is not None and (r[1] >= size_x or r[0] >= size_y or r[2] >= size_z),
        })
    return nodes
