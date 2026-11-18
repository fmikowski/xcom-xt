# SPDX-License-Identifier: GPL-3.0-or-later
"""World map for the geoscape: GEODATA/WORLD.DAT polygons rendered flat.

WORLD.DAT holds 20-byte polygons: four corners (longitude, latitude) in
1/8 degree (a fourth longitude of -1 means a triangle), then the texture.
Latitude grows southwards. Textures map to battle terrains via globe.rul.

    python tools/world.py <UFO data dir> out.png
"""

import struct
import sys

import ufodata

# Texture -> (terrain code used for packs, preview colour). From globe.rul:
# 0/6/10 forest+jungle, 1-4 farmland, 5 mountains, 7/8 desert, 9/12 polar,
# 11 forest.
TEXTURE_TERRAIN = {
    0: "FO", 1: "FA", 2: "FA", 3: "FA", 4: "FA", 5: "MO", 6: "JU", 7: "DE",
    8: "DE", 9: "PO", 10: "JU", 11: "FO", 12: "PO",
}
TERRAIN_COLOURS = {
    "FA": (60, 150, 50), "FO": (20, 100, 30), "JU": (40, 120, 60),
    "MO": (130, 110, 80), "DE": (200, 170, 90), "PO": (230, 230, 240),
}
WATER = (20, 40, 120)


def load_polygons(root):
    data = ufodata.read(root, "GEODATA/WORLD.DAT")
    polys = []
    for off in range(0, len(data) - 19, 20):
        v = struct.unpack_from("<10h", data, off)
        pts = [(v[0], v[1]), (v[2], v[3]), (v[4], v[5])]
        if v[6] != -1:
            pts.append((v[6], v[7]))
        polys.append((pts, v[8]))
    return polys


def _inside(px, py, pts):
    """Even-odd point-in-polygon test."""
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > py) != (yj > py):
            x = xi + (py - yi) * (xj - xi) / (yj - yi)
            if px < x:
                inside = not inside
        j = i
    return inside


def rasterise(root, width, height, textures=False):
    """Terrain code (or None for sea) per cell of a width x height
    equirectangular grid covering longitude 0..360, latitude -90..90
    (textures: the polygon's texture number instead of the terrain)."""
    grid = [[None] * width for _ in range(height)]
    for pts, tex in load_polygons(root):
        terrain = tex if textures else TEXTURE_TERRAIN.get(tex, "FA")
        # Work in 1/8 degree; polygons crossing longitude 0 are shifted.
        lons = [p[0] for p in pts]
        if max(lons) - min(lons) > 1440:
            pts = [(x + 2880 if x < 1440 else x, y) for x, y in pts]
        x0 = min(p[0] for p in pts) * width // 2880
        x1 = max(p[0] for p in pts) * width // 2880 + 1
        y0 = (min(p[1] for p in pts) + 720) * height // 1440
        y1 = (max(p[1] for p in pts) + 720) * height // 1440 + 1
        for cy in range(max(0, y0), min(height, y1 + 1)):
            lat = (cy + 0.5) * 1440 / height - 720
            for cx in range(x0, x1 + 1):
                lon = (cx + 0.5) * 2880 / width
                if _inside(lon, lat, pts):
                    grid[cy][cx % width] = terrain
    return grid


# ---------------------------------------------------------------------------
# Alien side of the geoscape: regions, countries, mission zones, UFO
# trajectories and mission waves from OpenXcom's rules, in map pixels.

MAP_W, MAP_H = 320, 160         # flat map; x = 0 at 180 degrees west
MISSIONS = ["STR_ALIEN_RESEARCH", "STR_ALIEN_HARVEST", "STR_ALIEN_ABDUCTION",
            "STR_ALIEN_INFILTRATION", "STR_ALIEN_BASE", "STR_ALIEN_TERROR",
            "STR_ALIEN_RETALIATION", "STR_ALIEN_SUPPLY"]
TABLE_MISSIONS = 5              # the ones regions weigh (research .. base)
RACES = ["STR_SECTOID", "STR_FLOATER", "STR_SNAKEMAN", "STR_MUTON", "STR_ETHEREAL",
         "STR_MIXED"]
UFOS = [None, "STR_SMALL_SCOUT", "STR_MEDIUM_SCOUT", "STR_LARGE_SCOUT", "STR_HARVESTER",
        "STR_ABDUCTOR", "STR_TERROR_SHIP", "STR_BATTLESHIP", "STR_SUPPLY_SHIP"]
MAX_WAVES, MAX_WAYPOINTS, RACE_MONTHS, ZONES = 8, 10, 5, 6
SHORT_NAMES = {"STR_USA": "USA", "STR_UK": "UK"}


def display_name(sid):
    """STR_NEW_YORK -> New York."""
    if sid in SHORT_NAMES:
        return SHORT_NAMES[sid]
    return " ".join(w.capitalize() for w in sid.replace("STR_", "").split("_"))


def rect_pixels(lon0, lon1, lat0, lat1):
    """Map pixel rectangles (x0, x1, y0, y1 inclusive) for a rule area in
    degrees; areas crossing the map's left edge are split in two."""
    if lon1 < lon0:
        lon1 += 360
    a0 = (lon0 - 180) % 360
    a1 = a0 + (lon1 - lon0)
    y0 = max(0, min(MAP_H - 1, int((lat0 + 90) * MAP_H / 180)))
    y1 = max(y0, min(MAP_H - 1, int((lat1 + 90) * MAP_H / 180 - 0.001)))
    spans = [(a0, min(a1, 360))] + ([(0, a1 - 360)] if a1 > 360 else [])
    out = []
    for s0, s1 in spans:
        x0 = min(MAP_W - 1, int(s0 * MAP_W / 360))
        x1 = max(x0, min(MAP_W - 1, int(s1 * MAP_W / 360 - 0.001)))
        out.append((x0, x1, y0, y1))
    return out


def lonlat_pixel(lon, lat):
    return (int((lon - 180) % 360 * MAP_W / 360),
            max(0, min(MAP_H - 1, int((lat + 90) * MAP_H / 180))))


def build_alien(rules_dir):
    """The "XAL1" section of the world pack, read by src/alien.c:

    header: "XAL1", u8 n_regions, n_countries, n_traj, n_missions, n_cities,
            n_ufos, u16 n_rects
    rects     n_rects x (u16 x0, x1; u8 y0, y1, city (255 none), pad)
    regions   name[16], u8 weight, mission_region, mission_weight[5], pad,
              u16 area, n_area, zone[6], n_zone[6]          (52 bytes)
    countries name[14], u16 funding_base, funding_cap ($K), u16 label_x,
              u8 label_y, region, u16 area, n_area          (26 bytes)
    cities    name[16]
    traj      u16 ground_timer, u8 n, pad, 10 x (zone, altitude, speed %)
    missions  u16 points, u8 objective, spawn_zone, n_waves, pad,
              8 x (u8 ufo, count, traj, objective, u16 timer),
              u8 race_month[5] (255 unused), race_weight[5][6]  (89 bytes)
    ufos      n_ufos x (u16 speed in knots, u16 score)
    """
    import os
    import struct
    import yaml

    def load(name, key):
        with open(os.path.join(rules_dir, name), encoding="utf-8") as f:
            return yaml.safe_load(f)[key]

    regions = load("regions.rul", "regions")
    countries = load("countries.rul", "countries")
    trajs = load("ufoTrajectories.rul", "ufoTrajectories")
    trajs = [t for t in trajs if not t["id"].startswith("__")]
    traj_ids = [t["id"] for t in trajs]
    missions = {m["type"]: m for m in load("alienMissions.rul", "alienMissions")}
    ufos = {u["type"]: u for u in load("ufos.rul", "ufos")}
    region_ids = [r["type"] for r in regions]

    rects, cities = [], []

    def add_areas(areas, named=False):
        first = len(rects)
        for a in areas:
            city = 255
            if named and len(a) > 5:
                city = len(cities)
                cities.append(display_name(a[5]))
            for x0, x1, y0, y1 in rect_pixels(*a[:4]):
                rects.append(struct.pack("<HHBBBx", x0, x1, y0, y1, city))
        return first, len(rects) - first

    out_regions = []
    for r in regions:
        area, n_area = add_areas(r["areas"])
        zones = r.get("missionZones", [])
        zone, n_zone = [], []
        for z in range(ZONES):
            f, n = add_areas(zones[z], named=(z == 3)) if z < len(zones) else (0, 0)
            zone.append(f)
            n_zone.append(n)
        mw = r.get("missionWeights", {})
        home = region_ids.index(r.get("missionRegion", r["type"]))
        out_regions.append(struct.pack("<16sBB5BxHH6H6H", display_name(r["type"]).encode()[:15],
                                       r.get("regionWeight", 0), home,
                                       *[mw.get(m, 0) for m in MISSIONS[:TABLE_MISSIONS]],
                                       area, n_area, *zone, *n_zone))

    def region_of(x, y):
        for i, r in enumerate(regions):
            for a in r["areas"]:
                for x0, x1, y0, y1 in rect_pixels(*a[:4]):
                    if x0 <= x <= x1 and y0 <= y <= y1:
                        return i
        return 255

    out_countries = []
    for c in countries:
        area, n_area = add_areas(c["areas"])
        lx, ly = lonlat_pixel(c["labelLon"], c["labelLat"])
        out_countries.append(struct.pack("<14sHHHBBHH", display_name(c["type"]).encode()[:13],
                                         c["fundingBase"], c["fundingCap"], lx, ly,
                                         region_of(lx, ly), area, n_area))

    out_traj = []
    for t in trajs:
        wps = t["waypoints"][:MAX_WAYPOINTS]
        data = b"".join(bytes(w) for w in wps) + bytes(3 * (MAX_WAYPOINTS - len(wps)))
        out_traj.append(struct.pack("<HBx", t["groundTimer"], len(wps)) + data)

    out_missions = []
    for name in MISSIONS:
        m = missions[name]
        waves = m["waves"][:MAX_WAVES]
        marked = any(w.get("objective") for w in waves)
        data = b""
        for i, w in enumerate(waves):
            obj = w.get("objective", False) or (not marked and i == len(waves) - 1)
            data += struct.pack("<BBBBH", UFOS.index(w["ufo"]), w["count"],
                                traj_ids.index(w["trajectory"]), 1 if obj else 0, w["timer"])
        data += bytes(6 * (MAX_WAVES - len(waves)))
        months = sorted(m.get("raceWeights", {}).items())[:RACE_MONTHS]
        race_month = bytes(mo for mo, _ in months) + b"\xff" * (RACE_MONTHS - len(months))
        weights = b""
        for k in range(RACE_MONTHS):
            w = months[k][1] if k < len(months) else {}
            weights += bytes(w.get(r, 0) for r in RACES)
        out_missions.append(struct.pack("<HBBBx", m.get("points", 0),
                                        m.get("objective", 0),
                                        m.get("spawnZone", 255), len(waves))
                            + data + race_month + weights)

    out_ufos = [struct.pack("<HH", ufos[u]["speedMax"], ufos[u]["score"]) if u else bytes(4)
                for u in UFOS]

    out = bytearray(struct.pack("<4sBBBBBBH", b"XAL1", len(out_regions), len(out_countries),
                                len(out_traj), len(out_missions), len(cities), len(out_ufos),
                                len(rects)))
    for part in (rects, out_regions, out_countries,
                 [struct.pack("<16s", n.encode()[:15]) for n in cities],
                 out_traj, out_missions, out_ufos):
        out += b"".join(part)
    return bytes(out)


def build_borders(rules_dir):
    """The "XBD1" section after the alien side, read by src/alien.c: the
    country borders (globe.rul polylines, drawn by Globe::drawDetail).

    "XBD1", u16 words, then s16 words: per line n, then n x (x, y), at the
    end a 0. Positions are 1/4 map pixel; longitude counted from 180 W, a
    line across the date line goes on past 1280 (or below 0) instead of
    wrapping. Latitude values grow southwards (like WORLD.DAT).
    """
    import os
    import struct
    import yaml

    with open(os.path.join(rules_dir, "globe.rul"), encoding="utf-8") as f:
        lines = yaml.safe_load(f)["globe"]["polylines"]
    words = []
    for line in lines:
        pts, prev = [], None
        for i in range(0, len(line) - 1, 2):
            lon = (line[i] - 180) % 360
            if prev is not None:
                while lon - prev > 180:
                    lon -= 360
                while prev - lon > 180:
                    lon += 360
            prev = lon
            pts.append((int(round(lon * MAP_W * 4 / 360)),
                        int(round((line[i + 1] + 90) * MAP_H * 4 / 180))))
        words.append(len(pts))
        for x, y in pts:
            words += [x, y]
    words.append(0)
    return struct.pack("<4sH%dh" % len(words), b"XBD1", len(words), *words)

def main():
    import png
    root, out = sys.argv[1:3]
    w, h = 320, 160
    grid = rasterise(root, w, h)
    img = [[TERRAIN_COLOURS[c] if c else WATER for c in row] for row in grid]
    png.write_png(out, w, h, img)
    land = sum(1 for row in grid for c in row if c)
    print("land cells", land, "of", w * h)


if __name__ == "__main__":
    main()
