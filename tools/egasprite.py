# SPDX-License-Identifier: GPL-3.0-or-later
"""Encodes 32x40 frames into the engine's planar EGA sprite formats.

Both formats work on 16-pixel words (two EGA plane bytes, left byte first),
one bit plane at a time, as blitted by src/blit.asm.

Masked format (any sprite):
    u8 first_row, u8 row_count, u16 mask_bytes, u16 plane_bytes, u8 run_count
    u8 ops[row_count]   bit0 word 0 present, bit1 word 0 opaque,
                        bit2 word 1 present, bit3 word 1 opaque
    u8 runs[run_count][2]   (rows, type): type = 3 * word0 + word1 with
                        0 absent, 1 masked, 2 opaque (drives the fast path)
    u16 masks[]         one per present, non-opaque word (bit 1 = keep)
    u16 plane0[] .. plane3[]   one per present word, row-major

OR format (ground floors, which never overlap each other):
    u8 first_row, u8 row_count
    u16 plane0[row_count * 2] .. plane3[row_count * 2]
"""

import struct

import egacolor


def _planar_rows(frame, mapping):
    """Per row: list of 4 (mask, [p0..p3]) byte columns."""
    rows = []
    for y, line in enumerate(frame):
        row = []
        for c in range(4):
            mask = 0xFF
            planes = [0, 0, 0, 0]
            for bit in range(8):
                x = c * 8 + bit
                idx = line[x]
                if not idx:
                    continue
                shift = 7 - bit
                mask &= ~(1 << shift) & 0xFF
                ega = egacolor.pixel_colour(mapping, idx, x, y)
                for p in range(4):
                    if ega & (1 << p):
                        planes[p] |= 1 << shift
            row.append((mask, planes))
        rows.append(row)
    return rows


def _bounds(rows):
    nonempty = [y for y, row in enumerate(rows) if any(m != 0xFF for m, _ in row)]
    if not nonempty:
        return 0, 0
    return nonempty[0], nonempty[-1] - nonempty[0] + 1


def _word_kind(bits):
    """Op bits of one word -> 0 absent, 1 masked, 2 opaque."""
    if not bits & 1:
        return 0
    return 2 if bits & 2 else 1


def encode(frame, mapping):
    rows = _planar_rows(frame, mapping)
    first, count = _bounds(rows)
    ops = bytearray()
    masks = bytearray()
    planes = [bytearray() for _ in range(4)]
    for row in rows[first:first + count]:
        op = 0
        for w in range(2):
            (m0, p0), (m1, p1) = row[w * 2], row[w * 2 + 1]
            if m0 == 0xFF and m1 == 0xFF:
                continue
            op |= 1 << (2 * w)
            if m0 == 0 and m1 == 0:
                op |= 2 << (2 * w)
            else:
                masks += bytes((m0, m1))
            for p in range(4):
                planes[p] += bytes((p0[p], p1[p]))
        ops.append(op)
    # Runs of rows with the same op pattern, as (count, type) pairs with
    # type = 3 * word0 + word1 and word = 0 absent, 1 masked, 2 opaque.
    runs = bytearray()
    for op in ops:
        kind = 3 * _word_kind(op & 3) + _word_kind((op >> 2) & 3)
        if runs and runs[-1] == kind and runs[-2] < 255:
            runs[-2] += 1
        else:
            runs += bytes((1, kind))
    out = bytearray((first, count)) + struct.pack("<HHB", len(masks), len(planes[0]),
                                                  len(runs) // 2)
    out += ops + runs + masks
    for p in range(4):
        out += planes[p]
    return bytes(out)


def encode_or(frame, mapping):
    rows = _planar_rows(frame, mapping)
    first, count = _bounds(rows)
    out = bytearray((first, count))
    for p in range(4):
        for row in rows[first:first + count]:
            out += bytes(row[c][1][p] for c in range(4))
    return bytes(out)


def _diamond_row_span(y):
    """Columns of X-COM's standard floor diamond in row y (rows 25..39,
    widest at row 32), or None. Neighbouring tiles' diamonds never overlap."""
    if not 25 <= y <= 39:
        return None
    half = 16 - 2 * abs(y - 32)
    return 16 - half, 15 + half


def is_simple_floor(frame):
    """True if every pixel lies in the tile's own floor diamond, so ground
    floors can be OR-ed into a cleared buffer without masks."""
    for y, line in enumerate(frame):
        span = _diamond_row_span(y)
        for x, c in enumerate(line):
            if c and (span is None or not span[0] <= x <= span[1]):
                return False
    return True


def _check_diamond_tiling():
    cells = {(x, y) for y in range(40) for x in range(32)
             if _diamond_row_span(y) and _diamond_row_span(y)[0] <= x <= _diamond_row_span(y)[1]}
    for dx, dy in ((16, 8), (-16, 8), (32, 0), (0, 16)):
        if cells & {(x + dx, y + dy) for x, y in cells}:
            raise AssertionError("floor diamonds overlap at offset %d,%d" % (dx, dy))
    return len(cells)


def decode(data):
    """Inverse of encode: rows of EGA colours, -1 = clear. For tests."""
    img = [[-1] * 32 for _ in range(40)]
    first, count = data[0], data[1]
    mask_bytes, plane_len, n_runs = struct.unpack_from("<HHB", data, 2)
    ops = data[7:7 + count]
    mpos = 7 + count + 2 * n_runs
    dpos = mpos + mask_bytes
    k = 0
    for r, op in enumerate(ops):
        for w in range(2):
            if not op & (1 << (2 * w)):
                continue
            if op & (2 << (2 * w)):
                m = (0, 0)
            else:
                m = (data[mpos], data[mpos + 1])
                mpos += 2
            for b in range(2):
                pl = [data[dpos + p * plane_len + k * 2 + b] for p in range(4)]
                for bit in range(8):
                    shift = 7 - bit
                    if m[b] & (1 << shift):
                        continue
                    img[first + r][w * 16 + b * 8 + bit] = sum(
                        ((pl[p] >> shift) & 1) << p for p in range(4))
            k += 1
    return img


def decode_or(data):
    """Inverse of encode_or (colour 0 where nothing is set). For tests."""
    img = [[0] * 32 for _ in range(40)]
    first, count = data[0], data[1]
    for p in range(4):
        for r in range(count):
            for c in range(4):
                byte = data[2 + (p * count + r) * 4 + c]
                for bit in range(8):
                    if byte & (1 << (7 - bit)):
                        img[first + r][c * 8 + bit] |= 1 << p
    return img
