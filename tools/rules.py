# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads the OpenXcom rule files (standard/xcom1/*.rul) that describe the
original game's terrains, UFOs, craft, alien deployments, races and units."""

import os

import yaml


def _load(rules_dir, name, key):
    with open(os.path.join(rules_dir, name), encoding="utf-8") as f:
        return yaml.safe_load(f)[key]


def _groups(block):
    g = block.get("groups", 0)
    return g if isinstance(g, list) else [g]


class Rules:
    def __init__(self, rules_dir):
        self.terrains = {t["name"]: t for t in _load(rules_dir, "terrains.rul", "terrains")}
        self.scripts = {s["type"]: s for s in _load(rules_dir, "mapScripts.rul", "mapScripts")}
        self.ufos = {u["type"]: u for u in _load(rules_dir, "ufos.rul", "ufos")}
        self.crafts = {c["type"]: c for c in _load(rules_dir, "crafts.rul", "crafts")}
        self.deployments = {d["type"]: d for d in _load(rules_dir, "alienDeployments.rul",
                                                        "alienDeployments")}
        self.races = {r["id"]: r for r in _load(rules_dir, "alienRaces.rul", "alienRaces")}
        self.units = {u["type"]: u for u in _load(rules_dir, "units.rul", "units")}
        self.armors = {a["type"]: a for a in _load(rules_dir, "armors.rul", "armors")}
        self.item_levels = _load(rules_dir, "alienItemLevels.rul", "alienItemLevels")
        # OpenXcom's corrections to the MCD files (optional): data set ->
        # [(MCDIndex, fields)], applied by ufodata.load_mcd.
        self.mcd_patches = {}
        if os.path.isfile(os.path.join(rules_dir, "mcdPatches.rul")):
            for p in _load(rules_dir, "mcdPatches.rul", "MCDPatches"):
                self.mcd_patches.setdefault(p["type"].upper(), []).extend(
                    (d["MCDIndex"], d) for d in p["data"])

    def terrain_blocks(self, name):
        """(10x10 fill blocks, 20x20 fill blocks, landing blocks) as indexes."""
        blocks = self.terrains[name]["mapBlocks"]
        fill, big, landing = [], [], []
        for i, b in enumerate(blocks):
            if 1 in _groups(b) and b["width"] == 10 and b["length"] == 10:
                landing.append(i)
            if b["width"] == 20 and b["length"] == 20:
                big.append(i)
            elif b["width"] == 10 and b["length"] == 10:
                fill.append(i)
        return fill, big, landing

    def urban_script(self, name):
        """The city map script (mapScripts.rul URBAN) as block indexes:
        road blocks by group (2 east-west, 3 north-south, 4 crossing) and
        the weighted fill list (each block repeated by its frequency)."""
        blocks = self.terrains[name]["mapBlocks"]
        roads = {}
        for i, b in enumerate(blocks):
            for g in _groups(b):
                if g in (2, 3, 4):
                    roads[g] = i
        fill = []
        big_count = 0
        for cmd in self.scripts[self.terrains[name]["script"]]["commands"]:
            if cmd["type"] == "fillArea":
                for blk, freq in zip(cmd["blocks"], cmd["freqs"]):
                    fill += [blk] * freq
            elif cmd["type"] == "addBlock" and cmd.get("size") == 2:
                big_count = cmd.get("executions", 1)
        return {"script": "URBAN", "roads": (roads[2], roads[3], roads[4]),
                "fill_blocks": fill, "big_count": big_count}

    def battle_unit(self, unit_type):
        """Stats of a unit type plus its armour's sprite sheet and routine."""
        u = self.units[unit_type]
        a = self.armors[u["armor"]]
        return {
            "type": unit_type,
            "stats": u["stats"],
            "sheet": a["spriteSheet"].rsplit(".", 1)[0].upper(),
            "routine": a.get("drawingRoutine", 0),
            "armor": (a.get("frontArmor", 0), a.get("sideArmor", 0), a.get("rearArmor", 0)),
            "fly": a.get("movementType", 0) == 1,
        }
