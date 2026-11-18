# SPDX-License-Identifier: GPL-3.0-or-later
"""The DOS order of the items on the Purchase, Sell and Equip Craft screens.

OpenXcom (and so DOS) lists items in the order of data/xcom1/items.rul (no
item there sets listOrder: each one's list order is its place in the file):
a weapon, then its ammunition (Pistol, Pistol Clip, Rifle, Rifle Clip,
Heavy Cannon, HC-AP Ammo, ...). The engine's item numbers (src/items.h) are
older and unrelated, so the screens walk a list of item numbers in the
DOS order. The lists are string literals in src/itemord.h (ITEM_ORDER:
Purchase, Sell, Stores and Transfer) and src/hwpscr.c (EQ_ORDER, HWP_ORDER: Equip Craft),
because named tables in the -zc overlay sections read garbage; this tool
writes them and checks that the sources hold the current ones (a new item
in items.h or items.rul changes them).

    python tools/itemorder.py            print the literals
    python tools/itemorder.py --check    exit 1 if src/itemord.h or src/hwpscr.c differ, or
                                         a name in src/items.c, craft.c or unit.c differs
                                         from DOS's (see "Names" below)

Names: every item, craft, craft weapon, armour, rank and UFOpaedia title the game shows is DOS's. --check
reads OpenXcom's English strings (Language/en-GB.yml, found under --lang PATH, $XCOM_LANG or the
usual folders) and the player's own GEODATA/ENGLISH.DAT (all but the UFOpaedia titles; --xcom DIR, $XCOM_DIR or
C:/Games/OpenXcom/UFO; skipped if not found) and fails where our names differ.
"""

import os
import re
import sys

import yaml

import convert
import packver

STR = {
    "PISTOL": "PISTOL", "RIFLE": "RIFLE", "HCANNON": "HEAVY_CANNON", "LPISTOL": "LASER_PISTOL",
    "LRIFLE": "LASER_RIFLE", "HLASER": "HEAVY_LASER", "PPISTOL": "PLASMA_PISTOL",
    "PRIFLE": "PLASMA_RIFLE", "HPLASMA": "HEAVY_PLASMA", "CLIP_PISTOL": "PISTOL_CLIP",
    "CLIP_RIFLE": "RIFLE_CLIP", "CLIP_CANNON": "HC_AP_AMMO", "CLIP_PPISTOL": "PLASMA_PISTOL_CLIP",
    "CLIP_PRIFLE": "PLASMA_RIFLE_CLIP", "CLIP_HPLASMA": "HEAVY_PLASMA_CLIP", "GRENADE": "GRENADE",
    "AGRENADE": "ALIEN_GRENADE", "HE": "HIGH_EXPLOSIVE", "ARMOUR": "PERSONAL_ARMOR",
    "POWERSUIT": "POWER_SUIT", "ALLOYS": "ALIEN_ALLOYS", "ELERIUM": "ELERIUM_115",
    "MEDIKIT": "MEDI_KIT", "STUNROD": "STUN_ROD", "LAUNCHER": "SMALL_LAUNCHER",
    "STUNBOMB": "STUN_BOMB", "SCANNER": "MOTION_SCANNER", "ACANNON": "AUTO_CANNON",
    "CLIP_ACANNON": "AC_AP_AMMO", "RLAUNCHER": "ROCKET_LAUNCHER", "ROCKET": "SMALL_ROCKET",
    "BLASTER": "BLASTER_LAUNCHER", "BLASTERBOMB": "BLASTER_BOMB", "CRAFT_CANNON": "CANNON",
    "CANNON_ROUNDS": "CANNON_ROUNDS_X50", "STINGRAY_L": "STINGRAY_LAUNCHER",
    "STINGRAY_M": "STINGRAY_MISSILES", "AVALANCHE_L": "AVALANCHE_LAUNCHER",
    "AVALANCHE_M": "AVALANCHE_MISSILES", "LASER_CANNON": "LASER_CANNON",
    "PLASMA_BEAM": "PLASMA_BEAM", "PSIAMP": "PSI_AMP", "MINDPROBE": "MIND_PROBE",
    "UFO_POWER": "UFO_POWER_SOURCE", "UFO_NAV": "UFO_NAVIGATION", "ALIEN_FOOD": "ALIEN_FOOD",
    "ALIEN_ENTERT": "ALIEN_ENTERTAINMENT", "ALIEN_SURGERY": "ALIEN_SURGERY",
    "EXAM_ROOM": "EXAMINATION_ROOM", "ALIEN_HABITAT": "ALIEN_HABITAT",
    "ALIEN_REPRO": "ALIEN_REPRODUCTION", "CORPSE_SECTOID": "SECTOID_CORPSE",
    "CORPSE_FLOATER": "FLOATER_CORPSE", "CORPSE_SNAKEMAN": "SNAKEMAN_CORPSE",
    "CORPSE_MUTON": "MUTON_CORPSE", "CORPSE_ETHEREAL": "ETHEREAL_CORPSE",
    "FUSION_L": "FUSION_BALL_LAUNCHER", "FUSION_BALL": "FUSION_BALL",
    "CLIP_CANNON_HE": "HC_HE_AMMO", "CLIP_CANNON_IN": "HC_I_AMMO", "CLIP_ACANNON_HE": "AC_HE_AMMO",
    "CLIP_ACANNON_IN": "AC_I_AMMO", "ROCKET_LARGE": "LARGE_ROCKET", "ROCKET_IN": "INCENDIARY_ROCKET",
    "SMOKE": "SMOKE_GRENADE", "PROXIMITY": "PROXIMITY_GRENADE", "FLARE": "ELECTRO_FLARE",
    "TANK_CANNON": "TANK_CANNON", "TANK_ROCKET": "TANK_ROCKET_LAUNCHER",
    "TANK_LASER": "TANK_LASER_CANNON", "HOVER_PLASMA": "HOVERTANK_PLASMA",
    "HOVER_LAUNCHER": "HOVERTANK_LAUNCHER", "HWP_SHELLS": "HWP_CANNON_SHELLS",
    "HWP_ROCKETS": "HWP_ROCKETS", "HWP_FUSION": "HWP_FUSION_BOMB", "FLYINGSUIT": "FLYING_SUIT",
}

# The items the Equip Craft screen lists as battle items (src/hwpscr.c usable()):
# what a soldier can take into battle, as in the game's Equip Craft.
EQUIP = """PISTOL CLIP_PISTOL RIFLE CLIP_RIFLE HCANNON CLIP_CANNON CLIP_CANNON_HE CLIP_CANNON_IN
 ACANNON CLIP_ACANNON CLIP_ACANNON_HE CLIP_ACANNON_IN RLAUNCHER ROCKET ROCKET_LARGE ROCKET_IN
 LPISTOL LRIFLE HLASER GRENADE SMOKE PROXIMITY HE SCANNER MEDIKIT PSIAMP STUNROD FLARE HPLASMA
 CLIP_HPLASMA PRIFLE CLIP_PRIFLE PPISTOL CLIP_PPISTOL BLASTER BLASTERBOMB LAUNCHER STUNBOMB
 AGRENADE MINDPROBE""".split()
HWPS = "TANK_CANNON HWP_SHELLS TANK_ROCKET HWP_ROCKETS TANK_LASER HOVER_PLASMA HOVER_LAUNCHER HWP_FUSION".split()


def orders():
    names = convert.item_enum()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, "data", "xcom1", "items.rul"), encoding="utf-8") as f:
        rul = [it["type"] for it in yaml.safe_load(f)["items"]]
    pos = {t: i for i, t in enumerate(rul)}

    def rank(i):
        n = names[i]
        if 27 <= i <= 55:
            return pos[convert.LIVE_ALIENS[i - 27]]
        s = STR.get(n[3:])
        return pos["STR_" + s] if s else 10000 + i        # the crafts: last

    ids = {n: i for i, n in enumerate(names) if n}
    # Purchase and Sell: everything but the crafts (never in stores).
    trade = sorted((i for i in range(len(names)) if "SHIP" not in (names[i] or "")), key=rank)
    eq = sorted((ids["IT_" + n] for n in EQUIP), key=rank)
    return trade, eq, [ids["IT_" + n] for n in HWPS], names


BS = chr(92)


def literal(items):
    out = []
    for k in range(0, len(items), 16):
        out.append('"' + "".join(BS + "x%02X" % i for i in items[k:k + 16]) + '"')
    return (" " + BS + "\n                 ").join(out)


def find(text, macro):
    m = re.search(r"#define %s\s+((?:\"[^\"]*\"\s*\\?\s*)+)" % macro, text)
    if not m:
        return None
    return [int(h, 16) for h in re.findall(r"\\x([0-9A-Fa-f]{2})", m.group(1))]


# ---------------------------------------------------------------------------
# Names: every name the game shows must be DOS's (OpenXcom's English strings).

# Item numbers with no entry in STR (they are not in the stores' DOS order).
NAME_ID = {"SHIP_FIRESTORM": "FIRESTORM", "SHIP_LIGHTNING": "LIGHTNING", "SHIP_AVENGER": "AVENGER"}
# Craft (src/craft.c g_craft_types), craft weapons (g_craft_weapons), armours
# (src/items.c g_armours) and soldier ranks (src/unit.c g_rank_names), as
# OpenXcom string ids. The craft are shown in capitals (STR_SKYRANGER is
# "SKYRANGER": Craft::getName appends "-1"), the armours too (the Select
# Armor list: STR_PERSONAL_ARMOR_UC).
CRAFT_STR = ["STR_INTERCEPTOR", "STR_FIRESTORM", "STR_LIGHTNING", "STR_AVENGER", "STR_SKYRANGER"]
CRAFT_WEAPON_STR = ["STR_CANNON_UC", "STR_STINGRAY", "STR_AVALANCHE", "STR_LASER_CANNON_UC",
                    "STR_PLASMA_BEAM_UC", "STR_FUSION_BALL_UC"]
ARMOUR_STR = ["STR_NONE_UC", "STR_PERSONAL_ARMOR_UC", "STR_POWER_SUIT_UC", "STR_FLYING_SUIT_UC"]
RANK_STR = ["STR_ROOKIE", "STR_SQUADDIE", "STR_SERGEANT", "STR_CAPTAIN", "STR_COLONEL",
            "STR_COMMANDER"]
# The race and rank parts ENGLISH.DAT joins into a live alien's name.
DAT_LIVE = {"Sectoid": 466, "Snakeman": 467, "Ethereal": 468, "Muton": 469, "Floater": 470,
            "Celatid": 471, "Silacoid": 472, "Chryssalid": 473, "Reaper": 474, "Sectopod": 475,
            "Cyberdisc": 476}

FACILITY_STR = ["STR_ACCESS_LIFT", "STR_LIVING_QUARTERS", "STR_LABORATORY", "STR_WORKSHOP",
                "STR_GENERAL_STORES", "STR_HANGAR", "STR_SMALL_RADAR_SYSTEM",
                "STR_LARGE_RADAR_SYSTEM", "STR_ALIEN_CONTAINMENT", "STR_MISSILE_DEFENSES",
                "STR_LASER_DEFENSES", "STR_PLASMA_DEFENSES", "STR_GRAV_SHIELD",
                "STR_PSIONIC_LABORATORY", "STR_FUSION_BALL_DEFENSES", "STR_MIND_SHIELD",
                "STR_HYPER_WAVE_DECODER"]
# Research topics in src/items.h RS_ order (the interrogations are the live aliens').
TOPIC_STR = ["STR_" + n for n in (
    "LASER_WEAPONS LASER_PISTOL LASER_RIFLE HEAVY_LASER ALIEN_ALLOYS PERSONAL_ARMOR ELERIUM_115 "
    "POWER_SUIT PLASMA_PISTOL PLASMA_RIFLE HEAVY_PLASMA ALIEN_GRENADE SECTOID_AUTOPSY "
    "FLOATER_AUTOPSY SNAKEMAN_AUTOPSY MUTON_AUTOPSY ETHEREAL_AUTOPSY ALIEN_ORIGINS "
    "THE_MARTIAN_SOLUTION CYDONIA_OR_BUST MEDI_KIT MOTION_SCANNER SMALL_LAUNCHER STUN_BOMB "
    "BLASTER_LAUNCHER BLASTER_BOMB LASER_CANNON PLASMA_CANNON PSI_LAB PSI_AMP MIND_PROBE "
    "UFO_POWER_SOURCE UFO_NAVIGATION UFO_CONSTRUCTION NEW_FIGHTER_CRAFT "
    "NEW_FIGHTER_TRANSPORTER ULTIMATE_CRAFT FUSION_MISSILE ALIEN_FOOD ALIEN_REPRODUCTION "
    "ALIEN_ENTERTAINMENT ALIEN_SURGERY EXAMINATION_ROOM LASER_DEFENSE PLASMA_DEFENSE "
    "FUSION_DEFENSE GRAV_SHIELD MIND_SHIELD HYPER_WAVE_DECODER").split()]
UFO_STR = ["", "STR_SMALL_SCOUT", "STR_MEDIUM_SCOUT", "STR_LARGE_SCOUT", "STR_HARVESTER",
           "STR_ABDUCTOR", "STR_TERROR_SHIP", "STR_BATTLESHIP", "STR_SUPPLY_SHIP"]
MISSION_STR = ["STR_ALIEN_" + n for n in "RESEARCH HARVEST ABDUCTION INFILTRATION BASE TERROR "
               "RETALIATION SUPPLY".split()]
RACE_STR = ["STR_" + n for n in "SECTOID FLOATER SNAKEMAN MUTON ETHEREAL".split()]
# The manufacture list's category column (src/labscr.c category()).
CATEGORY_STR = ["STR_CRAFT", "STR_CRAFT_WEAPON", "STR_CRAFT_AMMUNITION", "STR_WEAPON",
                "STR_AMMUNITION", "STR_PERSONAL_ARMOR", "STR_UFO_COMPONENT",
                "STR_HEAVY_WEAPONS_PLATFORM", "STR_EQUIPMENT", "STR_HWP_CANNON_SHELLS"]
# The names of the geoscape's targets (src/geowin.c gw_name_buf): formats.
SITE_STR = ["STR_UFO_", "STR_CRASH_SITE_", "STR_LANDING_SITE_", "STR_TERROR_SITE", "STR_ALIEN_BASE_",
            "STR_WAY_POINT_", "STR_WAY_POINT"]

# Where DOS's own text differs from OpenXcom's (OpenXcom corrected the typo): DOS wins.
DOS_SPELLING = {"STR_ABDUCTOR": "Abducter"}
# Strings ENGLISH.DAT has only inside longer ones.
NOT_IN_DAT = {"STR_WAY_POINT"}

# Window labels (src/geowin.c, craft_status in src/craft.c): each string must appear there
# exactly as DOS writes it (capitals); {ALT}{0} (a value follows) becomes nothing, {0} %d.
LABEL_STR = ["STR_LAUNCH_INTERCEPTION", "STR_CRAFT", "STR_STATUS", "STR_BASE", "STR_RETURN_TO_BASE",
             "STR_SELECT_NEW_TARGET", "STR_PATROL", "STR_SELECT_DESTINATION", "STR_INTERCEPT",
             "STR_REDIRECT_CRAFT", "STR_TARGETTED_BY", "STR_GO_TO_LAST_KNOWN_UFO_POSITION",
             "STR_CENTER_ON_UFO_TIME_5_SECONDS", "STR_CENTER_ON_SITE_TIME_5_SECONDS", "STR_TARGET",
             "STR_DESTINATION_UC_", "STR_INTERCEPTING_UFO", "STR_STATUS_", "STR_BASE_UC",
             "STR_SPEED_", "STR_MAXIMUM_SPEED_UC", "STR_ALTITUDE_", "STR_FUEL", "STR_DAMAGE_UC_",
             "STR_WEAPON_ONE", "STR_WEAPON_TWO", "STR_ROUNDS_", "STR_RETURNING_TO_BASE",
             "STR_LOW_FUEL_RETURNING_TO_BASE", "STR_PATROLLING", "STR_READY", "STR_OUT",
             "STR_REPAIRS", "STR_REFUELLING", "STR_REARMING", "STR_HYPER_WAVE_TRANSMISSIONS_ARE_DECODED",
             "STR_SIZE_UC", "STR_ALTITUDE", "STR_HEADING", "STR_SPEED", "STR_CRAFT_TYPE", "STR_RACE",
             "STR_MISSION", "STR_ZONE", "STR_VERY_LOW", "STR_VERY_HIGH", "STR_VERY_SMALL",
             "STR_VERY_LARGE", "STR_NORTH_EAST", "STR_SOUTH_WEST", "STR_YES", "STR_NO",
             "STR_CANCEL_UC", "STR_CYDONIA", "STR_OK_5_SECONDS"]

LANG_DIRS = ("", "Language", "standard/xcom1/Language", "bin/standard/xcom1/Language",
             "xcom1/Language")


def find_lang(path=None):
    """The OpenXcom language file: PATH (a file, or an OpenXcom folder), else
    $XCOM_LANG, else the usual places. en-GB first: DOS English is British
    ("Personal Armour"; en-US says "Personal Armor")."""
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cands = [path or os.environ.get("XCOM_LANG")] if (path or os.environ.get("XCOM_LANG")) else [
        os.path.join(here, "..", "ref", "OpenXcom"), os.path.join(here, "..", "..", "ref", "OpenXcom"),
        r"C:\Games\OpenXcom"]
    for c in cands:
        if os.path.isfile(c):
            return c
        for sub in LANG_DIRS:
            for fn in ("en-GB.yml", "en-US.yml"):
                p = os.path.join(c, sub, fn)
                if os.path.isfile(p):
                    return p
    return None


def find_dat(path=None):
    """GEODATA/ENGLISH.DAT of the player's X-COM ($XCOM_DIR or C:/Games/OpenXcom/UFO)."""
    import ufodata
    for c in (path, os.environ.get("XCOM_DIR"), r"C:\Games\OpenXcom\UFO", r"C:\Games\UFO"):
        if c and os.path.isdir(c):
            try:
                return ufodata.find_file(c, "GEODATA/ENGLISH.DAT")
            except FileNotFoundError:
                pass
    return None


def source_names():
    """(label, [names]) of every table the game takes names from, read from
    the C sources (the tables in src/items.c, src/craft.c, src/unit.c)."""
    src = packver.src_dir()

    def read(fn):
        with open(os.path.join(src, fn), encoding="ascii") as f:
            return f.read()

    def block(text, start):
        i = text.index(start)
        return text[i:text.index("};", i)]

    its = read("items.c")
    craft = read("craft.c")
    fac = [n for n in re.findall(r'^\s*\{ "([^"]*)",\s*"[A-Z]+",', read("facil.c"), re.M) if n]
    cat = re.findall(r'"([^"]*)"', block(read("labscr.c"), "static const char *category"))
    gw = block(read("geowin.c"), "const char *gw_name_buf")
    sites = re.findall(r'"([^"]*)"', gw.replace("%d", "%d"))
    ufo = re.findall(r'"([^"]*)"', block(read("geo.c"), "ufo_names["))
    mis = re.findall(r'"([^"]*)"', block(read("geo.c"), "mission_names["))
    return {
        "item": re.findall(r'^\s*\{ "([^"]*)",\s*IK_', its, re.M),
        "craft": re.findall(r'^\s*\{ "([^"]*)",\s*\d+,', block(craft, "g_craft_types["), re.M),
        "craft weapon": re.findall(r'^\s*\{ "([^"]*)",\s*IT_', block(craft, "g_craft_weapons["), re.M),
        "armour": re.findall(r'^\s*\{ "([^"]*)",\s*\d+,', block(its, "g_armours["), re.M),
        "rank": re.findall(r'"([^"]*)"', block(read("unit.c"), "g_rank_names")),
        "topic": re.findall(r'^\s*\{ "([^"]*)",\s*\d+,', block(its, "const Topic g_topics"), re.M),
        "facility": fac, "category": cat,
        "site": sites, "ufo": ufo, "mission": mis,
        "label": re.findall(r'"([^"]*)"', read("geowin.c") + read("craft.c")),
        "race": re.findall(r'"([^"]*)"', block(read("alien.c"), "g_race_names[6]"))[:5],
    }


def convert_rs_int():
    """Topics before the interrogations (RS_INT)."""
    return len(TOPIC_STR)


def expected_names(yml):
    """The names the sources must hold: [(table, index, label, string id, text)]."""
    names = convert.item_enum()
    out = []
    for i, n in enumerate(names):
        if 27 <= i <= 55:
            sid = convert.LIVE_ALIENS[i - 27]
        elif n and (n[3:] in STR or n[3:] in NAME_ID):
            sid = "STR_" + (STR.get(n[3:]) or NAME_ID[n[3:]])
        elif n:
            sid = None
        else:
            continue
        out.append(("item", i, n or "IT_LIVE+%d" % (i - 27), sid))
    for t, ids in (("craft", CRAFT_STR), ("craft weapon", CRAFT_WEAPON_STR),
                   ("armour", ARMOUR_STR), ("rank", RANK_STR)):
        out += [(t, i, "%s %d" % (t, i), sid) for i, sid in enumerate(ids)]
    out += [("facility", i, "facility %d" % i, s) for i, s in enumerate(FACILITY_STR)]
    topics = TOPIC_STR[:convert_rs_int()] + list(convert.LIVE_ALIENS) + ["STR_FLYING_SUIT"]
    out += [("topic", i, "topic %d" % i, s) for i, s in enumerate(topics)]
    out += [("ufo", i, "ufo %d" % i, s) for i, s in enumerate(UFO_STR) if s]
    out += [("mission", i, "mission %d" % i, s) for i, s in enumerate(MISSION_STR)]
    out += [("race", i, "race %d" % i, s) for i, s in enumerate(RACE_STR)]
    out += [("site", i, "site %d" % i, s) for i, s in enumerate(SITE_STR)]
    out += [("category", i, "category %d" % i, s) for i, s in enumerate(CATEGORY_STR)]
    out += [("label", i, "label %d" % i, s) for i, s in enumerate(LABEL_STR)]
    import pedia
    out += [("pedia", i, "pedia %d" % i, e[0]) for i, e in enumerate(pedia.ENTRIES)]
    return [(t, i, label, sid, DOS_SPELLING.get(sid) or (yml.get(sid) if sid else None))
            for t, i, label, sid in out]


def in_dat(strings, table, i, name):
    """Whether ENGLISH.DAT has the name (a live alien's, an autopsy's: its parts)."""
    name = name.replace("%d", "")
    if name in strings:
        return True
    race, _, rest = name.partition(" ")
    if race in DAT_LIVE and strings[DAT_LIVE[race]] == race:
        return " " + rest in strings
    return False


def check_names(lang_path=None, dat_path=None):
    """1 if a name in the sources differs from OpenXcom's string (and from
    ENGLISH.DAT's, where the player's copy is found), else 0."""
    lang = find_lang(lang_path)
    if not lang:
        print("names: no OpenXcom en-GB.yml found (give --lang PATH or set XCOM_LANG)")
        return 1
    with open(lang, encoding="utf-8") as f:
        yml = next(iter(yaml.safe_load(f).values()))
    dat = find_dat(dat_path)
    dat_strings = None
    if dat:
        with open(dat, "rb") as f:
            dat_strings = [p.decode("cp437") for p in f.read().split(b"\0")]
    import pedia
    ours = source_names()
    ours["pedia"] = [e[2] for e in pedia.ENTRIES]
    items = ours["item"]
    bad = 0
    n = 0
    for table, i, label, sid, want in expected_names(yml):
        if table not in ours or (i >= len(ours[table]) and table not in ("site", "category", "label")):
            print("names: %s %d (%s): no entry in the sources" % (table, i, label))
            bad = 1
            continue
        got = ours[table][i] if i < len(ours[table]) else None
        if table in ("site", "category", "label"):         # formats and a set of names, not a table
            if sid is not None and want is not None:
                want = want.replace("{ALT}{0}{ALT}", "").replace("{ALT}{0}", "").replace(
                    "{0}", "%d")
                alt = want.replace("%d", "%s")
                got = want if want in ours[table] else alt if alt in ours[table] else next(
                    (s for s in ours[table] if s.lower() in (want.lower(), alt.lower())),
                    "(missing)")
                if got == alt:
                    want = alt
        if sid is None:
            print("names: %s %s (%s) has no OpenXcom string id (add it to NAME_ID)" % (table, i, got))
            bad = 1
            continue
        n += 1
        if want is None:
            print("names: %s: %s is not in %s" % (label, sid, os.path.basename(lang)))
            bad = 1
        elif got != want:
            print("names: %s: ours %r, DOS %r (%s)" % (label, got, want, sid))
            bad = 1
        if dat_strings is not None and want == got and table not in ("pedia", "label") and sid not in NOT_IN_DAT and (
                not in_dat(dat_strings, table, i, got)):
            print("names: %s: %r is not in the player's ENGLISH.DAT" % (label, got))
            bad = 1
    if len(items) != len(convert.item_enum()):
        print("names: %d item names in src/items.c, %d items in src/items.h"
              % (len(items), len(convert.item_enum())))
        bad = 1
    if not bad:
        print("names: ok (%d names match %s%s)" % (n, os.path.basename(lang),
                                                    " and ENGLISH.DAT" if dat_strings else ""))
    return bad


def main():
    trade, eq, hwps, names = orders()
    arg = dict(zip(sys.argv[1::2], sys.argv[2::2]))
    if "--check" not in sys.argv:
        print("ITEM_ORDER (%d):\n#define ITEM_ORDER %s" % (len(trade), literal(trade)))
        print("EQ_ORDER (%d):\n#define EQ_ORDER %s" % (len(eq), literal(eq)))
        print("HWP_ORDER (%d):\n#define HWP_ORDER %s" % (len(hwps), literal(hwps)))
        print([names[i] for i in trade])
        return 0
    src = packver.src_dir()
    bad = 0
    for fname, macro, want in (("itemord.h", "ITEM_ORDER", trade), ("hwpscr.c", "EQ_ORDER", eq),
                               ("hwpscr.c", "HWP_ORDER", hwps)):
        with open(os.path.join(src, fname), encoding="ascii") as f:
            got = find(f.read(), macro)
        if got != want:
            print("src/%s: %s is out of date (run tools/itemorder.py)" % (fname, macro))
            bad = 1
    if not bad:
        print("item order: ok (%d trade items, %d equip items)" % (len(trade), len(eq)))
    return check_names(arg.get("--lang"), arg.get("--xcom")) or bad


if __name__ == "__main__":
    sys.exit(main())
