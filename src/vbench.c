/* SPDX-License-Identifier: GPL-3.0-or-later */
/* BENCH (main.c): the renderer, path search, sight and voxel lines; an
   overlay section of its own (only BENCH loads it). */

#include <stdio.h>
#include <string.h>
#include "canvas.h"
#include "cvpriv.h"
#include "battle.h"
#include "effects.h"
#include "fx_in.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "render.h"
#include "timer.h"
#include "unit.h"
#include "vox.h"
#include <conio.h>
#include <dos.h>
#include <i86.h>

void ms_call(union REGS *r);           /* mouse.c */
void cursor_show(void);
extern int g_mouse_x, g_mouse_y;


/* The reference search in C (path.h): Dijkstra with cost buckets (Dial's
   algorithm), every tile within max_tu, as pathasm.asm without a goal. A
   tile is in one bucket list at most (FR_Q: one next[] each); one whose
   cost fell while it waited in a later bucket is expanded there with its
   new cost, its neighbours going into the current bucket at the earliest.
   (Before, a tile was linked into a second list, which cut the first one:
   the tiles after it were never expanded.) */
void path_search_c(struct PathParams *p)
{
    enum { TILES = MAP_MAX_TILES, COST_NONE = 0xFF, FR_Q = 0x40, WF_LIFT = 8, NONE = 0xFFFFu };
    unsigned char *wf = MK_FP(p->walkseg + TILES / 16, 0);
    unsigned char *cost = MK_FP(p->searchseg, 0), *from = cost + TILES;
    unsigned short *next = (unsigned short *)(cost + 2 * TILES);
    unsigned short *bucket = (unsigned short *)(cost + 4 * TILES);
    int c, max_tu = p->max_tu;

    _fmemset(cost, COST_NONE, p->n);
    _fmemset(bucket, 0xFF, 256 * sizeof *bucket);
    cost[p->start] = 0;
    from[p->start] = 0xFF;
    bucket[0] = p->start;
    next[p->start] = NONE;
    for (c = 0; c <= max_tu; c++) {
        while (bucket[c] != NONE) {
            unsigned idx = bucket[c], rem, g;
            int x, y, z, dir;

            bucket[c] = next[idx];
            from[idx] &= ~FR_Q;
            g = cost[idx];
            z = idx / p->mxy;
            rem = idx - z * p->mxy;
            y = rem / p->mx;
            x = rem - y * p->mx;
            for (dir = 0; dir < 10; dir++) {
                int nz, sc, nc, k;
                unsigned nidx;

                if (dir >= 8 && !(wf[idx] & WF_LIFT) && !p->fly)
                    break;
                sc = path_step_idx(idx, x, y, z, dir, &nz, &nidx);
                if (sc < 0)
                    continue;
                nc = g + sc;
                if (nc > max_tu || cost[nidx] <= (unsigned)nc || (wf[nidx] & p->occ))
                    continue;
                k = cost[nidx] != COST_NONE && (from[nidx] & FR_Q);
                cost[nidx] = (unsigned char)nc;
                from[nidx] = (unsigned char)(dir | z << 4 | FR_Q);
                if (k)
                    continue;       /* waiting in a list: it stays there */
                if (nc < c)
                    nc = c;
                next[nidx] = bucket[nc];
                bucket[nc] = nidx;
            }
        }
    }
    if (p->goal != NONE && cost[p->goal] != COST_NONE)
        p->best = p->goal;
}

/* SHOTLOG: the map and its terrain records for tools/shotcheck.py (here,
   not in bplayer's section: the player's turn has no room). */
void dump_map(void)
{
    FILE *f = fopen("MAPDUMP.BIN", "wb");

    if (!f)
        return;
    fwrite("XMD1", 1, 4, f);
    fputc(g_mx, f);
    fputc(g_my, f);
    fputc(g_mz, f);
    fputc(g_pak.n_mcd & 0xFF, f);
    fputc(g_pak.n_mcd >> 8, f);
    fwrite(g_pak.mcd, sizeof(Mcd), g_pak.n_mcd, f);
    fwrite(g_map, 4, g_mx * g_my * g_mz, f);
    fclose(f);
}

/* Which aliens X-COM can see (Unit.seen); returns how many. */
static int spot_aliens(void)
{
    int a, s, count = 0;

    for (a = 0; a < g_nunits; a++) {
        Unit *al = &g_units[a];
        if (al->faction != FACTION_ALIEN || !al->alive)
            continue;
        al->seen = 0;
        for (s = 0; s < g_nunits; s++)
            if (g_units[s].faction == FACTION_XCOM && los_unit_sees(s, a)) {
                al->seen = 1;
                count++;
                break;
            }
    }
    return count;
}

/* SHOTLOG: every soldier's fog fan (los_reveal) in all 8 facings, as check
   sums of the tiles it marks, the box around them (losasm.asm) and its
   result: the same from any two programs whose fans mark the same tiles.
   The fog itself waits in WF_OCC's bit (path.c: free outside a search). */
void __cdecl reveal_get_box(unsigned char *box);

static void fan_bench(FILE *f, unsigned char *seen)
{
    unsigned n = g_mx * g_my * g_mz, i, fans = 0;
    unsigned long s1 = 0, s2 = 0;
    unsigned char box[6], dir;
    int u, d;

    for (i = 0; i < n; i++)
        seen[i] = (unsigned char)((seen[i] & ~(SEEN_BIT | 0x40)) | ((seen[i] & SEEN_BIT) << 2));
    g_seen = seen;
    for (u = 0; u < g_nunits; u++) {
        if (g_units[u].faction != FACTION_XCOM || !g_units[u].alive)
            continue;
        dir = g_units[u].dir;
        for (d = 0; d < 8; d++, fans++) {
            g_units[u].dir = (unsigned char)d;
            los_box_reset();
            s1 += los_reveal(u);
            reveal_get_box(box);
            for (i = 0; i < 6; i++)
                s2 += s1 += box[i];
            for (i = 0; i < n; i++)
                if (seen[i] & SEEN_BIT) {
                    seen[i] &= ~SEEN_BIT;
                    s2 += s1 += i;
                }
        }
        g_units[u].dir = dir;
    }
    los_box_reset();
    for (i = 0; i < n; i++)
        seen[i] = (unsigned char)((seen[i] & ~(SEEN_BIT | 0x40)) | ((seen[i] & 0x40) >> 2));
    g_seen = NULL;
    fprintf(f, "fog fans: %u, check sums %08lx %08lx\n", fans, s1, s2);
}

static void vox_bench(FILE *f, int u)
{
    const Unit *su = &g_units[u];
    int o[3], t[3], dx, dy, e, s, a, n, blocked;
    unsigned long tl;

    /* From the soldier's muzzle to the middle of every other tile within 20
       on its level (sight), then on past it (shots), terrain only; the
       lines go to BENCH.TXT for tools/voxcheck.py (with MAPDUMP.BIN). */
    vox_origin(u, su->x, su->y - 1, 0, o);
    g_vox_excl = VOX_NO_UNITS;
    for (e = 0; e < 2; e++) {
        blocked = n = 0;
        tl = timer_read();
        for (dy = -20; dy <= 20; dy += 2)
            for (dx = -20; dx <= 20; dx += 2)
                if (IN_MAP(su->x + dx, su->y + dy, su->z)) {
                    t[0] = (su->x + dx) * 16 + 8;
                    t[1] = (su->y + dy) * 16 + 8;
                    t[2] = su->z * 24 + 12;
                    blocked += vox_line(o, t, e) != V_EMPTY;
                    n++;
                }
        fprintf(f, "%d voxel lines%s: %lu ms, %d stopped\n", n, e ? " (on past the tile)" : "",
                timer_ms(timer_read() - tl), blocked);
    }
    {
        unsigned r0 = rd_count();
        tl = timer_read();
        for (n = 0; n < 336; n++)
            vox_line(o, o, 0);
        fprintf(f, "336 voxel lines of one voxel: %lu ms, %u reads\n", timer_ms(timer_read() - tl),
                rd_count() - r0);
        r0 = rd_count();
        tl = timer_read();
        for (n = 0; n < 336; n++)
            vox_level(1, 1, 0);
        fprintf(f, "336 vox_level: %lu ms, %u reads\n", timer_ms(timer_read() - tl), rd_count() - r0);
    }
    for (e = 0; e < 2; e++)
        for (dy = -20; dy <= 20; dy += 2)
            for (dx = -20; dx <= 20; dx += 2)
                if (IN_MAP(su->x + dx, su->y + dy, su->z)) {
                    int r;
                    t[0] = (su->x + dx) * 16 + 8;
                    t[1] = (su->y + dy) * 16 + 8;
                    t[2] = su->z * 24 + 12;
                    r = vox_line(o, t, e);
                    fprintf(f, "VL %d,%d,%d %d,%d,%d %d => %d at %d,%d,%d\n", o[0], o[1], o[2],
                            t[0], t[1], t[2], e, r, g_vox_hit[0], g_vox_hit[1], g_vox_hit[2]);
                }
    /* Every soldier's aim at every alien (canTargetUnit from the muzzle). */
    n = blocked = 0;
    tl = timer_read();
    for (s = 0; s < g_nunits; s++)
        for (a = 0; a < g_nunits; a++)
            if (g_units[s].alive && g_units[a].alive && g_units[s].faction == FACTION_XCOM
                && g_units[a].faction != FACTION_XCOM) {
                vox_origin(s, g_units[a].x, g_units[a].y, 0, o);
                g_vox_excl = s;
                n++;
                blocked += !vox_target_unit(o, a, t);
            }
    fprintf(f, "%d aims (canTargetUnit): %lu ms, %d without a line of fire\n", n,
            timer_ms(timer_read() - tl), blocked);
    dump_map();
}

static unsigned long last_ms;


static void timed_full(void)
{
    unsigned long t0 = timer_read();
    render_full();
    last_ms = timer_ms(timer_read() - t0);
}
/* Checks an incremental redraw (scroll, cursor): the screen must show the
   back buffer, and a full redraw must give the same buffer. */
static unsigned long buf_sum(void)
{
    unsigned n = g_video == VIDEO_EGA ? PLANE_BYTES * 4 : g_vga ? VGA_BYTES : BUFC_W * BUF_ROWS, i;
    const unsigned char far *b = (const unsigned char far *)MK_FP(g_bufseg, 0);
    unsigned long s = 0;

    for (i = 0; i < n; i++)
        s = (s << 1 | s >> 31) ^ b[i];
    return s;
}

static unsigned screen_diff(void)
{
    const unsigned char far *b = (const unsigned char far *)MK_FP(g_bufseg, 0);
    unsigned d = 0, i, r;
    int p;

    if (g_video == VIDEO_EGA) {
        const volatile unsigned char far *v = (const volatile unsigned char far *)MK_FP(0xA000, 0);
        for (p = 0; p < 4; p++) {
            outp(0x3CE, 4);
            outp(0x3CF, p);
            for (i = 0; i < PLANE_BYTES; i++) {
                unsigned char e = b[p * PLANE_BYTES + i];
                /* The night light (nlit.c): a dark cell's bytes. */
                if (g_lit && !(LITD->now[i / BUF_W >> 3] >> ((i % BUF_W) >> 1) & 1))
                    e = p == 3 ? 0 : e & (i / BUF_W & 1 ? 0x55 : 0xAA);
                d += v[i] != e;
            }
        }
        outp(0x3CE, 4);
        outp(0x3CF, 0);
    } else if (g_vga) {
        /* At night each 16 x 8 cell through its level (vgaasm.asm "Night
           lights": the shown levels, AUX; StandardShade). */
        const volatile unsigned char far *v = (const volatile unsigned char far *)MK_FP(0xA000, 0);
        const unsigned char far *aux = (const unsigned char far *)MK_FP(g_vga,
            VGA_AUX);
        int night = *(const int far *)MK_FP(g_vga, VGA_NIGHT) != 0;
        for (i = r = 0; r < VIEW_H; r++) {
            unsigned x;
            for (x = 0; x < 320; x++, i++) {
                unsigned e = b[i], l = aux[(r >> 3) * 20 + (x >> 4)] & 15;
                if (night && e)
                    e = (e & 15) + l > 15 ? e | 15 : e + l;
                d += v[i] != (unsigned char)e;
            }
        }
    } else
        for (r = 0; r < VIEW_H; r++) {
            const unsigned char far *v = (const unsigned char far *)MK_FP(0xB800,
                (r & 1) * 0x2000 + (r >> 1) * 80);
            for (i = 0; i < 80; i++)
                d += v[i] != b[r * BUFC_W + i];
        }
    return d;
}

static void check_redraw(FILE *f, const char *what)
{
    unsigned d = screen_diff();
    unsigned long s = buf_sum();

    render_full();
    fprintf(f, "check %s: %u screen bytes differ, full redraw %s\n", what, d,
            buf_sum() == s ? "same" : "DIFFERENT");
}

/* The canvas (canvas.c): with EMS or XMS, each tour view drawn, kept and
   copied back must be the drawing; scrolls back over kept ground and into
   ground drawn ahead; changes made behind the renderer's back (a unit
   turned, an item put down, tiles uncovered, smoke gone) must show. */
static unsigned long cv_same(void)
{
    unsigned long s = buf_sum();

    g_cv = 0;
    render_full();
    g_cv = 1;
    return buf_sum() == s;
}

static unsigned long cv_scrolls(int dx, int dy)
{
    unsigned long t0 = timer_read();
    int i;

    for (i = 0; i < 4; i++)
        render_scroll(g_cam_x + dx, g_cam_y + dy);
    return timer_ms(timer_read() - t0) / 4;
}

/* The XMS picture store (picplan.c): what a picture's fault costs in XMS
   moves, 700 bytes (a unit frame) and 2000 (a wall), into the back buffer
   (redrawn after). */
static void xs_bench(FILE *f)
{
    unsigned mv[8], x[5], i, k, len;
    unsigned long t0;

    if (!g_sc_xh || g_sc_top)
        return;
    for (k = 0; k < 2; k++) {
        len = k ? 2000 : 700;
        t0 = timer_read();
        for (i = 0; i < 100; i++) {
            mv[0] = len;
            mv[1] = 0;
            mv[2] = g_sc_xh;
            mv[3] = mv[4] = mv[5] = 0;
            mv[6] = 0;
            mv[7] = g_bufseg;
            x[0] = 0x0B00;
            x[3] = FP_OFF((unsigned far *)mv);
            x[4] = FP_SEG((unsigned far *)mv);
            xms_call((unsigned far *)x);
        }
        fprintf(f, "picture store: 100 XMS moves of %u bytes %lu ms%s\n", len,
                timer_ms(timer_read() - t0), x[0] == 1 ? "" : " (refused)");
    }
}

static void cv_bench(FILE *f, unsigned char *seen)
{
    char spots[12];             /* a copy, as in bench_run */
    unsigned long s0, s1, s2, td = 0, tc = 0, t0, ta, tb, ts, tg;
    int i, z, n = 0, bad = 0, u, ok[4];
    Unit *un;

    _fmemcpy(spots, "\24\24\5\5\43\5\5\43\43\43\24\24", 12);
    cv_battle(1);
    if (!g_cv) {
        fprintf(f, "canvas: off\n");
        return;
    }
    g_cursor_on = 1;
    g_cur_x = 21;
    g_cur_y = 20;
    g_cur_z = 0;
    for (z = 0; z < 4; z += 3) {
        g_view_z = z;
        for (i = 0; i < 6; i++) {
            render_center(spots[i * 2], spots[i * 2 + 1], 0);
            g_cv = 0;
            t0 = timer_read();
            render_full();
            ta = timer_read() - t0;
            td += ta;
            s0 = buf_sum();
            g_cv = 1;
            t0 = timer_read();
            render_full();                  /* drawn, kept */
            tb = timer_read() - t0;
            s1 = buf_sum();
            ts = cvd->t_sync;
            tg = cvd->t_get;
            t0 = timer_read();
            render_full();                  /* copied */
            t0 = timer_read() - t0;
            tc += t0;
            s2 = buf_sum();
            fprintf(f, "canvas: view %d,%d top %d: drawn %lu ms, drawn and kept %lu ms, "
                    "copied %lu ms (checks %lu ms, copies %lu ms)\n", spots[i * 2],
                    spots[i * 2 + 1], z, timer_ms(ta), timer_ms(tb), timer_ms(t0),
                    timer_ms(cvd->t_sync - ts), timer_ms(cvd->t_get - tg));
            bad += s1 != s0 || s2 != s0;
            n++;
        }
    }
    fprintf(f, "canvas: %d views: drawn %lu ms, from the canvas %lu ms; %d differ\n",
            n, timer_ms(td) / n, timer_ms(tc) / n, bad);
    /* Scrolls away (drawn, kept) and back (copied). */
    g_view_z = 0;
    render_center(20, 20, 0);
    render_full();
    ta = cv_scrolls(-32, 0);
    tb = cv_scrolls(32, 0);
    fprintf(f, "canvas: edge scroll across into new ground %lu ms, back %lu ms, %s\n",
            ta, tb, cv_same() ? "same" : "DIFFERENT");
    ta = cv_scrolls(0, -16);
    tb = cv_scrolls(0, 16);
    fprintf(f, "canvas: edge scroll down into new ground %lu ms, back %lu ms, %s\n",
            ta, tb, cv_same() ? "same" : "DIFFERENT");
    ta = cv_scrolls(-16, -8);
    tb = cv_scrolls(16, 8);
    fprintf(f, "canvas: one-tile scroll into new ground %lu ms, back %lu ms, %s\n",
            ta, tb, cv_same() ? "same" : "DIFFERENT");
    /* Level changes at one place: 0-3 and back to 0 (kept now). */
    g_view_z = 3;
    render_full();
    g_view_z = 0;
    t0 = timer_read();
    render_full();
    ta = timer_ms(timer_read() - t0);
    g_view_z = 3;
    t0 = timer_read();
    render_full();
    tb = timer_ms(timer_read() - t0);
    fprintf(f, "canvas: levels 0-3 to 0 and back, kept: %lu / %lu ms, %s\n", ta, tb,
            cv_same() ? "same" : "DIFFERENT");
    g_view_z = 0;
    /* Drawn ahead while waiting, then edge scrolls into that ground. */
    render_center(10, 30, 0);
    render_full();
    t0 = timer_read();
    for (i = 0; i < 300; i++)
        cv_idle();
    ta = timer_ms(timer_read() - t0);
    tb = cv_scrolls(32, 0);
    fprintf(f, "canvas: drawn ahead in %lu ms; edge scroll across into it %lu ms, %s\n",
            ta, tb, cv_same() ? "same" : "DIFFERENT");
    tb = cv_scrolls(0, 16);
    fprintf(f, "canvas: edge scroll down into it %lu ms, %s\n", tb, cv_same() ? "same" : "DIFFERENT");
    /* Changes the renderer does not announce. */
    for (u = 0; u < g_nunits && !(g_units[u].alive && g_units[u].faction == FACTION_XCOM); u++)
        ;
    un = &g_units[u];
    render_center(un->x, un->y, un->z);
    g_view_z = un->z;
    render_full();
    un->dir = (unsigned char)((un->dir + 2) & 7);
    render_full();
    ok[0] = (int)cv_same();
    un->dir = (unsigned char)((un->dir + 6) & 7);
    z = floor_add(un->x + 1, un->y, un->z, IT_FLARE, 1);
    render_full();
    ok[1] = (int)cv_same();
    if (z >= 0)
        g_floor[z].item = NO_ITEM;
    g_seen = seen;
    render_full();
    {
        int dx, dy;
        for (dy = -3; dy <= 3; dy++)
            for (dx = -3; dx <= 3; dx++)
                if (IN_MAP(un->x + dx, un->y + dy, un->z))
                    seen[TIDX(un->x + dx, un->y + dy, un->z)] |= SEEN_BIT;
    }
    render_full();
    ok[2] = (int)cv_same();
    g_seen = NULL;
    render_center(20, 20, 0);
    g_view_z = 0;
    render_full();
    fx_reset();                         /* fx_bench's smoke and fire gone */
    render_full();
    ok[3] = (int)cv_same();
    fprintf(f, "canvas: unseen changes: unit turned %s, item put down %s, tiles uncovered %s, "
            "effects gone %s\n", ok[0] ? "same" : "DIFFERENT", ok[1] ? "same" : "DIFFERENT",
            ok[2] ? "same" : "DIFFERENT", ok[3] ? "same" : "DIFFERENT");
    cv_battle(0);
}

/* The night light (nlit.c): the same moves with it and without, canvas off
   and on, the screen compared with the back buffer through the cells. */
static unsigned long lit_time(void far *L, int what)
{
    unsigned long t0;
    int i;

    g_lit = L;
    if (what < 3)
        render_center("\24\5\43"[what], "\24\5\43"[what], 0);
    else if (what == 8)
        render_center(g_units[0].x, g_units[0].y, g_units[0].z);
    else
        render_center(20, 20, 0);
    render_full();
    t0 = timer_read();
    switch (what) {
    case 0: case 1: case 2:
        for (i = 0; i < 4; i++)
            render_full();
        break;
    case 3:
        for (i = 0; i < 4; i++)
            gfx_present_cells(0, 0, 40, VIEW_H);
        break;
    case 4:
        for (i = 0; i < 4; i++) {
            int ox = g_cur_x;
            g_cur_x = 20 + (i & 1);
            render_cursor_move(ox, 20, 0);
        }
        break;
    case 5:
        for (i = 0; i < 4; i++)
            render_scroll(g_cam_x + ((i & 2) ? -32 : 32), g_cam_y);
        break;
    case 6:
        for (i = 0; i < 4; i++)
            render_scroll(g_cam_x, g_cam_y + ((i & 2) ? -16 : 16));
        break;
    case 7:
        for (i = 0; i < 4; i++)
            render_scroll(g_cam_x + ((i & 1) ? -16 : 16), g_cam_y + ((i & 1) ? -8 : 8));
        break;
    case 8:                     /* a soldier's steps: the cells that changed too */
        for (i = 0; i < 4; i++) {
            Unit *u = &g_units[0];
            int ox = u->x;
            u->x += i & 1 ? -1 : 1;
            render_step(0, ox, u->y, u->z, u->x, u->y, u->z);
        }
        break;
    }
    t0 = timer_ms(timer_read() - t0) / 4;
    return t0;
}

/* Each tile X-COM could see a unit on (fx_lit) has its diamond's cells lit:
   how many not. */
static int lit_check(void)
{
    int x, y, a, b, bad = 0;

    for (y = 0; y < (int)g_my; y++)
        for (x = 0; x < (int)g_mx; x++)
            if (fx_lit(x, y, 0)) {
                int u = TILE_SX(x, y) >> 4, v = (TILE_SY(x, y, 0) >> 3) + 3;
                for (a = 0; a < 2; a++)
                    for (b = 0; b < 2; b++)
                        if (u + a >= 0 && u + a < LIT_COLS && v + b >= 0 && v + b < LIT_BANDS
                            && !(LITD->now[v + b] >> (u + a) & 1))
                            bad++;
            }
    return bad;
}

static void lit_bench(FILE *f)
{
    static const char *const what[] = { "full redraw 20,20", "full redraw 5,5", "full redraw 35,35", "present view",
                                        "cursor step", "edge scroll across", "edge scroll down",
                                        "one-tile scroll", "soldier's step" };
    int night = g_night, c, i, bad, cv;
    unsigned n;
    unsigned long diff, moved = 0;
    void far *L;

    g_night = 1;
    cv_battle(1);
    g_night = night;
    L = g_lit;
    if (!L) {
        fprintf(f, "night light: off\n");
        cv_battle(0);
        return;
    }
    g_cursor_on = 1;
    g_cur_x = 21;
    g_cur_y = 20;
    g_cur_z = 0;
    g_view_z = 0;
    cv = g_cv;
    for (c = 0; c < 2; c++) {
        if (c && !cv)
            break;
        g_cv = c ? cv : 0;
        for (i = 0; i < 9; i++) {
            unsigned long a = lit_time(NULL, i), b;
            b = lit_time(L, i);
            fprintf(f, "night light%s: %s %lu ms, with it %lu ms; %u screen bytes differ\n",
                    c ? ", canvas" : "", what[i], a, b, screen_diff());
        }
    }
    g_cv = 0;
    /* What the rules light, with a flare, a fire and a soldier gone. */
    g_lit = L;
    render_center(20, 20, 0);
    render_full();
    bad = lit_check();
    diff = screen_diff();
    render_center(5, 5, 0);
    render_full();
    dev[n_dev].x = dev[n_dev].y = 5;
    dev[n_dev].z = 0;
    dev[n_dev].kind = DEV_FLARE;
    n_dev++;
    render_tile(5, 5, 0);
    bad += lit_check();
    diff += screen_diff();
    fprintf(f, "night light: a flare at 5,5: cell 8,11 %s\n", LITD->now[11] >> 8 & 1 ? "lit" : "DARK");
    bad += !(LITD->now[11] >> 8 & 1);
    n_dev--;
    fire[n_fire].x = fire[n_fire].y = 5;
    fire[n_fire].z = 0;
    n_fire++;
    render_tile(5, 5, 0);
    bad += lit_check();
    diff += screen_diff();
    n_fire--;
    render_tile(5, 5, 0);
    bad += lit_check();
    diff += screen_diff();
    { Unit *u = &g_units[0];
      int a = u->alive;
      render_center(u->x, u->y, u->z);
      render_full();
      u->alive = 0;
      render_tile(u->x, u->y, u->z);
      bad += lit_check();
      diff += screen_diff();
      u->alive = a;
    }
    render_center(20, 20, 0);
    render_full();
    bad += lit_check();
    diff += screen_diff();
    fprintf(f, "night light: %d cells of lit tiles dark, %lu screen bytes differ\n", bad, diff);
    log_printf("night light check: %d cells of lit tiles dark, %lu screen bytes differ", bad, diff + moved);
    {
        unsigned long t0 = timer_read();
        unsigned long r[LIT_BANDS];
        _fmemset(r, 0, sizeof r);
        for (i = 0; i < 100; i++)
            lit_disc(r, 10, 9, 0);
        fprintf(f, "night light: 100 discs %lu ms\n", timer_ms(timer_read() - t0));
    }
    cv_battle(0);
}

/* Timings for BENCH.TXT. */
static void mouse_bench(FILE *f)
{
    unsigned long t0;
    union REGS r;
    int i;

    if (!g_mouse) {
        fprintf(f, "mouse: no driver\n");
        return;
    }
    mouse_hide();
    t0 = timer_read();
    for (i = 0; i < 100; i++) {
        r.x.ax = 3;
        ms_call(&r);
    }
    fprintf(f, "mouse: 100 position reads (INT 33h fn 3) %lu ms\n", timer_ms(timer_read() - t0));
    t0 = timer_read();
    for (i = 0; i < 100; i++) {
        g_mouse_x = 100 + (i & 7);
        g_mouse_y = 60;
        cursor_show();
        mouse_hide();
    }
    fprintf(f, "mouse: 100 software pointer draw+erase %lu ms\n", timer_ms(timer_read() - t0));
    t0 = timer_read();
    for (i = 0; i < 100; i++) {
        r.x.ax = 1;                     /* the driver's pointer, for comparison */
        ms_call(&r);
        r.x.ax = 2;
        ms_call(&r);
    }
    fprintf(f, "mouse: 100 driver pointer show+hide (fn 1/2) %lu ms\n",
            timer_ms(timer_read() - t0));
    /* The driver may have changed EGA registers: back to our defaults. */
    if (g_video == VIDEO_EGA) {
        outp(0x3C4, 2);
        outp(0x3C5, 0x0F);
        outp(0x3CE, 4);
        outp(0x3CF, 0);
        outp(0x3CE, 5);
        outp(0x3CF, 0);
        outp(0x3CE, 8);
        outp(0x3CF, 0xFF);
        outp(0x3CE, 3);
        outp(0x3CF, 0);
        outp(0x3CE, 1);
        outp(0x3CF, 0);
    }
}

/* VGA prototype (gfx.h g_vga): every loaded sprite drawn on a cleared
   area, whole and clipped, must set exactly the pixels its data holds
   (tools/vgasprite.py formats; tools/vgacheck.py checks the data against
   the original pictures and the EGA pack). Expected: walks the format;
   only columns [cx0, cx1) and rows [cy0, cy1) of the box count. */
static unsigned vga_expect(const unsigned char far *s, int or_fmt, int cx0, int cx1,
                           int cy0, int cy1)
{
    unsigned n = 0, k, i;
    int r, x, len, a, b;

    for (r = s[0], k = s[1], s += 2; k; k--, r++) {
        unsigned spans = or_fmt ? 1 : *s++;
        for (; spans; spans--) {
            x = s[0] & 0x7F;
            len = or_fmt ? s[1] * 2 : s[1];
            s += 2 + (or_fmt ? 0 : s[0] >> 7);
            if (r >= cy0 && r < cy1) {
                a = x > cx0 ? x : cx0;
                b = x + len < cx1 ? x + len : cx1;
                for (i = 0; (int)i < b - a; i++)
                    n += s[a - x + i] != 0;
            }
            s += len;
        }
    }
    return n;
}

static void vga_sprite_check(FILE *f)
{
    const unsigned char far *b = (const unsigned char far *)MK_FP(g_bufseg, 0);
    int keep[4], pass, x, y;
    unsigned i, r, n = 0, bad = 0, got, want, first_bad = 0xFFFF;

    keep[0] = g_clip_x0;
    keep[1] = g_clip_y0;
    keep[2] = g_clip_x1;
    keep[3] = g_clip_y1;
    for (i = 0; i < g_pak.n_sprites; i++) {
        int or_fmt = 0;
        if (!g_pak.sprite[i])
            continue;               /* not on this map: not loaded */
        for (r = 1; r < g_pak.n_mcd; r++)
            if (MCD_PART(&g_pak.mcd[r]) == PART_FLOOR && OR_SPRITE(&g_pak.mcd[r]) == i)
                or_fmt = 1;
        for (pass = 0; pass < 2; pass++) {
            /* the box at 96,40 in a cleared area 80..144 x 32..88; pass 1
               clipped to x 112..128, rows 50..70 */
            g_clip_x0 = 0;
            g_clip_y0 = 0;
            g_clip_x1 = 320;
            g_clip_y1 = VIEW_H;
            gfx_clear_cells(10, 32, 8, 56);
            if (pass) {
                g_clip_x0 = 112;
                g_clip_y0 = 50;
                g_clip_x1 = 128;
                g_clip_y1 = 70;
            }
            if (or_fmt)
                gfx_sprite_or(i, 96, 40);
            else
                gfx_sprite(i, 96, 40);
            for (got = 0, y = 32; y < 88; y++)
                for (x = 80; x < 144; x++)
                    got += b[y * 320 + x] != 0;
            want = pass ? vga_expect(g_pak.sprite[i], or_fmt, 16, 32, 10, 30)
                        : vga_expect(g_pak.sprite[i], or_fmt, 0, 32, 0, 40);
            n++;
            if (got != want) {
                bad++;
                if (first_bad == 0xFFFF)
                    first_bad = i;
            }
        }
    }
    g_clip_x0 = keep[0];
    g_clip_y0 = keep[1];
    g_clip_x1 = keep[2];
    g_clip_y1 = keep[3];
    fprintf(f, "VGA sprite check: %u draws, %u differ", n, bad);
    if (bad)
        fprintf(f, " (first sprite %u)", first_bad);
    fprintf(f, "\n");
    render_full();
}

/* Renders a fixed tour of camera positions and logs the timings (the
   camera spots as a string: a -zc section's named table would be reached
   through an overlay vector). */
void bench_run(unsigned char *seen)
{
    char spots[12];             /* a copy: the section can move during the tour (calls into the core) */
    unsigned long total = 0, total_all = 0, t0;
    unsigned occ_views = 0, occ_diff = 0;
    FILE *f;
    int i, z, n = 0;

    _fmemcpy(spots, "\24\24\5\5\43\5\5\43\43\43\24\24", 12);
    f = fopen("BENCH.TXT", "w");
    if (g_vga)                  /* NIGHT: the views through the night's shade */
        gfx_night(g_night);
    for (z = 0; z < 4; z += 3) {
        g_view_z = z;
        for (i = 0; i < 6; i++) {
            render_center(spots[i * 2], spots[i * 2 + 1], 0);
            timed_full();
            total += last_ms;
            n++;
            if (f) {
                /* Covered sprites left out: the same picture as all drawn. */
                unsigned long s0 = buf_sum(), ms = last_ms;
                unsigned left;
                g_occ_count = 0;
                render_full();
                left = g_occ_count;
                g_occ_off = 1;
                timed_full();
                g_occ_off = 0;
                fprintf(f, "view %2d,%2d levels 0-%d: full redraw %lu ms, picture %08lx "
                        "(%u left out; all drawn %lu ms, %s)\n",
                        spots[i * 2], spots[i * 2 + 1], z, ms, s0, left, last_ms,
                        buf_sum() == s0 ? "same" : "DIFFERENT");
                occ_views++;
                occ_diff += buf_sum() != s0;
                total_all += last_ms;
            }
        }
    }
    g_view_z = 0;
    g_cur_x = 21;
    g_cur_y = 20;
    g_cur_z = 0;
    g_cursor_on = 1;
    render_center(20, 20, 0);
    render_full();
    t0 = timer_read();
    for (i = 0; i < 10; i++) {
        int ox = g_cur_x;
        g_cur_x = 20 + (i & 1);
        render_cursor_move(ox, 20, 0);
    }
    t0 = timer_ms(timer_read() - t0) / 10;
    if (f) {
        fprintf(f, "cursor step %lu ms\n", t0);
        check_redraw(f, "cursor");
    }
    /* A unit's step: its two tiles' areas, apart and as one rectangle
       (render_step). */
    t0 = timer_read();
    for (i = 0; i < 10; i++) {
        render_tile(20, 20, 0);
        render_tile(21, 20, 0);
    }
    t0 = timer_ms(timer_read() - t0) / 10;
    if (f)
        fprintf(f, "unit step, 2 tiles apart %lu ms\n", t0);
    t0 = timer_read();
    for (i = 0; i < 10; i++)
        render_step(0, 20, 20, 0, 21, 20, 0);
    t0 = timer_ms(timer_read() - t0) / 10;
    if (f)
        fprintf(f, "unit step, one rectangle %lu ms\n", t0);
    /* The selected unit's arrow (render.h) over a soldier standing at
       20,20,0: the cursor's steps over it, a unit's step under it, its
       move when the soldier kneels, a full redraw; the scrolls below keep
       it on (their checks then cover it). */
    g_arrow[0] = 1;
    g_arrow[1] = 12;
    g_arrow[2] = 40 * 8 + 24 - 9 - 22;
    g_arrow[3] = 0;
    render_full();
    t0 = timer_read();
    for (i = 0; i < 10; i++) {
        int ox = g_cur_x;
        g_cur_x = 20 + (i & 1);
        render_cursor_move(ox, 20, 0);
    }
    t0 = timer_ms(timer_read() - t0) / 10;
    if (f) {
        fprintf(f, "arrow: cursor step over it %lu ms\n", t0);
        check_redraw(f, "arrow, cursor");
    }
    t0 = timer_read();
    for (i = 0; i < 10; i++)
        render_step(0, 20, 20, 0, 21, 20, 0);
    t0 = timer_ms(timer_read() - t0) / 10;
    if (f)
        fprintf(f, "arrow: unit step under it %lu ms\n", t0);
    t0 = timer_read();
    for (i = 0; i < 10; i++) {
        /* kneeling: 8 down, 2 up (bplayer.c arrow_to: both places) */
        int x = g_arrow[1] + g_cam_x, y = g_arrow[2] + g_cam_y;
        g_arrow[2] += i & 1 ? -6 : 6;
        render_rect(x, y, x + 9, y + 9);
        y = g_arrow[2] + g_cam_y;
        render_rect(x, y, x + 9, y + 9);
    }
    t0 = timer_ms(timer_read() - t0) / 10;
    if (f) {
        fprintf(f, "arrow: moved (kneeling) %lu ms\n", t0);
        check_redraw(f, "arrow, moved");
    }
    timed_full();
    if (f)
        fprintf(f, "arrow: full redraw 20,20 level 0 %lu ms\n", last_ms);
    {
        unsigned long ts;
        render_center(20, 20, 0);
        render_full();
        ts = timer_read();
        for (i = 0; i < 4; i++)
            render_scroll(g_cam_x + 16, g_cam_y + 8);
        ts = timer_ms(timer_read() - ts) / 4;
        if (f) {
            fprintf(f, "scroll by one tile: %lu ms\n", ts);
            check_redraw(f, "scroll");
        }
        ts = timer_read();
        for (i = 0; i < 4; i++)
            render_scroll(g_cam_x + ((i & 1) ? -96 : 96), g_cam_y);
        ts = timer_ms(timer_read() - ts) / 4;
        if (f) {
            fprintf(f, "scroll by 3 tiles sideways: %lu ms\n", ts);
            check_redraw(f, "scroll 3");
        }
        /* The game's edge scrolling: 32 pixels across, 16 down. */
        ts = timer_read();
        for (i = 0; i < 4; i++)
            render_scroll(g_cam_x + ((i & 2) ? -32 : 32), g_cam_y);
        ts = timer_ms(timer_read() - ts) / 4;
        if (f) {
            fprintf(f, "edge scroll across (32 pixels): %lu ms\n", ts);
            check_redraw(f, "edge across");
        }
        ts = timer_read();
        for (i = 0; i < 4; i++)
            render_scroll(g_cam_x, g_cam_y + ((i & 2) ? -16 : 16));
        ts = timer_ms(timer_read() - ts) / 4;
        if (f) {
            fprintf(f, "edge scroll down (16 pixels): %lu ms\n", ts);
            check_redraw(f, "edge down");
        }
        render_center(20, 20, 0);
        render_full();
        ts = timer_read();
        for (i = 0; i < 10; i++)
            gfx_present_cells(0, 0, 40, VIEW_H);
        if (f)
            fprintf(f, "arrow: present view %lu ms\n", timer_ms(timer_read() - ts) / 10);
        g_arrow[0] = 0;
    }
    if (f) {
        extern unsigned g_blit_count, g_or_count;
        extern int g_no_blit;

        fprintf(f, "average full redraw %lu ms (all drawn %lu ms)\n", total / n, total_all / n);
        fprintf(f, "check covered sprites left out: %u views, %u differ\n", occ_views, occ_diff);
        g_blit_count = g_or_count = 0;
        render_center(20, 20, 0);
        timed_full();
        fprintf(f, "20,20 level 0: %lu ms, %u masked + %u OR sprites\n",
                last_ms, g_blit_count - g_or_count, g_or_count);
        g_no_blit = 1;
        timed_full();
        g_no_blit = 0;
        fprintf(f, "same without blits: %lu ms\n", last_ms);
        t0 = timer_read();
        for (i = 0; i < 10; i++)
            gfx_clear_cells(0, 0, 40, VIEW_H);
        fprintf(f, "clear view: %lu ms\n", timer_ms(timer_read() - t0) / 10);
        t0 = timer_read();
        for (i = 0; i < 10; i++)
            gfx_present_cells(0, 0, 40, VIEW_H);
        fprintf(f, "present view: %lu ms\n", timer_ms(timer_read() - t0) / 10);
        {
            /* 100 draws of one ground floor (OR and masked) and one tall object. */
            unsigned i, k, floor_m = 0, obj = 0;
            for (i = 1; i < g_pak.n_mcd; i++) {
                if (!g_pak.sprite[g_pak.mcd[i].sprite])
                    continue;           /* not on this map, not loaded */
                if (!floor_m && MCD_PART(&g_pak.mcd[i]) == PART_FLOOR
                    && OR_SPRITE(&g_pak.mcd[i]) != OR_NONE)
                    floor_m = i;
                if (!obj && MCD_PART(&g_pak.mcd[i]) == PART_OBJECT && i > 40)
                    obj = i;
            }
            t0 = timer_read();
            for (k = 0; k < 100; k++)
                gfx_sprite_or(OR_SPRITE(&g_pak.mcd[floor_m]), 96, 40);
            fprintf(f, "100 OR floors: %lu ms\n", timer_ms(timer_read() - t0));
            t0 = timer_read();
            for (k = 0; k < 100; k++)
                gfx_sprite(g_pak.mcd[floor_m].sprite, 96, 40);
            fprintf(f, "100 masked floors: %lu ms\n", timer_ms(timer_read() - t0));
            t0 = timer_read();
            for (k = 0; k < 100; k++)
                gfx_sprite(g_pak.mcd[obj].sprite, 96, 40);
            fprintf(f, "100 masked objects (mcd %u): %lu ms\n", obj, timer_ms(timer_read() - t0));
            t0 = timer_read();
            for (k = 0; k < 100; k++)
                gfx_sprite(g_pak.cursor_base, 96, 40);
            fprintf(f, "100 cursor sprites: %lu ms\n", timer_ms(timer_read() - t0));
        }
        {
            /* Game logic timings with the spawned squad. */
            unsigned long tl;
            int u = 0;
            for (u = 0; u < g_nunits; u += 3) {
                unsigned sum_c, sum_a;
                unsigned long tc, ta;
                g_path_asm = 0;
                tl = timer_read();
                path_search(u, 60);
                tc = timer_ms(timer_read() - tl);
                sum_c = path_checksum();
                g_path_asm = 1;
                tl = timer_read();
                path_search(u, 60);
                ta = timer_ms(timer_read() - tl);
                sum_a = path_checksum();
                fprintf(f, "path search unit %d (60 TU): C %lu ms, asm %lu ms, %s\n",
                        u, tc, ta, sum_c == sum_a ? "match" : "MISMATCH");
            }
            if (g_shot_log)
                fan_bench(f, seen);
            u = 0;
            g_seen = seen;
            tl = timer_read();
            los_reveal(u);
            fprintf(f, "vision update (1 soldier): %lu ms\n", timer_ms(timer_read() - tl));
            tl = timer_read();
            spot_aliens();
            fprintf(f, "spot aliens: %lu ms\n", timer_ms(timer_read() - tl));
            {
                /* Sight lines from the soldier to every other tile within
                   20 on its level: the cost of los_clear. */
                const Unit *su = &g_units[u];
                int dx, dy, clear = 0;
                tl = timer_read();
                for (dy = -20; dy <= 20; dy += 2)
                    for (dx = -20; dx <= 20; dx += 2)
                        if (IN_MAP(su->x + dx, su->y + dy, su->z))
                            clear += los_clear(su->x, su->y, su->z, su->x + dx, su->y + dy, su->z);
                fprintf(f, "441 sight lines: %lu ms, %d clear\n", timer_ms(timer_read() - tl), clear);
            }
            vox_bench(f, u);
            g_seen = NULL;
        }
        fx_bench(f);
        mouse_bench(f);
        xs_bench(f);
        cv_bench(f, seen);
        lit_bench(f);
        if (g_vga) {
            /* A tracer dot (gfx_dot, vgaasm.asm ui_dot): 4 pixels XORed in,
               gone when drawn again. */
            volatile unsigned char far *v = (volatile unsigned char far *)MK_FP(0xA000, 50 * 320 + 100);
            unsigned char was[4], d = 0, k;
            vga_sprite_check(f);
            render_full();
            mouse_hide();
            for (k = 0; k < 4; k++)
                was[k] = v[(k >> 1) * 320 + (k & 1)];
            gfx_dot(100, 50, 14);
            for (k = 0; k < 4; k++)
                d += v[(k >> 1) * 320 + (k & 1)] != was[k];
            gfx_dot(100, 50, 14);
            for (k = 0; k < 4; k++)
                d += (v[(k >> 1) * 320 + (k & 1)] != was[k]) * 16;
            fprintf(f, "VGA dot check: %u of 4 pixels changed, %u not back\n", d & 15, d >> 4);
            if (*(void far * far *)MK_FP(g_vga, 0x9D8)) {
                /* DOS's smoke and fire pictures' step (vgaasm.asm pics_next):
                   one pass of bplayer.c's idle loop over fx_bench's smoke
                   and fire, a rectangle an idle turn; the longest is what
                   a key waits for at most. */
                const int far *(__cdecl far *next)(void) =
                    (const int far *(__cdecl far *)(void))MK_FP(g_vga, VGA_PICS);
                const int far *r;
                unsigned long t0, t1, all = 0, most = 0;
                int n = 0, x, y;
                fx_reset();
                fx_smoke_cloud(20, 20, 0);
                for (y = 22; y < 26; y++)
                    for (x = 16; x < 20; x++)
                        fx_fire_set(x, y, 0, 3, 5);
                g_view_z = 0;
                *(unsigned char far *)MK_FP(g_vga, 0x9DC) = ~(*(unsigned char far *)MK_FP(0x40, 0x6C) >> 2);
                *(unsigned far *)MK_FP(g_vga, 0x9DE) = 0;     /* V_NXT: a pass from the start */
                render_center(20, 20, 0);
                render_full();
                for (;;) {
                    t0 = timer_read();
                    if ((r = next()) == NULL)
                        break;
                    render_rect(r[0], r[1], r[2], r[3]);
                    t1 = timer_read() - t0;
                    all += t1;
                    if (t1 > most)
                        most = t1;
                    if (++n == 12)  /* halfway: what is drawn is what a full redraw draws (no seams) */
                        check_redraw(f, "pictures halfway through their step");
                    if (!*(unsigned far *)MK_FP(g_vga, 0x9DE))
                        break;          /* the pass is over (V_NXT 0: the next call starts one) */
                }
                fprintf(f, "VGA pictures' step: %d redraws for %d smoke + %d fire tiles, %lu ms, longest %lu ms\n",
                        n, n_smoke, n_fire, timer_ms(all), timer_ms(most));
                for (x = 0; x < n_smoke; x++)
                    g_los_flags[TIDX(smoke[x].x, smoke[x].y, 0)] &= ~LF_FX;
                for (x = 0; x < n_fire; x++)
                    g_los_flags[TIDX(fire[x].x, fire[x].y, 0)] &= ~LF_FX;
                fx_reset();
                render_full();
            }
            if (g_night) {  /* the night lights (vgaasm.asm LD_NREL, LD_NPRE: in PAL,
                               which holds the palette by day) */
                const unsigned far *nl = (const unsigned far *)MK_FP(g_vga, 48);
                fprintf(f, "VGA lights: %u relights in %u night presents\n", nl[0], nl[1]);
            }
        }
        fclose(f);
    }
    render_center(20, 20, 0);
    g_cursor_on = 0;
    render_full();
    g_cursor_on = 1;
    {
        char line[48];
        sprintf(line, "Avg full redraw %lu ms", total / n);
        hud_clear_rows(HUD_ROW, 7);
        hud_text(1, HUD_ROW + 2, 15, line);
    }
}

