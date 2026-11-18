/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The base screens (the DOS layouts, src/uik.c): the basescape and its
   equipment screen, the hangar's craft list and the debug grant. Research
   and manufacture are labscr.c's; Purchase/Recruit, Sell, Stores and
   Transfers (the quantity screens) trade.c's, which the geoscape runs for
   the basescape. A rarely used overlay section of its own: loops over list
   lines only read data, calls into the hourly section (base.c, facil.c,
   craft.c) happen per action. Compiled with -zc: its tables and strings
   stay in the section's code, so tab stops are string literals. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alien.h"
#include "base.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "gfx.h"
#include "hwp.h"
#include "hud.h"
#include "input.h"
#include "inv.h"
#include "map.h"
#include "psi.h"
#include "qlist.h"
#include "uik.h"
#include "unit.h"

#define TABS(s)     ((const unsigned char *)(s))

/* ------------------------------------------------------------------ */
/* Screens (the DOS layouts, src/uik.c) */

/* The screens' list lines: on the heap while the base screen is open. */
#define MAX_LINES MAX_ROSTER     /* Equip Squad: the base's soldiers */
#define lines g_scr_lines        /* xfer.c */

/* Armour (SoldierArmorState): none -> personal armour -> power suit ->
   none, as the stores allow. The soldiers' weapons and kit are the craft's
   (Equip Craft in the hangar); the list shows what each takes by the
   equipment layout of the last battle. */
static int equip_key(int sel, int ch, int scan)
{
    Soldier *s = &g_roster[sel];
    int a;
    (void)scan;

    if (ch != 'a')
        return 0;
    if (s->armour)
        g_base.stores[g_armours[s->armour].item]++;
    for (a = s->armour + 1; a < N_ARMOURS; a++)
        if (g_base.stores[g_armours[a].item])
            break;
    if (a >= N_ARMOURS)
        a = 0;
    if (a)
        g_base.stores[g_armours[a].item]--;
    s->armour = (unsigned char)a;
    log_printf("BASE equip %s: %s", s->name, a ? g_armours[a].name : "-");
    return 2;                   /* the lines are made again */
}

static const UkMenu equip_menu = {
    UI_BG_EQUIP, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    "Equip Squad", "Soldier\tArmour\tLayout", TABS("\x64\xB4"), "A=Armour|Esc=OK", 178, NULL
};

static void equip_screen(void)
{
    int sel = 0, i, n;

    for (;;) {
        for (i = 0; i < g_roster_n; i++) {
            const Soldier *s = &g_roster[i];
            for (n = 0; n < LAYOUT_N && s->layout[n][0] != NO_ITEM; n++)
                ;
            sprintf(lines[i], n ? "%s\t%s\t%.12s +%d" : "%s\t%s\t-", s->name,
                    s->armour ? (s->armour == 1 ? "Armour" : g_armours[s->armour].name) : "-",
                    g_items[s->layout[0][0]].name, n - 1);
        }
        sel = ql_menu(&equip_menu, NULL, lines, g_roster_n, sel, equip_key, NULL);
        if (sel < 0)
            return;
    }
}

/* Room left in the stores during debug_grant, in tenths of a unit. */
static int grant_room;

/* At least n of an item in stores, as far as they fit (debug). */
static void top_up(int item, unsigned n)
{
    unsigned have = g_base.stores[item], size = g_item_size[item];

    if (have >= n)
        return;
    n -= have;
    if (n > (unsigned)grant_room / size)
        n = (unsigned)grant_room / size;
    g_base.stores[item] = have + n;
    grant_room -= n * size;
}

/* DEBUG option: stores topped up, as far as they fit (the test kit
   first): two small launchers and 12 stun bombs, a blaster launcher and
   bomb, 2 corpses of each race, UFO parts (2 power sources and
   navigation, 1 of the others), 50 Elerium, 130 alloys (a base with one
   General Stores has no room left for them: ENDGAME builds a second); a live
   navigator; funds to $5000K; the base's research projects and
   construction finish the next midnight (projects with scientists), each
   production run's next unit in the next hour (runs with engineers). Tests the
   economy and the craft chain without months of play. */
static void debug_grant(void)
{
    int t, x, y;

    grant_room = facil_capacity(CAP_STORES) * 10 - facil_stores_used();
    if (grant_room < 0)
        grant_room = 0;
    top_up(IT_LAUNCHER, 2);             /* stun captures in campaign tests */
    top_up(IT_STUNBOMB, 12);
    top_up(IT_BLASTER, 1);
    top_up(IT_BLASTERBOMB, 1);
    for (t = IT_CORPSE_SECTOID; t <= IT_CORPSE_ETHEREAL; t++)
        top_up(t, 2);
    for (t = IT_UFO_POWER; t <= IT_ALIEN_REPRO; t++)
        top_up(t, t <= IT_UFO_NAV ? 2 : 1);
    top_up(IT_ELERIUM, 50);
    top_up(IT_ALLOYS, 130);
    /* DEBUG only: a live sectoid commander, leader and navigator for the
       story's interrogations (The Martian Solution, Cydonia or Bust, the
       Hyper-wave Decoder). */
    for (t = 0; t < 5; t += t ? 3 : 1)
        if (!g_base.done[RS_INT + t] && !g_base.stores[IT_LIVE + t])
            g_base.stores[IT_LIVE + t] = 1;
    if (g_base.funds < 5000)
        g_base.funds = 5000;
    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++)
            if (g_facil.days[y][x] > 1)
                g_facil.days[y][x] = 1;
    for (t = 0; t < BASE_REC->n_res; t++) {
        ResProj *p = &BASE_REC->res[t];
        if (p->spent + 1 < p->cost)
            p->spent = p->cost - 1;
    }
    for (t = 0; t < BASE_REC->n_prod; t++) {
        ProdRun *p = &BASE_REC->prod[t];
        if (p->hours + 1 < g_recipes[PROD_RCP(p)].hours)
            p->hours = g_recipes[PROD_RCP(p)].hours - 1;
    }
    log_printf("DEBUG grant: funds $%ldK, stores %d of %d", g_base.funds,
               (facil_stores_used() + 9) / 10, facil_capacity(CAP_STORES));
}


/* ------------------------------------------------------------------ */
/* The basescape (BasescapeState): the base seen from above on the left; on
   the right the base's name, its region and the funds, the eight bases'
   small views and eleven buttons, one click each. Keys (DOS has none): a
   digit 1-8 shows that base, the arrows and Enter use the buttons, letters
   type a name (type-ahead; the screens' older names too: Hangar, Equip
   Squad (armour), Stores, Transfers, Psi Training, Select Base, Debug). */

#define RX          192         /* the right side */
#define BTN_Y       58
#define BTN_PITCH   13
#define N_BTN       11
#define MINI_Y      41

/* The buttons from the top, then the screens only the keyboard reaches. */
enum { BM_NEW, BM_INFO, BM_SOLDIERS, BM_CRAFT, BM_FACIL, BM_RESEARCH, BM_MAKE, BM_TRANSFER,
       BM_BUY, BM_SELL, BM_GEO, BM_STORES, BM_XFERS, BM_PSI, BM_SELECT, BM_DEBUG, BM_DISMANTLE,
       BM_EQUIPSQ, BM_ALIENS };

static const char *btn_label(int k)
{
    switch (k) {
    case BM_NEW: return "BUILD NEW BASE";
    case BM_INFO: return "BASE INFORMATION";
    case BM_SOLDIERS: return "SOLDIERS";
    case BM_CRAFT: return "EQUIP CRAFT";
    case BM_FACIL: return "BUILD FACILITIES";
    case BM_RESEARCH: return "RESEARCH";
    case BM_MAKE: return "MANUFACTURE";
    case BM_TRANSFER: return "TRANSFER";
    case BM_BUY: return "PURCHASE/RECRUIT";
    case BM_SELL: return "SELL/SACK";
    }
    return "GEOSCAPE";
}

/* Type-ahead names in search order (older test scripts type these) and
   what each one is (one string: this section's tables are code). */
#define NAMES "Build Facilities|Build New Base|Base Information|Equip Squad|Soldiers|Equip Craft|" \
              "Research|Manufacture|Transfer|Purchase/Recruit|Sell/Sack|Geoscape|Hangar|Stores|" \
              "Transfers|Psi Training|New Base|Select Base|Debug: loot, jobs"
#define NB_NAMES 19

static int name_act(int e)
{
    static const unsigned char acts[NB_NAMES] = {
        BM_FACIL, BM_NEW, BM_INFO, BM_EQUIPSQ, BM_SOLDIERS, BM_CRAFT, BM_RESEARCH, BM_MAKE,
        BM_TRANSFER, BM_BUY, BM_SELL, BM_GEO, BM_CRAFT, BM_STORES, BM_XFERS, BM_PSI, BM_NEW,
        BM_SELECT, BM_DEBUG
    };
    return acts[e];
}

/* 1 if name entry e starts with the typed letters. */
static int name_starts(int e, const char *typed, int len)
{
    const char *p = NAMES;
    int i;

    while (e-- > 0) {
        while (*p++ != '|')
            ;
    }
    for (i = 0; i < len; i++) {
        int c = p[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (!c || c == '|' || c != typed[i])
            return 0;
    }
    return 1;
}

static int shown_cell = -2;     /* the hover text's square, -1 none */

/* The craft standing in the hangar at cell (y * 8 + x), a g_craft slot, or
   -1 (BaseFacility::getCraft: the craft stand in the hangars in the order
   of both lists). */
static int cell_craft(int cell)
{
    unsigned char c = g_facil.cell[cell >> 3][cell & 7];
    const unsigned char *f = &g_facil.cell[0][0], *d = &g_facil.days[0][0];
    int k = 0, n = (cell >> 3) * BASE_W + (cell & 7) - (FAC_PART(c) & 1) - BASE_W * (FAC_PART(c) >> 1);

    if (FAC_TYPE(c) != FAC_HANGAR || d[n])
        return -1;
    while (n-- > 0)
        k += FAC_TYPE(f[n]) == FAC_HANGAR && !FAC_PART(f[n]) && !d[n];
    for (n = 0; n < MAX_CRAFT; n++)
        if (CRAFT_HERE(&g_craft[n]) && g_craft[n].state == CS_HANGAR && k-- == 0)
            return n;
    return -1;
}

/* DOS shows the facility under the mouse; a hangar its craft too. */
static void hover_text(int cell)
{
    char t[40];
    int x = cell & 7, y = cell >> 3, j;
    unsigned char c;

    if (cell == shown_cell)
        return;
    shown_cell = cell;
    t[0] = 0;
    if (cell >= 0 && FAC_TYPE(c = g_facil.cell[y][x]) != FAC_NONE) {
        strcpy(t, g_fdefs[FAC_TYPE(c)].name);
        if ((j = cell_craft(cell)) >= 0)
            sprintf(t + strlen(t), " CRAFT> %s", craft_name(&g_craft[j]));
    }
    uk_fill(0, 0, RX, 8, 0);
    uk_text(2, 0, UK_SMALL, 11, t);
}

static void draw_head(void)
{
    char t[24], m[16];
    const BaseHdr *h = &g_bhdr[g_bcur];

    uk_text(RX + 2, 0, uk_width(UK_BIG, h->name) > 124 ? UK_SMALL : UK_BIG, 13, h->name);
    uk_text(RX + 2, 16, UK_SMALL, 5, alien_region_name(h->x, h->y));
    geo_money(m, g_base.funds, g_base.funds10);
    sprintf(t, "FUNDS> %s", m);
    uk_text(RX + 2, 24, UK_SMALL, 9, t);
}

/* MiniBaseView: a small square of each base (the selected one framed in
   white) with its facilities, green built, red under construction. */
static void draw_mini(void)
{
    int i, x, y;

    for (i = 0; i < MAX_BASES; i++) {
        int bx = RX + i * 16;
        uk_fill(bx, MINI_Y, 16, 16, i == g_bcur ? 15 : 0);
        uk_fill(bx + 1, MINI_Y + 1, 14, 14, g_video == VIDEO_CGA ? 0 : 6);
        if (i >= g_nbases)
            continue;
        {
            const Facilities *f = i == g_bcur ? &g_facil : &g_bb->rec[i].facil;
            for (y = 0; y < BASE_W; y++)
                for (x = 0; x < BASE_W; x++) {
                    unsigned char c = f->cell[y][x];
                    if (FAC_TYPE(c) != FAC_NONE && !FAC_PART(c)) {
                        int s = g_fdefs[FAC_TYPE(c)].size * 2;
                        uk_fill(bx + 2 + x * 2, MINI_Y + 2 + y * 2, s, s, f->days[y][x] ? 12 : 10);
                    }
                }
        }
    }
}

static void draw_btn(int k, int on)
{
    uk_button(RX, BTN_Y + k * BTN_PITCH, 128, 12, btn_label(k), on);
}

static void draw_base(int hl)
{
    int k;

    uk_cga_palette(2);
    uk_screen(UK_NOBG, T_GOLD);
    facil_draw_view(0, 8);
    draw_head();
    draw_mini();
    for (k = 0; k < N_BTN; k++)
        if (k != BM_NEW || g_nbases < MAX_BASES)
            draw_btn(k, k == hl);
    uk_save(0, 200);
    shown_cell = -2;
}

int g_base_cell;

/* The right click on a facility (viewRightClick): the screen of what it is. */
static int facility_act(int cell)
{
    int x = cell & 7, y = cell >> 3;

    switch (FAC_TYPE(g_facil.cell[y][x])) {
    case FAC_NONE: return BM_INFO;
    case FAC_HANGAR: return BM_CRAFT;
    case FAC_STORES: return BM_SELL;
    case FAC_QUARTERS: return BM_SOLDIERS;
    case FAC_LAB: return BM_RESEARCH;
    case FAC_WORKSHOP: return BM_MAKE;
    case FAC_PSILAB: return BM_PSI;
    case FAC_CONTAIN: return BM_ALIENS;
    case FAC_LIFT:
    case FAC_SRADAR:
    case FAC_LRADAR: return BM_GEO;
    }
    return -1;
}

/* Cell number (y * 8 + x) of the base view under the pointer, -1 none. */
static int cell_at_mouse(void)
{
    int cx = g_mouse_x / 32, cy = (g_mouse_y - 8) / 32;

    return g_mouse_x < RX && g_mouse_y >= 8 && cy < BASE_W && cx < BASE_W ? cy * 8 + cx : -1;
}

/* The button or screen a click chose: a BM_ number, -1 nothing, -2 the base
   changed or was renamed (everything is drawn again). */
static int click_act(unsigned key)
{
    int mx = g_mouse_x, my = g_mouse_y, i;

    if (key == KEY_RCLICK) {
        if (mx < RX && (i = cell_at_mouse()) >= 0) {
            g_base_cell = cell_craft(i);        /* a hangar's craft: its screen (BS_EQUIP) */
            i = facility_act(i);
            return i == BM_PSI && facil_capacity(CAP_PSI) <= 0 ? -1 : i;
        }
        return -1;
    }
    if (mx < RX) {
        i = cell_at_mouse();
        if (i >= 0 && FAC_TYPE(g_facil.cell[i >> 3][i & 7]) != FAC_NONE) {
            g_base_cell = i;
            return BM_DISMANTLE;
        }
    } else if (my < MINI_Y - 2) {
        char nm[16];
        strcpy(nm, g_bhdr[g_bcur].name);
        if (uk_edit("Name of the base:", nm, 15) && nm[0])
            strcpy(g_bhdr[g_bcur].name, nm);
        return -2;
    } else if (my >= MINI_Y && my < MINI_Y + 16) {
        i = (mx - RX) / 16;
        if (i < g_nbases && i != g_bcur) {
            base_select(i);
            log_printf("BASE %s selected", g_bhdr[g_bcur].name);
            return -2;
        }
    } else if (my >= BTN_Y && (my - BTN_Y) / BTN_PITCH < N_BTN && (my - BTN_Y) % BTN_PITCH < 12) {
        i = (my - BTN_Y) / BTN_PITCH;
        if (i != BM_NEW || g_nbases < MAX_BASES)
            return i;
    }
    return -1;
}

/* Screens in other overlay sections (research, manufacture, facilities,
   transfer, base list, psi training, base information) are left to the
   geoscape (BS_*), which comes back here: the basescape's section need not
   stay loaded under them. */
int base_screen(void)
{
    static int cur = -1;                /* the keyboard's choice: a name entry */
    char typed[12];
    int ntyped = 0, ret = BS_DONE, act, i, e, hl;

    lines = malloc(MAX_LINES * sizeof *lines);
    if (!lines)
        return BS_DONE;
    uk_open();
    for (;;) {
        log_printf("BASE menu: %d buttons%s", N_BTN, facil_capacity(CAP_PSI) ? ", psi training" : "");
        hl = cur >= 0 && name_act(cur) < N_BTN ? name_act(cur) : -1;
        draw_base(hl);
        for (act = -1; act == -1;) {
            unsigned key;
            int ch, scan;

            while (!input_ready())
                hover_text(g_mouse ? cell_at_mouse() : -1);
            key = input_key();
            ch = key & 0xFF;
            scan = key >> 8;
            g_base_cell = -1;
            if (key == KEY_LCLICK || key == KEY_RCLICK)
                act = click_act(key);
            else if (ch == 27)
                act = BM_GEO;
            else if (ch == '!' && g_debug) {
                g_base.done[RS_PSILAB] = g_base.done[RS_PSIAMP] = g_base.done[RS_MINDPROBE] = 1;
                g_base.stores[IT_PSIAMP] += 2;
                g_base.stores[IT_MINDPROBE] += 1;
                log_printf("PSI debug: research and kit granted");
            } else if (ch == '@' && g_debug) {
                /* three personal armours and a power suit (the craft screens' Armour) */
                g_base.stores[IT_ARMOUR] += 3;
                g_base.stores[IT_POWERSUIT]++;
                log_printf("BASE debug: armour granted");
            } else if (ch >= '1' && ch <= '0' + g_nbases && ch - '1' != g_bcur) {
                base_select(ch - '1');
                log_printf("BASE %s selected", g_bhdr[g_bcur].name);
                act = -2;
            } else if (ch == 13) {
                if (cur >= 0)
                    act = name_act(cur);
            } else if (scan == 0x48 || scan == 0x50) {
                /* Up and down walk the buttons. */
                i = hl < 0 ? (scan == 0x48 ? N_BTN : -1) : hl;
                i += scan == 0x48 ? -1 : 1;
                if (i < 0)
                    i = N_BTN - 1;
                if (i >= N_BTN)
                    i = 0;
                for (e = NB_NAMES; e-- > 0;)
                    if (name_act(e) == i)
                        cur = e;
                ntyped = 0;
                if (hl >= 0)
                    draw_btn(hl, 0);
                draw_btn(i, 1);
                hl = i;
            } else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
                       || (ntyped && (ch == ' ' || ch == '-' || ch == '/' || ch == ':'))) {
                int from = cur < 0 ? 0 : cur;
                if (ch >= 'A' && ch <= 'Z')
                    ch += 32;
                if (ntyped < 12)
                    typed[ntyped++] = (char)ch;
                for (e = 0; e < NB_NAMES; e++)
                    if (name_starts((from + e) % NB_NAMES, typed, ntyped))
                        break;
                if (e == NB_NAMES) {
                    /* Not a longer name: this letter alone, from the next entry on. */
                    ntyped = 1;
                    typed[0] = (char)ch;
                    from = cur < 0 ? 0 : cur + 1;
                    for (e = 0; e < NB_NAMES; e++)
                        if (name_starts((from + e) % NB_NAMES, typed, 1))
                            break;
                }
                if (e < NB_NAMES) {
                    cur = (from + e) % NB_NAMES;
                    i = name_act(cur) < N_BTN ? name_act(cur) : -1;
                    if (i != hl) {
                        if (hl >= 0)
                            draw_btn(hl, 0);
                        if (i >= 0)
                            draw_btn(i, 1);
                        hl = i;
                    }
                }
            }
        }
        if (act == -2)
            continue;                   /* the base changed or was renamed: draw it all */
        switch (act) {
        case BM_NEW: ret = BS_NEW_BASE; break;
        case BM_INFO: ret = BS_INFO; break;
        case BM_SOLDIERS: ret = BS_SOLDIERS; break;
        case BM_EQUIPSQ: equip_screen(); break;
        case BM_CRAFT: ret = BS_EQUIP; break;   /* the craft screens (src/hangar.c) */
        case BM_FACIL: ret = BS_FACIL; break;
        case BM_RESEARCH: ret = BS_RESEARCH; break;
        case BM_MAKE: ret = BS_MANUF; break;
        case BM_TRANSFER: ret = BS_TRANSFER; break;
        case BM_BUY: ret = BS_BUY; break;
        case BM_SELL: ret = BS_SELL; break;
        case BM_STORES: ret = BS_STORES; break;
        case BM_XFERS: ret = BS_XFERS; break;
        case BM_PSI:
            if (facil_capacity(CAP_PSI) > 0)
                ret = BS_PSI;
            break;
        case BM_SELECT: ret = BS_SELECT; break;
        case BM_DISMANTLE: ret = BS_DISMANTLE; break;
        case BM_ALIENS: ret = BS_ALIENS; break;
        case BM_DEBUG:
            if (g_debug)
                debug_grant();
            break;
        }
        if (act == BM_GEO) {
            cur = -1;
            break;
        }
        uk_invalidate();
        if (ret != BS_DONE)
            break;
    }
    uk_invalidate();
    uk_close();
    free(lines);
    lines = NULL;
    return ret;
}
