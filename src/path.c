/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"
#include "pak.h"
#include "path.h"
#include "unit.h"

const signed char g_dir_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
const signed char g_dir_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

#define MAX_STEP_H  8       /* biggest height change walkable in one step */
#define NO_COST     0xFFFF
#define COST_NONE   0xFF    /* cost[] of a tile not reached */
#define MAX_BUCKET  256
#define STRIDE      MAP_MAX_TILES   /* table stride, shared with pathasm.asm */

/* Walk tables, rebuilt when terrain changes. They share one segment so the
   assembly search (pathasm.asm) can keep it in DS:
     wh[n]  standing height + 1 (0 = cannot stand)
     wf[n]  WF_ flags
     wc[n]  TU cost to enter
     wd[n]  TU cost of crossing the tile's passable west wall (low nibble)
            and north wall (high nibble); DOS walls cost at most 6
   Each array starts on a paragraph (STRIDE = 625 paragraphs), so wf can
   double as the fog-of-war table (SEEN_BIT, see path_fog_table). */
#define WF_WEST     0x01    /* west wall blocks x-1 <-> x */
#define WF_NORTH    0x02    /* north wall blocks y-1 <-> y */
#define WF_SOLID    0x04    /* impassable object */
#define WF_LIFT     0x08    /* grav lift floor */
#define WF_SEEN     SEEN_BIT    /* fog of war: discovered by X-COM (0x10) */
#define WF_AIR      0x20    /* no floor: flyers go up into it, down out of it */
#define WF_OCC      0x40    /* another unit stands here (during a search) */
#define WF_BIG      0x80    /* a 2x2 unit cannot stand with its first quarter here */

/* Search state, one segment for the assembly search (ES):
     cost[n] u8 (COST_NONE: not reached; searches stop at 254 TU),
     from[n] u8 (direction | level << 4; FR_START at the start; FR_Q
             while the tile waits in a bucket list),
     next[n] u16, bucket[MAX_BUCKET] u16 */
#define FR_START    0x80
#define FR_Q        0x40
static unsigned char *walk_seg, *search_seg;
static unsigned char *wh, *wf, *wc, *wd;
static unsigned short *next, *bucket;
static unsigned char *cost, *from;
static int search_unit = -1;
static unsigned char min_wc = 4;        /* cheapest tile to enter */
static unsigned mxy, ntiles;
static int dir_off[8];          /* index offset of each horizontal direction */

int g_path_asm = 1;             /* 0: use the C reference search */
static unsigned char occ_mask = WF_OCC;     /* wf bits that bar a destination */
static unsigned char fly;       /* the searching unit flies (hovertanks), or its missile */
int g_path_mis;                 /* path.h: a missile's target unit + 1 */
/* wc holds the flying TU (MCD TU_Fly, Pathfinding with MT_FLY) while the
   last search was a flyer's (wc_fly 2: Mcd.tu_fly is tu_walk[2]); fly_diff:
   the map has tiles where they differ from the walking TU (polar floors
   walkers cannot cross, 255 / 4; alien base furniture, 2 / 0). */
static unsigned char wc_fly, fly_diff;

static const Mcd *part(const unsigned char *t, int p)
{
    return t[p] ? &g_pak.mcd[t[p]] : NULL;
}

/* WF_BIG for the 2x2 footprint whose first quarter is (x, y): no wall
   or object inside, the quarters that stand on something at about the
   height of the first one that does, or on the floor of the level above
   (a tank half way down the Skyranger's ramp rests on the craft's floor).
   As OpenXcom, quarters over nothing do not make a large unit fall (a
   walker's first quarter stands: the search only goes there). A swinging
   door, either way it stands, in a wall of the last quarter bars it
   (Pathfinding::getTUCost "don't let tanks phase through doors"). */
static void big_update(int x, int y, int z)
{
    unsigned i, t[4], a;
    int ok = 0, k, h0 = 0;

    if (x < 0 || y < 0)
        return;
    i = TIDX(x, y, z);
    if (x + 1 < (int)g_mx && y + 1 < (int)g_my) {
        t[0] = i;
        t[1] = i + 1;
        t[2] = i + g_mx;
        t[3] = t[2] + 1;
        ok = !(((wf[t[0]] | wf[t[1]] | wf[t[2]] | wf[t[3]]) & WF_SOLID)
               | (wf[t[1]] & WF_WEST) | (wf[t[3]] & WF_WEST)
               | (wf[t[2]] & WF_NORTH) | (wf[t[3]] & WF_NORTH))
             && !((g_pak.mcd[g_map[t[3] * 4 + PART_WESTWALL]].flags
                   | g_pak.mcd[g_map[t[3] * 4 + PART_NORTHWALL]].flags) & MCD_DOOR);
        for (k = 0; k < 4 && ok; k++)
            if (wh[t[k]]) {
                if (!h0)
                    h0 = wh[t[k]];
                else if (wh[t[k]] - h0 > 8 || h0 - wh[t[k]] > 8) {
                    a = t[k] + mxy;
                    ok = z + 1 < (int)g_mz && wh[a] && wh[a] - h0 <= 8 && h0 - wh[a] <= 8;
                }
            }
    }
    if (ok)
        wf[i] &= ~WF_BIG;
    else
        wf[i] |= WF_BIG;
}

static void tile_update(unsigned idx, int z);

/* 1 if the tile's floor or object costs flyers other TU than walkers. */
static int fly_differs(const unsigned char *t)
{
    const Mcd *m = g_pak.mcd;

    return (t[PART_FLOOR] && m[t[PART_FLOOR]].tu_fly != m[t[PART_FLOOR]].tu_walk)
           || (t[PART_OBJECT] && m[t[PART_OBJECT]].tu_fly != m[t[PART_OBJECT]].tu_walk);
}

void path_update_tile(int x, int y, int z)
{
    tile_update(TIDX(x, y, z), z);
    big_update(x, y, z);
    big_update(x - 1, y, z);
    big_update(x, y - 1, z);
    big_update(x - 1, y - 1, z);
}

/* The tables of tile idx (TIDX) on level z, from its parts. */
static void tile_update(unsigned idx, int z)
{
    const unsigned char *t = g_map + (idx << 2);
    const Mcd *f = part(t, PART_FLOOR), *o = part(t, PART_OBJECT);
    const Mcd *w = part(t, PART_WESTWALL), *n = part(t, PART_NORTHWALL);
    unsigned char flags = 0;
    int level = 0, stand = 0, c;

    /* Passable walls and doors cost their TU to cross (Tile::getTUCost: a
       swinging door 8, either way it stands; a closed UFO door its TU, an
       open one its open record's). */
    unsigned char dw = 0, dn = 0;

    if (w) {
        if (w->tu_walk == 255)
            flags |= WF_WEST;
        else
            dw = w->tu_walk > 15 ? 15 : w->tu_walk;
    }
    if (n) {
        if (n->tu_walk == 255)
            flags |= WF_NORTH;
        else
            dn = n->tu_walk > 15 ? 15 : n->tu_walk;
    }
    wd[idx] = (unsigned char)(dw | dn << 4);
    if (o && o->tu_walk == 255)
        flags |= WF_SOLID;
    if (f && (f->flags & MCD_GRAVLIFT))
        flags |= WF_LIFT;
    if (!f || (f->flags & MCD_NO_FLOOR))
        flags |= WF_AIR;
    if (!(flags & WF_SOLID)) {
        if (f && !(f->flags & MCD_NO_FLOOR)) {
            stand = 1;
            if (f->t_level < level)
                level = f->t_level;
        }
        if (o && o->t_level < 0)
            stand = 1;
        if (o && o->t_level < level)
            level = o->t_level;
    }
    wf[idx] = (unsigned char)(flags | (wf[idx] & (WF_SEEN | WF_BIG)));    /* BIG: big_update */
    wh[idx] = stand ? (unsigned char)(z * 24 - level + 1) : 0;
    fly_diff |= fly_differs(t);
    c = f && (&f->tu_walk)[wc_fly] ? (&f->tu_walk)[wc_fly] : 4;
    if (o)
        c += (&o->tu_walk)[wc_fly];
    wc[idx] = (unsigned char)(c > 255 ? 255 : c);
    if (wc[idx] < min_wc)
        min_wc = wc[idx] ? wc[idx] : 1;
}

unsigned char g_path_load;

int path_init(void)
{
    unsigned n = g_mx * g_my * g_mz, i, k;
    const unsigned long *t;
    unsigned char *p;
    int x, y, z;

    mxy = g_mx * g_my;
    ntiles = n;
    for (x = 0; x < 8; x++)
        dir_off[x] = g_dir_dx[x] + g_dir_dy[x] * (int)g_mx;
    if (n > STRIDE)
        return 1;
    if (!walk_seg) {
        walk_seg = alloc_para(4 * STRIDE);
        search_seg = alloc_para(4 * STRIDE + 2 * MAX_BUCKET);
        if (!walk_seg || !search_seg)
            return 1;
        wh = walk_seg;
        wf = (unsigned char *)MK_FP(FP_SEG(walk_seg) + STRIDE / 16, 0);
        wc = walk_seg + 2 * STRIDE;
        wd = walk_seg + 3 * STRIDE;
        cost = search_seg;
        from = search_seg + STRIDE;
        next = (unsigned short *)(search_seg + 2 * STRIDE);
        bucket = (unsigned short *)(search_seg + 4 * STRIDE);
    }
    _fmemset(wf, 0, STRIDE);    /* a new map: nothing seen yet */
    search_unit = -1;
    min_wc = 255;
    wc_fly = fly_diff = 0;
    /* The map walked in index order (t: the tile's four parts). A tile with
       the parts of the one before it on its level gets that one's wh, wf,
       wc and wd (STRIDE apart from offset 0 of walk_seg): tile_update's
       result depends on nothing else here (the parts, the level; wf was
       just cleared). */
    t = (const unsigned long *)g_map;
    for (i = z = 0; z < (int)g_mz; z++)
        for (k = 0; k < mxy; k++, i++, t++)
            if (k && *t == t[-1])
                for (p = wh + i; p < wh + 4 * STRIDE; p += STRIDE)
                    *p = p[-1];
            else
                tile_update(i, z);
    /* WF_BIG: a load reads wf whole right after (bs_restore). */
    if (!g_path_load)
        for (z = 0; z < (int)g_mz; z++)
            for (y = 0; y < (int)g_my; y++)
                for (x = 0; x < (int)g_mx; x++)
                    big_update(x, y, z);
    return 0;
}

unsigned char *path_fog_table(void)
{
    return wf;
}

int tile_standable(int x, int y, int z, int *h)
{
    unsigned char v;

    if (!IN_MAP(x, y, z))
        return 0;
    v = wh[TIDX(x, y, z)];
    if (!v)
        return 0;
    *h = v - 1;
    return 1;
}

/* 1 if a wall blocks the orthogonal step from tile i by (dx, dy). */
static int wall_blocks(unsigned i, int dx, int dy)
{
    if (dx > 0)
        return wf[i + 1] & WF_WEST;
    if (dx < 0)
        return wf[i] & WF_WEST;
    if (dy > 0)
        return wf[i + g_mx] & WF_NORTH;
    return wf[i] & WF_NORTH;
}

static int move_blocked(unsigned i, int dx, int dy)
{
    unsigned ix, iy;

    if (!dx || !dy)
        return wall_blocks(i, dx, dy);
    /* Diagonal: both L-shaped routes must be open, no cutting corners. */
    ix = i + dx;
    iy = dy > 0 ? i + g_mx : i - g_mx;
    if (wall_blocks(i, dx, 0) || wall_blocks(i, 0, dy)
        || wall_blocks(ix, 0, dy) || wall_blocks(iy, dx, 0))
        return 1;
    return (wf[ix] | wf[iy]) & WF_SOLID;
}

/* One part's cost of a step from tile s to tile d (the same part of a 2x2
   unit), dz the levels it changes (Pathfinding::getTUCost). The floor and
   object (by the movement type of the last search, see wc_fly), one more to
   climb a level, x1.5 on a diagonal, then the walls crossed (392-435): the
   north wall of s going north, else of d, the west wall of d going east,
   else of s, read one level up on a climb (verticalOffset), and not d's on
   the way down (fellDown). Two on a diagonal: the first and half the other
   (its wallTmp; the first is the north wall but going south-east). */
static int step_part(unsigned s, unsigned d, int dir, int dz)
{
    int c = wc[d] + (dz > 0), dx = g_dir_dx[dir], dy = g_dir_dy[dir], a = 0, b = 0;

    if (dz > 0)
        s += mxy;
    if (dir & 1)
        c = c * 3 / 2;
    if (dx < 0)
        b = wd[s] & 15;
    else if (dx && dz >= 0)
        b = wd[d] & 15;
    if (dy < 0)
        a = wd[s] >> 4;
    else if (dy && dz >= 0)
        a = wd[d] >> 4;
    if (dir == 3) {
        dx = a;
        a = b;
        b = dx;
    }
    return c + (a ? a + b / 2 : b);
}

/* No floor on tile i (and, for a 2x2 flyer, its other three quarters). */
static int air4(unsigned i)
{
    return (wf[i] & WF_AIR) && (!(occ_mask & WF_BIG)
           || (wf[i + 1] & wf[i + g_mx] & wf[i + g_mx + 1] & WF_AIR));
}

/* step_cost on a known tile index; *nidx gets the destination index. */
int path_step_idx(unsigned idx, int x, int y, int z, int dir, int *nz, unsigned *nidx)
{
    int h, cz, dx, dy, best = -1, best_dh = 99, c, drop = 0;
    unsigned base, best_idx = 0;

    h = wh[idx] ? wh[idx] - 1 : z * 24;
    if (dir >= 8) {
        /* Grav lift: both tiles must carry a lift floor. Flyers go up into
           a tile without a floor, down out of one. */
        unsigned t;
        if (dir == DIR_UP) {
            if (z + 1 >= (int)g_mz)
                return -1;
            t = idx + mxy;
            *nz = z + 1;
        } else {
            if (z == 0)
                return -1;
            t = idx - mxy;
            *nz = z - 1;
        }
        if (!(wf[idx] & WF_LIFT) || !(wf[t] & WF_LIFT)) {
            if (!fly || (wf[t] & WF_SOLID) || !air4(dir == DIR_UP ? t : idx))
                return -1;
        }
        *nidx = t;
        return 8;
    }
    dx = g_dir_dx[dir];
    dy = g_dir_dy[dir];
    if ((unsigned)(x + dx) >= g_mx || (unsigned)(y + dy) >= g_my)
        return -1;
    base = idx + dir_off[dir];
    /* Same level if walkable, else the closer of one down (ramps) or one up
       (stairs), else a drop on this level: Pathfinding::getTUCost bars only
       a climb of more than 8 (off the side of a craft's ramp or of stairs).
       Mirrors pathasm.asm exactly. */
    if (wh[base]) {
        drop = wh[base] - 1 - h + MAX_STEP_H;   /* height change + 8 */
        if ((unsigned)drop <= 2 * MAX_STEP_H)
            goto level;
    } else if (fly && !(wf[base] & WF_SOLID)) {
        /* Flyers go on through the air at their level. */
level:
        if (move_blocked(idx, dx, dy))
            return -1;
        best = z;
        best_idx = base;
    }
    if (best < 0) {
        for (cz = z - 1; cz <= z + 1; cz += 2) {
            unsigned char v;
            unsigned ni;
            int dh;
            if ((unsigned)cz >= g_mz)
                continue;
            ni = cz < z ? base - mxy : base + mxy;
            v = wh[ni];
            if (!v)
                continue;
            dh = v - 1 - h;
            if (dh < 0)
                dh = -dh;
            if (dh > MAX_STEP_H || dh >= best_dh)
                continue;
            if (move_blocked(idx, dx, dy) || move_blocked(cz < z ? idx - mxy : idx + mxy, dx, dy))
                continue;
            best = cz;
            best_dh = dh;
            best_idx = ni;
        }
    }
    if (best < 0 && drop < 0)
        goto level;                     /* down more than 8: a drop */
    if (best < 0)
        return -1;
    *nz = best;
    *nidx = best_idx;
    /* Pathfinding::getTUCost: a 2x2 unit pays the average of its four
       parts. Mirrored by pathasm.asm. */
    c = step_part(idx, best_idx, dir, best - z);
    if (occ_mask & WF_BIG)
        c = (c + step_part(idx + 1, best_idx + 1, dir, best - z)
             + step_part(idx + g_mx, best_idx + g_mx, dir, best - z)
             + step_part(idx + g_mx + 1, best_idx + g_mx + 1, dir, best - z)) / 4;
    return c;
}

int step_cost(int x, int y, int z, int dir, int *nz)
{
    unsigned nidx;
    return path_step_idx(TIDX(x, y, z), x, y, z, dir, nz, &nidx);
}

#ifdef AMB_PATH
void path_apply(int dir, int *x, int *y, int *z, int nz)
{
    if (dir < 8) {
        *x += g_dir_dx[dir];
        *y += g_dir_dy[dir];
    }
    *z = nz;
}
#endif

void __cdecl path_search_asm(const struct PathParams *p);

/* Marks (or clears) WF_OCC where the searching unit cannot go because of
   the others: their tiles, and for a 2x2 searcher also the tiles from
   which its footprint would reach them. */
static void mark_units(int unit, int on)
{
    int i, big = g_units[unit].big;
    for (i = 0; i < g_nunits; i++) {
        const Unit *o = &g_units[i];
        int s = o->big ? 2 : 1, j, k;
        if (i == unit || !o->alive || i + 1 == g_path_mis)
            continue;
        for (j = -big; j < s; j++)
            for (k = -big; k < s; k++) {
                unsigned t;
                if ((unsigned)(o->x + k) >= g_mx || (unsigned)(o->y + j) >= g_my)
                    continue;
                t = TIDX(o->x + k, o->y + j, o->z);
                if (on)
                    wf[t] |= WF_OCC;
                else
                    wf[t] &= ~WF_OCC;
            }
    }
}

/* The A* estimate from (x, y) to the goal (pathasm.asm heur). */
static unsigned heur(int x, int y, int gx, int gy, int hs)
{
    unsigned a = abs(x - gx), b = abs(y - gy);

    return (a > b ? 2 * a + b : 2 * b + a) << hs;
}

/* path_search, or with gx >= 0 the A* search toward (gx, gy, gz):
   returns the tile index where it ended (the goal if reached). */
static unsigned search(int unit, int max_tu, int gx, int gy, int gz, int slack)
{
    const Unit *u = &g_units[unit];
    unsigned start;
    struct PathParams p;

    if (max_tu >= COST_NONE)
        max_tu = COST_NONE - 1;
    if (max_tu < 0)
        max_tu = 0;
    search_unit = unit;
    start = TIDX(u->x, u->y, u->z);
    occ_mask = u->big ? WF_OCC | WF_BIG : WF_OCC;
    fly = (unsigned char)unit_flies(u);
    if (fly_diff && wc_fly != fly * 2) {
        /* The entry costs of the other movement type, where they differ
           (Pathfinding::getTUCost by the unit's movement type). */
        unsigned i, k;
        int z;
        wc_fly = fly * 2;
        for (i = z = 0; z < (int)g_mz; z++)
            for (k = 0; k < mxy; k++, i++)
                if (fly_differs(g_map + (i << 2)))
                    tile_update(i, z);
    }
    /* Pathfinding::calculate: MT_FLY for a missile; validateUpDown looks at
       the unit's own movement type. (The entry costs above stay the unit's:
       they only break ties, and changing them takes half a second.) */
    p.mis = 0;
    if (g_path_mis) {
        p.mis = 2 - fly;
        fly = 1;
    }
    mark_units(unit, 1);
    p.best = start;
    p.walkseg = FP_SEG(walk_seg);
    p.searchseg = FP_SEG(search_seg);
    p.n = ntiles;
    p.mx = g_mx;
    p.my = g_my;
    p.mz = g_mz;
    p.mxy = mxy;
    p.start = start;
    p.max_tu = max_tu;
    p.occ = occ_mask;
    p.fly = fly;
    p.goal = NO_COST;
    if (gx >= 0) {
        /* (an estimate above any path's cost while no tile costs
           less than 4 to enter: the search finds the cheapest path) */
        p.hs = min_wc >= 4;
        p.goal = TIDX(gx, gy, gz);
        p.gx = gx;
        p.gy = gy;
        p.h0 = heur(u->x, u->y, gx, gy, p.hs);
        p.kmax = slack < MAX_BUCKET ? slack : MAX_BUCKET - 1;
    }
    if (g_path_asm)
        path_search_asm(&p);
    else
        path_search_c(&p);              /* (BENCH, vbench.c) */
    mark_units(unit, 0);
    return p.best;
}

void path_search(int unit, int max_tu)
{
    search(unit, max_tu, -1, 0, 0, 0);
}

int path_search_to(int unit, int x, int y, int z, int slack, int *bx, int *by, int *bz)
{
    const Unit *u = &g_units[unit];
    unsigned i = TIDX(u->x, u->y, u->z);

    if (IN_MAP(x, y, z))
        i = search(unit, 254, x, y, z, slack);
    *bz = i / mxy;
    i -= *bz * mxy;
    *by = i / g_mx;
    *bx = i - *by * g_mx;
    return *bx == x && *by == y && *bz == z;
}

unsigned path_cost(int x, int y, int z)
{
    if (!IN_MAP(x, y, z) || search_unit < 0)
        return NO_COST;
    return cost[TIDX(x, y, z)] == COST_NONE ? NO_COST : cost[TIDX(x, y, z)];
}

int path_build(int x, int y, int z, unsigned char *dirs, int max)
{
    int n = 0, i;
    unsigned idx;

    if (path_cost(x, y, z) == NO_COST)
        return 0;
    idx = TIDX(x, y, z);
    while (!(from[idx] & FR_START)) {
        int dir = from[idx] & 15;
        if (n >= max)
            return 0;
        dirs[n++] = (unsigned char)dir;
        if (dir < 8) {
            x -= g_dir_dx[dir];
            y -= g_dir_dy[dir];
        }
        z = (from[idx] >> 4) & 3;
        idx = TIDX(x, y, z);
    }
    for (i = 0; i < n / 2; i++) {
        unsigned char tmp = dirs[i];
        dirs[i] = dirs[n - 1 - i];
        dirs[n - 1 - i] = tmp;
    }
    return n;
}

unsigned path_checksum(void)
{
    unsigned i, s = 0;
    for (i = 0; i < ntiles; i++)
        if (cost[i] != COST_NONE)
            s = s * 31 + cost[i] + (from[i] & 15) * 7 + (from[i] >> 4);
    return s;
}
