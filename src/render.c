/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include "canvas.h"
#include "effects.h"
#include "gfx.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "panel.h"
#include "path.h"
#include "render.h"
#include "unit.h"

int g_cam_x, g_cam_y, g_view_z;
int g_all_layers;
unsigned char g_anim;
int g_cur_x, g_cur_y, g_cur_z, g_cursor_on;
unsigned char *g_seen;

/* Vertical reach of a tile's sprites around its box top: raised parts
   (P_Level) go up, the 40-pixel box goes down. */
#define REACH_UP        24
#define REACH_DOWN      40

#define VIEW_COLS       40

void render_center(int x, int y, int z)
{
    g_cam_x = 144 - ((x - y) << 4);
    g_cam_y = 72 - ((x + y) << 3) + z * 24;
}

/* A hovertank's turret rides higher by its body's direction (OpenXcom
   UnitSprite::drawRoutine2; the sideways shift is in its sprite set). */
static const signed char hover_y[8] = { -1, -3, -4, -5, -4, -3, -1, -1 };

/* Standing height of a tile (Tile::getTerrainLevel): the floor's or the
   object's, the higher one (negative = up). */
static int tile_level(const unsigned char *t)
{
    const Mcd *mcd = g_pak.mcd;
    int level = 0;

    if (t[PART_FLOOR])
        level = mcd[t[PART_FLOOR]].t_level;
    if (t[PART_OBJECT] && mcd[t[PART_OBJECT]].t_level < level)
        level = mcd[t[PART_OBJECT]].t_level;
    return level;
}

static void draw_unit(const Unit *u, const unsigned char *t, int x, int y, int sx, int sy)
{
    unsigned b = g_pak.unit_base[u->sprite_set];
    int part;

    if (u->big)             /* the quarters stand on the unit's own tile */
        t = TILE(u->x, u->y, u->z);
    sy += tile_level(t);
    if (u->big) {           /* 2x2: the quarter on this tile, part * 8 + dir */
        part = (x - u->x) + 2 * (y - u->y);
        if (part && u->xtype == XT_HOVER)
            /* A hovertank's propulsion under quarters 1-3 by the animation
               frame (drawRoutine2: 104 + (part - 1) * 8 + frame; here 48 +). */
            gfx_sprite(b + 40 + part * 8 + g_anim, sx, sy);
        if (part == 3 && UNIT_HWP(u)) {
            /* HWPs: the turret on the front quarter (frames 32 + its
               direction, 4 pixels up), on a hovertank facing the body's way
               the quarter with its turret (40 + direction). */
            if (u->xtype == XT_HOVER && u->turret == u->dir)
                part = 5;
            else {
                gfx_sprite(b + 24 + u->dir, sx, sy);
                gfx_sprite(b + 32 + u->turret, sx,
                           sy - 4 + (u->xtype == XT_HOVER ? hover_y[u->dir] : 0));
                return;
            }
        }
        gfx_sprite(b + part * 8 + u->dir, sx, sy);
    }
    else if (u->kneeling)   /* soldier sets carry 8 kneeling frames at 40 */
        gfx_sprite(b + 40 + u->dir, sx, sy);
    else if (u->xtype == XT_XCOM + ARM_FLYING && !u->phase && !tile_standable(u->x, u->y, u->z, &part))
        /* the Flying Suit hovering (drawRoutine0 legsFloat, BattleUnit
           _floating): frames 48-55, the standing ones when there was no
           room for them (pak.c LOAD_SUIT, floorld.c fly_load) */
        gfx_sprite(b + 48 + u->dir, sx, sy);
    else
        gfx_sprite(b + u->dir * 5 + u->phase, sx, sy);
}
/* Tiles of the current level with walls, objects, units or the cursor,
   collected during the floor pass in drawing order. */
#define MAX_LATE 400            /* a level has about 310 tiles in the view */
static unsigned late[MAX_LATE];
/* An entry is the tile's key x << 8 | y (maps are at most 60 tiles wide,
   so x and y take 6 bits each; the tile index and the screen position
   follow from them) with flags in the free bits. */
#define LATE_KEY    0x3F3F
#define LATE_HIDDEN 0x0040      /* not yet seen */
#define LATE_FX     0x0080      /* effects (fx_draw) */
#define LATE_FLOOR  0x4000      /* items or bodies (floor_draw) */
#define LATE_ROOF   0x8000      /* full floors of the level above cover it (rendasm.asm) */

static unsigned add[FX_MAX_TILES];

/* Merges the m tiles of level z in add[] into late[], which is ordered by
   x, then y: the few tiles with effects or items are sorted and merged
   in (with the bit), so the floor pass pays nothing for them. Returns the
   new count. */
static int merge(int z, int n, int m, unsigned bit, int x0, int y0, int x1, int y1)
{
    int na = 0, nn, i, j, k, x, y;

    /* Keep the tiles in the rectangle that X-COM has seen, sorted, once. */
    for (k = 0; k < m; k++) {
        unsigned key = add[k];
        int sx, sy;
        x = key >> 8;
        y = key & 0xFF;
        sx = TILE_SX(x, y);
        sy = TILE_SY(x, y, z);
        if (sx <= x0 - 32 || sx >= x1 || sy <= y0 - REACH_DOWN || sy >= y1 + REACH_UP
            || (g_seen && !(g_seen[TIDX(x, y, z)] & SEEN_BIT)))
            continue;
        for (i = na; i > 0 && add[i - 1] > key; i--)
            ;
        if (i > 0 && add[i - 1] == key)
            continue;                           /* already there */
        for (j = na; j > i; j--)
            add[j] = add[j - 1];
        add[i] = key;
        na++;
    }
    if (!na)
        return n;
    /* Mark the tiles late[] has; count the others. */
    nn = n;
    for (i = j = 0; j < na; j++) {
        while (i < n && (late[i] & LATE_KEY) < add[j])
            i++;
        if (i < n && (late[i] & LATE_KEY) == add[j]) {
            late[i] |= bit;
            add[j] = 0xFFFF;                    /* done */
        } else
            nn++;
    }
    if (nn > MAX_LATE)
        return n;
    /* Merge from the back. */
    i = n - 1;
    k = nn - 1;
    for (j = na - 1; j >= 0; j--) {
        unsigned key = add[j];
        if (key == 0xFFFF)
            continue;
        while (i >= 0 && (late[i] & LATE_KEY) > key)
            late[k--] = late[i--];
        late[k--] = key | bit;
    }
    return nn;
}

/* The floor pass in assembly (rendasm.asm): rd_setup before a redraw (the
   clip rectangle is the redrawn one), then per level rd_level draws the
   floors of the tiles whose sprites can touch it (ground floors with the
   OR blitter on the cleared area) and collects in late[] the tiles with
   walls, objects, units or the cursor, in drawing order; returns how many. */
void __cdecl rd_setup(int top, const Mcd *mcd, unsigned char **spr, unsigned *late,
                      unsigned *cov, int occ);
int __cdecl rd_level(int z);

/* Covered sprites (packs with cover data, tools/occlude.py): what later
   sprites draw over whole is not drawn; the picture stays the same. A
   floor inside its diamond is left out when each of its four quads is
   drawn whole by a wall or object of its tile or of one in front on its
   level (rd_level, with add[] as its cover map), or by a full floor of one
   of the two levels above; a tile's walls, objects, floor items and
   effects when full floors of the level above cover all its sprites can
   reach (rd_level marks it LATE_ROOF; a unit is drawn all the same). */
int g_occ_off;
unsigned g_occ_count;

/* Draws every tile whose sprites can touch the pixel rectangle, back to
   front. Per level all floors go first, then the tiles collected in late[]. */
static void draw_region(int x0, int y0, int x1, int y1)
{
    const Mcd *mcd = g_pak.mcd;
    unsigned level_tiles = g_mx * g_my;
    int z, i, n_late, top = g_all_layers ? (int)g_mz - 1 : g_view_z;

    int occ = g_pak.occ && !g_occ_off;

    rd_setup(top, mcd, g_pak.sprite, late, add, occ);
    for (z = 0; z <= top; z++) {
        int cursor_z = g_cursor_on && z == g_cur_z;
        unsigned zbase = z * level_tiles;

        n_late = rd_level(z);
        if (g_fx_count)
            n_late = merge(z, n_late, fx_level_tiles(z, add), LATE_FX, x0, y0, x1, y1);
        n_late = merge(z, n_late, floor_tiles(z, add, FX_MAX_TILES), LATE_FLOOR, x0, y0, x1, y1);
        for (i = 0; i < n_late; i++) {
            unsigned l = late[i];
            int lx = l >> 8 & 0x3F, ly = l & 0x3F;
            unsigned idx = zbase + ly * g_mx + lx;
            const unsigned char *t = g_map + (idx << 2);
            int sx = TILE_SX(lx, ly), sy = TILE_SY(lx, ly, z);
            int cursor = cursor_z && lx == g_cur_x && ly == g_cur_y;
            unsigned char u;

            if (cursor)
                render_cursor_sprite(0, sx, sy);
            if (!(l & LATE_HIDDEN)) {
                int over = l & LATE_ROOF;
                if (over)
                    g_occ_count++;
                else {
                    if (t[PART_WESTWALL]) {
                        const Mcd *m = &mcd[t[PART_WESTWALL]];
                        gfx_sprite(m->sprite, sx, sy - m->p_level);
                    }
                    if (t[PART_NORTHWALL]) {
                        const Mcd *m = &mcd[t[PART_NORTHWALL]];
                        gfx_sprite(m->sprite, sx, sy - m->p_level);
                    }
                    if (t[PART_OBJECT]) {
                        const Mcd *m = &mcd[t[PART_OBJECT]];
                        gfx_sprite(m->sprite, sx, sy - m->p_level);
                    }
                    if (l & LATE_FLOOR)
                        floor_draw(lx, ly, z, sx, sy + tile_level(t));
                }
                u = g_unit_at ? g_unit_at[idx] : 0;
                if (u) {
                    const Unit *un = &g_units[u - 1];
                    if (un->faction == FACTION_XCOM || un->seen) {
                        draw_unit(un, t, lx, ly, sx, sy);
                        if ((un->burning & ~UB_HIT) && !(g_los_flags[idx] & LF_FIRE))
                            fx_flames(sx, sy);      /* a burning unit */
                    }
                }
                if ((l & LATE_FX) && !over)
                    fx_draw(lx, ly, z, sx, sy);
            }
            if (cursor)
                render_cursor_sprite(1, sx, sy);
        }
    }
}
/* Redraws a window rectangle in the back buffer. Columns are bytes and must
   be even (16-pixel words); sprites are clipped to it, so the rest of the
   window stays intact. */
void render_redraw(int c0, int r0, int c1, int r1)
{
    int r[4];

    if (g_cs_ok && c0 < g_cs_box[2] && g_cs_box[0] < c1 && r0 < g_cs_box[3] && g_cs_box[1] < r1)
        g_cs_ok = 0;            /* the kept cursor box is drawn over */
    r[0] = c0;
    r[1] = r0;
    r[2] = c1;
    r[3] = r1;
    /* The canvas (canvas.h) copies what it has; r is what is left. */
    if (g_cv == 1 && !cv_op(CV_READ, r))
        return;
    g_clip_x0 = r[0] * 8;
    g_clip_y0 = r[1];
    g_clip_x1 = r[2] * 8;
    g_clip_y1 = r[3];
    gfx_clear_cells(r[0], r[1], r[2] - r[0], r[3] - r[1]);
    draw_region(r[0] * 8, r[1], r[2] * 8, r[3]);
    if (g_cv)
        cv_op(CV_STORE, r);
}

void render_rect(int x0, int y0, int x1, int y1)
{
    int c0, c1;

    if (g_cv)
        g_cv = 2;               /* a change: drawn, not looked up (canvas.h) */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 320) x1 = 320;
    if (y1 > VIEW_H) y1 = VIEW_H;
    if (x0 >= x1 || y0 >= y1)
        return;
    c0 = (x0 >> 4) << 1;
    c1 = ((x1 + 15) >> 4) << 1;
    render_redraw(c0, y0, c1, y1);
    gfx_present_cells(c0, y0, c1 - c0, y1 - y0);
}

void render_full(void)
{
    render_redraw(0, 0, VIEW_COLS, VIEW_H);
    gfx_present_cells(0, 0, VIEW_COLS, VIEW_H);
}
void render_tile(int x, int y, int z)
{
    int sx = TILE_SX(x, y), sy = TILE_SY(x, y, z);

    if (g_cv)
        cv_tile(x, y, z);       /* terrain_changed: the canvas forgets the tile */
    render_rect(sx, sy - REACH_UP, sx + 32, sy + REACH_DOWN);
}

int render_tile_visible(int x, int y, int z)
{
    int sx = TILE_SX(x, y), sy = TILE_SY(x, y, z);
    return sx >= 32 && sx <= 320 - 64 && sy >= 8 && sy <= VIEW_H - 56;
}

void render_scroll(int cam_x, int cam_y)
{
    int dx = cam_x - g_cam_x, dy = cam_y - g_cam_y, dc = dx / 8;

    if (dx == 0 && dy == 0)
        return;
    g_cam_x = cam_x;
    g_cam_y = cam_y;
    if ((dx & 15) || dc >= VIEW_COLS - 8 || -dc >= VIEW_COLS - 8
        || dy >= VIEW_H - 32 || -dy >= VIEW_H - 32) {
        render_full();
        return;
    }
    g_cs_ok = 0;                /* the kept cursor box moves away */
    gfx_shift_window(dc, dy);
    if (g_video == VIDEO_EGA)
        panel_shift(dc, dy);    /* the markers stay where they are */
    if (dc > 0)
        render_redraw(0, 0, dc, VIEW_H);
    else if (dc < 0)
        render_redraw(VIEW_COLS + dc, 0, VIEW_COLS, VIEW_H);
    if (dy > 0)
        render_redraw(0, 0, VIEW_COLS, dy);
    else if (dy < 0)
        render_redraw(0, VIEW_H + dy, VIEW_COLS, VIEW_H);
    if (g_video == VIDEO_EGA) {
        /* The rest moved on the screen already: show the strips. */
        if (dc > 0)
            gfx_present_cells(0, 0, dc, VIEW_H);
        else if (dc < 0)
            gfx_present_cells(VIEW_COLS + dc, 0, -dc, VIEW_H);
        if (dy > 0)
            gfx_present_cells(0, 0, VIEW_COLS, dy);
        else if (dy < 0)
            gfx_present_cells(0, VIEW_H + dy, VIEW_COLS, -dy);
    } else
        gfx_present_cells(0, 0, VIEW_COLS, VIEW_H);
}

void render_follow(int x, int y, int z)
{
    int old_x = g_cam_x, old_y = g_cam_y;

    if (render_tile_visible(x, y, z))
        return;
    render_center(x, y, z);
    {
        int nx = g_cam_x, ny = g_cam_y;
        g_cam_x = old_x;
        g_cam_y = old_y;
        render_scroll(nx, ny);
    }
}
