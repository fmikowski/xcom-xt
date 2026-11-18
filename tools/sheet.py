# SPDX-License-Identifier: GPL-3.0-or-later
"""Renders a unit sprite sheet as a grid, 8 frames per row, for study.

    python tools/sheet.py <UFO data dir> <SHEET> out.png [first] [count]
"""

import sys

import png
import ufodata


def main():
    root, sheet, out = sys.argv[1:4]
    first = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    frames = ufodata.load_pck(root, "UNITS/" + sheet)
    count = int(sys.argv[5]) if len(sys.argv) > 5 else len(frames) - first
    frames = frames[first:first + count]
    pal = ufodata.load_palette(root, ufodata.PAL_BATTLESCAPE)
    rows = (len(frames) + 7) // 8
    w, h = 8 * 34, rows * 42
    img = [[(30, 30, 60) if ((x // 34) + (y // 42)) % 2 else (45, 45, 90) for x in range(w)]
           for y in range(h)]
    for i, fr in enumerate(frames):
        ox, oy = (i % 8) * 34, (i // 8) * 42
        for y, row in enumerate(fr):
            for x, c in enumerate(row):
                if c:
                    img[oy + y][ox + x] = pal[c]
    png.write_png(out, w, h, img)
    print(sheet, "frames", first, "to", first + len(frames) - 1)


if __name__ == "__main__":
    main()
