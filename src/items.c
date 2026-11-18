/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "items.h"

/* name, kind, power, snap acc/TU, aimed acc/TU, auto acc/TU, clip, clip item, $K */
const ItemDef g_items[N_ITEMS] = {
    { "Pistol",        IK_WEAPON,  26,  60, 18,  78, 30,   0,  0, 12, IT_CLIP_PISTOL,  1 },
    { "Rifle",         IK_WEAPON,  30,  60, 25, 110, 80,  35, 35, 20, IT_CLIP_RIFLE,   3 },
    { "Heavy Cannon",  IK_WEAPON,  56,  60, 33,  90, 80,   0,  0,  6, IT_CLIP_CANNON,  6 },
    { "Laser Pistol",  IK_WEAPON,  46,  40, 20,  68, 55,  28, 25,  0, NO_ITEM,         0 },
    { "Laser Rifle",   IK_WEAPON,  60,  65, 25, 100, 50,  46, 34,  0, NO_ITEM,         0 },
    { "Heavy Laser",   IK_WEAPON,  85,  50, 33,  84, 75,   0,  0,  0, NO_ITEM,         0 },
    { "Plasma Pistol", IK_WEAPON,  52,  65, 30,  85, 60,  50, 30, 26, IT_CLIP_PPISTOL, 0 },
    { "Plasma Rifle",  IK_WEAPON,  80,  86, 30, 100, 60,  55, 36, 28, IT_CLIP_PRIFLE,  0 },
    { "Heavy Plasma",  IK_WEAPON, 115,  75, 30, 110, 60,  50, 35, 35, IT_CLIP_HPLASMA, 0 },
    { "Pistol Clip",   IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Rifle Clip",    IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "HC-AP Ammo",     IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Plasma Pistol Clip", IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Plasma Rifle Clip",  IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Heavy Plasma Clip", IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Grenade",       IK_GRENADE, 50,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Alien Grenade", IK_GRENADE, 90,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "High Explosive",IK_GRENADE,110,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         2 },
    { "Personal Armour",IK_ARMOUR,  0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Power Suit",    IK_ARMOUR,   0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Alien Alloys",  IK_MATERIAL, 0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Elerium-115",   IK_MATERIAL, 0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Medi-Kit",      IK_EQUIP,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Stun Rod",      IK_WEAPON,  65, 100, 30,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Small Launcher",IK_WEAPON,  90,  65, 40, 110, 75,   0,  0,  1, IT_STUNBOMB,     0 },
    { "Stun Bomb",     IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Motion Scanner",IK_EQUIP,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Sectoid Commander", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Sectoid Leader", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Sectoid Engineer", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Sectoid Medic", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Sectoid Navigator", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Sectoid Soldier", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Floater Commander", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Floater Leader", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Floater Engineer", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Floater Medic", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Floater Navigator", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Floater Soldier", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Snakeman Commander", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Snakeman Leader", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Snakeman Engineer", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Snakeman Navigator", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Snakeman Soldier", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Muton Engineer", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Muton Navigator", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Muton Soldier", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Ethereal Commander", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Ethereal Leader", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Ethereal Soldier", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Cyberdisc Terrorist", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Sectopod Terrorist", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Celatid Terrorist", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Chryssalid Terrorist", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Silacoid Terrorist", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Reaper Terrorist", IK_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0, NO_ITEM, 0 },
    { "Auto-Cannon",   IK_WEAPON,  42,  56, 33,  82, 80,  32, 40, 14, IT_CLIP_ACANNON, 13 },
    { "AC-AP Ammo",    IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Rocket Launcher",IK_WEAPON, 75,  55, 45, 115, 75,   0,  0,  1, IT_ROCKET,       4 },
    { "Small Rocket",  IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Blaster Launcher",IK_WEAPON,200,  0,  0, 120, 66,   0,  0,  1, IT_BLASTERBOMB,  0 },
    { "Blaster Bomb",  IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Cannon",        IK_CRAFT,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,        30 },
    { "Cannon Rounds(x50)",IK_CRAFT,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Stingray Launcher",IK_CRAFT,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,        16 },
    { "Stingray Missile",IK_CRAFT,  0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         3 },
    { "Avalanche Launcher",IK_CRAFT,   0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,        17 },
    { "Avalanche Missile", IK_CRAFT,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         9 },
    { "Laser Cannon",  IK_CRAFT,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Plasma Beam",   IK_CRAFT,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Psi-Amp",       IK_EQUIP,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Mind Probe",    IK_EQUIP,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "UFO Power Source",IK_MATERIAL,0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "UFO Navigation", IK_MATERIAL, 0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Alien Food",    IK_MATERIAL, 0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Alien Entertainment",IK_MATERIAL,0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Alien Surgery", IK_MATERIAL, 0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Examination Room",IK_MATERIAL,0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Alien Habitat", IK_MATERIAL, 0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Alien Reproduction",IK_MATERIAL,0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Sectoid Corpse", IK_MATERIAL, 0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Floater Corpse", IK_MATERIAL, 0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Snakeman Corpse",IK_MATERIAL, 0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Muton Corpse",  IK_MATERIAL, 0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Ethereal Corpse",IK_MATERIAL, 0,  0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Fusion Ball Launcher",IK_CRAFT,  0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Fusion Ball",   IK_CRAFT,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "FIRESTORM",     IK_SHIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "LIGHTNING",     IK_SHIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "AVENGER",       IK_SHIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "HC-HE Ammo",     IK_CLIP,    52,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "HC-I Ammo",     IK_CLIP,    60,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "AC-HE Ammo",    IK_CLIP,    44,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "AC-I Ammo",    IK_CLIP,    48,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Large Rocket",  IK_CLIP,   100,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Incendiary Rocket",IK_CLIP,    90,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Smoke Grenade", IK_GRENADE, 60,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Proximity Grenade",IK_GRENADE, 70,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    { "Electro-flare", IK_GRENADE, 15,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         1 },
    /* HWPs: the power is the ammunition's (items.rul), the price is in
       g_buy10 (more than the $K byte holds) */
    { "Tank/Cannon",   IK_HWP,     60,  60, 33,  90, 80,   0,  0, 30, IT_HWP_SHELLS,   0 },
    { "Tank/Rocket Launcher",IK_HWP,85, 55, 45, 115, 75,   0,  0,  8, IT_HWP_ROCKETS,  0 },
    { "Tank/Laser Cannon",IK_HWP, 110,  50, 33,  85, 75,   0,  0,  0, NO_ITEM,         0 },
    { "Hovertank/Plasma",IK_HWP,  110,  85, 30, 100, 60,   0,  0,  0, NO_ITEM,         0 },
    { "Hovertank/Launcher",IK_HWP,140,   0,  0, 100, 66,   0,  0,  8, IT_HWP_FUSION,   0 },
    { "HWP Cannon Shells",IK_CLIP,  0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "HWP Rockets",   IK_CLIP,     0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "HWP Fusion Bomb",IK_CLIP,    0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
    { "Flying Suit",   IK_ARMOUR,   0,   0,  0,   0,  0,   0,  0,  0, NO_ITEM,         0 },
};

/* Sale prices, $10 units (OpenXcom items.rul costSell / 10). Live aliens
   as their crew rank (soldier 40K, leader and commander 60K). */
const unsigned g_sell10[N_ITEMS] = {
    60, 225, 480, 2000, 3690, 6100, 8400, 12650, 17160,     /* weapons */
    5, 15, 22, 444, 629, 959,                               /* clips */
    24, 1485, 120,                                          /* grenades */
    5400, 8500,                                             /* armour */
    650, 500,                                               /* alloys, elerium */
    4650,                                                   /* medi-kit */
    94, 12000, 1520, 4560,                                  /* stun rod .. scanner */
    /* live aliens */
    6000, 6000, 6000, 6000, 6000, 4000, 6000, 6000,
    6000, 6000, 6000, 4000, 6000, 6000, 6000, 6000,
    4000, 6000, 6000, 4000, 8000, 6000, 6000, 8000,
    8000, 4000, 6000, 4000, 4000,
    1012, 40, 300, 48, 14400, 1702,                         /* heavy weapons */
    2250, 101, 1200, 240, 1275, 720, 21100, 26730,          /* craft weapons */
    19470, 30400,                                           /* psi-amp, mind probe */
    25000, 8000, 500, 2000, 3800, 900, 10, 4000,            /* UFO components */
    2000, 2000, 2000, 2000, 2000,                           /* corpses */
    28100, 5330,                                            /* fusion ball */
    0, 0, 0,                                                /* craft */
    27, 30, 56, 52, 72, 96, 12, 40, 4,                      /* special ammo, grenades */
    /* HWPs: the hovertanks' $980K and $1043K do not fit 16 bits; 1 marks
       them (basescr.c sell10 knows their prices) */
    34000, 36000, 59400, 1, 1,
    10, 225, 3150,                                          /* HWP ammunition */
    11500,                                                  /* flying suit */
};

/* weapon, clip, impact, HUD tag (items.rul compatibleAmmo) */
const AmmoDef g_ammo[N_AMMO] = {
    { IT_HCANNON,   IT_CLIP_CANNON,      BLAST_NONE, "AP" },
    { IT_HCANNON,   IT_CLIP_CANNON_HE,   BLAST_HE,   "HE" },
    { IT_HCANNON,   IT_CLIP_CANNON_IN,   BLAST_FIRE, "IN" },
    { IT_ACANNON,   IT_CLIP_ACANNON,     BLAST_NONE, "AP" },
    { IT_ACANNON,   IT_CLIP_ACANNON_HE,  BLAST_HE,   "HE" },
    { IT_ACANNON,   IT_CLIP_ACANNON_IN,  BLAST_FIRE, "IN" },
    { IT_RLAUNCHER, IT_ROCKET,           BLAST_HE,   "S" },
    { IT_RLAUNCHER, IT_ROCKET_LARGE,     BLAST_HE,   "L" },
    { IT_RLAUNCHER, IT_ROCKET_IN,        BLAST_FIRE, "IN" },
    { IT_TANK_ROCKET, IT_HWP_ROCKETS,    BLAST_HE,   "R" },
    { IT_HOVER_LAUNCHER, IT_HWP_FUSION,  BLAST_HE,   "F" },
};

const AmmoDef *ammo_find(int weapon, int clip)
{
    int i;
    for (i = 0; i < N_AMMO; i++)
        if (g_ammo[i].weapon == weapon && g_ammo[i].clip == clip)
            return &g_ammo[i];
    return 0;
}

const ArmourDef g_armours[N_ARMOURS] = {
    { "NONE",       12,   8,  5,  2, NO_ITEM },
    { "PERSONAL ARMOUR", 50,  40, 30, 30, IT_ARMOUR },
    { "POWER SUIT",     100,  80, 70, 60, IT_POWERSUIT },
    { "FLYING SUIT",    110,  90, 80, 70, IT_FLYINGSUIT },
};

/* name, scientist-days, requires, requires, item in stores, race met,
   points (research.rul). The interrogations are DOS's, one per race and
   rank (the live alien is used up); the plasma clip topics are folded
   into the weapons. Any interrogation opens Alien Origins, a leader's or
   commander's The Martian Solution (with Alien Origins), a commander's
   Cydonia or Bust (with The Martian Solution), a navigator's the
   Hyper-wave Decoder, a sectoid leader's or commander's or any ethereal's
   the Psi Lab (basescr.c topic_available). */
const Topic g_topics[N_TOPICS] = {
    { "Laser Weapons",         50, REQ_NONE,     REQ_NONE,    NO_ITEM,     -1, 10 },
    { "Laser Pistol",         100, RS_LASER,     REQ_NONE,    NO_ITEM,     -1, 10 },
    { "Laser Rifle",          300, RS_LPISTOL,   REQ_NONE,    NO_ITEM,     -1, 10 },
    { "Heavy Laser",          460, RS_LRIFLE,    REQ_NONE,    NO_ITEM,     -1, 10 },
    { "Alien Alloys",         400, REQ_NONE,     REQ_NONE,    IT_ALLOYS,   -1, 30 },
    { "Personal Armour",      180, RS_ALLOYS,    REQ_NONE,    NO_ITEM,     -1, 20 },
    { "Elerium-115",          450, REQ_NONE,     REQ_NONE,    IT_ELERIUM,  -1, 60 },
    /* also needs UFO Power Source (topic_available) */
    { "Power Suit",           205, RS_ARMOUR,    RS_ELERIUM,  NO_ITEM,     -1, 20 },
    { "Plasma Pistol",        600, REQ_NONE,     REQ_NONE,    IT_PPISTOL,  -1, 20 },
    { "Plasma Rifle",         700, REQ_NONE,     REQ_NONE,    IT_PRIFLE,   -1, 25 },
    { "Heavy Plasma",         800, REQ_NONE,     REQ_NONE,    IT_HPLASMA,  -1, 30 },
    { "Alien Grenade",        200, REQ_NONE,     REQ_NONE,    IT_AGRENADE, -1, 20 },
    { "Sectoid autopsy",      180, REQ_NONE,     REQ_NONE,    IT_CORPSE_SECTOID,  0, 50 },
    { "Floater autopsy",      180, REQ_NONE,     REQ_NONE,    IT_CORPSE_FLOATER,  1, 50 },
    { "Snakeman autopsy",     180, REQ_NONE,     REQ_NONE,    IT_CORPSE_SNAKEMAN,  2, 50 },
    { "Muton autopsy",        180, REQ_NONE,     REQ_NONE,    IT_CORPSE_MUTON,  3, 50 },
    { "Ethereal autopsy",     180, REQ_NONE,     REQ_NONE,    IT_CORPSE_ETHEREAL,  4, 50 },
    { "Alien Origins",        300, REQ_NONE,     REQ_NONE,    NO_ITEM,     -1, 60 },
    { "The Martian Solution", 500, RS_ORIGINS,   REQ_NONE,    NO_ITEM,     -1, 60 },
    { "Cydonia or Bust",      600, RS_MARTIAN,   REQ_NONE,    NO_ITEM,     -1, 60 },
    { "Medi-Kit",             210, REQ_NONE,     REQ_NONE,    NO_ITEM,     -1, 20 },
    { "Motion Scanner",       180, REQ_NONE,     REQ_NONE,    NO_ITEM,     -1, 20 },
    { "Small Launcher",       550, REQ_NONE,     REQ_NONE,    IT_LAUNCHER, -1, 30 },
    { "Stun Bomb",            180, REQ_NONE,     REQ_NONE,    IT_STUNBOMB, -1, 10 },
    { "Blaster Launcher",     900, REQ_NONE,     REQ_NONE,    IT_BLASTER,  -1, 10 },
    { "Blaster Bomb",         300, REQ_NONE,     REQ_NONE,    IT_BLASTERBOMB, -1, 10 },
    { "Laser Cannon",         420, RS_HLASER,    REQ_NONE,    NO_ITEM,     -1, 15 },
    { "Plasma Cannon",        660, RS_HPLASMA,   RS_PRIFLE,   NO_ITEM,     -1, 25 },
    /* Psi Lab: interrogating a psionic captive (ethereal, sectoid leader or
       commander; base.c psi_captive) */
    { "Psi-Lab",              420, REQ_NONE,     REQ_NONE,    NO_ITEM,     -1, 25 },
    { "Psi-Amp",              500, RS_PSILAB,    REQ_NONE,    NO_ITEM,     -1, 20 },
    { "Mind Probe",           600, REQ_NONE,     REQ_NONE,    IT_MINDPROBE, -1, 25 },
    { "UFO Power Source",     450, REQ_NONE,     REQ_NONE,    IT_UFO_POWER, -1, 30 },
    { "UFO Navigation",       450, REQ_NONE,     REQ_NONE,    IT_UFO_NAV,  -1, 30 },
    /* also needs Alien Alloys and Elerium-115 (topic_available) */
    { "UFO Construction",     450, RS_POWER_SOURCE, RS_NAVIGATION, NO_ITEM, -1, 30 },
    { "New Fighter Craft",    600, RS_UFO_CONSTR, REQ_NONE,   NO_ITEM,     -1, 30 },
    { "New Fighter-Transporter", 700, RS_NEW_FIGHTER, REQ_NONE, NO_ITEM,   -1, 30 },
    { "Ultimate Craft",       900, RS_NEW_TRANSPORTER, REQ_NONE, NO_ITEM,  -1, 30 },
    { "Fusion Missile",       880, RS_BLASTER,   RS_BLASTERBOMB, NO_ITEM,  -1, 25 },
    { "Alien Food",           150, REQ_NONE,     REQ_NONE,    IT_ALIEN_FOOD, -1, 30 },
    { "Alien Reproduction",   150, REQ_NONE,     REQ_NONE,    IT_ALIEN_REPRO, -1, 30 },
    { "Alien Entertainment",  150, REQ_NONE,     REQ_NONE,    IT_ALIEN_ENTERT, -1, 30 },
    { "Alien Surgery",        150, REQ_NONE,     REQ_NONE,    IT_ALIEN_SURGERY, -1, 30 },
    { "Examination Room",     150, REQ_NONE,     REQ_NONE,    IT_EXAM_ROOM, -1, 30 },
    { "Laser Defence",        510, RS_LASER_CANNON, REQ_NONE, NO_ITEM,     -1, 15 },
    { "Plasma Defence",       620, RS_PLASMA_BEAM, REQ_NONE,  NO_ITEM,     -1, 25 },
    { "Fusion Defence",       800, RS_FUSION_MISSILE, REQ_NONE, NO_ITEM,   -1, 25 },
    { "Grav Shield",          930, RS_NEW_TRANSPORTER, REQ_NONE, NO_ITEM,  -1, 25 },
    { "Mind Shield",          360, RS_PSILAB,    REQ_NONE,    NO_ITEM,     -1, 25 },
    { "Hyper-wave Decoder",   670, REQ_NONE,     REQ_NONE,    NO_ITEM,     -1, 25 },
    /* Interrogations: the live alien of the race and rank (IT_LIVE + i) */
    { "Sectoid Commander", 190, REQ_NONE, REQ_NONE, IT_LIVE + 0, -1, 50 },
    { "Sectoid Leader", 192, REQ_NONE, REQ_NONE, IT_LIVE + 1, -1, 50 },
    { "Sectoid Engineer", 192, REQ_NONE, REQ_NONE, IT_LIVE + 2, -1, 50 },
    { "Sectoid Medic", 192, REQ_NONE, REQ_NONE, IT_LIVE + 3, -1, 50 },
    { "Sectoid Navigator", 192, REQ_NONE, REQ_NONE, IT_LIVE + 4, -1, 50 },
    { "Sectoid Soldier", 192, REQ_NONE, REQ_NONE, IT_LIVE + 5, -1, 50 },
    { "Floater Commander", 192, REQ_NONE, REQ_NONE, IT_LIVE + 6, -1, 50 },
    { "Floater Leader", 192, REQ_NONE, REQ_NONE, IT_LIVE + 7, -1, 50 },
    { "Floater Engineer", 192, REQ_NONE, REQ_NONE, IT_LIVE + 8, -1, 50 },
    { "Floater Medic", 192, REQ_NONE, REQ_NONE, IT_LIVE + 9, -1, 50 },
    { "Floater Navigator", 192, REQ_NONE, REQ_NONE, IT_LIVE + 10, -1, 50 },
    { "Floater Soldier", 192, REQ_NONE, REQ_NONE, IT_LIVE + 11, -1, 50 },
    { "Snakeman Commander", 192, REQ_NONE, REQ_NONE, IT_LIVE + 12, -1, 50 },
    { "Snakeman Leader", 192, REQ_NONE, REQ_NONE, IT_LIVE + 13, -1, 50 },
    { "Snakeman Engineer", 192, REQ_NONE, REQ_NONE, IT_LIVE + 14, -1, 50 },
    { "Snakeman Navigator", 192, REQ_NONE, REQ_NONE, IT_LIVE + 15, -1, 50 },
    { "Snakeman Soldier", 192, REQ_NONE, REQ_NONE, IT_LIVE + 16, -1, 50 },
    { "Muton Engineer", 192, REQ_NONE, REQ_NONE, IT_LIVE + 17, -1, 50 },
    { "Muton Navigator", 192, REQ_NONE, REQ_NONE, IT_LIVE + 18, -1, 50 },
    { "Muton Soldier", 192, REQ_NONE, REQ_NONE, IT_LIVE + 19, -1, 50 },
    { "Ethereal Commander", 192, REQ_NONE, REQ_NONE, IT_LIVE + 20, -1, 50 },
    { "Ethereal Leader", 192, REQ_NONE, REQ_NONE, IT_LIVE + 21, -1, 50 },
    { "Ethereal Soldier", 192, REQ_NONE, REQ_NONE, IT_LIVE + 22, -1, 50 },
    { "Cyberdisc Terrorist", 170, REQ_NONE, REQ_NONE, IT_LIVE + 23, -1, 50 },
    { "Sectopod Terrorist", 170, REQ_NONE, REQ_NONE, IT_LIVE + 24, -1, 50 },
    { "Celatid Terrorist", 170, REQ_NONE, REQ_NONE, IT_LIVE + 25, -1, 50 },
    { "Chryssalid Terrorist", 170, REQ_NONE, REQ_NONE, IT_LIVE + 26, -1, 50 },
    { "Silacoid Terrorist", 170, REQ_NONE, REQ_NONE, IT_LIVE + 27, -1, 50 },
    { "Reaper Terrorist", 170, REQ_NONE, REQ_NONE, IT_LIVE + 28, -1, 50 },
    { "Flying Suit",          330, RS_POWERSUIT, RS_NAVIGATION, NO_ITEM,     -1, 20 },
};

/* item, research, engineer-hours, $100, alloys, elerium, power sources,
   navigation, workshop space (manufacture.rul) */
const Recipe g_recipes[N_RECIPES] = {
    { IT_LPISTOL,      RS_LPISTOL,   300,   80, 0, 0, 0, 0,  2 },
    { IT_LRIFLE,       RS_LRIFLE,    400,  200, 0, 0, 0, 0,  3 },
    { IT_HLASER,       RS_HLASER,    700,  320, 0, 0, 0, 0,  4 },
    { IT_ARMOUR,       RS_ARMOUR,    800,  220, 4, 0, 0, 0, 12 },
    { IT_POWERSUIT,    RS_POWERSUIT,1000,  420, 5, 5, 0, 0, 16 },
    { IT_PPISTOL,      RS_PPISTOL,   600,  560, 1, 0, 0, 0,  3 },
    { IT_PRIFLE,       RS_PRIFLE,    820,  880, 1, 0, 0, 0,  4 },
    { IT_HPLASMA,      RS_HPLASMA,  1000, 1220, 1, 0, 0, 0,  4 },
    { IT_CLIP_PPISTOL, RS_PPISTOL,    60,   20, 0, 1, 0, 0,  4 },
    { IT_CLIP_PRIFLE,  RS_PRIFLE,     80,   30, 0, 2, 0, 0,  4 },
    { IT_CLIP_HPLASMA, RS_HPLASMA,    80,   60, 0, 3, 0, 0,  4 },
    { IT_AGRENADE,     RS_AGRENADE,  200,   67, 0, 2, 0, 0,  2 },
    { IT_MEDIKIT,      RS_MEDIKIT,   420,  280, 0, 0, 0, 0,  4 },
    { IT_SCANNER,      RS_SCANNER,   220,  340, 0, 0, 0, 0,  4 },
    { IT_LAUNCHER,     RS_LAUNCHER,  900,  780, 1, 0, 0, 0,  3 },
    { IT_STUNBOMB,     RS_STUNBOMB,  200,   70, 0, 1, 0, 0,  2 },
    { IT_BLASTER,      RS_BLASTER,  1200,  900, 1, 0, 0, 0,  5 },
    { IT_BLASTERBOMB,  RS_BLASTERBOMB,220,  80, 0, 3, 0, 0,  3 },
    { IT_LASER_CANNON, RS_LASER_CANNON, 300, 1820, 0, 0, 0, 0, 6 },
    { IT_PLASMA_BEAM,  RS_PLASMA_BEAM,  500, 2260, 0, 15, 0, 0, 8 },
    { IT_PSIAMP,       RS_PSIAMP,    500, 1600, 0, 1, 0, 0,  6 },
    { IT_MINDPROBE,    RS_MINDPROBE,1200, 2620, 0, 1, 0, 0,  4 },
    { IT_FUSION_L,     RS_FUSION_MISSILE, 400, 2420, 1, 0, 0, 0, 6 },
    { IT_FUSION_BALL,  RS_FUSION_MISSILE, 600,  280, 0, 4, 0, 0, 6 },
    { IT_SHIP_FIRESTORM, RS_NEW_FIGHTER,    14000, 4000,  65, 0, 1, 1, 30 },
    { IT_SHIP_LIGHTNING, RS_NEW_TRANSPORTER, 18000, 6000, 85, 0, 1, 1, 34 },
    { IT_SHIP_AVENGER,   RS_ULTIMATE,       34000, 9000, 120, 0, 2, 1, 36 },
    /* HWPs (manufacture.rul; the hovertanks and the fusion bomb also need
       New Fighter Craft: basescr.c recipe_available) */
    { IT_TANK_LASER,   RS_LASER_CANNON,    1200, 5000, 0, 0, 0, 0, 25 },
    { IT_HOVER_PLASMA, RS_PLASMA_BEAM,     1200, 8500, 5, 30, 0, 0, 30 },
    { IT_HOVER_LAUNCHER, RS_FUSION_MISSILE, 1400, 9000, 8, 25, 0, 0, 30 },
    { IT_HWP_FUSION,   RS_FUSION_MISSILE,   400,  150, 8, 5, 0, 0, 25 },
    { IT_FLYINGSUIT,   RS_FLYINGSUIT,      1400,  580, 5, 16, 0, 0, 16 },
};

/* Purchase prices, $10 units (items.rul costBuy / 10). */
const unsigned g_buy10[N_ITEMS] = {
    80, 300, 640, 0, 0, 0, 0, 0, 0,                         /* weapons */
    7, 20, 30, 0, 0, 0,                                     /* clips */
    30, 0, 150,                                             /* grenades */
    0, 0, 0, 0, 0,                                          /* armour .. medi-kit */
    126, 0, 0, 0,                                           /* stun rod .. scanner */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,            /* live aliens */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1350, 50, 400, 60, 0, 0,                                /* heavy weapons */
    3000, 124, 1600, 300, 1700, 900, 0, 0,                  /* craft weapons */
    0, 0,                                                   /* psi-amp, mind probe */
    0, 0, 0, 0, 0, 0, 0, 0,                                 /* UFO components */
    0, 0, 0, 0, 0,                                          /* corpses */
    0, 0,                                                   /* fusion ball */
    0, 0, 0,                                                /* craft */
    50, 40, 70, 65, 90, 120, 15, 50, 6,                     /* special ammo, grenades */
    42000, 48000, 0, 0, 0,                                  /* HWPs */
    20, 300, 0,                                             /* HWP ammunition */
};

/* Delivery: craft weapons and missiles 48 hours, cannon rounds and HWPs
   96, HWP ammunition 48, the rest 24 (items.rul transferTime). */
int item_transfer_hours(int item)
{
    if (item == IT_CANNON_ROUNDS || g_items[item].kind == IK_HWP)
        return 96;
    if (item >= IT_HWP_SHELLS)
        return 48;
    return g_items[item].kind == IK_CRAFT ? 48 : 24;
}
