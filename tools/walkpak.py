# SPDX-License-Identifier: GPL-3.0-or-later
"""The walk phases the unit packs leave out (units.DRAWN_WALK_SLOTS).

Unit sets keep 4 of DOS's 8 walk phases, and only walk slots 2 and 4
(phases 2 and 6) carry pixels: memory. With upper or extended memory the
engine (src/floorld.c `walk_load`) loads slots 1 and 3 (phases 0 and 4) too,
so a walk shows four leg positions instead of two. They come from a file
beside each pack that holds unit sets: <name>W.PAK for <name>.PAK
(A1E.PAK -> A1EW.PAK, JU7E.PAK -> JU7EW.PAK, RSE.PAK -> RSEW.PAK).

    header (8 bytes): "XWK1", u8 video, u8 n_sets (the pack's own sets, in
                      its order), u16 0
    offsets ((n_sets * 16 + 1) x u32, from the data's start): frame
            set * 16 + direction * 2 + k, k 0 = walk slot 1, 1 = slot 3;
            a set without walk frames (2x2 units) has 16 empty frames
    data: the frames in the set's own pack's sprite format

EGA and CGA only (VGA packs have their palette at a fixed place from the
end; nothing here touches them). Older pack folders simply have no W
files: the engine then draws two leg positions, as before.

    python tools/walkpak.py <UFO folder> <rules folder> <packs folder> [--cga]
"""
import argparse
import os
import struct

import convert
import rules as rulesmod
import ufodata
import units

MAGIC = b"XWK1"
EXTRA_SLOTS = tuple(s for s in range(1, 5) if s not in units.DRAWN_WALK_SLOTS)


def extra_frames(pck, routine):
    """The 16 frames the pack leaves empty: direction * 2 + k."""
    if routine in units.BIG_ROUTINES:
        return [None] * 16
    out = []
    for d in range(8):
        for slot in EXTRA_SLOTS:
            out.append(units.compose(pck, d, units.WALK_PHASES[slot - 1], routine))
    return out


def w_pack(video, sets):
    """sets: [(frames or Nones, codec, mapping)] in the pack's order."""
    data = bytearray()
    offsets = []
    for frames, codec, mapping in sets:
        for fr in frames:
            offsets.append(len(data))
            if fr is not None:
                data += codec.encode(fr, mapping)
    offsets.append(len(data))
    out = bytearray(struct.pack("<4sBBH", MAGIC, video, len(sets), 0))
    for o in offsets:
        out += struct.pack("<I", o)
    return bytes(out + data)


def armour_sets(root, code, video):
    codec, mapping = convert.codec_for(root, video)
    sheet, torso, _ = convert.ARMOUR_SHEETS[code]
    pck = ufodata.load_pck(root, "UNITS/" + sheet)
    if torso != 32:
        pck = list(pck)
        pck[32:40] = pck[torso:torso + 8]
    return [(extra_frames(pck, 0), codec, mapping)]


def race_sets(root, rules, race, video, terror):
    """The sheets build_race puts into the pack, in its order."""
    got = []
    keep = convert._unit_pack

    def capture(root_, video_, sheets, *a, **k):
        got.extend(sheets)
        return b""
    convert._unit_pack = capture
    try:
        convert.build_race(root, rules, race, video, terror, zombies=True)
    finally:
        convert._unit_pack = keep
    codec, mapping = convert.codec_for(root, video)
    return [(extra_frames(ufodata.load_pck(root, "UNITS/" + sheet), routine), codec,
             convert.unit_mapping(mapping, sheet, video)) for sheet, routine in got]


def mission_sets(root, rules, n_sets, video):
    """Soldiers (XCOM_0), then the civilians (terror sites): build_spec's order."""
    codec, mapping = convert.codec_for(root, video)
    out = []
    for sheet in ["XCOM_0", "CIVM", "CIVF"][:n_sets]:
        routine = rules.armors[{"CIVM": "CIVM_ARMOR", "CIVF": "CIVF_ARMOR"}[sheet]].get(
            "drawingRoutine", 0) if sheet != "XCOM_0" else 0
        out.append((extra_frames(ufodata.load_pck(root, "UNITS/" + sheet), routine), codec, mapping))
    return out


def write_all(root, rules, outdir, video):
    """Writes <name>W.PAK beside every armour, race, terror and mission pack
    of this video mode in outdir. Returns the number written."""
    vcode = "C" if video == convert.VIDEO_CGA else "E"
    n = 0

    def put(name, sets):
        nonlocal n
        data = w_pack(video, sets)
        with open(os.path.join(outdir, name[:-4] + "W.PAK"), "wb") as f:
            f.write(data)
        n += 1
        print(name[:-4] + "W.PAK", len(data), "bytes")

    for code in convert.ARMOUR_SHEETS:
        name = "A%s%s.PAK" % (code, vcode)
        if os.path.exists(os.path.join(outdir, name)):
            put(name, armour_sets(root, code, video))
    for race, code in convert.RACE_CODES.items():
        for prefix, terror in (("R", False), ("T", True)):
            name = "%s%s%s.PAK" % (prefix, code, vcode)
            if os.path.exists(os.path.join(outdir, name)):
                put(name, race_sets(root, rules, race, video, terror))
    for name in sorted(os.listdir(outdir)):
        up = name.upper()
        if not up.endswith(vcode + ".PAK") or up.endswith("W.PAK"):
            continue
        with open(os.path.join(outdir, name), "rb") as f:
            head = f.read(32)
        if len(head) < 28 or head[:4] != convert.MAGIC or head[21] != video:
            continue
        n_sets = struct.unpack_from("<H", head, 26)[0]
        if 1 <= n_sets <= 3:
            put(up, mission_sets(root, rules, n_sets, video))
    return n


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data")
    ap.add_argument("rules")
    ap.add_argument("outdir")
    ap.add_argument("--cga", action="store_true")
    args = ap.parse_args(argv)
    rules = rulesmod.Rules(args.rules)
    ufodata.MCD_PATCHES = rules.mcd_patches
    video = convert.VIDEO_CGA if args.cga else convert.VIDEO_EGA
    print(write_all(args.data, rules, args.outdir, video), "walk packs")


if __name__ == "__main__":
    main()
