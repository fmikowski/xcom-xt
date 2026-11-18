# SPDX-License-Identifier: GPL-3.0-or-later
"""Collects everything one battlescape mission type needs from the original data.

A mission combines a terrain (map blocks + data sets), a UFO and the
Skyranger. All their MCD records go into one global list (index 0 = empty),
every map block is remapped to it, and records no block can reach are
dropped so the list fits in one byte per tile part.
"""

import random

import ufodata

TERRAIN_TITLES = {
    "CULTA": "Farmland", "DESERT": "Desert", "FOREST": "Forest",
    "JUNGLE": "Jungle", "MOUNT": "Mountains", "POLAR": "Polar", "MARS": "Mars",
}
UFO_TITLES = {
    "STR_SMALL_SCOUT": "Small Scout", "STR_MEDIUM_SCOUT": "Medium Scout",
    "STR_LARGE_SCOUT": "Large Scout", "STR_HARVESTER": "Harvester",
    "STR_ABDUCTOR": "Abducter", "STR_TERROR_SHIP": "Terror ship",
    "STR_SUPPLY_SHIP": "Supply ship", "STR_BATTLESHIP": "Battleship",
}


# The transports, in the engine's order (src/pak.h CM_*): each lands with
# its own craft map (crafts.rul battlescapeTerrainData) and, where the rules
# give one, its deployment list (x, y, z, direction per seat).
TRANSPORTS = ("STR_SKYRANGER", "STR_LIGHTNING", "STR_AVENGER")


def craft_specs(rules):
    out = []
    for name in TRANSPORTS:
        r = rules.crafts[name]
        c = r["battlescapeTerrainData"]
        out.append({"name": name, "sets": c["mapDataSets"], "blocks": [c["mapBlocks"][0]["name"]],
                    "deployment": [tuple(d) for d in r.get("deployment", [])]})
    return out


def make_spec(rules, terrain, ufo, craft="STR_SKYRANGER"):
    """ufo None = a terror site: the city terrain's map script, no UFO.
    "craft" is the Skyranger (previews); "crafts" every transport, each
    with its own records in the pack (convert.py build_spec)."""
    t = rules.terrains[terrain]
    c = rules.crafts[craft]["battlescapeTerrainData"]
    fill, big, landing = rules.terrain_blocks(terrain)
    spec = {
        "terrain": {"sets": t["mapDataSets"], "blocks": [b["name"] for b in t["mapBlocks"]]},
        "craft": {"sets": c["mapDataSets"], "blocks": [c["mapBlocks"][0]["name"]]},
        "crafts": craft_specs(rules),
        "fill_blocks": fill,
        "big_blocks": big,
        "landing_blocks": landing,
        "script": None,
    }
    if ufo is None:
        dep = rules.deployments["STR_TERROR_MISSION"]
        script = rules.urban_script(terrain)
        spec.update(script)
        spec["title"] = "Terror Site"
        spec["civilians"] = dep.get("civilians", 0)
        spec["ufo"] = {"sets": [], "blocks": []}
    else:
        u = rules.ufos[ufo]["battlescapeTerrainData"]
        dep = rules.deployments[ufo]
        spec["title"] = "%s - %s" % (UFO_TITLES.get(ufo, ufo), TERRAIN_TITLES.get(terrain, terrain))
        spec["ufo"] = {"sets": u["mapDataSets"], "blocks": [u["mapBlocks"][0]["name"]]}
        spec["civilians"] = 0
    spec["map_size"] = (dep.get("width", 40), dep.get("length", 40), dep.get("height", 4))
    spec["deployment"] = dep
    return spec


# Map scripts without a UFO (pack header byte "script", spec "map_script";
# 1 is the terror city): an alien base, the Mars surface and the Cydonia
# base with the brain, after OpenXcom's mapScripts.rul (ALIENBASE, MARS,
# BOSSBATTLE).
SCRIPT_UFO, SCRIPT_ALIEN_BASE, SCRIPT_MARS, SCRIPT_BOSS = 0, 2, 3, 4
# digTunnel replacements of ALIENBASE / BOSSBATTLE: (data set index, entry).
TUNNEL = {"west": (2, 18), "north": (2, 17), "corner": (2, 19), "floor": (1, 63)}
SPECIAL_NAVIGATION, SPECIAL_MUST_DESTROY = 3, 14


def _groups(block):
    g = block.get("groups", 0)
    return g if isinstance(g, list) else [g]


def make_script_spec(rules, terrain, deployment, script, title, craft="STR_SKYRANGER"):
    """Spec for a mission without a UFO. The pack's "UFO block" is the main
    block (command centre, brain, Mars pyramid); landing blocks are the
    X-COM entry lifts for the bases, big blocks the random 20x20 rooms, fill
    blocks the 10x10 rooms weighted 3:2 by group (fillArea freqs)."""
    t = rules.terrains[terrain]
    blocks = t["mapBlocks"]
    names = [b["name"] for b in blocks]
    dep = rules.deployments[deployment]
    small = [i for i, b in enumerate(blocks) if b["width"] == 10]
    if script == SCRIPT_MARS:
        main = [i for i in small if 2 in _groups(blocks[i])][0]
        fill = [i for i in small if 0 in _groups(blocks[i])]
        landing = [i for i in small if 1 in _groups(blocks[i])]
        big = []
    else:
        main = [i for i, b in enumerate(blocks)
                if (4 if script == SCRIPT_BOSS else 3) in _groups(b)][0]
        landing = [i for i in small if 2 in _groups(blocks[i])]
        big = [i for i, b in enumerate(blocks) if b["width"] == 20 and _groups(b) == [0]]
        g0 = [i for i in small if _groups(blocks[i]) == [0]]
        g1 = [i for i in small if _groups(blocks[i]) == [1]]
        fill = g0 * 2 + g1 * (len(g0) * 4 // 3 // max(1, len(g1)))
    spec = {
        "title": title,
        "map_script": script,
        "terrain": {"sets": t["mapDataSets"], "blocks": names},
        "main_block": main,
        "fill_blocks": fill,
        "big_blocks": big,
        "landing_blocks": landing,
        "map_size": (dep.get("width", 50), dep.get("length", 50), dep.get("height", 4)),
        "deployment": dep,
        "objective": {SCRIPT_ALIEN_BASE: SPECIAL_NAVIGATION,
                      SCRIPT_BOSS: SPECIAL_MUST_DESTROY}.get(script, 0),
    }
    if script == SCRIPT_MARS:
        c = rules.crafts[craft]["battlescapeTerrainData"]
        spec["craft"] = {"sets": c["mapDataSets"], "blocks": [c["mapBlocks"][0]["name"]]}
        spec["crafts"] = craft_specs(rules)
    else:
        sets = t["mapDataSets"]
        spec["keep"] = {k: (sets[s], e) for k, (s, e) in TUNNEL.items()}
    return spec


# The original farm crash site, kept for the preview tool.
FARM_SCOUT = {
    "title": "Small Scout - Farmland",
    "terrain": {"sets": ["BLANKS", "CULTIVAT", "BARN"],
                "blocks": ["CULTA%02d" % i for i in range(19)]},
    "ufo": {"sets": ["BLANKS", "UFO1"], "blocks": ["UFO1A"]},
    "craft": {"sets": ["BLANKS", "PLANE"], "blocks": ["PLANE"]},
    "fill_blocks": list(range(1, 19)),
    "big_blocks": [],
    "landing_blocks": [0, 7, 11],
    "map_size": (40, 40, 4),
}


class Mission:
    def __init__(self, root, spec):
        self.root = root
        self.spec = spec
        self.mcd = [None]           # global records; 0 = empty
        self.mcd_set = [None]       # data set name per record
        self.sprites = []           # decoded 32x40 frames
        self.set_base = {}          # set name -> first global MCD index
        self.set_sprite_base = {}
        self.blocks = {}            # name -> (sx, sy, sz, tiles with global ids)
        crafts = spec.get("crafts")
        for part in ("terrain", "ufo") + (() if crafts else ("craft",)):
            if part not in spec:
                continue
            for name in spec[part]["sets"]:
                self._add_set(name)
            for block in spec[part]["blocks"]:
                self._add_block(block, spec[part]["sets"])
        # Transports: one block each, whose records (beyond those the rest
        # of the map has too) share one range of record ids, so only the
        # landing craft's take part of the 255 (see _prune).
        self.craft_blocks = []
        for c in crafts or []:
            for name in c["sets"]:
                self._add_set(name)
            self._add_block(c["blocks"][0], c["sets"])
            self.craft_blocks.append(c["blocks"][0])
        self._prune()

    def extra_tile(self, set_name, entries):
        """A tile of 4 parts naming records of one data set by their local
        index (0 = none), for records the engine places itself."""
        parts = [0, 0, 0, 0]
        for i, e in enumerate(entries):
            parts[i + 1] = self.set_base[set_name] + e if e else 0
        return tuple(self._new_ids.get(p, 0) for p in parts)

    def _add_set(self, name):
        if name in self.set_base:
            return
        records = ufodata.load_mcd(self.root, name)
        frames = ufodata.load_pck(self.root, "TERRAIN/" + name)
        self.set_base[name] = len(self.mcd)
        self.set_sprite_base[name] = len(self.sprites)
        self.sprites.extend(frames)
        for rec in records:
            rec = dict(rec)
            rec["sprite"] = rec["frames"][0] + self.set_sprite_base[name]
            rec["alt_mcd"] = self._local(name, rec["alt_mcd"], len(records))
            rec["die_mcd"] = self._local(name, rec["die_mcd"], len(records))
            self.mcd.append(rec)
            self.mcd_set.append(name)

    def _local(self, name, index, count):
        # Death/alternate records point inside the same data set.
        if index == 0 or index >= count:
            return 0
        return self.set_base[name] + index

    def _add_block(self, name, sets):
        sx, sy, sz, tiles = ufodata.load_map(self.root, name)
        sizes = [len(ufodata.load_mcd(self.root, s)) for s in sets]

        def remap(v):
            if v == 0:
                return 0
            for s, size in zip(sets, sizes):
                if v < size:
                    return self.set_base[s] + v
                v -= size
            raise ValueError("tile index out of range in %s" % name)

        out = [[[tuple(remap(p) for p in tiles[z][y][x]) for x in range(sx)]
                for y in range(sy)] for z in range(sz)]
        self.blocks[name] = (sx, sy, sz, out)

    def _prune(self):
        """Keeps only records reachable from the blocks (incl. death and
        alternate chains) and renumbers them from 1.

        With transports (spec "crafts"): the records of the rest of the map
        come first (ids 1 .. n_common - 1); then each craft's own records,
        all numbered from n_common on (a mission has one craft). self.mcd
        is the pack's record table: the common ones, then each craft's
        (self.groups: (table index, count) per craft); self.alt_index the
        table index of each record's alternate."""
        def tiles_of(name):
            sx, sy, sz, tiles = self.blocks[name]
            return set(p for z in range(sz) for y in range(sy) for x in range(sx)
                       for p in tiles[z][y][x] if p)

        def closure(seed):
            used = set(seed)
            todo = list(used)
            while todo:
                r = self.mcd[todo.pop()]
                for link in (r["die_mcd"], r["alt_mcd"]):
                    if link and link not in used:
                        used.add(link)
                        todo.append(link)
            return used

        used = set()
        keep = {k: self.set_base[s] + e for k, (s, e) in self.spec.get("keep", {}).items()}
        used.update(keep.values())
        for name in self.blocks:
            if name not in self.craft_blocks:
                used |= tiles_of(name)
        for set_name, entries in self.spec.get("extra_tiles", []):
            used.update(self.set_base[set_name] + e for e in entries if e)
        used = closure(used)
        groups = [sorted(closure(tiles_of(name)) - used) for name in self.craft_blocks]
        order = sorted(used)
        most = len(order) + max([len(g) for g in groups] or [0])
        if most > 255:
            raise ValueError("%d terrain records in one mission (max 255)" % most)
        new = {old: i + 1 for i, old in enumerate(order)}
        self._new_ids = new
        self.n_common = len(order) + 1
        maps = []
        for g in groups:
            m = dict(new)
            m.update({old: self.n_common + i for i, old in enumerate(g)})
            maps.append(m)
        mcd, sets, alt = [None], [None], [0]
        self.groups = []

        def emit(olds, m, first):
            for old in olds:
                rec = dict(self.mcd[old])
                a = rec["alt_mcd"]
                rec["die_mcd"] = m.get(rec["die_mcd"], 0)
                rec["alt_mcd"] = m.get(a, 0)
                # table index of the alternate: common, or this craft's
                alt.append(new[a] if a in new else first + m[a] - self.n_common if a in m else 0)
                mcd.append(rec)
                sets.append(self.mcd_set[old])
        emit(order, new, 1)
        for g, m in zip(groups, maps):
            self.groups.append((len(mcd), len(g)))
            emit(g, m, len(mcd))
        self.mcd, self.mcd_set, self.alt_index = mcd, sets, alt
        self.kept = {k: new[v] for k, v in keep.items()}
        for name, (sx, sy, sz, tiles) in self.blocks.items():
            m = maps[self.craft_blocks.index(name)] if name in self.craft_blocks else new
            self.blocks[name] = (sx, sy, sz, [[[tuple(m.get(p, 0) for p in tiles[z][y][x])
                                                for x in range(sx)] for y in range(sy)]
                                              for z in range(sz)])


def generate_map(mission, seed):
    """Python twin of map_generate in src/map.c, for previews.

    Returns (size, tiles[z][y][x] of 4 parts, placements).
    """
    rng = random.Random(seed)
    spec = mission.spec
    mx, my, mz = spec["map_size"]
    gx, gy = mx // 10, my // 10
    used = [[False] * gx for _ in range(gy)]
    tiles = [[[(0, 0, 0, 0)] * mx for _ in range(my)] for _ in range(mz)]
    placements = []

    def place(name, cx, cy, overlay=False):
        sx, sy, sz, block = mission.blocks[name]
        for z in range(min(sz, mz)):
            for y in range(sy):
                for x in range(sx):
                    old = tiles[z][cy * 10 + y][cx * 10 + x]
                    new = block[z][y][x]
                    if overlay:
                        # loadMAP: any UFO/craft part removes the terrain's object.
                        if any(new):
                            old = old[:3] + (0,)
                        new = tuple(n or o for n, o in zip(new, old))
                    tiles[z][cy * 10 + y][cx * 10 + x] = new
        for y in range(sy // 10):
            for x in range(sx // 10):
                used[cy + y][cx + x] = True
        placements.append((name, cx * 10, cy * 10))

    def free_spot(w, h):
        spots = [(x, y) for y in range(gy - h + 1) for x in range(gx - w + 1)
                 if all(not used[y + j][x + i] for j in range(h) for i in range(w))]
        return rng.choice(spots) if spots else None

    def place_on_landing_zone(name):
        sx, sy = mission.blocks[name][0], mission.blocks[name][1]
        cx, cy = free_spot(sx // 10, sy // 10)
        for j in range(sy // 10):
            for i in range(sx // 10):
                b = rng.choice(spec["landing_blocks"])
                place(spec["terrain"]["blocks"][b], cx + i, cy + j)
        place(name, cx, cy, overlay=True)

    if spec.get("script") == "URBAN":
        # Twin of urban_generate: craft, roads, big blocks, weighted fill.
        place_on_landing_zone(spec["craft"]["blocks"][0])
        names = spec["terrain"]["blocks"]
        ew, ns, cross = spec["roads"]
        lines = {}

        def lay(vertical):
            n = gx if vertical else gy
            other = lines.get(not vertical)
            spots = [i for i in range(1, n)
                     if all(not used[k if vertical else i][i if vertical else k]
                            or k == other for k in range(gy if vertical else gx))]
            if not spots:
                return False
            i = rng.choice(spots)
            for k in range(gy if vertical else gx):
                x, y = (i, k) if vertical else (k, i)
                place(names[cross if k == other else (ns if vertical else ew)], x, y)
            lines[vertical] = i
            return True

        if not (rng.random() < 0.5 and lay(True)) and not (rng.random() < 0.5 and lay(False)):
            lay(True)
            lay(False)
        for _ in range(spec["big_count"]):
            spot = free_spot(2, 2)
            if spot:
                place(names[rng.choice(spec["big_blocks"])], *spot)
        for cy in range(gy):
            for cx in range(gx):
                if not used[cy][cx]:
                    place(names[rng.choice(spec["fill_blocks"])], cx, cy)
        return (mx, my, mz), tiles, placements

    place_on_landing_zone(spec["ufo"]["blocks"][0])
    place_on_landing_zone(spec["craft"]["blocks"][0])
    for _ in range(3 if spec["big_blocks"] else 0):
        spot = free_spot(2, 2)
        if spot:
            place(spec["terrain"]["blocks"][rng.choice(spec["big_blocks"])], *spot)
    for cy in range(gy):
        for cx in range(gx):
            if not used[cy][cx]:
                place(spec["terrain"]["blocks"][rng.choice(spec["fill_blocks"])], cx, cy)
    return (mx, my, mz), tiles, placements
