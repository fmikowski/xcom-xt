# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks the engine's tile sight lines against the map's walls.

Shots are voxel lines now (src/vox.c): tools/voxcheck.py replays them;
the SHOT replay below is for logs of the tile shots before the voxel
branch, --sweep still checks the tile sight lines (los_clear).

    python tools/shotcheck.py MAPDUMP.BIN [LOG.TXT] [--pack FA1E.PAK]
        [--data UFO --rules standard/xcom1] [--sweep N] [--seed S]

Run the engine with SHOTLOG (and LOG file): it writes MAPDUMP.BIN (map size,
the pack's MCD records, the tiles) at the start of the battle and logs every
shot's tile path ("SHOT ...") and every terrain change ("TILE ...").

For each logged shot this tool
  - replays the engine's ray (a copy of ray_start / ray_next in src/los.c)
    and compares it with the logged path,
  - replays the line of sight from the shooter to the target tile,
  - judges every step of both against the walls as they really stand: an
    orthogonal step crosses the wall between the tiles, a diagonal step is
    split into the orthogonal moves in the order the straight line crosses
    the tile edges, a step between levels crosses the floor.
A step that the engine lets through but that crosses a solid wall, a solid
object or a floor is printed as a LEAK.

"Solid" is the engine's own rule (blocks_sight in los.c); with --pack,
--data and --rules the original MCD records are rebuilt (tools/mission.py)
and a wall also counts as solid when its LOFT voxels close the tile edge at
body height (layers 5-9), as OpenXcom's voxel shots would see it.

--sweep N casts N random lines between tiles up to 20 apart on the dumped
map and counts the leaks by kind.
"""

import argparse
import random
import re
import struct
import sys

MCD_STOP_LOS, MCD_NO_FLOOR, MCD_BIG_WALL, MCD_DOOR, MCD_UFO_DOOR = 1, 2, 4, 8, 16
LF_WEST, LF_NORTH, LF_OBJECT, LF_FLOOR = 1, 2, 4, 8
RAY_OK, RAY_OUT, RAY_FLOOR, RAY_WESTWALL, RAY_NORTHWALL, RAY_OBJECT = range(6)
CODES = {"": RAY_OK, "X": RAY_OUT, "F": RAY_FLOOR, "W": RAY_WESTWALL,
         "N": RAY_NORTHWALL, "O": RAY_OBJECT}
PART_NAMES = ("floor", "west wall", "north wall", "object")
MID_LAYERS = range(5, 10)       # LOFT layers at body height (voxels 10-19)


def load_dump(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"XMD1":
        raise ValueError("not a map dump")
    mx, my, mz = data[4], data[5], data[6]
    n_mcd = data[7] | data[8] << 8
    mcd = []
    off = 9
    for i in range(n_mcd):
        (sprite, tile_type, p_level, t_level, tu_walk, tu_slide, tu_fly, flags, armor,
         die, alt, special, burn, or_sprite) = struct.unpack_from("<HBBbBBBBBBBBBH", data, off)
        mcd.append({"tile_type": tile_type & 3, "tu_walk": tu_walk, "flags": flags,
                    "armor": armor, "die": die, "alt": alt})
        off += 16
    n = mx * my * mz
    tiles = [list(data[off + i * 4:off + i * 4 + 4]) for i in range(n)]
    return mx, my, mz, mcd, tiles


def engine_blocks(m):
    """blocks_sight() in src/los.c."""
    return (bool(m["flags"] & (MCD_STOP_LOS | MCD_BIG_WALL | MCD_DOOR))
            or (m["tu_walk"] == 255 and m["armor"] >= 60))


def engine_blocks_old(m):
    return bool(m["flags"] & (MCD_STOP_LOS | MCD_BIG_WALL)) or (m["tu_walk"] == 255 and m["armor"] >= 60)


class Map:
    def __init__(self, mx, my, mz, mcd, tiles, loft_solid=None, engine_rule=engine_blocks):
        self.mx, self.my, self.mz, self.mcd, self.tiles = mx, my, mz, mcd, tiles
        self.loft_solid = loft_solid or {}
        self.engine_rule = engine_rule
        self.lf = [0] * len(tiles)
        self.truth = [0] * len(tiles)
        for i in range(len(tiles)):
            self.update(i)

    def idx(self, x, y, z):
        return (z * self.my + y) * self.mx + x

    def solid(self, rec):
        """Physically closes the tile edge (walls) or fills the tile (objects)."""
        m = self.mcd[rec]
        return engine_blocks(m) or self.engine_rule(m) or self.loft_solid.get(rec, False)

    def update(self, i):
        t = self.tiles[i]
        f = g = 0
        if t[0] and not (self.mcd[t[0]]["flags"] & MCD_NO_FLOOR):
            f |= LF_FLOOR
            g |= LF_FLOOR
        for part, bit in ((1, LF_WEST), (2, LF_NORTH), (3, LF_OBJECT)):
            if t[part]:
                if self.engine_rule(self.mcd[t[part]]):
                    f |= bit
                if self.solid(t[part]):
                    g |= bit
        self.lf[i] = f
        self.truth[i] = g

    def in_map(self, x, y, z):
        return 0 <= x < self.mx and 0 <= y < self.my and 0 <= z < self.mz


class Ray:
    """ray_start / ray_next of src/los.c (16-bit 8.8 fixed point)."""

    old = False         # --old-ray: the engine before the edge-order fix

    def __init__(self, m, x0, y0, z0, x1, y1, z1, max_steps):
        self.m = m
        dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
        n = max(abs(dx), abs(dy), abs(dz))
        self.x, self.y, self.z = x0, y0, z0
        self.idx = m.idx(x0, y0, z0)
        self.f = [x0 * 256 + 128, y0 * 256 + 128, z0 * 256 + 128]
        if n == 0:
            self.s = [0, 0, 0]
            self.steps = 0
        else:
            def cdiv(a, b):         # C division truncates towards zero
                q = abs(a) // abs(b)
                return q if (a >= 0) == (b > 0) else -q
            self.s = [cdiv(dx * 256, n), cdiv(dy * 256, n), cdiv(dz * 256, n)]
            self.steps = max(max_steps, n)
        self.i = 0
        self.prev_f = list(self.f)

    def wall_between(self, i, dx, dy):
        lf, mx = self.m.lf, self.m.mx
        if dx > 0:
            return RAY_WESTWALL if lf[i + 1] & LF_WEST else 0
        if dx < 0:
            return RAY_WESTWALL if lf[i] & LF_WEST else 0
        if dy > 0:
            return RAY_NORTHWALL if lf[i + mx] & LF_NORTH else 0
        if dy < 0:
            return RAY_NORTHWALL if lf[i] & LF_NORTH else 0
        return 0

    def next(self):
        if self.old:
            return self.next_old()
        m, lf = self.m, self.m.lf
        p = [self.x, self.y, self.z]
        if self.i >= self.steps:
            return RAY_OUT
        self.i += 1
        self.prev_f = list(self.f)
        self.f = [self.f[k] + self.s[k] for k in range(3)]
        if min(self.f) < 0:
            return RAY_OUT
        n = [v >> 8 for v in self.f]
        if not m.in_map(*n):
            return RAY_OUT
        pi = self.idx
        if sum(1 for k in range(3) if n[k] != p[k]) > 1:
            w = self.corner_step(p, n)
            if w:
                return w
        else:
            if n[2] > p[2]:
                ni = pi + m.mx * m.my
                w = RAY_FLOOR if lf[ni] & LF_FLOOR else 0
            elif n[2] < p[2]:
                ni = pi - m.mx * m.my
                w = RAY_FLOOR if lf[pi] & LF_FLOOR else 0
            else:
                ni = m.idx(*n)
                w = self.wall_between(pi, n[0] - p[0], n[1] - p[1])
            if w and (n[0] < p[0] or n[1] < p[1] or n[2] < p[2]):
                return w
            self.x, self.y, self.z = n
            self.idx = ni
            if w:
                return w
        if lf[self.idx] & LF_OBJECT:
            return RAY_OBJECT
        return RAY_OK

    def route(self, c, i, d, order):
        m, lf = self.m, self.m.lf
        steps = (1, m.mx, m.mx * m.my)
        bits = (LF_WEST, LF_NORTH, LF_FLOOR)
        what = (RAY_WESTWALL, RAY_NORTHWALL, RAY_FLOOR)
        for j, k in enumerate(order):
            if lf[i + steps[k] if d[k] > 0 else i] & bits[k]:
                if d[k] > 0:
                    i += steps[k]
                    c[k] += 1
                return what[k], c, i
            i += steps[k] * d[k]
            c[k] += d[k]
            if j < len(order) - 1 and lf[i] & LF_OBJECT:
                return RAY_OBJECT, c, i
        return RAY_OK, c, i

    def corner_step(self, p, n):
        d = [n[k] - p[k] for k in range(3)]
        f0 = self.prev_f
        e, ln, order = {}, {}, []
        for k in range(3):
            if d[k]:
                e[k] = ((p[k] + 1) << 8) - f0[k] if d[k] > 0 else f0[k] - (p[k] << 8)
                ln[k] = abs(self.s[k])
                assert e[k] * ln[k] < 65536
                order.append(k)
        for j in range(1, len(order)):
            k = j
            while k > 0 and e[order[k - 1]] * ln[order[k]] > e[order[k]] * ln[order[k - 1]]:
                order[k], order[k - 1] = order[k - 1], order[k]
                k -= 1
        tie = -1
        for j in range(len(order) - 1):
            if e[order[j]] * ln[order[j + 1]] == e[order[j + 1]] * ln[order[j]]:
                tie = j
        res, c, i = self.route(list(p), self.idx, d, order)
        if res and tie >= 0:
            o2 = list(order)
            o2[tie], o2[tie + 1] = o2[tie + 1], o2[tie]
            r2, c2, i2 = self.route(list(p), self.idx, d, o2)
            if r2 == RAY_OK:
                res, c, i = RAY_OK, c2, i2
        self.x, self.y, self.z = c
        self.idx = i
        return res

    def next_old(self):
        m, lf = self.m, self.m.lf
        px, py, pz = self.x, self.y, self.z
        if self.i >= self.steps:
            return RAY_OUT
        self.i += 1
        self.prev_f = list(self.f)
        self.f = [self.f[k] + self.s[k] for k in range(3)]
        if min(self.f) < 0:
            return RAY_OUT
        nx, ny, nz = (v >> 8 for v in self.f)
        if not m.in_map(nx, ny, nz):
            return RAY_OUT
        pi = self.idx
        ni = m.idx(nx, ny, nz)
        self.idx, self.x, self.y, self.z = ni, nx, ny, nz
        if nz > pz and lf[ni] & LF_FLOOR:
            return RAY_FLOOR
        if nz < pz and lf[pi] & LF_FLOOR:
            self.x, self.y, self.z, self.idx = px, py, pz, pi
            return RAY_FLOOR
        dx, dy = nx - px, ny - py
        if dx and dy:
            ix = pi + dx
            iy = pi + m.mx if dy > 0 else pi - m.mx
            a = self.wall_between(pi, dx, 0) or self.wall_between(ix, 0, dy) or lf[ix] & LF_OBJECT
            b = self.wall_between(pi, 0, dy) or self.wall_between(iy, dx, 0) or lf[iy] & LF_OBJECT
            if a and b:
                return self.wall_between(pi, dx, 0) or self.wall_between(pi, 0, dy) or RAY_WESTWALL
        else:
            w = self.wall_between(pi, dx, dy)
            if w:
                if dx < 0 or dy < 0:
                    self.x, self.y, self.z, self.idx = px, py, pz, pi
                return w
        if lf[ni] & LF_OBJECT:
            return RAY_OBJECT
        return RAY_OK


def true_step(m, p, n, f0, f1):
    """What really stands between tile p and tile n (one ray step from fixed
    point position f0 to f1): None if the way is free, else a description.
    The step is split into single-axis moves in the order the straight line
    crosses the tile edges."""
    moves = []
    for k in range(3):
        d = n[k] - p[k]
        if not d:
            continue
        s = f1[k] - f0[k]
        edge = n[k] * 256 if d > 0 else p[k] * 256
        t = (edge - f0[k]) / s if s else 0.0
        moves.append((t, k, d))
    moves.sort()
    # The line through an edge or corner: either order is right.
    ties = any(abs(moves[j][0] - moves[j + 1][0]) < 1e-9 for j in range(len(moves) - 1))
    cur = list(p)
    for j, (t, k, d) in enumerate(moves):
        i = m.idx(*cur)
        nxt = list(cur)
        nxt[k] += d
        ni = m.idx(*nxt)
        tr = m.truth
        if k == 0:
            hold = ni if d > 0 else i
            if tr[hold] & LF_WEST:
                return "west wall %s of %s" % (part_desc(m, hold, 1), tuple(nxt if d > 0 else cur)), ties
        elif k == 1:
            hold = ni if d > 0 else i
            if tr[hold] & LF_NORTH:
                return "north wall %s of %s" % (part_desc(m, hold, 2), tuple(nxt if d > 0 else cur)), ties
        else:
            hold = ni if d > 0 else i
            if tr[hold] & LF_FLOOR:
                return "floor %s of %s" % (part_desc(m, hold, 0), tuple(nxt if d > 0 else cur)), ties
        if j < len(moves) - 1 and tr[ni] & LF_OBJECT:
            return "object %s in %s" % (part_desc(m, ni, 3), tuple(nxt)), ties
        cur = nxt
    return None, ties


def part_desc(m, i, part):
    rec = m.tiles[i][part]
    extra = getattr(m, "names", {}).get(rec, "")
    return "mcd %d%s" % (rec, " (%s)" % extra if extra else "")


def walk(m, x0, y0, z0, x1, y1, z1, max_steps, stop_at_target, ray=None):
    """Replays a ray; returns (path [(x, y, z, res)], leaks [(step, text)]).
    ray = (fz, sx, sy, sz): a shot's own start height and steps (logged
    since shots aim at voxels), 100 steps long."""
    r = Ray(m, x0, y0, z0, x1, y1, z1, max_steps)
    if ray:
        r.f[2] = ray[0]
        r.s = list(ray[1:])
        r.steps = 100
        r.prev_f = list(r.f)
    path, leaks = [], []
    prev = (x0, y0, z0)
    while True:
        f0 = r.prev_f = list(r.f)
        res = r.next()
        path.append((r.x, r.y, r.z, res))
        if res == RAY_OUT:
            break
        target = tuple(v >> 8 for v in r.f)
        if res == RAY_OK or (res == RAY_OBJECT and (r.x, r.y, r.z) == target):
            why, ties = true_step(m, prev, target, f0, r.f)
            if why and not ties:
                leaks.append((len(path), "%s -> %s crosses %s" % (prev, target, why)))
        if res != RAY_OK:
            break
        if stop_at_target and (r.x, r.y, r.z) == (x1, y1, z1):
            break
        prev = (r.x, r.y, r.z)
    return path, leaks


def loft_solid_records(args, mcd):
    """Pack record -> True if its LOFT closes the tile at body height."""
    sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
    import convert
    import mission as missionmod
    import rules as rulesmod
    import ufodata
    name = args.pack.upper().split("\\")[-1].split("/")[-1]
    rules = rulesmod.Rules(args.rules)
    if name.startswith("UR0"):
        spec = missionmod.make_spec(rules, "URBAN", None)
    else:
        terrain = {v: k for k, v in convert.TERRAIN_CODES.items()}[name[:2]]
        ufo = {v: k for k, v in convert.UFO_CODES.items()}[int(name[2])]
        spec = missionmod.make_spec(rules, terrain, ufo)
    ms = missionmod.Mission(args.data, spec)
    if len(ms.mcd) != len(mcd):
        raise ValueError("pack has %d records, rebuilt mission %d" % (len(mcd), len(ms.mcd)))
    raw = ufodata.read(args.data, "GEODATA/LOFTEMPS.DAT")
    lofts = [struct.unpack_from("<16H", raw, i * 32) for i in range(len(raw) // 32)]

    def spans(l, tt):
        rows = lofts[l]
        if tt == 1:
            return all(rows)
        if tt == 2:
            acc = 0
            for row in rows:
                acc |= row
            return acc == 0xFFFF
        return sum(bin(row).count("1") for row in rows) >= 154    # objects: 60 %
    solid, names, sets = {}, {}, {}
    keys = ("frames", "loft", "tile_type", "tu_walk", "armor", "stop_los", "door", "big_wall")
    for i in range(1, len(ms.mcd)):
        rec = ms.mcd[i]
        set_name = ms.mcd_set[i]
        if set_name not in sets:
            sets[set_name] = ufodata.load_mcd(args.data, set_name)
        local = [j for j, o in enumerate(sets[set_name]) if all(o[k] == rec[k] for k in keys)]
        names[i] = "%s.MCD %s" % (set_name, local[0] if local else "?")
        tt = rec["tile_type"]
        if tt in (1, 2) or (tt == 3 and rec["tu_walk"] == 255):
            solid[i] = all(spans(rec["loft"][k], tt) for k in MID_LAYERS)
    return solid, names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump")
    ap.add_argument("log", nargs="?")
    ap.add_argument("--pack")
    ap.add_argument("--data")
    ap.add_argument("--rules")
    ap.add_argument("--sweep", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--old", action="store_true",
                    help="the engine before the fix: diagonal steps pass if either L-shaped "
                         "route is open, closed doors without STOP_LOS let sight through")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    mx, my, mz, mcd, tiles = load_dump(args.dump)
    solid, names = {}, {}
    if args.pack and args.data and args.rules:
        solid, names = loft_solid_records(args, mcd)
    rule = engine_blocks_old if args.old else engine_blocks
    Ray.old = args.old
    m = Map(mx, my, mz, mcd, tiles, solid, rule)
    m.names = names
    print("map %dx%dx%d, %d records; %d records solid by LOFT but not by the engine" % (
        mx, my, mz, len(mcd), sum(1 for k, v in solid.items() if v and not rule(mcd[k]))))
    for k, v in sorted(solid.items()):
        if v and not rule(mcd[k]):
            print("  record %d (%s): type %d, flags %02x" % (k, names.get(k, "?"), mcd[k]["tile_type"], mcd[k]["flags"]))

    shots = leaks = mismatches = los_leaks = 0
    if args.log:
        pat = re.compile(r"SHOT T(\d+) u(\d+) (\d+),(\d+),(\d+) tgt (\d+),(\d+),(\d+) aim (-?\d+),(-?\d+),(-?\d+)"
                         r"(?: ray (-?\d+) (-?\d+) (-?\d+) (-?\d+))?(?: acc -?\d+)?:(.*?) => (.*)$")
        with open(args.log, errors="replace") as f:
            for line in f:
                line = line.rstrip("\n")
                mt = re.match(r"TILE (\d+),(\d+),(\d+) (\d+) (\d+) (\d+) (\d+)", line)
                if mt:
                    x, y, z = map(int, mt.group(1, 2, 3))
                    i = m.idx(x, y, z)
                    m.tiles[i] = [int(v) for v in mt.group(4, 5, 6, 7)]
                    m.update(i)
                    continue
                ms = pat.match(line)
                if not ms:
                    continue
                shots += 1
                v = [int(g) for g in ms.groups()[:11]]
                sx, sy, sz, tx, ty, tz, ax, ay, az = v[2:11]
                ray = tuple(int(g) for g in ms.groups()[11:15]) if ms.group(12) else None
                if ray:         # aim in voxels: the tile for the messages
                    ax, ay, az = ax // 16, ay // 16, az // 24
                logged = []
                for tok in ms.group(16).split():
                    mm = re.match(r"(\d+),(\d+),(\d+)([A-Z]?)$", tok)
                    logged.append((int(mm.group(1)), int(mm.group(2)), int(mm.group(3)), CODES[mm.group(4)]))
                path, lk = walk(m, sx, sy, sz, ax, ay, az, 40, False, ray)
                n = len(logged)
                if path[:n] != logged:
                    mismatches += 1
                    print("MISMATCH T%d u%d: replay %s" % (v[0], v[1], path[:n]))
                    print("   log %s" % logged)
                # Only steps the logged shot really took (a unit may stop it).
                lk = [l for l in lk if l[0] <= n]
                for step, text in lk:
                    leaks += 1
                    print("LEAK shot T%d u%d %d,%d,%d -> aim %d,%d,%d step %d: %s  [%s]" % (
                        v[0], v[1], sx, sy, sz, ax, ay, az, step, text, ms.group(17)))
                _, lk2 = walk(m, sx, sy, sz, tx, ty, tz, 0, True)
                for step, text in lk2:
                    los_leaks += 1
                    print("LEAK sight T%d u%d %d,%d,%d -> %d,%d,%d step %d: %s" % (
                        v[0], v[1], sx, sy, sz, tx, ty, tz, step, text))
                if args.verbose and not lk and not lk2:
                    print("ok T%d u%d %s" % (v[0], v[1], ms.group(17)))
        print("%d shots, %d replay mismatches, %d shot leaks, %d sight leaks" % (
            shots, mismatches, leaks, los_leaks))

    if args.sweep:
        rnd = random.Random(args.seed)
        kinds = {}
        examples = {}
        rays = 0
        while rays < args.sweep:
            x0, y0, z0 = rnd.randrange(mx), rnd.randrange(my), rnd.randrange(mz)
            x1 = x0 + rnd.randint(-20, 20)
            y1 = y0 + rnd.randint(-20, 20)
            z1 = z0 + rnd.randint(-1, 1)
            if not m.in_map(x1, y1, z1) or (x0, y0, z0) == (x1, y1, z1):
                continue
            rays += 1
            _, lk = walk(m, x0, y0, z0, x1, y1, z1, 0, True)
            for step, text in lk:
                kind = re.sub(r"\(\d+, \d+, \d+\)", "", text.split("crosses ")[1])
                kind = re.sub(r" in $| of $", "", kind).strip()
                diag = "diagonal" if text.count(",") and _is_diag(text) else "straight"
                key = (diag, kind)
                kinds[key] = kinds.get(key, 0) + 1
                examples.setdefault(key, "%d,%d,%d -> %d,%d,%d: %s" % (x0, y0, z0, x1, y1, z1, text))
        print("sweep: %d rays, %d leaks" % (rays, sum(kinds.values())))
        for key, c in sorted(kinds.items(), key=lambda kv: -kv[1]):
            print("  %5d  %-8s %s   e.g. %s" % (c, key[0], key[1], examples[key]))


def _is_diag(text):
    a, b = re.findall(r"\((\d+), (\d+), (\d+)\)", text)[:2]
    return sum(1 for p, q in zip(a, b) if p != q) > 1


if __name__ == "__main__":
    main()
