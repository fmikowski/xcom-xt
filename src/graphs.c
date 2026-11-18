/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The DOS Graphs screen (OpenXcom's GraphsState): UFO and X-COM activity
   by area and by country, the countries' funding (Income) and the
   finances, each over the last 12 months, with a toggle button for each
   line and the total. In the report section next to the geoscape; -zc:
   its strings stay in this section (tables are string literals). The history is g_bb->gr (base.h). */

#include <stdio.h>
#include <string.h>
#include "alien.h"
#include "alien_in.h"
#include "base.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "uik.h"

enum { GP_UFO_AREA, GP_UFO_COUNTRY, GP_XCOM_AREA, GP_XCOM_COUNTRY, GP_INCOME, GP_FINANCE };

#define BTN_H   11              /* toggle buttons: 88 x 11 down the left */
#define BASE_Y  175             /* the graph's zero line at the bottom */

static int page, rows, live_score;
static long live_maint;

/* Item n of a '|'-separated list. */
static const char *nth(const char *s, int n, char *out)
{
    int k = 0;

    while (n && *s)
        if (*s++ == '|')
            n--;
    while (*s && *s != '|' && k < 30)
        out[k++] = *s++;
    out[k] = 0;
    return out;
}

static unsigned long *toggles(void)
{
    return page == GP_UFO_AREA || page == GP_XCOM_AREA ? &g_bb->gr.rtog : &g_bb->gr.ctog;
}

static int pressed(int r)
{
    if (page == GP_FINANCE)
        return (g_bb->gr.ftog >> r) & 1;
    return (int)((*toggles() >> r) & 1);
}

/* Line r (rows: the total) of this page, m months back. */
static long value(int r, int m)
{
    const Graphs *g = &g_bb->gr;
    long v = 0;
    int i, area = page == GP_UFO_AREA || page == GP_XCOM_AREA;

    if (page == GP_FINANCE) {
        switch (r) {
        case 0: return g->income[m];
        case 1: return g->expend[m];
        case 2: return g->maint[m] + (m ? 0 : live_maint);
        case 3: return m ? g->balance[m] : g_base.funds;
        default: return m ? g->score[m] : live_score;
        }
    }
    if (r == rows) {
        for (i = 0; i < rows; i++)
            v += value(i, m);
        return v;
    }
    if (area)
        return (page == GP_UFO_AREA ? g->ralien : g->rxcom)[m][r];
    if (!m)     /* this month: alien.c's counts */
        return page == GP_UFO_COUNTRY ? al_cstate[r].alien
             : page == GP_XCOM_COUNTRY ? al_cstate[r].xcom : al_cstate[r].funding;
    return (page == GP_UFO_COUNTRY ? g->calien : page == GP_XCOM_COUNTRY ? g->cxcom
            : g->cfund)[m][r];
}

/* The line colours (EGA; CGA maps them to its four). */
static int colour(int r)
{
    if (page == GP_FINANCE)
        return "\x0E\x0C\x0B\x09\x0F"[r];
    return r == rows ? 15 : "\x0E\x0C\x0B\x0D\x09\x06\x04\x03\x05\x07\x01\x08\x0A\x02\x0E\x0C"[r & 15];
}

static void button(int r)
{
    char s[32];
    int y = r * BTN_H;

    if (page == GP_FINANCE)
        nth("Income|Expenditure|Maintenance|Balance|SCORE", r, s);
    else if (r == rows)
        strcpy(s, "TOTAL");
    else
        strcpy(s, page == GP_UFO_AREA || page == GP_XCOM_AREA ? al_regions[r].name
                  : al_countries[r].name);
    if (!pressed(r)) {
        uk_button(0, y, 88, BTN_H, s, 0);
        return;
    }
    /* ToggleTextButton pressed: in the line's colour */
    uk_fill(0, y, 88, BTN_H, colour(r));
    uk_center(0, 88, y + 1, UK_SMALL, 0 | UK_FLAT, s);
}

/* One segment, column by column (17 pixels wide). */
static void segment(int x, int y0, int y1, int c)
{
    int k, a, b;

    for (k = 0; k < 17; k++) {
        a = y0 + (y1 - y0) * k / 17;
        b = y0 + (y1 - y0) * (k + 1) / 17;
        if (a > b) {
            int t = a;
            a = b;
            b = t;
        }
        uk_fill(x + k, a, 1, b - a + 1, c);
    }
}

/* The grid, the scale and the pressed lines (GraphsState::drawLines). */
static void draw_lines(void)
{
    char s[16];
    long lower = 0, upper = 0, check, range, v;
    int r, m, n = page == GP_FINANCE ? 5 : rows + 1, x, y, py = 0, lines = 0;

    for (r = 0; r < n; r++)
        for (m = 0; m < GR_MONTHS && pressed(r); m++) {
            v = value(r, m);
            if (v > upper)
                upper = v;
            if (v < lower)
                lower = v;
        }
    check = page == GP_FINANCE ? 250 : page == GP_INCOME ? 50 : 10;
    while (upper - lower > check * 9)
        check *= 2;
    v = lower;
    lower = 0;
    upper = check * 9;
    while (v < lower) {
        lower -= check;
        upper -= check;
    }
    range = upper - lower;
    /* the grid: cells of 17 x 14 with a bevel */
    uk_fill(80, 45, 240, 137, 0);
    uk_fill(125, 49, 188, 127, 0);
    for (y = 50; y <= 163; y += 14)
        for (x = 126; x <= 297; x += 17) {
            uk_fill(x, y, 16, 13, 2);     /* dark green, as DOS's (graph 160) */
            uk_fill(x + 1, y + 1, 14, 11, 0);
        }
    for (r = 0; r < 10; r++) {
        sprintf(s, "%ld", lower + (range / 9 < 10 ? 10 : range / 9) * r);
        uk_text(122 - uk_width(UK_SMALL, s), 171 - r * 14, UK_SMALL, 10, s);
    }
    for (r = 0; r < n; r++) {
        if (!pressed(r))
            continue;
        lines++;
        for (m = 0; m < GR_MONTHS; m++) {
            y = (int)(BASE_Y + lower * 126 / range - value(r, m) * 126 / range);
            if (y > BASE_Y && page != GP_FINANCE)
                y = BASE_Y;
            if (m)
                segment(312 - m * 17, y, py, colour(r));
            py = y;
        }
    }
    log_printf("GRAPHS %s: %d lines, scale %ld to %ld",
               nth("UFO areas|UFO countries|XCOM areas|XCOM countries|Income|Finance", page, s),
               lines, lower, upper);
}

/* The whole screen for page p. */
static void show(int p, int month, int year)
{
    char s[32];
    int i, w, h;

    page = p;
    rows = page == GP_FINANCE ? 5 : page == GP_UFO_AREA || page == GP_XCOM_AREA
           ? al_n_regions : al_n_countries;
    if (page != GP_FINANCE && rows > 16)
        rows = 16;
    uk_screen(UK_NOBG, T_GREY);
    if (g_video == VIDEO_CGA)
        uk_cga_palette(1);
    /* the seven icon buttons (GRAPHS.SPK); older packs: plain buttons */
    if (!(uk_pic_box(UI_GRAPHS, &w, &h) && w == 224 && h == 24 && uk_pic(UI_GRAPHS, -1, 0)))
        for (i = 0; i < 7; i++)
            uk_button(96 + i * 32, 0, 32, 24, nth("UA|UC|XA|XC|Inc|Fin|Geo", i, s), 0);
    for (i = 0; i < (page == GP_FINANCE ? 5 : rows + 1); i++)
        button(i);
    uk_center(90, 230, 28, UK_BIG, 12,
              nth("UFO Activity in Areas|UFO Activity in Countries|X-Com Activity in Areas|"
                  "X-Com Activity in Countries|Income|Finance", page, s));
    if (page == GP_INCOME || page == GP_FINANCE)
        uk_text(96, 28, UK_SMALL, 12, "$1000's");
    /* the months, from 11 months ago to this one; the years under them */
    for (i = 0; i < 12; i++) {
        int mo = (month + 1 + i) % 12;
        uk_text(115 + i * 17, 183, UK_SMALL, 10, nth("Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec",
                                                     mo, s));
        if (!mo) {
            sprintf(s, "%d", year);
            uk_text(121 + i / 2 * 34, 191, UK_SMALL, 10, s);
            if (i > 2) {
                sprintf(s, "%d", year - 1);
                uk_text(121, 191, UK_SMALL, 10, s);
            }
        }
    }
    draw_lines();
}

void graphs_screen(int month, int year, int score)
{
    int b, cur = g_bcur;

    if (!g_bb || !al_world)
        return;
    /* this month's maintenance so far: what every base would pay now */
    live_maint = 0;
    for (b = 0; b < g_nbases; b++) {
        base_select(b);
        live_maint += base_monthly_costs();
    }
    base_select(cur);
    live_score = score;
    uk_open();
    uk_invalidate();
    show(GP_UFO_AREA, month, year);
    for (;;) {
        unsigned key = input_key();
        int ch = key & 0xFF, mx = g_mouse_x, my = g_mouse_y;
        if (ch == 27 || ch == 13 || ch == 'g' || ch == 'G')
            break;
        if (key != KEY_LCLICK)
            continue;
        if (my < 24 && mx >= 96) {
            if ((mx - 96) / 32 == 6)
                break;
            show((mx - 96) / 32, month, year);
        } else if (mx < 88 && my < 17 * BTN_H) {
            int r = my / BTN_H, n = page == GP_FINANCE ? 5 : rows + 1;
            if (r >= n)
                continue;
            if (page == GP_FINANCE)
                g_bb->gr.ftog ^= (unsigned char)(1 << r);
            else
                *toggles() ^= 1UL << r;
            button(r);
            draw_lines();
        }
    }
    uk_close();
    hud_clear_rows(0, 25);
}

/* Month end (SavedGame::monthlyFunding, Country / Region::newMonth): the
   month's values are kept and everything moves a month back; the new
   month starts with the funding as income and the maintenance as
   expenditure. */
static void shift(void *a, unsigned row)
{
    memmove((char *)a + row, a, row * (GR_MONTHS - 1));
    memset(a, 0, row);
}

void graphs_month(long costs, long funding, int score)
{
    Graphs *g;
    int i;

    if (!g_bb)
        return;
    g = &g_bb->gr;
    for (i = 0; i < al_n_countries && i < GR_COUNTRIES; i++) {
        g->calien[0][i] = al_cstate[i].alien;
        g->cxcom[0][i] = al_cstate[i].xcom;
        g->cfund[0][i] = (int)al_cstate[i].funding;
    }
    g->maint[0] = costs;
    g->score[0] = score;
    g->balance[0] = g_base.funds + funding - costs;
    shift(g->ralien, sizeof g->ralien[0]);
    shift(g->rxcom, sizeof g->rxcom[0]);
    shift(g->calien, sizeof g->calien[0]);
    shift(g->cxcom, sizeof g->cxcom[0]);
    shift(g->cfund, sizeof g->cfund[0]);
    shift(g->income, sizeof g->income[0]);
    shift(g->expend, sizeof g->expend[0]);
    shift(g->maint, sizeof g->maint[0]);
    shift(g->balance, sizeof g->balance[0]);
    shift(g->score, sizeof g->score[0]);
    g->income[0] = funding;
    g->expend[0] = costs;
    g->funds = g->balance[1];
}
