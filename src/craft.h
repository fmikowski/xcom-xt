/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CRAFT_H
#define CRAFT_H

#include "afwin.h"
#include "items.h"

/* Interceptors: hangar state (repair, rearm, refuel), craft weapons and
   the air combat screen. Flight on the world map is in geo.c. Values
   follow the original rules (crafts.rul, craftWeapons.rul, ufos.rul). */

/* All bases' craft together (Craft.base). DOS keeps every craft, base, UFO,
   site and way point in one list of 50 (LOC.DAT, OpenXcom SaveConverter):
   with 8 hangars a base at most 44 craft (6 bases); the bases block
   (geoscape memory only) holds them. */
#define MAX_CRAFT   44

enum { CS_EMPTY, CS_HANGAR, CS_OUT, CS_RETURN, CS_ORDERED };

/* Destinations of a craft out (CS_OUT): Craft.target >= 0 a UFO slot
   (target_id its id), CT_WAYPOINT way point pad (g_wp), CT_SITE site pad,
   CT_PATROL none (it patrols where it is, DOS destination 0). */
#define CT_WAYPOINT (-1)
#define CT_SITE     (-2)
#define CT_PATROL   (-3)

/* Craft types (g_craft_types, Craft.type). */
enum { CT_INTERCEPTOR, CT_FIRESTORM, CT_LIGHTNING, CT_AVENGER, CT_SKYRANGER, N_CRAFT_TYPES };

enum { CW_CANNON, CW_STINGRAY, CW_AVALANCHE, CW_LASER, CW_PLASMA, CW_FUSION, N_CRAFT_WEAPONS };
#define CW_NONE 0xFF

typedef struct {
    unsigned char state, type;          /* CS_*, index into g_craft_types */
    unsigned char base, pad;            /* home base */
    unsigned char weapon[2];            /* CW_*, CW_NONE */
    int ammo[2];
    int num;                            /* Interceptor-<num> */
    int x, y;                           /* world map, 1/16 pixel */
    int fuel, damage;
    int target, target_id;              /* UFO slot and that UFO's id, or CT_* */
    int timer;                          /* hours until an ordered or transferred
                                           craft arrives; out: 1 = at its target,
                                           landing asked */
    unsigned char hwp[4];               /* HWPs aboard (IT_TANK_CANNON ..
                                           IT_HOVER_LAUNCHER, 0 = none), each with
                                           its full load of ammunition, taken
                                           from the stores (src/hwpscr.c) */
    unsigned char items[N_ITEMS];       /* equipment aboard (CraftEquipmentState:
                                           taken from the stores, counted in the
                                           base's stores space; src/hwpscr.c) */
} Craft;

typedef struct {
    const char *name;
    int fuel, hp;
    int speed;                          /* world map pixels per hour */
    int fuel_hour;                      /* fuel used per hour of flight */
    int cost_k, score;
    unsigned char weapons;              /* weapon slots */
    unsigned char soldiers;             /* squad carried into battle, 0 = none */
    unsigned char elerium;              /* refuels with Elerium-115 (fuel in tenths) */
    unsigned char vehicles;             /* HWPs carried (crafts.rul), 4 seats each */
} CraftType;

typedef struct {
    const char *name;
    unsigned char launcher, ammo_item;  /* stores items; ammo NO_ITEM: none */
    unsigned char damage, range, accuracy;
    unsigned char reload[3];            /* ticks: cautious, standard, aggressive */
    int ammo_max, per_item, rearm;      /* rounds per stores item, rounds per hour */
} CraftWeapon;

/* UFO combat values by geoscape UFO type (1 small scout .. 8 supply ship). */
typedef struct {
    int hp, power, range, reload, breakoff, score;
    int speed;                          /* top speed when breaking off, pixels/hour */
    int size;                           /* 0 very small .. 4 very large */
} UfoCombat;

#define N_UFO_COMBAT 9

/* g_craft[MAX_CRAFT] lives in the bases block (base.h). */
#define g_craft     (g_bb->craft)
extern const CraftType g_craft_types[N_CRAFT_TYPES];
extern const CraftWeapon g_craft_weapons[N_CRAFT_WEAPONS];
extern const UfoCombat g_ufo_combat[N_UFO_COMBAT];

/* The Skyranger and two armed Interceptors, as the original starting
   base (base 0). */
void craft_new(void);

/* One hour in the hangars of every base: repair, else rearm one weapon
   from the base's stores; ordered and transferred craft arrive. Returns a
   message or NULL. Every half hour craft with nothing else to do refuel. */
const char *craft_hour(void);
void craft_half_hour(void);
/* Craft::checkup: a craft arrived at its base (flew in, delivered) sets
   its status from its damage and weapons (src/cbay.c). A craft in its
   hangar keeps Craft.cpp's status and its weapons' rearming flags in
   Craft.pad (free there; out, pad is the weapons-off bits or a way point
   / site): */
void craft_checkup(Craft *c);
void craft_rearm(Craft *c, int k);          /* an hour of rearming (src/cbay.c) */
void craft_debug_lose(void);                /* DEBUG key K: a transport shot down */
#define HP_R(s)     (1 << (s))  /* weapon s rearming (CraftWeapon::_rearming) */
#define HP_ARM      4           /* STR_REARMING */
#define HP_READY    8           /* STR_READY */
#define HP_LOW      16          /* Craft::_lowFuel: Elerium asked for once */
/* STR_REPAIRS: damage > 0; neither HP_ARM nor HP_READY: STR_REFUELLING. */

/* The hangar's rates (crafts.rul repairRate 1, refuelRate 50, 5 for Elerium craft,
   whose fuel is in tenths; craftWeapons.rul rearmRate, CraftWeapon.rearm). */
#define REPAIR_HOUR 1           /* damage points repaired per hour (repairRate) */
#define REFUEL_HALF 50          /* fuel per half hour (refuelRate) */
#define ELERIUM_FUEL    50      /* fuel tenths per Elerium-115 (refuelRate 5) */

/* Craft c is at the current base (or on its way to it). */
#define CRAFT_HERE(c)   ((c)->state != CS_EMPTY && (c)->base == g_bcur)

/* Ready (repaired, rearmed, refuelled); and armed. */
int craft_ready(const Craft *c);
int craft_can_launch(const Craft *c);

/* Monthly rent of the current base's craft, $K (Skyrangers and
   Interceptors). */
long craft_rent(void);

const char *craft_name(const Craft *c);
const char *craft_status(const Craft *c);    /* "Ready", "Refuelling", ... */

/* The craft screens (from the base menu, Equip Craft in DOS; src/hangar.c):
   the base's craft, a craft's own screen with its weapon boxes and the
   Crew, Equipment and Armour buttons of a transport. Screens in other
   overlay sections are the geoscape's to run: craft_screens returns
   CR_EQUIP (Equipment: hwp_equip_screen, src/hwpscr.c), CR_CREW
   (crew_screen, Select Squad) or CR_ARMOUR (armour_screen, Select Armor,
   both src/crewscr.c) with *k the craft; the geoscape calls it again, *k
   unchanged, to come back to that craft's screen. *k = -1 starts at the
   list; CR_DONE: finished (*k = -1). */
enum { CR_DONE, CR_EQUIP, CR_CREW, CR_ARMOUR };
/* A weapon chosen on the craft's screen (None or CW_*) is fitted by
   armour_screen (src/crewscr.c), which the geoscape calls for CR_ARMOUR and
   any other number: craft_screens hands over the craft, the slot and the
   weapon in *k, CR_FIT(slot, w), and takes them off again when it is called
   back (no screen of the craft's section runs another one: the sections next
   to the geoscape load one at a time, and no new overlay entry is needed). */
#define CR_FIT(slot, w)     (((slot) << 5) | ((unsigned char)((w) + 2) << 6))
int craft_screens(int *k);
void crew_screen(int k);
void armour_screen(int k);

/* Hangars of the current base: in use (every craft of the base, also on
   its way, one being manufactured) and free for a new craft. */
int craft_hangars_used(void);
int craft_hangar_free(void);

/* A manufactured craft of type CT_* arrives in a hangar of the current
   base, unarmed; 0 if there is no room. */
int craft_add(int type);
/* An empty craft slot set up for a new craft of that type at the current
   base (state CS_ORDERED: the caller sets it), or NULL. */
Craft *craft_new_slot(int type);

/* Transports: every craft with seats flies its own troop mission (DOS).
   The current base's ready transport with the most seats (Skyranger,
   Lightning, Avenger), -1 none (the Enter key). */
int craft_best_transport(void);
/* The craft whose squad fights the battle now (g_troop_craft, -1 none)
   and its name for the briefing (b: unused). */
extern int g_troop_craft;
const char *craft_troop_name(int b);
/* Crews (Soldier.craft): fit soldiers of the current base in g_craft[k]'s
   crew (0 if k < 0); a lost or transferred craft's crew is unassigned. */
int craft_crew(int k);
void craft_crew_clear(const Craft *c);
/* Seats of craft c left for soldiers: four go to each HWP aboard. */
int craft_seats(const Craft *c);
/* Craft::getFuelLimit: the fuel left only just takes it home. */
int craft_low_fuel(const Craft *c);
/* Cydonia (DOS: the spacecraft, ready, soldiers aboard): a ready Avenger
   with a fit crew or HWPs aboard, the current base's first; -1 none. */
int craft_for_cydonia(void);
/* Craft c is lost (shot down, left with its squad, its base destroyed):
   the score and the slot. */
void craft_lose(Craft *c);

/* Air combat between craft and UFOs (DogfightState), up to four windows
   at once over the geoscape (src/airfight.c). The caller fills c and the
   UFO fields, result -1, started 0 (resume: from standoff, a minimised
   fight brought back), off: the weapons switched off; ufo_damage and
   escape (the UFO's break-off countdown, 0 = not set) are kept with the
   UFO. craft_dogfights runs the open ones (result < 0) until all are
   over (then a key closes the windows) or one is minimised (at standoff:
   DF_MINIMISED, its craft follows the UFO); it returns how many are
   still going on (call again: the windows are placed anew). DF_ENDED:
   the UFO was brought down in another of the fights. */
enum { DF_DISENGAGED, DF_UFO_ESCAPED, DF_UFO_DOWN, DF_UFO_DESTROYED, DF_CRAFT_LOST,
       DF_MINIMISED, DF_ENDED };
#define MAX_FIGHTS  4
#define DF_SHOTS    8
typedef struct {
    Craft *c;
    int ufo_type, ufo_id;
    const char *ufo_name;
    int *ufo_damage, *escape;
    int result;                         /* -1 going on, DF_* */
    unsigned char started, resume, off[2];
    unsigned char counts;               /* this fight runs the UFO's countdown */
    int dist, stance, breaking, cooldown[2], reload, ufo_cd;
    int msgt;                           /* ticks the status text still shows */
    unsigned char retreat;              /* cautious and half damage: standoff distance */
    int craft_flash, ufo_flash, dirty, shown_dmg;
    struct { int pos, slot, cov; } shots[DF_SHOTS];
    AwWin w;
} Fight;
int craft_dogfights(Fight *f, int n);

#endif
