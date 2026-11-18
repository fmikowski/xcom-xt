/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef EFFECTS_H
#define EFFECTS_H

/* Battlescape effects (src/effects.c): smoke, fire, electro-flares, armed
   proximity grenades and timed high explosives, and the ammunition types.
   Everything lives in short lists; tiles with an effect carry LF_HAZE,
   LF_SMOKE (thick), LF_FIRE or LF_DEV in the sight flags of src/los.c. */

#include <stdio.h>
#include "unit.h"


extern int g_fx_count;          /* entries in the lists: 0 = nothing to draw */
extern int g_fx_flares;         /* electro-flares on the map */

void fx_reset(void);            /* start of a battle */

/* Unit u's loaded ammunition: impact (BLAST_*), power, HUD tag ("" or
   " HE"). */
int fx_round_blast(const Unit *u);
int fx_round_power(const Unit *u);

/* R: reloads, asking for the clip type when there is a choice (in bplayer.c,
   the player's overlay section). */
void fx_reload(int u);

/* A thrown item lands at a tile: primed grenades (smoke, high
   explosive and the rest: they go off at the end of the turn in which
   their fuse is 0, see bturn.c), proximity grenades and flares are handled
   here (returns 1); anything else explodes (0). fuse: ends of a side's
   turn the grenade waits before that (its timer, PrimeGrenadeState).
   DOS has no limit; with FX_DEVS devices down, a flare on the ground goes
   out (it lies there as an item) to make room, and with no flare down the
   new one lies there as an item, unarmed (LOG "devices full: i at x,y,z":
   i = the flare's device, = the count if none). Nothing goes off early
   or is lost. */
int fx_throw(int u, int item, int x, int y, int z, int fuse);

/* The inventory (invscr.c, inv.c): puts a primed grenade (fuse: its
   fuse) or a flare down as fx_throw without messages (always 1 now: see
   fx_throw). invscr.c finds the devices on a tile and takes them off the
   ground (the device list: src/fx_in.h). */
#ifndef FX_DEVS                 /* (tests build with a smaller list) */
#define FX_DEVS     24          /* the device list (MAX_DEV of src/fx_in.h) */
#ifndef FX_DEVS_TOP
#define FX_DEVS_TOP 72          /* ... with more memory (lim_init, src/startup.c) */
#endif
#endif
#ifndef FX_DEVS_TOP
#define FX_DEVS_TOP FX_DEVS
#endif
int fx_drop(int u, int item, int x, int y, int z, int fuse);

/* Incendiary round or rocket hitting a tile. */
void fx_incendiary(int x, int y, int z, int power, int shooter);

/* Called at the end of every HE explosion: the smoke of what broke, the
   redraw. */
void fx_after_explode(void);
/* An HE blast of a power and radius at (x, y, z), before it hurts the
   units (TileEngine::explode): the items and devices on that level whose
   armour (items.rul) the power there exceeds are destroyed; grenades do
   not go off. Returns how many. */
int fx_blast_items(int x, int y, int z, int power, int radius);

/* TileEngine::detonate: an HE blast's power p at tile (x, y, z) hits a
   big wall in the tile west or north of it that faces it (big wall kinds
   block 1, east and south 8; the west one's east wall 6, the north one's
   south wall 7), its object, north and west walls and floor, the south
   and east walls (the neighbours' north and west walls) and the floor
   above: each part breaks, catches fire, smokes. Next to such a big wall,
   or with a big wall of its own (unless one on its east or south side
   that breaks), the south and east walls are spared; a diagonal big wall
   that stands spares all but itself. */
void fx_detonate(int x, int y, int z, int p);

/* After each step of unit u: proximity grenades next to it. Returns 1 if
   one went off. */
int fx_step(int u);


/* Devices over a hole fall to the floor below (bt_fall). */
void fx_fall(void);

/* The smoke on a tile, 0-15 (0 also on a burning tile). */
int fx_smoke(int x, int y, int z);

/* 1 if the tile is lit enough at night to see a unit on it from beyond 9
   tiles (shade 9 or less, TileEngine::visible): terrain lights, fires,
   flares, X-COM units' personal light, burning units. */
int fx_lit(int x, int y, int z);

/* Tiles with effects on level z as x << 8 | y keys (a tile can come more
   than once): devices, fires, then smoke; returns how many, at most
   FX_MAX_TILES (the lists can hold more with more memory: the rest is not
   drawn, as if out of view). */
#define FX_MAX_TILES    (240 + FX_DEVS)     /* MAX_SMOKE + MAX_FIRE + MAX_DEV */
int fx_level_tiles(int z, unsigned *keys);

/* Render late pass: the overlays of tile x, y, z drawn at its sprite box. */
void fx_draw(int x, int y, int z, int sx, int sy);
/* The flames of a burning tile over a sprite box (also a burning unit's). */
void fx_flames(int sx, int sy);
/* A plain mark on a tile's floor: something lies there whose picture is
   not loaded (floor.c). */
void fx_mark(int sx, int sy);
/* Device i (0 .. max_dev - 1): its item and at[0..2] its tile, NO_ITEM
   when there is no such device (devices are 0 .. their count - 1). */
int fx_dev_pos(int i, unsigned char *at);

void fx_bench(FILE *f);         /* BENCH: redraw time with effects in view (bturn.c) */

/* HE blockage (TileEngine::horizontalBlockage for HE and fire) from tile
   (x, y) to its neighbour (x + dx, y + dy) on level z, both in the map:
   the wall crossed (a diagonal step: half of each pair of walls of the
   four tiles and half the objects of the two tiles beside it), the object
   of the tile left if obj, and a big wall entered (each big wall by its
   kind: on which sides it blocks); an open UFO door has none. */
int fx_step_he(int x, int y, int z, int dx, int dy, int obj);
/* 1: fx_step_he and fx_line_he give the smoke blockage
   (TileEngine::horizontalBlockage for DT_SMOKE): 0 or 256 and more
   (smoke spreads between rounds where it is 0, bturn.c). */
extern int fx_blk_smoke;
/* The HE blockage along the straight tile line from (cx, cy) to (x, y) on
   level z (the centre's own object counts only if it is a big wall; a
   diagonal one by the side of it fx_bv is on): a blast loses twice that
   (TileEngine::explode). */
int fx_line_he(int cx, int cy, int z, int x, int y);
/* The blast's voxel in its centre tile, x << 4 | y (0x88: the middle;
   a shot's blast sets it, battle.c fire_round). */
extern unsigned char fx_bv;
/* A tile offset's distance as the explosion rays reach it: the straight
   distance rounded. */
int fx_blast_dist(int dx, int dy);

/* Services of src/battle.c. */
void msg(int colour, const char *s);    /* (battle_in.h) */
#define bt_msg msg
void bt_explode(int x, int y, int z, int power, int thrower);
/* The power of an HE blast from (cx, cy, cz) at tile (x, y, cz), 0 if it
   does not get there: 10 less a tile, twice the HE blockage passed. */
int bt_blast_power(int cx, int cy, int cz, int power, int radius, int x, int y);
/* Part pt (PART_*) of tile t on level z is destroyed (Tile::destroy):
   the objective count, its destroyed version (a ground floor without one
   scorched earth), and if it is explosive, its blast to come (bturn.c). */
void bt_break(unsigned char *t, int pt, int z);
void bt_burn(int u, int dmg);   /* fire damage straight to health */
void bt_redraw(int u);          /* a unit's tile, if X-COM sees it */
void bt_vision(int u);          /* aliens' visibility; u >= 0 also reveals for u */

#endif
