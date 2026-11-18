/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The DOS research and manufacture screens of the current base, after
   OpenXcom's ResearchState, NewResearchListState, ResearchInfoState,
   ManufactureState, NewManufactureListState, ManufactureStartState and
   ManufactureInfoState: several projects at once, scientists and
   engineers allocated to each. An overlay section of its own, run from
   the geoscape (base_screen returns BS_RESEARCH / BS_MANUF), compiled
   with -zc: its strings and tables stay in its code (valid while it is
   loaded; it only calls resident code and the hourly section, which is
   loaded under it). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "gfx.h"
#include "input.h"
#include "uik.h"

#define LAB_LINES   N_TOPICS    /* the longest list: new research topics */
#define WHITE       15
/* Column offsets as string literals: a named table that a far pointer in
   this section refers to gets an overlay vector from the linker (as if it
   were a function), and the pointer and every access then reach the
   vector instead of the table. */
#define TABS(s)     ((const unsigned char *)(s))

static int act;                 /* what an on_key asked for (ACT_*) */
enum { ACT_NONE, ACT_NEW, ACT_DBG };

/* ------------------------------------------------------------------ */
/* Helpers */

static int sci_free(void)
{
    return g_base.scientists - base_sci_allocated();
}

static int labs_free(void)
{
    return facil_capacity(CAP_LABS) - base_sci_allocated();
}

static int eng_free(void)
{
    return g_base.engineers - base_eng_allocated();
}

static int workshops_free(void)
{
    return facil_capacity(CAP_WORKSHOPS) - base_workshops_used();
}

/* "Label>" in the text colour, the value in white (OpenXcom {ALT}). */
static void label_value(int x, int y, int font, const char *label, const char *value)
{
    uk_text(uk_text(x, y, font, g_uk->text, label), y, font, WHITE, value);
}

static void label_num(int x, int y, int font, const char *label, long v)
{
    char t[12];

    sprintf(t, "%ld", v);
    label_value(x, y, font, label, t);
}

/* Money as OpenXcom's formatFunding: "$1,234,000". */
static char *money(char *buf, long dollars)
{
    char t[12];
    int n, i, k = 0;

    if (dollars < 0) {
        buf[k++] = '-';
        dollars = -dollars;
    }
    buf[k++] = '$';
    n = sprintf(t, "%ld", dollars);
    for (i = 0; i < n; i++) {
        if (i && (n - i) % 3 == 0)
            buf[k++] = ',';
        buf[k++] = t[i];
    }
    buf[k] = 0;
    return buf;
}

static long funds_dollars(void)
{
    return g_base.funds * 1000L + g_base.funds10 * 10L;
}

/* An ArrowButton (13 x 14): up or down. */
static void arrow(int x, int y, int up)
{
    int r;

    uk_button(x, y, 13, 14, NULL, 0);
    for (r = 0; r < 5; r++)
        uk_fill(x + 6 - r, up ? y + 4 + r : y + 9 - r, 2 * r + 1, 1, g_uk->btext);
}

static int inside(int x, int y, int w, int h)
{
    return g_mouse_x >= x && g_mouse_x < x + w && g_mouse_y >= y && g_mouse_y < y + h;
}

/* A popup window with the screen's picture inside (as uk_menu's
   UKS_POPUP); its static parts then become the clean copy. */
static void popup(unsigned bg, int theme, int x, int y, int w, int h)
{
    uk_invalidate();
    uk_theme(theme);
    uk_save(0, 200);
    if (uk_pic_clean(bg))
        uk_restore(x, y, w, h);
    else
        uk_fill(x, y, w, h, 0);
    uk_window(x, y, w, h);
}

/* ------------------------------------------------------------------ */
/* Research */

/* ResearchProject::getResearchProgress: none without scientists, unknown
   up to a third of the rules' cost, then by scientists per cost. */
static const char *progress_text(const ResProj *p)
{
    long cost = g_topics[p->topic].cost;

    if (!p->sci)
        return "None";
    if (p->spent * 1000L <= cost * 333)
        return "Unknown";
    if (p->sci * 100L <= cost * 7)
        return "Poor";
    if (p->sci * 100L <= cost * 13)
        return "Average";
    if (p->sci * 100L <= cost * 25)
        return "Good";
    return "Excellent";
}

/* ResearchInfoState: the scientists of project i (new: Start Project or
   Cancel, else OK or Cancel Project). */
static void research_alloc(int i, int isnew)
{
    ResProj *p = &BASE_REC->res[i];
    int t = p->topic, cancel = 0, first = 0;
    unsigned rep = 0;           /* an arrow button held (DOS: repeats) */

    popup(UI_BG_RESEARCH, T_BLUE, 45, 30, 230, 140);
    uk_text(61, 40, UK_BIG, g_uk->title, g_topics[t].name);
    uk_text(85, 100, UK_BIG, g_uk->text, "Increase");
    uk_text(85, 120, UK_BIG, g_uk->text, "Decrease");
    arrow(195, 100, 1);
    arrow(195, 120, 0);
    uk_button(61, 145, 90, 16, isnew ? "CANCEL" : "CANCEL PROJECT", 0);
    uk_button(169, 145, 90, 16, isnew ? "START PROJECT" : "OK", 0);
    uk_save(0, 200);
    for (;;) {
        unsigned key;
        int ch, scan, n = 0;

        uk_restore(56, 60, 216, 36);
        label_num(61, 60, UK_SMALL, "SCIENTISTS AVAILABLE>", sci_free());
        label_num(61, 70, UK_SMALL, "LABORATORY SPACE AVAILABLE>", labs_free());
        label_num(61, 80, UK_BIG, "Scientists Allocated>", p->sci);
        if (rep && input_held(first ? 250 : 50)) {
            key = rep;
            first = 0;
        } else {
            rep = 0;
            key = input_key();
        }
        if (key == KEY_LCLICK || key == KEY_RCLICK) {
            int all = key == KEY_RCLICK;
            if (inside(195, 100, 13, 14))
                key = all ? 'a' : (first = 1, rep = '+');
            else if (inside(195, 120, 13, 14))
                key = all ? 'z' : (first = 1, rep = '-');
            else if (all)
                key = 0x011B;
            else if (inside(61, 145, 90, 16))
                key = isnew ? 0x011B : 'c';
            else if (inside(169, 145, 90, 16))
                key = 0x1C0D;
            else
                continue;
        }
        ch = key & 0xFF;
        scan = key >> 8;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch == '+' || ch == '=' || scan == 0x48 || scan == 0x4D)
            n = 1;
        else if (ch == '-' || scan == 0x50 || scan == 0x4B)
            n = -1;
        else if (scan == 0x49)
            n = 10;
        else if (scan == 0x51)
            n = -10;
        else if (ch == 'a')
            n = 255;
        else if (ch == 'z')
            n = -255;
        else if (ch == 13)
            break;
        else if (ch == 27) {
            cancel = isnew;     /* Esc: Cancel a new project, OK an old one */
            break;
        } else if (ch == 'c' && !isnew) {
            cancel = 1;
            break;
        }
        /* moreByValue / lessByValue: free scientists and lab space. */
        if (n > 0) {
            int room = sci_free() < labs_free() ? sci_free() : labs_free();
            if (n > room)
                n = room;
            if (n > 0)
                p->sci += (unsigned char)n;
        } else if (n < 0)
            p->sci -= (unsigned char)(-n < p->sci ? -n : p->sci);
    }
    if (cancel)
        base_research_cancel(i);
    else
        log_printf("BASE research %s: %d scientists", g_topics[t].name, p->sci);
    uk_invalidate();
}

static void newres_extra(void)
{
    uk_center(53, 214, 38, UK_SMALL, g_uk->title, "NEW RESEARCH PROJECTS");
}

static const UkMenu newres_menu = {
    UI_BG_RESEARCH, T_BLUE, UKS_POPUP, 45, 30, 230, 140, 53, 54, 198, 9, 9, 38,
    NULL, NULL, NULL, "Esc=OK", 146, newres_extra
};

/* NewResearchListState: topics the base can start; Enter allocates. */
static void new_research(char (*ln)[41])
{
    unsigned char map[LAB_LINES];
    int n, t, sel = 0, i;

    for (;;) {
        for (n = t = 0; t < N_TOPICS; t++)
            if (base_topic_available(t)) {
                strcpy(ln[n], g_topics[t].name);
                map[n++] = (unsigned char)t;
            }
        sel = uk_menu(&newres_menu, NULL, ln, n, sel, NULL, NULL);
        if (sel < 0)
            return;
        if ((i = base_research_start(map[sel])) < 0) {
            uk_notice("No room for more research projects.");
            continue;
        }
        research_alloc(i, 1);
    }
}

static void research_extra(void)
{
    char t[8];

    sprintf(t, "%d", sci_free());
    label_value(10, 24, UK_SMALL, "Scientists Available>", t);
    sprintf(t, "%d", base_sci_allocated());
    label_value(160, 24, UK_SMALL, "Scientists Allocated>", t);
    sprintf(t, "%d", labs_free());
    label_value(10, 34, UK_SMALL, "Laboratory Space Available>", t);
    uk_text(10, 44, UK_SMALL, g_uk->text, "RESEARCH PROJECT");
    uk_text(120, 44, UK_SMALL, g_uk->text, "SCIENTISTS");
    uk_text(120, 52, UK_SMALL, g_uk->text, "ALLOCATED");
    uk_text(226, 44, UK_SMALL, g_uk->text, "PROGRESS");
}

static int research_key(int sel, int ch, int scan)
{
    (void)sel;
    (void)scan;
    if (ch != 'n')
        return 0;
    act = ACT_NEW;
    return 2;
}

static const UkMenu research_menu = {
    UI_BG_RESEARCH, T_BLUE, UKS_LIST | UKS_KEYS0 | UKS_CLICK, 0, 0, 320, 200, 8, 62, 296, 12, 9, 8,
    "CURRENT RESEARCH", NULL, TABS("\x9E\xD8"), "N=New Project|Esc=OK", 176, research_extra
};

/* ResearchState: the base's projects; Enter changes a project's
   scientists, N starts a new one. */
void research_screen(void)
{
    char (*ln)[41] = malloc(LAB_LINES * sizeof *ln);
    int sel = 0, n, i;

    if (!ln)
        return;
    uk_open();
    uk_invalidate();
    for (;;) {
        const BaseRec *r = BASE_REC;
        for (n = i = 0; i < r->n_res; i++) {
            const ResProj *p = &r->res[i];
            sprintf(ln[n++], "%.26s\t%d\t%s", g_topics[p->topic].name, p->sci,
                    progress_text(p));
        }
        act = ACT_NONE;
        sel = uk_menu(&research_menu, NULL, ln, n, sel, research_key, NULL);
        if (act == ACT_NEW)
            new_research(ln);
        else if (sel < 0)
            break;
        else if (sel < n)
            research_alloc(sel, 0);
        uk_invalidate();
    }
    uk_close();
    free(ln);
}

/* ------------------------------------------------------------------ */
/* Manufacture */

/* manufacture.rul's categories, from the item: the number of its text in
   CATEGORIES (a row of the list has 40 characters, the whole text of
   "Heavy Weapons Platform" does not fit next to a long item name: the
   list draws it from g_uk_ext). */
#define CATEGORIES "CRAFT\0Craft Ammunition\0Craft Weapon\0Weapon\0HWP Cannon Shells\0" \
                   "Ammunition\0Personal Armour\0UFO component\0Heavy Weapons Platform\0Equipment"
static int category(int item)
{
    switch (g_items[item].kind) {
    case IK_SHIP: return 0;
    case IK_CRAFT:
        return item == IT_FUSION_BALL || item == IT_CANNON_ROUNDS || item == IT_STINGRAY_M
               || item == IT_AVALANCHE_M ? 1 : 2;
    case IK_WEAPON: case IK_GRENADE: return 3;
    case IK_CLIP: return item == IT_HWP_FUSION ? 4 : 5;
    case IK_ARMOUR: return 6;
    case IK_MATERIAL: return 7;
    case IK_HWP: return 8;
    }
    return 9;
}

static int in_production(int r)
{
    const BaseRec *b = BASE_REC;
    int i;

    for (i = 0; i < b->n_prod; i++)
        if (PROD_RCP(&b->prod[i]) == r)
            return 1;
    return 0;
}

static int hangars_free(void)
{
    return facil_capacity(CAP_HANGARS) - craft_hangars_used();
}

static void units_text(char *t, const ProdRun *p)
{
    if (p->total == PROD_INFINITE)
        strcpy(t, "inf");
    else
        sprintf(t, "%u", p->total);
}

/* ManufactureInfoState: engineers and units of run i; new: OK pays for
   the first unit. */
static void production_alloc(int i, int isnew)
{
    ProdRun *p = &BASE_REC->prod[i];
    const Recipe *rc = &g_recipes[PROD_RCP(p)];
    const char *name = g_items[rc->item].name;
    int ship = g_items[rc->item].kind == IK_SHIP, stop = 0, first = 0;
    unsigned rep = 0;           /* an arrow button held (DOS: repeats) */
    char t[12];

    popup(UI_BG_MANUF, T_PURPLE, 0, 20, 320, 160);
    uk_center(0, 320, 30, UK_BIG, g_uk->title, name);
    uk_text(16, 80, UK_BIG, g_uk->text, "Engineers");
    uk_text(16, 96, UK_BIG, g_uk->text, "Allocated");
    uk_text(168, 80, UK_BIG, g_uk->text, "Units to");
    uk_text(168, 96, UK_BIG, g_uk->text, "Produce");
    uk_text(40, 118, UK_SMALL, g_uk->text, "INCREASE");
    uk_text(40, 138, UK_SMALL, g_uk->text, "DECREASE");
    uk_text(192, 118, UK_SMALL, g_uk->text, "INCREASE");
    uk_text(192, 138, UK_SMALL, g_uk->text, "DECREASE");
    arrow(132, 114, 1);
    arrow(132, 136, 0);
    arrow(284, 114, 1);
    arrow(284, 136, 0);
    uk_button(16, 155, 136, 16, "STOP PRODUCTION", 0);
    uk_button(168, 155, 136, 16, "OK", 0);
    uk_save(0, 200);
    for (;;) {
        unsigned key;
        int ch, scan, de = 0, du = 0;

        if (!ship)              /* OpenXcom's SELL toggle; craft are kept */
            uk_button(244, 61, 60, 16, "SELL", p->recipe & PROD_SELL);

        uk_restore(8, 50, 160, 18);
        label_num(16, 50, UK_SMALL, "ENGINEERS AVAILABLE>", eng_free());
        label_num(16, 60, UK_SMALL, "WORKSHOP SPACE AVAILABLE>", workshops_free());
        uk_restore(128, 88, 40, 16);
        uk_restore(280, 88, 32, 16);
        label_num(128, 88, UK_BIG, ">", p->eng);
        units_text(t, p);
        label_value(280, 88, UK_BIG, ">", t);
        if (rep && input_held(first ? 250 : 50)) {
            key = rep;
            first = 0;
        } else {
            rep = 0;
            key = input_key();
        }
        if (key == KEY_LCLICK || key == KEY_RCLICK) {
            int all = key == KEY_RCLICK;
            if (inside(132, 114, 13, 14))
                key = all ? 'a' : (first = 1, rep = '+');
            else if (inside(132, 136, 13, 14))
                key = all ? 'z' : (first = 1, rep = '-');
            else if (inside(284, 114, 13, 14))
                key = all ? 'i' : (first = 1, rep = 0x4D00);
            else if (inside(284, 136, 13, 14))
                key = all ? 'o' : (first = 1, rep = 0x4B00);
            else if (all)
                key = 0x011B;
            else if (inside(16, 155, 136, 16))
                key = 's';
            else if (inside(244, 61, 60, 16))
                key = 'l';
            else if (inside(168, 155, 136, 16))
                key = 0x1C0D;
            else
                continue;
        }
        ch = key & 0xFF;
        scan = key >> 8;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch == '+' || ch == '=' || scan == 0x48)
            de = 1;
        else if (ch == '-' || scan == 0x50)
            de = -1;
        else if (ch == 'a')
            de = 255;
        else if (ch == 'z')
            de = -255;
        else if (scan == 0x4D)
            du = 1;
        else if (scan == 0x4B)
            du = -1;
        else if (scan == 0x49)
            du = 10;
        else if (scan == 0x51)
            du = -10;
        else if (ch == 'i')
            du = 999;           /* right click: infinite (craft: every free hangar) */
        else if (ch == 'o')
            du = -999;          /* right click: the unit under way only */
        else if (ch == 13 || ch == 27)
            break;
        else if (ch == 's') {
            stop = 1;
            break;
        } else if (ch == 'l' && !ship) {
            p->recipe ^= PROD_SELL;
            log_printf("BASE manufacture %s sell %s", name, p->recipe & PROD_SELL ? "on" : "off");
            continue;
        }
        /* moreEngineer / lessEngineer: free engineers and workshop space. */
        if (de > 0) {
            int room = eng_free() < workshops_free() ? eng_free() : workshops_free();
            if (de > room)
                de = room;
            if (de > 0)
                p->eng += (unsigned char)de;
        } else if (de < 0)
            p->eng -= (unsigned char)(-de < p->eng ? -de : p->eng);
        /* moreUnit / lessUnit: craft need a free hangar each. */
        if (du > 0 && p->total != PROD_INFINITE) {
            if (ship) {
                int room = hangars_free();
                if (room <= 0) {
                    uk_notice("NO FREE HANGARS FOR CRAFT PRODUCTION!");
                    continue;
                }
                if (du > room)
                    du = room;
            } else if (du == 999)
                p->total = PROD_INFINITE;
            if (p->total != PROD_INFINITE)
                p->total = p->total + du > 999 ? 999 : p->total + du;
        } else if (du < 0) {
            long v = p->total == PROD_INFINITE ? 0 : (long)p->total + du;
            p->total = (unsigned)(v > (long)p->made + 1 ? v : p->made + 1L);
        }
    }
    if (stop || (isnew && !base_pay_item(PROD_RCP(p)))) {
        log_printf("BASE manufacture %s stopped", name);
        base_prod_stop(i);
    } else {
        units_text(t, p);
        log_printf("BASE manufacture %s x%s, %d engineers", name, t, p->eng);
    }
    uk_invalidate();
}

/* ManufactureStartState: what one unit takes; Enter starts the run if the
   base can pay for one, has workshop space and (craft) a free hangar. */
static void production_start(int r)
{
    const Recipe *rc = &g_recipes[r];
    unsigned char need[4], mat_item[4];
    int k, y = 108, ok, i;
    char t[24];

    need[0] = rc->alloys;
    need[1] = rc->elerium;
    need[2] = rc->power;
    need[3] = rc->nav;
    mat_item[0] = IT_ALLOYS;
    mat_item[1] = IT_ELERIUM;
    mat_item[2] = IT_UFO_POWER;
    mat_item[3] = IT_UFO_NAV;

    ok = funds_dollars() >= rc->cost100 * 100L && workshops_free() > 0;
    popup(UI_BG_MANUF, T_BLUE, 0, 20, 320, 160);
    uk_center(0, 320, 30, UK_BIG, g_uk->title, g_items[rc->item].name);
    sprintf(t, "%u", rc->hours);
    uk_text(uk_text(16, 50, UK_SMALL, WHITE, t) + 3, 50, UK_SMALL, g_uk->text,
            "Engineer hours to produce one unit");
    label_value(16, 60, UK_SMALL, "Cost per unit>", money(t, rc->cost100 * 100L));
    label_num(16, 70, UK_SMALL, "Work Space Required>", rc->space);
    if (rc->alloys || rc->elerium || rc->power || rc->nav) {
        uk_center(16, 290, 84, UK_SMALL, g_uk->text, "SPECIAL MATERIALS REQUIRED");
        uk_text(30, 92, UK_SMALL, g_uk->text, "ITEM");
        uk_text(30, 100, UK_SMALL, g_uk->text, "REQUIRED");
        uk_text(155, 92, UK_SMALL, g_uk->text, "UNITS");
        uk_text(155, 100, UK_SMALL, g_uk->text, "REQUIRED");
        uk_text(230, 92, UK_SMALL, g_uk->text, "UNITS");
        uk_text(230, 100, UK_SMALL, g_uk->text, "AVAILABLE");
        for (k = 0; k < 4; k++)
            if (need[k]) {
                unsigned have = g_base.stores[mat_item[k]];
                uk_text(30, y, UK_SMALL, g_uk->list, g_items[mat_item[k]].name);
                sprintf(t, "%u", need[k]);
                uk_text(170, y, UK_SMALL, WHITE, t);
                sprintf(t, "%u", have);
                uk_text(245, y, UK_SMALL, WHITE, t);
                ok = ok && have >= need[k];
                y += 9;
            }
    }
    uk_button(16, 155, 136, 16, "CANCEL", 0);
    if (ok)
        uk_button(168, 155, 136, 16, "START PRODUCTION", 0);
    uk_save(0, 200);
    for (;;) {
        unsigned key = input_key();
        int ch;
        if (key == KEY_RCLICK)
            break;
        if (key == KEY_LCLICK) {
            if (inside(16, 155, 136, 16))
                break;
            if (!ok || !inside(168, 155, 136, 16))
                continue;
            key = 0x1C0D;
        }
        ch = key & 0xFF;
        if (ch == 27)
            break;
        if (ch != 13 || !ok)
            continue;
        if (g_items[rc->item].kind == IK_SHIP && hangars_free() <= 0)
            uk_notice("NO FREE HANGARS FOR CRAFT PRODUCTION!");
        else if (rc->space > workshops_free())
            uk_notice("NOT ENOUGH WORK SPACE AVAILABLE!");
        else if ((i = base_prod_start(r)) < 0)
            uk_notice("No room for more production runs.");
        else {
            production_alloc(i, 1);
            break;
        }
    }
    uk_invalidate();
}

/* DEBUG: '!' marks every topic researched (the tests list the whole menu). */
static int newprod_key(int sel, int ch, int scan)
{
    int t;

    (void)sel;
    (void)scan;
    if (ch != '!' || !g_debug)
        return 0;
    for (t = 0; t < N_TOPICS; t++)
        g_base.done[t] = 1;
    act = ACT_DBG;
    return 2;
}

static const UkMenu newprod_menu = {
    UI_BG_MANUF, T_PURPLE, UKS_POPUP | UKS_KEYS0, 0, 20, 320, 160, 8, 70, 296, 8, 9, 30,
    "Production Items", "ITEM\tCATEGORY", TABS("\x9C"), "Esc=OK", 154, NULL
};

/* NewManufactureListState: what the base can make (researched, not in
   production here). */
static void new_production(char (*ln)[41])
{
    unsigned char map[N_RECIPES];
    int n, r, sel = 0;

    for (;;) {
        for (n = r = 0; r < N_RECIPES; r++)
            if (base_recipe_known(r) && !in_production(r)) {
                sprintf(ln[n], "%.36s\t\1%c", g_items[g_recipes[r].item].name,
                        '0' + category(g_recipes[r].item));
                map[n++] = (unsigned char)r;
            }
        log_printf("MANUF new production list: %d items", n);
        g_uk_ext = CATEGORIES;
        act = ACT_NONE;
        sel = uk_menu(&newprod_menu, NULL, ln, n, sel, newprod_key, NULL);
        g_uk_ext = NULL;
        if (act == ACT_DBG)
            continue;
        if (sel < 0)
            return;
        production_start(map[sel]);
    }
}

static void make_extra(void)
{
    char t[16];

    label_num(8, 24, UK_SMALL, "Engineers Available>", eng_free());
    label_num(160, 24, UK_SMALL, "Engineers Allocated>", base_eng_allocated());
    label_num(8, 34, UK_SMALL, "Workshop Space Available>", workshops_free());
    label_value(166, 34, UK_SMALL, "Current Funds>", money(t, funds_dollars()));
    /* OpenXcom's columns, a little apart for the wider DOS font. */
    uk_text(10, 52, UK_SMALL, g_uk->text, "ITEM");
    uk_text(104, 44, UK_SMALL, g_uk->text, "Engineers");
    uk_text(104, 52, UK_SMALL, g_uk->text, "Allocated");
    uk_text(158, 44, UK_SMALL, g_uk->text, "Units");
    uk_text(158, 52, UK_SMALL, g_uk->text, "Produced");
    uk_text(206, 44, UK_SMALL, g_uk->text, "Cost");
    uk_text(206, 52, UK_SMALL, g_uk->text, "per");
    uk_text(206, 60, UK_SMALL, g_uk->text, "Unit");
    uk_text(256, 44, UK_SMALL, g_uk->text, "Days/");
    uk_text(256, 52, UK_SMALL, g_uk->text, "Hours");
    uk_text(256, 60, UK_SMALL, g_uk->text, "Left");
}

static const UkMenu make_menu = {
    UI_BG_MANUF, T_PURPLE, UKS_LIST | UKS_KEYS0 | UKS_CLICK, 0, 0, 320, 200, 8, 80, 296, 9, 9, 8,
    "CURRENT PRODUCTION", NULL, TABS("\x6A\x96\xC4\xF6"), "N=New Production|Esc=OK", 176, make_extra
};

/* One line of ManufactureState's list: item, engineers, produced / to
   produce, cost per unit, days / hours left (rounded up per hour). */
static void prod_line(char *s, const ProdRun *p)
{
    const Recipe *rc = &g_recipes[PROD_RCP(p)];
    char tail[40], u[6], c[12], left[12];

    units_text(u, p);
    if (p->total == PROD_INFINITE)
        strcpy(left, "inf");
    else if (p->eng) {
        long need = (long)p->total * rc->hours - ((long)p->made * rc->hours + p->hours);
        long h = (need + p->eng - 1) / p->eng;
        sprintf(left, "%ld/%ld", h / 24, h % 24);
    } else
        strcpy(left, "-");
    sprintf(tail, "\t%d\t%u/%s\t%s\t%s", p->eng, p->made, u, money(c, rc->cost100 * 100L),
            left);
    sprintf(s, "%.*s%s", 40 - (int)strlen(tail), g_items[rc->item].name, tail);
}

/* ManufactureState: the base's runs; Enter changes a run, N starts a new
   one. */
void manufacture_screen(void)
{
    char (*ln)[41] = malloc(LAB_LINES * sizeof *ln);
    int sel = 0, n, i;

    if (!ln)
        return;
    uk_open();
    uk_invalidate();
    for (;;) {
        const BaseRec *r = BASE_REC;
        for (n = i = 0; i < r->n_prod; i++)
            prod_line(ln[n++], &r->prod[i]);
        act = ACT_NONE;
        sel = uk_menu(&make_menu, NULL, ln, n, sel, research_key, NULL);
        if (act == ACT_NEW)
            new_production(ln);
        else if (sel < 0)
            break;
        else if (sel < n)
            production_alloc(sel, 0);
        uk_invalidate();
    }
    uk_close();
    free(ln);
}
