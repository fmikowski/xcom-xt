/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battlescape effects: smoke, fire, electro-flares, proximity grenades,
   timed high explosives, and the ammunition types of the heavy weapons.

   Everything is kept in short lists (a few hundred bytes) instead of
   per-tile arrays; tiles with an effect are marked in the sight flags of
   src/los.c (LF_HAZE, LF_SMOKE, LF_FIRE, LF_DEV), which the renderer and
   the sight rays read anyway. */

#include <i86.h>
#include <stdio.h>
#include <string.h>
#include "battle_in.h"
#include "canvas.h"
#include "effects.h"
#include "fx_in.h"
#include "gfx.h"
#include "input.h"
#include "inv.h"
#include "items.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "render.h"
#include "rules.h"
#include "timer.h"
#include "unit.h"

#define COL_INFO    7
#define COL_GOOD    10
#define COL_BAD     12

/* The lists live in the near heap, sized by lim_init (src/startup.c). */
Spot __near *smoke, __near *fire;
Device __near *dev;
int max_smoke, max_fire, max_dev;
int n_smoke, n_fire, n_dev;
int g_fx_count, g_fx_flares;

static int chain;               /* inside a chain of explosions */
int fx_smoke_changed;       /* sight must be checked again */

/* Screen area to redraw: tile box of the changes. */
int fx_dirty;
static int bx0, bx1, by0, by1, bz0, bz1;

void fx_reset(void)
{
    n_smoke = n_fire = n_dev = 0;
    g_fx_count = g_fx_flares = 0;
    chain = fx_dirty = fx_smoke_changed = 0;
}

void fx_count(void)
{
    g_fx_count = n_smoke + n_fire + n_dev;
}

static void dirty_add(int x, int y, int z)
{
    if (!fx_dirty) {
        bx0 = bx1 = x;
        by0 = by1 = y;
        bz0 = bz1 = z;
        fx_dirty = 1;
        return;
    }
    if (x < bx0) bx0 = x;
    if (x > bx1) bx1 = x;
    if (y < by0) by0 = y;
    if (y > by1) by1 = y;
    if (z < bz0) bz0 = z;
    if (z > bz1) bz1 = z;
}

/* Redraws the changed area and checks who sees whom through the smoke. */
void fx_flush(void)
{
    if (fx_dirty) {
        fx_dirty = 0;
        render_rect(TILE_SX(bx0, by1), TILE_SY(bx0, by0, bz1) - 24,
                    TILE_SX(bx1, by0) + 32, TILE_SY(bx1, by1, bz0) + 40);
    }
    if (fx_smoke_changed) {
        fx_smoke_changed = 0;
        bt_vision(-1);
    }
}

/* The first of n entries of size bytes at p (each starts with x, y, z, as
   Spot and Device) on the tile: its index, n if none. A tile has one smoke
   or fire entry at most. */
static int at_tile(const void __near *p, int n, int size, int x, int y, int z)
{
    const unsigned char __near *e = p;
    int i;

    for (i = 0; i < n; i++, e += size)
        if (e[0] == x && e[1] == y && e[2] == z)
            break;
    return i;
}

/* Sets the tile's effect bits (LF_FX) from the lists. */
void fx_tile_flags(int x, int y, int z)
{
    unsigned char *f = &g_los_flags[TIDX(x, y, z)], b = 0, old = *f;
    int i;

    if ((i = at_tile(smoke, n_smoke, sizeof(Spot), x, y, z)) < n_smoke)
        b = LF_HAZE | (smoke[i].v >= SMOKE_THICK ? LF_SMOKE : 0);
    if (at_tile(fire, n_fire, sizeof(Spot), x, y, z) < n_fire)
        b |= LF_FIRE;
    if (at_tile(dev, n_dev, sizeof(Device), x, y, z) < n_dev)
        b |= LF_DEV;
    *f = (unsigned char)((old & ~LF_FX) | b);
    if (((old | b) & LF_HAZE) || (g_xcom_range < VIEW_RANGE && ((old ^ b) & (LF_FIRE | LF_DEV))))
        fx_smoke_changed = 1;       /* smoke shortens sight, fires and flares light the dark */
    dirty_add(x, y, z);
}

/* ------------------------------------------------------------------ */
/* Smoke */

static int lflags(int x, int y, int z)
{
    return g_los_flags[TIDX(x, y, z)];
}

Spot *fx_smoke_spot(int x, int y, int z)
{
    int i;

    if ((lflags(x, y, z) & LF_HAZE) && (i = at_tile(smoke, n_smoke, sizeof(Spot), x, y, z)) < n_smoke)
        return &smoke[i];
    return NULL;
}

int fx_smoke(int x, int y, int z)
{
    Spot *s = fx_smoke_spot(x, y, z);

    return s ? s->v & 15 : 0;
}

static int fire_find(int x, int y, int z)
{
    return at_tile(fire, n_fire, sizeof(Spot), x, y, z);
}

void fx_smoke_put(int x, int y, int z, int v, int force)
{
    Spot *s;

    if (!IN_MAP(x, y, z) || (lflags(x, y, z) & (LF_OBJECT | LF_FIRE)))
        return;                 /* not inside solid objects, not on fire */
    if ((s = fx_smoke_spot(x, y, z)) == NULL) {
        if (n_smoke >= max_smoke)
            return;             /* (bturn.c keeps room for a grenade) */
        s = &smoke[n_smoke++];
        s->x = (unsigned char)x;
        s->y = (unsigned char)y;
        s->z = (unsigned char)z;
        s->v = 0;
    }
    if (force || s->v < v)
        s->v = (unsigned char)v;
    fx_count();
    fx_tile_flags(x, y, z);
}

/* ------------------------------------------------------------------ */
/* Fire */

/* The parts' Flammable and Fuel as DOS's MCD (pak.h MCD_FLAM, MCD_FUEL;
   record 0, no part: 255 and 0). */
int fx_tile_burn(int x, int y, int z, int *fuel)
{
    const unsigned char *t = TILE(x, y, z), *e = t + 4;
    int lo = 255, f = 0, b;

    for (; t < e; t++) {
        if ((b = MCD_FLAM(*t)) < lo)
            lo = b;
        if ((b = MCD_FUEL(&g_pak.mcd[*t])) > f)
            f = b;
    }
    *fuel = f;
    return lo;
}

void fx_fire_set(int x, int y, int z, int turns, int intensity)
{
    Spot *s;
    int i = fire_find(x, y, z);

    if (i == max_fire)
        return;
    if ((s = fx_smoke_spot(x, y, z)) != NULL)
        *s = smoke[--n_smoke];  /* a burning tile's smoke is its intensity */
    if (i == n_fire) {
        n_fire++;
        fire[i].x = (unsigned char)x;
        fire[i].y = (unsigned char)y;
        fire[i].z = (unsigned char)z;
    }
    fire[i].v = (unsigned char)((turns > 15 ? 15 : turns) << 4 | intensity);
    fx_count();
    fx_tile_flags(x, y, z);
}

void fx_fire_out(int i)
{
    Spot s = fire[i];

    fire[i] = fire[--n_fire];
    fx_count();
    fx_tile_flags(s.x, s.y, s.z);
}

int fx_fire_at(int x, int y, int z)
{
    return lflags(x, y, z) & LF_FIRE;
}

/* Fire damage as in DOS: 5-10 through no armour, times the armour's
   incendiary modifier (personal armour 80 %, power suit 0, silacoids 0,
   snakemen and ethereals 70 %, reapers 170 %: OpenXcom armors.rul). */
int fx_fire_damage(const Unit *u)
{
    return (5 + (int)rnd(6)) * rl_dmg_mod(u, DT_IN) / 100;
}

/* chance < 0: always (TileEngine::explode), unless immune. */
void fx_catch_fire(int u, int chance)
{
    Unit *un = &g_units[u];
    int mod = rl_dmg_mod(un, DT_IN), t;

    if (!un->alive || !mod || (chance >= 0 && (int)rnd(100) >= chance * mod / 100))
        return;
    t = (int)rnd(5 * mod / 100 + 1);
    if (t > (un->burning & ~UB_HIT)) {
        un->burning = (unsigned char)((un->burning & UB_HIT) | t);
        log_printf("%s catches fire: %d turns", un->name, t);
        bt_redraw(u);           /* in flames */
    }
}

/* Intensity of a fire on a tile of flammability flam (TileEngine::explode,
   detonate: 15 - Flammable / 10 within 1-12). */
static int fire_intensity(int flam)
{
    flam = 15 - flam / 10;
    return flam < 1 ? 1 : flam > 12 ? 12 : flam;
}

/* ------------------------------------------------------------------ */
/* HE blockage (TileEngine::blockage, horizontalBlockage) */

int fx_blk_smoke;              /* 1: the parts' smoke blockage, not the HE blockage */

/* Part p of tile (x, y, z) in the map: its HE blockage (MCD_HE), none for
   an open UFO door. Smoke (fx_blk_smoke, TileEngine::blockage for DT_SMOKE):
   256 for any wall, any big wall (by its kind) and any object with
   Block_Smoke, else 0. */
static int rec_he(int w, int p)
{
    const Mcd *m;

    if (!w)
        return 0;
    m = &g_pak.mcd[w];
    if (m->alt_mcd && !(m->flags & MCD_UFO_DOOR) && (g_pak.mcd[m->alt_mcd].flags & MCD_UFO_DOOR))
        return 0;
    if (fx_blk_smoke)
        return p != PART_OBJECT || MCD_BW(m) || (m->flags & MCD_SMOKE) ? 256 : 0;
    return MCD_HE(m);
}

static int part_he(int x, int y, int z, int p)
{
    return rec_he(TILE(x, y, z)[p], p);
}

/* The wall between (x, y) and its neighbour (x + dx, y + dy), one of dx,
   dy 0: it belongs to the tile on the + side. */
static int cross_he(int x, int y, int z, int dx, int dy)
{
    return dx ? part_he(dx > 0 ? x + 1 : x, y, z, PART_WESTWALL)
              : part_he(x, dy > 0 ? y + 1 : y, z, PART_NORTHWALL);
}

/* Direction (0 north, clockwise; Pathfinding::vectorToDirection) of a
   step (dx, dy), by (dy + 1) * 3 + dx + 1. */
static const char step_dir[] = "\7\0\1\6\0\2\5\4\3";

/* The directions of travel (bit d) in which a big wall of each kind does
   not block (TileEngine::blockage): west 4, north 5, east 6, south 7,
   east and south 8, west and north 9 block only on their sides. */
static const unsigned char bw_open[10] = { 0, 0, 0, 0, 0x1F, 0x7C, 0xF1, 0xC7, 0xC1, 0x1C };

/* The object of tile (x, y, z) to a blast going in direction d (blockage
   with a direction): a big wall only on its sides; entering the tile
   (enter) only a big wall counts, a diagonal one not (but for smoke). */
static int obj_he(int x, int y, int z, int d, int enter)
{
    int w = TILE(x, y, z)[PART_OBJECT], k = MCD_BW(&g_pak.mcd[w]);

    if ((enter && (!k || ((k == 2 || k == 3) && !fx_blk_smoke))) || (bw_open[k] >> d & 1))
        return 0;
    return rec_he(w, PART_OBJECT);
}

int fx_step_he(int x, int y, int z, int dx, int dy, int obj)
{
    int nx = x + dx, ny = y + dy, b, d = step_dir[(dy + 1) * 3 + dx + 1], mx, my;

    if (dx && dy) {
        /* horizontalBlockage: half of each pair of walls (those of the
           south-east tile of the four, and the other two) and half the
           objects of the two tiles beside, each facing back to (x, y) */
        mx = dx > 0 ? nx : x;
        my = dy > 0 ? ny : y;
        b = (part_he(mx, my, z, PART_WESTWALL) + part_he(mx, my, z, PART_NORTHWALL)) / 2
            + (part_he(mx, my - 1, z, PART_WESTWALL) + part_he(mx - 1, my, z, PART_NORTHWALL)) / 2
            + (obj_he(x, ny, z, dy < 0 ? 4 : 0, 0) + obj_he(nx, y, z, dx > 0 ? 6 : 2, 0)) / 2;
    } else
        b = cross_he(x, y, z, dx, dy);
    if (obj)
        b += obj_he(x, y, z, d, 0);
    return b + obj_he(nx, ny, z, (d + 4) & 7, 1);
}

unsigned char fx_bv = 0x88;

int fx_line_he(int cx, int cy, int z, int x, int y)
{
    int dx = x - cx, dy = y - cy, n, sx, sy, fx, fy, px = cx, py = cy, nx, ny, b = 0, k, obj;

    n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (!n)
        return 0;
    /* The first step counts the centre's object only if it is a big wall,
       a diagonal one by the side of it the blast is on (fx_bv: its voxel
       in the tile, x << 4 | y) and the way the line goes (explode's big
       wall deflection: hitSide and the ray's angle). */
    k = MCD_BW(&g_pak.mcd[TILE(cx, cy, z)[PART_OBJECT]]);
    obj = k != 0;
    if (k == 2 || k == 3) {
        sx = k == 2 ? dx + dy : dy - dx;
        if (k == 2 ? (fx_bv >> 4) + (fx_bv & 15) <= 15 : (fx_bv >> 4) > (fx_bv & 15))
            obj = !(sx < 0 || (!sx && dx > 0));
        else
            obj = sx <= 0;
    }
    sx = dx * 256 / n;
    sy = dy * 256 / n;
    fx = cx * 256 + 128;
    fy = cy * 256 + 128;
    while (n--) {
        fx += sx;
        fy += sy;
        nx = fx >> 8;
        ny = fy >> 8;
        b += fx_step_he(px, py, z, nx - px, ny - py, obj);
        obj = 1;
        px = nx;
        py = ny;
    }
    return b;
}

/* The least d with (2d + 1)^2 > 4 (dx^2 + dy^2), that is d (d + 1) >= s:
   t runs through d (d + 1), in 16 bits (|dx|, |dy| < 50 on any map). */
int fx_blast_dist(int dx, int dy)
{
    unsigned s = dx * dx + dy * dy, t = 0;
    int d = 0;

    while (t < s) {
        d++;
        t += d + d;
    }
    return d;
}

/* TileEngine::explode for DT_SMOKE and DT_IN (in): flat, the rays losing
   10 power a tile out to the radius (smoke grenade: power 60, radius 3;
   incendiary: half the power, radius power / 30 + 1, 5 more a diagonal
   step). Both lose twice the blockage on their way (fx_line_he): HE
   blockage for fire, smoke blockage for smoke (any wall, big walls and
   objects with Block_Smoke stop it). Smoke: a tile with less than 10 smoke
   stops burning and gets 7-15. Fire: a tile with a floor or an object that does
   not burn yet burns for its fuel + 1 rounds; a unit on a tile it reaches
   (each tile of a 2x2 unit) takes 5-10 x its incendiary modifier and,
   unless immune, catches fire for 0 to 5 x that modifier turns. */
static void fx_area(int cx, int cy, int cz, int power, int in)
{
    int r = in ? power / 30 + 1 : power / 20, dx, dy, d, x, y, diag, n = 0, fuel;
    unsigned idx;
    const unsigned char *t;
    Spot *s;

    if (in)
        power /= 2;
    fx_blk_smoke = !in;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++) {
            x = cx + dx;
            y = cy + dy;
            d = fx_blast_dist(dx, dy);
            diag = dx * dx < dy * dy ? dx : dy;
            if (diag < 0)
                diag = -diag;
            if (d > r || !IN_MAP(x, y, cz)
                || power - 10 * d - (in ? 5 * diag : 0) - 2 * fx_line_he(cx, cy, cz, x, y) <= 0)
                continue;
            if (!in) {
                if ((s = fx_smoke_spot(x, y, cz)) != NULL && s->v >= 10)
                    continue;
                if (fx_fire_at(x, y, cz))
                    fx_fire_out(fire_find(x, y, cz));
                fx_smoke_put(x, y, cz, 7 + (int)rnd(9), 1);
                continue;
            }
            idx = TIDX(x, y, cz);
            t = g_map + (idx << 2);
            if (!t[0] && !t[1] && !t[2] && !t[3])
                continue;           /* open air */
            if ((t[PART_FLOOR] || t[PART_OBJECT]) && !(g_los_flags[idx] & LF_FIRE)) {
                d = fire_intensity(fx_tile_burn(x, y, cz, &fuel));
                fx_fire_set(x, y, cz, fuel + 1, d);
                n++;
            }
            if ((d = g_unit_at[idx]) != 0 && g_units[--d].alive) {
                bt_burn(d, fx_fire_damage(&g_units[d]));
                fx_catch_fire(d, -1);
            }
        }
    fx_blk_smoke = 0;
    if (in)
        log_printf("incendiary at %d,%d,%d radius %d: %d fires", cx, cy, cz, r, n);
}

void fx_smoke_cloud(int cx, int cy, int cz)
{
    fx_area(cx, cy, cz, 60, 0);
}

void fx_incendiary(int cx, int cy, int cz, int power, int shooter)
{
    (void)shooter;
    fx_area(cx, cy, cz, power, 1);
    fx_flush();
}

/* TileEngine::detonate for one part: it breaks while p is at least twice
   its armour, each break using up twice the armour (floors never leave a
   hole in the ground level; units on a floor that breaks fall: bt_fall);
   it catches fire if the power left is more than twice its Flammable (its
   last version's); if it broke and does not burn, it smokes. Returns 1 if
   it broke. */
static int blast_part(int x, int y, int z, int pt, int p)
{
    unsigned char *t = TILE(x, y, z);
    int v = t[pt], flam, solid, broke = 0;
    const Mcd *m;
    Spot *s;

    if (!v)
        return 0;
    while (t[pt]) {
        m = &g_pak.mcd[t[pt]];
        if (m->armor == 255 || 2 * m->armor > p
            || (pt == PART_FLOOR && !z && !m->die_mcd && !g_pak.scorched))
            break;
        p -= 2 * m->armor;
        v = t[pt];
        bt_break(t, pt, z);
        broke = 1;
    }
    if (broke)
        terrain_parts(x, y, z);     /* (its picture: fx_detonate) */
    if (t[pt])
        v = t[pt];
    flam = MCD_FLAM(v);
    solid = t[PART_FLOOR] || t[PART_OBJECT];
    if (2 * flam < p && solid)
        fx_fire_set(x, y, z, MCD_FUEL(&g_pak.mcd[v]) + 1, fire_intensity(flam));
    if (!broke)
        return 0;
    if (fx_fire_at(x, y, z)) {
        if (solid)
            return 1;
        fx_fire_out(fire_find(x, y, z));        /* nothing left to burn on */
    }
    /* Smoke by the part's volume, rolled in fx_after_explode once the
       blast has hurt the units (as DOS: detonate comes after explode):
       0x80 | the part's kind << 4 | the smoke the tile had. */
    fx_smoke_put(x, y, z, 1, 0);
    if ((s = fx_smoke_spot(x, y, z)) != NULL) {
        v = pt == PART_FLOOR ? 0 : pt == PART_OBJECT ? 1 : 2;
        if (!(s->v & 0x80) || v > (s->v >> 4 & 3))
            s->v = (unsigned char)(0x80 | v << 4 | (s->v & 15));
    }
    return 1;
}

/* The parts a tile's blast hits (TileEngine::detonate's nine, in its
   order): big walls beside (west, north), its object, north and west
   walls, floor, the south and east walls, the floor above. */
static const signed char det_part[9][4] = {         /* dx, dy, dz, part */
    { -1, 0, 0, PART_OBJECT }, { 0, -1, 0, PART_OBJECT },
    { 0, 0, 0, PART_OBJECT }, { 0, 0, 0, PART_NORTHWALL }, { 0, 0, 0, PART_WESTWALL },
    { 0, 0, 0, PART_FLOOR }, { 0, 1, 0, PART_NORTHWALL }, { 1, 0, 0, PART_WESTWALL },
    { 0, 0, 1, PART_FLOOR }
};

void fx_detonate(int x, int y, int z, int p)
{
    int i, k, nx, ny, nz, spare = 0;

    for (i = 0; i < 9; i++) {
        nx = x + det_part[i][0];
        ny = y + det_part[i][1];
        nz = z + det_part[i][2];
        if (!IN_MAP(nx, ny, nz) || (spare && (i == 6 || i == 7)))
            continue;
        k = MCD_BW(&g_pak.mcd[TILE(nx, ny, nz)[PART_OBJECT]]);
        if (i < 2) {            /* west: block, east and south, east; north: south */
            if (k != 1 && k != 8 && k != 6 + i)
                continue;
            spare = 1;
        } else if (i == 2 && k)
            spare = 1;
        if (blast_part(nx, ny, nz, det_part[i][3], p)) {
            /* The broken part is not drawn by itself (terrain_changed's
               render_tile, 40-100 a rocket): the blast's whole area once,
               when it is over (fx_flush), as DOS shows the crater at once
               after its animation. The canvas forgets the tile now. */
            if (g_cv)
                cv_tile(nx, ny, nz);
            dirty_add(nx, ny, nz);
            if (i == 2 && k >= 6 && k <= 8)
                spare = 0;
        } else if (i == 2 && (k == 2 || k == 3))
            return;
    }
}

/* ------------------------------------------------------------------ */
/* Devices: armed grenades and flares on the ground */

void fx_dev_remove(int i)
{
    Device d = dev[i];
    dev[i] = dev[--n_dev];
    if (d.kind == DEV_FLARE)
        g_fx_flares--;
    fx_count();
    fx_tile_flags(d.x, d.y, d.z);
}

/* Sets off every pending device, one after the other (proximity grenades
   stepped next to, fuses run out; an explosion destroys the devices it
   reaches with more power than their armour instead, as DOS). */
void fx_run_chain(void)
{
    int i;

    chain = 1;
    for (;;) {
        Device d;
        for (i = 0; i < n_dev && !dev[i].pending; i++)
            ;
        if (i == n_dev)
            break;
        d = dev[i];
        fx_dev_remove(i);
        log_printf("device %d at %d,%d,%d goes off", d.kind, d.x, d.y, d.z);
        if (d.item == IT_SMOKE) {
            /* a smoke grenade: power 60, a cloud of radius 3 (DOS shows
               no message) */
            fx_smoke_cloud(d.x, d.y, d.z);
            log_printf("smoke at %d,%d,%d: %d smoke tiles", d.x, d.y, d.z, n_smoke);
        } else
            bt_explode(d.x, d.y, d.z, d.power, d.owner);
    }
    chain = 0;
    fx_flush();
}

void fx_after_explode(void)
{
    int v;
    Spot *s;

    /* The smoke of the broken parts (blast_part): 1-(vol / 2 + 3) +
       vol / 2 by the volume (DOS: the LOFT layers; here a floor 1, an
       object 8, a wall 12), if more than the tile had. */
    for (s = smoke; s < smoke + n_smoke; s++)
        if (s->v & 0x80) {
            v = "\1\10\14"[s->v >> 4 & 3];
            v = 1 + (int)rnd(v / 2 + 3) + v / 2;
            s->v &= 15;
            if (v > s->v)
                s->v = (unsigned char)v;    /* (at most 15) */
            fx_tile_flags(s->x, s->y, s->z);
        }
    if (!chain)
        fx_flush();
}

/* Armour of an item (items.rul armor, 20 when not given). */
static int item_armour(int it)
{
    switch (it) {
    case IT_FLARE:      return 8;
    case IT_CLIP_PISTOL: return 12;
    case IT_BLASTER:    return 40;
    case IT_PPISTOL:    return 45;
    case IT_PRIFLE:
    case IT_HPLASMA:    return 50;
    }
    return 20;
}

/* TileEngine::explode removes the items the power at their tile exceeds
   the armour of (a grenade does not go off). */
int fx_blast_items(int cx, int cy, int cz, int power, int radius)
{
    int i, n = 0;
    FloorItem *f;

    for (f = g_floor; f < g_floor_end; f++)
        if (f->item != NO_ITEM && f->z == cz
            && bt_blast_power(cx, cy, cz, power, radius, f->x, f->y) > item_armour(f->item)) {
            f->item = NO_ITEM;
            dirty_add(f->x, f->y, cz);
            n++;
        }
    for (i = n_dev - 1; i >= 0; i--)
        if (dev[i].z == cz
            && bt_blast_power(cx, cy, cz, power, radius, dev[i].x, dev[i].y) > item_armour(dev[i].item)) {
            fx_dev_remove(i);
            n++;
        }
    return n;
}

static int fx_quiet;            /* fx_drop: no messages, no drawing */

int fx_throw(int u, int item, int x, int y, int z, int fuse)
{
    Device *d;
    char text[40];
    int i;

    /* Primed grenades go off when their fuse runs out (the DOS default:
       no instant grenades). */
    if (item != IT_PROXIMITY && item != IT_HE && item != IT_FLARE && item != IT_GRENADE
        && item != IT_AGRENADE && item != IT_SMOKE)
        return 0;
    if (n_dev >= max_dev) {
        /* No room (effects.h): a flare on the ground goes out, else the
           grenade lies there unarmed. */
        for (i = 0; i < n_dev && dev[i].kind != DEV_FLARE; i++)
            ;
        log_printf("devices full: %d at %d,%d,%d", i, x, y, z);
        if (i == n_dev) {
            floor_add(x, y, z, item, 1);
            dirty_add(x, y, z);
            if (!fx_quiet)
                fx_flush();
            return 1;
        }
        floor_add(dev[i].x, dev[i].y, dev[i].z, IT_FLARE, 1);
        fx_dev_remove(i);
        fx_smoke_changed = 1;               /* its light is gone: sight again */
    }
    d = &dev[n_dev++];
    d->x = (unsigned char)x;
    d->y = (unsigned char)y;
    d->z = (unsigned char)z;
    d->kind = item == IT_PROXIMITY ? DEV_PROX : item == IT_FLARE ? DEV_FLARE : DEV_TIMER;
    d->owner = (unsigned char)u;
    d->power = g_items[item].power;
    d->pending = 0;
    d->item = (unsigned char)item;
    d->fuse = (unsigned char)fuse;
    fx_count();
    fx_tile_flags(x, y, z);
    log_printf("device %d placed at %d,%d,%d", d->kind, x, y, z);
    if (d->kind == DEV_FLARE)
        g_fx_flares++;
    if (fx_quiet)
        return 1;               /* put down in the inventory: drawn later */
    if (d->kind == DEV_PROX)
        bt_msg(COL_INFO, "Proximity grenade armed");
    else if (d->kind == DEV_TIMER) {
        if (fuse)
            sprintf(text, "%s (%d turns)", item == IT_HE ? "High explosive set" : "Grenade primed",
                    fuse);
        bt_msg(COL_INFO, fuse ? text : item == IT_HE ? "High explosive set (end of turn)"
                                                     : "Grenade primed (end of turn)");
    }
    else
        bt_msg(COL_INFO, "Electro-flare lights the area");
    fx_flush();
    if (d->kind == DEV_FLARE)
        bt_vision(u);
    return 1;
}

int fx_step(int u)
{
    const Unit *un = &g_units[u];
    int s = un->big ? 2 : 1, prox = 0;
    Device __near *d;

    if (!g_fx_count)
        return 0;
    for (d = dev; d < dev + n_dev; d++)
        if (d->kind == DEV_PROX && d->z == un->z
            && d->x + 1 >= un->x && d->x <= un->x + s
            && d->y + 1 >= un->y && d->y <= un->y + s) {
            d->pending = 1;
            prox = 1;
        }
    /* Walking into fire does no harm (UnitWalkBState): the flames burn
       between rounds. */
    if (prox) {
        bt_msg(COL_BAD, "Proximity grenade!");
        fx_run_chain();
    }
    return prox;
}

void fx_fall(void)
{
    int h;
    Device __near *d;

    for (d = dev; d < dev + n_dev; d++)
        if (d->z && !tile_standable(d->x, d->y, d->z, &h)) {
            int oz = d->z, z = oz;
            while (z > 0 && !tile_standable(d->x, d->y, z, &h))
                z--;
            d->z = (unsigned char)z;
            fx_tile_flags(d->x, d->y, oz);
            fx_tile_flags(d->x, d->y, z);
        }
}

/* ------------------------------------------------------------------ */
/* Light */

/* Within 9 tiles, the distance rounded: a light of 15 gives 6 or more. */
static int near15(int x, int y, int lx, int ly)
{
    x -= lx;
    y -= ly;
    return x >= -9 && x <= 9 && y >= -9 && y <= 9 && x * x + y * y <= 90;
}

/* TileEngine::calculateTerrainLighting, calculateUnitLighting: light of
   6 or more on the tile's column (light reaches every level): from the
   terrain's light sources (pak.c), fires, electro-flares, the X-COM
   units' personal light and burning units (power 15 each: 9 tiles). */
int fx_lit(int x, int y, int z)
{
    unsigned i = y * g_mx + x;
    const Unit *u;
    const Spot __near *f;
    const Device __near *d;

    (void)z;
    if (g_pak.relight)
        pak_light_map();        /* a light source broke */
    if (g_pak.lit && (g_pak.lit[i >> 3] & (1 << (i & 7))))
        return 1;
    for (f = fire; f < fire + n_fire; f++)
        if (near15(x, y, f->x, f->y))
            return 1;
    for (d = dev; d < dev + n_dev; d++)
        if (d->kind == DEV_FLARE && near15(x, y, d->x, d->y))
            return 1;
    for (u = g_units; u < g_units + g_nunits; u++)
        if (u->alive && (u->faction == FACTION_XCOM || (u->burning & ~UB_HIT))
            && near15(x, y, u->x, u->y))
            return 1;
    return 0;
}

int fx_level_tiles(int z, unsigned *keys)
{
    int n = 0;
    const Spot __near *s;
    const Device __near *d;

    /* (lists larger than FX_MAX_TILES together: the rest is not drawn) */
    for (d = dev; d < dev + n_dev; d++)
        if (d->z == z)
            keys[n++] = d->x << 8 | d->y;
    for (s = fire; s < fire + n_fire; s++)
        if (s->z == z && n < FX_MAX_TILES)
            keys[n++] = s->x << 8 | s->y;
    for (s = smoke; s < smoke + n_smoke; s++)
        if (s->z == z && n < FX_MAX_TILES)
            keys[n++] = s->x << 8 | s->y;
    return n;
}

/* ------------------------------------------------------------------ */
/* Ammunition */

static int loaded_clip(const Unit *u)
{
    return u->load ? u->load : g_items[u->weapon].clip_item;
}

int fx_round_blast(const Unit *u)
{
    const AmmoDef *a;

    if (u->weapon == IT_LAUNCHER)
        return BLAST_STUN;
    if (u->weapon == IT_BLASTER)
        return BLAST_HE;
    if (u->weapon == NO_ITEM)
        return BLAST_NONE;
    a = ammo_find(u->weapon, loaded_clip(u));
    return a ? a->blast : BLAST_NONE;
}

int fx_round_power(const Unit *u)
{
    int c;

    if (u->weapon == NO_ITEM)
        return 0;
    c = loaded_clip(u);
    if (c != NO_ITEM && g_items[c].power)
        return g_items[c].power;
    return g_items[u->weapon].power;
}

/* ------------------------------------------------------------------ */
/* Drawing: dithered spans straight into the back buffer, clipped to the
   rectangle being redrawn. pat: 8-pixel pattern, bit 7 = x % 8 == 0. */

void __cdecl fxtile_ega(unsigned seg, unsigned off, const unsigned char *shape,
                        unsigned r0, unsigned r1, unsigned pat01, unsigned pat23,
                        unsigned colour, unsigned cols);
void __cdecl fxtile_cga(unsigned seg, unsigned off, const unsigned char *shape,
                        unsigned r0, unsigned r1, unsigned pat01, unsigned pat23,
                        unsigned fill, unsigned cols);

/* Overlay shapes: 32x40 bit masks, 4 bytes a row (bit 7 = left pixel),
   only their rows are kept: smoke puff, fire, inner flames, flare glow,
   flare, grenade dot (the rows of the half-widths the battle once
   computed at its first redraw: the table is smaller than that code). */
#define SH_SMOKE    0
#define SH_FIRE     1
#define SH_FLAME    2           /* inner flames */
#define SH_GLOW     3           /* flare light */
#define SH_CORE     4           /* flare (unused: devices are floor items now) */
#define SH_DOT      5           /* something on the floor without its picture */
#define N_SHAPES    6
static const unsigned char sh_first[N_SHAPES] = { 4, 16, 19, 26, 30, 31 };
static const unsigned char sh_rows[N_SHAPES] = { 36, 24, 19, 12, 4, 2 };
static const unsigned short sh_off[N_SHAPES] = { 0, 144, 240, 316, 364, 380 };
static const unsigned char shape_pool[4 * (36 + 24 + 19 + 12 + 4 + 2)] = {
    0x00, 0x03, 0xC0, 0x00, 0x00, 0x07, 0xE0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x1F, 0xF8, 0x00,
    0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80,
    0x03, 0xFF, 0xFF, 0xC0, 0x07, 0xFF, 0xFF, 0xE0, 0x0F, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xF8,
    0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC,
    0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC,
    0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC,
    0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC,
    0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x0F, 0xFF, 0xFF, 0xF0, 0x03, 0xFF, 0xFF, 0xC0,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x03, 0xC0, 0x00,
    0x00, 0x03, 0xC0, 0x00, 0x00, 0x07, 0xE0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x1F, 0xF8, 0x00,
    0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80,
    0x03, 0xFF, 0xFF, 0xC0, 0x07, 0xFF, 0xFF, 0xE0, 0x0F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0,
    0x0F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0,
    0x0F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0, 0x03, 0xFF, 0xFF, 0xC0,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x03, 0xC0, 0x00,
    0x00, 0x01, 0x80, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x07, 0xE0, 0x00, 0x00, 0x0F, 0xF0, 0x00,
    0x00, 0x1F, 0xF8, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x3F, 0xFC, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x03, 0xC0, 0x00,
    0x00, 0x03, 0xC0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x03, 0xFF, 0xFF, 0xC0, 0x0F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0, 0x03, 0xFF, 0xFF, 0xC0,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x03, 0xC0, 0x00,
    0x00, 0x03, 0xC0, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x03, 0xC0, 0x00,
    0x00, 0x03, 0xC0, 0x00, 0x00, 0x03, 0xC0, 0x00,
};

int fx_no_span;             /* BENCH: drawing switched off */

/* Draws a shape at the tile box (sx, sy) in a colour with a dither
   pattern (4 bytes, by screen row & 3), clipped to the redraw rectangle. */
static void overlay(int sh, int sx, int sy, int colour, const unsigned char *pat)
{
    int r0 = g_clip_y0 - sy, r1 = g_clip_y1 - sy, b, cols = 0;
    int first = sh_first[sh], end = first + sh_rows[sh];
    unsigned char q[4];
    const unsigned char *shape;

    if (g_vga) {                /* DOS's pictures: the whole box (vgaasm.asm fxpic) */
        first = 0;
        end = 40;
    }
    if (r0 < first)
        r0 = first;
    if (r1 > end)
        r1 = end;
    if (r0 >= r1 || fx_no_span)
        return;
    for (b = 0; b < 4; b++)
        if (sx + 8 * b >= g_clip_x0 && sx + 8 * b + 8 <= g_clip_x1)
            cols |= 1 << b;
    if (!cols)
        return;
    for (b = 0; b < 4; b++)
        q[b] = pat[(sy + b) & 3];
    /* Row r of the shape at shape + 4 * r (the offset may wrap: the
       routines only read rows r0..r1-1). */
    shape = shape_pool + sh_off[sh] - 4 * sh_first[sh];
    if (g_video == VIDEO_EGA)
        fxtile_ega(g_bufseg, (unsigned)(sy * BUF_W + (sx >> 3)), shape, r0, r1,
                   q[0] | q[1] << 8, q[2] | q[3] << 8, colour, cols);
    else
        fxtile_cga(g_bufseg, (unsigned)(sy * BUFC_W + (sx >> 2)), shape, r0, r1,
                   q[0] | q[1] << 8, q[2] | q[3] << 8, colour * 0x55, cols);
}

static const unsigned char pat_half[4] = { 0xAA, 0x55, 0xAA, 0x55 };
static const unsigned char pat_thin[4] = { 0x88, 0x00, 0x22, 0x00 };
static const unsigned char pat_flame[4] = { 0x44, 0x00, 0x11, 0x00 };
static const unsigned char pat_full[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

#define EGA (g_video == VIDEO_EGA)

void fx_flames(int sx, int sy)
{
    overlay(SH_FIRE, sx, sy, EGA ? 12 : 2, pat_half);
    overlay(SH_FLAME, sx, sy, EGA ? 14 : 3, pat_flame);
}

void fx_mark(int sx, int sy)
{
    overlay(SH_DOT, sx, sy, EGA ? 14 : 3, pat_full);
}

int fx_dev_pos(int i, unsigned char *at)
{
    if (i >= n_dev)
        return NO_ITEM;
    at[0] = dev[i].x;
    at[1] = dev[i].y;
    at[2] = dev[i].z;
    return dev[i].item;
}

void fx_draw(int x, int y, int z, int sx, int sy)
{
    int i, f = g_los_flags[TIDX(x, y, z)];

    if (f & LF_FIRE)
        fx_flames(sx, sy);
    /* The devices themselves are items on the floor (floor.c); a flare's
       light stands in for the DOS shading. */
    for (i = 0; i < n_dev && (f & LF_DEV); i++)
        if (dev[i].x == x && dev[i].y == y && dev[i].z == z && dev[i].kind == DEV_FLARE)
            overlay(SH_GLOW, sx, sy, EGA ? 14 : 3, pat_half);
    if (f & LF_HAZE)
        overlay(SH_SMOKE, sx, sy, EGA ? 7 : 0, f & LF_SMOKE ? pat_half : pat_thin);
}

/* ------------------------------------------------------------------ */
/* Devices in the inventory: picked up, put down */

int fx_drop(int u, int item, int x, int y, int z, int fuse)
{
    int r;

    fx_quiet = 1;
    r = fx_throw(u, item, x, y, z, fuse);
    fx_quiet = 0;
    return r;
}
