/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "effects.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "render.h"
#include "unit.h"
#include "vox.h"

unsigned char *g_los_flags;     /* per-tile sight flags (LF_*) */
int g_xcom_range = VIEW_RANGE;       /* 9 at night */
unsigned char g_exact;          /* DOS's lines without the tile line (los.h) */
unsigned g_los_mxy;             /* (los_init, spawn.c) */
#define lf  g_los_flags
#define mxy g_los_mxy

static int blocks_sight(const Mcd *m)
{
    /* Closed doors too: some city doors lack the STOP_LOS flag (URBAN.MCD
       79, 80) though they are as solid as the others. Not the craft's parts
       without that flag (armour 255, only they have it: the landing gear,
       thin struts): DOS has no armour rule, its voxel lines decide there. */
    return (m->flags & (MCD_STOP_LOS | MCD_BIG_WALL | MCD_DOOR))
        || (m->tu_walk == 255 && m->armor >= 60 && m->armor != 255);
}

void los_update_tile(int x, int y, int z)
{
    const unsigned char *t = TILE(x, y, z);
    unsigned char f = 0;

    if (t[PART_FLOOR] && !(g_pak.mcd[t[PART_FLOOR]].flags & MCD_NO_FLOOR))
        f |= LF_FLOOR;
    if (t[PART_WESTWALL] && blocks_sight(&g_pak.mcd[t[PART_WESTWALL]]))
        f |= LF_WEST;
    if (t[PART_NORTHWALL] && blocks_sight(&g_pak.mcd[t[PART_NORTHWALL]]))
        f |= LF_NORTH;
    if (t[PART_OBJECT] && blocks_sight(&g_pak.mcd[t[PART_OBJECT]]))
        f |= LF_OBJECT;
    {
        unsigned i = TIDX(x, y, z);
        lf[i] = (unsigned char)(f | (lf[i] & LF_FX));
    }
    g_vox_seen_ok = 0;          /* the kept voxel sight results */
}

void ray_start(Ray *r, int x0, int y0, int z0, int x1, int y1, int z1, int max_steps)
{
    int dx = x1 - x0, dy = y1 - y0, dz = z1 - z0, n;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, az = dz < 0 ? -dz : dz;

    n = ax > ay ? ax : ay;
    if (az > n)
        n = az;
    r->x = x0;
    r->y = y0;
    r->z = z0;
    r->idx = TIDX(x0, y0, z0);
    r->fx = x0 * 256 + 128;
    r->fy = y0 * 256 + 128;
    r->fz = z0 * 256 + 128;
    if (n == 0) {
        r->sx = r->sy = r->sz = 0;
        r->steps = 0;
    } else {
        r->sx = dx * 256 / n;
        r->sy = dy * 256 / n;
        r->sz = dz * 256 / n;
        r->steps = max_steps > n ? max_steps : n;
    }
    r->i = 0;
}

/* Wall between tile i and its neighbour in an orthogonal direction. */
static int wall_between(unsigned i, int dx, int dy)
{
    if (dx > 0)
        return lf[i + 1] & LF_WEST ? RAY_WESTWALL : 0;
    if (dx < 0)
        return lf[i] & LF_WEST ? RAY_WESTWALL : 0;
    if (dy > 0)
        return lf[i + g_mx] & LF_NORTH ? RAY_NORTHWALL : 0;
    if (dy < 0)
        return lf[i] & LF_NORTH ? RAY_NORTHWALL : 0;
    return 0;
}

/* Single-axis moves (axis 0 x, 1 y, 2 z; d[axis] = +-1) from tile c at
   index *pi in the given order. Returns what blocks them, or RAY_OK; c and
   *pi end at the tile that holds the blocking part (the tile reached when
   nothing blocks). Objects count in the tiles passed on the way only. */
static int route(int *c, unsigned *pi, const int *d, const unsigned char *order, int n)
{
    static const unsigned char bit[3] = { LF_WEST, LF_NORTH, LF_FLOOR };
    static const unsigned char what[3] = { RAY_WESTWALL, RAY_NORTHWALL, RAY_FLOOR };
    unsigned i = *pi, step;
    int j, k;

    for (j = 0; j < n; j++) {
        k = order[j];
        step = k == 0 ? 1 : k == 1 ? g_mx : mxy;
        /* The wall or floor on the way belongs to the tile on the + side. */
        if (lf[d[k] > 0 ? i + step : i] & bit[k]) {
            if (d[k] > 0) {
                i += step;
                c[k]++;
            }
            *pi = i;
            return what[k];
        }
        if (d[k] > 0)
            i += step;
        else
            i -= step;
        c[k] += d[k];
        if (j < n - 1 && (lf[i] & LF_OBJECT)) {
            *pi = i;
            return RAY_OBJECT;
        }
    }
    *pi = i;
    return RAY_OK;
}

/* A step that changes tile along two or three axes: the straight line
   crosses the tile edges one after the other; the walls and floors met in
   that order (and objects in the tiles between) decide. When the line goes
   exactly through a corner, either order will do. */
static int corner_step(Ray *r, int px, int py, int pz, int nx, int ny, int nz)
{
    int c[3], d[3], f0[3], s[3], k, j, n = 0, res, tie = -1;
    unsigned e[3], len[3], i = r->idx;
    unsigned char order[3], t;

    c[0] = px; c[1] = py; c[2] = pz;
    d[0] = nx - px; d[1] = ny - py; d[2] = nz - pz;
    s[0] = r->sx; s[1] = r->sy; s[2] = r->sz;
    f0[0] = r->fx - s[0]; f0[1] = r->fy - s[1]; f0[2] = r->fz - s[2];
    for (k = 0; k < 3; k++)
        if (d[k]) {
            /* Distance to the edge and step length: the edge is crossed
               after e / len of the step (both <= 256, products < 65536). */
            e[k] = d[k] > 0 ? ((unsigned)(c[k] + 1) << 8) - f0[k] : f0[k] - ((unsigned)c[k] << 8);
            len[k] = s[k] < 0 ? -s[k] : s[k];
            order[n++] = (unsigned char)k;
        }
    for (j = 1; j < n; j++)
        for (k = j; k > 0 && e[order[k - 1]] * len[order[k]] > e[order[k]] * len[order[k - 1]]; k--) {
            t = order[k];
            order[k] = order[k - 1];
            order[k - 1] = t;
        }
    for (j = 0; j + 1 < n; j++)
        if (e[order[j]] * len[order[j + 1]] == e[order[j + 1]] * len[order[j]])
            tie = j;
    res = route(c, &i, d, order, n);
    if (res && tie >= 0) {
        int c2[3];
        unsigned i2 = r->idx;
        c2[0] = px; c2[1] = py; c2[2] = pz;
        t = order[tie];
        order[tie] = order[tie + 1];
        order[tie + 1] = t;
        if (route(c2, &i2, d, order, n) == RAY_OK) {
            res = RAY_OK;
            c[0] = nx; c[1] = ny; c[2] = nz;
            i = i2;
        }
    }
    r->x = c[0];
    r->y = c[1];
    r->z = c[2];
    r->idx = i;
    return res;
}

int ray_next(Ray *r)
{
    int px = r->x, py = r->y, pz = r->z, nx, ny, nz, w;
    unsigned pi, ni;

    if (r->i >= r->steps)
        return RAY_OUT;
    r->i++;
    r->fx += r->sx;
    r->fy += r->sy;
    r->fz += r->sz;
    nx = r->fx >> 8;
    ny = r->fy >> 8;
    nz = r->fz >> 8;
    if (r->fx < 0 || r->fy < 0 || r->fz < 0 || !IN_MAP(nx, ny, nz))
        return RAY_OUT;
    pi = r->idx;
    if (nz == pz && nx != px && ny != py) {
        /* Diagonal on one level (the common case, no multiply): the major
           axis (step 256) is always half way through its tile, so the
           minor one is crossed first if twice its distance to the edge is
           less than its step. */
        int dx = nx - px, dy = ny - py, first = 0, a, b;    /* 1 x, 2 y first */
        unsigned ix = pi + dx, iy = dy > 0 ? pi + g_mx : pi - g_mx;
        int ax = r->sx < 0 ? -r->sx : r->sx, ay = r->sy < 0 ? -r->sy : r->sy;
        if (ax != ay) {
            int e2;
            if (ax == 256) {
                e2 = (r->fy - r->sy) & 255;
                e2 = 2 * (dy > 0 ? 256 - e2 : e2);
                first = e2 < ay ? 2 : e2 > ay ? 1 : 0;
            } else if (ay == 256) {
                e2 = (r->fx - r->sx) & 255;
                e2 = 2 * (dx > 0 ? 256 - e2 : e2);
                first = e2 < ax ? 1 : e2 > ax ? 2 : 0;
            }
        }
        a = first == 2 ? 1 : wall_between(pi, dx, 0) || wall_between(ix, 0, dy)
            || (lf[ix] & LF_OBJECT);
        b = first == 1 ? 1 : wall_between(pi, 0, dy) || wall_between(iy, dx, 0)
            || (lf[iy] & LF_OBJECT);
        if (a && b)
            return corner_step(r, px, py, pz, nx, ny, nz);  /* names the part */
        r->x = nx;
        r->y = ny;
        r->idx = iy + dx;
    } else if ((nx != px) + (ny != py) + (nz != pz) > 1) {
        w = corner_step(r, px, py, pz, nx, ny, nz);
        if (w)
            return w;
    } else {
        /* One axis: the wall or floor between the two tiles. A part on the
           far tile belongs to it: r then names that tile. */
        if (nz > pz) {
            ni = pi + mxy;
            w = lf[ni] & LF_FLOOR ? RAY_FLOOR : 0;
        } else if (nz < pz) {
            ni = pi - mxy;
            w = lf[pi] & LF_FLOOR ? RAY_FLOOR : 0;
        } else {
            ni = nx > px ? pi + 1 : nx < px ? pi - 1 : ny > py ? pi + g_mx : pi - g_mx;
            w = wall_between(pi, nx - px, ny - py);
        }
        if (w && (nx < px || ny < py || nz < pz))
            return w;
        r->x = nx;
        r->y = ny;
        r->z = nz;
        r->idx = ni;
        if (w)
            return w;
    }
    if (lf[r->idx] & LF_OBJECT)
        return RAY_OBJECT;
    return RAY_OK;
}

/* Smoke on the ray's tile over w voxels of the line: smoke / 3 a voxel. */
static int smoke_cost(const Ray *r, int w)
{
    return lf[r->idx] & LF_HAZE ? fx_smoke(r->x, r->y, r->z) / 3 * w : 0;
}

/* Walks the line between two tiles. smoke: as TileEngine::visible, the
   line's length in voxels (16 a tile) plus smoke / 3 for every voxel in a
   smoky tile (not a burning one) must stay within the 20-tile view, 320
   voxels; the line runs from the middle of the start tile to the middle
   of the end tile, so those two count 8 voxels each. */
static int line_clear(int x0, int y0, int z0, int x1, int y1, int z1, int smoke)
{
    Ray r;
    int res, left = 1;

    ray_start(&r, x0, y0, z0, x1, y1, z1, 0);
    /* (lines longer than the view, AI checks, pass only without smoke) */
    if (smoke && (left = (r.steps < VIEW_RANGE ? 16 * (VIEW_RANGE - r.steps) : 0)
                         - smoke_cost(&r, 8)) < 0)
        return 0;
    while (r.i < r.steps) {
        res = ray_next(&r);
        if (res == RAY_OUT)
            return 0;
        if (r.x == x1 && r.y == y1 && r.z == z1)
            return (res == RAY_OK || res == RAY_OBJECT) && (!smoke || left >= smoke_cost(&r, 8));
        if (res != RAY_OK || (smoke && (left -= smoke_cost(&r, 16)) < 0))
            return 0;
    }
    return 1;
}

int los_clear(int x0, int y0, int z0, int x1, int y1, int z1)
{
    return line_clear(x0, y0, z0, x1, y1, z1, g_fx_count);
}

int los_reach(int x0, int y0, int z0, int x1, int y1, int z1)
{
    return line_clear(x0, y0, z0, x1, y1, z1, 0);
}

/* 90-degree view cone: the angle between v and the facing at most 45
   degrees (dot > 0 and 2 dot^2 >= |v|^2 |facing|^2), worked out without
   multiplies. With v turned so that the facing points to + x and / or
   + y: facing along an axis, the other coordinate within + and - the
   distance along it; facing a diagonal, both coordinates 0 or more. The
   unit's own tile counts as in. */
static int in_cone(int dir, int vx, int vy)
{
    int dx = g_dir_dx[dir], dy = g_dir_dy[dir];

    if (!(vx | vy))
        return 1;
    if (dx < 0)
        vx = -vx;
    if (dy < 0)
        vy = -vy;
    if (!dy)
        return vy <= vx && -vy <= vx;
    if (!dx)
        return vx <= vy && -vx <= vy;
    return vx >= 0 && vy >= 0;
}

int los_unit_sees(int a, int b)
{
    const Unit *ua = &g_units[a], *ub = &g_units[b];
    int vx, vy, d2, bx, by;

    /* The cheap rejections first: most pairs end here. */
    if (!ua->alive || !ub->alive)
        return 0;
    vx = ub->x - ua->x;
    vy = ub->y - ua->y;
    if (vx < -VIEW_RANGE || vx > VIEW_RANGE || vy < -VIEW_RANGE || vy > VIEW_RANGE
        || !in_cone(ua->dir, vx, vy) || (d2 = vx * vx + vy * vy) > VIEW_RANGE * VIEW_RANGE)
        return 0;
    bx = ub->x + (ub->big && vx < 0);
    by = ub->y + (ub->big && vy < 0);
    /* A 2x2 unit looks from its quarter nearest the other unit and is seen
       at its quarter nearest the viewer (OpenXcom: from its middle, at any
       of its voxels). */
    if (!(g_exact ? los_smoke : los_clear)(ua->x + (ua->big && vx > 0), ua->y + (ua->big && vy > 0), ua->z, bx, by, ub->z))
        return 0;
    /* In the dark X-COM sees beyond 9 tiles only units on lit tiles
       (TileEngine::visible: shade 9 or less); aliens see in the dark. */
    if (ua->faction == FACTION_XCOM && d2 > g_xcom_range * g_xcom_range
        && !fx_lit(bx, by, ub->z))
        return 0;
    /* Then DOS's own test (TileEngine::visible): canTargetUnit from the
       eyes through the voxels of terrain and other units, so low cover,
       window sills and units in the way hide (vox.c, kept by pair). */
    return vox_sees(a, b);
}

/* Parameter block for reveal_fan_asm (losasm.asm). */
struct RevealFan {
    unsigned lfseg, seenseg, pi, mxy;
    unsigned char mx, my, x, y, z, up;
    signed char vx, vy, sx, sy;
};

#if VIEW_RANGE != 20
#error losasm.asm: RANGE
#endif
int __cdecl reveal_fan_asm(const struct RevealFan *p);
void __cdecl reveal_reset_box(void);
void __cdecl reveal_get_box(unsigned char *box);

void los_box_reset(void)
{
    reveal_reset_box();
}

int los_box_screen(int *x0, int *y0, int *x1, int *y1)
{
    unsigned char b[6];

    reveal_get_box(b);
    if (b[0] > b[1])
        return 0;
    *x0 = (b[0] - b[3]) * 16 + g_cam_x;
    *x1 = (b[1] - b[2]) * 16 + g_cam_x + 32;
    *y0 = (b[0] + b[2]) * 8 - b[5] * 24 + g_cam_y - 24;
    *y1 = (b[1] + b[3]) * 8 - b[4] * 24 + g_cam_y + 40;
    return 1;
}

int los_reveal(int unit)
{
    const Unit *u = &g_units[unit];
    struct RevealFan p;
    int d, revealed = 0;

    if (!g_seen)
        return 0;
    p.pi = TIDX(u->x, u->y, u->z);
    if (!(g_seen[p.pi] & SEEN_BIT)) {
        g_seen[p.pi] |= SEEN_BIT;
        revealed = 1;
    }
    /* Rays to every tile of the square ring at the view range that lies in
       the view cone, by night too (calculateFOV: only units need light),
       each to the unit's level and the ones above and below (losasm.asm):
       the 2 * VIEW_RANGE + 1 ring tiles clockwise from the one 45 degrees
       left of the facing (a corner, or the middle of a side) to the one 45
       degrees right, turning at the corners (exactly the ring tiles
       in_cone accepts, checked for every facing). The rays only mark
       tiles, so their order does not matter. */
    p.lfseg = FP_SEG(lf);
    p.seenseg = FP_SEG(g_seen);
    p.mxy = mxy;
    p.mx = (unsigned char)g_mx;
    p.my = (unsigned char)g_my;
    p.x = u->x;
    p.y = u->y;
    p.z = u->z;
    p.up = u->z + 1u < g_mz;
    d = (u->dir + 7) & 7;
    p.vx = (signed char)(g_dir_dx[d] * VIEW_RANGE);
    p.vy = (signed char)(g_dir_dy[d] * VIEW_RANGE);
    d = (u->dir + 2) & 6;           /* clockwise along the side */
    p.sx = g_dir_dx[d];
    p.sy = g_dir_dy[d];
    return revealed | reveal_fan_asm(&p);
}
