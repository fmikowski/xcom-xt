# SPDX-License-Identifier: GPL-3.0-or-later
"""Cover data for the battlescape renderer's skipped sprites (src/render.c
`floor_hidden`, `roofed`), stored in the MCD records' or_sprite field.

The view is cut into "quads": X-COM floor diamonds tile the screen (the
standard diamond of egasprite._diamond_row_span, 256 pixels, on the lattice
(16, 8) / (16, -8)), and so do the diamonds of the level above (24 rows up,
an odd number of 8-row steps: the same lattice shifted by 8 rows). Each
diamond of one lattice is split by four diamonds of the other into four
quads (top, bottom, left, right). A sprite hides a quad when it draws every
pixel of it; a floor that stays inside its diamond is hidden when all four
of its quads are.

or_sprite, packs with the cover data (header byte 63 = 1):
    floors (tile_type 0): bit 15 = the sprite (p_level 0) draws every pixel
        of its diamond; bits 0-14 = the OR-format copy, 0x7FFF = none.
    walls and objects: the quads the sprite (at its p_level) draws fully,
        as bits of the floor diamonds of the tiles behind it on its level,
        tile (x - a, y - b) for the record standing on (x, y):
        bits 0-3   (0, 0)  quads top, bottom, left, right (bits 0, 1, 2, 3)
        bits 4-7   (1, 1)  same order
        bits 8-11  (2, 2)  same order
        bit 12     (1, 0)  right
        bit 13     (0, 1)  left
        bit 14     (2, 1)  right
        bit 15     (1, 2)  left
    (never 0xFFFF; older packs have 0xFFFF there and no cover data).
"""

TOP, BOTTOM, LEFT, RIGHT = 0, 1, 2, 3


def _span(r):
    if not 25 <= r <= 39:
        return None
    half = 16 - 2 * abs(r - 32)
    return 16 - half, 15 + half


DIAMOND = frozenset((c, r) for r in range(25, 40) for c in range(_span(r)[0], _span(r)[1] + 1))


def _owner(px, py, parity):
    """The diamond (u, v), u + v of the given parity, at box offset
    (16u, 8v) that holds pixel (px, py) of the box of tile (0, 0)."""
    for v in range((py - 39) // 8 - 1, (py - 25) // 8 + 2):
        for u in range(px // 16 - 1, px // 16 + 2):
            if (u + v) % 2 == parity and (px - 16 * u, py - 8 * v) in DIAMOND:
                return u, v
    raise AssertionError("no owner for %d,%d" % (px, py))


def _quads():
    q = [set() for _ in range(4)]
    side = {(0, -1): TOP, (0, 1): BOTTOM, (-1, 0): LEFT, (1, 0): RIGHT}
    for c, r in DIAMOND:
        u, v = _owner(c, r, 1)
        q[side[(u, v)]].add((c, r))
    return [frozenset(s) for s in q]


QUADS = _quads()

# Bit -> (a, b, quad): the floor diamond of tile (x - a, y - b) sits at box
# offset (-16 (a - b), -8 (a + b)) from the box of tile (x, y).
BITS = ([(0, 0, k) for k in range(4)] + [(1, 1, k) for k in range(4)]
        + [(2, 2, k) for k in range(4)]
        + [(1, 0, RIGHT), (0, 1, LEFT), (2, 1, RIGHT), (1, 2, LEFT)])


def _check():
    assert len(DIAMOND) == 256 and sum(len(s) for s in QUADS) == 256
    # The lattice tiles the plane: every pixel of a test area has exactly
    # one owner of each parity.
    for py in range(0, 64):
        for px in range(0, 64):
            for parity in (0, 1):
                n = 0
                for v in range(-8, 10):
                    for u in range(-4, 6):
                        if (u + v) % 2 == parity and (px - 16 * u, py - 8 * v) in DIAMOND:
                            n += 1
                assert n == 1, (px, py, parity, n)


_check()


def _pixels(frame, dy):
    return {(c, r + dy) for r, line in enumerate(frame) for c, i in enumerate(line) if i}


def full_floor(frame):
    """True if the floor sprite (p_level 0) draws its whole diamond."""
    return DIAMOND <= _pixels(frame, 0)


def cover_bits(frame, p_level):
    """The or_sprite cover mask of a wall or object (module docstring)."""
    px = _pixels(frame, -p_level)
    mask = 0
    for bit, (a, b, k) in enumerate(BITS):
        du, dv = -16 * (a - b), -8 * (a + b)
        if all((c + du, r + dv) in px for c, r in QUADS[k]):
            mask |= 1 << bit
    return mask & 0x7FFF if mask == 0xFFFF else mask
