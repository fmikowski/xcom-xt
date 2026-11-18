# SPDX-License-Identifier: GPL-3.0-or-later
"""Converts the original X-COM sound effects and AdLib music into packs.

    python tools/sound.py <UFO data dir> <out dir> [--pcm] [--stats]

Writes SOUNDS.PAK (digitised effects for the Sound Blaster) and MUSIC.PAK
(AdLib tracks in the game's own format, played by src/music.c).

Sources (DOS version, see https://www.ufopaedia.org/index.php/SOUND):
    SOUND/SOUND1.CAT  battlescape effects     SOUND/SOUND2.CAT  geoscape effects
    SOUND/ADLIB.CAT   AdLib music
CAT files start with (u32 offset, u32 size) per entry. A DOS sound entry is
a 0 byte (empty name), 5 header bytes (u8 pages, u16 length, u16 length),
then 6-bit unsigned samples at 8 kHz and a trailing 0.

SOUNDS.PAK (little endian), mirrored by src/sound.c:
    "XSN1", u16 count
    count x (u32 offset, u16 bytes, u16 rate, u8 format, u8 pad)
    sample data; format 0 = 8-bit unsigned PCM, 1 = Creative 4-bit ADPCM
    (first byte = reference sample, then two samples per byte, high nibble
    first: Sound Blaster DSP command 75h)

MUSIC.PAK:
    "XMU1", u16 count, count x (u32 offset, u16 bytes), the ADLIB.CAT entries
"""

import argparse
import os
import struct

# Effects in the order of the SFX_* enum in src/sound.h:
# (name, cat, index, longest ms or None, rate)
EFFECTS = [
    ("rifle",        1, 4,  450, 8000),
    ("cannon",       1, 12, 450, 8000),
    ("laser",        1, 11, 450, 8000),
    ("plasma",       1, 18, 500, 8000),
    ("hit bullet",   1, 22, 300, 8000),
    ("hit beam",     1, 19, 350, 8000),
    ("explosion",    1, 0,  900, 6000),
    ("die sectoid",  1, 10, 800, 6000),
    ("die snakeman", 1, 8,  700, 6000),
    ("die muton",    1, 6,  800, 6000),
    ("die ethereal", 1, 9,  800, 6000),
    ("die male",     1, 41, 600, 6000),
    ("die female",   1, 44, 600, 6000),
    ("door",         1, 3,  350, 6000),
    ("step",         1, 26, 80,  8000),
    ("throw",        1, 39, 300, 6000),
    ("geo popup",    2, 3,  250, 8000),
    ("craft cannon", 2, 4,  450, 8000),
    ("craft missile", 2, 5, 800, 6000),
    ("ufo fire",     2, 8,  500, 8000),
    ("ufo hit",      2, 12, 500, 6000),
    ("ufo crash",    2, 10, 900, 6000),
    ("craft explode", 2, 13, 900, 6000),
]

# MUSIC.PAK tracks in the order of the MUS_* enum (ADLIB.CAT entries, as in
# OpenXcom's music.rul): GMGEO1, GMGEO2, GMTACTIC, GMINTER, GMWIN, GMLOSE,
# GMMARS, then the intro's GMINTRO1-3 (catPos 19-21: past ADLIB.CAT's 19
# entries, OpenXcom reads them from AINTRO.CAT; the DOS intro's own copy is
# UFOINTRO/ADLIB.CAT).
TRACKS = [("geo1", 0), ("geo2", 18), ("tactic", 17), ("inter", 2),
          ("win", 11), ("lose", 10), ("mars", 9),
          ("intro1", 19), ("intro2", 20), ("intro3", 21)]

FADE_MS = 30


def cat_entries(path):
    with open(path, "rb") as f:
        data = f.read()
    n = struct.unpack_from("<I", data, 0)[0] // 8
    out = []
    for i in range(n):
        off, size = struct.unpack_from("<II", data, i * 8)
        out.append(data[off:off + size])
    return out


def find_file(root, name):
    """Case-insensitive lookup of SOUND/<name> (CD versions use lower case)."""
    d = os.path.join(root, "SOUND")
    for f in os.listdir(d):
        if f.upper() == name.upper():
            return os.path.join(d, f)
    raise FileNotFoundError(os.path.join(d, name))


def dos_samples(entry):
    """8-bit unsigned samples (centre 128) of a DOS sound entry."""
    if len(entry) < 8:
        return []
    l1, l2 = struct.unpack_from("<HH", entry, 2)
    raw = entry[6:-1]
    n = min(len(raw), max(l1, l2) or len(raw))
    return [min(255, v * 4) for v in raw[:n]]


def trim(s, rate, max_ms):
    """Cuts the quiet tail (and anything past max_ms), fading out the end."""
    end = 0
    for i, v in enumerate(s):
        if abs(v - 128) >= 12:
            end = i + 1
    if max_ms:
        end = min(end, rate * max_ms // 1000)
    s = s[:end]
    fade = min(len(s), rate * FADE_MS // 1000)
    for k in range(fade):
        i = len(s) - fade + k
        s[i] = 128 + (s[i] - 128) * (fade - k) // (fade + 1)
    return s


def resample(s, src, dst):
    if src == dst:
        return list(s)
    # 3-tap low-pass, then linear interpolation.
    f = [s[0]] + [(s[i - 1] + 2 * s[i] + s[i + 1]) // 4 for i in range(1, len(s) - 1)] + [s[-1]]
    n = len(s) * dst // src
    out = []
    for j in range(n):
        p = j * src / dst
        i = int(p)
        t = p - i
        a = f[i]
        b = f[i + 1] if i + 1 < len(f) else a
        out.append(int(round(a + (b - a) * t)))
    return out


# Creative 4-bit ADPCM, as decoded by the Sound Blaster DSP (tables as in
# DOSBox and 86Box).
SCALE4 = [0, 1, 2, 3, 4, 5, 6, 7, 0, -1, -2, -3, -4, -5, -6, -7,
          1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15,
          2, 6, 10, 14, 18, 22, 26, 30, -2, -6, -10, -14, -18, -22, -26, -30,
          4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60]
ADJUST4 = [0, 0, 0, 0, 0, 16, 16, 16] * 2 + [-16, 0, 0, 0, 0, 16, 16, 16] * 4 + \
          [-16, 0, 0, 0, 0, 0, 0, 0] * 2


def adpcm_step(ref, step, nib):
    i = max(0, min(63, nib + step))
    ref = max(0, min(255, ref + SCALE4[i]))
    return ref, step + ADJUST4[i]


def adpcm_encode(s, width=16):
    """Reference byte + packed nibbles. Beam search: keeps the `width` decoder
    states with the least squared error so far (one path per state)."""
    paths = [(0, s[0], 0, None)]            # error, ref, step, (nibble, parent)
    for t in s[1:]:
        best = {}
        for err, ref, step, hist in paths:
            for n in range(16):
                r, st = adpcm_step(ref, step, n)
                e = err + (r - t) * (r - t)
                k = (r, st)
                if k not in best or best[k][0] > e:
                    best[k] = (e, r, st, (n, hist))
        paths = sorted(best.values(), key=lambda p: p[0])[:width]
    nibs = []
    hist = paths[0][3]
    while hist:
        nibs.append(hist[0])
        hist = hist[1]
    nibs.reverse()
    if len(nibs) & 1:
        nibs.append(0)
    out = bytearray([s[0]])
    for k in range(0, len(nibs), 2):
        out.append((nibs[k] << 4) | nibs[k + 1])
    return bytes(out)


def adpcm_decode(data):
    ref, step = data[0], 0
    out = [ref]
    for b in data[1:]:
        for nib in (b >> 4, b & 15):
            ref, step = adpcm_step(ref, step, nib)
            out.append(ref)
    return out


def snr_db(a, b):
    import math
    n = min(len(a), len(b))
    sig = sum((a[i] - 128) ** 2 for i in range(n)) or 1
    err = sum((a[i] - b[i]) ** 2 for i in range(n)) or 1
    return 10 * math.log10(sig / err)


def build_sounds(root, pcm=False):
    cats = {1: cat_entries(find_file(root, "SOUND1.CAT")),
            2: cat_entries(find_file(root, "SOUND2.CAT"))}
    entries = []
    report = []
    for name, cat, idx, max_ms, rate in EFFECTS:
        s = resample(dos_samples(cats[cat][idx]), 8000, rate)
        s = trim(s, rate, max_ms)
        if pcm:
            data, fmt, q = bytes(s), 0, 99.0
        else:
            data, fmt = adpcm_encode(s), 1
            q = snr_db(s, adpcm_decode(data))
        entries.append((data, rate, fmt))
        report.append("%-14s cat%d #%-2d %5d ms %5d bytes %4.1f dB" % (
            name, cat, idx, len(s) * 1000 // rate, len(data), q))
    out = bytearray(b"XSN1" + struct.pack("<H", len(entries)))
    off = len(out) + 10 * len(entries)
    for data, rate, fmt in entries:
        out += struct.pack("<IHHBB", off, len(data), rate, fmt, 0)
        off += len(data)
    for data, _, _ in entries:
        out += data
    return bytes(out), report


def build_music(root):
    cat = cat_entries(find_file(root, "ADLIB.CAT"))
    try:
        intro = cat_entries(find_file(root, "AINTRO.CAT"))
    except FileNotFoundError:
        try:
            intro = cat_entries(os.path.join(os.path.dirname(find_file(root, "ADLIB.CAT")),
                                             "..", "UFOINTRO", "ADLIB.CAT"))
        except OSError:
            intro = []
    cat = cat + intro
    datas = [cat[i] if i < len(cat) else b"" for _, i in TRACKS]
    out = bytearray(b"XMU1" + struct.pack("<H", len(datas)))
    off = len(out) + 6 * len(datas)
    for d in datas:
        out += struct.pack("<IH", off, len(d))
        off += len(d)
    for d in datas:
        out += d
    return bytes(out), datas


def music_stats(data, seconds=180, hz=72.8, voices=9):
    """Runs the X-COM AdLib driver logic (as in src/music.c) and counts the
    OPL register writes per second (the CPU cost on the target)."""
    p = 0
    tempo = data[p]; p += 1
    nsamp = data[p]; p += 1 + nsamp * 24
    nsub = data[p]; p += 1
    subs = []
    for _ in range(nsub):
        ln = struct.unpack_from("<H", data, p)[0]
        subs.append(p + 4)
        p += ln
    ninst = data[p]; p += 1
    starts = [None] * 16
    for i in range(ninst):
        ln = struct.unpack_from("<H", data, p)[0]
        if i < 16:
            starts[i] = p + 4
        p += ln
        if p >= len(data):
            break

    def numseq(pos):
        v = 0
        while True:
            c = data[pos]; pos += 1
            v = (v << 7) + (c & 0x7F)
            if not c & 0x80:
                return v, pos

    writes = [0]
    notes = [0]
    state = {}

    def init():
        for i in range(16):
            st = {"addr": None, "delay": 0, "prev": 0, "ret": None}
            if starts[i] is not None:
                st["delay"], st["addr"] = numseq(starts[i])
            state[i] = st

    chan = [{"note": 0, "dur": 0, "inst": -1} for _ in range(voices)]
    init()
    tempo = int(tempo * 0.4)
    run, inc = tempo, int(round(70 * hz / 70.1))
    playing = True
    loops = 0
    order = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 9]
    chorus = [0] * 16

    def note_on(inst, n):
        for c in chan:
            c["dur"] += 1
        best = max(range(voices), key=lambda k: chan[k]["dur"])
        for k in range(voices):
            if chan[k]["note"] == 0:
                best = k
                break
        new_sample = chan[best]["inst"] != inst
        chan[best].update(note=n, dur=0, inst=inst)
        writes[0] += 13 if new_sample else 4
        notes[0] += 1

    def note_off(inst, n):
        for c in chan:
            if c["note"] == n and c["inst"] == inst:
                c["note"] = 0
                writes[0] += 1

    def decode(i):
        nonlocal playing, tempo
        st = state[i]
        pos = st["addr"]
        another = False
        while True:
            op = data[pos]; pos += 1
            # A call or return is followed by a delay at the new position.
            if op == 0xFE:
                st["ret"] = pos + 1
                pos = subs[data[pos]]
            elif op == 0xFD:
                if st["ret"] is not None:
                    pos, st["ret"] = st["ret"], None
            elif op == 0xFF:
                playing = False
                return 0, another
            elif op >= 0x80:
                st["prev"] = op
                op = data[pos]; pos += 1
            if op < 0x80:
                a1, cmd = op, st["prev"] & 0xF0
                if cmd in (0x80, 0x90):
                    a2 = data[pos]; pos += 1
                    if cmd == 0x90 and a2:
                        note_on(i, a1)
                        if chorus[i]:
                            note_on(chorus[i], a1)
                    else:
                        note_off(i, a1)
                        if chorus[i]:
                            note_off(chorus[i], a1)
                elif cmd == 0xB0:
                    a2 = data[pos]; pos += 1
                    if a1 == 0 and a2:
                        tempo = int(a2 * 0.8)
                    elif a1 == 0x7E:
                        chorus[i] = a2 - 1
                    elif a1 == 0x7F:
                        chorus[i] = 0
                elif cmd == 0xC0 and a1 == 0x7E:
                    another = True
                elif cmd == 0xE0:
                    writes[0] += 2
            d, pos = numseq(pos)
            if d:
                st["addr"] = pos
                return d, another

    ticks = int(seconds * hz)
    for _ in range(ticks):
        if not playing:
            break
        run -= tempo
        if run > 0:
            continue
        run += inc
        again = True
        while again:
            again = False
            for i in order:
                st = state[i]
                if st["addr"] is None:
                    continue
                if st["delay"] == 0:
                    st["delay"], a = decode(i)
                    again |= a
                    if not playing:
                        break
                st["delay"] -= 1
            if again:
                loops += 1
                init()
                for c in chan:
                    c["note"] = 0
                break
    return {"writes_per_s": writes[0] / seconds, "notes_per_s": notes[0] / seconds,
            "ended": not playing, "loops": loops}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("data")
    ap.add_argument("outdir")
    ap.add_argument("--pcm", action="store_true", help="8-bit PCM instead of ADPCM")
    ap.add_argument("--stats", action="store_true", help="print music driver load")
    args = ap.parse_args(argv)

    data, report = build_sounds(args.data, args.pcm)
    with open(os.path.join(args.outdir, "SOUNDS.PAK"), "wb") as f:
        f.write(data)
    print("\n".join(report))
    print("SOUNDS.PAK", len(data), "bytes")
    data, tracks = build_music(args.data)
    with open(os.path.join(args.outdir, "MUSIC.PAK"), "wb") as f:
        f.write(data)
    print("MUSIC.PAK", len(data), "bytes:",
          ", ".join("%s %d" % (n, len(d)) for (n, _), d in zip(TRACKS, tracks)))
    if args.stats:
        for (n, _), d in zip(TRACKS, tracks):
            print(n, music_stats(d))


if __name__ == "__main__":
    main()
