/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FX_IN_H
#define FX_IN_H

/* Inside the battlescape effects: shared by src/effects.c (battle core:
   what happens during a step, shot or blast, and the drawing) and
   src/bturn.c (the turn section: what happens between turns). */

#include "effects.h"

/* The lists' sizes on a 640 KB XT (MAX_*) and with more memory (*_TOP):
   lim_init (src/startup.c) sets max_smoke, max_fire, max_dev between
   them. DOS has no limit. */
#ifndef MAX_SMOKE              /* test builds may set them (XT_DEFS) */
#define MAX_SMOKE   144
#ifndef SMOKE_TOP
#define SMOKE_TOP   432
#endif
#endif
#ifndef SMOKE_TOP
#define SMOKE_TOP   MAX_SMOKE
#endif
#ifndef MAX_FIRE
#define MAX_FIRE    96
#ifndef FIRE_TOP
#define FIRE_TOP    384
#endif
#endif
#ifndef FIRE_TOP
#define FIRE_TOP    MAX_FIRE
#endif
#define MAX_DEV     FX_DEVS     /* FX_MAX_TILES = the three together */
#define DEV_TOP     FX_DEVS_TOP

/* Smoke and fire tiles as DOS keeps them (OpenXcom Tile _smoke, _fire).
   Smoke: v = the smoke, 1-15 (it loses 1 a round, so about as many
   rounds), thick (blocks sight, drawn denser) from SMOKE_THICK; while
   bturn.c works out a round, z's high bits count the smoke that arrived
   (Tile _overlaps) and v sums it. Fire: v = the rounds it still burns
   (up to 15) << 4 | its intensity (DOS's smoke on a burning tile, 1-15:
   damage to units in it, smoke it gives off, power to set neighbours
   alight). A tile has smoke or fire, not both. */
typedef struct {
    unsigned char x, y, z, v;
} Spot;

#define FIRE_TURNS(s)   ((s).v >> 4)
#define FIRE_INT(s)     ((s).v & 15)

#define SMOKE_THICK 7
#define SMOKE_ROOM  48          /* entries kept free between rounds: a grenade's cloud */

#define DEV_PROX    1           /* proximity grenade: a unit steps next to it */
#define DEV_TIMER   2           /* primed grenade, high explosive: when its fuse runs out */
#define DEV_FLARE   3           /* electro-flare: lights the area at night */

/* fuse: ends of a side's turn still to pass before a primed grenade goes
   off (it goes off at the end of the turn in which it is 0, as OpenXcom's
   BattleItem fuse timer). */
typedef struct {
    unsigned char x, y, z, kind, owner, power, pending, item, fuse;
} Device;

extern Spot __near *smoke, __near *fire;
extern Device __near *dev;
extern int max_smoke, max_fire, max_dev;
extern int n_smoke, n_fire, n_dev;
extern int fx_dirty, fx_smoke_changed, fx_no_span;

void fx_count(void);
void fx_tile_flags(int x, int y, int z);
void fx_flush(void);
/* A smoke grenade's cloud (TileEngine::explode, DT_SMOKE). */
void fx_smoke_cloud(int cx, int cy, int cz);
/* The tile's smoke entry, NULL if none. */
Spot *fx_smoke_spot(int x, int y, int z);
/* Smoke v (1-15, unchecked) on a tile without fire: at least v (force:
   exactly v). */
void fx_smoke_put(int x, int y, int z, int v, int force);
/* A tile's flammability (the lowest of its parts, 0-255, 255 = does not
   burn; DOS MCD Flammable) and *fuel (the highest, Tile::getFuel). */
int fx_tile_burn(int x, int y, int z, int *fuel);
/* Sets a tile burning for turns rounds at an intensity (Tile::setFire,
   setSmoke): its smoke goes. */
void fx_fire_set(int x, int y, int z, int turns, int intensity);
void fx_fire_out(int i);        /* fire[i] goes out */
int fx_fire_at(int x, int y, int z);
int fx_fire_damage(const Unit *u);
void fx_run_chain(void);
/* Takes device i off the list. */
void fx_dev_remove(int i);
/* A unit in the flames may catch fire (chance percent x its armour's
   incendiary modifier) for 0-5 x that modifier turns. */
void fx_catch_fire(int u, int chance);

#endif
