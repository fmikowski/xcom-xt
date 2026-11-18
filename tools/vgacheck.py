# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks VGA packs (tools/vgasprite.py) against the EGA packs of the same
missions and units: every sprite must cover exactly the pixels the EGA
sprite's mask covers (both come from the same original picture; OR-format
floors may cover more, the zeros inside a row). With --shots also compares
an EGA and a VGA screenshot of the same view (86Box, 2x, tools\\snapwin.ps1):
every EGA pixel must be one of the two EGA colours the converter mixes for
the VGA pixel's palette index (tools/egacolor.py, dither phase ignored).

    python tools/vgacheck.py packs FA1 JU7 RS RM A1 H1 ...
    python tools/vgacheck.py --shots ega.png vga.png [--data C:\\Games\\OpenXcom\\UFO]

Exit status 1 on a difference.
"""

import argparse
import os
import struct
import sys

import egacolor
import egasprite
import ufodata
import vgasprite


def load(path):
    """(sprites, OR sprite indices) of a mission pack (the common sprites:
    the transports' are numbered by the engine) or a unit pack."""
    d = open(path, "rb").read()
    if d[:4] == b"XRC1":
        n_spr = struct.unpack_from("<H", d, 8)[0]
        pos = 32 + 8 * 24
        offs = struct.unpack_from("<%dI" % (n_spr + 1), d, pos)
        base = pos + (n_spr + 1) * 4
        return [d[base + offs[i]:base + offs[i + 1]] for i in range(n_spr)], set()
    n_mcd, n_spr = struct.unpack_from("<HH", d, 6)
    nf, nl, nb = d[18], d[19], d[20]
    n_us = struct.unpack_from("<H", d, 26)[0]
    pos = 64
    n_common, spr_common = n_mcd, n_spr
    if d[62] == 1:                              # "XCR1" (convert.py craft_section)
        n_common, spr_common = struct.unpack_from("<HH", d, pos + 5)
        q = pos + 9
        for _ in range(d[pos + 4]):
            q += 10 + 4 * d[q + 9]
        pos = q
    mcd = [d[pos + 16 * i:pos + 16 * i + 16] for i in range(n_mcd)]
    pos += n_mcd * 16 + nf + nl + nb + n_us * 2
    offs = struct.unpack_from("<%dI" % (n_spr + 1), d, pos)
    base = pos + (n_spr + 1) * 4
    ors = set()
    for r in mcd[1:n_common]:
        if r[2] & 3 == 0:                       # a floor: or_sprite is its OR sprite
            o = struct.unpack_from("<H", r, 14)[0] & 0x7FFF
            if o != 0x7FFF:
                ors.add(o)
    return [d[base + offs[i]:base + offs[i + 1]] for i in range(spr_common)], ors


def mask(img, clear):
    return {(x, y) for y, row in enumerate(img) for x, c in enumerate(row) if c != clear}


def compare(ega_path, vga_path):
    se, oe = load(ega_path)
    sv, ov = load(vga_path)
    if len(se) != len(sv) or oe != ov:
        print("%s: %d sprites (%d OR) against %d (%d OR)" % (vga_path, len(sv), len(ov), len(se), len(oe)))
        return 1
    bad = 0
    for i, (a, b) in enumerate(zip(se, sv)):
        if i in oe:
            ok = mask(vgasprite.decode_or(b), 0) >= mask(egasprite.decode_or(a), 0)
        else:
            ok = mask(vgasprite.decode(b), -1) == mask(egasprite.decode(a), -1)
        if not ok:
            print("%s: sprite %d differs from the EGA pack's" % (vga_path, i))
            bad += 1
    print("%s: %d sprites, %d differ" % (os.path.basename(vga_path), len(sv), bad))
    return 1 if bad else 0


def shots(ega_png, vga_png, data, ox=3, oy=73):
    from PIL import Image
    e_img = Image.open(ega_png).convert("RGB")
    v_img = Image.open(vga_png).convert("RGB")
    raw = ufodata.read(data, "GEODATA/PALETTES.DAT")
    off = ufodata.PAL_BATTLESCAPE * (768 + 6)
    raw = raw[off:off + 768]
    mapping = egacolor.build_mapping(ufodata.load_palette(data, ufodata.PAL_BATTLESCAPE))
    by_rgb = {}
    for i in range(256):
        by_rgb.setdefault(tuple(c * 255 // 63 for c in raw[3 * i:3 * i + 3]), set()).add(i)
    ega = [tuple(c) for c in egacolor.EGA_RGB]

    def ega_index(p):
        return min(range(16), key=lambda k: sum((a - b) ** 2 for a, b in zip(ega[k], p)))
    bad = unknown = 0
    for y in range(144):
        for x in range(320):
            idx = by_rgb.get(v_img.getpixel((ox + 2 * x, oy + 2 * y)))
            if idx is None:
                unknown += 1
                continue
            ok = set()
            for i in idx:
                a, b, _ = mapping[i] if i else (0, 0, 0)
                ok |= {a, b}
            bad += ega_index(e_img.getpixel((ox + 2 * x, oy + 2 * y))) not in ok
    print("screenshots: %d view pixels differ, %d not in the palette" % (bad, unknown))
    return 1 if bad or unknown else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("packs", nargs="?")
    ap.add_argument("names", nargs="*", help="pack names without E/V and .PAK")
    ap.add_argument("--shots", nargs=2, metavar=("EGA_PNG", "VGA_PNG"))
    ap.add_argument("--data", default=r"C:\Games\OpenXcom\UFO")
    args = ap.parse_args()
    if args.shots:
        return shots(args.shots[0], args.shots[1], args.data)
    rc = 0
    for n in args.names:
        rc |= compare(os.path.join(args.packs, n + "E.PAK"), os.path.join(args.packs, n + "V.PAK"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
