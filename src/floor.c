/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Items on the battle floor drawn as DOS draws them (Map::drawTerrain,
   Tile::getTopItemSprite): a tile's heaviest item, body or device with
   its FLOOROB picture, on the tile's floor, after its walls and object
   and before its unit. In the battle core (render.c calls it); the
   pictures are loaded by floorld.c. */

#include "effects.h"
#include "gfx.h"
#include "inv.h"
#include "pak.h"
#include "unit.h"

unsigned char *g_fob;
unsigned g_fob_size, g_fob_sum;

/* scan's two uses: collecting tile keys (out), or finding the picture
   of the tile want. */
static unsigned *out, want;
static int n_out, max_out, best;

/* What lies at tile key k (x << 8 | y): into out[] if there is room,
   else, if it is the tile wanted, its picture slot s if that is heavier
   than the best so far (the first of equals stays, as the first item in
   a DOS tile's list). */

static void found(unsigned k, int s)
{
    if (out) {
        if (n_out < max_out)
            out[n_out++] = k;
    } else if (k == want && s != 0xFF
               && (best < 0 || g_fob[FOB_N + 1 + s] > g_fob[FOB_N + 1 + best]))
        best = s;
}

/* Every item, body and device of level z. */
static void scan(int z)
{
    int i, q, b;
    unsigned char at[3];
    const FloorItem __near *f;

    for (f = g_floor; f < g_floor_end; f++)
        if (f->item != NO_ITEM && f->z == z)
            found(f->x << 8 | f->y, g_fob ? g_fob[f->item] : 0xFF);
    for (i = 0; i < g_nunits; i++) {
        const Unit *u = &g_units[i];
        if (u->alive || u->z != z || (u->special & SP_CARRIED) || (b = unit_body(u)) < 0)
            continue;
        b = g_fob ? g_fob[N_ITEMS + b] : 0xFF;
        /* A 2x2 body: its quarters' pictures follow each other. */
        for (q = 0; q < (u->big ? 4 : 1); q++)
            found((u->x + (q & 1)) << 8 | (u->y + (q >> 1)), b == 0xFF ? b : b + q);
    }
    for (i = 0; (b = fx_dev_pos(i, at)) != NO_ITEM; i++)   /* devices 0 .. n - 1 */
        if (at[2] == z)
            found(at[0] << 8 | at[1], g_fob ? g_fob[b] : 0xFF);
}

int floor_tiles(int z, unsigned *keys, int max)
{
    out = keys;
    n_out = 0;
    max_out = max;
    scan(z);
    out = 0;
    return n_out;
}

void floor_draw(int x, int y, int z, int sx, int sy)
{
    best = -1;
    want = x << 8 | y;
    scan(z);
    if (best < 0) {
        fx_mark(sx, sy);            /* no picture loaded for it */
        return;
    }
    /* Through the sprite table's spare entry (src/pak.c). */
    g_pak.sprite[g_pak.n_sprites] =
        g_fob + ((const unsigned short *)(g_fob + FOB_N + 1 + g_fob[FOB_N]))[best];
    gfx_sprite(g_pak.n_sprites, sx, sy);
}
