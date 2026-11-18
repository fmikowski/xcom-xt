/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exact mode's sight and lines of fire (g_exact, los.h; 286 and up or EXACTAI):
   an overlay section of its own next to the core, never loaded on an XT by
   default, so the core does not grow for it. */

#include <stdlib.h>
#include "ai_in.h"
#include "battle.h"
#include "battle_in.h"
#include "input.h"
#include "effects.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "rules.h"
#include "unit.h"
#include "vox.h"

/* floor(n / d) for d > 0. */
static int fdiv(long n, long d)
{
    return (int)(n >= 0 ? n / d : -((d - 1 - n) / d));
}

/* TileEngine::canTargetUnit as vox_target_unit, for unit u thought to stand
   at (px, py, pz). The hit test compares with the tile where it really stands
   (xOffset, its position less the tile's). The empty line that counts for hyp
   is what the code there means: as written in OpenXcom it cannot happen
   (storeTrajectory false leaves the trajectory empty on V_EMPTY), so an alien
   there hardly judges a tile it is not on (docs/compromises.md). */
int vox_target_at(const int *o, int u, int px, int py, int pz, int hyp)
{
    const Unit *un = &g_units[u];
    const UnitXStat *xs = unit_xstat(un);
    int s[3], lo = pz * 24 - vox_level(px, py, pz) + xs->floating, h = vox_height(u), mid, hr, rad;
    int rx = 0, ry = 0, i, j, r, tx = px * 16 + 8, ty = py * 16 + 8, w = xs->width;
    long dx = tx - o[0], dy = ty - o[1], len;

    mid = (lo + lo + h) / 2;
    hr = h / 2 > 10 ? 10 : h / 2;
    rad = un->big ? 3 : w <= 5 ? 2 : w <= 7 ? 3 : 5;
    len = rl_isqrt((dx * dx + dy * dy) << 8);           /* x 16 */
    if (len) {
        rx = fdiv(dy * rad * 32 + len, 2 * len);
        ry = fdiv(-dx * rad * 32 + len, 2 * len);
    }
    for (i = 0; i <= hr; i++) {
        s[2] = mid + (signed char)"\0\376\2\374\4\372\6\370\10\364\14"[i];
        for (j = 0; j < 5 && (j < 3 || i >= hr - 1); j++) {
            s[0] = tx + (j == 1 ? rx : j == 2 ? -rx : j == 3 ? ry : j == 4 ? -ry : 0);
            s[1] = ty + (j == 1 ? ry : j == 2 ? -ry : j == 3 ? -rx : j == 4 ? rx : 0);
            r = vox_line(o, s, 0);
            if (r == V_UNIT) {
                unsigned hx = (g_vox_hit[0] >> 4) - (s[0] >> 4) - (un->x - px),
                         hy = (g_vox_hit[1] >> 4) - (s[1] >> 4) - (un->y - py);
                if (hx <= un->big && hy <= un->big && g_vox_hit[2] >= lo && g_vox_hit[2] <= lo + h)
                    return 1;
            } else if (r == V_EMPTY && hyp)
                return 1;
        }
    }
    return 0;
}

/* What a unit's sight keeps of the tile line (TileEngine::visible): the smoke
   along it; walls and objects are the voxel test's. 16 voxels a tile, 8 for
   the two ends, smoke / 3 a voxel in a smoky tile, within the 20-tile view. */
int los_smoke(int x0, int y0, int z0, int x1, int y1, int z1)
{
    Ray r;
    int k, left;

    if (!g_fx_count)
        return 1;
    ray_start(&r, x0, y0, z0, x1, y1, z1, 0);
    left = 16 * VIEW_RANGE - 16 * r.steps;
    for (k = 0; k <= r.steps && left >= 0; k++) {
        if (g_los_flags[r.idx] & LF_HAZE)
            left -= fx_smoke(r.x, r.y, r.z) / 3 * (k && k < r.steps ? 16 : 8);
        r.fx += r.sx;
        r.fy += r.sy;
        r.fz += r.sz;
        r.x = r.fx >> 8;
        r.y = r.fy >> 8;
        r.z = r.fz >> 8;
        r.idx = TIDX(r.x, r.y, r.z);
    }
    return left >= 0;
}

/* Tests (BOOMAIM, ai.c; here to leave the AI's section the room). */
int ai_boom_aim(int u)
{
    const Unit *a = &g_units[u];
    const unsigned char *t = TILE(0, 0, a->z);
    unsigned x, y, d, best = 0xFFFF, tu = unit_shot_tu(a, SHOT_SNAP);
    int p;

    if (!tu || a->tu < tu)
        return 0;
    for (y = 0; y < g_my; y++)
        for (x = 0; x < g_mx; x++)
            for (p = 4; p--; t++)
                if (*t && (g_pak.mcd[*t].flags & MCD_EXPL)
                    && (d = (x - a->x) * (x - a->x) + (y - a->y) * (y - a->y)) != 0 && d < best) {
                    best = d;
                    ai_wx[2] = (unsigned char)x;
                    ai_wy[2] = (unsigned char)y;
                }
    if (best == 0xFFFF)
        return 0;
    log_printf("T%d: DEBUG alien %d aims at", bt_turn, u);
    return 1;
}
