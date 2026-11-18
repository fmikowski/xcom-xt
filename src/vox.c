/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shots voxel by voxel (see vox.h): OpenXcom's TileEngine (calculateLine,
   voxelCheck, canTargetUnit, getOriginVoxel), the DOS reference. The line
   itself is src/voxasm.asm; canTargetTile is src/voxtile.c. */

#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "battle.h"
#include "input.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "rules.h"
#include "unit.h"
#include "vox.h"

unsigned char *g_vox;
unsigned g_vox_stamp;
int g_vox_excl = -1;

#define VR_CALL     6       /* vox_run: tile_info first */
#define TF_PARTS    2       /* VoxRun.tfl */
#define TF_UNIT     4
#define TF_LIFT     8

/* The blob (tools/convert.py loft_section): u16 offsets of the template
   masks, template values, general templates and rows, then the columns'
   offsets; each MCD record's column is its vcol. */
#define VW(o)       (*(const unsigned short *)(g_vox + (o)))

/* The run of src/voxasm.asm (its offsets are V_* there). */
#pragma pack(1)
struct VoxRun {
    unsigned mapseg, unitseg, voxseg, mcdseg, mcdoff;
    unsigned base, cols, tmask, tval, gen, rows;
    unsigned mx, my, mz, mxy;
    unsigned char excl, phase;
    int tx, ty, tz;
    unsigned ti;
    unsigned char ox, oy, oz, tfl;
    unsigned col[4], move[3];
    unsigned char axis[3], ext;
    int n, d[3], r1, r2;
    unsigned k, sp;
    int ubot, utop;             /* the unit in the tile (tile_info) */
    unsigned char utpl, pad;
    unsigned lmask, rv[4], rva, urv;
    unsigned char key, ukey;
    int o[3], t[3], f0[3];      /* the line; its origin by role */
    int hit[3];                 /* where it stopped */
};
#pragma pack()

int __cdecl vox_run(struct VoxRun *r, int resume);

static struct VoxRun vr;
static unsigned vr_stamp;       /* g_vox_stamp the run was set up for */
int *g_vox_hit = vr.hit;

int vox_level(int x, int y, int z)
{
    const unsigned char *t = TILE(x, y, z);
    int l = t[PART_FLOOR] ? g_pak.mcd[t[PART_FLOOR]].t_level : 0;

    if (t[PART_OBJECT] && g_pak.mcd[t[PART_OBJECT]].t_level < l)
        l = g_pak.mcd[t[PART_OBJECT]].t_level;
    return l;
}

/* Tile::hasNoFloor(tile below) of the tile above tile index i (with the
   tile below: the one at i), or of tile i itself with below = 0. */
static int no_floor(unsigned i, int below)
{
    const unsigned char *t;

    if (below) {
        t = g_map + i * 4;
        if ((t[PART_FLOOR] && g_pak.mcd[t[PART_FLOOR]].t_level == -24)
            || (t[PART_OBJECT] && g_pak.mcd[t[PART_OBJECT]].t_level == -24))
            return 0;
        i += g_mx * g_my;
    }
    t = g_map + i * 4;
    return !t[PART_FLOOR] || (g_pak.mcd[t[PART_FLOOR]].flags & MCD_NO_FLOOR);
}

int vox_height(int u)
{
    const UnitXStat *x = unit_xstat(&g_units[u]);
    return g_units[u].kneeling ? x->kneel : x->height;
}

/* The template of a 1x1 unit's shape by its body width: 1-3 for LOFTEMPS
   2, 3, 5. */
static int unit_tpl(const Unit *u)
{
    int w = unit_xstat(u)->width;
    return w <= 5 ? 1 : w <= 7 ? 2 : 3;
}

/* The run entered a tile with a unit or a grav lift: what of them counts
   there (voxelCheck: a lift's floor unless the lift goes on below, the
   tile's unit or, through a missing floor, the head of the one below:
   its voxels above the highest terrain under it, up to its height). */
static void tile_info(void)
{
    unsigned i = vr.ti, b = i - vr.mxy;
    const unsigned char *t = g_map + i * 4;
    unsigned char u, fl = (unsigned char)(vr.tfl & TF_PARTS);
    const Mcd *mcd = g_pak.mcd;

    if (t[PART_FLOOR] && (mcd[t[PART_FLOOR]].flags & MCD_GRAVLIFT)
        && !(vr.tz && g_map[b * 4] && (mcd[g_map[b * 4]].flags & MCD_GRAVLIFT)))
        fl |= TF_LIFT;
    if (vr.excl != 255) {
        u = g_unit_at[i];
        if (!u && vr.tz && no_floor(b, 1))
            u = g_unit_at[b];
        if (u && u != vr.excl) {
            const Unit *un = &g_units[u - 1];
            int s = un->big, j, k, l = 0, m;
            for (j = 0; j <= s; j++)
                for (k = 0; k <= s; k++)
                    if ((m = vox_level(un->x + j, un->y + k, un->z)) < l)
                        l = m;
            vr.ubot = un->z * 24 + unit_xstat(un)->floating - l;
            vr.utop = vr.ubot + vox_height(u - 1);
            vr.utpl = (unsigned char)(s ? 4 + "\1\0\3\2"[vr.tx - un->x + 2 * (vr.ty - un->y)]
                                        : unit_tpl(un));
            fl |= TF_UNIT;
        }
    }
    vr.tfl = fl;
}

int vox_line(const int *o, const int *tg, int ext)
{
    int r, k;

    if (vr_stamp != g_vox_stamp) {
        /* a new pack or map: where the run finds its data */
        unsigned v = FP_OFF(g_vox);
        vr_stamp = g_vox_stamp;
        vr.mapseg = FP_SEG(g_map);
        vr.unitseg = FP_SEG(g_unit_at);
        vr.voxseg = FP_SEG(g_vox);
        vr.mcdseg = FP_SEG(g_pak.mcd);
        vr.mcdoff = FP_OFF(g_pak.mcd);
        vr.base = v;
        vr.cols = v + 8;
        vr.tmask = v + VW(0);
        vr.tval = v + VW(2);
        vr.gen = v + VW(4);
        vr.rows = v + VW(6);
        vr.mx = g_mx;
        vr.my = g_my;
        vr.mz = g_mz;
        vr.mxy = g_mx * g_my;
    }
    for (k = 0; k < 3; k++) {
        vr.hit[k] = vr.o[k] = o[k];
        vr.t[k] = tg[k];
    }
    if ((unsigned)o[0] >= vr.mx * 16 || (unsigned)o[1] >= vr.my * 16 || (unsigned)o[2] >= vr.mz * 24)
        return V_OUT;
    vr.ext = (unsigned char)ext;
    vr.excl = (unsigned char)(g_vox_excl == VOX_NO_UNITS ? 255 : g_vox_excl + 1);
    for (r = vox_run(&vr, 0); r == VR_CALL; r = vox_run(&vr, 1))
        tile_info();
    return r;
}

void vox_prev(int *p)
{
    int c[3], dr1, dr2, i, k, *d = vr.d;

    /* The main step before the last one: the line again up to there (the
       hit itself when there is none). */
    c[0] = vr.f0[0];
    c[1] = vr.f0[1];
    c[2] = vr.f0[2];
    dr1 = dr2 = d[0] / 2;
    for (i = 1; i < (int)vr.k; i++) {
        dr1 -= d[1];
        dr2 -= d[2];
        if (dr1 < 0) {
            c[1] += vr.axis[1] & 1 ? -1 : 1;
            dr1 += d[0];
        }
        if (dr2 < 0) {
            c[2] += vr.axis[2] & 1 ? -1 : 1;
            dr2 += d[0];
        }
        c[0] += vr.axis[0] & 1 ? -1 : 1;
    }
    for (k = 0; k < 3; k++)
        p[vr.axis[k] >> 1] = vr.k ? c[k] : vr.hit[vr.axis[k] >> 1];
}

int vox_arc(int u, const int *o, const int *t, int curv, int *land)
{
    long q[3], r[3], acc[3], p[3], ro16, rr, za, m;
    unsigned long s = 0;
    int k, i, e[3], last[3], next[3], res = V_EMPTY, excl = g_vox_excl;

    for (k = 0; k < 3; k++) {
        e[k] = t[k] - o[k];
        s += rl_imul(e[k], e[k]);
        last[k] = o[k];
    }
    if (!s || s >= 0x100000UL)          /* (64 tiles) */
        goto done;
    /* ro (the straight length) in 1/16 voxel, refined to 1/256 */
    ro16 = rl_isqrt(s << 8);
    rr = s < 0x80000UL ? (ro16 * 16 + (long)(s * 4096 / ro16)) >> 1 : ro16 * 16;
    za = ((long)rl_isqrt((unsigned long)rr << 14) * curv + 4096) >> 13;   /* sqrt(ro) x curv, Q8 */
    /* Each axis steps i x (t - o) / ro voxels (Q8, exact rational steps). */
    for (k = 0; k < 3; k++) {
        m = (long)e[k] << 16;
        q[k] = m / rr;
        r[k] = m % rr;
        if (r[k] < 0) {
            r[k] += rr;
            q[k]--;
        }
        acc[k] = 0;
        p[k] = (long)o[k] << 8;
    }
    g_vox_excl = u;
    /* TileEngine::calculateParabola: a point every voxel of the straight
       length from the 8th on, the arc's height 4 sqrt(ro) curv i (ro - i)
       / ro^2 on top, the voxel line from point to point. */
    for (i = 1; i < 2000; i++) {
        for (k = 0; k < 3; k++) {
            p[k] += q[k];
            if ((acc[k] += r[k]) >= rr) {
                acc[k] -= rr;
                p[k]++;
            }
            next[k] = (int)(p[k] >> 8);
        }
        if (i < 8)
            continue;
        m = 16L * i;
        next[2] = (int)((p[2] + za * (4 * m * (ro16 - m) / ro16) / ro16) >> 8);
        if ((res = vox_line(last, next, 0)) != V_EMPTY) {
            vox_prev(last);
            break;
        }
        for (k = 0; k < 3; k++)
            last[k] = next[k];
        if (next[2] <= 0)
            break;
    }
    g_vox_excl = excl;
done:
    land[0] = last[0] >> 4;
    land[1] = last[1] >> 4;
    land[2] = last[2] / 24;
    return res;
}

/* The hand (getOriginVoxel) and the target voxel: the middle of the tile,
   1 voxel above its surface (calculateThrow). */
static void throw_ends(int s, int tx, int ty, int tz, int *o, int *t)
{
    vox_origin(s, tx, ty, 1, o);
    t[0] = tx * 16 + 8;
    t[1] = ty * 16 + 8;
    t[2] = tz * 24 + 1 - vox_level(tx, ty, tz);
}

/* An object nothing walks through (TU 255): no throw lands there. */
static int throw_blocked(const int *c)
{
    unsigned char o = TILE(c[0], c[1], c[2])[PART_OBJECT];
    return o && g_pak.mcd[o].tu_walk == 255;
}

int vox_throw(int s, int item, int tx, int ty, int tz)
{
    const Unit *u = &g_units[s];
    int o[3], t[3], c[3], w = rl_weight(item), curv, dx = tx - u->x, dy = ty - u->y;
    long range;

    throw_ends(s, tx, ty, tz, o, t);
    /* validThrowRange: the drop from the hand to 2 voxels above the
       target's surface */
    range = rl_throw_range(u, item, o[2] - t[2] - 1) + 8;
    if (((long)dx * dx + (long)dy * dy) * 256 > range * range)
        return -1;
    c[0] = tx;
    c[1] = ty;
    c[2] = tz;
    if (!u->strength || throw_blocked(c))
        return 0;
    /* validateThrow: from 1.73 / (strength / weight)^0.25 (at least 0.48,
       +0.1 kneeling) up by 0.5 below 5 */
    curv = (int)(1814036L / rl_isqrt((unsigned long)rl_isqrt((unsigned long)u->strength
                                     * 16777216UL / (w > 0 ? w : 1)) << 8))
           + (u->kneeling ? 102 : 0);
    if (curv < 492)
        curv = 492;
    for (; curv < 5120; curv += 512)
        if (vox_arc(s, o, t, curv, c) != V_OUT && c[0] == tx && c[1] == ty && c[2] == tz)
            return curv;
    return 0;
}

void vox_throw_land(int s, int tx, int ty, int tz, int curv, int acc, int *c)
{
    int o[3], t[3], v[3], n;

    throw_ends(s, tx, ty, tz, o, t);
    /* The miss is rolled again while the arc leaves the map or lands on
       an object nothing walks through (calculateThrow). */
    for (n = 0; n < 20; n++) {
        memcpy(v, t, sizeof v);
        rl_throw_aim(o, v, acc, curv);
        if (vox_arc(s, o, v, curv, c) != V_OUT && !throw_blocked(c))
            return;
    }
    c[0] = tx;
    c[1] = ty;
    c[2] = tz;
}

/* floor(n / d) for d > 0. */
static int rdiv(long n, long d)
{
    return (int)(n >= 0 ? n / d : -((d - 1 - n) / d));
}

int vox_target_unit(const int *o, int u, int *scan)
{
    const Unit *un = &g_units[u];
    int tx = un->x * 16 + 8, ty = un->y * 16 + 8, lo, h, mid, hr, rad, rx = 0, ry = 0, i, j;
    int sl[10];
    long dx = tx - o[0], dy = ty - o[1], len;

    lo = un->z * 24 - vox_level(un->x, un->y, un->z) + unit_xstat(un)->floating;
    h = vox_height(u);
    mid = (lo + lo + h) / 2;
    hr = h / 2 > 10 ? 10 : h / 2;
    /* the slices' offset: radius (the LOFTEMPS index; 2x2 units 3) at
       right angles to the line, rounded (canTargetUnit) */
    rad = un->big ? 3 : "\0\2\3\5"[unit_tpl(un)];
    len = rl_isqrt((dx * dx + dy * dy) << 8);           /* x 16 */
    if (len) {
        rx = rdiv(dy * rad * 32 + len, 2 * len);
        ry = rdiv(-dx * rad * 32 + len, 2 * len);
    }
    sl[0] = 0; sl[1] = 0;
    sl[2] = rx; sl[3] = ry;
    sl[4] = -rx; sl[5] = -ry;
    sl[6] = ry; sl[7] = -rx;
    sl[8] = -ry; sl[9] = rx;
    for (i = 0; i <= hr; i++) {
        scan[2] = mid + (signed char)"\0\376\2\374\4\372\6\370\10\364\14"[i];
        for (j = 0; j < 5 && (j < 3 || i >= hr - 1); j++) {
            scan[0] = tx + sl[j * 2];
            scan[1] = ty + sl[j * 2 + 1];
            if (vox_line(o, scan, 0) == V_UNIT) {
                unsigned hx = (vr.hit[0] >> 4) - (scan[0] >> 4), hy = (vr.hit[1] >> 4) - (scan[1] >> 4);
                if (hx <= un->big && hy <= un->big && vr.hit[2] >= lo && vr.hit[2] <= lo + h)
                    return 1;
            }
        }
    }
    return 0;
}

/* The height (voxel z) of unit u's head less d, under the ceiling (less c)
   unless the tile above has no floor (getOriginVoxel, getSightOriginVoxel). */
static int head_z(int u, int d, int c)
{
    const Unit *un = &g_units[u];
    int z = un->z, h = z * 24 - vox_level(un->x, un->y, z) + vox_height(u) + unit_xstat(un)->floating - d;

    if (h >= (z + 1) * 24 && !(z + 1 < (int)g_mz && no_floor(TIDX(un->x, un->y, z + 1), 0)))
        h = (z + 1) * 24 - c;
    return h;
}

void vox_eyes(int u, int *o)
{
    const Unit *un = &g_units[u];
    int s = un->big ? 16 : 8;

    o[0] = un->x * 16 + s;
    o[1] = un->y * 16 + s;
    o[2] = head_z(u, 1 - un->big, 1);
}

/* vox_sees's results by pair, kept while neither unit moves or kneels and
   the terrain stays (other units moving do not clear them). */
#define VIS_N       64
static struct { unsigned char a, b; unsigned pa, pb; } vis[VIS_N];
unsigned char g_vox_seen_ok;
#define VIS_SEEN    0x4000u     /* in pa; tile indices stay below it */

int vox_sees(int a, int b)
{
    const Unit *ua = &g_units[a], *ub = &g_units[b];
    unsigned pa = TIDX(ua->x, ua->y, ua->z) | (ua->kneeling ? 0x8000u : 0);
    unsigned pb = TIDX(ub->x, ub->y, ub->z) | (ub->kneeling ? 0x8000u : 0);
    unsigned k = (a * 11 + b) & (VIS_N - 1);
    int o[3], s[3], seen, excl = g_vox_excl;

    if (!g_vox_seen_ok) {
        memset(vis, 0xFF, sizeof vis);
        g_vox_seen_ok = 1;
    }
    if (vis[k].a == a && vis[k].b == b && (vis[k].pa & ~VIS_SEEN) == pa && vis[k].pb == pb)
        return (vis[k].pa & VIS_SEEN) != 0;
    vox_eyes(a, o);
    g_vox_excl = a;
    seen = vox_target_unit(o, b, s);
    g_vox_excl = excl;
    vis[k].a = (unsigned char)a;
    vis[k].b = (unsigned char)b;
    vis[k].pa = pa | (seen ? VIS_SEEN : 0);
    vis[k].pb = pb;
    if (g_shot_log)     /* tools/sightcheck.py --log replays these */
        log_printf("SEE u%d u%d eye %d,%d,%d at %d,%d,%d lo %d h %d t %d => %d", a, b, o[0], o[1], o[2],
                   ub->x, ub->y, ub->z, ub->z * 24 - vox_level(ub->x, ub->y, ub->z) + unit_xstat(ub)->floating,
                   vox_height(b), ub->big ? 0 : unit_tpl(ub), seen);
    return seen;
}

void vox_origin(int u, int tx, int ty, int throw_, int *o)
{
    const Unit *un = &g_units[u];
    int d, s = un->big ? 2 : 1, dx = tx - un->x, dy = ty - un->y, ax = abs(dx), ay = abs(dy);

    o[2] = head_z(u, throw_ ? 3 : 4, 5);
    /* TileEngine::getDirectionTo: eighths of the circle from north */
    if (ay >= ax && (ax + ay) * (ax + ay) < 2 * ay * ay)
        d = dy < 0 ? 0 : 4;
    else if (ax > ay && (ax + ay) * (ax + ay) < 2 * ax * ax)
        d = dx > 0 ? 2 : 6;
    else
        d = dx > 0 ? (dy < 0 ? 1 : 3) : (dy > 0 ? 5 : 7);
    o[0] = un->x * 16 + "\10\16\17\17\10\1\1\1"[d] * s;
    o[1] = un->y * 16 + "\1\1\10\17\17\17\10\1"[d] * s;
}
