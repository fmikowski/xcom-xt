# SPDX-License-Identifier: GPL-3.0-or-later
"""Reduces the 256-colour X-COM palette to the 16 fixed EGA/RGBI colours.

Each palette entry becomes a pair of EGA colours plus a 2x2 ordered-dither
level, so mid tones are approximated by a checker of two neighbours.
"""

EGA_RGB = [
    (0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
    (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
    (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
    (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255),
]

# 2x2 Bayer thresholds in quarters: pixel takes colour b when level > t.
BAYER2 = ((0, 2), (3, 1))


def _dist(c1, c2):
    """'Redmean' weighted RGB distance squared."""
    rm = (c1[0] + c2[0]) / 2
    dr, dg, db = c1[0] - c2[0], c1[1] - c2[1], c1[2] - c2[2]
    return (2 + rm / 256) * dr * dr + 4 * dg * dg + (2 + (255 - rm) / 256) * db * db


CGA_PALETTES = {
    "0lo": [(0, 0, 0), (0, 170, 0), (170, 0, 0), (170, 85, 0)],
    "0hi": [(0, 0, 0), (85, 255, 85), (255, 85, 85), (255, 255, 85)],
    "1lo": [(0, 0, 0), (0, 170, 170), (170, 0, 170), (170, 170, 170)],
    "1hi": [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)],
}


def build_mapping(palette, noise_penalty=0.35, target=None):
    """Returns per palette index (a, b, level) with level in 0..4 quarters of b.

    target: the device colours to mix (default: the 16 EGA colours)."""
    target = target or EGA_RGB
    mixes = []
    for a in range(len(target)):
        mixes.append((a, a, 0, target[a], 0.0))
        for b in range(a + 1, len(target)):
            pair = _dist(target[a], target[b])
            for level in (1, 2, 3):
                t = level / 4
                rgb = tuple(target[a][i] * (1 - t) + target[b][i] * t for i in range(3))
                mixes.append((a, b, level, rgb, noise_penalty * pair * t * (1 - t)))
    mapping = []
    for colour in palette:
        best = min(mixes, key=lambda m: _dist(colour, m[3]) + m[4])
        mapping.append(best[:3])
    return mapping


def pixel_colour(mapping, index, x, y):
    a, b, level = mapping[index]
    return b if level > BAYER2[y & 1][x & 1] else a
