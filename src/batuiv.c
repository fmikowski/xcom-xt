/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The battle minimap in 256 colours (the VGA prototype): its own overlay
   section beside batui (build.ps1), so batui's sets keep their room on
   the XT. The frames are the VGA pack's "XMV1" (tools/convert.py), 16
   palette indices a cell, 0 clear; batui.c loads them into g_mm. */

#include <i86.h>
#include <string.h>
#include "batui.h"
#include "battle.h"
#include "battle_in.h"
#include "effects.h"
#include "gfx.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "render.h"
#include "unit.h"

#define MM_X        48          /* (batui.c) */
#define MM_Y        16

/* VGA: the cell's 4 x 4 palette indices (as compose: 0 clear). */
static void compose_vga(int x, int y, unsigned char *px)
{
    int z, p, i;
    const unsigned char *s;

    memset(px, 0, 16);
    if (x < 0 || y < 0 || x >= (int)g_mx || y >= (int)g_my)
        return;
    for (z = 0; z <= g_mm_z; z++) {
        unsigned idx = (z * g_my + y) * g_mx + x;
        const unsigned char *t = g_map + idx * 4;
        int seen = !g_seen || g_seen[idx];
        for (p = 0; p < 4; p++)
            if (t[p] && t[p] < g_pak.n_mcd)
                for (s = g_mm + t[p] * 16, i = 0; i < 16; i++)
                    if (s[i])
                        px[i] = seen ? s[i] : 0;
        if (g_night && z == g_mm_z && !fx_lit(x, y, z) && !(g_los_flags[idx] & LF_FIRE))
            for (i = 0; i < 16; i++)
                if ((i + (i >> 2)) & 1)
                    px[i] = 0;
        if (g_unit_at[idx]) {
            const Unit *u = &g_units[g_unit_at[idx] - 1];
            if (u->alive && (u->faction == FACTION_XCOM || !g_seen || u->seen))
                for (s = g_mm + (g_pak.n_mcd + (u->faction > 2 ? 2 : u->faction) * 3 + g_mm_anim) * 16,
                     i = 0; i < 16; i++)
                    if (s[i])
                        px[i] = s[i];
        }
    }
}

/* Cells c0..c1 - 1 of tile rows ty0..ty1 - 1 to the screen. */
void bu_mm_vga(int ty0, int ty1, int c0, int c1)
{
    int ty, c, r;

    for (ty = ty0; ty < ty1; ty++)
        for (c = c0; c < c1; c++) {
            unsigned char px[16];
            unsigned char far *d = (unsigned char far *)MK_FP(0xA000,
                (MM_Y + ty * 4) * 320 + MM_X + c * 4);
            compose_vga(g_mm_x0 + c, g_mm_y0 + ty, px);
            for (r = 0; r < 4; r++)
                _fmemcpy(d + r * 320, px + r * 4, 4);
        }
}
