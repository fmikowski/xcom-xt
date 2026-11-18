# SPDX-License-Identifier: GPL-3.0-or-later
"""UFOpaedia pack (UFOPAED.PAK) from OpenXcom's rules and English text.

Layout (little endian), read by src/pedia.c:
    "XUP2", u16 n_entries, u16 n_sections
    sections   n_sections x name[24]
    index      n_entries x (title[24], unlock[24], u8 section, u8 reserved,
               u16 offset, u16 length); strings NUL padded, offset from the
               text area; unlock as in ENTRIES below
    text area  per entry: lines of at most WIDTH characters joined by '\\n';
               baked stats lines first (craft, facilities, aliens, UFOs), a
               blank line, then the whole description (src/pedia.c pages it).

Weapon and armour stats are not baked: the engine shows its own tables
(items and armour named like the entry's title).
"""

import os
import re
import struct
import textwrap

import yaml

import gametext

WIDTH = 38          # characters per line on the 40-column screen

# The category buttons' texts (STR_XCOM_CRAFT_ARMAMENT ...).
SECTIONS = ["X-COM CRAFT & ARMAMENT", "HEAVY WEAPONS PLATFORMS", "WEAPONS AND EQUIPMENT",
            "ALIEN ARTEFACTS", "BASE FACILITIES", "ALIEN LIFE FORMS", "ALIEN RESEARCH", "UFOs"]
CRAFT, HWP, EQUIP, ARTIFACTS, FACILITIES, ALIENS, RESEARCH, UFOS = range(8)

# (ufopaedia id, text id, title, section, unlock). unlock: "" always, a
# research topic name (src/items.c), "#n" UFO type n recovered, "?name" an
# item of that name exists. Unknown names stay locked, so entries for
# features of other branches can wait here.
ENTRIES = [
    ("STR_SKYRANGER", "STR_SKYRANGER_UFOPEDIA", "SKYRANGER", CRAFT, ""),
    ("STR_INTERCEPTOR", "STR_INTERCEPTOR_UFOPEDIA", "INTERCEPTOR", CRAFT, ""),
    ("STR_STINGRAY", "STR_STINGRAY_UFOPEDIA", "STINGRAY", CRAFT, "?Stingray Launcher"),
    ("STR_AVALANCHE", "STR_AVALANCHE_UFOPEDIA", "AVALANCHE", CRAFT, "?Avalanche Launcher"),
    ("STR_CANNON_UC", "STR_CANNON_UFOPEDIA", "CANNON", CRAFT, "?Cannon"),
    ("STR_LASER_CANNON_UC", "STR_LASER_CANNON_UFOPEDIA", "LASER CANNON", CRAFT, "Laser Cannon"),
    ("STR_PLASMA_BEAM_UC", "STR_PLASMA_BEAM_UFOPEDIA", "PLASMA BEAM", CRAFT, "Plasma Cannon"),
    ("STR_FUSION_BALL_UC", "STR_FUSION_BALL_UFOPEDIA", "FUSION BALL", CRAFT, "Fusion Missile"),
    ("STR_FIRESTORM", "STR_FIRESTORM_UFOPEDIA", "FIRESTORM", CRAFT, "New Fighter Craft"),
    ("STR_LIGHTNING", "STR_LIGHTNING_UFOPEDIA", "LIGHTNING", CRAFT, "New Fighter-Transporter"),
    ("STR_AVENGER", "STR_AVENGER_UFOPEDIA", "AVENGER", CRAFT, "Ultimate Craft"),
    # HWPs (titles are the item names: the engine adds the weapon's stats;
    # the hovertanks also need New Fighter Craft in DOS)
    ("STR_TANK_CANNON", "STR_TANK_CANNON_UFOPEDIA", "Tank/Cannon", HWP, ""),
    ("STR_TANK_ROCKET_LAUNCHER", "STR_TANK_ROCKET_LAUNCHER_UFOPEDIA", "Tank/Rocket Launcher",
     HWP, ""),
    ("STR_TANK_LASER_CANNON", "STR_TANK_LASER_CANNON_UFOPEDIA", "Tank/Laser Cannon", HWP,
     "Laser Cannon"),
    ("STR_HOVERTANK_PLASMA", "STR_HOVERTANK_PLASMA_UFOPEDIA", "Hovertank/Plasma", HWP,
     "Plasma Cannon"),
    ("STR_HOVERTANK_LAUNCHER", "STR_HOVERTANK_LAUNCHER_UFOPEDIA", "Hovertank/Launcher", HWP,
     "Fusion Missile"),
    ("STR_PISTOL", "STR_PISTOL_UFOPEDIA", "Pistol", EQUIP, ""),
    ("STR_RIFLE", "STR_RIFLE_UFOPEDIA", "Rifle", EQUIP, ""),
    ("STR_HEAVY_CANNON", "STR_HEAVY_CANNON_UFOPEDIA", "Heavy Cannon", EQUIP, ""),
    ("STR_AUTO_CANNON", "STR_AUTO_CANNON_UFOPEDIA", "Auto-Cannon", EQUIP, "?Auto-Cannon"),
    ("STR_ROCKET_LAUNCHER", "STR_ROCKET_LAUNCHER_UFOPEDIA", "Rocket Launcher", EQUIP,
     "?Rocket Launcher"),
    ("STR_LASER_PISTOL", "STR_LASER_PISTOL_UFOPEDIA", "Laser Pistol", EQUIP, "Laser Pistol"),
    ("STR_LASER_RIFLE", "STR_LASER_RIFLE_UFOPEDIA", "Laser Rifle", EQUIP, "Laser Rifle"),
    ("STR_HEAVY_LASER", "STR_HEAVY_LASER_UFOPEDIA", "Heavy Laser", EQUIP, "Heavy Laser"),
    ("STR_GRENADE", "STR_GRENADE_UFOPEDIA", "Grenade", EQUIP, ""),
    ("STR_HIGH_EXPLOSIVE", "STR_HIGH_EXPLOSIVE_UFOPEDIA", "High Explosive", EQUIP, ""),
    ("STR_MEDI_KIT", "STR_MEDI_KIT_UFOPEDIA", "Medi-Kit", EQUIP, "Medi-Kit"),
    ("STR_MOTION_SCANNER", "STR_MOTION_SCANNER_UFOPEDIA", "Motion Scanner", EQUIP,
     "Motion Scanner"),
    ("STR_PSI_AMP", "STR_PSI_AMP_UFOPEDIA", "Psi-Amp", EQUIP, "Psi-Amp"),
    ("STR_PERSONAL_ARMOR_UC", "STR_PERSONAL_ARMOR_UFOPEDIA", "PERSONAL ARMOUR", EQUIP,
     "Personal Armour"),
    ("STR_POWER_SUIT_UC", "STR_POWER_SUIT_UFOPEDIA", "POWER SUIT", EQUIP, "Power Suit"),
    ("STR_FLYING_SUIT_UC", "STR_FLYING_SUIT_UFOPEDIA", "FLYING SUIT", EQUIP, "Flying Suit"),
    ("STR_PLASMA_PISTOL", "STR_PLASMA_PISTOL_UFOPEDIA", "Plasma Pistol", ARTIFACTS,
     "Plasma Pistol"),
    ("STR_PLASMA_RIFLE", "STR_PLASMA_RIFLE_UFOPEDIA", "Plasma Rifle", ARTIFACTS, "Plasma Rifle"),
    ("STR_HEAVY_PLASMA", "STR_HEAVY_PLASMA_UFOPEDIA", "Heavy Plasma", ARTIFACTS, "Heavy Plasma"),
    ("STR_ALIEN_GRENADE", "STR_ALIEN_GRENADE_UFOPEDIA", "Alien Grenade", ARTIFACTS,
     "Alien Grenade"),
    ("STR_ELERIUM_115", "STR_ELERIUM_115_UFOPEDIA", "Elerium-115", ARTIFACTS, "Elerium-115"),
    ("STR_ALIEN_ALLOYS", "STR_ALIEN_ALLOYS_UFOPEDIA", "Alien Alloys", ARTIFACTS, "Alien Alloys"),
    ("STR_MIND_PROBE", "STR_MIND_PROBE_UFOPEDIA", "Mind Probe", ARTIFACTS, "Mind Probe"),
    ("STR_UFO_POWER_SOURCE", "STR_UFO_POWER_SOURCE_UFOPEDIA", "UFO Power Source", ARTIFACTS,
     "UFO Power Source"),
    ("STR_UFO_NAVIGATION", "STR_UFO_NAVIGATION_UFOPEDIA", "UFO Navigation", ARTIFACTS,
     "UFO Navigation"),
    ("STR_UFO_CONSTRUCTION", "STR_UFO_CONSTRUCTION_UFOPEDIA", "UFO Construction", ARTIFACTS,
     "UFO Construction"),
    ("STR_ALIEN_FOOD", "STR_ALIEN_FOOD_UFOPEDIA", "Alien Food", ARTIFACTS, "Alien Food"),
    ("STR_ALIEN_REPRODUCTION", "STR_ALIEN_REPRODUCTION_UFOPEDIA", "Alien Reproduction",
     ARTIFACTS, "Alien Reproduction"),
    ("STR_ALIEN_ENTERTAINMENT", "STR_ALIEN_ENTERTAINMENT_UFOPEDIA", "Alien Entertainment",
     ARTIFACTS, "Alien Entertainment"),
    ("STR_ALIEN_SURGERY", "STR_ALIEN_SURGERY_UFOPEDIA", "Alien Surgery", ARTIFACTS,
     "Alien Surgery"),
    ("STR_EXAMINATION_ROOM", "STR_EXAMINATION_ROOM_UFOPEDIA", "Examination Room", ARTIFACTS,
     "Examination Room"),
    ("STR_ACCESS_LIFT", "STR_ACCESS_LIFT_UFOPEDIA", "Access Lift", FACILITIES, ""),
    ("STR_LIVING_QUARTERS", "STR_LIVING_QUARTERS_UFOPEDIA", "Living Quarters", FACILITIES, ""),
    ("STR_LABORATORY", "STR_LABORATORY_UFOPEDIA", "Laboratory", FACILITIES, ""),
    ("STR_WORKSHOP", "STR_WORKSHOP_UFOPEDIA", "Workshop", FACILITIES, ""),
    ("STR_SMALL_RADAR_SYSTEM", "STR_SMALL_RADAR_SYSTEM_UFOPEDIA", "Small Radar System",
     FACILITIES, ""),
    ("STR_LARGE_RADAR_SYSTEM", "STR_LARGE_RADAR_SYSTEM_UFOPEDIA", "Large Radar System",
     FACILITIES, ""),
    ("STR_MISSILE_DEFENSES", "STR_MISSILE_DEFENSES_UFOPEDIA", "Missile Defences", FACILITIES,
     ""),
    ("STR_GENERAL_STORES", "STR_GENERAL_STORES_UFOPEDIA", "General Stores", FACILITIES, ""),
    ("STR_ALIEN_CONTAINMENT", "STR_ALIEN_CONTAINMENT_UFOPEDIA", "Alien Containment",
     FACILITIES, ""),
    ("STR_HANGAR", "STR_HANGAR_UFOPEDIA", "Hangar", FACILITIES, ""),
    ("STR_PSIONIC_LABORATORY", "STR_PSIONIC_LABORATORY_UFOPEDIA", "Psionic Laboratory",
     FACILITIES, "Psi-Lab"),
    ("STR_LASER_DEFENSES", "STR_LASER_DEFENSES_UFOPEDIA", "Laser Defences", FACILITIES,
     "Laser Defence"),
    ("STR_PLASMA_DEFENSES", "STR_PLASMA_DEFENSES_UFOPEDIA", "Plasma Defences", FACILITIES,
     "Plasma Defence"),
    ("STR_FUSION_BALL_DEFENSES", "STR_FUSION_BALL_DEFENSES_UFOPEDIA", "Fusion Ball Defences",
     FACILITIES, "Fusion Defence"),
    ("STR_GRAV_SHIELD", "STR_GRAV_SHIELD_UFOPEDIA", "Grav Shield", FACILITIES, "Grav Shield"),
    ("STR_MIND_SHIELD", "STR_MIND_SHIELD_UFOPEDIA", "Mind Shield", FACILITIES, "Mind Shield"),
    ("STR_HYPER_WAVE_DECODER", "STR_HYPER_WAVE_DECODER_UFOPEDIA", "Hyper-wave Decoder",
     FACILITIES, "Hyper-wave Decoder"),
    ("STR_SECTOID", "STR_SECTOID_AUTOPSY_UFOPEDIA", "Sectoid", ALIENS, "Sectoid autopsy"),
    ("STR_FLOATER", "STR_FLOATER_AUTOPSY_UFOPEDIA", "Floater", ALIENS, "Floater autopsy"),
    ("STR_SNAKEMAN", "STR_SNAKEMAN_AUTOPSY_UFOPEDIA", "Snakeman", ALIENS, "Snakeman autopsy"),
    ("STR_MUTON", "STR_MUTON_AUTOPSY_UFOPEDIA", "Muton", ALIENS, "Muton autopsy"),
    ("STR_ETHEREAL", "STR_ETHEREAL_AUTOPSY_UFOPEDIA", "Ethereal", ALIENS, "Ethereal autopsy"),
    ("STR_CYBERDISC", "STR_CYBERDISC_AUTOPSY_UFOPEDIA", "Cyberdisc", ALIENS, "Cyberdisc autopsy"),
    ("STR_REAPER", "STR_REAPER_AUTOPSY_UFOPEDIA", "Reaper", ALIENS, "Reaper autopsy"),
    ("STR_CHRYSSALID", "STR_CHRYSSALID_AUTOPSY_UFOPEDIA", "Chryssalid", ALIENS,
     "Chryssalid autopsy"),
    ("STR_SILACOID", "STR_SILACOID_AUTOPSY_UFOPEDIA", "Silacoid", ALIENS, "Silacoid autopsy"),
    ("STR_CELATID", "STR_CELATID_AUTOPSY_UFOPEDIA", "Celatid", ALIENS, "Celatid autopsy"),
    ("STR_SECTOPOD", "STR_SECTOPOD_AUTOPSY_UFOPEDIA", "Sectopod", ALIENS, "Sectopod autopsy"),
    ("STR_ALIEN_ORIGINS", "STR_ALIEN_ORIGINS_UFOPEDIA", "Alien Origins", RESEARCH,
     "Alien Origins"),
    ("STR_THE_MARTIAN_SOLUTION", "STR_THE_MARTIAN_SOLUTION_UFOPEDIA", "The Martian Solution",
     RESEARCH, "The Martian Solution"),
    ("STR_CYDONIA_OR_BUST", "STR_CYDONIA_OR_BUST_UFOPEDIA", "Cydonia or Bust", RESEARCH,
     "Cydonia or Bust"),
    ("STR_SMALL_SCOUT", "STR_SMALL_SCOUT_UFOPEDIA", "Small Scout", UFOS, "#1"),
    ("STR_MEDIUM_SCOUT", "STR_MEDIUM_SCOUT_UFOPEDIA", "Medium Scout", UFOS, "#2"),
    ("STR_LARGE_SCOUT", "STR_LARGE_SCOUT_UFOPEDIA", "Large Scout", UFOS, "#3"),
    ("STR_HARVESTER", "STR_HARVESTER_UFOPEDIA", "Harvester", UFOS, "#4"),
    ("STR_ABDUCTOR", "STR_ABDUCTOR_UFOPEDIA", "Abducter", UFOS, "#5"),
    ("STR_TERROR_SHIP", "STR_TERROR_SHIP_UFOPEDIA", "Terror ship", UFOS, "#6"),
    ("STR_BATTLESHIP", "STR_BATTLESHIP_UFOPEDIA", "Battleship", UFOS, "#7"),
    ("STR_SUPPLY_SHIP", "STR_SUPPLY_SHIP_UFOPEDIA", "Supply ship", UFOS, "#8"),
]

UFO_SIZES = {"STR_VERY_SMALL": "Very small", "STR_SMALL": "Small", "STR_MEDIUM_UC": "Medium",
             "STR_LARGE": "Large", "STR_VERY_LARGE": "Very large"}


def _rul(rules_dir, name, key):
    with open(os.path.join(rules_dir, name), encoding="utf-8") as f:
        return {r.get("type"): r for r in yaml.safe_load(f)[key]}


def ascii_text(s):
    """Plain 7-bit text for the BIOS font."""
    for a, b in (("\u2019", "'"), ("\u2018", "'"), ("\u201c", '"'), ("\u201d", '"'),
                 ("\u2013", "-"), ("\u2014", "-"), ("\u2026", "..."), ("\xe9", "e")):
        s = s.replace(a, b)
    s = s.replace("{NEWLINE}", " ").replace("\n", " ")
    s = "".join(c if 32 <= ord(c) < 127 else " " for c in s)
    return re.sub(r"\s+", " ", s).strip()


def fix_case(text):
    """Entries written in capitals (OpenXcom's) read as sentences."""
    if text.isupper():
        text = ". ".join(p.strip().capitalize() for p in text.split(". "))
        text = re.sub(r"\bx-com\b", "X-COM", text, flags=re.I)
    return text


class Stats:
    """Short stats blocks for entries whose numbers are not in the engine."""

    def __init__(self, rules_dir):
        self.crafts = _rul(rules_dir, "crafts.rul", "crafts")
        self.weapons = _rul(rules_dir, "craftWeapons.rul", "craftWeapons")
        self.ufos = _rul(rules_dir, "ufos.rul", "ufos")
        self.facilities = _rul(rules_dir, "facilities.rul", "facilities")
        self.units = _rul(rules_dir, "units.rul", "units")
        self.armors = _rul(rules_dir, "armors.rul", "armors")

    def lines(self, uid):
        # One "label  value" pair a line, as OpenXcom's article stats lists.
        if uid in self.crafts:
            c = self.crafts[uid]
            lines = ["Maximum speed  %d" % c["speedMax"],
                     "Acceleration  %d" % c["accel"],
                     "Fuel capacity  %d" % c["fuelMax"],
                     "Weapon pods  %d" % c.get("weapons", 0),
                     "Damage capacity  %d" % c["damageMax"]]
            if c.get("soldiers"):
                lines.append("Crew capacity  %d" % c["soldiers"])
            return lines
        if uid in self.weapons:
            w = self.weapons[uid]
            return ["Damage  %d" % w.get("damage", 0),
                    "Range  %d km" % w.get("range", 0),
                    "Accuracy  %d%%" % w.get("accuracy", 0),
                    "Re-load time  %d s" % w.get("reloadStandard", 0),
                    "Rounds  %d" % w.get("ammoMax", 0)]
        if uid in self.ufos:
            u = self.ufos[uid]
            return ["Damage capacity  %d" % u["damageMax"],
                    "Weapon power  %d" % u.get("power", 0),
                    "Weapon range  %d km" % u.get("range", 0),
                    "Maximum speed  %d knots" % u["speedMax"]]
        if uid in self.facilities:
            f = self.facilities[uid]
            lines = ["Construction time  %d days" % f["buildTime"],
                     "Construction cost  $%dK" % (f["buildCost"] // 1000),
                     "Maintenance cost  $%dK" % (f["monthlyCost"] // 1000)]
            if f.get("defense"):
                lines += ["Defence value  %d" % f["defense"],
                          "Hit ratio  %d%%" % f.get("hitRatio", 0)]
            return lines
        if uid in self.units:
            # HWPs (ArticleStateVehicle)
            u = self.units[uid]
            s = u["stats"]
            a = self.armors[u["armor"]]
            return ["Time units %d  Health %d" % (s["tu"], s["health"]),
                    "Armour F%d S%d R%d U%d" % (a.get("frontArmor", 0), a.get("sideArmor", 0),
                                                a.get("rearArmor", 0), a.get("underArmor", 0))]
        for unit_type in (uid + "_SOLDIER", uid + "_TERRORIST"):
            if unit_type in self.units:
                u = self.units[unit_type]
                s = u["stats"]
                a = self.armors[u["armor"]]
                return ["%s: TU %d  Health %d" % ("Soldier" if unit_type.endswith("SOLDIER")
                                                   else "Terror unit", s["tu"], s["health"]),
                        "Firing %d  Reactions %d  Bravery %d" % (s["firing"], s["reactions"],
                                                                 s["bravery"]),
                        "Armour F%d S%d R%d U%d" % (a.get("frontArmor", 0),
                                                    a.get("sideArmor", 0),
                                                    a.get("rearArmor", 0),
                                                    a.get("underArmor", 0))]
        return []


def build_pedia(rules_dir, root):
    """The pack from the rules and the game's texts (gametext: ENGLISH.DAT of
    the original game in root)."""
    lang = gametext.lang(root)
    stats = Stats(rules_dir)
    index, area = [], bytearray()
    for uid, text_id, title, section, unlock in ENTRIES:
        lines = stats.lines(uid)
        text = fix_case(ascii_text(lang.get(text_id, "")))
        if lines and text:
            lines.append("")
        lines += textwrap.wrap(text, WIDTH)
        body = "\n".join(lines).encode("ascii")
        assert len(title) < 24 and len(unlock) < 24
        index.append(struct.pack("<24s24sBBHH", title.encode("ascii"), unlock.encode("ascii"),
                                 section, 0, len(area), len(body)))
        area += body
    out = bytearray(struct.pack("<4sHH", b"XUP2", len(index), len(SECTIONS)))
    for name in SECTIONS:
        out += struct.pack("<24s", name.encode("ascii"))
    for rec in index:
        out += rec
    out += area
    assert len(out) < 65536
    return bytes(out), {"entries": len(index), "total": len(out)}
