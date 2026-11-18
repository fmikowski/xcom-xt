# SPDX-License-Identifier: GPL-3.0-or-later
"""Encodes 32x40 frames into the engine's VGA sprite formats (mode 13h, one
byte a pixel, the original palette index; src/vgaasm.asm blits them).

Made for the 286: each row is a list of opaque spans copied with rep movsw
into the 320-byte rows of the back buffer, so no masks and no per-pixel
tests; a span's data is laid out so that its word reads are aligned when
its pixels land on an even column (the sprite itself starts at an even
offset: every encoded sprite has an even length).

Masked format (any sprite):
    u8 first_row, u8 row_count
    per row: u8 n_spans, then n_spans x
        u8 x | pad << 7    first column (0..31); pad: one filler byte comes
                           before the data
        u8 len             pixels (1..32)
        [u8 pad], u8 data[len]
    (a filler byte at the end makes the length even)

OR format (ground floors, which never overlap each other and are drawn on
a cleared area, so the zeros inside a row may be copied as well):
    u8 first_row, u8 row_count
    per row: u8 x (even), u8 words, u16 data[words]
The row's span is rounded out to even columns: floors lie inside X-COM's
floor diamond, whose rows start and end on even columns
(egasprite._diamond_row_span), so the rounding never leaves it.
"""

import struct

W, H = 32, 40


def _rows(frame, mapping):
    out = []
    for line in frame:
        out.append([mapping[c] if c else 0 for c in line[:W]])
    return out


def _bounds(frame):
    nonempty = [y for y, line in enumerate(frame) if any(line[:W])]
    if not nonempty:
        return 0, 0
    return nonempty[0], nonempty[-1] - nonempty[0] + 1


def _spans(line):
    spans = []
    x = 0
    while x < W:
        if not line[x]:
            x += 1
            continue
        s = x
        while x < W and line[x]:
            x += 1
        spans.append((s, x - s))
    return spans


def _opaque(frame):
    return {(x, y) for y, line in enumerate(frame[:H]) for x, c in enumerate(line[:W]) if c}


def encode(frame, mapping):
    """mapping: palette index -> pixel byte (identity for the battlescape
    palette); index 0 is transparent. Checks that the result decodes to
    exactly the frame's opaque pixels."""
    out = _encode(frame, mapping)
    dec = decode(out)
    got = {(x, y) for y in range(H) for x in range(W) if dec[y][x] >= 0}
    if got != _opaque(frame):
        raise AssertionError("vgasprite.encode: %d pixels in, %d out" % (len(_opaque(frame)), len(got)))
    return out


def _encode(frame, mapping):
    first, count = _bounds(frame)
    out = bytearray((first, count))
    for y in range(first, first + count):
        src = frame[y]
        line = _rows([src], mapping)[0]
        # transparent where the frame has index 0 (a mapped 0 stays opaque)
        spans = _spans([1 if c else 0 for c in src[:W]])
        out.append(len(spans))
        for x, n in spans:
            # data at an even offset for an even column, odd for odd
            data_at = len(out) + 2
            pad = 1 if (data_at & 1) != (x & 1) else 0
            out += bytes((x | pad << 7, n))
            if pad:
                out.append(0)
            out += bytes(line[x:x + n])
    if len(out) & 1:
        out.append(0)
    return bytes(out)


def encode_or(frame, mapping):
    """Checks that every opaque pixel of the frame is in the result (the
    zeros between them may be too)."""
    out = _encode_or(frame, mapping)
    dec = decode_or(out)
    if any(dec[y][x] != mapping[frame[y][x]] for x, y in _opaque(frame)):
        raise AssertionError("vgasprite.encode_or: pixels lost")
    return out


def _encode_or(frame, mapping):
    first, count = _bounds(frame)
    out = bytearray((first, count))
    for y in range(first, first + count):
        src = frame[y][:W]
        line = _rows([src], mapping)[0]
        xs = [x for x in range(W) if src[x]]
        if not xs:
            out += bytes((0, 0))
            continue
        a, b = xs[0] & ~1, xs[-1] | 1
        out += bytes((a, (b - a + 1) // 2))
        out += bytes(line[a:b + 1])
    return bytes(out)


def decode(data):
    """Inverse of encode: rows of pixel bytes, -1 = clear. For tests."""
    img = [[-1] * W for _ in range(H)]
    first, count = data[0], data[1]
    p = 2
    for r in range(count):
        n = data[p]
        p += 1
        for _ in range(n):
            x, ln = data[p] & 0x7F, data[p + 1]
            pad = data[p] >> 7
            p += 2 + pad
            for i in range(ln):
                img[first + r][x + i] = data[p + i]
            p += ln
    return img


def decode_or(data):
    """Inverse of encode_or (0 where nothing is set). For tests."""
    img = [[0] * W for _ in range(H)]
    first, count = data[0], data[1]
    p = 2
    for r in range(count):
        x, n = data[p], data[p + 1]
        p += 2
        for i in range(2 * n):
            img[first + r][x + i] = data[p + i]
        p += 2 * n
    return img


def _self_test():
    import random
    rnd = random.Random(1)
    ident = list(range(256))
    for _ in range(200):
        fr = [[rnd.choice((0, 0, rnd.randrange(1, 256))) for _ in range(W)] for _ in range(H)]
        for y in range(rnd.randrange(0, 10)):
            fr[y] = [0] * W
        enc = encode(fr, ident)
        assert len(enc) % 2 == 0
        dec = decode(enc)
        for y in range(H):
            for x in range(W):
                assert dec[y][x] == (fr[y][x] if fr[y][x] else -1), (x, y)
        # data alignment: each span's data starts on the column's parity
        first, count = enc[0], enc[1]
        p = 2
        for r in range(count):
            n = enc[p]
            p += 1
            for _ in range(n):
                x, ln, pad = enc[p] & 0x7F, enc[p + 1], enc[p] >> 7
                p += 2 + pad
                assert (p & 1) == (x & 1)
                p += ln
    print("vgasprite: ok")


if __name__ == "__main__":
    _self_test()
