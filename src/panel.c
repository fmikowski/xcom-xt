/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The battlescape control panel of the DOS game (see panel.h). Positions
   follow OpenXcom's BattlescapeState, the panel's top row being y 144. */

#include <i86.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "input.h"
#include "inv.h"
#include "items.h"
#include "los.h"
#include "panel.h"
#include "uik.h"
#include "unit.h"

#define PY          144
#define LHAND_X     8
#define RHAND_X     280
#define HAND_Y      (PY + 4)
#define MSG_X       48
#define MSG_Y       (PY + 32)
#define MSG_W       224
#define MSG_H       24
#define MARK_X      300
#define MARK_C      GFX_HOOK_C      /* its first 8-pixel cell */
#define MARK_Y(i)   (PY - 16 - (i) * 13)
#define MAX_MARKS   10

static int msg_on, shown_sel = -2, shown_level = -1, reserve_mode, opened;
static unsigned char shown_hand[2][2] = { { 0xFE }, { 0xFE } };  /* left, right: item, number */
static unsigned char marks[MAX_MARKS], n_marks;
static char cursor_text[24];
/* What figures() drew last and the screen still shows: the unit (its
   offset; 0: none, FIG_NONE: something else, all is drawn again) and its
   numbers: time units, energy, health, morale, then their most. */
#define FIG_NONE    0xFFFFu
static unsigned fig_u;
static unsigned char fig_v[8] = { 0, 0, 0, 0, 0, 0, 0, 100 };
static void marks_over(int r0, int r1);

/* The pressed reserve-TU button (OpenXcom's reserve group) goes into the
   clean copy, so the lower panel's redraws bring it back for free. */
static void draw_reserve(void)
{
    uk_pic_clean(UI_RESERVE + reserve_mode);
}

/* 3x5 digits (OpenXcom's NumberText): rows of 3 bits. */
static const unsigned short digits[10] = {
    075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717
};

static void number(int x, int y, int c, unsigned n)
{
    char s[6];
    int i, r, b;

    sprintf(s, "%u", n);
    for (i = 0; s[i]; i++, x += 4) {
        unsigned d = digits[s[i] - '0'];
        for (r = 0; r < 5; r++)
            for (b = 0; b < 3; b++, d <<= 1)
                if (d & 0x4000)
                    uk_fill(x + b, y + r, 1, 1, c);
    }
}

void panel_open(void)
{
    uk_open();
    uk_cga_palette(0);
    uk_theme(T_GREY);
    if (!uk_pic(UI_ICONS, -1, 0))
        uk_fill(0, PY, 320, 56, 8);
    uk_save(PY, 200);
    draw_reserve();
    uk_restore(48, PY + 32, 64, 24);
    opened = 1;
    fig_u = 0;
    msg_on = 0;
    shown_sel = -2;
    shown_level = -1;
    shown_hand[0][0] = shown_hand[1][0] = 0xFE;
    n_marks = 0;
    g_present_hook = marks_over;
    cursor_text[0] = 0;
}

void panel_close(void)
{
    opened = 0;
    g_present_hook = 0;
    uk_close();
}

/* A hand's item; n as inv_hand: a gun shows its rounds. */
static void hand(int x, unsigned char item, unsigned char n)
{
    uk_restore(x, HAND_Y, 32, 48);
    if (item == NO_ITEM)
        return;
    uk_pic_in(item == IT_BODY ? UI_BODY + unit_body(&g_units[n])
              : item >= IT_TANK_CANNON ? UI_HWP + item - IT_TANK_CANNON : UI_BIGOB + item, x,
              HAND_Y);
    if (item < N_ITEMS && g_items[item].clip_size)
        number(x + 1, HAND_Y + 1, 14, n);
}

/* Red squares with numbers for the aliens the selected unit sees, above
   the right of the panel (BattlescapeState's _btnVisibleUnit). They lie
   over the map on the screen only, never in the back buffer: every
   present of the view over their column draws them again (gfx.c's hook),
   so scrolls and redraws leave them in place. */
static void draw_mark(int i)
{
    int y = MARK_Y(i);

    uk_fill(MARK_X, y, 15, 12, 4);
    uk_rect(MARK_X, y, 15, 12, 12);
    number(MARK_X + 6 - (i + 1 >= 10) * 2, y + 4, 15, i + 1);
}

/* gfx_present_cells hook: rows r0-r1 from column MARK_C were shown. */
static void marks_over(int r0, int r1)
{
    int i;

    for (i = 0; i < n_marks; i++)
        if (r0 < MARK_Y(i) + 12 && MARK_Y(i) < r1)
            draw_mark(i);
}

/* The list is the unit's own sight now (BattleUnit::getVisibleUnits as
   calculateFOV keeps it: hostile units it sees), none without one. */
void panel_marks(int sel)
{
    int i, n = 0, old = n_marks, changed = 0;
    const Unit *a = g_units;

    if (sel >= 0)
        for (i = 0; i < g_nunits && n < MAX_MARKS; i++, a++)
            if (a->faction == FACTION_ALIEN && los_unit_sees(sel, i)) {
                changed |= marks[n] != i;
                marks[n++] = (unsigned char)i;
            }
    n_marks = (unsigned char)n;
    if (changed || n != old)    /* tests/README.md: how many, the first */
        log_printf("MARKS u%d %d %d", sel, n, n ? marks[0] : -1);
    if (n != old) {
        /* the map over the longer list (removed markers go), the hook
           draws the new ones */
        if (old > n)
            n = old;
        gfx_present_cells(MARK_C, MARK_Y(n - 1), 3, PY - 4 - MARK_Y(n - 1));
    }
}

/* EGA: the screen moved by dc cells, dr rows (gfx_shift_window): the
   markers' moved copy goes, the markers come back in place. */
void panel_shift(int dc, int dr)
{
    int y0 = MARK_Y(n_marks - 1) + dr, y1 = PY - 4 + dr;

    if (!n_marks || g_view_off)
        return;
    if (y0 < 0)
        y0 = 0;
    if (y1 > VIEW_H)
        y1 = VIEW_H;
    /* (|dc| < 32: the copy starts right of cell 5; from cell 37 on, the
       markers' own cells are shown with it) */
    dc += MARK_C;
    if (y0 < y1)
        gfx_present_cells(dc > MARK_C ? MARK_C : dc, y0, 3, y1 - y0);
    marks_over(0, VIEW_H);
}

/* The lower panel: reserve buttons, and the soldier's rank, name and
   figures (u NULL: none selected). While the unit is the one shown, the
   buttons and the rank picture left of the name (x 48-135) stay as they
   are: the picture comes from the pack, a disk read. h: MSG_H; from
   panel_cursor 9 or 0: with the unit's numbers also as shown, only the
   name line is drawn (the small font's 9 rows), or nothing. */
static void figures(const Unit *u, int h)
{
    static const unsigned char at[7] = {
        offsetof(Unit, tu), offsetof(Unit, energy), offsetof(Unit, hp), offsetof(Unit, morale),
        offsetof(Unit, tu_max), offsetof(Unit, stamina), offsetof(Unit, hp_max)
    };
    /* each number's colour and place */
    static const unsigned char colour[4] = { 10, 14, 12, 13 }, nx[4] = { 137, 155, 137, 155 },
                               ny[4] = { PY + 42, PY + 42, PY + 50, PY + 50 };
    int i;
    unsigned char d = 0, c;

    if (FP_OFF(u))
        for (i = 0; i < 7; i++) {
            unsigned char v = ((const unsigned char *)u)[at[i]];
            if (v != fig_v[i]) {
                fig_v[i] = v;
                d = 1;
            }
        }
    if (FP_OFF(u) != fig_u) {
        fig_u = FP_OFF(u);
        h = MSG_H;
        uk_restore(MSG_X, MSG_Y, 136 - MSG_X, MSG_H);
        if (FP_OFF(u) && u->roster >= 0 && !uk_pic(UI_RANK + g_roster[u->roster].rank, -1, 0))
            fig_u = FIG_NONE;   /* (not in the pack: looked for every time) */
    } else if (d)
        h = MSG_H;
    else if (!h)
        return;
    uk_restore(136, MSG_Y, MSG_X + MSG_W - 136, h);
    if (!FP_OFF(u))
        return;
    uk_text(136, PY + 32, UK_SMALL, 14, unit_title(u));
    if (cursor_text[0])
        uk_text(271 - uk_width(UK_SMALL, cursor_text), PY + 32, UK_SMALL, 11, cursor_text);
    if (h != MSG_H)
        return;
    for (i = 0; i < 4; i++) {
        /* its number, and a bar: the value over its most in the dark shade */
        int value = fig_v[i], max = fig_v[i + 4], dark, y = PY + 41 + i * 4;
        if (i == 1 && !max)
            max = value;        /* (no stamina: the energy) */
        c = colour[i];
        dark = c - 8;
        number(nx[i], ny[i], c, value);
        if (max > 102)
            max = 102;
        if (value > max)
            value = max;
        /* On black where both shades look alike (CGA, a night without the
           bright colour). */
        if ((g_video != VIDEO_EGA && !g_vga) || (g_uk_cmap && g_uk_cmap[c] == g_uk_cmap[dark]))
            dark = 0;
        uk_fill(170, y, max, 3, dark);
        if (value > 0)
            uk_fill(170, y, value, 3, c);
    }
}

void panel_status(int sel, int level)
{
    const Unit *u = sel >= 0 ? &g_units[sel] : NULL;
    int k;

    uk_theme(T_GREY);
    if (level != shown_level) {
        uk_restore(232, PY + 6, 8, 5);
        number(232, PY + 6, 14, level + 1);
        shown_level = level;
    }
    for (k = 0; k < 2; k++) {   /* the left hand, the right */
        int n = 0;
        unsigned char item = u ? inv_hand(u, !k, &n) : NO_ITEM;
        if (item != shown_hand[k][0] || n != shown_hand[k][1]) {
            hand(k ? RHAND_X : LHAND_X, item, (unsigned char)n);
            shown_hand[k][0] = item;
            shown_hand[k][1] = (unsigned char)n;
        }
    }
    if (!msg_on)
        figures(u, MSG_H);
    shown_sel = sel;
}

/* Most calls find the unit's figures on the screen as they are (a cursor
   step, the call after panel_status): figures() then draws the name line
   alone, or nothing if the text is the same too. */
void panel_cursor(const char *s)
{
    int h = 0, i;
    char c;

    for (i = 0; i < sizeof cursor_text - 1; i++) {      /* (as strncpy; h: another text) */
        c = s[i];
        if (cursor_text[i] != c)
            h = 9;
        cursor_text[i] = c;
        if (!c)
            break;
    }
    if (!msg_on && shown_sel >= 0)
        figures(&g_units[shown_sel], h);
}

void panel_message(int colour, const char *s)
{
    char a[48];
    int n = strlen(s), cut = n;

    if (!s[0]) {
        panel_unmessage();
        return;
    }
    uk_theme(T_GREY);
    uk_fill(MSG_X, MSG_Y, MSG_W, MSG_H, 0);
    uk_rect(MSG_X, MSG_Y, MSG_W, MSG_H, 8);
    strncpy(a, s, sizeof a - 1);
    a[sizeof a - 1] = 0;
    /* One line, or two cut at a space. */
    while (cut > 0 && uk_width(UK_SMALL, a) > MSG_W - 8) {
        while (cut > 0 && a[cut] != ' ')
            cut--;
        a[cut] = 0;
    }
    if (cut < n && cut > 0) {
        uk_center(MSG_X, MSG_W, MSG_Y + 3, UK_SMALL, colour, a);
        uk_center(MSG_X, MSG_W, MSG_Y + 12, UK_SMALL, colour, s + cut + 1);
    } else
        uk_center(MSG_X, MSG_W, MSG_Y + 8, UK_SMALL, colour, s);
    msg_on = 1;
}

void panel_unmessage(void)
{
    if (!msg_on)
        return;
    msg_on = 0;
    fig_u = FIG_NONE;
    figures(shown_sel >= 0 && g_units[shown_sel].alive ? &g_units[shown_sel] : NULL, MSG_H);
}

void panel_reserve(int mode)
{
    reserve_mode = mode & 3;
    if (!opened)
        return;
    uk_pic_clean(UI_ICONS);             /* the old button unpressed */
    draw_reserve();
    if (!msg_on) {
        fig_u = FIG_NONE;
        figures(shown_sel >= 0 && g_units[shown_sel].alive ? &g_units[shown_sel] : NULL, MSG_H);
    }
}

/* The two rows of buttons, from x 48, 32 x 16 each (OpenXcom's order):
   unit up / down, map up / down, map / kneel, inventory / centre, next /
   next, layers / options, end turn / abort. */
static const unsigned top_keys[7] = { PK_UNIT_UP, '+', PK_MAP, 'i', '\t', PK_LAYERS, 'e' };
static const unsigned bottom_keys[7] = { PK_UNIT_DOWN, '-', 'k', 'c', '\t', PK_OPTIONS, PK_ABORT };

unsigned panel_click(int *alien)
{
    int x = g_mouse_x, y = g_mouse_y, i;

    *alien = -1;
    for (i = 0; i < n_marks; i++) {
        int my = PY - 16 - i * 13;
        if (x >= MARK_X && x < MARK_X + 15 && y >= my && y < my + 12) {
            *alien = marks[i];
            return 0;
        }
    }
    if (y < PY)
        return 0;
    if (y < PY + 32 && x >= 48 && x < 272) {
        int k = (x - 48) / 32;
        return y < PY + 16 ? top_keys[k] : bottom_keys[k];
    }
    if (!msg_on && x >= 49 && x < 106 && y >= PY + 33 && x != 77 && y != PY + 44)
        return PK_RESERVE + (x >= 78) + (y >= PY + 45) * 2;     /* reserve TU buttons */
    if (y >= HAND_Y && y < HAND_Y + 48) {
        if (x >= LHAND_X && x < LHAND_X + 32)
            return 0x101;
        if (x >= RHAND_X && x < RHAND_X + 32)
            return 0x102;
    }
    return 0;
}
