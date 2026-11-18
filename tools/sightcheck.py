# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares xcom-xt's sight of units with DOS's voxel sight on a map dump.

    python tools/sightcheck.py MAPDUMP.BIN PACK [--pairs N] [--seed S] [--data UFO]

MAPDUMP.BIN comes from a battle run with SHOTLOG; PACK is that battle's
mission pack (its "XLF1" voxel shapes). Random pairs of tiles a unit can
stand on, at most 20 tiles apart, are tested both ways:

- tile: xcom-xt's test before the cover branch (los.c los_clear: one tile
  line from mid tile to mid tile that stops only at parts that block
  sight),
- vox: DOS (OpenXcom TileEngine::visible: canTargetUnit from
  getSightOriginVoxel, the viewer's eyes, through calculateLine and
  voxelCheck; terrain and the target, no other units),
- now: what the engine does since the cover branch (los_unit_sees: the
  tile line, then canTargetUnit from the eyes for the pairs it passes):
  it never sees a unit DOS hides, and misses the pairs DOS sees that the
  tile line stops ("DOS only"),
- a possible next step: a tile line from the eyes to the body's middle
  that stops only at voxel-solid parts, then canTargetUnit (fewer misses,
  but many more voxel tests that fail: trees, jungle plants),

for a standing soldier looking at a standing soldier, a kneeling soldier
and a sectoid, and a sectoid looking at a standing and a kneeling
soldier. It prints how often they disagree and what the disagreements
are (what stopped the voxel lines, what stopped the tile line), and how
many voxel lines each rule traces. With --log LOG.TXT it replays the
engine's logged sight tests ("SEE" lines, SHOTLOG) instead. View cones, range rules,
darkness and smoke are left out (the same in both).
"""

import argparse
import collections
import math
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voxcheck as vc  # noqa: E402

MCD_STOP_LOS, MCD_NO_FLOOR, MCD_BIG_WALL, MCD_DOOR = 1, 2, 4, 8
LF_WEST, LF_NORTH, LF_OBJECT, LF_FLOOR = 1, 2, 4, 8
RAY_OK, RAY_OUT, RAY_FLOOR, RAY_WESTWALL, RAY_NORTHWALL, RAY_OBJECT = range(6)
PART_NAMES = ("floor", "west wall", "north wall", "object", "unit")
HFC = (0, -2, 2, -4, 4, -6, 6, -8, 8, -12, 12)

# name: height, kneel/stand height used, LOFTEMPS template (pack index), radius
SOLDIER = dict(h=22, tpl=2, rad=3)
KNEEL = dict(h=14, tpl=2, rad=3)
SECTOID = dict(h=16, tpl=1, rad=2)


def ctrunc(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


class Sight:
    def __init__(self, dump, shapes):
        self.mx, self.my, self.mz, self.mcd, self.tiles = dump
        self.w = vc.World(dump, shapes)
        self.mxy = self.mx * self.my
        n = self.mxy * self.mz
        self.lf = [0] * (n + self.mxy + self.mx + 2)
        for i in range(n):
            self.lf[i] = self.flags(self.tiles[i])
        # each record's 12 layers of 16 rows (bit 15 - x)
        self.rows = [None] + [[shapes.rows(t) for t in shapes.col(r)] for r in range(1, len(self.mcd))]
        self.unit_rows = {t: shapes.rows(t) for t in (1, 2, 3)}
        self.unit = None            # (tile index, bottom, top, rows)

    def blocks(self, rec):
        m = self.mcd[rec]
        if m["flags"] & (MCD_STOP_LOS | MCD_BIG_WALL | MCD_DOOR):
            return True
        if self.rule == "stoplos":
            return False
        return m["tu_walk"] == 255 and m["armor"] >= 60 and (self.rule != "craft" or m["armor"] != 255)

    def solid(self, rec, part):
        """No voxel line gets through or past the part within its tile: a
        wall with a full column (west) or row (north) in every layer, an
        object full in every layer."""
        rows = self.rows[rec]
        if part == 1:
            return any(all(all(r >> (15 - x) & 1 for r in layer) for layer in rows) for x in range(16))
        if part == 2:
            return any(all(layer[y] == 0xFFFF for layer in rows) for y in range(16))
        return all(all(r == 0xFFFF for r in layer) for layer in rows)

    def flags(self, t, solid=False):
        f = 0
        if t[0] and not self.mcd[t[0]]["flags"] & MCD_NO_FLOOR:
            f |= LF_FLOOR
        for p, bit in ((1, LF_WEST), (2, LF_NORTH), (3, LF_OBJECT)):
            if t[p] and self.blocks(t[p]) and (not solid or self.solid(t[p], p)):
                f |= bit
        return f

    def use_solid(self, on):
        """The tile line on solid parts only (on) or on every part that
        blocks sight (the fog of war's rule)."""
        if not hasattr(self, "_lf"):
            self._lf = {False: self.lf, True: list(self.lf)}
            for i in range(self.mxy * self.mz):
                self._lf[True][i] = self.flags(self.tiles[i], True)
        self.lf = self._lf[on]

    def idx(self, x, y, z):
        return (z * self.my + y) * self.mx + x

    def tile(self, x, y, z):
        return self.tiles[self.idx(x, y, z)]

    def in_map(self, x, y, z):
        return 0 <= x < self.mx and 0 <= y < self.my and 0 <= z < self.mz

    # ---- los.c: the tile line -------------------------------------------
    def wall_between(self, i, dx, dy):
        lf = self.lf
        if dx > 0:
            return RAY_WESTWALL if lf[i + 1] & LF_WEST else 0
        if dx < 0:
            return RAY_WESTWALL if lf[i] & LF_WEST else 0
        if dy > 0:
            return RAY_NORTHWALL if lf[i + self.mx] & LF_NORTH else 0
        if dy < 0:
            return RAY_NORTHWALL if lf[i] & LF_NORTH else 0
        return 0

    def route(self, c, i, d, order):
        bit = (LF_WEST, LF_NORTH, LF_FLOOR)
        what = (RAY_WESTWALL, RAY_NORTHWALL, RAY_FLOOR)
        n = len(order)
        for j in range(n):
            k = order[j]
            step = 1 if k == 0 else self.mx if k == 1 else self.mxy
            if self.lf[i + step if d[k] > 0 else i] & bit[k]:
                if d[k] > 0:
                    i += step
                    c[k] += 1
                return what[k], i
            i = i + step if d[k] > 0 else i - step
            c[k] += d[k]
            if j < n - 1 and self.lf[i] & LF_OBJECT:
                return RAY_OBJECT, i
        return RAY_OK, i

    def corner_step(self, r, p, nn):
        c = list(p)
        d = [nn[k] - p[k] for k in range(3)]
        s = [r["sx"], r["sy"], r["sz"]]
        f0 = [r["fx"] - s[0], r["fy"] - s[1], r["fz"] - s[2]]
        e, ln, order = [0] * 3, [0] * 3, []
        for k in range(3):
            if d[k]:
                e[k] = ((c[k] + 1) << 8) - f0[k] if d[k] > 0 else f0[k] - (c[k] << 8)
                ln[k] = abs(s[k])
                order.append(k)
        n = len(order)
        for j in range(1, n):
            k = j
            while k > 0 and e[order[k - 1]] * ln[order[k]] > e[order[k]] * ln[order[k - 1]]:
                order[k], order[k - 1] = order[k - 1], order[k]
                k -= 1
        tie = -1
        for j in range(n - 1):
            if e[order[j]] * ln[order[j + 1]] == e[order[j + 1]] * ln[order[j]]:
                tie = j
        res, i = self.route(c, r["idx"], d, order)
        if res and tie >= 0:
            c2 = list(p)
            order[tie], order[tie + 1] = order[tie + 1], order[tie]
            res2, i2 = self.route(c2, r["idx"], d, order)
            if res2 == RAY_OK:
                res, c, i = RAY_OK, list(nn), i2
        r["x"], r["y"], r["z"] = c
        r["idx"] = i
        return res

    def ray_next(self, r):
        lf = self.lf
        if r["i"] >= r["steps"]:
            return RAY_OUT
        r["i"] += 1
        r["fx"] += r["sx"]
        r["fy"] += r["sy"]
        r["fz"] += r["sz"]
        px, py, pz = r["x"], r["y"], r["z"]
        nx, ny, nz = r["fx"] >> 8, r["fy"] >> 8, r["fz"] >> 8
        if r["fx"] < 0 or r["fy"] < 0 or r["fz"] < 0 or not self.in_map(nx, ny, nz):
            return RAY_OUT
        pi = r["idx"]
        if nz == pz and nx != px and ny != py:
            dx, dy, first = nx - px, ny - py, 0
            ix = pi + dx
            iy = pi + self.mx if dy > 0 else pi - self.mx
            ax, ay = abs(r["sx"]), abs(r["sy"])
            if ax != ay:
                if ax == 256:
                    e2 = (r["fy"] - r["sy"]) & 255
                    e2 = 2 * (256 - e2 if dy > 0 else e2)
                    first = 2 if e2 < ay else 1 if e2 > ay else 0
                elif ay == 256:
                    e2 = (r["fx"] - r["sx"]) & 255
                    e2 = 2 * (256 - e2 if dx > 0 else e2)
                    first = 1 if e2 < ax else 2 if e2 > ax else 0
            a = 1 if first == 2 else (self.wall_between(pi, dx, 0) or self.wall_between(ix, 0, dy)
                                      or lf[ix] & LF_OBJECT)
            b = 1 if first == 1 else (self.wall_between(pi, 0, dy) or self.wall_between(iy, dx, 0)
                                      or lf[iy] & LF_OBJECT)
            if a and b:
                return self.corner_step(r, (px, py, pz), (nx, ny, nz))
            r["x"], r["y"] = nx, ny
            r["idx"] = iy + dx
        elif (nx != px) + (ny != py) + (nz != pz) > 1:
            w = self.corner_step(r, (px, py, pz), (nx, ny, nz))
            if w:
                return w
        else:
            if nz > pz:
                ni = pi + self.mxy
                w = RAY_FLOOR if lf[ni] & LF_FLOOR else 0
            elif nz < pz:
                ni = pi - self.mxy
                w = RAY_FLOOR if lf[pi] & LF_FLOOR else 0
            else:
                ni = pi + 1 if nx > px else pi - 1 if nx < px else pi + self.mx if ny > py else pi - self.mx
                w = self.wall_between(pi, nx - px, ny - py)
            if w and (nx < px or ny < py or nz < pz):
                return w
            r["x"], r["y"], r["z"], r["idx"] = nx, ny, nz, ni
            if w:
                return w
        if lf[r["idx"]] & LF_OBJECT:
            return RAY_OBJECT
        return RAY_OK

    def tile_line(self, a, b, fz=None):
        """los_clear without smoke: (clear, what stopped it, at tile). fz:
        the heights to go from and to (8.8 tiles) instead of mid tile."""
        f0, f1 = fz if fz else (a[2] * 256 + 128, b[2] * 256 + 128)
        a = (a[0], a[1], f0 >> 8)
        b = (b[0], b[1], f1 >> 8)
        dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        n = max(abs(dx), abs(dy), abs(dz))
        r = dict(x=a[0], y=a[1], z=a[2], idx=self.idx(*a), fx=a[0] * 256 + 128, fy=a[1] * 256 + 128,
                 fz=f0, i=0, steps=n,
                 sx=ctrunc(dx * 256, n) if n else 0, sy=ctrunc(dy * 256, n) if n else 0,
                 sz=ctrunc(f1 - f0, n) if n else 0)
        if not self.in_map(*a):
            return False, RAY_OUT, a
        while r["i"] < r["steps"]:
            res = self.ray_next(r)
            at = (r["x"], r["y"], r["z"])
            if res == RAY_OUT:
                return False, res, at
            if at == tuple(b):
                return res in (RAY_OK, RAY_OBJECT), res, at
            if res != RAY_OK:
                return False, res, at
        return True, RAY_OK, tuple(b)

    # ---- voxels ------------------------------------------------------------
    def level(self, x, y, z):
        t = self.tile(x, y, z)
        lv = self.mcd[t[0]]["t_level"] if t[0] else 0
        if t[3] and self.mcd[t[3]]["t_level"] < lv:
            lv = self.mcd[t[3]]["t_level"]
        return lv

    def no_floor(self, x, y, z):
        """Tile::hasNoFloor(tile below)."""
        if z > 0:
            b = self.tile(x, y, z - 1)
            if (b[0] and self.mcd[b[0]]["t_level"] == -24) or (b[3] and self.mcd[b[3]]["t_level"] == -24):
                return False
        t = self.tile(x, y, z)
        return not t[0] or bool(self.mcd[t[0]]["flags"] & MCD_NO_FLOOR)

    def check(self, vx, vy, vz):
        """voxelCheck with the one unit in self.unit."""
        if vx < 0 or vy < 0 or vz < 0:
            return vc.V_OUT
        x, y, z = vx >> 4, vy >> 4, vz // 24
        if x >= self.mx or y >= self.my or z >= self.mz:
            return vc.V_OUT
        i = (z * self.my + y) * self.mx + x
        t = self.tiles[i]
        lz = vz % 24
        if lz < 2 and t[0] and self.mcd[t[0]]["flags"] & vc.MCD_GRAVLIFT:
            b = self.tiles[i - self.mxy] if z else None
            if not b or not b[0] or not self.mcd[b[0]]["flags"] & vc.MCD_GRAVLIFT:
                return vc.V_FLOOR
        bx = 15 - (vx & 15)
        ry = vy & 15
        for p in range(4):
            if t[p] and (self.rows[t[p]][lz >> 1][ry] >> bx) & 1:
                return p
        u = self.unit
        if u and (u[0] == i or (u[0] == i - self.mxy and z and self.no_floor(x, y, z))):
            if u[1] < vz <= u[2] and (u[3][ry] >> bx) & 1:
                return vc.V_UNIT
        return vc.V_EMPTY

    def line(self, o, t):
        """calculateLine with voxel checks, not extended: (result, hit, voxels)."""
        x0, y0, z0 = o
        x1, y1, z1 = t
        sxy = abs(y1 - y0) > abs(x1 - x0)
        if sxy:
            x0, y0, x1, y1 = y0, x0, y1, x1
        sxz = abs(z1 - z0) > abs(x1 - x0)
        if sxz:
            x0, z0, x1, z1 = z0, x0, z1, x1
        dx, dy, dz = abs(x1 - x0), abs(y1 - y0), abs(z1 - z0)
        dxy = dxz = dx // 2
        stx, sty, stz = (-1 if x0 > x1 else 1), (-1 if y0 > y1 else 1), (-1 if z0 > z1 else 1)
        y, z, x = y0, z0, x0
        n = 0
        while True:
            for step in (0, 1, 2):
                if step == 1:
                    dxy -= dy
                    dxz -= dz
                    if dxy >= 0:
                        continue
                    y += sty
                    dxy += dx
                elif step == 2:
                    if dxz >= 0:
                        continue
                    z += stz
                    dxz += dx
                cx, cy, cz = x, y, z
                if sxz:
                    cx, cz = cz, cx
                if sxy:
                    cx, cy = cy, cx
                n += 1
                r = self.check(cx, cy, cz)
                if r != vc.V_EMPTY:
                    return r, (cx, cy, cz), n
                if step == 0 and x == x1:
                    return vc.V_EMPTY, (cx, cy, cz), n
            x += stx

    def eyes(self, pos, kind):
        """getSightOriginVoxel."""
        x, y, z = pos
        oz = z * 24 - self.level(x, y, z) + kind["h"] - 1
        if oz >= (z + 1) * 24 and not (z + 1 < self.mz and self.no_floor(x, y, z + 1)):
            oz = (z + 1) * 24 - 1
        return (x * 16 + 8, y * 16 + 8, oz)

    def can_target(self, o, pos, kind, lo=None):
        """canTargetUnit at a unit of `kind` on tile pos (its feet at voxel
        height lo, else on the terrain): (seen, lines, first line's
        (result, hit))."""
        x, y, z = pos
        tx, ty = x * 16 + 8, y * 16 + 8
        tmin = z * 24 - self.level(x, y, z) if lo is None else lo
        h = kind["h"]
        tmax = tmin + h
        self.unit = (self.idx(x, y, z), tmin, tmax, self.unit_rows[kind["tpl"]])
        mid = (tmin + tmax) // 2
        hr = min(h // 2, 10)
        rx, ry = tx - o[0], ty - o[1]
        dist = math.sqrt(rx * rx + ry * ry)
        if dist:
            nrm = kind["rad"] / dist
            relx, rely = math.floor(ry * nrm + 0.5), math.floor(-rx * nrm + 0.5)
        else:
            relx = rely = 0
        sl = (0, 0, relx, rely, -relx, -rely, rely, -relx, -rely, relx)
        lines = 0
        f = None
        for i in range(hr + 1):
            sz = mid + HFC[i]
            for j in range(5):
                if i < hr - 1 and j > 2:
                    break
                sx, sy = tx + sl[j * 2], ty + sl[j * 2 + 1]
                res, hit, _ = self.line(o, (sx, sy, sz))
                lines += 1
                if f is None:
                    f = (res, hit)
                if res == vc.V_UNIT and hit[0] >> 4 == sx >> 4 and hit[1] >> 4 == sy >> 4 and tmin <= hit[2] <= tmax:
                    self.unit = None
                    return True, lines, f
        self.unit = None
        return False, lines, f

    def standable(self):
        out = []
        for z in range(self.mz):
            for y in range(self.my):
                for x in range(self.mx):
                    t = self.tile(x, y, z)
                    if z and (not t[0] or self.mcd[t[0]]["flags"] & MCD_NO_FLOOR):
                        continue
                    if t[3] and self.mcd[t[3]]["tu_walk"] == 255:
                        continue
                    if z + 1 < self.mz and not self.no_floor(x, y, z + 1) and self.level(x, y, z) < -8:
                        continue        # no head room
                    out.append((x, y, z))
        return out

    def part_height(self, rec):
        """Top of the record's shape in voxels (0 = empty)."""
        top = 0
        for lz, layer in enumerate(self.rows[rec]):
            if any(layer):
                top = lz * 2 + 2
        return top


def describe_vox(s, res, hit, target, names):
    """What stopped the first voxel line, and where (near the target or
    the viewer)."""
    if res == vc.V_OUT:
        return "out of the map"
    if res == vc.V_EMPTY:
        return "nothing (reached the scan voxel)"
    x, y, z = hit[0] >> 4, hit[1] >> 4, hit[2] // 24
    near = max(abs(x - target[0]), abs(y - target[1]), abs(z - target[2]))
    where = "at the target's tile" if near == 0 else "next to the target" if near == 1 else "farther away"
    if res == vc.V_UNIT:
        return "unit " + where
    rec = s.tile(x, y, z)[res]
    m = s.mcd[rec]
    kind = PART_NAMES[res]
    if res == 0:
        if z != target[2]:
            kind = "floor between levels"
    top = s.part_height(rec)
    tag = "low (top %d)" % top if top < 20 and res else "high (top %d)" % top if res else ""
    flags = []
    if m["flags"] & MCD_STOP_LOS:
        flags.append("stops sight")
    nm = names[rec] if names else ""
    return "%s %s %s rec %d %s %s" % (kind, tag, where, rec, nm, " ".join(flags))


def describe_tile(s, res, at):
    if res == RAY_OUT:
        return "out"
    part = {RAY_FLOOR: 0, RAY_WESTWALL: 1, RAY_NORTHWALL: 2, RAY_OBJECT: 3}[res]
    rec = s.tile(*at)[part]
    m = s.mcd[rec] if rec else None
    why = []
    if m:
        if m["flags"] & MCD_STOP_LOS:
            why.append("stops sight")
        if m["flags"] & MCD_BIG_WALL:
            why.append("big wall")
        if m["flags"] & MCD_DOOR:
            why.append("door")
        if m["tu_walk"] == 255 and m["armor"] >= 60:
            why.append("solid armour %d" % m["armor"])
    return "%s rec %d top %d (%s)" % (PART_NAMES[part], rec, s.part_height(rec) if rec else 0, ", ".join(why))


def record_names(args, n_mcd):
    if not args.data:
        return None
    import convert
    import mission as missionmod
    import rules as rulesmod
    name = os.path.basename(args.pack).upper()
    rules = rulesmod.Rules(args.rules)
    try:
        if name.startswith("UR0"):
            spec = missionmod.make_spec(rules, "URBAN", None)
        else:
            terrain = {v: k for k, v in convert.TERRAIN_CODES.items()}[name[:2]]
            ufo = {v: k for k, v in convert.UFO_CODES.items()}[int(name[2])]
            spec = missionmod.make_spec(rules, terrain, ufo)
        ms = missionmod.Mission(args.data, spec)
    except Exception as e:          # base and script packs: no names
        print("no record names: %s" % e)
        return None
    if len(ms.mcd) != n_mcd:
        return None
    local = {new: old - ms.set_base[ms.mcd_set[new]] for old, new in ms._new_ids.items()}
    return [None] + ["%s/%d" % (ms.mcd_set[i], local[i]) for i in range(1, n_mcd)]


def replay(s, log):
    """The engine's voxel sight tests (vox.c vox_sees, logged as "SEE u<a>
    u<b> eye x,y,z at x,y,z lo l h h t t => r") replayed with OpenXcom's
    canTargetUnit, terrain and the target only: the engine may see less
    (other units in the way), never more."""
    see = re.compile(r"SEE u(\d+) u(\d+) eye (-?\d+),(-?\d+),(-?\d+) at (\d+),(\d+),(\d+) lo (-?\d+) h (\d+)"
                     r" t (\d) => (\d)")
    tile = re.compile(r"TILE (\d+),(\d+),(\d+) (\d+) (\d+) (\d+) (\d+)")
    n = seen = more = less = big = 0
    with open(log, errors="replace") as f:
        for ln in f:
            mt = tile.search(ln)
            if mt:
                x, y, z = map(int, mt.group(1, 2, 3))
                i = s.idx(x, y, z)
                s.tiles[i] = [int(v) for v in mt.group(4, 5, 6, 7)]
                s.lf[i] = s.flags(s.tiles[i])
                continue
            m = see.search(ln)
            if not m:
                continue
            v = [int(g) for g in m.groups()]
            if not v[10]:
                big += 1        # 2x2 targets: not replayed
                continue
            n += 1
            kind = dict(h=v[9], tpl=v[10], rad=(0, 2, 3, 5)[v[10]])
            dos = s.can_target(tuple(v[2:5]), tuple(v[5:8]), kind, lo=v[8])[0]
            seen += v[11]
            if v[11] and not dos:
                more += 1
                print("MISMATCH (engine sees, DOS does not): " + ln.strip())
            elif dos and not v[11]:
                less += 1
    print("%d sight tests (%d seen), %d 2x2 targets skipped: engine sees more %d, less %d"
          " (other units in the way, or a mismatch)" % (n, seen, big, more, less))
    return 1 if more else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump")
    ap.add_argument("pack")
    ap.add_argument("--pairs", type=int, default=1500)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--data")
    ap.add_argument("--rules", default=r"C:\Games\OpenXcom\standard\xcom1")
    ap.add_argument("--top", type=int, default=8)
    ap.add_argument("--rule", default="craft", choices=("armour", "craft", "stoplos"),
                    help="the tile line's parts: the old rule, los.c's (without armour 255), DOS's Stop_LOS only")
    ap.add_argument("--near", action="store_true",
                    help="viewers within 2 tiles of a craft part the armour rule stops (armour 255)")
    ap.add_argument("--log", help="replay the SEE lines of a SHOTLOG battle's LOG instead")
    args = ap.parse_args()
    dump = vc.load_dump(args.dump)
    vcol, blob = vc.load_xlf(args.pack)
    vcol = [m["vcol"] for m in dump[3]]     # (the battle's records: the craft's after the common ones)
    Sight.rule = args.rule
    s = Sight(dump, vc.Shapes(vcol, blob))
    if args.log:
        return replay(s, args.log)
    names = record_names(args, len(s.mcd))
    rnd = random.Random(args.seed)
    stand = s.standable()
    pairs = []
    start = stand
    if args.near:
        gear = {(x, y, z) for z in range(s.mz) for y in range(s.my) for x in range(s.mx)
                for r in s.tile(x, y, z)[1:] if r and s.mcd[r]["armor"] == 255 and s.mcd[r]["tu_walk"] == 255
                and not s.mcd[r]["flags"] & (MCD_STOP_LOS | MCD_BIG_WALL | MCD_DOOR)}
        print("craft parts stopped by the armour rule at", sorted(gear))
        start = [a for a in stand if any(abs(a[0] - g[0]) <= 2 and abs(a[1] - g[1]) <= 2 and a[2] == g[2] for g in gear)]
    while len(pairs) < args.pairs:
        a, b = rnd.choice(start), rnd.choice(stand)
        d2 = (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2
        if a != b and d2 <= 400 and abs(a[2] - b[2]) <= 2:
            pairs.append((a, b))
    cases = (("soldier sees soldier", SOLDIER, SOLDIER), ("soldier sees kneeling", SOLDIER, KNEEL),
             ("soldier sees sectoid", SOLDIER, SECTOID), ("sectoid sees soldier", SECTOID, SOLDIER),
             ("sectoid sees kneeling", SECTOID, KNEEL))
    print("%s: %dx%dx%d, %d standable tiles, %d pairs (%d on one level)" % (
        os.path.basename(args.pack), s.mx, s.my, s.mz, len(stand), len(pairs),
        sum(a[2] == b[2] for a, b in pairs)))
    tile = [s.tile_line(a, b) for a, b in pairs]
    s.use_solid(True)
    tsol = [s.tile_line(a, b)[0] for a, b in pairs]
    s.use_solid(False)
    print("tile line clear: %d of %d; on solid parts only: %d" % (sum(t[0] for t in tile), len(pairs), sum(tsol)))
    summary = []
    for name, va, tb in cases:
        cnt = collections.Counter()
        why_hidden, why_blocked = collections.Counter(), collections.Counter()
        lines_rule = lines_dos = 0
        first_ok = collections.Counter()
        sol = collections.Counter()
        eye = collections.Counter()
        for (a, b), t, ts in zip(pairs, tile, tsol):
            o = s.eyes(a, va)
            seen, n, first = s.can_target(o, b, tb)
            s.use_solid(True)
            mid = b[2] * 24 - s.level(*b) + tb["h"] // 2
            te = s.tile_line(a, b, (o[2] * 256 // 24, mid * 256 // 24))[0]
            s.use_solid(False)
            eye[(te, seen)] += 1
            if te:
                eye["lines"] += n
            lines_dos += n
            if t[0]:
                lines_rule += n
            if seen:
                first_ok[n] += 1
            sol[(ts, seen)] += 1
            if ts:
                sol["lines"] += n
                for cap in (2, 4, 8):
                    sol["lines%d" % cap] += min(n, cap)
                    sol["miss%d" % cap] += seen and n > cap
            cnt[(t[0], seen)] += 1
            if t[0] and not seen:
                why_hidden[describe_vox(s, first[0], first[1], b, names)] += 1
            elif seen and not t[0]:
                why_blocked[describe_tile(s, t[1], t[2])] += 1
        both, tonly, vonly, none = cnt[(True, True)], cnt[(True, False)], cnt[(False, True)], cnt[(False, False)]
        n = len(pairs)
        print("\n%s: both see %d, tile only %d (%.1f%%), DOS only %d (%.1f%%), neither %d" % (
            name, both, tonly, 100.0 * tonly / n, vonly, 100.0 * vonly / n, none))
        print("  tile vs DOS wrong %.1f%% of pairs; now (tile line + voxels) wrong %.1f%%" % (
            100.0 * (tonly + vonly) / n, 100.0 * vonly / n))
        print("  of the pairs DOS sees, the tile line misses %.1f%%; of the pairs the tile line sees, DOS hides %.1f%%" % (
            100.0 * vonly / max(1, both + vonly), 100.0 * tonly / max(1, both + tonly)))
        print("  voxel lines: rule %.2f a pair (%.1f a pair it tests), DOS %.2f a pair" % (
            lines_rule / n, lines_rule / max(1, both + tonly), lines_dos / n))
        nseen = both + vonly
        cum, acc = [], 0
        for k in range(1, 38):
            acc += first_ok[k]
            if k in (1, 2, 3, 4, 6, 8, 12, 20):
                cum.append("%d: %.0f%%" % (k, 100.0 * acc / max(1, nseen)))
        print("  DOS-seen pairs found within the first n lines: " + ", ".join(cum))
        print("  solid-parts tile line, then voxels: passes %d, misses %d DOS sees (%.1f%%), %.2f lines a pair;"
              " capped at 2/4/8 lines: %.2f/%.2f/%.2f lines, misses +%d/+%d/+%d" % (
                  sol[(True, True)] + sol[(True, False)], sol[(False, True)], 100.0 * sol[(False, True)] / n,
                  sol["lines"] / n, sol["lines2"] / n, sol["lines4"] / n, sol["lines8"] / n,
                  sol["miss2"], sol["miss4"], sol["miss8"]))
        print("  the same from the eyes to the body's middle (a possible next step): passes %d, misses %d DOS sees"
              " (%.1f%%), %.2f lines a pair" % (
                  eye[(True, True)] + eye[(True, False)], eye[(False, True)], 100.0 * eye[(False, True)] / n,
                  eye["lines"] / n))
        summary.append("%-8s %-22s DOS sees %4.1f%%  old: wrong %4.1f%% (sees hidden %4.1f%%, misses seen %4.1f%%)"
                       "  next step: misses seen %4.1f%%, %.1f lines a pair" % (
                           os.path.basename(args.pack).split(".")[0], name, 100.0 * (both + vonly) / n,
                           100.0 * (tonly + vonly) / n, 100.0 * tonly / n, 100.0 * vonly / n,
                           100.0 * eye[(False, True)] / n, eye["lines"] / n))
        if why_hidden:
            print("  tile sees, DOS does not (first line stopped by):")
            for k, v in why_hidden.most_common(args.top):
                print("    %4d %s" % (v, k))
        if why_blocked:
            print("  DOS sees, tile line stopped by:")
            for k, v in why_blocked.most_common(args.top):
                print("    %4d %s" % (v, k))
    print()
    for ln in summary:
        print(ln)
    return 0


if __name__ == "__main__":
    sys.exit(main())
