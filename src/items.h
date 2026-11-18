/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ITEMS_H
#define ITEMS_H

/* Items, armour, research topics and manufacturing (see docs/roadmap.md). */

enum {
    IT_PISTOL, IT_RIFLE, IT_HCANNON, IT_LPISTOL, IT_LRIFLE, IT_HLASER,
    IT_PPISTOL, IT_PRIFLE, IT_HPLASMA,
    IT_CLIP_PISTOL, IT_CLIP_RIFLE, IT_CLIP_CANNON, IT_CLIP_PPISTOL,
    IT_CLIP_PRIFLE, IT_CLIP_HPLASMA,
    IT_GRENADE, IT_AGRENADE, IT_HE,
    IT_ARMOUR, IT_POWERSUIT,
    IT_ALLOYS, IT_ELERIUM,
    IT_MEDIKIT,
    IT_STUNROD, IT_LAUNCHER, IT_STUNBOMB, IT_SCANNER,
    IT_LIVE, IT_LIVE_LAST = IT_LIVE + 28,   /* live aliens by race and rank (N_LIVE) */
    IT_ACANNON, IT_CLIP_ACANNON, IT_RLAUNCHER, IT_ROCKET, IT_BLASTER, IT_BLASTERBOMB,
    /* Craft weapons and their ammunition (src/craft.c) */
    IT_CRAFT_CANNON, IT_CANNON_ROUNDS, IT_STINGRAY_L, IT_STINGRAY_M,
    IT_AVALANCHE_L, IT_AVALANCHE_M, IT_LASER_CANNON, IT_PLASMA_BEAM,
    /* Psionics (src/psi.c, src/psibat.c): carried in the backpack */
    IT_PSIAMP, IT_MINDPROBE,
    /* UFO components and alien corpses (recovered, researched, sold) */
    IT_UFO_POWER, IT_UFO_NAV, IT_ALIEN_FOOD, IT_ALIEN_ENTERT, IT_ALIEN_SURGERY,
    IT_EXAM_ROOM, IT_ALIEN_HABITAT, IT_ALIEN_REPRO,
    IT_CORPSE_SECTOID, IT_CORPSE_FLOATER, IT_CORPSE_SNAKEMAN, IT_CORPSE_MUTON,
    IT_CORPSE_ETHEREAL,
    IT_FUSION_L, IT_FUSION_BALL,        /* Fusion Ball launcher and ammunition */
    /* Manufactured craft: never in stores, they go to a hangar (craft.c) */
    IT_SHIP_FIRESTORM, IT_SHIP_LIGHTNING, IT_SHIP_AVENGER,
    /* Ammunition variety and special grenades (src/effects.c) */
    IT_CLIP_CANNON_HE, IT_CLIP_CANNON_IN, IT_CLIP_ACANNON_HE, IT_CLIP_ACANNON_IN,
    IT_ROCKET_LARGE, IT_ROCKET_IN,
    IT_SMOKE, IT_PROXIMITY, IT_FLARE,
    /* Heavy weapons platforms (src/hwp.c): the HWP itself, which is also
       its weapon in battle, and the HWP ammunition (items.rul) */
    IT_TANK_CANNON, IT_TANK_ROCKET, IT_TANK_LASER, IT_HOVER_PLASMA, IT_HOVER_LAUNCHER,
    IT_HWP_SHELLS, IT_HWP_ROCKETS, IT_HWP_FUSION,
    IT_FLYINGSUIT,                      /* the third armour (after the HWPs: saves) */
    N_ITEMS
};

/* Items with hand and backpack pictures (src/uipak.h UI_BIGOB, UI_ICON): the
   armour added last never is in a hand. */
enum {
    N_PICTURED = IT_FLYINGSUIT
};

#define IK_WEAPON   0
#define IK_CLIP     1
#define IK_GRENADE  2
#define IK_ARMOUR   3
#define IK_MATERIAL 4
#define IK_EQUIP    5
#define IK_CRAFT    6                   /* craft weapon or craft ammunition */
#define IK_SHIP     7                   /* a craft (manufacture only) */
#define IK_HWP      8                   /* a heavy weapons platform (src/hwp.c) */

#define NO_ITEM     0xFF

typedef struct {
    const char *name;
    unsigned char kind;
    unsigned char power;
    unsigned char acc_snap, tu_snap;    /* percent; TU in % of max */
    unsigned char acc_aimed, tu_aimed;
    unsigned char acc_auto, tu_auto;    /* 0 = no auto fire */
    unsigned char clip_size;            /* 0 = no ammo needed (lasers) */
    unsigned char clip_item;            /* NO_ITEM for none */
    unsigned char buy_k;                /* price in $K rounded up (shown), 0 = cannot buy */
} ItemDef;

extern const ItemDef g_items[N_ITEMS];

/* What a round does on impact. */
#define BLAST_NONE  0
#define BLAST_STUN  1               /* stun bomb */
#define BLAST_HE    2               /* explodes (rockets, HE rounds) */
#define BLAST_FIRE  3               /* incendiary: sets the area burning */

/* Clips a weapon takes, as in DOS: Heavy Cannon and Auto-Cannon AP/HE/IN,
   Rocket Launcher small/large/incendiary. The weapon's clip_item is the
   standard one (belt clips); a clip's power, if not 0, replaces the
   weapon's. */
typedef struct {
    unsigned char weapon, clip, blast;
    char tag[3];                    /* short name on the HUD */
} AmmoDef;

#define N_AMMO 11
extern const AmmoDef g_ammo[N_AMMO];

/* The g_ammo entry for a weapon and clip, or NULL. */
const AmmoDef *ammo_find(int weapon, int clip);

/* Armour types: 0 none, 1 personal armour, 2 power suit, 3 flying suit. */
#define N_ARMOURS 4
#define ARM_FLYING 3                    /* it flies (armors.rul movementType 1) */
typedef struct {
    const char *name;
    unsigned char front, side, rear, under;
    unsigned char item;                 /* stores item, NO_ITEM for none */
} ArmourDef;

extern const ArmourDef g_armours[N_ARMOURS];

/* Research topics. */
enum {
    RS_LASER, RS_LPISTOL, RS_LRIFLE, RS_HLASER, RS_ALLOYS, RS_ARMOUR,
    RS_ELERIUM, RS_POWERSUIT, RS_PPISTOL, RS_PRIFLE, RS_HPLASMA, RS_AGRENADE,
    RS_AUT_SECTOID, RS_AUT_FLOATER, RS_AUT_SNAKEMAN, RS_AUT_MUTON, RS_AUT_ETHEREAL,
    RS_ORIGINS, RS_MARTIAN, RS_CYDONIA,
    RS_MEDIKIT,
    RS_SCANNER, RS_LAUNCHER, RS_STUNBOMB,
    RS_BLASTER, RS_BLASTERBOMB,
    RS_LASER_CANNON, RS_PLASMA_BEAM,
    RS_PSILAB, RS_PSIAMP, RS_MINDPROBE,
    /* UFO technology and the alien-tech craft (research.rul) */
    RS_POWER_SOURCE, RS_NAVIGATION, RS_UFO_CONSTR, RS_NEW_FIGHTER,
    RS_NEW_TRANSPORTER, RS_ULTIMATE, RS_FUSION_MISSILE,
    RS_ALIEN_FOOD, RS_ALIEN_REPRO, RS_ALIEN_ENTERT, RS_ALIEN_SURGERY, RS_EXAM_ROOM,
    /* Base facilities (research.rul) */
    RS_LASER_DEF, RS_PLASMA_DEF, RS_FUSION_DEF, RS_GRAV_SHIELD, RS_MIND_SHIELD, RS_HYPERWAVE,
    /* Interrogations as in DOS, one per race and rank: RS_INT + i needs
       live alien IT_LIVE + i */
    RS_INT, RS_INT_LAST = RS_INT + 28,
    RS_FLYINGSUIT,
    N_TOPICS
};

#define REQ_NONE 0xFF

/* Live aliens (research.rul and items.rul STR_SECTOID_COMMANDER ...):
   sectoid, floater commander, leader, engineer, medic, navigator,
   soldier; snakeman commander, leader, engineer, navigator, soldier;
   muton engineer, navigator, soldier; ethereal commander, leader,
   soldier; cyberdisc, sectopod, celatid, chryssalid, silacoid, reaper
   terrorists. A unit's live alien is its race pack's (UnitXStat.live). */
#define N_LIVE 29

typedef struct {
    const char *name;
    unsigned cost;                      /* scientist-days */
    unsigned char req1, req2;           /* topics, REQ_NONE */
    unsigned char need_item;            /* item that must be in stores */
    signed char race;                   /* autopsy: race met (0-4), -1 none */
    unsigned char points;               /* score when discovered (research.rul) */
} Topic;

extern const Topic g_topics[N_TOPICS];

typedef struct {
    unsigned char item;
    unsigned char topic;                /* research needed */
    unsigned hours;                     /* engineer-hours each */
    unsigned cost100;                   /* cost of each, in $100 */
    unsigned char alloys, elerium;
    unsigned char power, nav;           /* UFO power sources, navigation (craft) */
    unsigned char space;                /* workshop space the project takes */
} Recipe;

#define N_RECIPES 32
extern const Recipe g_recipes[N_RECIPES];

/* Sale prices in units of $10 (items.rul costSell), 0 = cannot be sold. */
extern const unsigned g_sell10[N_ITEMS];

/* Purchase prices in $10 (items.rul costBuy) and delivery times in hours
   (transferTime, 24 if not given). */
extern const unsigned g_buy10[N_ITEMS];
int item_transfer_hours(int item);

#endif
