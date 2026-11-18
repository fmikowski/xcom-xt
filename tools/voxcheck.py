# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks the engine's voxel shots (src/vox.c) against a replay in Python.

    python tools/voxcheck.py MAPDUMP.BIN LOG.TXT PACK [--data UFO]

Run the engine with SHOTLOG (and a LOG file): it writes MAPDUMP.BIN (map
size, the pack's MCD records, the tiles) at the start of the battle and
logs every shot ("SHOT T<n> u<i> x,y,z org ox,oy,oz aim ax,ay,az acc a =>
r at hx,hy,hz") and every terrain change ("TILE ..."). PACK is the mission
pack the battle used; its "XLF1" section holds the voxel shapes.

Each shot is replayed with a copy of OpenXcom's TileEngine::calculateLine
and voxelCheck written from OpenXcom's source (terrain only: the log has no
unit positions): a terrain hit must be the same part at the same voxel, a
unit hit must lie on the line before any terrain, a shot out of the map
must meet no terrain. With --data the pack's voxel shapes are compared
with the original MCD records' LOFT layers and LOFTEMPS.DAT.
"""

import argparse
import re
import struct
import sys

V_EMPTY, V_FLOOR, V_WESTWALL, V_NORTHWALL, V_OBJECT, V_UNIT, V_OUT = -1, 0, 1, 2, 3, 4, 5
MCD_NO_FLOOR, MCD_GRAVLIFT = 2, 32


def load_dump(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"XMD1":
        raise ValueError("not a map dump")
    mx, my, mz = data[4], data[5], data[6]
    n_mcd = data[7] | data[8] << 8
    mcd, off = [], 9
    for i in range(n_mcd):
        (sprite, tile_type, p_level, t_level, tu_walk, tu_slide, tu_fly, flags, armor,
         die, alt, special, burn, or_sprite) = struct.unpack_from("<HBBbBBBBBBBBBH", data, off)
        mcd.append({"t_level": t_level, "flags": flags, "tu_walk": tu_walk, "armor": armor, "vcol": tu_slide})
        off += 16
    tiles = [list(data[off + i * 4:off + i * 4 + 4]) for i in range(mx * my * mz)]
    return mx, my, mz, mcd, tiles


def load_xlf(path):
    """The XLF1 blob from the pack's trailer: col[n], cols, templates."""
    with open(path, "rb") as f:
        data = f.read()
    if data[-4:] != b"XTR1":
        raise ValueError("pack without trailer")
    ln = struct.unpack_from("<H", data, len(data) - 6)[0]
    pos = len(data) - 6 - ln
    while pos < len(data) - 6:
        magic = data[pos:pos + 4]
        if magic == b"XCU1":
            pos += 20
            continue
        n = struct.unpack_from("<H", data, pos + 4)[0]
        if magic == b"XMM1":
            pos += 6 + n * (6 if data[21] else 10)      # header byte 21: video (CGA 1)
            continue
        if magic in (b"XHB1", b"XFM1"):
            pos += 6 + n
            continue
        if magic == b"XLT1":
            pos += 7 + 2 * n
            continue
        if magic == b"XEX1":
            pos += 6 + 2 * n
            continue
        if magic == b"XLF1":
            size = struct.unpack_from("<H", data, pos + 6)[0]
            vcol = [data[64 + i * 16 + 6] for i in range(n)]    # Mcd.vcol (header 64 bytes)
            return vcol, data[pos + 8:pos + 8 + size]
        break
    raise ValueError("no XLF1 section")


class Shapes:
    def __init__(self, vcol, blob):
        self.o_tmask, self.o_tval, self.o_gen, self.o_rows = struct.unpack_from("<4H", blob, 0)
        self.blob, self.vcol = blob, vcol

    def col(self, rec):
        """The record's 12 layer templates from its column's runs."""
        b = self.blob
        p = struct.unpack_from("<H", b, 8 + 4 * self.vcol[rec])[0]
        out = []
        while len(out) < 12:
            last, tpl = b[p], b[p + 1]
            out += [tpl] * (last + 1 - len(out))
            p += 2
        return out

    def bit(self, t, x, y):
        b = self.blob
        if not struct.unpack_from("<H", b, self.o_tmask + 2 * t)[0] & (1 << y):
            return 0
        v = b[self.o_tval + t]
        if v & 0x80:
            v = b[self.o_gen + (v & 0x7F) * 16 + y]
        return (struct.unpack_from("<H", b, self.o_rows + 2 * v)[0] >> (15 - x)) & 1

    def rows(self, t):
        return [sum(self.bit(t, x, y) << (15 - x) for x in range(16)) for y in range(16)]


class World:
    def __init__(self, dump, shapes):
        self.mx, self.my, self.mz, self.mcd, self.tiles = dump
        self.s = shapes

    def tile(self, x, y, z):
        return self.tiles[(z * self.my + y) * self.mx + x]

    def check(self, vx, vy, vz):
        """voxelCheck without units."""
        if vx < 0 or vy < 0 or vz < 0:
            return V_OUT
        x, y, z = vx // 16, vy // 16, vz // 24
        if x >= self.mx or y >= self.my or z >= self.mz:
            return V_OUT
        t = self.tile(x, y, z)
        lz = vz % 24
        if lz < 2 and t[0] and self.mcd[t[0]]["flags"] & MCD_GRAVLIFT:
            b = self.tile(x, y, z - 1) if z else None
            if not b or not b[0] or not self.mcd[b[0]]["flags"] & MCD_GRAVLIFT:
                return V_FLOOR
        for p in range(4):
            if t[p] and self.s.bit(self.s.col(t[p])[lz // 2], vx % 16, vy % 16):
                return p
        return V_EMPTY


def line(w, o, t, ext):
    """TileEngine::calculateLine with voxel checks: (result, hit, path)."""
    x0, y0, z0 = o
    x1, y1, z1 = t
    if ext:
        m = max(abs(x1 - x0), abs(y1 - y0), abs(z1 - z0))
        if m:
            n = (1700 + m - 1) // m
            x1, y1, z1 = x0 + (x1 - x0) * n, y0 + (y1 - y0) * n, z0 + (z1 - z0) * n
    sxy = abs(y1 - y0) > abs(x1 - x0)
    if sxy:
        x0, y0, x1, y1 = y0, x0, y1, x1
    sxz = abs(z1 - z0) > abs(x1 - x0)
    if sxz:
        x0, z0, x1, z1 = z0, x0, z1, x1
    dx, dy, dz = abs(x1 - x0), abs(y1 - y0), abs(z1 - z0)
    dxy = dxz = dx // 2
    stx, sty, stz = (-1 if x0 > x1 else 1), (-1 if y0 > y1 else 1), (-1 if z0 > z1 else 1)
    path = []

    def voxel(x, y, z):
        if sxz:
            x, z = z, x
        if sxy:
            x, y = y, x
        path.append((x, y, z))
        return w.check(x, y, z)
    y, z, x = y0, z0, x0
    while True:
        r = voxel(x, y, z)
        if r != V_EMPTY:
            return r, path[-1], path
        if x == x1:
            return V_EMPTY, path[-1], path
        dxy -= dy
        dxz -= dz
        if dxy < 0:
            y += sty
            dxy += dx
            r = voxel(x, y, z)
            if r != V_EMPTY:
                return r, path[-1], path
        if dxz < 0:
            z += stz
            dxz += dx
            r = voxel(x, y, z)
            if r != V_EMPTY:
                return r, path[-1], path
        x += stx


def compare_data(args, shapes, mcd_count):
    """The pack's columns against the original records' LOFT layers."""
    sys.path.insert(0, __file__.replace("\\", "/").rsplit("/", 1)[0])
    import ufodata
    raw = ufodata.read(args.data, "GEODATA/LOFTEMPS.DAT")
    lofts = [struct.unpack_from("<16H", raw, i * 32) for i in range(len(raw) // 32)]
    bad = 0
    for t, l in enumerate((2, 3, 5, 92, 89, 90, 91), 1):
        if shapes.rows(t) != list(lofts[l]):
            print("unit template %d (LOFT %d) differs" % (t, l))
            bad += 1
    print("unit templates checked, %d bad" % bad)
    # Every record's 12 layers against its LOFT ids (the mission rebuilt
    # from the original data, as the converter does).
    import convert
    import mission as missionmod
    import rules as rulesmod
    name = args.pack.upper().replace("\\", "/").split("/")[-1]
    rules = rulesmod.Rules(args.rules)
    if name.startswith("UR0"):
        spec = missionmod.make_spec(rules, "URBAN", None)
    else:
        terrain = {v: k for k, v in convert.TERRAIN_CODES.items()}[name[:2]]
        ufo = {v: k for k, v in convert.UFO_CODES.items()}[int(name[2])]
        spec = missionmod.make_spec(rules, terrain, ufo)
    ms = missionmod.Mission(args.data, spec)
    if len(ms.mcd) != mcd_count:
        print("pack has %d records, rebuilt mission %d" % (mcd_count, len(ms.mcd)))
        return bad + 1
    checked = 0
    for i in range(1, len(ms.mcd)):
        rec = ms.mcd[i]
        alt = ms.mcd[rec["alt_mcd"]] if rec["alt_mcd"] else None
        want = [[0] * 16] * 12 if alt and alt["ufo_door"] and not rec["ufo_door"] else \
            [list(lofts[l]) if l < len(lofts) else [0] * 16 for l in rec["loft"]]
        got = [shapes.rows(t) for t in shapes.col(i)]
        checked += 1
        if got != want:
            print("record %d: voxel layers differ from its LOFT %s" % (i, rec["loft"]))
            bad += 1
    print("%d records' voxel columns checked, %d bad" % (checked, bad))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump")
    ap.add_argument("log")
    ap.add_argument("pack")
    ap.add_argument("--data")
    ap.add_argument("--rules", default=r"C:\Games\OpenXcom\standard\xcom1")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    dump = load_dump(args.dump)
    vcol, blob = load_xlf(args.pack)
    shapes = Shapes(vcol, blob)
    w = World(dump, shapes)
    if args.data:
        compare_data(args, shapes, len(dump[3]))
    pat = re.compile(r"SHOT T(\d+) u(\d+) \d+,\d+,\d+ org (-?\d+),(-?\d+),(-?\d+) aim (-?\d+),(-?\d+),(-?\d+)"
                     r" acc -?\d+ => (-?\d+) at (-?\d+),(-?\d+),(-?\d+)")
    shots = bad = units = lines = 0
    vl = re.compile(r"VL (-?\d+),(-?\d+),(-?\d+) (-?\d+),(-?\d+),(-?\d+) (\d) => (-?\d+) at (-?\d+),(-?\d+),(-?\d+)")
    with open(args.log, errors="replace") as f:
        for ln in f:
            mv = vl.match(ln)
            if mv:
                # BENCH.TXT: lines without units, replayed exactly
                v = [int(g) for g in mv.groups()]
                r, h, _ = line(w, v[0:3], v[3:6], v[6])
                lines += 1
                if (r, h) != (v[7], tuple(v[8:11])):
                    bad += 1
                    print("MISMATCH line %s: engine %d at %s, replay %d at %s" % (v[:7], v[7], tuple(v[8:11]), r, h))
                continue
            mt = re.match(r"TILE (\d+),(\d+),(\d+) (\d+) (\d+) (\d+) (\d+)", ln)
            if mt:
                x, y, z = map(int, mt.group(1, 2, 3))
                w.tiles[(z * w.my + y) * w.mx + x] = [int(v) for v in mt.group(4, 5, 6, 7)]
                continue
            ms = pat.search(ln)
            if not ms:
                continue
            shots += 1
            v = [int(g) for g in ms.groups()]
            o, t, res, hit = v[2:5], v[5:8], v[8], tuple(v[9:12])
            r, h, path = line(w, o, t, True)
            ok = True
            if res == V_UNIT:
                units += 1
                ok = hit in path and (r in (V_EMPTY, V_OUT) or path.index(hit) < len(path) - 1)
            elif res != r or (res != V_OUT and h != hit):
                ok = False
            if not ok:
                bad += 1
                print("MISMATCH T%d u%d: engine %d at %s, replay %d at %s" % (v[0], v[1], res, hit, r, h))
            elif args.verbose:
                print("ok T%d u%d %d at %s" % (v[0], v[1], res, hit))
    print("%d lines, %d shots, %d unit hits, %d mismatches" % (lines, shots, units, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
