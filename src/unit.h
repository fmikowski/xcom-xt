/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UNIT_H
#define UNIT_H

#define MAX_UNITS       60      /* squad (at most 26 soldiers: MAX_SQUAD), crew
                                   (Superhuman: 24 at a terror site, 28 on a
                                   battleship), terror units, 16 civilians */
#define FACTION_XCOM    0
#define FACTION_ALIEN   1
#define FACTION_CIVILIAN 2      /* neutral: aliens attack them */

/* Hostile pairs: aliens against everyone else. */
#define HOSTILE(a, b)   (((a) == FACTION_ALIEN) != ((b) == FACTION_ALIEN))

/* Unit.special */
#define SP_ZOMBIFY      1       /* chryssalid: melee victims become zombies */
#define SP_ZOMBIE       2       /* a chryssalid bursts out when it dies */
#define SP_GONE         4       /* dead without a body: risen as a zombie, blown
                                   up, or left outside the craft by an abort */
#define SP_CARRIED      8       /* its body is in an inventory (inv.h IT_BODY) */
#define SP_LEFT         16      /* soldiers: the left hand is the active one */
#define SP_MIRROR       32      /* soldiers: hands swapped for an action (inv_mirror) */

#include "items.h"
#include "pak.h"

#define SHOT_SNAP   0
#define SHOT_AIMED  1
#define SHOT_AUTO   2

#define PACK_SLOTS  4

typedef struct {
    unsigned char x, y, z, dir;
    unsigned char faction;
    unsigned char alive;
    unsigned char sprite_set;       /* index into g_pak.unit_base */
    unsigned char phase;            /* 0 = standing, 1..8 = walk phase */
    unsigned char seen;             /* aliens: visible to X-COM */
    unsigned char tu, tu_max;
    unsigned char hp, hp_max;
    unsigned char accuracy;         /* firing accuracy, percent */
    unsigned char reactions;
    unsigned char armor[3];         /* front, side, rear */
    unsigned char weapon, ammo;     /* weapon item (NO_ITEM = unarmed), rounds */
    char name[14];
    unsigned char kills;            /* this battle */
    signed char roster;             /* X-COM: index in g_roster, -1 = none */
    unsigned char armour;           /* armour type (sprite set chosen by it) */
    unsigned char grenade, n_grenades, clips;   /* soldiers: the left hand's grenade
                                       (n_grenades 1), their clips are in the
                                       inventory (inv.h); aliens: belt clips,
                                       grenades */
    unsigned char kneeling;
    unsigned char morale, bravery, medi;    /* medi: medi-kit uses left */
    unsigned char panicked;
    unsigned char rank;            /* aliens: 0 commander, 1 leader, ... */
    unsigned char stun;             /* stun damage taken */
    unsigned char stunned;          /* unconscious (alive = 0 too) */
    unsigned char moved;            /* aliens: moved this turn (motion scanner) */
    unsigned char wounds;           /* fatal wounds: HP lost every turn */
    unsigned char psi;              /* psi strength; aliens: 0 = no psionics */
    unsigned char hits, reacted;    /* experience this battle */
    unsigned char big;              /* 2x2: also x+1, y+1, x+1/y+1 */
    unsigned char melee;            /* melee power, 0 = none */
    unsigned char special;          /* SP_ */
    unsigned char psi_skill;        /* X-COM: trained psi skill (psi.c) */
    unsigned char psi_state;        /* PSI_CTRL, PSI_PROBED */
    unsigned char psi_exp;          /* successful psi attacks this battle */
    unsigned char load;             /* clip type loaded, 0 = the weapon's standard */
    /* Battle rules as in DOS (src/rules.c, docs/design.md "Battle rules vs DOS") */
    unsigned char strength, throwing;
    unsigned char stamina, energy;  /* energy: spent walking, recovered every turn */
    unsigned char en_rec;           /* energy recovered a turn */
    unsigned char armor_under;      /* explosions at its feet */
    unsigned char xtype;            /* XT_*: damage modifiers, height, body width */
    unsigned char wound[6];         /* fatal wounds by body part (BP_*); wounds = sum */
    unsigned char exp_throw, exp_brave;     /* experience: throws, panic resisted */
    unsigned char stims, pkills;    /* medi-kit stimulants and painkillers left (medi: heals) */
    unsigned char morale_restored;  /* painkillers: health loss already made up */
    unsigned char armor_left;       /* armor[1] is the right side */
    unsigned char live;             /* aliens: live alien item IT_LIVE + live - 1, 0 none */
    unsigned char turret;           /* HWPs: the turret's direction (the body's
                                       when it walks or turns, the target's
                                       when it fires) */
    unsigned char fuse;             /* the belt grenade primed in hand: its timer + 1
                                       (PrimeGrenadeState), 0 = not primed */
    unsigned char burning;          /* turns the unit stays on fire (BattleUnit::_fire);
                                       | UB_HIT: burnt by a tile this round */
} Unit;

#define UB_HIT      0x80

/* Body parts (OpenXcom order). */
#define BP_HEAD     0
#define BP_TORSO    1
#define BP_RARM     2
#define BP_LARM     3
#define BP_RLEG     4
#define BP_LLEG     5

/* Unit.xtype: per-type rules (unit_xstat): race pack ranks 0-7, then the
   X-COM armours, civilians and zombies. */
#define XT_XCOM     8               /* + armour type */
#define XT_CIVILIAN 12
#define XT_ZOMBIE   13
#define XT_TANK     14              /* HWPs (src/hwp.h) */
#define XT_HOVER    15              /* hovertanks: they fly */
#define UNIT_HWP(u) ((u)->xtype >= XT_TANK)
/* An X-COM soldier by birth (inventory as DOS's, inv.h). */
#define UNIT_SOLDIER(u) ((u)->xtype >= XT_XCOM && (u)->xtype < XT_CIVILIAN)

/* Unit.panicked */
#define PANIC_FREEZE    1
#define PANIC_BERSERK   2

/* Unit.psi_state */
#define PSI_CTRL        1       /* alien under X-COM mind control (faction XCOM) */
#define PSI_PROBED      2       /* soldier: psi strength seen with a mind probe */
#define PSI_ALIEN       4       /* soldier under alien control (faction ALIEN) */
#define PSI_HIT         8       /* not psi: DOS's hit state (BattleUnit::_hitByAnything),
                                   hurt since the last unit selection or AI move
                                   (bt_hit_reset): it reacts all round */

/* The campaign's soldiers, kept between battles. Every base's are in the
   bases block's pool (far, the geoscape only; base.h); g_roster points at
   the current base's there. During a battle only the squad's records are
   in memory (a DOS block of their size, roster_squad_alloc). Bases hold
   as many people as their living quarters, as DOS, and MAX_SOLDIERS in
   all (DOS SOLDIER.DAT has 250 records). */
#define MAX_ROSTER   MAX_SOLDIERS   /* a base: its living quarters only, as DOS */
#define MAX_SOLDIERS 250            /* every base's together (the pool; DOS SOLDIER.DAT) */
#define MAX_SQUAD    26             /* a battle's (the Avenger's seats; base defence) */
#define LAYOUT_N   26             /* all 23 cells, both hands, a special clip (more: left out) */
typedef struct {
    char name[14];
    unsigned char tu, hp, accuracy, reactions;
    unsigned char missions, kills, alive;
    unsigned char armour;
    unsigned char bravery;
    /* Equipment layout (Soldier::_equipmentLayout, saved when the
       inventory before a battle closes): item and slot (inv.h SLOT; a
       weapon with special ammunition: then its clip at SEC_LOAD, the
       EquipmentLayoutItem's ammoItem), NO_ITEM ends. The battle's setup gives the soldier these
       items from the craft's (src/hwpscr.c). */
    unsigned char layout[LAYOUT_N][2];
    unsigned char rank;             /* RANK_* */
    unsigned char psi;              /* psi strength (hidden) */
    unsigned char psi_skill;        /* 0 = never trained */
    unsigned char psi_flags;        /* PSF_* */
    unsigned char stamina, strength, throwing;
    unsigned char tu0;              /* TU when recruited: energy recovery a turn is a third */
    unsigned char recovery;         /* days in hospital (wounds), 0 = fit */
    unsigned char transit;          /* hours until a transfer arrives, 0 = here */
    unsigned char craft;            /* crew of g_craft[craft - 1], 0 = none */
} Soldier;

/* Fit for a mission: alive, not wounded, at the base. */
#define SOLDIER_READY(s)    ((s)->alive && !(s)->recovery && !(s)->transit)

/* Soldier.psi_flags */
#define PSF_TRAIN       1       /* in psi training (psi lab) */
#define PSF_KNOWN       2       /* psi strength known: trained or mind-probed */

#define RANK_ROOKIE     0
#define RANK_SQUADDIE   1
#define RANK_SERGEANT   2
#define RANK_CAPTAIN    3
#define RANK_COLONEL    4
#define RANK_COMMANDER  5
extern const char *const g_rank_names[6];       /* DOS: "Rookie", "Squaddie", ... */

/* The current base's soldiers (geoscape), the squad's (battle); NULL: no
   roster (quick battles: new soldiers every battle). */
extern Soldier *g_roster;
/* Battle: squad soldier i is soldier g_squad_idx[i] of the base (0xFF: none). */
extern unsigned char g_squad_idx[MAX_SQUAD];
extern unsigned char g_armour_set[N_ARMOURS];   /* filled when packs load */
extern unsigned char g_quick_armour;            /* quick battles (debug option) */
extern unsigned char g_quick_gear;              /* quick battles: stun and scanner kit */
extern unsigned char g_quick_psi;               /* quick battles: psi skill, Psi-Amps */
extern int g_squad_craft;           /* battle squad: that craft's crew (Soldier.craft), 0 = the fit ones */
extern int g_roster_n;              /* soldiers at g_roster */
extern int g_squad_max;             /* soldiers the transport carries (Skyranger
                                       14, Lightning 12, Avenger 26; the battle
                                       places as many as there are start tiles) */

void roster_new(int n);             /* a fresh squad of n (base.c) */
int roster_recruit(void);           /* adds a recruit; index or -1 if full (base.c) */
int roster_alive(void);
int roster_ready(void);             /* SOLDIER_READY ones */
/* The squad's records for a battle: a DOS block of n (at least one, so
   that a campaign battle of HWPs alone still has a roster) becomes
   g_roster; 0 = ok. roster_squad_free gives it back (g_roster NULL). */
int roster_squad_alloc(int n);
void roster_squad_free(void);
int roster_squad_on(void);          /* g_roster is the squad's block */
/* Deaths, kills, experience, wound recovery days of the squad; the
   records go home to the pool when the bases come back (base_unpark,
   which drops the dead; promotions: base_promote_all, over every base). */
void roster_after_battle(void);
extern int g_squad_home;            /* 1: base_unpark writes the squad back */

/* Months since the campaign began (geo.c; 0 in quick battles): the
   aliens' item level (alienItemLevels.rul) grows with it. */
extern int g_months_played;

extern Unit g_units[MAX_UNITS];
extern int g_nunits;
extern unsigned char *g_unit_at;    /* per tile: unit index + 1, 0 = none */

#define UNIT_AT(x, y, z) \
    g_unit_at[(((unsigned)(z) * g_my + (unsigned)(y)) * g_mx) + (unsigned)(x)]

/* Places the squad in the craft and the pack's alien crew in and around
   the UFO (at most aliens per crew entry). 0 = ok. */
int units_spawn(int soldiers, int aliens);

/* The craft or exit area a soldier leaves the battle from when it is
   aborted: the craft's start tiles, on Mars (Cydonia's first stage) the
   exit area (END_POINT tiles) that leads to the second stage. */
int unit_in_exit_area(const Unit *u);

/* Cydonia's two stages (DOS BattlescapeGenerator::nextStage): after the
   first one (won: every soldier still alive or unconscious; aborted: those
   in the exit area, abort_evacuate having lost the others and the items
   outside) the survivors go on as they are (health, wounds, stun,
   what they carry) and so do the items on the ground (after an abort,
   those in the exit area). The fallen are marked dead in the roster
   (dropped after the second stage). Returns the soldiers carried; the
   next units_spawn places them instead of the roster's. */
int roster_stage_carry(void);
extern int g_carried;               /* units 0..g_carried-1 came from the first stage */

void unit_move_to(int u, int x, int y, int z);
/* A new unit of a faction at a tile (its first quarter): index or -1. */
int add_unit(int faction, int x, int y, int z, int dir);
void unit_kill(int u);

/* The body unit u leaves on the ground when it is dead or unconscious
   (inv.h BODY_*: its armours.rul corpseBattle), whatever its state now;
   -1 for none (zombies, SP_GONE). */
int unit_body(const Unit *u);

/* 1 if unit u (its whole footprint) fits at the tile: standable and free
   of other units. */
int unit_fits(int u, int x, int y, int z);
/* ... a unit of that size (u: itself, -1 none); loose: only free of others
   (unit.c, for the setup in spawn.c). */
int unit_fits_at(int u, int big, int x, int y, int z, int loose);
int unit_standable(int x, int y, int z);    /* in the map, free, standable */
#define N_NAMES 12
extern const char *const g_unit_names[N_NAMES];     /* quick battles' soldiers */
extern int g_carry_n;               /* soldiers carried to Cydonia's second stage */
/* ... only free of others (a 2x2 unit walking where the path search let
   it: quarters over nothing or on the level above, path.c big_update). */
int unit_free_at(int u, int x, int y, int z);

/* A new unit of a race type (terror spawns, zombies) at a tile, in a free
   slot or a dead civilian's; -1 if there is no room. */
int unit_spawn_type(const AlienType *t, int x, int y, int z, int dir);

/* Raises a zombie where unit u died; its index, or -1. */
int unit_spawn_zombie(int u);

/* Civilians on the map at the start, killed by aliens and by X-COM. */
extern int g_civ_total, g_civ_killed_alien, g_civ_killed_xcom;

/* TU cost of a shot in the given mode (SHOT_*), 0 if the weapon has no
   such mode or the unit is unarmed. */
int unit_shot_tu(const Unit *u, int mode);

/* Firing accuracy as OpenXcom BattleUnit::getFiringAccuracy: firing x the
   mode's accuracy, x 1.15 kneeling, x the health and wounds modifier. */
int unit_shot_acc(const Unit *u, int mode);

/* Health and wounds modifier (percent, at least 10): 75 + 25 x health /
   max - 10 per wound in the head and the arm(s) holding the item (the
   left arm for the left hand's item, SP_MIRROR). */
int unit_acc_mod(const Unit *u, int item);

/* Two-handed items (rifles, heavy weapons, launchers, stun rod, Psi-Amp). */
int item_two_handed(int item);

/* Per-type battle rules of a unit (Unit.xtype). */
const UnitXStat *unit_xstat(const Unit *u);
/* The name DOS shows: an alien's is its live alien's (race and rank, "Sectoid
   Commander", "Cyberdisc Terrorist"), built from Unit.live, not kept; the
   others' Unit.name. */
const char *unit_title(const Unit *u);
/* sprintf(text, fmt, unit_title(v), n), returned: one call, not a sprintf's pushes. */
char *unit_fmt(char *text, const char *fmt, const Unit *v, int n);

/* 1 if the unit flies (armors.rul movementType 1: the Flying Suit,
   hovertanks, floaters, ethereals, cyberdiscs: race packs' AT_FLY). */
int unit_flies(const Unit *u);

/* Direction from one tile to another (0-7). */
int dir_towards(int fx, int fy, int tx, int ty);

#endif
