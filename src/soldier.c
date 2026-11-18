/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Soldiers button's screens (SoldiersState, SoldierInfoState,
   SackSoldierState, SoldierArmorState): an overlay section of their own
   (xfer.c's was full: the sections that run together must leave the
   manager its 1 KB, build.ps1). Compiled with -zc: strings and tables stay
   in the section's code, so tab stops are string literals. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alien.h"
#include "base.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "inv.h"
#include "items.h"
#include "qlist.h"
#include "uik.h"
#include "unit.h"

#define TABS(s)     ((const unsigned char *)(s))

/* The screens' list lines (xfer.c's pointer). */
extern char (*g_scr_lines)[41];
#define lines g_scr_lines

/* The Soldiers button (SoldiersState, SoldierInfoState, SackSoldierState,
   SoldierArmorState): "Soldier List" with name, rank and craft; a click on
   a soldier opens his screen (stat bars, Armor, Sack, Prev, Next); a Psi
   Training button only where there is a Psionic Laboratory. The R key keeps
   the older list above (Enter changes the craft). */

#define rank_full(r)    g_rank_names[r]

/* A window with two lines and OK / Cancel buttons (SackSoldierState: 152 x
   80 at 84, 60); 1 = OK (Enter), 0 = Cancel (Esc). */
static int okcancel(int x, const char *l1, const char *l2)
{
    int ok = 0;

    uk_fill(x, 60, 152, 80, 0);
    uk_window(x, 60, 152, 80);
    uk_center(x, 152, 75, UK_SMALL, 9, l1);
    uk_center(x, 152, 85, UK_SMALL, 9, l2);
    uk_button(x + 16, 115, 44, 16, "OK", 0);
    uk_button(x + 92, 115, 44, 16, "CANCEL", 0);
    for (;;) {
        unsigned key = input_key();
        if (key == KEY_LCLICK && g_mouse_y >= 115 && g_mouse_y < 131) {
            if (g_mouse_x >= x + 16 && g_mouse_x < x + 60)
                key = 13;
            else if (g_mouse_x >= x + 92 && g_mouse_x < x + 136)
                key = 27;
        }
        if ((key & 0xFF) == 13) {
            ok = 1;
            break;
        }
        if ((key & 0xFF) == 27 || key == KEY_RCLICK)
            break;
    }
    return ok;
}

/* SoldierArmorState: None and the armours in the stores (and the one worn,
   which goes back to the stores). */
static const UkMenu sarmour_menu = {
    UI_BG_HANGAR, T_GREEN, UKS_LIST | UKS_POPUP, 64, 20, 192, 160, 78, 68, 160, 10, 8, 28,
    "Select Armour", "TYPE\tQUANTITY", TABS("\x64"), "Esc=CANCEL", 156, NULL
};

static void choose_armour(Soldier *s)
{
    char (*ln)[41] = malloc(N_ARMOURS * sizeof *ln);
    unsigned char map[N_ARMOURS];
    int a, n = 0, sel = 0;

    if (!ln)
        return;
    for (a = 0; a < N_ARMOURS; a++) {
        if (a && !g_base.stores[g_armours[a].item] && s->armour != a)
            continue;
        if (s->armour == a)
            sel = n;
        if (a)
            sprintf(ln[n], "%s\t%u", g_armours[a].name, g_base.stores[g_armours[a].item]);
        else
            strcpy(ln[n], "NONE");
        map[n++] = (unsigned char)a;
    }
    uk_invalidate();
    sel = uk_menu(&sarmour_menu, s->name, ln, n, sel, NULL, NULL);
    if (sel >= 0 && map[sel] != s->armour) {
        a = map[sel];
        if (s->armour)
            g_base.stores[g_armours[s->armour].item]++;
        if (a)
            g_base.stores[g_armours[a].item]--;
        s->armour = (unsigned char)a;
        log_printf("BASE equip %s: %s", s->name, a ? g_armours[a].name : "-");
    }
    free(ln);
    uk_invalidate();
}

/* A stat bar (Bar, scale 1): a frame as long as the stat, the stat in its
   colour; what the soldier started with in a lighter one. */
static void sbar(int y, int val, int init, int col)
{
    int dark = col >= 8 ? col - 8 : 0;

    if (val > 169)
        val = 169;
    if (init > val)
        init = val;
    uk_fill(150, y, val + 1, 7, dark);
    uk_fill(150, y + 1, val, 5, 0);
    uk_fill(150, y + 1, val, 5, col);
    uk_fill(150, y + 1, init, 5, col < 8 ? col + 8 : col);
}

/* One stat: its name, number and bar. */
static int sstat(int y, const char *name, int val, int init, int col)
{
    char n[8];

    uk_text(6, y, UK_SMALL, 13, name);
    sprintf(n, "%d", val);
    uk_text(131, y, UK_SMALL, 15, n);
    sbar(y, val, init, col);
    return y + 11;
}

static void sinfo_draw(const Soldier *s)
{
    char t[40];
    int y = 80, out = s->craft && !s->recovery && g_craft[s->craft - 1].state != CS_HANGAR;

    uk_cga_palette(2);
    uk_screen(UI_BG_SOLDIER_INFO, T_PURPLE);
    uk_pic(UI_SOLDIER_RANK + s->rank, -1, 0);
    uk_text(40, 9, UK_BIG, 9, s->name);
    uk_button(0, 33, 28, 14, "<<", 0);
    uk_button(30, 33, 48, 14, "OK", 0);
    uk_button(80, 33, 28, 14, ">>", 0);
    sprintf(t, "ARMOR> %s", s->armour ? g_armours[s->armour].name : "NONE");
    uk_button(130, 33, 110, 14, t, 0);
    if (!out)
        uk_button(260, 33, 60, 14, "SACK", 0);
    sprintf(t, "RANK> %s", rank_full(s->rank));
    uk_text(0, 48, UK_SMALL, 9, t);
    sprintf(t, "MISSIONS> %d", s->missions);
    uk_text(130, 48, UK_SMALL, 9, t);
    sprintf(t, "KILLS> %d", s->kills);
    uk_text(200, 48, UK_SMALL, 9, t);
    sprintf(t, "CRAFT> %s", s->craft ? craft_name(&g_craft[s->craft - 1]) : "NONE");
    uk_text(0, 56, UK_SMALL, 9, t);
    if (s->recovery) {
        sprintf(t, "WOUND RECOVERY> %d day%s", s->recovery, s->recovery == 1 ? "" : "s");
        uk_text(130, 56, UK_SMALL, 9, t);
    }
    if (s->psi_flags & PSF_TRAIN)
        uk_text(0, 66, UK_SMALL, 13, "In Psionic Training");
    y = sstat(y, "TIME UNITS", s->tu, s->tu0 < s->tu ? s->tu0 : s->tu, 10);
    y = sstat(y, "STAMINA", s->stamina, s->stamina, 11);
    y = sstat(y, "HEALTH", s->hp, s->hp, 12);
    y = sstat(y, "BRAVERY", s->bravery, s->bravery, 14);
    y = sstat(y, "REACTIONS", s->reactions, s->reactions, 6);
    y = sstat(y, "FIRING ACCURACY", s->accuracy, s->accuracy, 3);
    y = sstat(y, "THROWING ACCURACY", s->throwing, s->throwing, 9);
    y = sstat(y, "STRENGTH", s->strength, s->strength, 5);
    if (s->psi_skill) {
        y = sstat(y, "PSIONIC STRENGTH", s->psi, s->psi, 13);
        sstat(y, "PSIONIC SKILL", s->psi_skill, s->psi_skill, 13);
    }
}

/* SoldierInfoState for soldier k of the roster; the arrows (<< >> or
   Left / Right) go through the roster. Returns the soldier shown last, or
   -1 when he was sacked. */
static int soldier_info(int k)
{
    for (;;) {
        Soldier *s = &g_roster[k];
        unsigned key;
        int ch, out = s->craft && !s->recovery && g_craft[s->craft - 1].state != CS_HANGAR;

        sinfo_draw(s);
        key = input_key();
        ch = key & 0xFF;
        if (key == KEY_LCLICK) {
            int mx = g_mouse_x, my = g_mouse_y;
            key = 0;
            if (my >= 33 && my < 47)
                key = mx < 28 ? 0x4B00 : mx >= 30 && mx < 78 ? 27 : mx >= 80 && mx < 108 ? 0x4D00
                    : mx >= 130 && mx < 240 ? 'a' : mx >= 260 ? 's' : 0;
            else if (my >= 9 && my < 25 && mx >= 40 && mx < 250)
                key = 'n';
            ch = key & 0xFF;
        } else if (key == KEY_RCLICK)
            ch = 27;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch == 27 || ch == 13)
            return k;
        if (g_roster_n > 1 && ((key >> 8) == 0x4B || (key >> 8) == 0x48 || (key >> 8) == 0x49))
            k = k ? k - 1 : g_roster_n - 1;
        else if (g_roster_n > 1 && ((key >> 8) == 0x4D || (key >> 8) == 0x50 || (key >> 8) == 0x51))
            k = k + 1 < g_roster_n ? k + 1 : 0;
        else if (ch == 'a') {
            choose_armour(s);
        } else if (ch == 'n') {
            char nm[16];
            strcpy(nm, s->name);
            if (uk_edit("Name of the soldier:", nm, 13) && nm[0])
                strcpy(s->name, nm);
        } else if (ch == 's' && !out) {
            char q[24];
            sprintf(q, "%s %.13s?", rank_full(s->rank), s->name);
            sinfo_draw(s);
            if (okcancel(84, "SACK", q)) {
                /* Base::removeSoldier: the armour goes back to the stores. */
                if (s->armour)
                    g_base.stores[g_armours[s->armour].item]++;
                log_printf("SELL sacked %s, %d soldiers left", s->name, roster_alive() - 1);
                base_soldier_remove(g_bcur, k);
                return -1;
            }
        }
    }
}

/* Name, rank, craft (Wounded, NONE or the craft; a soldier on his way
   shows the hours). */
static const UkMenu soldiers_def = {
    UI_BG_SOLDIERS, T_BLUE, UKS_LIST | UKS_CLICK, 0, 0, 320, 200, 14, 40, 290, 16, 8, 8,
    "Soldier List", "NAME\tRANK\tCRAFT", TABS("\x72\xCE"), "Esc=OK", 176, NULL
};

static const UkMenu soldiers_psi_def = {
    UI_BG_SOLDIERS, T_BLUE, UKS_LIST | UKS_CLICK, 0, 0, 320, 200, 14, 40, 290, 16, 8, 8,
    "Soldier List", "NAME\tRANK\tCRAFT", TABS("\x72\xCE"), "P=Psi Training|Esc=OK", 176, NULL
};

static int soldiers_psi, psi_ok;

static int soldiers_list_key(int sel, int ch, int scan)
{
    (void)sel;
    (void)scan;
    soldiers_psi = ch == 'p' && psi_ok;
    return soldiers_psi ? 2 : 0;
}

/* 0 when left, 1 when Psi Training was chosen (the geoscape runs it and
   comes back). */
int soldiers_list(void)
{
    static int sel;
    char t[64];
    int i, ret = 0;
    int psi = psi_ok = facil_capacity(CAP_PSI) > 0;

    lines = malloc((g_roster_n ? g_roster_n : 1) * sizeof *lines);
    if (!lines)
        return 0;
    uk_open();
    uk_invalidate();
    for (;;) {
        for (i = 0; i < g_roster_n; i++) {
            const Soldier *s = &g_roster[i];
            char st[16];
            if (s->transit)
                sprintf(st, "Transfer %dh", s->transit);
            else if (s->recovery)
                strcpy(st, "WOUNDED");
            else
                strcpy(st, s->craft ? craft_name(&g_craft[s->craft - 1]) : "NONE");
            sprintf(t, "%.13s\t%s\t%.14s", s->name, rank_full(s->rank), st);
            strncpy(lines[i], t, 40);
            lines[i][40] = 0;
        }
        soldiers_psi = 0;
        if (sel >= g_roster_n)
            sel = g_roster_n ? g_roster_n - 1 : 0;
        i = ql_menu(psi ? &soldiers_psi_def : &soldiers_def, NULL, lines, g_roster_n, sel,
                    soldiers_list_key, NULL);
        if (soldiers_psi) {
            ret = 1;
            break;
        }
        if (i < 0)
            break;
        sel = soldier_info(i);
        if (sel < 0)
            sel = i;
        uk_invalidate();
    }
    uk_invalidate();
    uk_close();
    free(lines);
    lines = NULL;
    return ret;
}

/* ------------------------------------------------------------------ */
/* Monthly Costs (MonthlyCostsState, interface costsInfo): the button of
   Base Information. Costs are in $K here; DOS has whole dollars. */

/* A text with dots after it up to width w (TextList::setDot). */
static void dotted(int x, int y, int w, int c, const char *s)
{
    char t[40];
    int n = strlen(s);

    if (n > 24)
        n = 24;
    memcpy(t, s, n);
    t[n] = 0;
    while (n < 38 && uk_width(UK_SMALL, t) < w) {
        t[n++] = '.';
        t[n] = 0;
    }
    uk_text(x, y, UK_SMALL, c, t);
}

/* One line of the costs lists: name, cost per unit, quantity, total (the
   columns of MonthlyCostsState at 10, 135, 205 and 249). */
static int cost_row(int y, const char *name, long unit_k, long qty)
{
    char m[16], q[8];

    dotted(10, y, 125, 9, name);
    geo_money(m, unit_k, 0);
    dotted(135, y, 70, 9, m);
    sprintf(q, "%ld", qty);
    dotted(205, y, 44, 9, q);
    geo_money(m, unit_k * qty, 0);
    uk_text(249, y, UK_SMALL, 9, m);
    return y + 8;
}

void costs_screen(void)
{
    char m[16], t[40];
    long all = 0;
    int i, k, n, y, cur = g_bcur;

    uk_open();
    uk_cga_palette(2);
    uk_screen(UI_BG_STORES, T_PURPLE);
    uk_window(0, 0, 320, 200);
    uk_center(0, 320, 12, UK_BIG, 13, "Monthly Costs");
    uk_text(115, 32, UK_SMALL, 13, "Cost per unit");
    uk_text(195, 32, UK_SMALL, 13, "Quantity");
    uk_text(249, 32, UK_SMALL, 13, "Total");
    uk_text(10, 40, UK_SMALL, 13, "Craft Rental");
    for (y = 48, k = 0; k < 2; k++) {
        int ty = k ? CT_INTERCEPTOR : CT_SKYRANGER;
        for (i = n = 0; i < MAX_CRAFT; i++)
            n += CRAFT_HERE(&g_craft[i]) && g_craft[i].type == ty;
        y = cost_row(y, g_craft_types[ty].name, g_craft_types[ty].cost_k, n);
    }
    uk_text(10, 80, UK_SMALL, 13, "Salaries");
    y = cost_row(88, "Soldiers", 20, roster_alive() + (long)base_arriving(TR_SOLDIER));
    y = cost_row(y, "Engineers", 25, g_base.engineers + (long)base_arriving(TR_ENGINEER));
    cost_row(y, "Scientists", 30, g_base.scientists + (long)base_arriving(TR_SCIENTIST));
    dotted(10, 128, 239, 13, "Base maintenance");
    geo_money(m, facil_upkeep(), 0);
    uk_text(249, 128, UK_SMALL, 13, m);
    for (k = 0; k < g_nbases; k++) {
        base_select(k);
        all += base_monthly_costs();
    }
    base_select(cur);
    geo_money(m, alien_funding(), 0);
    sprintf(t, "Income=%s", m);
    uk_text(10, 146, UK_SMALL, 9, t);
    geo_money(m, all, 0);
    sprintf(t, "Maintenance=%s", m);
    uk_text(10, 154, UK_SMALL, 9, t);
    dotted(205, 150, 44, 15, "Total");
    geo_money(m, base_monthly_costs(), 0);
    uk_text(249, 150, UK_SMALL, 15, m);
    uk_button(10, 170, 300, 20, "OK", 0);
    for (;;) {
        unsigned key = input_key();
        if (key == KEY_RCLICK || (key & 0xFF) == 27 || (key & 0xFF) == 13
            || (key == KEY_LCLICK && g_mouse_y >= 170 && g_mouse_y < 190))
            break;
    }
    uk_invalidate();
    uk_close();
}
