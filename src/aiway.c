/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A blaster bomb for the AI (OpenXcom AIModule::wayPointAction): the first
   enemy it knows of, seen or not, worth the bomb (explosiveEfficacy) with a
   path for it (Pathfinding::calculate with the target and no TU limit, path.h
   g_path_mis). The path is walked step by step: where the voxel line from a
   step back to the last waypoint is blocked, the step before becomes a
   waypoint; where it hits the target on its tile, that tile does.
   An overlay section of its own (-zc), small enough to sit beside the battle
   core and the AI in the overlay area's free kilobyte: it loads no other
   section while it runs (build.ps1 $beside). */

#include <i86.h>
#include <stdio.h>
#include "ai_in.h"
#include "battle.h"
#include "battle_in.h"
#include "geo.h"
#include "input.h"
#include "map.h"
#include "path.h"
#include "unit.h"
#include "vox.h"

#define WAY_STEPS   120     /* path_build: a longer path is no path */
#define WAY_RADIUS  11      /* the bomb's power / 20 + 1 */
#define AI_COMBAT   2       /* ai.c */

int ai_way_unit;
static int nwp;             /* the waypoints in bt_wp */
static int no_u = -1, no_turn;  /* who found no path this turn (ai_way) */

/* The middle of a tile, a bit up (wayPointAction's line ends). */
static void way_voxel(int *v, int x, int y, int z)
{
    v[0] = x * 16 + 8;
    v[1] = y * 16 + 8;
    v[2] = z * 24 + 16;
}

/* Doors to walls, or back (path.h). */
static void way_doors(void)
{
    path_doors_asm(FP_SEG(path_fog_table()) - MAP_MAX_TILES / 16, g_mx * g_my * g_mz, g_map, g_pak.mcd);
}

/* The way to unit t, as above: the waypoints; 0 if they do not get there,
   -1 if there is no path. Outside combat mode the
   launch can only come about if a unit stands on the first waypoint
   (evaluateAIMode), so with none there the rest is not looked at; the lines
   of a long path add up to a second. */
static int way_to(int u, int t)
{
    const Unit *a = &g_units[u], *g = &g_units[t];
    unsigned char dirs[WAY_STEPS], *w;
    int x = a->x, y = a->y, z = a->z, px, py, pz, lx = x, ly = y, lz = z, i, r, steps = 0, own = 0;
    int o[3], e[3];

    ai_searched = 0;            /* (its table is the search's) */
    g_path_mis = t + 1;
    way_doors();
    r = path_search_to(u, g->x, g->y, g->z, 255, &px, &py, &pz);
    way_doors();
    g_path_mis = 0;
    if (r)
        steps = path_build(g->x, g->y, g->z, dirs, WAY_STEPS);
    r = -1;
    if (steps) {
        nwp = 0;
        g_vox_excl = u;
        for (i = 0; i < steps && nwp < BT_MAX_WP; i++) {
            px = x;
            py = y;
            pz = z;
            /* Pathfinding::directionToVector (a stair's level is not in it) */
            if (dirs[i] < 8) {
                x += g_dir_dx[dirs[i]];
                y += g_dir_dy[dirs[i]];
            } else
                z += dirs[i] == DIR_UP ? 1 : -1;
            way_voxel(o, x, y, z);
            way_voxel(e, lx, ly, lz);
            r = vox_line(o, e, 0);
            if (r == V_UNIT && UNIT_AT(x, y, z) == t + 1) {
                px = x;
                py = y;
                pz = z;
            } else if (r <= V_EMPTY || r >= V_UNIT)
                continue;
            else if (!i) {
                /* (a first one on its own tile: no leg, and a unit stands on it) */
                own = 1;
                continue;
            }
            w = bt_wp[nwp++];
            w[0] = (unsigned char)(lx = px);
            w[1] = (unsigned char)(ly = py);
            w[2] = (unsigned char)(lz = pz);
            if (nwp == 1 && !(ai_way_unit = own | UNIT_AT(lx, ly, lz)) && ai_unit[u].mode != AI_COMBAT)
                break;
        }
        r = lx == g->x && ly == g->y && lz == g->z ? nwp : 0;
    }
    return r;
}

int ai_way(int u, int go)
{
    const Unit *t, *a = &g_units[u];
    int i, d, r = -2;

    if (go) {
        /* The launch: combat's destination, bt_nwp; its shot mode. */
        log_printf("T%d: alien %d launches", bt_turn, u);
        bt_nwp = nwp;
        ai_wx[2] = bt_wp[0][0];
        ai_wy[2] = bt_wp[0][1];
        ai_wz[2] = bt_wp[0][2];
        return SHOT_AIMED;
    }
    /* Tests (the BOMBAI option): as if in combat mode already. */
    if (g_debug & 4)
        ai_unit[u].mode = AI_COMBAT;
    /* The same unit again this turn after finding no path: none (it may have
       walked out of the closed room meanwhile, but then it has not the TU). */
    if (a->tu < unit_shot_tu(a, SHOT_AIMED) || (no_u == u && no_turn == bt_turn))
        return 0;
    /* (the cheap test first: the same answer as DOS's order) */
    for (i = 0; i < g_nunits && r < 0; i++) {
        t = &g_units[i];
        if (!ai_valid(i, 1) || !ai_efficacy(t->x, t->y, t->z, WAY_RADIUS, 0))
            continue;
        /* After a failed search (all it could reach, 0.3 s) only with a tile
           next to the target among them. */
        for (d = r == -2 ? 8 : 0; d < 8; d++)
            if (path_cost(t->x + g_dir_dx[d], t->y + g_dir_dy[d], t->z) != 0xFFFF)
                d = 8;
        if (d == 8 && r == -1)
            continue;
        r = way_to(u, i);
    }
    if (r == -1) {
        no_u = u;
        no_turn = bt_turn;
    }
    return r > 0 ? r : 0;
}
