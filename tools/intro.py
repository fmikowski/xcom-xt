# SPDX-License-Identifier: GPL-3.0-or-later
"""The intro movie and the end-game slides (cutscenes) for src/cut.c.

    python tools/intro.py <UFO data dir> <rules dir> <out dir> [--cga | --vga]
                          [--preview dir] [--all]

Writes INTROE.PAK / INTROC.PAK (the intro, UFOINTRO/UFOINT.FLI) and
SLIDESE.PAK / SLIDESC.PAK (the win and lose slides, UFOINTRO/PICTn.LBM with their
captions). Needs only the Python standard library (captions from the game's
GEODATA/ENGLISH.DAT, see gametext.py).

The intro (DOS / OpenXcom VideoState): 891 FLI frames, 320x200, one palette,
14 frames a second at first (FLI speed 5/70 s), the frame delays changed at
the frames of OpenXcom's introSoundTrack; the AdLib tracks GMINTRO1-3 start
at frames 0, 211 and 378; the last (ring) frame is not shown, the picture
before it stays 10 s, then the screen fades. Frames are dithered to the 16
EGA colours (tools/egacolor.py, 2x2 ordered: still areas stay still) or to
a CGA palette, then stored as deltas of the video memory bytes. A 10 MHz
8088 cannot show every frame where much changes: the converter plays the
movie against a cost model of the player (disk read and video writes, per
byte and per run, measured in 86Box) and leaves out the frames that would
be shown late; the next frame kept then carries their changes.

INTROx.PAK (little endian):
    "XIN1", u8 video (0 EGA, 1 CGA), u8 CGA palette (0-3: 0lo 0hi 1lo 1hi),
    u16 frames, u16 hold (1/70 s after the last frame), u16 buffer (bytes the
    player needs: the largest delta plus the next header), u16 source frames,
    u16 0
    then frame 0's header, and per frame: its data, the next frame's header
    (after the last frame a header of zeros).
    header (8 bytes): u16 size, u16 due (1/70 s from the start), u8 flags
    (1 = full screen, 2 = start music track `music`), u8 music (0-2),
    u8 CGA palette (0xFF = unchanged), u8 0
    full screen: EGA planes 0-3, 8000 bytes each; CGA the even and the odd
    line bank, 8000 bytes each (B800:0000, B800:2000)
    delta: per EGA plane (CGA: one plane, video offsets 0-16383): u16 runs,
    runs x (u8 skip, u8 n, n bytes): skip bytes after the previous run's
    end, then n bytes to copy (n 0: a longer skip continues)

SLIDESx.PAK (not ENDx.PAK: main.c takes 4-letter names for
mission packs):
    "XEN1", u8 slides, u8 win (the first `win` slides are the victory, the
    rest the defeat)
    slides x (u32 offset of the picture, u16 offset of the caption, u16 x,
    u16 y, u8 colour (EGA 0-15), u8 CGA palette, u16 0): 14 bytes each
    pictures as the intro's full screens; captions: NUL-terminated ASCII,
    lines (already wrapped to the box, OpenXcom Text with word wrap) split
    by '\\n', drawn in the small font 8 pixels apart.
"""

import argparse
import os
import struct

import egacolor
import gametext
import pedia
import png
import ufodata

W, H = 320, 200
JIFFY = 1000.0 / 70             # FLI frame delays are 1/70 s

# OpenXcom VideoState::introSoundTrack: frame delay changes (0x4nn: n/70 s)
# and the music (0x200-0x202: GMINTRO1-3), applied before the frame is shown.
SPEEDS = [(211, 7), (535, 5), (560, 7), (582, 5), (613, 7), (668, 1), (681, 6), (687, 2),
          (689, 7), (711, 7), (824, 10), (855, 7)]
MUSIC = [(0, 0), (211, 1), (378, 2)]
HOLD_MS = 10000                 # flcPlayer->delay(10000) after the last frame

# cutscenes.rul: winGame and loseGame slides (picture, caption, size, position).
WIN_SLIDES = [("PICT1.LBM", "STR_VICTORY_1", (195, 56), (5, 0)),
              ("PICT2.LBM", "STR_VICTORY_2", (232, 64), (88, 136)),
              ("PICT3.LBM", "STR_VICTORY_3", (254, 48), (66, 152)),
              ("PICT6.LBM", "STR_VICTORY_4", (300, 200), (5, 0)),
              ("PICT7.LBM", "STR_VICTORY_5", (310, 42), (5, 158))]
LOSE_SLIDES = [("PICT4.LBM", "STR_GAME_OVER_1", (190, 104), (0, 0)),
               ("PICT5.LBM", "STR_GAME_OVER_2", (200, 34), (32, 0))]
CAPTION_COLOUR = 249           # OpenXcom Text: font pixel v is drawn in colour + v
CGA_NAMES = ["0lo", "0hi", "1lo", "1hi"]


# ----------------------------------------------------------------------
# Sources


def fli_frames(path):
    """Yields (pixels, palette) per frame: 64000 palette indices."""
    with open(path, "rb") as f:
        d = f.read()
    pos = 128
    scr = bytearray(W * H)
    pal = [(0, 0, 0)] * 256
    while pos + 6 <= len(d):
        fs, ft = struct.unpack_from("<IH", d, pos)
        if fs < 6:
            break
        if ft != 0xF1FA:
            pos += fs
            continue
        nch = struct.unpack_from("<H", d, pos + 6)[0]
        cp = pos + 16
        for _ in range(nch):
            cs, ct = struct.unpack_from("<IH", d, cp)
            c = cp + 6
            if ct in (4, 11):                       # COLOR_256 / COLOR_64
                scale = 1 if ct == 4 else 4
                npk = struct.unpack_from("<H", d, c)[0]
                c += 2
                i = 0
                pal = list(pal)
                for _ in range(npk):
                    i += d[c]
                    n = d[c + 1] or 256
                    c += 2
                    for _ in range(n):
                        pal[i & 255] = tuple(min(255, v * scale) for v in d[c:c + 3])
                        c += 3
                        i += 1
            elif ct == 15:                          # BRUN
                for y in range(H):
                    c += 1
                    x = y * W
                    end = x + W
                    while x < end:
                        n = struct.unpack_from("b", d, c)[0]
                        c += 1
                        if n > 0:
                            scr[x:x + n] = bytes([d[c]]) * n
                            c += 1
                        else:
                            n = -n
                            scr[x:x + n] = d[c:c + n]
                            c += n
                        x += n
            elif ct == 12:                          # LC (delta)
                y0, nl = struct.unpack_from("<HH", d, c)
                c += 4
                for y in range(y0, y0 + nl):
                    npk = d[c]
                    c += 1
                    x = y * W
                    for _ in range(npk):
                        x += d[c]
                        n = struct.unpack_from("b", d, c + 1)[0]
                        c += 2
                        if n > 0:
                            scr[x:x + n] = d[c:c + n]
                            c += n
                        else:
                            n = -n
                            scr[x:x + n] = bytes([d[c]]) * n
                            c += 1
                        x += n
            elif ct == 13:                          # BLACK
                scr[:] = bytes(W * H)
            elif ct == 16:                          # COPY
                scr[:] = d[c:c + W * H]
            cp += cs
        yield bytes(scr), pal
        pos += fs


def schedule(n, speed):
    """Due time of each frame in 1/70 s."""
    out, t, si = [], 0, 0
    for k in range(n):
        while si < len(SPEEDS) and SPEEDS[si][0] <= k:
            speed = SPEEDS[si][1]
            si += 1
        if k:
            t += speed
        out.append(t)
    return out


def load_lbm(path):
    """IFF PBM (chunky, ByteRun1): (pixels, palette)."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"FORM" or d[8:12] not in (b"PBM ", b"ILBM"):
        raise ValueError(path + ": not an IFF picture")
    p = 12
    pal, body, w, h, comp = None, None, W, H, 0
    while p + 8 <= len(d):
        cid = d[p:p + 4]
        n = struct.unpack_from(">I", d, p + 4)[0]
        c = d[p + 8:p + 8 + n]
        if cid == b"BMHD":
            w, h = struct.unpack_from(">HH", c, 0)
            comp = c[10]
        elif cid == b"CMAP":
            pal = [tuple(c[i * 3:i * 3 + 3]) for i in range(len(c) // 3)]
        elif cid == b"BODY":
            body = c
        p += 8 + n + (n & 1)
    if d[8:12] != b"PBM " or (w, h) != (W, H):
        raise ValueError(path + ": only 320x200 PBM pictures are supported")
    if comp:
        out = bytearray()
        i = 0
        while i < len(body) and len(out) < w * h:
            b = body[i]
            i += 1
            if b < 128:
                out += body[i:i + b + 1]
                i += b + 1
            elif b > 128:
                out += bytes([body[i]]) * (257 - b)
                i += 1
        body = bytes(out)
    pal = (pal + [(0, 0, 0)] * 256)[:256]
    return bytes(body[:w * h]), pal


# ----------------------------------------------------------------------
# Video memory images


class Device:
    """Turns 320x200 palette pictures into video memory: EGA 4 planes of
    8000 bytes, CGA one 16384-byte image (even lines at 0, odd at 8192)."""

    def __init__(self, cga, pal, cga_pal=0, pen=0.25):
        self.cga = cga
        target = egacolor.CGA_PALETTES[CGA_NAMES[cga_pal]] if cga else None
        self.mp = egacolor.build_mapping(pal, noise_penalty=pen, target=target)
        bits = 2 if cga else 1
        # per plane (EGA) / pixel position, line parity, column parity:
        # palette index -> '0' / '1' digits of the device bytes.
        self.tab = []
        for p in range(1 if cga else 4):
            tp = []
            for yy in range(2):
                tr = []
                for xx in range(2):
                    if cga:
                        tr.append([bytes(egacolor.pixel_colour(self.mp, i, xx, yy) for i in range(256))])
                    else:
                        tr.append(bytes(48 + ((egacolor.pixel_colour(self.mp, i, xx, yy) >> p) & 1)
                                        for i in range(256)))
                tp.append(tr)
            self.tab.append(tp)
        del bits

    def image(self, px):
        if self.cga:
            out = bytearray(16384)
            buf = bytearray(W)
            for y in range(H):
                row = px[y * W:(y + 1) * W]
                t = self.tab[0][y & 1]
                buf[0::2] = row[0::2].translate(t[0][0])
                buf[1::2] = row[1::2].translate(t[1][0])
                # 4 pixels a byte, the left one in the top bits
                v = bytes(((buf[i] << 6) | (buf[i + 1] << 4) | (buf[i + 2] << 2) | buf[i + 3])
                          for i in range(0, W, 4))
                o = (y & 1) * 8192 + (y >> 1) * 80
                out[o:o + 80] = v
            return [bytes(out)]
        planes = []
        buf = bytearray(W)
        for p in range(4):
            pl = bytearray()
            for y in range(H):
                row = px[y * W:(y + 1) * W]
                t = self.tab[p][y & 1]
                buf[0::2] = row[0::2].translate(t[0])
                buf[1::2] = row[1::2].translate(t[1])
                pl += int(buf, 2).to_bytes(40, "big")
            planes.append(bytes(pl))
        return planes

    def rgb_rows(self, planes, cga_pal=0):
        """The picture as the card shows it (previews)."""
        rows = []
        for y in range(H):
            r = []
            for x in range(W):
                if self.cga:
                    b = planes[0][(y & 1) * 8192 + (y >> 1) * 80 + (x >> 2)]
                    c = (b >> (6 - 2 * (x & 3))) & 3
                    r.append(egacolor.CGA_PALETTES[CGA_NAMES[cga_pal]][c])
                else:
                    c = 0
                    for p in range(4):
                        if planes[p][y * 40 + (x >> 3)] & (0x80 >> (x & 7)):
                            c |= 1 << p
                    r.append(egacolor.EGA_RGB[c])
            rows.append(r)
        return rows


def mix_error(pal, mp, target, i):
    """How far the device's mix for palette index i is from its colour."""
    a, b, lv = mp[i]
    t = lv / 4
    mix = tuple(target[a][j] * (1 - t) + target[b][j] * t for j in range(3))
    return egacolor._dist(pal[i], mix)


def full_data(planes, cga):
    if cga:
        return planes[0][0:8000] + planes[0][8192:8192 + 8000]
    return b"".join(planes)


# ----------------------------------------------------------------------
# Deltas


GAP = 3                         # unchanged bytes copied rather than a new run


def diff_runs(a, b, row):
    """Runs (start, end) where b differs from a; gaps of up to GAP merged.
    row: compare row slices first (most rows do not change)."""
    out = []
    n = len(a)
    for r0 in range(0, n, row):
        r1 = min(n, r0 + row)
        if a[r0:r1] == b[r0:r1]:
            continue
        for i in range(r0, r1):
            if a[i] != b[i]:
                if out and i - out[-1][1] <= GAP:
                    out[-1][1] = i + 1
                else:
                    out.append([i, i + 1])
    return out


def encode_plane(a, b, row):
    """(data, runs, bytes) of the delta from a to b."""
    runs = diff_runs(a, b, row)
    out = bytearray()
    n = 0
    pos = 0
    for s, e in runs:
        skip = s - pos
        while skip > 255:
            out += b"\xff\x00"
            n += 1
            skip -= 255
        first = True
        while s < e:
            k = min(255, e - s)
            out += bytes((skip if first else 0, k)) + b[s:s + k]
            n += 1
            first = False
            s += k
        pos = e
    return struct.pack("<H", n) + bytes(out), n, sum(e - s for s, e in runs)


def decode_plane(cur, data, at):
    """Applies one plane's delta (checks the encoder); returns the end."""
    n = struct.unpack_from("<H", data, at)[0]
    at += 2
    pos = 0
    for _ in range(n):
        pos += data[at]
        k = data[at + 1]
        at += 2
        cur[pos:pos + k] = data[at:at + k]
        at += k
        pos += k
    return at


class Cost:
    """The player's time for a frame, ms (measured on 86Box, 10 MHz 8088)."""

    def __init__(self, a):
        self.frame = a.frame_ms
        self.read = a.read_us / 1000.0
        self.blit = a.blit_us / 1000.0
        self.run = a.run_us / 1000.0
        self.full_blit = a.full_ms

    def of(self, size, runs, nbytes, full):
        rd = self.frame + size * self.read
        if full:
            return rd + self.full_blit, 0.0     # read straight into video memory
        return rd, runs * self.run + nbytes * self.blit


def build_intro(root, cga, args, preview=None):
    path = ufodata.find_file(root, "UFOINTRO/UFOINT.FLI")
    with open(path, "rb") as f:
        hdr = f.read(128)
    speed = struct.unpack_from("<H", hdr, 16)[0] or 5
    frames = list(fli_frames(path))
    shown = len(frames) - 1             # the ring frame is not shown
    due = schedule(len(frames), speed)
    pal = frames[0][1]
    row = 80 if cga else 40
    nplanes = 1 if cga else 4
    cost = Cost(args)
    # CGA: each scene (cut where most of the picture changes) in the
    # palette that shows it best, as the other converted pictures; the
    # palette switches with the scene's first frame, a full screen.
    pal_of = [0] * shown
    starts = {}
    devs = {}
    if cga:
        devs = {cp: Device(True, pal, cp) for cp in range(4)}
        errs = {cp: [mix_error(pal, devs[cp].mp, egacolor.CGA_PALETTES[CGA_NAMES[cp]], i)
                     for i in range(256)] for cp in range(4)}
        scenes = [0]
        for k in range(1, shown):
            a, b = frames[k - 1][0][::13], frames[k][0][::13]
            if sum(1 for x, y in zip(a, b) if x != y) > len(a) // 2:
                scenes.append(k)
        scenes.append(shown)
        for s, e in zip(scenes, scenes[1:]):
            hist = [0] * 256
            for k in range(s, e, 3):
                for i in frames[k][0][::31]:
                    hist[i] += 1
            cp = min(range(4), key=lambda c: sum(h * errs[c][i] for i, h in enumerate(hist) if h))
            for k in range(s, e):
                pal_of[k] = cp
        for k in range(shown):
            if k == 0 or pal_of[k] != pal_of[k - 1]:
                starts[k] = pal_of[k]
        images = [devs[pal_of[k]].image(px) for k, (px, _) in enumerate(frames[:shown])]
    else:
        devs = {0: Device(False, pal)}
        images = [devs[0].image(px) for px, _ in frames[:shown]]
    music_at = dict(MUSIC)

    recs = []                            # (header fields, data)
    prev = [bytes(len(p)) for p in images[0]]
    ready = 0.0                          # ms when the player is free again
    late_max = 0.0
    dropped = 0
    for k in range(shown):
        forced = k == 0 or k in music_at or k == shown - 1 or k in starts
        img = images[k]
        parts, runs, nbytes = [], 0, 0
        for p in range(nplanes):
            d, r, b = encode_plane(prev[p], img[p], row)
            parts.append(d)
            runs += r
            nbytes += b
        data = b"".join(parts)
        if not nbytes and not forced:
            continue                     # nothing changes: nothing to show
        t_due = due[k] * JIFFY
        rd, bl = cost.of(len(data), runs, nbytes, False)
        frd, _ = cost.of(16000 if cga else 32000, 0, 0, True)
        full = k in starts or len(data) > args.max_delta or frd < rd + bl
        if full:
            data = full_data(img, cga)
            rd, bl = frd, 0.0
        show = max(t_due, ready + rd)
        if show - t_due > args.late_ms and not forced and not args.all:
            dropped += 1
            continue
        late_max = max(late_max, show - t_due)
        ready = show + bl
        flags = (1 if full else 0) | (2 if k in music_at else 0)
        recs.append(((len(data), due[k], flags, music_at.get(k, 0), starts.get(k, 0xFF)), data, k))
        prev = img
    hold = due[shown] - due[shown - 1] + round(HOLD_MS / JIFFY)
    bufsize = max([len(d) for h, d, k in recs if not h[2] & 1] + [0]) + 8
    out = bytearray(b"XIN1" + struct.pack("<BBHHHHH", 1 if cga else 0, pal_of[0], len(recs), hold,
                                          bufsize, len(frames), 0))
    hdr = lambda h: struct.pack("<HHBBBB", h[0], h[1], h[2], h[3], h[4], 0)
    out += hdr(recs[0][0])
    for i, (h, data, k) in enumerate(recs):
        out += data
        out += hdr(recs[i + 1][0]) if i + 1 < len(recs) else bytes(8)

    # Play the stream back: every kept frame must come out as converted.
    check_stream(bytes(out), images, [k for h, d, k in recs], cga)
    if preview:
        os.makedirs(preview, exist_ok=True)
        for h, d, k in recs[::max(1, len(recs) // 24)]:
            png.write_png(os.path.join(preview, "intro%03d.png" % k), W, H,
                          devs[pal_of[k]].rgb_rows(images[k], pal_of[k]))
    stats = {"frames": len(recs), "of": shown, "dropped": dropped,
             "palettes": sorted(starts.items()) if cga else None,
             "late_max_ms": round(late_max), "buffer": bufsize, "bytes": len(out),
             "seconds": round((due[shown - 1] + hold) / 70.0, 1)}
    return bytes(out), stats


def check_stream(data, images, kept, cga):
    nplanes = 1 if cga else 4
    n = struct.unpack_from("<H", data, 6)[0]
    at = 16
    cur = [bytearray(len(p)) for p in images[0]]
    for i in range(n):
        size, due_, flags = struct.unpack_from("<HHB", data, at)
        at += 8
        if flags & 1:
            if cga:
                cur[0][0:8000] = data[at:at + 8000]
                cur[0][8192:16192] = data[at + 8000:at + 16000]
            else:
                for p in range(4):
                    cur[p][:] = data[at + p * 8000:at + (p + 1) * 8000]
            at += size
        else:
            end = at
            for p in range(nplanes):
                end = decode_plane(cur[p], data, end)
            if end != at + size:
                raise AssertionError("frame %d: size %d, decoded %d" % (i, size, end - at))
            at = end
        want = images[kept[i]]
        for p in range(nplanes):
            a, b = bytes(cur[p]), want[p]
            if cga:
                a, b = a[:8000] + a[8192:16192], b[:8000] + b[8192:16192]
            if a != b:
                raise AssertionError("frame %d (source %d) differs" % (i, kept[i]))


# ----------------------------------------------------------------------
# End-game slides


def font_widths(root):
    data = ufodata.read(root, "GEODATA/SMALLSET.DAT")
    gw, gh = 8, 9
    per = gw * gh
    widths = {}
    for ch in range(32, 128):
        g = ch - 33
        cols = []
        if 0 <= g < len(data) // per:
            cols = [x for x in range(gw) if any(data[g * per + y * gw + x] for y in range(gh))]
        widths[chr(ch)] = 4 if ch == 32 or not cols else cols[-1] + 1
    return widths


def wrap(text, width, widths):
    """OpenXcom Text word wrap: words onto a line while they fit."""
    lines = []
    for para in text.split("\n"):
        line = ""
        for word in para.split(" "):
            cand = word if not line else line + " " + word
            if line and sum(widths.get(c, 4) for c in cand) > width:
                lines.append(line)
                line = word
            else:
                line = cand
        lines.append(line)
    return lines


def nearest(rgb, colours):
    return min(range(len(colours)), key=lambda i: egacolor._dist(rgb, colours[i]))


def build_intro_vga(root):
    """INTROV.PAK: the intro as UFOINT.FLI has it, for mode 13h (src/cutasm.asm
    cut_fli): the INTROx.PAK container (video 2), each frame's data the FLI
    frame's chunks after a u16 chunk count (COLOR, LC, BRUN, ...), every
    frame kept (a 286 at 8 MHz keeps up but for a few heavy frames:
    docs/design.md)."""
    path = ufodata.find_file(root, "UFOINTRO/UFOINT.FLI")
    with open(path, "rb") as f:
        d = f.read()
    speed = struct.unpack_from("<H", d, 16)[0] or 5
    raw, o = [], 128
    while o + 16 <= len(d):
        fsize, magic, chunks = struct.unpack_from("<IHH", d, o)
        if magic != 0xF1FA or fsize < 16:
            break
        raw.append(struct.pack("<H", chunks) + d[o + 16:o + fsize])
        o += fsize
    shown = len(raw) - 1                # the ring frame is not shown
    due = schedule(len(raw), speed)
    music_at = dict(MUSIC)
    recs = [((len(raw[k]), due[k], 2 if k in music_at else 0, music_at.get(k, 0), 0xFF), raw[k])
            for k in range(shown)]
    hold = due[shown] - due[shown - 1] + round(HOLD_MS / JIFFY)
    bufsize = max(len(x) for x in raw[:shown]) + 8
    out = bytearray(b"XIN1" + struct.pack("<BBHHHHH", 2, 0, len(recs), hold, bufsize, len(raw), 0))
    hdr = lambda h: struct.pack("<HHBBBB", h[0], h[1], h[2], h[3], h[4], 0)
    out += hdr(recs[0][0])
    for i, (h, data) in enumerate(recs):
        out += data
        out += hdr(recs[i + 1][0]) if i + 1 < len(recs) else bytes(8)
    return bytes(out), {"frames": len(recs), "buffer": bufsize, "bytes": len(out)}


def build_end(root, rules_dir, cga, preview=None, vga=False):
    """The slides' pack. VGA (SLIDESV.PAK): each picture is its 768 DAC
    bytes (6 bits) and 64000 bytes a pixel, the caption's colour the
    palette's index of the text's body (OpenXcom: colour + 1)."""
    lang = gametext.lang(root)
    widths = font_widths(root)
    slides = WIN_SLIDES + LOSE_SLIDES
    head = bytearray(b"XEN1" + bytes((len(slides), len(WIN_SLIDES))))
    table_at = len(head)
    head += bytes(14 * len(slides))
    texts = bytearray()
    pics = []
    recs = []
    for name, sid, (bw, bh), (bx, by) in slides:
        px, pal = load_lbm(ufodata.find_file(root, "UFOINTRO/" + name))
        if vga:
            lines = wrap(pedia.ascii_text(lang.get(sid, sid)), bw, widths)
            lines = lines[:max(1, (H - by) // 8)]
            recs.append((len(texts), bx, by, CAPTION_COLOUR + 1, 0))
            texts += "\n".join(lines).encode("ascii") + b"\0"
            pics.append(bytes(c >> 2 for rgb in pal for c in rgb) + bytes(px[:W * H]))
            continue
        if cga:
            # The CGA palette that shows the picture best.
            best = None
            for cp in range(4):
                d = Device(True, pal, cp, pen=0.35)
                tgt = egacolor.CGA_PALETTES[CGA_NAMES[cp]]
                sample = px[::97]
                err = sum(mix_error(pal, d.mp, tgt, i) * sample.count(i) for i in set(sample))
                if best is None or err < best[0]:
                    best = (err, cp, d)
            _, cp, dev = best
            # uk_text takes colours 0-3 as CGA colours: the nearest one
            # that is not black.
            tgt = egacolor.CGA_PALETTES[CGA_NAMES[cp]]
            colour = 1 + nearest(pal[CAPTION_COLOUR + 1], tgt[1:])
        else:
            cp = 0
            dev = Device(False, pal, pen=0.35)
            colour = nearest(pal[CAPTION_COLOUR + 1], egacolor.EGA_RGB)
        img = dev.image(px)
        lines = wrap(pedia.ascii_text(lang.get(sid, sid)), bw, widths)
        # OpenXcom clips the caption to its box; DOS shows it all (the
        # box is a little small for our glyph widths): keep it on the screen.
        lines = lines[:max(1, (H - by) // 8)]
        recs.append((len(texts), bx, by, colour, cp))
        texts += "\n".join(lines).encode("ascii") + b"\0"
        pics.append(full_data(img, cga))
        if preview:
            os.makedirs(preview, exist_ok=True)
            png.write_png(os.path.join(preview, name.replace(".LBM", ".png").lower()), W, H,
                          dev.rgb_rows(img, cp))
    text_at = len(head)
    out = head + texts
    for i, (toff, bx, by, colour, cp) in enumerate(recs):
        struct.pack_into("<IHHHBB", out, table_at + 14 * i, len(out), text_at + toff, bx, by,
                         colour, cp)
        out += pics[i]
    return bytes(out), {"slides": len(slides), "bytes": len(out)}


def parser():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data")
    ap.add_argument("rules")
    ap.add_argument("outdir")
    ap.add_argument("--cga", action="store_true")
    ap.add_argument("--vga", action="store_true", help="INTROV.PAK and SLIDESV.PAK (src/cut.c)")
    ap.add_argument("--preview", help="write PNGs of some frames and the slides there")
    ap.add_argument("--all", action="store_true", help="keep every frame (timing tests)")
    # Cost model of src/cut.c on a 10 MHz 8088 (86Box xt88adl, XT-IDE hard
    # disk; INTROTIME's INTRO.TXT fitted: read 3.9 ms + 4.2 us a byte, video
    # 0.2 ms + 8 us a run + 2.3 us a byte, a full screen 185 ms).
    ap.add_argument("--frame-ms", type=float, default=4.0, help="per frame")
    ap.add_argument("--read-us", type=float, default=4.2, help="disk read per byte")
    ap.add_argument("--blit-us", type=float, default=2.4, help="video write per byte")
    ap.add_argument("--run-us", type=float, default=7.8, help="per run")
    ap.add_argument("--full-ms", type=float, default=47.0, help="extra for a full screen")
    ap.add_argument("--late-ms", type=float, default=40.0, help="lateness allowed")
    ap.add_argument("--max-delta", type=int, default=16000, help="larger: a full screen")
    return ap


def convert(args):
    if args.vga:                # the FLI's own frames and the slides in 256 colours
        data, stats = build_intro_vga(args.data)
        with open(os.path.join(args.outdir, "INTROV.PAK"), "wb") as f:
            f.write(data)
        print("INTROV.PAK", stats)
        data, stats = build_end(args.data, args.rules, False, None, vga=True)
        with open(os.path.join(args.outdir, "SLIDESV.PAK"), "wb") as f:
            f.write(data)
        print("SLIDESV.PAK", stats)
        return
    v = "C" if args.cga else "E"
    data, stats = build_intro(args.data, args.cga, args, args.preview)
    with open(os.path.join(args.outdir, "INTRO%s.PAK" % v), "wb") as f:
        f.write(data)
    print("INTRO%s.PAK" % v, stats)
    data, stats = build_end(args.data, args.rules, args.cga, args.preview)
    with open(os.path.join(args.outdir, "SLIDES%s.PAK" % v), "wb") as f:
        f.write(data)
    print("SLIDES%s.PAK" % v, stats)


def main():
    convert(parser().parse_args())


if __name__ == "__main__":
    main()