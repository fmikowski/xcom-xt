/* SPDX-License-Identifier: GPL-3.0-or-later */
/* X-COM aiming at terrain (TileEngine::canTargetTile, see vox.h): in the
   section of bact.c, next to the player's commands (shots at tiles
   without a unit come from the player's turn). */

#include "pak.h"
#include "vox.h"

/* What one voxel holds, units left out (voxelCheck(.., 0, true)). */
static int voxel(int x, int y, int z)
{
    int p[3];

    p[0] = x;
    p[1] = y;
    p[2] = z;
    return vox_line(p, p, 0);
}

int vox_target_tile(const int *o, int x, int y, int z, int part, int *scan)
{
    /* canTargetTile's search patterns, (x, y) in the tile: the object's
       and floor's spiral, the west and north walls' rows (string literals:
       a named table in a -zc section is reached through an overlay
       vector). */
    const char *sp = part == PART_WESTWALL ? "\0\7\0\11\0\6\0\13\0\4\0\15\0\2"
                   : part == PART_NORTHWALL ? "\7\0\11\0\6\0\13\0\4\0\15\0\2\0"
                   : "\10\10\10\6\12\6\12\10\12\12\10\12\6\12\6\10\6\6"
                     "\10\4\12\4\14\4\14\6\14\10\14\12\14\14\12\14\10\14\6\14\4\14\4\12\4\10\4\6\4\4\6\4"
                     "\10\1\14\1\17\1\17\4\17\10\17\14\17\17\14\17\10\17\4\17\1\17\1\14\1\10\1\4\1\1\4\1";
    const char *fc = "\0\376\2\374\4\372\6\370\10\364\14";     /* heightFromCenter */
    int n = part == PART_WESTWALL || part == PART_NORTHWALL ? 7 : 41;
    int bx = x * 16, by = y * 16, bz = z * 24, lo = 0, hi = 0, i, j, range, ex = g_vox_excl;

    if (part != PART_FLOOR) {
        int found = 0;
        g_vox_excl = VOX_NO_UNITS;
        for (j = 1; j < 12 && !found; j++)
            for (i = 0; i < n; i++)
                if (voxel(bx + sp[i * 2], by + sp[i * 2 + 1], bz + j * 2) == part) {
                    lo = j * 2;
                    found = 1;
                    break;
                }
        for (j = 10; j >= 0 && found == 1; j--)
            for (i = 0; i < n; i++)
                if (voxel(bx + sp[i * 2], by + sp[i * 2 + 1], bz + j * 2) == part) {
                    hi = j * 2;
                    found = 2;
                    break;
                }
        g_vox_excl = ex;
        if (found < 2)
            return 0;
        if (lo > hi)
            lo = hi;
    }
    range = hi - lo;
    if (range > 10)
        range = 10;
    for (j = 0; j <= range; j++) {
        scan[2] = bz + (hi + lo) / 2 + (signed char)fc[j];
        for (i = 0; i < n; i++) {
            scan[0] = bx + sp[i * 2];
            scan[1] = by + sp[i * 2 + 1];
            if (vox_line(o, scan, 0) == part && g_vox_hit[0] >> 4 == x && g_vox_hit[1] >> 4 == y
                && g_vox_hit[2] / 24 == z)
                return 1;
        }
    }
    return 0;
}
