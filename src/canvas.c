/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The battle view's canvas (docs/design.md "The canvas"). With EMS or XMS
   the pictures the view showed are kept there by map position (blocks of
   128 x 24 pixels, cells of 16 x 8, per number of levels drawn), so that a
   redraw of a place whose picture is still right is a copy instead of a
   drawing: scrolls back over seen ground, level changes, jumps to a unit,
   the view after the inventory. A picture is right while nothing that the
   renderer draws there has changed. Changes reach the canvas two ways:
   render_tile's tiles (terrain_changed: every change to the map's parts),
   and shadows of everything else render.c reads (the units, the items on
   the floor, the devices, the effects' tiles and flags, the fog bits of
   the tiles there), compared before a copy and before a picture is kept,
   so nothing drawn from changed state is copied. render_rect's redraws
   (the game showing a change) draw without asking. The cursor is never
   kept: cells under it are drawn. While the player's turn waits for a
   key, the blocks around the view are drawn into the canvas (cv_idle).
   An overlay section of its own: it runs from an overlay area in a UMB,
   next to the battle core and the AI or the player's turn (cvinit.c
   cv_battle checks that there is one). No static data but two words. */

#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "canvas.h"
#include "effects.h"
#include "gfx.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "render.h"
#include "sound.h"
#include "timer.h"
#include "unit.h"

#include "cvpriv.h"

int g_cv;
Cv far *cvd;

static int top(void)
{
    return g_all_layers ? (int)g_mz - 1 : g_view_z;
}

/* ------------------------------------------------------------------ */
/* Blocks: a hash of key (top, column, row) to slot */

#define KEY(t, bi, bj)  ((unsigned)(t) << 13 | (unsigned)(bi) << 7 | (unsigned)(bj))

static unsigned home(unsigned k)
{
    return (unsigned)(k * 40503u) >> (16 - cvd->hbits);
}

static unsigned find(unsigned k)
{
    unsigned m = (1u << cvd->hbits) - 1, i = home(k), s;

    while ((s = cvd->hash[i]) != 0) {
        if (cvd->key[s - 1] == k)
            return s - 1;
        i = (i + 1) & m;
    }
    return 0xFFFF;
}

static void unhash(unsigned k)
{
    unsigned m = (1u << cvd->hbits) - 1, i = home(k), j, h, s;

    while ((s = cvd->hash[i]) != 0 && cvd->key[s - 1] != k)
        i = (i + 1) & m;
    if (!s)
        return;
    cvd->hash[i] = 0;
    for (j = (i + 1) & m; (s = cvd->hash[j]) != 0; j = (j + 1) & m) {
        h = home(cvd->key[s - 1]);
        if (i <= j ? (h <= i || h > j) : (h <= i && h > j)) {
            cvd->hash[i] = s;
            cvd->hash[j] = 0;
            i = j;
        }
    }
}

/* A slot for key k (the clock takes one back when all are used). */
static unsigned take(unsigned k)
{
    unsigned m = (1u << cvd->hbits) - 1, s, i;

    if (cvd->used < cvd->slots)
        s = cvd->used++;
    else {
        for (;;) {
            s = cvd->hand;
            if (++cvd->hand == cvd->slots)
                cvd->hand = 0;
            if (!cvd->ref[s])
                break;
            cvd->ref[s] = 0;
        }
        unhash(cvd->key[s]);
        cvd->n_evict++;
    }
    cvd->key[s] = k;
    _fmemset(cvd->mask + s * 3, 0, 3);
    cvd->ref[s] = 1;
    for (i = home(k); cvd->hash[i]; i = (i + 1) & m)
        ;
    cvd->hash[i] = s + 1;
    return s;
}

/* Cells: per block 8 columns x 3 rows (a byte each); op 0 clears, 1 sets,
   2 counts the set ones among columns c0..c1-1, rows r0..r1-1. */
static int cells(unsigned s, int c0, int c1, int r0, int r1, int op)
{
    unsigned char far *p = cvd->mask + s * 3;
    unsigned char b = (unsigned char)((1 << c1) - (1 << c0)), v;
    int r, n = 0;

    for (r = r0; r < r1; r++) {
        if (op == 0)
            p[r] &= (unsigned char)~b;
        else if (op == 1)
            p[r] |= b;
        else
            for (v = (unsigned char)(b & p[r]); v; v &= v - 1)
                n++;
    }
    return n;
}

/* The block's data: EMS mapped, XMS in the bounce buffer (read in if load). */
static void xmove(int out, unsigned long off, void far *p, unsigned len)
{
    unsigned r[5];
    unsigned long a = (unsigned long)FP_SEG(p) << 16 | FP_OFF(p);

    off += cvd->xbase;
    cvd->len = len;
    cvd->sh = out ? 0 : cvd->handle;
    cvd->so = out ? a : off;
    cvd->dh = out ? cvd->handle : 0;
    cvd->dof = out ? off : a;
    r[0] = 0x0B00;
    r[1] = r[2] = 0;
    r[3] = FP_OFF(cvd);
    r[4] = FP_SEG(cvd);
    xms_call((unsigned far *)r);
    if (r[0] != 1)
        cvd->n_bad++;
}

/* A block's layout: EGA 4 planes of BLK_H rows of BLK_W bytes; VGA 8
   columns of 16 pixels, each BLK_H rows of 16 bytes (a column 384 bytes,
   so that a strip's columns are one XMS move). The offset of pixel dx, dy
   (dx a multiple of 16 in VGA, of 8 in EGA). */
#define COL_SZ (16 * BLK_H)
static unsigned koff(int dx, int dy)
{
    return (unsigned)dy * BLK_W + (cvd->vga ? (unsigned)(dx >> 4) * COL_SZ : (unsigned)dx >> 3);
}

/* XMS: block s's 16-pixel columns c0..c1-1 between the handle and the
   bounce buffer (EGA's planes: the whole block). */
static void xcols(int out, unsigned s, unsigned c0, unsigned c1)
{
    unsigned long off = (unsigned long)s * cvd->bsz;

    if (!cvd->vga)
        c0 = 0, c1 = BLK_SZ / COL_SZ;       /* (1536 = 4 x 384) */
    xmove(out, off + c0 * COL_SZ, cvd->bounce + c0 * COL_SZ, (c1 - c0) * COL_SZ);
}

static unsigned char far *blk(unsigned s, int load, unsigned c0, unsigned c1)
{
    if (cvd->mode == MODE_EMS) {
        unsigned pg = s / cvd->per_pg;
        if (pg != cvd->page) {
            if (cv_map(cvd->handle, pg))
                cvd->n_bad++;
            cvd->page = pg;
        }
        return (unsigned char far *)MK_FP(cvd->frame + (s % cvd->per_pg) * (cvd->bsz / 16), 0);
    }
    if (load)
        xcols(0, s, c0, c1);
    return cvd->bounce;
}

/* ------------------------------------------------------------------ */
/* Rectangles in world pixels (screen - camera): x0, y0, x1, y1 */

/* What a world rectangle does to the blocks of top t:
   R_INVAL clears the cells it touches; R_TEST counts the cells it touches
   (cvd->n... in o[0]) and the invalid ones (o[1]) with their bounding box
   (o[2..5]); R_GET copies the blocks with valid cells to the back buffer
   (and clears the parts outside the map); R_PUT keeps the back buffer's
   rectangle (cells inside it become valid, not those under the cursor
   box C). */
#define R_INVAL 0
#define R_TEST  1
#define R_GET   2
#define R_PUT   3

static int outside(int X, int Y)
{
    return X + BLK_PX <= -(int)g_my * 16 || X >= ((int)g_mx + 1) * 16
        || Y + BLK_H <= -(int)g_mz * 24 - 24 || Y >= ((int)g_mx + (int)g_my) * 8 + 40;
}

static void rect(int t, const int *R, int op, const int *C, int *o)
{
    int bi, bj, bi0 = (R[0] + OFFX) >> BLK_SH, bi1 = (R[2] - 1 + OFFX) >> BLK_SH;
    int bj0 = (R[1] + OFFY) / BLK_H, bj1 = (R[3] - 1 + OFFY) / BLK_H;

    for (bj = bj0; bj <= bj1; bj++)
        for (bi = bi0; bi <= bi1; bi++) {
            int X = (bi << BLK_SH) - OFFX, Y = bj * BLK_H - OFFY;
            int x0 = R[0] > X ? R[0] : X, x1 = R[2] < X + BLK_PX ? R[2] : X + BLK_PX;
            int y0 = R[1] > Y ? R[1] : Y, y1 = R[3] < Y + BLK_H ? R[3] : Y + BLK_H;
            /* cells touched, cells inside */
            int c0 = (x0 - X) >> 4, c1 = (x1 - X + 15) >> 4, r0 = (y0 - Y) >> 3, r1 = (y1 - Y + 7) >> 3;
            int f0 = (y0 - Y + 7) >> 3, f1 = (y1 - Y) >> 3;
            unsigned sh = cvd->psh, s = find(KEY(t, bi, bj)), boff = (unsigned)(y0 + g_cam_y) * cvd->bw
                         + ((x0 + g_cam_x) >> sh);
            unsigned ko = koff(x0 - X, y0 - Y);
            unsigned bytes = (unsigned)(x1 - x0) >> sh, rows = (unsigned)(y1 - y0);
            int k, n;
            unsigned char far *b;

            if (op == R_INVAL) {
                if (s != 0xFFFF) {
                    cells(s, c0, c1, r0, r1, 0);
                    if (!cells(s, 0, 8, 0, 3, 2))
                        cvd->ref[s] = 0;
                }
                continue;
            }
            if (outside(X, Y)) {
                /* nothing is ever drawn there */
                if (op == R_GET)
                    gfx_clear_cells((x0 + g_cam_x) >> 3, y0 + g_cam_y, (x1 - x0) >> 3, rows);
                if (op == R_TEST)
                    o[0] += (c1 - c0) * (r1 - r0);
                continue;
            }
            if (op == R_TEST) {
                /* cells touched; the valid ones, less those under the cursor */
                n = (c1 - c0) * (r1 - r0);
                o[0] += n;
                k = s == 0xFFFF ? 0 : cells(s, c0, c1, r0, r1, 2);
                if (k && C && C[0] < X + BLK_PX && C[2] > X && C[1] < Y + BLK_H && C[3] > Y) {
                    int a0 = (C[0] - X) >> 4, a1 = (C[2] - X + 15) >> 4;
                    int e0 = (C[1] - Y) >> 3, e1 = (C[3] - Y + 7) >> 3;
                    if (a0 < c0) a0 = c0;
                    if (a1 > c1) a1 = c1;
                    if (e0 < r0) e0 = r0;
                    if (e1 > r1) e1 = r1;
                    if (a0 < a1 && e0 < e1)
                        k -= cells(s, a0, a1, e0, e1, 2);
                }
                if (k < n) {
                    /* the block's touched part counts as to be drawn */
                    o[1] += n - k;
                    if (o[2] > x0) o[2] = x0;
                    if (o[3] > y0) o[3] = y0;
                    if (o[4] < x1) o[4] = x1;
                    if (o[5] < y1) o[5] = y1;
                }
                continue;
            }
            if (op == R_GET) {
                if (s == 0xFFFF || !cells(s, c0, c1, r0, r1, 2))
                    continue;
                cvd->ref[s] = 1;
                b = blk(s, 1, c0, c1);
                cv_copy(g_bufseg, boff, b + ko, BLK_W, bytes, rows, cvd->vga);
                continue;
            }
            /* R_PUT */
            if (s == 0xFFFF) {
                if (f0 >= f1)
                    continue;       /* no cell becomes valid */
                s = take(KEY(t, bi, bj));
            } else if (cvd->check) {
                /* the check: every valid cell inside must be what was drawn */
                for (k = c0; k < c1; k++)
                    for (n = f0; n < f1; n++)
                        if (cells(s, k, k + 1, n, n + 1, 2)) {
                            int vx = X + 16 * k, vy = Y + 8 * n;
                            if (vx < x0 || vx + 16 > x1 || (C && vx < C[2] && vx + 16 > C[0]
                                                            && vy < C[3] && vy + 8 > C[1]))
                                continue;
                            b = blk(s, 1, k, k + 1);
                            if (cv_cmp(g_bufseg, (unsigned)(vy + g_cam_y) * cvd->bw + ((vx + g_cam_x) >> sh),
                                       b + koff(vx - X, vy - Y), BLK_W,
                                       16 >> sh, 8, cvd->vga)) {
                                cvd->n_bad++;
                                if (cvd->n_bad < 20)
                                    log_printf("canvas: CHECK cell %d,%d top %d differs", vx, vy, t);
                            }
                        }
            }
            /* (XMS: VGA reads and writes the columns it changes) */
            b = blk(s, cvd->mode == MODE_XMS && (y0 > Y || y1 < Y + BLK_H
                                                 || (!cvd->vga && (x0 > X || x1 < X + BLK_PX)))
                       && cells(s, 0, 8, 0, 3, 2), c0, c1);
            cv_copy(g_bufseg, boff, b + ko, BLK_W, bytes, rows, 1 | cvd->vga);
            if (cvd->mode == MODE_XMS)
                xcols(1, s, c0, c1);
            cvd->ref[s] = 1;
            /* cells wholly inside (columns: x0, x1 are multiples of 16) */
            if (f0 < f1)
                cells(s, c0, (x1 - X) >> 4, f0, f1, 1);
            if (C && C[0] < X + BLK_PX && C[2] > X && C[1] < Y + BLK_H && C[3] > Y) {
                int a0 = (C[0] - X) >> 4, a1 = (C[2] - X + 15) >> 4;
                int e0 = (C[1] - Y) >> 3, e1 = (C[3] - Y + 7) >> 3;
                cells(s, a0 < 0 ? 0 : a0, a1 > 8 ? 8 : a1, e0 < 0 ? 0 : e0, e1 > 3 ? 3 : e1, 0);
            }
        }
}

/* Clears what a change touched in the levels drawn from z up. */
static void inval(int z, const int *R)
{
    int t;

    cvd->gen++;
    for (t = z < 0 ? 0 : z; t < (int)g_mz; t++)
        rect(t, R, R_INVAL, NULL, NULL);
}

/* A tile's sprites (big: the four of a 2x2 unit), render.c's REACH. */
static void inval_tile(int x, int y, int z, int big)
{
    int R[4];

    R[0] = (x - y - big) * 16;
    R[1] = (x + y) * 8 - z * 24 - 24;
    R[2] = (x - y + big) * 16 + 32;
    R[3] = (x + y + 2 * big) * 8 - z * 24 + 40;
    inval(z, R);
}

/* The cursor's sprite box if top t draws it. */
static int cursor(int t, int *C)
{
    if (!g_cursor_on || g_cur_z > t)
        return 0;
    C[0] = (g_cur_x - g_cur_y) * 16;
    C[1] = (g_cur_x + g_cur_y) * 8 - g_cur_z * 24;
    C[2] = C[0] + 32;
    C[3] = C[1] + 40;
    return 1;
}

/* Everything again (the fog switched, the floor pictures changed). */
static void reset(void)
{
    _fmemset(cvd->mask, 0, cvd->slots * 3);
    _fmemset(cvd->ref, 0, cvd->slots);
    cvd->gen++;
}

/* ------------------------------------------------------------------ */
/* Shadows of what the renderer reads */

static int tile_level(int x, int y, int z)
{
    const unsigned char *t = TILE(x, y, z);
    const Mcd *mcd = g_pak.mcd;
    int level = 0;

    if (t[PART_FLOOR])
        level = mcd[t[PART_FLOOR]].t_level;
    if (t[PART_OBJECT] && mcd[t[PART_OBJECT]].t_level < level)
        level = mcd[t[PART_OBJECT]].t_level;
    return level;
}

/* render.c draw_unit's inputs, floor.c's body. */
static void urec(int i, USh *r)
{
    const Unit *u = &g_units[i];
    int h;

    memset(r, 0, sizeof *r);
    if (i >= g_nunits)
        return;
    if (u->alive) {
        if ((u->faction != FACTION_XCOM && !u->seen) || UNIT_AT(u->x, u->y, u->z) != i + 1)
            return;
        r->kind = 1;
        r->pose = (unsigned char)(u->dir | (u->kneeling ? 8 : 0) | u->phase << 4);
        r->set = u->sprite_set;
        r->xt = u->xtype;
        r->tur = u->turret;
        r->misc = (unsigned char)(((u->burning & ~UB_HIT) ? 1 : 0)
                                  | (u->xtype == XT_HOVER ? g_anim << 1 : 0)
                                  | (u->xtype == XT_XCOM + ARM_FLYING && !u->phase
                                     && !tile_standable(u->x, u->y, u->z, &h) ? 0x10 : 0));
        r->lev = (signed char)tile_level(u->x, u->y, u->z);
    } else {
        int b;
        if ((u->special & SP_CARRIED) || (b = unit_body(u)) < 0)
            return;
        r->kind = 2;
        r->body = (unsigned char)b;
    }
    r->x = u->x;
    r->y = u->y;
    r->z = u->z;
    r->big = u->big;
}

static void tsh(TSh far *s, int x, int y, int z, int f)
{
    TSh n;

    n.x = (unsigned char)x;
    n.y = (unsigned char)y;
    n.z = (unsigned char)z;
    n.f = (unsigned char)f;
    if (_fmemcmp(s, &n, sizeof n)) {
        if (s->f != NO_ITEM)
            inval_tile(s->x, s->y, s->z, 0);
        if (f != NO_ITEM)
            inval_tile(x, y, z, 0);
        *s = n;
    }
}

/* Units, floor items, devices, effects: all of them (cheap). */
static void sync_all(void)
{
    USh r;
    TSh e;
    unsigned char at[3];
    int i, it, diff = 0;
    unsigned n = 0, z, k, m, j;

    if (g_fob != cvd->fob) {
        cvd->fob = g_fob;
        reset();
    }
    for (i = 0; i < MAX_UNITS; i++) {
        USh far *s = &cvd->us[i];
        if (i >= g_nunits && !s->kind)
            continue;               /* no unit, none drawn */
        urec(i, &r);
        if (_fmemcmp(s, &r, sizeof r)) {
            if (s->kind)
                inval_tile(s->x, s->y, s->z, s->big);
            if (r.kind)
                inval_tile(r.x, r.y, r.z, r.big);
            *s = r;
        }
    }
    for (i = 0; i < g_max_floor; i++) {
        const FloorItem *f = &g_floor[i];
        TSh far *s = &cvd->fl[i];
        /* (x, y, z, item: the shadow's four bytes) */
        if (f->item == NO_ITEM ? s->f == NO_ITEM
                               : *(const unsigned long *)f == *(const unsigned long far *)s)
            continue;
        if (f->item == NO_ITEM)
            tsh(s, 0, 0, 0, NO_ITEM);
        else
            tsh(s, f->x, f->y, f->z, f->item);
    }
    /* devices 0 .. their count - 1, as were */
    for (i = 0; i < FX_DEVS_TOP; i++) {
        it = fx_dev_pos(i, at);
        if (it == NO_ITEM && cvd->dv[i].f == NO_ITEM)
            break;
        if (it == NO_ITEM)
            tsh(&cvd->dv[i], 0, 0, 0, NO_ITEM);
        else
            tsh(&cvd->dv[i], at[0], at[1], at[2], it);
    }
    /* The effects' tiles in fx_level_tiles' order with their flags: any
       change, and the tiles from there on before and after are drawn. */
    for (z = 0; z < g_mz; z++) {
        m = g_fx_count ? fx_level_tiles(z, cvd->keys) : 0;
        for (k = 0; k < m && n < FX_MAX_TILES; k++, n++) {
            e.x = (unsigned char)(cvd->keys[k] >> 8);
            e.y = (unsigned char)cvd->keys[k];
            e.z = (unsigned char)z;
            e.f = (unsigned char)(g_los_flags[TIDX(e.x, e.y, z)] & LF_FX);
            if (!diff && (n >= cvd->nfx || _fmemcmp(&cvd->fx[n], &e, sizeof e))) {
                diff = 1;
                for (j = n; j < cvd->nfx; j++)
                    inval_tile(cvd->fx[j].x, cvd->fx[j].y, cvd->fx[j].z, 0);
            }
            if (diff) {
                cvd->fx[n] = e;
                inval_tile(e.x, e.y, z, 0);
            }
        }
    }
    if (!diff)
        for (j = n; j < cvd->nfx; j++)
            inval_tile(cvd->fx[j].x, cvd->fx[j].y, cvd->fx[j].z, 0);
    cvd->nfx = n;
}

/* The fog bits of the tiles whose sprites reach R, levels 0..t. */
static void sync_seen(int t, const int *R)
{
    unsigned char far *sp = g_seen;
    int z, v, xa, xb, ua = ((R[0] - 32) >> 4) + 1, ub = (R[2] - 1) >> 4;
    unsigned idx, n, k;
    int step = 1 - (int)g_mx;

    if (sp != cvd->seenp) {
        /* (cvinit.c fills the bits at the start; here, a bit off counts
           as not known: SEEN_BIT is only ever set) */
        cvd->seenp = sp;
        _fmemset(cvd->seen, 0, (MAP_MAX_TILES + 7) / 8);
        reset();
        return;
    }
    if (!sp)
        return;
    for (z = 0; z <= t; z++) {
        int va = ((R[1] - 40 + 24 * z) >> 3) + 1, vb = (R[3] + 23 + 24 * z) >> 3;
        if (va < 0)
            va = 0;
        if (vb > (int)(g_mx + g_my) - 2)
            vb = (int)(g_mx + g_my) - 2;
        for (v = va; v <= vb; v++) {
            xa = (ua + v + 1) >> 1;
            xb = (ub + v) >> 1;
            if (xa < v - (int)g_my + 1)
                xa = v - (int)g_my + 1;
            if (xa < 0)
                xa = 0;
            if (xb > v)
                xb = v;
            if (xb > (int)g_mx - 1)
                xb = (int)g_mx - 1;
            if (xa > xb)
                continue;
            idx = ((unsigned)z * g_my + (unsigned)(v - xa)) * g_mx + (unsigned)xa;
            n = (unsigned)(xb - xa + 1);
            while (n) {
                k = cv_seen(sp, idx, step, n, cvd->seen);
                if (k == n)
                    break;
                idx += k * step;
                xa += k;
                cvd->seen[idx >> 3] ^= (unsigned char)(1 << (idx & 7));
                inval_tile(xa, v - xa, z, 0);
                idx += step;
                xa++;
                n -= k + 1;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* render.c's calls */

static int aligned(void)
{
    return !(g_cam_x & 15) && !(g_cam_y & 7);
}

int cv_op(int op, int *r)
{
    int R[4], C[4], o[6], t = top(), c, synced = cvd->synced, change = g_cv == 2;
    unsigned long t0;

    cvd->page = 0xFFFF;             /* EMS: map before use */
    cvd->synced = 0;                /* (a read's sync serves its store) */
    if (op == CV_STORE)
        g_cv = 1;                   /* (render_rect's mark of a change) */
    if (!aligned())
        return 1;
    R[0] = r[0] * 8 - g_cam_x;
    R[1] = r[1] - g_cam_y;
    R[2] = r[2] * 8 - g_cam_x;
    R[3] = r[3] - g_cam_y;
    c = cursor(t, C);
    if (op == CV_STORE && change) {
        /* A change shown (render_rect: a walk step, a door, a shot) is not
           kept: the walker's next step makes it stale at once (+25-60 ms a
           step). Its place is forgotten instead; cv_idle draws it again
           while the turn waits. */
        cvd->gen++;
        rect(t, R, R_INVAL, NULL, NULL);
        return 0;
    }
    if (cvd->vga && cvd->mode == MODE_XMS
        && (long)(R[2] - R[0]) * (R[3] - R[1]) < 320L * VIEW_H / 4)
        return 1;   /* VGA on XMS: a strip (a scroll's, the cursor's) is drawn and not kept:
                       each block is an XMS move of about 2 ms on the 286, and VGA shows the
                       whole view after a scroll anyway (docs/design.md) */
    if (op == CV_STORE) {
        /* the shadows as drawn (a picture is kept only with them right) */
        if (!synced) {
            t0 = timer_read();
            sync_all();
            cvd->t_sync += timer_read() - t0;
        }
        cvd->n_store++;
        rect(t, R, R_PUT, c ? C : NULL, NULL);
        return 0;
    }
    /* CV_READ */
    cvd->n_read++;
    o[0] = o[1] = 0;
    o[2] = o[3] = 0x7FFF;
    o[4] = o[5] = -0x7FFF;
    rect(t, R, R_TEST, c ? C : NULL, o);
    if (o[1] == o[0]) {             /* nothing kept: no need to look further */
        cvd->n_miss++;
        return 1;
    }
    t0 = timer_read();
    sync_all();
    sync_seen(t, R);
    cvd->t_sync += timer_read() - t0;
    cvd->synced = 1;                /* until the store, if one follows */
    if (cvd->check)
        return 1;                   /* drawn, then compared (R_PUT) */
    o[0] = o[1] = 0;
    o[2] = o[3] = 0x7FFF;
    o[4] = o[5] = -0x7FFF;
    rect(t, R, R_TEST, c ? C : NULL, o);
    if (o[1] == o[0] || (long)(o[4] - o[2]) * (o[5] - o[3]) * 10
                        > (long)(R[2] - R[0]) * (R[3] - R[1]) * 6) {
        cvd->n_miss++;
        return 1;
    }
    t0 = timer_read();
    rect(t, R, R_GET, NULL, NULL);
    cvd->t_get += timer_read() - t0;
    if (!o[1]) {
        cvd->synced = 0;
        cvd->n_hit++;
        return 0;
    }
    /* the part still to draw */
    cvd->n_part++;
    if (o[2] > R[0]) r[0] = (o[2] + g_cam_x) >> 3;
    if (o[3] > R[1]) r[1] = o[3] + g_cam_y;
    if (o[4] < R[2]) r[2] = (o[4] + g_cam_x) >> 3;
    if (o[5] < R[3]) r[3] = o[5] + g_cam_y;
    return 1;
}

void cv_tile(int x, int y, int z)
{
    inval_tile(x, y, z, 0);
}

/* ------------------------------------------------------------------ */
/* Drawing ahead while the player's turn waits */

void cv_idle(void)
{
    int t = top(), ring, bi, bj, vi0, vi1, vj0, vj1, X, Y, i;
    int cx = g_cam_x, cy = g_cam_y, con = g_cursor_on, csok = g_cs_ok;
    unsigned s;
    unsigned char far *b;
    int R[4];

    if (!aligned() || input_ready())
        return;
    if (cvd->idle_gen == cvd->gen && cvd->idle_x == cx && cvd->idle_y == cy && cvd->idle_t == t)
        return;                     /* all drawn around this view */
    vi0 = (-cx + OFFX) >> BLK_SH;
    vi1 = (-cx + 319 + OFFX) >> BLK_SH;
    vj0 = (-cy + OFFY) / BLK_H;
    vj1 = (-cy + VIEW_H - 1 + OFFY) / BLK_H;
    /* rings of a block across, 3 down (128 x 72 pixels) */
    for (ring = 0; ring <= FILL_R; ring++)
        for (bj = vj0 - 3 * ring; bj <= vj1 + 3 * ring; bj++)
            for (bi = vi0 - ring; bi <= vi1 + ring; bi++) {
                if (ring && bj > vj0 - 3 * ring + 2 && bj < vj1 + 3 * ring - 2
                    && bi != vi0 - ring && bi != vi1 + ring)
                    continue;       /* inside: an earlier ring */
                X = (bi << BLK_SH) - OFFX;
                Y = bj * BLK_H - OFFY;
                if (outside(X, Y))
                    continue;
                s = find(KEY(t, bi, bj));
                if (s != 0xFFFF && cells(s, 0, 8, 0, 3, 2) == 24)
                    continue;
                goto draw;
            }
    cvd->idle_gen = cvd->gen;
    cvd->idle_x = cx;
    cvd->idle_y = cy;
    cvd->idle_t = t;
    return;
draw:
    /* The back buffer's top rows lend their room: kept, drawn there with
       the camera moved, kept as the block, put back. */
    cvd->page = 0xFFFF;
    if (cvd->mode == MODE_EMS) {
        cv_map(cvd->handle, cvd->save_pg);
        cvd->page = cvd->save_pg;
        b = (unsigned char far *)MK_FP(cvd->frame, 0);
        cv_copy(g_bufseg, 0, b, cvd->vga ? BLK_W : BUF_W, cvd->bw, BLK_H, 1 | cvd->vga);
    } else
        for (i = 0; i < (cvd->vga ? 1 : 4); i++)
            xmove(1, (unsigned long)cvd->slots * cvd->bsz + i * (BUF_W * BLK_H),
                  MK_FP(g_bufseg, i * PLANE_BYTES), cvd->bw * BLK_H);
    g_cam_x = -X;
    g_cam_y = -Y;
    g_cursor_on = 0;
    g_cv = 0;
    render_redraw(0, 0, BLK_W, BLK_H);
    g_cv = 1;
    sync_all();
    R[0] = X;
    R[1] = Y;
    R[2] = X + BLK_PX;
    R[3] = Y + BLK_H;
    rect(t, R, R_PUT, NULL, NULL);
    cvd->n_fill++;
    g_cam_x = cx;
    g_cam_y = cy;
    g_cursor_on = con;
    g_cs_ok = csok;
    cvd->page = 0xFFFF;
    if (cvd->mode == MODE_EMS) {
        cv_map(cvd->handle, cvd->save_pg);
        cv_copy(g_bufseg, 0, b, cvd->vga ? BLK_W : BUF_W, cvd->bw, BLK_H, cvd->vga);
    } else
        for (i = 0; i < (cvd->vga ? 1 : 4); i++)
            xmove(0, (unsigned long)cvd->slots * cvd->bsz + i * (BUF_W * BLK_H),
                  MK_FP(g_bufseg, i * PLANE_BYTES), cvd->bw * BLK_H);
    cvd->page = 0xFFFF;
}

