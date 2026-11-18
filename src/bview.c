/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What X-COM must see outside its own turn (OpenXcom Map::draw, which shows
   the map instead of Hidden Movement for a projectile on a visible tile, a
   dying soldier and an explosion; Map::drawTerrain moves the camera after a
   projectile, ExplosionBState centres it on a blast and on a melee victim),
   and the blast's picture. A section of its own: rarely needed, it calls
   only the core and resident code, so while it runs no other section loads
   (build.ps1 $beside; docs/design.md, "Seeing the soldier shot"). Each jump
   of the camera is a full redraw, about 1 s on the 8088: only for a place
   off the screen or on another level. */

#include <dos.h>
#include <fcntl.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "battle.h"
#include "battle_in.h"
#include "gfx.h"
#include "input.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "render.h"
#include "sound.h"
#include "timer.h"
#include "unit.h"

/* HOLDVIEW n (tests): stays a minute on the picture of a round (bit 0), a
   fallen soldier (1) or a blast (2), for the screenshot. */
static void hold(int bit)
{
    if (g_view_hold & bit) {
        log_printf("VIEW HOLD");
        timer_pace(timer_read(), 60000u, 0);
    }
}

/* The view on a tile: its level (Camera::setViewLevel, the cursor along), and
   centred if it is off the screen; the map instead of Hidden Movement. While
   that is up the redraw goes only into the back buffer: one present. */
static void look(int x, int y, int z)
{
    int sx = TILE_SX(x, y), sy = TILE_SY(x, y, z);
    int off = sx < 0 || sx > 320 - 32 || sy < 0 || sy > VIEW_H - 40;

    if (off || z != g_view_z) {
        log_printf("T%d: view to %d,%d,%d", bt_turn, x, y, z);
        if (z == g_view_z)
            render_follow(x, y, z);
        else {
            if (off)
                render_center(x, y, z);
            g_view_z = g_cur_z = z;
            render_full();
        }
    }
    bt_view_on();
}

/* A soldier stands here or sees it: X-COM's view at this moment, as near as
   a line of sight from any soldier tells (Tile::getVisible; the view cones
   left out). */
static int watched(int x, int y, int z)
{
    int i, vx, vy;
    const Unit *u;

    if (g_seen && !(g_seen[TIDX(x, y, z)] & SEEN_BIT))
        return 0;
    for (i = 0; i < g_nunits; i++) {
        u = &g_units[i];
        if (!u->alive || u->faction != FACTION_XCOM)
            continue;
        vx = x - u->x;
        vy = y - u->y;
        if (vx >= -VIEW_RANGE && vx <= VIEW_RANGE && vy >= -VIEW_RANGE && vy <= VIEW_RANGE
            && vx * vx + vy * vy <= VIEW_RANGE * VIEW_RANGE
            && los_clear(u->x, u->y, u->z, x, y, z))
            return 1;
    }
    return 0;
}

/* A round from voxel o to voxel h, n dots a tile apart (battle.c fire_round):
   the view goes to the last place on its way that X-COM sees (Map::draw
   _projectileInFOV, Map::drawTerrain follows it there; one jump, not one per
   screen). */
void bv_round(const int *o, const int *h, int n)
{
    int sx = (h[0] - o[0]) / n, sy = (h[1] - o[1]) / n, sz = (h[2] - o[2]) / n;
    int vx = h[0], vy = h[1], vz = h[2], x, y, z;

    for (; n > 0; n -= 2, vx -= 2 * sx, vy -= 2 * sy, vz -= 2 * sz) {
        x = vx >> 4;
        y = vy >> 4;
        z = vz / 24;
        if (vx >= 0 && vy >= 0 && vz >= 0 && IN_MAP(x, y, z) && watched(x, y, z)) {
            look(x, y, z);
            hold(1);
            return;
        }
    }
}

/* The view on a soldier. */
static void look_unit(int u)
{
    const Unit *v = &g_units[u];

    look(v->x, v->y, v->z);
}

/* A blow at soldier t: the view on him (ExplosionBState, the cosmetic hit) and
   the hit's 4 frames of 50 ms. */
void bv_melee(int t)
{
    look_unit(t);
    timer_pace(timer_read(), 200, 'h');
}

/* A soldier down (Map::setUnitDying): the view on him, already drawn fallen,
   for the time of UnitDieBState: 6 steps of 100 ms. */
void bv_fallen(int u)
{
    unsigned long t0 = timer_read();

    look_unit(u);
    hold(2);
    timer_pace(t0, 600, 'd');
}

/* BLASTV.PAK (tools/convert.py build_blast): "XBL1", u8 video, u8 pad, u16 n,
   then n bytes: 8 x u16 offsets of 8 sprites of 32 x 40, 4 in a row, the
   middle of the blast at (64, BLAST_MID). */
#define BLAST_MID   44
#define BLAST_W     128
#define BLAST_H     80
#define BLAST_KEEP  768         /* paragraphs the battle keeps free (floorld.c) */

/* A blast: the view on it (ExplosionBState: every blast, Map::draw isBig, seen
   or not), the picture with the sound for its 8 frames of 50 ms, read from the
   disk for the time being when 12 KB stay free, else only the sound. Then what
   it covered comes back; the blast takes effect after it, as in DOS. Returns 1
   if no picture took the time. */
int bv_blast(int cx, int cy, int cz)
{
    unsigned char hd[8];
    unsigned char *b;
    const unsigned short *off;
    unsigned seg, size = 0, got, i;
    int fd, x, y, c0, c1, r0, r1, shown = 0;
    unsigned long t0;

    look(cx, cy, cz);
    if (!_dos_open("BLASTE.PAK\0BLASTC.PAK\0BLASTV.PAK" + 11 * g_pak.video, O_RDONLY, &fd)) {
        if (!_dos_read(fd, hd, 8, &got) && got == 8 && hd[4] == g_pak.video) {
            size = *(unsigned short *)(hd + 6);
            _dos_allocmem(0xFFFF, &seg);    /* fails: seg = the largest block */
            if (seg >= (size + 15) / 16 + BLAST_KEEP && !_dos_allocmem((size + 15) / 16, &seg)) {
                b = (unsigned char *)MK_FP(seg, 0);
                shown = !_dos_read(fd, b, size, &got) && got == size;
                if (!shown)
                    _dos_freemem(seg);
            }
        }
        _dos_close(fd);
    }
    if (!shown) {
        log_printf("blast picture: none (%u bytes)", size);
        snd_explode();
        hold(4);
        return 1;
    }
    /* Through the sprite table's spare entry, as the floor's pictures. */
    off = (const unsigned short *)b;
    x = TILE_SX(cx, cy) - (BLAST_W / 2 - 16);
    y = TILE_SY(cx, cy, cz) + 32 - BLAST_MID;
    for (i = 0; i < 8; i++) {
        g_pak.sprite[g_pak.n_sprites] = b + off[i];
        gfx_sprite(g_pak.n_sprites, x + (i & 3) * 32, y + (i >> 2) * 40);
    }
    c0 = x < 0 ? 0 : x >> 3;
    c1 = x + BLAST_W > 320 ? 40 : (x + BLAST_W) >> 3;
    r0 = y < 0 ? 0 : y;
    r1 = y + BLAST_H > VIEW_H ? VIEW_H : y + BLAST_H;
    if (c0 < c1 && r0 < r1)
        gfx_present_cells(c0, r0, c1 - c0, r1 - r0);
    snd_explode();
    t0 = timer_read();
    log_printf("T%d: blast picture", bt_turn);
    hold(4);
    timer_pace(t0, 400, 'x');
    _dos_freemem(seg);
    render_rect(x, y, x + BLAST_W, y + BLAST_H);
    return 0;
}
