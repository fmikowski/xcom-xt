# SPDX-License-Identifier: GPL-3.0-or-later
"""Cuts the emulated screen out of 86Box window shots (tools/snapwin.ps1) for the
public screenshots (tools/pubshots.ps1):

    python pubshots.py OUTDIR shot.png=ega-battle.png ...

The name's first part is the mode. EGA and VGA: the 640x400 picture as it is (320x200
doubled). Hercules: the game's 640x200 picture from the middle of the 720x348 screen,
lines doubled. CGA: 86Box blends each pixel with its neighbour (3/4 + 1/4) and shows
170 as 196, so each 320x200 pixel is read where it is sharp, snapped to the screen's
four colours (uk_cga_palette: palette 0 or 1, low intensity) and doubled again in
the standard RGBI values. Pictures of 256 colours or fewer are saved with a palette."""
import os
import sys
from PIL import Image

CGA = [
    {(0, 0, 0): (0, 0, 0), (0, 196, 0): (0, 170, 0), (196, 0, 0): (170, 0, 0), (196, 126, 0): (170, 85, 0)},
    {(0, 0, 0): (0, 0, 0), (0, 196, 196): (0, 170, 170), (196, 0, 196): (170, 0, 170),
     (196, 196, 196): (170, 170, 170)},
]


def cga(im):
    px = [im.getpixel((2 * x + 1, 2 * y)) for y in range(200) for x in range(320)]
    pal = CGA[1] if sum(1 for p in px if p == (0, 196, 196)) > 100 else CGA[0]
    keys = list(pal)
    near = {}
    for p in set(px):
        near[p] = min(range(4), key=lambda i: sum((a - b) ** 2 for a, b in zip(p, keys[i])))
    out = Image.new('P', (320, 200))
    out.putpalette([c for k in keys for c in pal[k]] + [0] * (768 - 12))
    out.putdata([near[p] for p in px])
    return out.resize((640, 400), Image.NEAREST)


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    for arg in sys.argv[2:]:
        src, name = arg.rsplit('=', 1)
        im = Image.open(src).convert('RGB')
        w, h = im.size
        im = im.crop((3, 73, w - 3, h - 25))        # 86Box's borders, menu, toolbar, status bar
        mode = name.split('-')[0]
        if mode == 'herc':
            im = im.crop((40, 74, 680, 274)).resize((640, 400), Image.NEAREST)
        elif mode == 'cga':
            im = cga(im)
        if im.size != (640, 400):
            sys.exit(f'{src}: {im.size}, not 640x400')
        if im.mode == 'RGB':
            cols = im.getcolors(256)
            if cols:
                pal = Image.new('P', (1, 1))
                flat = [c for _, rgb in cols for c in rgb]
                pal.putpalette(flat + [0] * (768 - len(flat)))
                im = im.quantize(palette=pal, dither=Image.Dither.NONE)
        path = os.path.join(outdir, name)
        im.save(path, optimize=True)
        print(name, os.path.getsize(path))


if __name__ == '__main__':
    main()
