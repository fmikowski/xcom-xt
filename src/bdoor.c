/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Doors (OpenXcom TileEngine::unitOpensDoor, Tile::openDoor, TileEngine::
   checkAdjacentDoors) for the walk (battle.c `walk`, UnitWalkBState: no
   rClick, the step's direction) and the right click (bact.c `bt_turn_to`,
   UnitTurnBState: rClick, the facing). An overlay section of its own
   (-zc), small enough to sit beside the battle core and the AI in the
   overlay area's free kilobyte: it loads no other section while it runs
   (build.ps1 $beside, docs/design.md "Doors"). */

#include <stdio.h>
#include "battle.h"
#include "battle_in.h"
#include "map.h"
#include "pak.h"
#include "sound.h"
#include "unit.h"

int g_door_x, g_door_y, g_door_z, g_door_p, g_door_tu;

/* The walls looked at for a unit facing / stepping each way (checkPositions
   in their order), up to six: (dx + 1) << 3 | (dy + 1) << 1 | 1 for the
   north wall (0 the west wall), and in the top 3 bits which quarters of a
   2x2 unit look there (`quarters`; a 1x1 unit is quarter 0): D_R all,
   but only with rClick; D_X qx != 0, D_Y qy != 0, D_NX qx == 0, D_NY
   qy == 0 (OpenXcom's conditions); 0 = no more. */
#define D(x, y, n)  (((x) + 1) << 3 | ((y) + 1) << 1 | (n))
#define D_R         0x20
#define D_X         0x40
#define D_Y         0x60
#define D_NX        0x80
#define D_NY        0xA0
static const unsigned char door_at[8][6] = {
    { D(0, 0, 1), D(0, -1, 0) | D_X },
    { D(0, 0, 1), D(1, -1, 0), D(1, 0, 0) | D_R, D(1, 0, 1) | D_R },
    { D(1, 0, 0) },
    { D(1, 1, 0) | D_NY, D(1, 1, 1) | D_NX, D(1, 0, 0) | D_R, D(0, 1, 1) | D_R },
    { D(0, 1, 1) },
    { D(0, 0, 0), D(-1, 1, 1), D(0, 1, 0) | D_R, D(0, 1, 1) | D_R },
    { D(0, 0, 0), D(-1, 0, 1) | D_Y },
    { D(0, 0, 0), D(0, 0, 1), D(-1, -1, 0) | D_X, D(-1, -1, 1) | D_Y,
      D(0, -1, 0) | D_R, D(-1, 0, 1) | D_R }
};

/* The quarters (bit q = qx * 2 + qy) each condition lets look, without
   and with rClick. */
static const unsigned char quarters[2][6] = {
    { 15, 0, 12, 10, 3, 5 }, { 15, 15, 12, 10, 3, 5 }
};

/* A tile's parts, NULL outside the map (record 0 is all zeros). */
static unsigned char *door_tile(unsigned x, unsigned y, unsigned z)
{
    return x < g_mx && y < g_my && z < g_mz ? g_map + ((z * g_my + y) * g_mx + x) * 4 : NULL;
}

/* The swap: the part becomes its alternative record (an open UFO door is
   the record whose alternative is the closed one, tools/convert.py). 0 if
   it cannot be (the other wall where the open door would go is taken:
   then it stays shut). */
static int door_swap(unsigned x, unsigned y, unsigned z, unsigned p)
{
    unsigned char *t = door_tile(x, y, z), alt = g_pak.mcd[t[p]].alt_mcd;
    unsigned np = MCD_PART(&g_pak.mcd[alt]);

    if (!alt || (np != p && t[np]))
        return 0;
    t[p] = 0;
    t[np] = alt;
    terrain_changed(x, y, z);
    return 1;
}

int door_try(Unit *un, int dir, int rclick)
{
    const Mcd *mcd = g_pak.mcd, *m;
    const unsigned char *e = door_at[dir], *qs = quarters[rclick];
    unsigned ux = un->x, uy = un->y, z = un->z, x, y, q, i, c, p;
    unsigned char *t = door_tile(ux, uy, z);
    int lv = mcd[t[PART_FLOOR]].t_level, fly = unit_flies(un) ? 2 : 0, cost, r = 4;

    /* On stairs or raised ground (Tile::getTerrainLevel < -12) the doors
       one level up, only. */
    if (mcd[t[PART_OBJECT]].t_level < lv)
        lv = mcd[t[PART_OBJECT]].t_level;
    if (lv < -12)
        z++;
    /* The first door found: a 2x2 unit's quarters x by x, then y
       (TileEngine::unitOpensDoor 2014-2110). */
    for (q = 0; q <= un->big * 3u; q++)
        for (i = 0; i < 6 && (c = e[i]) != 0; i++) {
            if (!(qs[c >> 5] >> q & 1))
                continue;
            x = ux + (q >> 1) + (c >> 3 & 3) - 1;
            y = uy + (q & 1) + (c >> 1 & 3) - 1;
            p = PART_WESTWALL + (c & 1);
            if ((t = door_tile(x, y, z)) == NULL)
                continue;
            m = &mcd[t[p]];
            if (!(m->flags & (MCD_DOOR | MCD_UFO_DOOR)) || !m->alt_mcd)
                continue;
            /* Tile::openDoor: a swinging door not for a 2x2 unit, nor
               where another unit stands that is a 2x2 unit's other quarter
               (the search goes on; a 1x1 unit stands on its own tile). */
            if ((m->flags & MCD_DOOR)
                && (un->big || ((c = g_unit_at[(unsigned)(t - g_map) >> 2]) != 0
                                && (g_units[c - 1].x != x || g_units[c - 1].y != y))))
                continue;
            goto found;
        }
    return -1;
found:
    g_door_x = x;
    g_door_y = y;
    g_door_z = z;
    g_door_p = p;
    cost = (&m->tu_walk)[fly];
    /* Tile::openDoor: the record's TU and the reserved shot's, else 4
       (whose TU are still charged below, as OpenXcom does). */
    if (un->tu >= cost + (bt_reserve ? unit_shot_tu(un, bt_reserve - 1) : 0)) {
        if (!door_swap(x, y, z, p))
            return -1;
        snd_door();
        if (m->flags & MCD_UFO_DOOR) {
            /* checkAdjacentDoors (2158-2182): the UFO door parts in line
               (a west wall south and north, a north wall east and west)
               up to the first part that is no UFO door; open ones are
               passed. They cost nothing. */
            for (c = 1; c != (unsigned)-3; c -= 2)
                for (i = c;; i += c) {
                    unsigned cx = p == PART_WESTWALL ? x : x + i, cy = p == PART_WESTWALL ? y + i : y;
                    const Mcd *a;
                    if ((t = door_tile(cx, cy, z)) == NULL)
                        break;
                    a = &mcd[t[p]];
                    if (a->flags & MCD_UFO_DOOR)
                        door_swap(cx, cy, z, p);
                    else if (!(mcd[a->alt_mcd].flags & MCD_UFO_DOOR))
                        break;
                }
            r = 9;
        } else {
            /* A walk pays nothing; a right click the open door's TU (the
               tile's other wall after the swap, 2111-2122). */
            t = door_tile(x, y, z);
            cost = rclick ? (&mcd[t[PART_WESTWALL + PART_NORTHWALL - p]].tu_walk)[fly] : 0;
            r = 8;
        }
    }
    if (cost) {
        if (!reserve_ok(un, cost))
            r = (r & 8) | 5, cost = 0;      /* checkReservedTU's 5 */
        else if (un->tu < cost)
            r = (r & 8) | 4, cost = 0;
        else
            un->tu -= cost;
    }
    g_door_tu = cost;
    return r;
}
