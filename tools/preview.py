# SPDX-License-Identifier: GPL-3.0-or-later
"""Renders a whole generated mission map to PNG, in VGA or EGA colours.

    python tools/preview.py <UFO data dir> out.png [--ega] [--seed N]
        [--terror <rules dir>]
"""

import argparse

import egacolor
import mission as missionmod
import png
import ufodata


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("data")
    ap.add_argument("out")
    ap.add_argument("--ega", action="store_true")
    ap.add_argument("--cga", help="CGA palette: 0lo, 0hi, 1lo or 1hi")
    ap.add_argument("--crop", help="x,y,w,h of the output")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--terror", metavar="RULES_DIR", help="a terror site (city) instead")
    args = ap.parse_args()

    if args.terror:
        import rules as rulesmod
        spec = missionmod.make_spec(rulesmod.Rules(args.terror), "URBAN", None)
    else:
        spec = missionmod.FARM_SCOUT
    m = missionmod.Mission(args.data, spec)
    (mx, my, mz), tiles, placements = missionmod.generate_map(m, args.seed)
    print("placements:", placements)
    pal = ufodata.load_palette(args.data, ufodata.PAL_BATTLESCAPE)
    device = egacolor.EGA_RGB
    if args.cga:
        device = egacolor.CGA_PALETTES[args.cga]
        args.ega = True
    mapping = egacolor.build_mapping(pal, target=device)

    width = (mx + my) * 16 + 32
    height = (mx + my) * 8 + mz * 24 + 48
    ox, oy = my * 16 - 16, mz * 24
    img = [[(0, 0, 0)] * width for _ in range(height)]

    def blit(frame, sx, sy):
        for fy, row in enumerate(frame):
            py = sy + fy
            if not 0 <= py < height:
                continue
            for fx, c in enumerate(row):
                px = sx + fx
                if c and 0 <= px < width:
                    if args.ega:
                        img[py][px] = device[egacolor.pixel_colour(mapping, c, px, py)]
                    else:
                        img[py][px] = pal[c]

    for z in range(mz):
        for x in range(mx):
            for y in range(my):
                sx = (x - y) * 16 + ox
                sy = (x + y) * 8 - z * 24 + oy
                for part in tiles[z][y][x]:
                    if part:
                        rec = m.mcd[part]
                        blit(m.sprites[rec["sprite"]], sx, sy - rec["p_level"])
    if args.crop:
        cx, cy, cw, ch = (int(v) for v in args.crop.split(","))
        img = [row[cx:cx + cw] for row in img[cy:cy + ch]]
        width, height = cw, ch
    png.write_png(args.out, width, height, img)
    print("wrote", args.out, width, "x", height, "-", len(m.mcd) - 1, "terrain records")


if __name__ == "__main__":
    main()
