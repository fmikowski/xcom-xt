/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battle screens kept out of the battle section (see batui.h). Compiled
   with -zc: the strings stay in this overlay section. */

#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "batui.h"
#include "battle.h"
#include "battle_in.h"
#include "gfx.h"
#include "hud.h"
#include "inv.h"
#include "items.h"
#include "los.h"
#include "effects.h"
#include "input.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "psi.h"
#include "render.h"
#include "sound.h"
#include "timer.h"
#include "uik.h"
#include "unit.h"

char g_pack_names[BU_PACKS][13];

#define VGA         ((volatile unsigned char far *)MK_FP(0xA000, 0))

/* ------------------------------------------------------------------ */
/* Pack trailers (tools/convert.py): sections before u16 length, "XTR1" */

static FILE *trailer(const char *name, const char *magic)
{
    unsigned short len, n;
    char m[4];
    long pos;
    FILE *f;

    if (!name[0] || (f = fopen(name, "rb")) == NULL)
        return NULL;
    if (!fseek(f, -6L, SEEK_END) && fread(&len, 2, 1, f) == 1 && fread(m, 4, 1, f) == 1
        && !memcmp(m, "XTR1", 4))
        for (pos = -6L - len; pos < -6L;) {
            if (fseek(f, pos, SEEK_END) || fread(m, 4, 1, f) != 1)
                break;
            if (!memcmp(m, magic, 4))
                return f;
            if (!memcmp(m, "XCU1", 4))
                pos += 20;
            else if (!memcmp(m, "XMM1", 4) && fread(&n, 2, 1, f) == 1)
                pos += 6 + (long)n * (g_video == VIDEO_EGA ? 10 : 6);
            else
                break;
        }
    fclose(f);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Night colours of the panel */

/* The night palette shows registers k and k + 8 (k 1-7) in the same
   colour (7 and 15: light grey; 8 alone dark grey). Where the battle's sprites hardly use one register
   of a pair (per mille of their pixels, summed over the packs), it shows
   a bright colour the panel needs instead, most needed first. */
#define FREE_LIMIT  6

static unsigned char cmap[16];

static void night_default(void)
{
    int i;

    for (i = 0; i < 16; i++)
        g_night_pal[i] = (unsigned char)(i < 8 ? i : i == 8 ? 16 : i - 8);
}

void bu_night(void)
{
    static const unsigned char want[7] = { 15, 14, 12, 10, 13, 11, 9 };
    unsigned use[16];
    unsigned char reg[7], shows[16], u[16];
    int i, k, r, have = 0;
    char line[64];

    night_default();
    g_uk_cmap = NULL;
    if (g_video != VIDEO_EGA)
        return;
    if (g_lit) {                /* the night light: the day's colours */
        memcpy(g_night_pal, g_day_pal, 16);
        return;
    }
    memset(use, 0, sizeof use);
    for (k = 0; k < BU_PACKS; k++) {
        FILE *f;
        if (!g_pack_names[k][0])
            continue;
        f = trailer(g_pack_names[k], "XCU1");
        if (!f || fread(u, 16, 1, f) != 1) {
            if (f)
                fclose(f);
            log_printf("night: no colour table in %s", g_pack_names[k]);
            return;
        }
        fclose(f);
        for (i = 0; i < 16; i++)
            use[i] += u[i];
        have = 1;
    }
    if (!have)
        return;
    /* The less used register of each pair, cheapest first. */
    for (k = 1; k <= 7; k++) {
        int a = k, b = k + 8;
        unsigned char c = (unsigned char)(use[b] <= use[a] ? b : a);
        for (i = k - 1; i > 0 && use[reg[i - 1]] > use[c]; i--)
            reg[i] = reg[i - 1];
        reg[i] = c;
    }
    memset(shows, 0, sizeof shows);
    for (i = 0; i < 7 && use[reg[i]] <= FREE_LIMIT; i++) {
        shows[reg[i]] = want[i];
        g_night_pal[reg[i]] = g_day_pal[want[i]];
    }
    /* Each day colour to a register showing it at night. */
    for (i = 0; i < 16; i++) {
        int c = i;
        if (c >= 9) {
            for (r = 0; r < 16 && shows[r] != c; r++)
                ;
            if (r < 16) {
                cmap[i] = (unsigned char)r;
                continue;
            }
            c = c == 15 ? 7 : c - 8;            /* not kept: as dark as the map */
        }
        if (c == 8)
            r = 8;                              /* dark grey */
        else
            r = c && shows[c] ? c + 8 : c;
        cmap[i] = (unsigned char)r;
    }
    g_uk_cmap = cmap;
    for (i = k = 0; i < 16; i++)
        if (shows[i])
            k += sprintf(line + k, " %d=%d", i, shows[i]);
    line[k] = 0;
    log_printf("night: panel colours in registers%s", k ? line : " (none)");
}

void bu_day(void)
{
    g_uk_cmap = NULL;
    night_default();
}

/* ------------------------------------------------------------------ */
/* Minimap (MiniMapState / MiniMapView): 4x4 pictures from SCANG.DAT */

#define MM_X        48
#define MM_Y        16
#define MM_COLS     54          /* 216 pixels: whole bytes */
#define MM_ROWS     37
#define UNIT_MINI   9           /* X-COM, alien, civilian x 3 frames */

/* (the frames and the view also src/batuiv.c: batui.h) */
unsigned char *g_mm;            /* the pack's minimap frames */
static unsigned short (*cells)[4];      /* a row of cells, pixel rows */
static unsigned mm_n, mm_fs;
int g_mm_x0, g_mm_y0, g_mm_z, g_mm_anim;
#define mm g_mm
#define mm_x0 g_mm_x0
#define mm_y0 g_mm_y0
#define mm_z g_mm_z
#define mm_anim g_mm_anim

/* Bit masks of 4 pixels: EGA nibbles, CGA 2-bit pairs. */
static unsigned short mask_px(unsigned bits)
{
    unsigned short m = 0;
    int i, w = g_video == VIDEO_EGA ? 4 : 2;

    for (i = 0; i < 4; i++)
        if (bits & (8 >> i))
            m |= ((1 << w) - 1) << ((3 - i) * w);
    return m;
}

/* Frame f over the cell (black: the shape in black, an undiscovered tile). */
static void put(unsigned f, unsigned short *rows, int black)
{
    const unsigned char *p = mm + f * mm_fs;
    unsigned m = *(const unsigned short *)(p + mm_fs - 2);
    int r;

    for (r = 0; r < 4; r++) {
        unsigned short bits = mask_px((m >> (12 - 4 * r)) & 15);
        unsigned short d = g_video == VIDEO_EGA ? ((const unsigned short *)p)[r] : p[r];
        if (bits)
            rows[r] = (unsigned short)((rows[r] & ~bits) | (black ? 0 : d & bits));
    }
}

static void compose(int x, int y, unsigned short *rows)
{
    int z, p;

    rows[0] = rows[1] = rows[2] = rows[3] = 0;
    if (x < 0 || y < 0 || x >= (int)g_mx || y >= (int)g_my)
        return;
    for (z = 0; z <= mm_z; z++) {
        unsigned idx = (z * g_my + y) * g_mx + x;
        const unsigned char *t = g_map + idx * 4;
        int seen = !g_seen || g_seen[idx];
        for (p = 0; p < 4; p++)
            if (t[p] && t[p] < g_pak.n_mcd)
                put(t[p], rows, !seen);
        /* At night the tiles outside the light of flares and fires are
           shaded (MiniMapView: the tile's shade, at most 7): every other
           pixel dark. */
        if (g_night && z == mm_z && !fx_lit(x, y, z) && !(g_los_flags[idx] & LF_FIRE))
            for (p = 0; p < 4; p++)
                rows[p] &= g_video == VIDEO_EGA ? (p & 1 ? 0x0F0F : 0xF0F0) : (p & 1 ? 0x33 : 0xCC);
        if (g_unit_at[idx]) {
            const Unit *u = &g_units[g_unit_at[idx] - 1];
            if (u->alive && (u->faction == FACTION_XCOM || !g_seen || u->seen))
                put(g_pak.n_mcd + (u->faction > 2 ? 2 : u->faction) * 3 + mm_anim, rows, 0);
        }
    }
}

/* Crosshair around the centre cell (MiniMapView). */
static void crosshair(void)
{
    static const signed char d[8][2] = {
        { -3, -3 }, { -2, -2 }, { 6, -3 }, { 5, -2 }, { -3, 6 }, { -2, 5 }, { 6, 6 }, { 5, 5 }
    };
    int i, cx = MM_X + (MM_COLS / 2) * 4, cy = MM_Y + (MM_ROWS / 2) * 4;

    for (i = 0; i < 8; i++)
        uk_fill(cx + d[i][0], cy + d[i][1], 1, 1, 15);
}

/* Draws tile rows ty0..ty1 - 1, cells c0..c1 - 1 (c0, c1 even). */
static void mm_draw(int ty0, int ty1, int c0, int c1)
{
    unsigned char line[MM_COLS / 2];
    int ty, c, r, p, b;

    mouse_hide_rect(MM_X + c0 * 4, MM_Y + ty0 * 4, MM_X + c1 * 4, MM_Y + ty1 * 4);
    if (g_vga) {
        bu_mm_vga(ty0, ty1, c0, c1);
        crosshair();
        return;
    }
    for (ty = ty0; ty < ty1; ty++) {
        for (c = c0; c < c1; c++)
            compose(mm_x0 + c, mm_y0 + ty, cells[c]);
        for (r = 0; r < 4; r++) {
            int y = MM_Y + ty * 4 + r;
            if (g_video != VIDEO_EGA) {
                unsigned char far *d = (unsigned char far *)MK_FP(0xB800,
                    (y & 1) * 0x2000 + (y >> 1) * 80 + MM_X / 4);
                for (c = c0; c < c1; c++)
                    d[c] = (unsigned char)cells[c][r];
                continue;
            }
            for (p = 0; p < 4; p++) {
                for (b = c0 / 2; b < c1 / 2; b++) {
                    unsigned v = 0, d0 = cells[2 * b][r], d1 = cells[2 * b + 1][r];
                    int k;
                    for (k = 12; k >= 0; k -= 4)
                        v = (v << 1) | ((d0 >> (k + p)) & 1);
                    for (k = 12; k >= 0; k -= 4)
                        v = (v << 1) | ((d1 >> (k + p)) & 1);
                    line[b] = (unsigned char)v;
                }
                outpw(0x3C4, (0x100 << p) | 2);
                _fmemcpy((void far *)(VGA + y * 40 + MM_X / 8 + c0 / 2), line + c0 / 2,
                         (c1 - c0) / 2);
            }
            outpw(0x3C4, 0x0F02);
        }
    }
    crosshair();
}

static void mm_level(void)
{
    char s[4];

    uk_restore(280, 72, 32, 20);
    sprintf(s, "%d", mm_z + 1);
    uk_text(296 - uk_width(UK_BIG, s) / 2, 75, UK_BIG, 10, s);
}

/* Units change their marker frame (MiniMapView's animation). */
static void mm_units(void)
{
    int i;

    mm_anim = (mm_anim + 1) % 3;
    for (i = 0; i < g_nunits; i++) {
        const Unit *u = &g_units[i];
        int c = u->x - mm_x0, ty = u->y - mm_y0;
        if (!u->alive || u->z > mm_z || c < 0 || c >= MM_COLS || ty < 0 || ty >= MM_ROWS)
            continue;
        if (u->faction != FACTION_XCOM && g_seen && !u->seen)
            continue;
        c &= ~1;
        mm_draw(ty, ty + 1 + (u->big && ty + 1 < MM_ROWS), c,
                c + 2 + (u->big && c + 2 < MM_COLS) * 2);
    }
}

int bu_minimap(int *px, int *py, int *pz)
{
    int picked = 0, redraw = 1;
    const unsigned char *cm;
    unsigned long t0;
    FILE *f;
    unsigned short n = 0;

    f = trailer(g_pack_names[0], "XMM1");
    mm_fs = g_video == VIDEO_EGA ? 10 : 6;
    if (f && g_vga) {
        /* VGA packs: "XMV1" u16 n, n frames of 16 palette indices, u16 n,
           just before the palette "XPL1" (774 bytes) at the end (convert.py) */
        mm_fs = 16;
        if (fseek(f, -6L - 774 - 2, SEEK_END) || fread(&n, 2, 1, f) != 1
            || fseek(f, -6L - 774 - 2 - (long)n * 16, SEEK_END))
            n = 0;
    } else if (!f || fread(&n, 2, 1, f) != 1)
        n = 0;
    if (n && n == g_pak.n_disk_mcd + UNIT_MINI)
        mm = malloc((g_pak.n_mcd + UNIT_MINI) * mm_fs + (g_vga ? 0 : MM_COLS * 8));
    if (mm) {
        /* The pack's frames by its record table: the common records', the
           landing craft's (Pack.craft_rec0), the unit markers'. */
        unsigned c0 = g_pak.craft_rec0, nc = g_pak.n_mcd - c0;
        long at = ftell(f);
        n = g_pak.n_mcd + UNIT_MINI;
        if (fread(mm, mm_fs, c0, f) != c0
            || fseek(f, at + (long)g_pak.craft_rec_disk * mm_fs, SEEK_SET)
            || fread(mm + c0 * mm_fs, mm_fs, nc, f) != nc
            || fseek(f, at + (long)g_pak.n_disk_mcd * mm_fs, SEEK_SET)
            || fread(mm + (c0 + nc) * mm_fs, mm_fs, UNIT_MINI, f) != UNIT_MINI) {
            free(mm);
            mm = NULL;
        }
    }
    if (!mm) {
        if (f)
            fclose(f);
        free(mm);
        mm = NULL;
        log_printf("minimap: no pictures in %s", g_pack_names[0]);
        return 0;
    }
    fclose(f);
    mm_n = n;
    cells = (unsigned short (*)[4])(mm + n * mm_fs);
    mm_z = *pz;
    mm_x0 = *px - MM_COLS / 2;
    mm_y0 = *py - MM_ROWS / 2;
    mm_anim = 0;

    uk_open();
    gfx_night(0);               /* a screen of its own, in day colours */
    cm = g_uk_cmap;
    g_uk_cmap = NULL;
    uk_theme(T_GREEN);
    mouse_hide();
    uk_pic(UI_SCANBORD, 0, 0);
    uk_fill(MM_X, MM_Y, MM_COLS * 4 + 6, MM_ROWS * 4, 0);
    uk_save(72, 92);
    log_printf("minimap level %d centre %d,%d", mm_z, *px, *py);
    for (;;) {
        unsigned key;
        int ch, scan;

        if (redraw) {
            mm_draw(0, MM_ROWS, 0, MM_COLS);
            mm_level();
            redraw = 0;
        }
        t0 = timer_read();
        while (!input_ready())
            if (timer_read() - t0 > 1193UL * 250) {
                mm_units();
                t0 = timer_read();
            }
        key = input_key();
        ch = key & 0xFF;
        scan = key >> 8;
        if (key == KEY_LCLICK) {
            int mx = g_mouse_x, my = g_mouse_y;
            if (mx >= MM_X && mx < MM_X + MM_COLS * 4 && my >= MM_Y && my < MM_Y + MM_ROWS * 4) {
                /* Centre there; the battlescape cursor goes there too. */
                *px = mm_x0 + (mx - MM_X) / 4;
                *py = mm_y0 + (my - MM_Y) / 4;
                if (*px >= 0 && *py >= 0 && *px < (int)g_mx && *py < (int)g_my) {
                    mm_x0 = *px - MM_COLS / 2;
                    mm_y0 = *py - MM_ROWS / 2;
                    picked = redraw = 1;
                }
                continue;
            }
            if (mx >= 24 && mx < 42 && my >= 62 && my < 82)
                ch = '+';
            else if (mx >= 24 && mx < 42 && my >= 88 && my < 108)
                ch = '-';
            else if (mx >= 275 && mx < 307 && my >= 145 && my < 177)
                break;
            else
                continue;
        }
        if (key == KEY_RCLICK || ch == 27 || ch == 13 || ch == ' ')
            break;
        if ((ch == '+' || scan == 0x49) && mm_z < (int)g_mz - 1)
            mm_z++, redraw = 1;
        else if ((ch == '-' || scan == 0x51) && mm_z > 0)
            mm_z--, redraw = 1;
        else if (scan == 0x48)
            mm_y0 -= 4, redraw = 1;
        else if (scan == 0x50)
            mm_y0 += 4, redraw = 1;
        else if (scan == 0x4B)
            mm_x0 -= 4, redraw = 1;
        else if (scan == 0x4D)
            mm_x0 += 4, redraw = 1;
    }
    *pz = mm_z;
    free(mm);
    mm = NULL;
    uk_close();
    hud_clear_rows(0, 25);
    g_uk_cmap = cm;
    gfx_night(g_night);
    return picked;
}

/* ------------------------------------------------------------------ */
/* Windows over the map (rows 0-143) on TAC00.SCR, as AbortMissionState */

static void tac_window(void)
{
    uk_open();
    uk_theme(T_GREY);
    mouse_hide();
    uk_pic(UI_BG_ABORT, 0, 0);
    uk_restore(0, 144, 320, 56);        /* the panel's clean copy */
    uk_window(0, 0, 320, 144);
}

/* Waits for a key of keys or a click on one of its buttons (120 x 16 at
   the given x, y). Enter = def. Returns the key's character. */
static int tac_wait(const char *keys, const short (*btn)[2], int def)
{
    int n = strlen(keys);

    for (;;) {
        unsigned key = input_key();
        int ch = key & 0xFF, i;
        if (key == KEY_RCLICK)
            return 27;
        if (key == KEY_LCLICK) {
            for (i = 0; i < n; i++)
                if (g_mouse_x >= btn[i][0] && g_mouse_x < btn[i][0] + 120
                    && g_mouse_y >= btn[i][1] && g_mouse_y < btn[i][1] + 16)
                    return keys[i];
            continue;
        }
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch == 13)
            ch = def;
        if (ch == 27 || strchr(keys, ch))
            return ch;
    }
}

int bu_abort(void)
{
    static const short btn[2][2] = { { 16, 110 }, { 184, 110 } };
    char line[40];
    int i, in = 0, out = 0, ch;

    for (i = 0; i < g_nunits; i++) {
        const Unit *u = &g_units[i];
        if (!u->alive || u->faction != FACTION_XCOM)
            continue;
        if (unit_in_exit_area(u))
            in++;
        else
            out++;
    }
    tac_window();
    sprintf(line, "%d units in exit area", in);
    uk_text(16, 25, UK_BIG, g_uk->title, line);
    sprintf(line, "%d units outside exit area", out);
    uk_text(16, 50, UK_BIG, g_uk->title, line);
    uk_center(0, 320, 75, UK_BIG, g_uk->hilite, "Abort Mission ?");
    uk_button(btn[0][0], btn[0][1], 120, 16, "OK", 0);
    uk_button(btn[1][0], btn[1][1], 120, 16, "Cancel", 0);
    ch = tac_wait("yn", btn, 'y');
    uk_close();
    return ch == 'y';
}

/* Scrolling at the screen's edges (the DOS Game Options' Scroll Speed and
   Scroll Type, bplayer.c's map_key): 32 pixels across or 16 down a step
   (whole EGA words: render_scroll moves what is drawn), a step every
   400 .. 0 ms (speed 1 .. 5); the middle of the map stays within half the
   map's size of the middle of the view. */
int bu_scroll(int click)
{
    static unsigned long last;
    unsigned long gap = (5 - g_bopt[BO_SCROLL]) * 119318UL;
    int nx, ny, half = (g_mx + g_my) << 3, c;

    if ((unsigned)(g_mouse_x - 2) < 316 && (unsigned)(g_mouse_y - 2) < 196)
        return 0;               /* not at an edge */
    do {
        nx = g_cam_x;
        ny = g_cam_y;
        if (g_mouse_x < 2)
            nx += 32;
        else if (g_mouse_x >= 318)
            nx -= 32;
        if (g_mouse_y < 2)
            ny += 16;
        else if (g_mouse_y >= 198)
            ny -= 16;
        c = ((g_mx - g_my) << 3) + nx - 144;
        if (c < -half || c > half)
            nx = g_cam_x;
        c = ((g_mx + g_my) << 2) + ny - VIEW_H / 2;
        if (c < -half / 2 || c > half / 2)
            ny = g_cam_y;
        if (nx == g_cam_x && ny == g_cam_y)
            continue;
        while (timer_read() - last < gap)
            if (!click)
                return 1;
        mouse_hide();
        render_scroll(nx, ny);
        last = timer_read();
        log_printf("T%d: view scrolled to %d,%d", bt_turn, nx, ny);
    } while (click && input_held(1));
    return 1;
}

/* An unconscious unit gets up when its stun is below its health and its
   tile is free (SavedBattleGame::reviveUnconsciousUnits), taking back its
   weapon if it still lies there; one carried gets up next to its carrier
   (placeUnitNearPosition: the eight neighbours from the north on) and
   leaves the inventory. Here rather than in the core (bturn.c and
   bplayer.c call it; both fit next to this section). */
void wake_up(int i)
{
    Unit *u = &g_units[i];
    char text[48];
    int k = g_max_floor, d;

    if (u->stun >= u->hp)
        return;
    if (u->special & SP_CARRIED) {
        const Unit *c;
        int e = inv_body_entry(i);
        if (e < 0)
            return;
        c = &g_units[g_carry[e].unit];
        for (d = 0; d < 8 && !unit_standable(c->x + g_dir_dx[d], c->y + g_dir_dy[d], c->z); d++)
            ;
        if (d == 8)
            return;             /* no room: still carried */
        inv_body_down(e, c->x + g_dir_dx[d], c->y + g_dir_dy[d], c->z);
        log_printf("T%d: %s gets up from %s's inventory at %d,%d,%d", bt_turn, u->name, c->name,
                   u->x, u->y, u->z);
    } else if (UNIT_AT(u->x, u->y, u->z))
        return;
    else
        k = 0;
    u->alive = 1;
    u->stunned = 0;
    u->kneeling = 0;
    UNIT_AT(u->x, u->y, u->z) = (unsigned char)(i + 1);
    for (; k < g_max_floor; k++) {
        FloorItem *f = &g_floor[k];
        if (f->item != NO_ITEM && g_items[f->item].kind == IK_WEAPON
            && f->x == u->x && f->y == u->y && f->z == u->z) {
            inv_arm(u, f);
            break;
        }
    }
    sprintf(text, "%s regains consciousness", unit_title(u));
    if (u->faction == FACTION_XCOM || u->seen) {
        msg(u->faction == FACTION_XCOM ? COL_GOOD : COL_BAD, text);
        render_tile(u->x, u->y, u->z);
    } else
        log_msg(COL_INFO, text);
}

/* ------------------------------------------------------------------ */
/* The Mind Probe screen (psi.h; here rather than in bplayer.c, which calls
   it: memory, see build.ps1) */

/* Mind probe in a hand (bplayer.c): the stats of the unit at the cursor;
   1 when the screen was shown. */
int bu_mind_probe(int s)
{
    Unit *u = &g_units[s];
    unsigned char occ = UNIT_AT(g_cur_x, g_cur_y, g_cur_z);
    Unit *v = occ ? &g_units[occ - 1] : NULL;
    int cost = u->tu_max * PROBE_TU_PCT / 100;

    if (!v || !unit_visible(v)) {
        msg(COL_BAD, "Put the cursor on a unit in sight");
        return 0;
    }
    if (u->tu < cost) {
        msg(COL_BAD, "Not enough time units");
        return 0;
    }
    u->tu -= cost;
    if (v->faction == FACTION_XCOM)
        v->psi_state |= PSI_PROBED;
    log_printf("T%d: mind probe on %s: tu %d hp %d acc %d psi %d skill %d", bt_turn, v->name,
               v->tu_max, v->hp_max, v->accuracy, psi_strength(v), psi_skill(v));
    psi_probe_screen(v);
    msg(COL_INFO, "");
    return 1;
}

/* One line of the probe screen at *row. */
static void show(int *row, int colour, const char *s)
{
    static const unsigned char tabs[] = { 120, 0 };
    uk_line(24, 30 + (*row)++ * 9, 260, colour, s, tabs);
}

void psi_probe_screen(const Unit *u)
{
    char line[48];
    int row = 2;

    uk_open();
    uk_theme(T_PURPLE);
    uk_fill(0, 0, 320, 144, 0);
    uk_window(0, 0, 320, 144);
    sprintf(line, "Mind Probe: %s", unit_title(u));
    uk_center(0, 320, 8, UK_BIG, g_uk->title, line);
    row = 0;
    sprintf(line, "Time units\t%d / %d", u->tu, u->tu_max);
    show(&row, 7, line);
    sprintf(line, "Health\t%d / %d", u->hp, u->hp_max);
    show(&row, 7, line);
    sprintf(line, "Firing accuracy\t%d", u->accuracy);
    show(&row, 7, line);
    sprintf(line, "Reactions\t%d", u->reactions);
    show(&row, 7, line);
    sprintf(line, "Bravery\t%d", u->bravery);
    show(&row, 7, line);
    sprintf(line, "Morale\t%d", u->morale);
    show(&row, 7, line);
    sprintf(line, "Armour\tfront %d side %d rear %d", u->armor[0], u->armor[1], u->armor[2]);
    show(&row, 7, line);
    sprintf(line, "Weapon\t%s", u->weapon == NO_ITEM ? (u->melee ? "Melee" : "-")
                                                             : g_items[u->weapon].name);
    show(&row, 7, line);
    row++;
    sprintf(line, "Psi strength\t%d", psi_strength(u));
    show(&row, 13, line);
    sprintf(line, "Psi skill\t%d", psi_skill(u));
    show(&row, 13, line);
    input_key();
    uk_close();
}

/* Motion scanner: shows where unseen aliens moved last turn until a key
   is pressed. */
void bu_motion_scan(int s)
{
    Unit *u = &g_units[s];
    static const signed char ring[8][2] = {
        { -3, 0 }, { 3, 0 }, { 0, -2 }, { 0, 2 }, { -2, -1 }, { 2, -1 }, { -2, 1 }, { 2, 1 }
    };
    int px[MAX_UNITS], py[MAX_UNITS], n = 0, i, k, cost = u->tu_max / 4;
    char text[40];

    if (u->tu < cost) {
        msg(COL_BAD, "Not enough time units");
        return;
    }
    u->tu -= cost;
    for (i = 0; i < g_nunits; i++) {
        const Unit *a = &g_units[i];
        int x, y;
        if (!a->alive || a->faction != FACTION_ALIEN || !a->moved || a->seen)
            continue;
        x = TILE_SX(a->x, a->y) + 16;
        y = TILE_SY(a->x, a->y, a->z) + 20;
        if (x < 4 || x > 315 || y < 3 || y >= VIEW_H - 3)
            continue;
        px[n] = x;
        py[n++] = y;
    }
    for (k = 0; k < 2; k++) {               /* draw, then erase (XOR) */
        for (i = 0; i < n; i++) {
            int j;
            for (j = 0; j < 8; j++)
                gfx_dot(px[i] + ring[j][0], py[i] + ring[j][1], COL_BAD);
        }
        if (!k) {
            sprintf(text, "Scanner: %d movements. Press a key", n);
            msg(COL_INFO, text);
            input_key();
            msg(COL_INFO, "");
        }
    }
}
