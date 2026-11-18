# SPDX-License-Identifier: GPL-3.0-or-later
"""The game's texts, read from the player's own copy of X-COM.

GEODATA/ENGLISH.DAT holds the English texts of DOS X-COM: 973 strings
separated by NUL bytes (the UFOpaedia articles, the mission briefings, the
end-game slides' captions, the interface texts). This module knows only
WHICH string is which (the numbers in INDEX below: no game text is kept
here); the text itself is read at conversion time.

The converter (tools/convert.py, xcomconv.py) asks `lang(root).get(id)` with
OpenXcom's string ids (STR_..._UFOPEDIA). The order of the sources:

1. with prefer_yml: an OpenXcom language file the player points the
   converter at (Language/en-US.yml of an OpenXcom installation);
2. the game's ENGLISH.DAT;
3. the OpenXcom language file, if given (for what the game does not have);
4. OWN: short texts written for this project (OpenXcom wrote the UFOpaedia
   articles of the three armours; the DOS game has none);
5. the caller's default.
"""

import os

import ufodata

# String number in ENGLISH.DAT by OpenXcom string id.
INDEX = {
    "STR_SKYRANGER_UFOPEDIA": 3, "STR_INTERCEPTOR_UFOPEDIA": 1, "STR_STINGRAY_UFOPEDIA": 5,
    "STR_AVALANCHE_UFOPEDIA": 6, "STR_CANNON_UFOPEDIA": 7, "STR_LASER_CANNON_UFOPEDIA": 9,
    "STR_PLASMA_BEAM_UFOPEDIA": 10, "STR_FUSION_BALL_UFOPEDIA": 8,
    "STR_FIRESTORM_UFOPEDIA": 4, "STR_LIGHTNING_UFOPEDIA": 2, "STR_AVENGER_UFOPEDIA": 0,
    "STR_TANK_CANNON_UFOPEDIA": 821, "STR_TANK_ROCKET_LAUNCHER_UFOPEDIA": 822,
    "STR_TANK_LASER_CANNON_UFOPEDIA": 823, "STR_HOVERTANK_PLASMA_UFOPEDIA": 824,
    "STR_HOVERTANK_LAUNCHER_UFOPEDIA": 825, "STR_PISTOL_UFOPEDIA": 663,
    "STR_RIFLE_UFOPEDIA": 664, "STR_HEAVY_CANNON_UFOPEDIA": 665,
    "STR_AUTO_CANNON_UFOPEDIA": 666, "STR_ROCKET_LAUNCHER_UFOPEDIA": 667,
    "STR_LASER_PISTOL_UFOPEDIA": 668, "STR_LASER_RIFLE_UFOPEDIA": 669,
    "STR_HEAVY_LASER_UFOPEDIA": 670, "STR_GRENADE_UFOPEDIA": 671,
    "STR_HIGH_EXPLOSIVE_UFOPEDIA": 674, "STR_MEDI_KIT_UFOPEDIA": 676,
    "STR_MOTION_SCANNER_UFOPEDIA": 675, "STR_PSI_AMP_UFOPEDIA": 677,
    "STR_PLASMA_PISTOL_UFOPEDIA": 681, "STR_PLASMA_RIFLE_UFOPEDIA": 682,
    "STR_HEAVY_PLASMA_UFOPEDIA": 682, "STR_ALIEN_GRENADE_UFOPEDIA": 688,
    "STR_ELERIUM_115_UFOPEDIA": 43, "STR_ALIEN_ALLOYS_UFOPEDIA": 41,
    "STR_MIND_PROBE_UFOPEDIA": 680, "STR_UFO_POWER_SOURCE_UFOPEDIA": 33,
    "STR_UFO_NAVIGATION_UFOPEDIA": 34, "STR_UFO_CONSTRUCTION_UFOPEDIA": 35,
    "STR_ALIEN_FOOD_UFOPEDIA": 36, "STR_ALIEN_REPRODUCTION_UFOPEDIA": 37,
    "STR_ALIEN_ENTERTAINMENT_UFOPEDIA": 38, "STR_ALIEN_SURGERY_UFOPEDIA": 39,
    "STR_EXAMINATION_ROOM_UFOPEDIA": 40, "STR_ACCESS_LIFT_UFOPEDIA": 646,
    "STR_LIVING_QUARTERS_UFOPEDIA": 647, "STR_LABORATORY_UFOPEDIA": 648,
    "STR_WORKSHOP_UFOPEDIA": 649, "STR_SMALL_RADAR_SYSTEM_UFOPEDIA": 650,
    "STR_LARGE_RADAR_SYSTEM_UFOPEDIA": 651, "STR_MISSILE_DEFENSES_UFOPEDIA": 652,
    "STR_GENERAL_STORES_UFOPEDIA": 653, "STR_ALIEN_CONTAINMENT_UFOPEDIA": 654,
    "STR_HANGAR_UFOPEDIA": 662, "STR_PSIONIC_LABORATORY_UFOPEDIA": 660,
    "STR_LASER_DEFENSES_UFOPEDIA": 655, "STR_PLASMA_DEFENSES_UFOPEDIA": 656,
    "STR_FUSION_BALL_DEFENSES_UFOPEDIA": 657, "STR_GRAV_SHIELD_UFOPEDIA": 658,
    "STR_MIND_SHIELD_UFOPEDIA": 659, "STR_HYPER_WAVE_DECODER_UFOPEDIA": 661,
    "STR_SECTOID_AUTOPSY_UFOPEDIA": 12, "STR_FLOATER_AUTOPSY_UFOPEDIA": 26,
    "STR_SNAKEMAN_AUTOPSY_UFOPEDIA": 14, "STR_MUTON_AUTOPSY_UFOPEDIA": 18,
    "STR_ETHEREAL_AUTOPSY_UFOPEDIA": 16, "STR_CYBERDISC_AUTOPSY_UFOPEDIA": 32,
    "STR_REAPER_AUTOPSY_UFOPEDIA": 28, "STR_CHRYSSALID_AUTOPSY_UFOPEDIA": 24,
    "STR_SILACOID_AUTOPSY_UFOPEDIA": 22, "STR_CELATID_AUTOPSY_UFOPEDIA": 20,
    "STR_SECTOPOD_AUTOPSY_UFOPEDIA": 30, "STR_ALIEN_ORIGINS_UFOPEDIA": 44,
    "STR_THE_MARTIAN_SOLUTION_UFOPEDIA": 45, "STR_CYDONIA_OR_BUST_UFOPEDIA": 46,
    "STR_SMALL_SCOUT_UFOPEDIA": 840, "STR_MEDIUM_SCOUT_UFOPEDIA": 841,
    "STR_LARGE_SCOUT_UFOPEDIA": 842, "STR_HARVESTER_UFOPEDIA": 843,
    "STR_ABDUCTOR_UFOPEDIA": 844, "STR_TERROR_SHIP_UFOPEDIA": 845,
    "STR_BATTLESHIP_UFOPEDIA": 846, "STR_SUPPLY_SHIP_UFOPEDIA": 847,
    "STR_UFO_CRASH_RECOVERY_BRIEFING": 238, "STR_UFO_GROUND_ASSAULT_BRIEFING": 240,
    "STR_TERROR_MISSION_BRIEFING": 250, "STR_ALIEN_BASE_ASSAULT_BRIEFING": 246,
    "STR_BASE_DEFENSE_BRIEFING": 243, "STR_MARS_CYDONIA_LANDING_BRIEFING": 913,
    "STR_MARS_THE_FINAL_ASSAULT_BRIEFING": 915, "STR_UFO_CRASH_RECOVERY": 237,
    "STR_UFO_GROUND_ASSAULT": 239, "STR_TERROR_MISSION": 249,
    "STR_ALIEN_BASE_ASSAULT": 244, "STR_BASE_DEFENSE": 241,
    "STR_MARS_CYDONIA_LANDING": 912, "STR_MARS_THE_FINAL_ASSAULT": 914,
    "STR_VICTORY_1": 152, "STR_VICTORY_2": 153, "STR_VICTORY_3": 154, "STR_VICTORY_4": 157,
    "STR_VICTORY_5": 158, "STR_GAME_OVER_1": 155, "STR_GAME_OVER_2": 156,
    # The month report (MonthlyReportState): the heading, the council's words and
    # what it says of the countries ({0} is the list of their names).
    "STR_XCOM_PROJECT_MONTHLY_REPORT": 704, "STR_COUNCIL_IS_GENERALLY_SATISFIED": 706,
    "STR_COUNCIL_IS_VERY_PLEASED": 707, "STR_COUNCIL_IS_DISSATISFIED": 708,
    "STR_YOU_HAVE_NOT_SUCCEEDED": 709, "STR_COUNTRY_IS_PARTICULARLY_PLEASED": 710,
    "STR_COUNTRIES_ARE_PARTICULARLY_HAPPY": 711,
    "STR_COUNTRY_IS_UNHAPPY_WITH_YOUR_ABILITY": 713,
    "STR_COUNTRIES_ARE_UNHAPPY_WITH_YOUR_ABILITY": 714,
    "STR_COUNTRY_HAS_SIGNED_A_SECRET_PACT": 716, "STR_COUNTRIES_HAVE_SIGNED_A_SECRET_PACT": 717,
    "STR_COUNCIL_REDUCE_DEBTS": 720,
    # Messages and window texts (tools/ui.py msg_table).
    "STR_CANNOT_DISMANTLE_FACILITY": 850, "STR_CANNOT_BUILD_HERE": 254,
    "STR_ARE_YOU_SURE_CYDONIA": 278, "STR_IS_LOW_ON_FUEL_RETURNING_TO_BASE": 802,
    "STR_CRAFT_READY_TO_LAND_NEAR_DESTINATION": 343,
    "STR_XCOM_AGENTS_HAVE_LOCATED_AN_ALIEN_BASE_IN_REGION": 918,
    "STR_NOT_ENOUGH_EQUIPMENT_TO_FULLY_RE_EQUIP_SQUAD": 911,
}

# Our own short texts for the ids ENGLISH.DAT lacks.
OWN = {
    "STR_CONTAINMENT_FULL_XT": "Not enough alien containment space. The aliens that do not "
                               "fit will die.",
    "STR_PERSONAL_ARMOR_UFOPEDIA": "Body armour made from Alien Alloys. It stops much more "
                                   "damage than the clothing a soldier starts with.",
    "STR_POWER_SUIT_UFOPEDIA": "A powered suit of armour driven by an Elerium power source. "
                               "It protects better than any other suit and boosts the "
                               "strength of the soldier who wears it.",
    "STR_FLYING_SUIT_UFOPEDIA": "A Power Suit fitted with alien anti-gravity units. Its "
                                "wearer can fly over the battlefield.",
}

_yml_path = None
_prefer_yml = False
_cache = {}


def configure(yml_path=None, prefer_yml=False):
    """Sets the optional OpenXcom language file (a Language/en-US.yml, or the
    folder of an OpenXcom installation) and whether it beats ENGLISH.DAT."""
    global _yml_path, _prefer_yml
    _yml_path = find_yml(yml_path) if yml_path else None
    _prefer_yml = bool(prefer_yml) and _yml_path is not None
    _cache.clear()


def find_yml(path):
    """A language file given as a file, or as a folder of an OpenXcom
    installation (data or user folder, the xcom1 mod folder, or its
    Language folder)."""
    if os.path.isfile(path):
        return path
    for sub in ("", "Language", "standard/xcom1/Language", "bin/standard/xcom1/Language",
                "xcom1/Language"):
        for name in ("en-US.yml", "en-GB.yml"):
            p = os.path.join(path, sub, name)
            if os.path.isfile(p):
                return p
    raise FileNotFoundError("no en-US.yml / en-GB.yml under %s" % path)


class Lang:
    def __init__(self, root):
        self.strings = []
        path = ufodata.find_file(root, "GEODATA/ENGLISH.DAT")
        with open(path, "rb") as f:
            self.strings = [p.decode("cp437") for p in f.read().split(b"\0")]
        self.yml = {}
        if _yml_path:
            import yaml
            with open(_yml_path, encoding="utf-8") as f:
                doc = yaml.safe_load(f)
            self.yml = next(iter(doc.values()))
        self.prefer_yml = _prefer_yml

    def get(self, sid, default=None):
        if self.prefer_yml and sid in self.yml:
            return self.yml[sid]
        i = INDEX.get(sid)
        if i is not None and i < len(self.strings):
            return self.strings[i]
        if sid in self.yml:
            return self.yml[sid]
        return OWN.get(sid, default)

    def source(self, sid):
        """Where get(sid) comes from: 'yml', 'dat', 'own' or None."""
        if self.prefer_yml and sid in self.yml:
            return "yml"
        if sid in INDEX:
            return "dat"
        if sid in self.yml:
            return "yml"
        return "own" if sid in OWN else None


def lang(root):
    key = os.path.abspath(root)
    if key not in _cache:
        _cache[key] = Lang(root)
    return _cache[key]