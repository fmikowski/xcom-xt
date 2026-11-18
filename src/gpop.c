/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The DOS completion pop-ups over the geoscape, laid out as OpenXcom's
   ResearchCompleteState, NewPossibleResearchState,
   NewPossibleManufactureState and ProductionCompleteState (also for
   construction). Each returns what the player chose; the geoscape then
   opens the UFOpaedia article, the research or manufacture screen or the
   basescape (other sections). An overlay section of its own, compiled
   with -zc: strings stay in its code. Window x positions are on whole
   bytes so the background stays inside the frame. */

#include <stdio.h>
#include <string.h>
#include "base.h"
#include "facil.h"
#include "input.h"
#include "inv.h"
#include "uik.h"
#include "unit.h"
#include "gpop.h"

#define YELLOW      14
#define WHITE       15
#define LIST_ROWS   6

typedef struct { int x, y, w, h; unsigned char key, key2; } Hit;

static void win(unsigned bg, int x, int y, int w, int h)
{
    int pw, ph;

    uk_open();
    uk_theme(T_GREEN);
    uk_save(0, 200);
    if (!(uk_pic_box(bg, &pw, &ph) && pw == 320 && ph == 200))
        bg = UI_BG_MENU;
    if (uk_pic_clean(bg))
        uk_restore(x, y, w, h);
    else
        uk_fill(x, y, w, h, 0);
    uk_window(x, y, w, h);
}

static void btn(Hit *b, int x, int y, int w, int h, int key, int key2, const char *s)
{
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->key = (unsigned char)key;
    b->key2 = (unsigned char)key2;
    uk_button(x, y, w, h, s, 0);
}

/* Waits for one of two buttons (a click or its keys); a right click is the
   first (OK). scroll: Up / Down call it with -1 / 1 (lists). */
static int wait2(const Hit *b, void (*scroll)(int))
{
    for (;;) {
        unsigned key = input_key();
        int i, ch;
        if (key == KEY_RCLICK)
            return 0;
        if (key == KEY_LCLICK) {
            for (i = 0; i < 2; i++)
                if (g_mouse_x >= b[i].x && g_mouse_x < b[i].x + b[i].w && g_mouse_y >= b[i].y
                    && g_mouse_y < b[i].y + b[i].h) {
                    uk_button(b[i].x, b[i].y, b[i].w, b[i].h, "", 1);
                    return i;
                }
            continue;
        }
        if (scroll && ((key >> 8) == 0x48 || (key >> 8) == 0x50)) {
            scroll((key >> 8) == 0x48 ? -1 : 1);
            continue;
        }
        ch = key & 0xFF;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        for (i = 0; i < 2; i++)
            if (ch && (ch == b[i].key || ch == b[i].key2))
                return i;
    }
}

/* Big text centred in [x, x + w), words wrapped; the lines go down from y
   (16 pixels each). Returns the line count (or only counts: draw 0). */
static int big_text(int x, int w, int y, const char *s, int draw)
{
    char line[41];
    int n = 0;

    while (*s) {
        int len = 0, cut = 0;
        while (s[len] && len < 40) {
            memcpy(line, s, len + 1);
            line[len + 1] = 0;
            if (uk_width(UK_BIG, line) > w)
                break;
            len++;
            if (s[len] == ' ' || !s[len])
                cut = len;
        }
        if (!cut)
            cut = len ? len : 1;
        memcpy(line, s, cut);
        line[cut] = 0;
        if (draw)
            uk_center(x, w, y + n * 16, UK_BIG, YELLOW, line);
        n++;
        s += cut;
        while (*s == ' ')
            s++;
    }
    return n;
}

/* ResearchCompleteState: 230 x 140 at 45, 30 (here 48: whole bytes).
   1 = View Reports (Enter), 0 = OK (Esc). */
int gpop_research(int topic)
{
    Hit b[2];
    int r;

    win(UI_BG_RESEARCH, 48, 30, 224, 140);
    uk_center(48, 224, 70, UK_BIG, g_uk->text, "Research Completed");
    big_text(48, 224, 96, g_topics[topic].name, 1);
    btn(&b[0], 64, 146, 80, 16, 27, 'o', "OK");
    btn(&b[1], 176, 146, 80, 16, 13, 'v', "VIEW REPORTS");
    r = wait2(b, NULL);
    log_printf("WIN Research Completed: %s: %s", g_topics[topic].name, r ? "View Reports" : "OK");
    uk_close();
    return r;
}

/* The list of a NewPossible* window: up to LIST_ROWS rows from top, Up /
   Down scroll. */
static char (*rows)[41];
static int n_rows, top;

static void draw_rows(int d)
{
    int i;

    if (d && (top + d < 0 || top + d + LIST_ROWS > n_rows))
        return;
    top += d;
    uk_restore(32, 50, 256, LIST_ROWS * 16);
    for (i = 0; i < LIST_ROWS && top + i < n_rows; i++)
        uk_center(35, 250, 50 + i * 16, UK_BIG, YELLOW, rows[top + i]);
}

/* NewPossibleResearchState / NewPossibleManufactureState: 288 x 180 at
   16, 10. 1 = Allocate (Enter), 0 = OK (Esc). */
static int possible(unsigned bg, const char *title, const char *alloc, char (*ln)[41], int n)
{
    Hit b[2];
    int r;

    win(bg, 16, 10, 288, 180);
    uk_center(16, 288, 20, UK_BIG, g_uk->text, title);
    btn(&b[0], 80, 149, 160, 14, 27, 'o', "OK");
    btn(&b[1], 80, 165, 160, 14, 13, 'a', alloc);
    uk_save(0, 200);
    rows = ln;
    n_rows = n;
    top = 0;
    draw_rows(0);
    r = wait2(b, draw_rows);
    log_printf("WIN %s: %d, %s", title, n, r ? alloc : "OK");
    uk_close();
    return r;
}

/* The topics that topic t opened at the current base: available now, not
   while t was not done, not listed before in this round. -1 = none (no
   window), else as possible(). */
int gpop_new_research(int t, char (*ln)[41])
{
    unsigned char *pp = g_bb->popped;
    int u, n = 0;

    for (u = 0; u < N_TOPICS; u++) {
        int was;
        if ((pp[u >> 3] >> (u & 7)) & 1 || !base_topic_available(u))
            continue;
        g_base.done[t] = 0;
        was = base_topic_available(u);
        g_base.done[t] = 1;
        if (was)
            continue;
        pp[u >> 3] |= 1 << (u & 7);
        log_printf("WIN We can now research: %s", g_topics[u].name);
        strcpy(ln[n++], g_topics[u].name);
    }
    if (!n)
        return -1;
    return possible(UI_BG_RESEARCH, "We can now research", "Allocate Research", ln, n);
}

/* The items topic t made producible (SavedGame::getDependableManufacture). */
int gpop_new_manufacture(int t, char (*ln)[41])
{
    int r, n = 0;

    for (r = 0; r < N_RECIPES; r++) {
        int it = g_recipes[r].item;
        if (!base_recipe_known(r)
            || (g_recipes[r].topic != t
                && !(t == RS_NEW_FIGHTER && (it == IT_HOVER_PLASMA || it == IT_HOVER_LAUNCHER
                                             || it == IT_HWP_FUSION))))
            continue;
        log_printf("WIN We can now produce: %s", g_items[it].name);
        strcpy(ln[n++], g_items[it].name);
    }
    if (!n)
        return -1;
    return possible(UI_BG_MANUF, "We can now produce", "Allocate Manufacture", ln, n);
}

/* ProductionCompleteState: 256 x 160 at 32, 20; the message in 246 x 110
   at 37, 35, centred both ways. 1 = Allocate Manufacture / Go to Base,
   0 = OK (Esc). */
int gpop_production(int kind, int id, int base)
{
    /* no named table: in this -zc section it would get an overlay vector
       (docs/design.md, Research and manufacturing) */
    const char *head = kind == POP_PROD_DONE ? "Production of"
                     : kind == POP_PROD_MONEY ? "Not enough money to produce"
                     : kind == POP_PROD_MATERIALS ? "Not enough special materials to produce"
                     : "Construction of";
    const char *what = kind == POP_BUILT ? g_fdefs[id].name : g_items[g_recipes[id].item].name;
    const char *go = kind == POP_BUILT ? "Go to Base" : "Allocate Manufacture";
    Hit b[2];
    int n, y, r;

    win(UI_BG_MANUF, 32, 20, 256, 160);
    n = big_text(37, 246, 0, head, 0) + big_text(37, 246, 0, what, 0) + 2
        + (kind == POP_PROD_DONE || kind == POP_BUILT);
    y = 35 + (110 - n * 16) / 2;
    y += 16 * big_text(37, 246, y, head, 1);
    y += 16 * big_text(37, 246, y, what, 1);
    uk_center(37, 246, y, UK_BIG, YELLOW, "at");
    uk_center(37, 246, y + 16, UK_BIG, YELLOW, g_bhdr[base].name);
    if (kind == POP_PROD_DONE || kind == POP_BUILT)
        uk_center(37, 246, y + 32, UK_BIG, YELLOW, "is complete");
    btn(&b[0], 40, 154, 118, 18, 27, 'o', "OK");
    btn(&b[1], 162, 154, 118, 18, kind == POP_BUILT ? 'g' : 'a', 0, go);
    r = wait2(b, NULL);
    log_printf("WIN %s %s at %s: %s", head, what, g_bhdr[base].name, r ? go : "OK");
    uk_close();
    return r;
}

/* ItemsArrivingState: 320 x 184 at 0, 8: the item, quantity and base of every
   transfer that came in this step (the list shows 14). */
int gpop_arrivals(int k)
{
    Hit b[2];
    PopEvent *e;
    char t[24];
    int i, n = 0, r, last = 0;

    win(UI_BG_REPORT, 0, 8, 320, 184);
    uk_center(0, 320, 18, UK_BIG, g_uk->title, "Items Arriving");
    uk_text(16, 34, UK_SMALL, g_uk->text, "ITEM");
    uk_text(152, 34, UK_SMALL, g_uk->text, "QUANTITY");
    uk_text(212, 34, UK_SMALL, g_uk->text, "DESTINATION");
    for (i = k; i < g_bb->n_pop; i++) {
        e = &g_bb->pop[i];
        if (e->kind != POP_ARRIVAL)
            continue;
        e->kind = POP_USED;
        last = e->base;
        if (n++ >= 14)
            continue;
        uk_text(16, 42 + n * 8, UK_SMALL, g_uk->list,
                e->id == TR_SCIENTIST ? "Scientists" : e->id == TR_ENGINEER ? "Engineers"
                : e->id == TR_SOLDIER ? "Soldier" : g_items[e->id - 1].name);
        sprintf(t, "%u", e->n);
        uk_text(171, 42 + n * 8, UK_SMALL, g_uk->list, t);
        uk_text(212, 42 + n * 8, UK_SMALL, g_uk->list, g_bhdr[e->base].name);
    }
    btn(&b[0], 16, 166, 142, 16, 27, 'o', "OK");
    btn(&b[1], 162, 166, 142, 16, 13, 'g', "Go to Base");
    log_printf("WIN Items Arriving: %d rows", n);
    r = g_popup_auto ? 0 : wait2(b, NULL);
    log_printf("WIN Items Arriving: %s", r ? "Go to Base" : "OK");
    uk_close();
    return r ? last : -1;
}

/* ------------------------------------------------------------------ */
/* Debriefing points (DebriefingState::prepareDebriefing): aliens killed
   score their value (units.rul, by race and rank), live captures twice
   it, corpses 5 (terror units 3); a soldier lost costs 20 + missions +
   rank bonus. The value table as a string literal (a named table of a
   -zc section reads garbage through a far pointer, docs/design.md):
   commander, leader, engineer, medic, navigator, soldier, terror units
   1 and 2, by race (sectoid; cyberdisc .. mixed). */
#define ALIEN_VALUE \
    "\x19\x14\x10\x0E\x0C\x0A\x14\x14" "\x23\x19\x12\x10\x0E\x0C\x19\x19" \
    "\x24\x1C\x14\x11\x11\x0F\x19\x12" "\x13\x13\x13\x11\x11\x0F\x14\x14" \
    "\x28\x1E\x14\x14\x14\x14\x1E\x1E" "\x28\x1E\x13\x11\x11\x0F\x19\x19"

static void stat(DebStats *ds, int k, int qty, int score)
{
    ds->qty[k] += qty;
    ds->score[k] += score;
}

int gpop_soldier_points(int res, void *stats)
{
    DebStats *ds = stats;
    int i, pts = 0, p;

    for (i = 0; i < g_nunits; i++) {
        const Unit *u = &g_units[i];
        if (u->faction == FACTION_XCOM && u->roster >= 0 && !u->alive
            && (!u->stunned || res != 1)) {
            p = 20 + g_roster[u->roster].missions + "\0\0\1\3\6\12"[g_roster[u->roster].rank % 6];
            stat(ds, DS_SOLDIERS, 1, -p);
            pts -= p;
        } else if (u->faction == FACTION_XCOM && UNIT_HWP(u) && !u->alive) {
            stat(ds, DS_TANKS, 1, -20);     /* units.rul value */
            pts -= 20;
        }
    }
    return pts;
}

int gpop_alien_points(int won, int race, void *stats)
{
    DebStats *ds = stats;
    int i, pts = 0, live = 0, v, c;

    if (race < 0 || race > 5)
        race = 0;
    for (i = 0; i < g_nunits; i++) {
        const Unit *u = &g_units[i];
        if (u->faction != FACTION_ALIEN || u->alive)
            continue;
        v = (unsigned char)ALIEN_VALUE[race * 8 + (u->rank & 7)];
        c = u->rank < 6 ? 5 : 3;
        if (!u->stunned) {
            stat(ds, DS_KILLED, 1, v);
            pts += v;
            if (won && !(u->special & SP_GONE)) {     /* the corpse not blown up */
                stat(ds, DS_CORPSES, 1, c);
                pts += c;
            }
        } else if (won && !(u->special & SP_GONE)) {   /* (not left outside by an abort) */
            if (live++ < g_captured) {
                stat(ds, DS_LIVE, 1, 2 * v);
                pts += 2 * v;
            } else {
                stat(ds, DS_CORPSES, 1, 5);
                pts += 5;
            }
        }
    }
    return pts;
}

int gpop_survivors(void)
{
    int i, n = 0;

    for (i = 0; i < g_nunits; i++)
        if (g_units[i].faction == FACTION_XCOM
            && (g_units[i].roster >= 0 || UNIT_HWP(&g_units[i]))
            && (g_units[i].alive || g_units[i].stunned))
            n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* DebriefingState: window 320 x 200, title at 16, 8 (big), ITEM /
   QUANTITY / SCORE at y 24, the stats from y 32 (8 a line: name at 16,
   quantity at 240, score at 270), "UFO RECOVERY" and its list below,
   TOTAL, the rating at 64, 180 and OK (40 x 12) at 16, 180. */

static const char *ds_name(int k)
{
    switch (k) {
    case DS_KILLED: return "ALIENS KILLED";
    case DS_CORPSES: return "ALIEN CORPSES RECOVERED";
    case DS_LIVE: return "LIVE ALIENS RECOVERED";
    case DS_ARTIFACTS: return "ALIEN ARTIFACTS RECOVERED";
    case DS_BASE_CONTROL: return "ALIEN BASE CONTROL DESTROYED";
    case DS_CIV_ALIENS: return "CIVILIANS KILLED BY ALIENS";
    case DS_CIV_XCOM: return "CIVILIANS KILLED BY X-COM OPERATIVES";
    case DS_CIV_SAVED: return "CIVILIANS SAVED";
    case DS_SOLDIERS: return "X-COM OPERATIVES KILLED";
    case DS_TANKS: return "TANKS DESTROYED";
    case DS_CRAFT: return "X-COM CRAFT LOST";
    case DS_POWER: return "UFO Power Source";
    case DS_NAV: return "UFO Navigation";
    case DS_CONSTR: return "UFO Construction";
    case DS_FOOD: return "Alien Food";
    case DS_REPRO: return "Alien Reproduction";
    case DS_ENTERT: return "Alien Entertainment";
    case DS_SURGERY: return "Alien Surgery";
    case DS_EXAM: return "Examination Room";
    case DS_ALLOYS: return "Alien Alloys";
    case DS_HABITAT: return "Alien Habitat";
    }
    return "Elerium-115";
}

static void ds_line(int y, const char *name, int qty, int score, int show_qty)
{
    char t[8];

    uk_text(16, y, UK_SMALL, g_uk->list, name);
    if (show_qty) {
        sprintf(t, "%d", qty);
        uk_text(240, y, UK_SMALL, WHITE, t);
    }
    sprintf(t, "%d", score);
    uk_text(270, y, UK_SMALL, WHITE, t);
}

void gpop_debrief(int mission, int res, int craft_lost, const void *stats)
{
    const DebStats *d = (const DebStats *)stats;
    const char *title, *rating;
    int k, y = 32, total = 0, rec = 0;

    if (res == 1)
        title = mission == DB_BASE ? "Base is saved" : mission == DB_UFO ? "UFO is recovered"
              : mission == DB_ALIEN_BASE ? "Alien Base destroyed" : "Aliens defeated";
    else
        title = craft_lost ? "Craft is lost" : mission == DB_BASE ? "Base is lost"
              : mission == DB_UFO ? "UFO is not recovered"
              : mission == DB_ALIEN_BASE ? "Alien Base still intact" : "Terror continues";
    uk_page(UI_BG_MENU, T_GREEN, "");
    uk_center(16, 288, 8, UK_BIG, g_uk->title, title);
    uk_text(16, 24, UK_SMALL, g_uk->text, "ITEM");
    uk_text(200, 24, UK_SMALL, g_uk->text, "QUANTITY");
    uk_text(270, 24, UK_SMALL, g_uk->text, "SCORE");
    log_printf("DEBRIEF %s", title);
    for (k = 0; k < DS_N; k++) {
        if (!d->qty[k])
            continue;
        total += d->score[k];
        if (k >= DS_POWER && !rec++) {
            uk_text(16, y + 5, UK_SMALL, g_uk->text, "UFO RECOVERY");
            y += 13;
        }
        ds_line(y, ds_name(k), d->qty[k], d->score[k], 1);
        log_printf("DEBRIEF %s %d %d", ds_name(k), d->qty[k], d->score[k]);
        y += 8;
    }
    rating = total <= -200 ? "TERRIBLE!" : total <= 0 ? "POOR!" : total <= 200 ? "OK"
           : total <= 500 ? "GOOD!" : "EXCELLENT!";
    ds_line(y + 5, "TOTAL", 0, total, 0);
    uk_text(uk_text(64, 182, UK_SMALL, g_uk->text, "RATING> "), 182, UK_SMALL, WHITE, rating);
    log_printf("DEBRIEF TOTAL %d RATING %s", total, rating);
    uk_button(16, 180, 40, 12, "OK", 0);
    input_key();
    uk_close();
}
