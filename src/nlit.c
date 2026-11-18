/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Night light (docs/design.md "Night and light"; OpenXcom Map::drawTerrain,
   Tile::getShade, TileEngine::calculateTerrainLighting): with the day
   palette on the screen, the map view's dark places are shown darker at the
   present to the screen, so the back buffer and the canvas keep the day
   picture and nothing is redrawn when the light moves. Two levels: a cell
   of 16 x 8 pixels is lit (light of 6 or more on its tile, what fx_lit
   says: shown as drawn) or dark (litasm.asm lit_rect: intensity plane off,
   every other pixel black). Cells, not tiles: fires, flares and units
   light a disc of cells at their level (at most the view's), so the picture
   of another level is up to 1.5 tiles off per level at the edge of the
   light; the terrain's lights at level 0. In the canvas's overlay section:
   needs its UMB overlay area, so a plain XT keeps the night palette
   (cvinit.c cv_battle, batui.c bu_night). */

#include <string.h>
#include "effects.h"
#include "fx_in.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "pak.h"
#include "render.h"
#include "unit.h"
#include "cvpriv.h"

/* Every light that counts (fx_lit): a word that differs when one moved, came
   or went; top >= 0: their discs of cells in now[], at their levels, the
   view's at most. */
static unsigned lights(int top)
{
    unsigned s = (unsigned)n_fire << 8 ^ g_pak.relight;
    const Unit *u = g_units;
    const Spot __near *f = fire;
    const Device __near *d = dev;
    int i;

    for (i = 0; i < n_fire + n_dev + g_nunits; i++) {
        int x, y, z;
        if (i < n_fire) {
            x = f->x;
            y = f->y;
            z = f->z;
            f++;
        } else if (i < n_fire + n_dev) {
            x = d->x;
            y = d->y;
            z = d->z;
            if ((d++)->kind != DEV_FLARE)
                continue;
        } else {
            x = u->x;
            y = u->y;
            z = u->z;
            if (!u->alive || (u->faction != FACTION_XCOM && !(u->burning & ~UB_HIT))) {
                u++;
                continue;
            }
            u++;
        }
        s = (s << 1 | s >> 15) ^ (x << 8 | y | z << 6);
        if (top >= 0) {
            if (z > top)
                z = top;
            lit_disc(LITD->now, ((x - y) * 16 + g_cam_x + 16) >> 4,
                     ((x + y) * 8 - z * 24 + g_cam_y + 32) >> 3, 0);
        }
    }
    return s;
}

/* The terrain's lights (Pack.lit, a bit per map column) in the view, at
   level 0: a tile's diamond is the 2 x 2 cells from (x - y, x + y + 3); as
   in lit_disc, also the rows a level's height above it. */
static void terrain(void)
{
    int cu = g_cam_x >> 4, cv = g_cam_y >> 3, u, v;
    Lit far *L = LITD;

    _fmemset(L->now, 0, LIT_BANDS * 4);
    if (g_pak.lit)
        for (v = -4 - cv; v < LIT_BANDS - cv; v++)
            for (u = -1 - cu + ((v + cu + 1) & 1); u < LIT_COLS - cu; u += 2) {
                int x = (v + u) >> 1, y = (v - u) >> 1;
                unsigned i = y * g_mx + x;
                if ((unsigned)x < g_mx && (unsigned)y < g_my
                    && (g_pak.lit[i >> 3] & (1 << (i & 7))))
                    lit_disc(L->now, u + cu + 1, v + cv + 4, 1);
            }
    _fmemcpy(L->ter, L->now, LIT_BANDS * 4);
}

static void make(int top)
{
    Lit far *L = LITD;

    _fmemcpy(L->was, L->now, LIT_BANDS * 4);
    if (L->cam_x != g_cam_x || L->cam_y != g_cam_y || L->relight != g_pak.relight + 1) {
        terrain();
        L->relight = g_pak.relight + 1;
    } else
        _fmemcpy(L->now, L->ter, LIT_BANDS * 4);
    lights(top);
    L->cam_x = g_cam_x;
    L->cam_y = g_cam_y;
    L->top = top;
    L->n_make++;
}

/* gfx.c gfx_present_cells while g_lit: the rectangle through the cells; when
   the lights changed, the cells that changed first. A scroll moved the
   screen's cells with the camera (vram_shift), so a new camera alone needs
   no more; a new camera and new lights together show the whole view. */
void lit_present(int c0, int r0, int cols, int rows)
{
    Lit far *L = LITD;
    unsigned s = lights(-1);
    int top = g_all_layers ? (int)g_mz - 1 : g_view_z;
    int moved = L->cam_x != g_cam_x || L->cam_y != g_cam_y;

    if (s != L->sum || top != L->top) {
        make(top);
        L->sum = s;
        if (moved) {
            mouse_hide_rect(0, 0, 320, VIEW_H);
            c0 = r0 = 0;
            cols = BUF_W;
            rows = VIEW_H;
        } else {
            int b, c, n;
            L->n_change++;
            for (b = 0; b < LIT_BANDS; b++) {
                unsigned long d = L->now[b] ^ L->was[b];
                for (c = 0; d; c++, d >>= 1)
                    if (d & 1) {
                        for (n = 1; d & 2; n++, d >>= 1)
                            ;
                        mouse_hide_rect(c * 16, b * 8, (c + n) * 16, b * 8 + 8);
                        lit_rect(g_bufseg, L->now, c * 2, b * 8, n * 2, 8);
                        L->n_cells += n;
                        c += n - 1;
                    }
            }
        }
    } else if (moved)
        make(top);
    lit_rect(g_bufseg, L->now, c0, r0, cols, rows);
}
