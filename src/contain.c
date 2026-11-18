/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Manage Alien Containment (OpenXcom's ManageAlienContainmentState): the
   live aliens the base holds, by race and rank, with how many are under
   interrogation and how many are to be removed; Remove Selected sends the
   rejected ones to the stores as corpses (they die). Opened by a right
   click on an Alien Containment in the basescape, and after a battle when
   the base holds more aliens than its containment (DebriefingState: the
   screen cannot be cancelled then, and Remove Selected shows when enough
   are rejected). An overlay section of its own next to the geoscape and
   qlist, compiled with -zc: strings stay in its code. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "facil.h"
#include "input.h"
#include "inv.h"
#include "items.h"
#include "qlist.h"
#include "uik.h"
#include "uitext.h"
#include "unit.h"

#define TABS(s)     ((const unsigned char *)(s))

/* On the heap while the screen is open, behind g_scr_lines: the rows'
   lines, the info line and a row's item, rejected and studied aliens. */
typedef struct {
    char line[N_LIVE][41];
    char info[48];
    unsigned char item[N_LIVE], rej[N_LIVE], std[N_LIVE];
    unsigned char n, over, full, ok;
    int rejected;
} Cw;
#define W       ((Cw *)g_scr_lines)

/* Containment space left once the rejected aliens are gone. */
static int space(void)
{
    return facil_capacity(CAP_ALIENS) - live_aliens() + W->rejected;
}

static void line(int i)
{
    Cw *w = W;

    sprintf(w->line[i], "%.19s\t%u\t%u\t%u", g_items[w->item[i]].name,
            g_base.stores[w->item[i]] - w->rej[i], w->rej[i], w->std[i]);
}

static void info(void)
{
    sprintf(W->info, "SPACE AVAILABLE>%d   SPACE USED>%d", space(), facil_capacity(CAP_ALIENS) - space());
}

/* The rejected quantity of a row changes (the live quantity follows). */
static int step(int i, int d)
{
    Cw *w = W;
    long have = g_base.stores[w->item[i]], was = w->rej[i], now = was + d;

    if (now > have)
        now = have;
    if (now < 0)
        now = 0;
    if (now == was)
        return 0;
    w->rej[i] = (unsigned char)now;
    w->rejected += (int)(now - was);
    line(i);
    info();
    if (w->over && (space() >= 0) != w->full) {
        w->full = space() >= 0;         /* Remove Selected comes or goes: draw the buttons again */
        return 2;
    }
    return 1;
}

static int key(int sel, int ch, int scan)
{
    (void)sel;
    (void)scan;
    if (ch != 9)
        return 0;
    W->ok = 1;
    return 2;
}

static void head(void)
{
    uk_text(10, 44, UK_SMALL, g_uk->text, "Alien");
    uk_text(114, 35, UK_SMALL, g_uk->text, "Live");
    uk_text(114, 43, UK_SMALL, g_uk->text, "Specimens");
    uk_text(184, 35, UK_SMALL, g_uk->text, "Rejected");
    uk_text(184, 43, UK_SMALL, g_uk->text, "Specimens");
    uk_text(264, 35, UK_SMALL, g_uk->text, "Being");
    uk_text(264, 43, UK_SMALL, g_uk->text, "Studied");
}

static const UkMenu menu = {
    UI_BG_RESEARCH, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 52, 304, 13, 9, 8,
    "Manage Alien Containment", NULL, TABS("\x68\xB6\xFE"), NULL, 176, head
};

/* The corpse a removed alien leaves: its race's (the live aliens run
   Sectoid 6 ranks, Floater 6, Snakeman 5, Muton 3, Ethereal 3, then the
   terror units, which leave none). */
static void remove_aliens(int i)
{
    Cw *w = W;
    const unsigned char *edge = (const unsigned char *)"\x06\x0C\x11\x14\x17";
    int r = 0, k = w->item[i] - IT_LIVE;

    while (r < 5 && k >= edge[r])
        r++;
    g_base.stores[w->item[i]] -= w->rej[i];
    if (r < 5)
        g_base.stores[IT_CORPSE_SECTOID + r] += w->rej[i];
    log_printf("CONTAIN removed %u %s: %u %s in the stores", w->rej[i], g_items[w->item[i]].name,
               g_base.stores[IT_CORPSE_SECTOID + (r < 5 ? r : 0)],
               g_items[IT_CORPSE_SECTOID + (r < 5 ? r : 0)].name);
}

static void screen(int over)
{
    UkMenu m = menu;
    Cw *w;
    int i, j, sel = 0;
    const BaseRec *r = BASE_REC;

    g_scr_lines = malloc(sizeof(Cw));
    if (!g_scr_lines)
        return;
    w = W;
    memset(w, 0, sizeof *w);
    w->over = (unsigned char)over;
    for (i = IT_LIVE; i <= IT_LIVE_LAST; i++) {
        unsigned char std = 0;
        for (j = 0; j < r->n_res; j++)
            std += g_topics[r->res[j].topic].need_item == i;
        if (g_base.stores[i] || std) {
            w->item[w->n] = (unsigned char)i;
            w->std[w->n] = std;
            line(w->n++);
        }
    }
    info();
    w->full = space() >= 0;
    log_printf("CONTAIN screen: %d kinds, %d of %d", w->n, live_aliens(), facil_capacity(CAP_ALIENS));
    for (i = j = 0; i < w->n; i++)
        j += w->std[i];
    log_printf("CONTAIN studied: %d, live %d", j, live_aliens() - j);
    /* DOS's columns: Alien, Live, Rejected (the arrows), Being Studied: the
       arrows move left to leave the last column to the right of them. */
    g_ql_shift = -40;
    for (;;) {
        /* Overcrowded: no Cancel, and Remove Selected only when enough is rejected. */
        m.buttons = !over ? "Tab=Remove Selected|Esc=Cancel" : w->full ? "Tab=Remove Selected" : NULL;
        sel = ql_menu(&m, w->info, w->line, w->n, sel, key, step);
        if (w->ok && (!over || w->full)) {
            for (i = 0; i < w->n; i++)
                if (w->rej[i])
                    remove_aliens(i);
            break;
        }
        w->ok = 0;
        if (sel < 0 && !over)
            break;
        if (sel < 0)
            sel = 0;
    }
    g_ql_shift = 0;
    free(g_scr_lines);
    g_scr_lines = NULL;
}

void contain_screen(void)
{
    screen(0);
}

/* After a battle (DebriefingState): more aliens than room. */
void contain_check(void)
{
    char t[192], m[96];

    if (live_aliens() <= facil_capacity(CAP_ALIENS))
        return;
    ui_text(UI_MSG, m, sizeof m, MSG_CONTAIN);
    sprintf(t, "%.16s: %s", g_bhdr[g_bcur].name, m);
    uk_page(UI_BG_MENU, T_GREEN, "");
    uk_notice(t);
    uk_close();
    screen(1);
}
