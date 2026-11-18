/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INV_H
#define INV_H

#include "unit.h"

/* Battle inventory: items lying on the floor, a soldier's backpack, the
   inventory screen, and recovery of floor items after a won battle. */

/* The floor list holds g_max_floor entries: MAX_FLOOR on a 640 KB XT, up
   to FLOOR_TOP with more memory (lim_init, src/startup.c). */
#ifndef MAX_FLOOR               /* (tests build with a smaller list) */
#define MAX_FLOOR   64
#ifndef FLOOR_TOP
#define FLOOR_TOP   240         /* (invscr.c keeps entries in a byte) */
#endif
#endif
#ifndef FLOOR_TOP
#define FLOOR_TOP   MAX_FLOOR
#endif

typedef struct {
    unsigned char x, y, z;
    unsigned char item;         /* NO_ITEM = free entry */
    unsigned char n;            /* weapon: rounds; clips, grenades: count;
                                   medi-kit: uses */
} FloorItem;

extern FloorItem __near *g_floor;
extern int g_max_floor;
extern FloorItem __near *g_floor_end;     /* g_floor + g_max_floor */

/* Pictures of what lies on the battle floor (FLOORV.PAK, tools/convert.py
   build_floor; the battlescape draws a tile's heaviest item or body, as
   DOS). floor_load (src/pak.c, after the units are placed) reads only
   those of the items the battle's units carry or that lie on the ground,
   and of the units' bodies, into one block: g_fob[item] and
   g_fob[N_ITEMS + body] are picture slots (0xFF none; a 2x2 body's
   quarters are 4 slots in a row), g_fob[FOB_N] the slot count k, then
   the slots' weights (k bytes) and block offsets (k x u16), then the
   pictures. NULL: no pack or no room (the tiles get a mark). */
#define N_BODIES    36          /* tools/convert.py BODY_ARMORS + LIVE_ALIENS */
#define BODY_CIVM   3           /* + 1: female */
#define BODY_TANK   5           /* + 1: hovertank */
#define BODY_LIVE   7           /* + UnitXStat.live - 1 */
#define FOB_N       (N_ITEMS + N_BODIES)
extern unsigned char *g_fob;
extern unsigned g_fob_size, g_fob_sum;    /* floorld.c, tests: bdebug.c */
void floor_load(void);          /* src/floorld.c */

/* The renderer (src/floor.c, battle core): the tiles of level z with
   items, bodies or devices as x << 8 | y keys (at most max, a tile can
   come more than once); the top one of tile x, y, z drawn with its
   sprite box at sx, sy (its floor's height included). */
int floor_tiles(int z, unsigned *keys, int max);
void floor_draw(int x, int y, int z, int sx, int sy);

void floor_clear(void);
/* An item on the floor (n as FloorItem.n); its entry. When the list is
   full (DOS has no list), an entry lying aboard the craft (in the exit
   area: the spare equipment) or else the new item becomes loot: a carry
   entry CARRY_LOOT (slot: 1 aboard), off the map, that inv_floor_home
   brings home as if it lay there (abort_evacuate drops those outside);
   with the carry pool full too, it goes back to the stores (LOG "floor
   full"). Returns its entry, -1 if it went to the loot or stores. */
int floor_add(int x, int y, int z, int item, int n);
int floor_count_at(int x, int y, int z);

/* What units carry, as DOS's inventory (inventories.rul): sections with
   cells, items of their inventory size (invWidth x invHeight). The right
   hand's weapon (Unit.weapon, ammo, load) and the left hand's grenade
   (Unit.grenade, n_grenades 1, fuse) live in the unit, for the battle
   rules; everything else a unit carries is an entry here: the other
   sections' items and a hand's item of another kind. Before a campaign
   battle the geoscape puts the squad's equipment here (unit = CARRY_ROSTER
   | roster index; hands too), and units_spawn gives it to the units
   (inv_bind). Aliens keep
   their clips and grenades in Unit.clips, n_grenades.
   n: a weapon's rounds; a grenade's fuse + 1 (0 = not primed); a
   medi-kit's heals, CARRY_ACTIVE for the kit whose uses are in Unit.medi,
   stims, pkills. A gun's n (Carry and FloorItem) is its rounds in bits
   0-5 and, in bits 6-7, the special clip it holds (inv_wn, inv_wload):
   it keeps it wherever it goes. */
enum { SEC_RHAND, SEC_LHAND, SEC_BELT, SEC_RLEG, SEC_LLEG, SEC_RSHLD, SEC_LSHLD, SEC_PACK,
       SEC_LOAD, SEC_GROUND };
#define SLOT(sec, x, y)     ((sec) << 4 | (y) << 2 | (x))
#define SLOT_SEC(s)         ((s) >> 4)
#define SLOT_X(s)           ((s) & 3)
#define SLOT_Y(s)           ((s) >> 2 & 3)

typedef struct {
    unsigned char unit;         /* CARRY_FREE: a free entry */
    unsigned char slot, item, n;
} Carry;

/* g_max_carry entries: MAX_CARRY on a 640 KB XT, up to CARRY_TOP with more
   memory (lim_init, src/startup.c). */
#ifndef MAX_CARRY               /* (tests build with a smaller pool) */
#define MAX_CARRY       96
#ifndef CARRY_TOP
#define CARRY_TOP       255     /* (invscr.c keeps entries in a byte) */
#endif
#endif
#ifndef CARRY_TOP
#define CARRY_TOP       MAX_CARRY
#endif
#define CARRY_FREE      0xFF
#define CARRY_LOOT      0xFE    /* a floor item off the map (floor_add) */
#define CARRY_ROSTER    0x80
#define CARRY_ACTIVE    0xFF
extern Carry __near *g_carry;
extern int g_max_carry;
extern Carry __near *g_carry_end;         /* g_carry + g_max_carry */

/* A body carried (DOS: the corpse item of a dead or unconscious unit,
   UnitDieBState::convertUnitToCorpse): Carry.item IT_BODY, Carry.n the
   unit, which has SP_CARRIED; it is no g_items entry. A body on the ground
   is its unit lying there (floor.c), as before. Bodies of 2x2 units cannot
   be picked up (items.rul invWidth 0), the others are 2 x 3. */
#define IT_BODY         N_ITEMS
#define IK_BODY         9               /* ITEM_KIND of IT_BODY */
#define ITEM_KIND(item) ((item) == IT_BODY ? IK_BODY : g_items[item].kind)
/* Corpse weights (items.rul) by body (BODY_*), 0: cannot be picked up. */
extern const unsigned char g_body_weight[N_BODIES];
int inv_body_weight(int unit);          /* of the unit's body, 0 none */

/* Inventory size of an item: width << 2 | height (items.rul). */
extern const unsigned char g_item_inv[N_ITEMS];
#define ITEM_INV(item)   ((item) == IT_BODY ? 11 : g_item_inv[item])
#define ITEM_W(item)     (ITEM_INV(item) >> 2)
#define ITEM_H(item)     (ITEM_INV(item) & 3)

void carry_clear(void);
int carry_add(int unit, int slot, int item, int n);     /* index, -1 = full */
int carry_find(int unit, int item);                     /* first entry, -1 */
int carry_at(int unit, int sec);                        /* first entry in a section */
int carry_cell(int sec, int x, int y);                  /* a cell of the section */
/* Room for item at slot (section, x, y) among the unit's other entries
   (skip: an entry to leave out, -1 none); hands hold one item of any size. */
int carry_fits(int unit, int slot, int item, int skip);
/* The first slot of the belt, legs, shoulders and backpack where it fits
   (BattlescapeGenerator::addItem's order), -1 none. */
int carry_free_slot(int unit, int item);
/* Item into its first free slot; the entry or -1. */
int carry_place(int unit, int item, int n);
/* A weapon's n from its rounds and loaded clip type (Unit.load, 0 = the
   standard one); the special clip type of an n (0: the standard clip). */
int inv_wn(int weapon, int load, int ammo);
int inv_wload(int weapon, int n);
/* A unit takes the weapon of a floor entry into its right hand. */
void inv_arm(Unit *u, FloorItem *f);
/* The unit's weapon is in its right hand, a soldier's grenade in the left. */
int carry_rhand(const Unit *u);         /* item in the right hand, NO_ITEM */
int carry_lhand(const Unit *u);
/* The item in a hand (left 0 / 1) and its n: a weapon's rounds, a
   grenade's fuse + 1, a body's unit, else Carry.n. */
int inv_hand(const Unit *u, int left, int *n);
/* A soldier's hands swap (the left hand's item into the right hand's
   place, Unit.weapon, a grenade into the left's, Unit.grenade): the left
   hand's weapon fires and reloads through the battle rules, the right
   hand's grenade is thrown and primed (BattleUnit::getActiveHand). A
   second call swaps them back (SP_MIRROR; a gun keeps its clip type in its
   n). 0 if there is no free entry for it. */
int inv_mirror(int u);
/* An item carried outside the hands taken into a free hand (the left
   first): its entry, -1 none (*hand SEC_RHAND / SEC_LHAND, *cost the
   move's TU, inventories.rul). */
int inv_reach(int u, int item, int *hand, int *cost);
/* Clips the right hand's weapon can load (standard or special) that the
   unit carries: Unit.clips plus entries. */
int inv_clips(const Unit *u);
/* The battle's setup: soldier r's entries (CARRY_ROSTER | r) become unit
   u's; its right hand's weapon, the clip in it and its left hand's
   grenade go into the unit's fields. */
void inv_bind(int r, int u);
/* The medi-kit whose uses are in the unit: after kits came or went. */
void inv_medikit(int u);
/* A soldier's left hand empty: the grenade carried that is quickest to
   take into it (its entry, -1 none; *cost: the TU, inventories.rul). */
int inv_next_grenade(int u, int *cost);

/* Everything a unit carries falls to its tile (on death). */
void inv_drop_all(int u);
/* A body put down at a tile, out of its entry (-1: none). */
void inv_body_down(int k, int x, int y, int z);
/* The entry carrying unit u's body, -1 none. */
int inv_body_entry(int u);
/* Carried bodies take their carriers' tiles (for the abort's exit area,
   DebriefingState: a carried unit is where its carrier is). */
void inv_bodies_sync(void);

/* The inventory screen for unit u (invscr.c); returns when the player
   leaves it. The caller redraws the battlescape. */
void inv_screen(int u);


/* After a won battle (postbat.c, next to the geoscape): floor items
   and the backpacks of survivors go to the base stores. Returns the number of weapons
   recovered. */
int inv_recover(void);
/* The floor items alone (after an abort: those aboard, abort_evacuate
   having left the others); the alien guns among them. */
int inv_floor_home(void);
extern int g_captured;          /* live aliens taken by inv_recover */
extern int g_lost_captives;     /* ... and those that died: no containment room */
extern int g_corpses;           /* alien corpses taken by inv_recover */
int live_aliens(void);
/* What a floor entry or loot brings home to the stores (1: an alien gun). */
int inv_home(int item, int n);

/* DEBUG option, battle key Z (test scripts; bact.c, a section with
   room next to the battle core): the alien at tile *x, *y, *z dies; an
   objective there (alien base control, the brain) takes a blaster bomb's
   blast; else *x, *y, *z become the first tile with an objective part
   (on Mars the first tile of the exit area); on maps with neither the
   tile at the cursor takes the blast. Returns 1 if it acted. */
int inv_debug_zap(int *x, int *y, int *z);
/* DEBUG option, battle key Y: the unit at the tile is stunned; with SHOTLOG
   too, on a tile without one, the walls go (not doors). */
void inv_debug_stun(int x, int y, int z);

/* Backpacks of surviving soldiers to the stores (any outcome). */
void inv_unpack_survivors(void);

#endif
