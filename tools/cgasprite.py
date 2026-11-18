# SPDX-License-Identifier: GPL-3.0-or-later
"""Encodes 32x40 frames into the engine's CGA sprite formats (src/blitc.asm).

CGA mode 4 packs 4 pixels per byte, leftmost pixel in the top two bits.
Sprites are handled in 8-pixel words (two bytes, left byte first).

Masked format:
    u8 first_row, u8 row_count, u16 mask_bytes, u16 data_bytes
    u8 ops[row_count]   bits 2i/2i+1: word i present / opaque (i = 0..3)
    u16 masks[]         one per present, non-opaque word (bits 11 = keep)
    u16 data[]          one per present word, row-major

OR format (ground floors):
    u8 first_row, u8 row_count, then row_count * 8 data bytes
"""

import struct

import egacolor


def _packed_rows(frame, mapping):
    """Per row: 8 (mask, data) byte pairs."""
    rows = []
    for y, line in enumerate(frame):
        row = []
        for b in range(8):
            mask = 0xFF
            data = 0
            for p in range(4):
                x = b * 4 + p
                idx = line[x]
                if not idx:
                    continue
                shift = 6 - 2 * p
                mask &= ~(3 << shift) & 0xFF
                data |= egacolor.pixel_colour(mapping, idx, x, y) << shift
            row.append((mask, data))
        rows.append(row)
    return rows


def _bounds(rows):
    nonempty = [y for y, row in enumerate(rows) if any(m != 0xFF for m, _ in row)]
    if not nonempty:
        return 0, 0
    return nonempty[0], nonempty[-1] - nonempty[0] + 1


def encode(frame, mapping):
    rows = _packed_rows(frame, mapping)
    first, count = _bounds(rows)
    ops = bytearray()
    masks = bytearray()
    data = bytearray()
    for row in rows[first:first + count]:
        op = 0
        for w in range(4):
            (m0, d0), (m1, d1) = row[w * 2], row[w * 2 + 1]
            if m0 == 0xFF and m1 == 0xFF:
                continue
            op |= 1 << (2 * w)
            if m0 == 0 and m1 == 0:
                op |= 2 << (2 * w)
            else:
                masks += bytes((m0, m1))
            data += bytes((d0, d1))
        ops.append(op)
    out = bytearray((first, count)) + struct.pack("<HH", len(masks), len(data))
    return bytes(out + ops + masks + data)


def encode_or(frame, mapping):
    rows = _packed_rows(frame, mapping)
    first, count = _bounds(rows)
    out = bytearray((first, count))
    for row in rows[first:first + count]:
        out += bytes(d for _, d in row)
    return bytes(out)


def decode(data):
    """Inverse of encode: rows of CGA colours, -1 = clear. For tests."""
    img = [[-1] * 32 for _ in range(40)]
    first, count = data[0], data[1]
    mask_bytes, _ = struct.unpack_from("<HH", data, 2)
    ops = data[6:6 + count]
    mpos = 6 + count
    dpos = mpos + mask_bytes
    for r, op in enumerate(ops):
        for w in range(4):
            if not op & (1 << (2 * w)):
                continue
            if op & (2 << (2 * w)):
                m = (0, 0)
            else:
                m = (data[mpos], data[mpos + 1])
                mpos += 2
            d = (data[dpos], data[dpos + 1])
            dpos += 2
            for b in range(2):
                for p in range(4):
                    shift = 6 - 2 * p
                    if (m[b] >> shift) & 3:
                        continue
                    img[first + r][w * 8 + b * 4 + p] = (d[b] >> shift) & 3
    return img
