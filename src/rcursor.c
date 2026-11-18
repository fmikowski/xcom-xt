/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The battlescape cursor's moves (render.h). What the cursor covers, its
   tile's 32x40 sprite box, is kept from the back buffer without the cursor
   while it stands there, so that moving it puts the picture back instead
   of redrawing the tiles around it; only the new box is redrawn. Any other
   redraw over the kept box, a scroll or a full redraw makes the copy stale
   (g_cs_ok = 0): then the old box is redrawn. Resident, outside the battle
   core's overlay section, which bounds the overlay area. */

#include <i86.h>
#include "gfx.h"
#include "render.h"

int g_cs_ok, g_cs_box[4];
unsigned g_cur_spr[3];

void render_cursor_sprite(int front, int sx, int sy)
{
    gfx_sprite(g_cur_spr[front], sx, sy);
    if (front && g_cur_spr[2])
        gfx_sprite(g_cur_spr[2], sx, sy);
}

/* The cursor's box at tile (x, y, z) in cells and rows, clipped to the
   view; 0 if it is off the view. */
static int box(int x, int y, int z, int *b)
{
    int sx = TILE_SX(x, y), sy = TILE_SY(x, y, z);

    b[0] = sx < 0 ? 0 : sx >> 3;
    b[1] = sy < 0 ? 0 : sy;
    b[2] = sx > 320 - 32 ? 40 : (sx + 32) >> 3;
    b[3] = sy > VIEW_H - 40 ? VIEW_H : sy + 40;
    return b[0] < b[2] && b[1] < b[3];
}

/* EGA: to the spare video memory between pages 0 and 1 (blit.asm); CGA:
   behind the back buffer (gfx.h BUFC_SAVE). */
static void copy(int out)
{
    int *b = g_cs_box;

    if (g_video == VIDEO_EGA)
        vram_box(g_bufseg, b[1] * BUF_W + b[0], b[2] - b[0], b[3] - b[1], out);
    else
        buf_box(g_bufseg, b[1] * BUFC_W + b[0] * 2, BUFC_W, (b[2] - b[0]) * 2, b[3] - b[1],
                MK_FP(g_bufseg, BUFC_W * BUF_ROWS), out);
}

/* The area a unit's sprites can cover at (x, y, z) (big: 2x2 tiles), as
   battle.c's render_at. */
static void unit_rect(int big, int x, int y, int z, int *r)
{
    r[0] = TILE_SX(x, y + big);
    r[1] = TILE_SY(x, y, z) - 24;
    r[2] = TILE_SX(x + big, y) + 32;
    r[3] = TILE_SY(x + big, y + big, z) + 40;
}

void render_step(int big, int ox, int oy, int oz, int x, int y, int z)
{
    int a[4], b[4], i;

    unit_rect(big, ox, oy, oz, a);
    unit_rect(big, x, y, z, b);
    for (i = 0; i < 2; i++) {
        if (b[i] < a[i])
            a[i] = b[i];
        if (b[i + 2] > a[i + 2])
            a[i + 2] = b[i + 2];
    }
    render_rect(a[0], a[1], a[2], a[3]);
}

void render_cursor_move(int ox, int oy, int oz)
{
    int ob[4], o;

    if (!g_cursor_on)
        return;
    o = box(ox, oy, oz, ob);
    g_cursor_on = 0;
    if (o) {
        if (g_cs_ok && ob[0] == g_cs_box[0] && ob[1] == g_cs_box[1] && ob[2] == g_cs_box[2]
            && ob[3] == g_cs_box[3])
            copy(0);
        else
            render_redraw(ob[0], ob[1], ob[2], ob[3]);
    }
    g_cursor_on = 1;
    g_cs_ok = 0;
    if (box(g_cur_x, g_cur_y, g_cur_z, g_cs_box)) {
        copy(1);
        render_redraw(g_cs_box[0], g_cs_box[1], g_cs_box[2], g_cs_box[3]);
        g_cs_ok = 1;
        gfx_present_cells(g_cs_box[0], g_cs_box[1], g_cs_box[2] - g_cs_box[0],
                          g_cs_box[3] - g_cs_box[1]);
    }
    if (o)
        gfx_present_cells(ob[0], ob[1], ob[2] - ob[0], ob[3] - ob[1]);
}
