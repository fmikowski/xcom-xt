/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A craft's crew and armour (OpenXcom's CraftSoldiersState,
   CraftArmorState and SoldierArmorState): Select Squad, the base's
   soldiers with the craft each belongs to (a click on a row cycles it
   through the transports here that have room, and none), and Select
   Armor, the craft's soldiers with their
   armour, each chosen from the stores. Opened by the geoscape from the
   craft's screen (src/hangar.c); an overlay section of its own, compiled
   with -zc (its strings stay in its code). Also here: fitting the weapon
   chosen in Select Armament (fit_weapon, reached through armour_screen: CR_FIT in
   src/craft.h), which left the craft screens' section for the times it
   shows. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "craft.h"
#include "hwp.h"
#include "input.h"
#include "qlist.h"
#include "uik.h"
#include "unit.h"

#define TABS(s)     ((const unsigned char *)(s))

/* On the heap while a screen is open: the lines, then 56 characters of
   info line, 40 of title and a byte a row (the soldier of each line). */
static char (*lines)[41];
static int cur;                         /* the craft */
#define INFO        ((char *)(lines + g_roster_n + 1))
#define TITLE       (INFO + 56)
#define PICK        ((unsigned char *)TITLE + 40)

static int open_lines(const char *title, int with_craft)
{
    lines = malloc((g_roster_n + 1) * sizeof *lines + 96 + g_roster_n);
    if (!lines)
        return 0;
    if (with_craft)
        sprintf(TITLE, "%s %s", title, craft_name(&g_craft[cur]));
    else
        strcpy(TITLE, title);
    return 1;
}

static int assigned(void)
{
    int i, n = 0;

    for (i = 0; i < g_roster_n; i++)
        n += g_roster[i].craft == cur + 1;
    return n;
}

/* ------------------------------------------------------------------ */
/* Select Squad (CraftSoldiersState as DOS has it: the title at 16,7, the
   space lines at 16 and 122 under it, NAME, RANK and CRAFT at 16, 122 and
   224, the rows white in this craft, purple in another, OK at the bottom;
   no arrows or Sort by: Left and Right move the soldier) */

static void crew_head(void)
{
    uk_text(16, 7, UK_BIG, g_uk->title, TITLE);
    uk_line(16, 32, 286, g_uk->title, "NAME\tRANK\tCRAFT", TABS("\x6A\xD0"));
}

static const UkMenu crew_menu = {
    UI_BG_SOLDIERS, T_PURPLE, UKS_LIST | UKS_CLICK | UKS_TOGGLE | UKS_LEFT, 0, 0, 320, 200,
    14, 42, 290, 14, 9, 7,
    NULL, NULL, TABS("\x6A\xD0"), "Esc=OK", 176, crew_head
};

static void crew_lines(void)
{
    const Craft *c = &g_craft[cur];
    int i, used = assigned(), k;

    for (i = 0; i < g_roster_n; i++) {
        const Soldier *s = &g_roster[i];
        char st[16], t[64];
        if (s->transit)
            sprintf(st, "Transfer %dh", s->transit);
        else if (s->recovery)
            strcpy(st, "WOUNDED");         /* Soldier::getCraftString */
        else if (s->psi_flags & PSF_TRAIN)
            strcpy(st, "Psi training");
        else
            strcpy(st, s->craft ? craft_name(&g_craft[s->craft - 1]) : "NONE");
        sprintf(t, "%s%.13s\t%s\t%.14s", s->craft == cur + 1 ? "\x02" : s->craft ? "\x03" : "",
                s->name, g_rank_names[s->rank], st);
        strncpy(lines[i], t, 40);
        lines[i][40] = 0;
    }
    for (k = 0; k < MAX_HWP && c->hwp[k]; k++)
        used += HWP_SEATS;
    sprintf(INFO, "SPACE AVAILABLE>%d\tSPACE USED>%d", g_craft_types[c->type].soldiers - used,
            used);
}

/* Soldier.craft cycles through the transports of this base with a free
   seat, then none (the Soldiers list's Enter does the same). */
static void cycle(Soldier *s)
{
    int k, i, n;

    if (s->recovery || s->transit) {    /* CraftSoldiersState refuses the wounded */
        int mine = s->craft == cur + 1;     /* ... but takes one off this craft's crew */
        log_printf("CREW %s %s%s", s->name, s->recovery ? "wounded" : "away",
                   mine ? ", unassigned" : "");
        if (mine)
            s->craft = 0;
        return;
    }
    if (s->craft && g_craft[s->craft - 1].state != CS_HANGAR) {
        log_printf("CREW %s: %s is out", s->name, craft_name(&g_craft[s->craft - 1]));
        return;
    }
    for (k = s->craft; k < MAX_CRAFT; k++) {
        const Craft *c = &g_craft[k];
        if (c->state != CS_HANGAR || c->base != g_bcur || !g_craft_types[c->type].soldiers)
            continue;
        for (i = n = 0; i < g_roster_n; i++)
            n += g_roster[i].craft == k + 1;
        if (n < craft_seats(c)) {
            s->craft = (unsigned char)(k + 1);
            log_printf("CREW %s in %s", s->name, craft_name(c));
            return;
        }
    }
    s->craft = 0;
    log_printf("CREW %s unassigned", s->name);
}

static int moved = -1;                 /* the row a soldier moved to */

/* Space, Enter or a click on a row cycles the craft; Left and Right swap
   the soldier with the one above or below (the order in which the squad
   leaves the craft), and the list opens again with him selected. */
static int crew_key(int sel, int ch, int scan)
{
    if (scan == 0x4B || scan == 0x4D) {
        int to = sel + (scan == 0x4B ? -1 : 1);
        Soldier t;
        if (to < 0 || to >= g_roster_n)
            return 0;
        t = g_roster[sel];
        g_roster[sel] = g_roster[to];
        g_roster[to] = t;
        log_printf("CREW %s moved to place %d", t.name, to + 1);
        crew_lines();
        moved = to;
        return 2;
    }
    if (ch != 'c' && ch != ' ')
        return 0;
    cycle(&g_roster[sel]);
    crew_lines();
    return 1;
}

void crew_screen(int k)
{
    UkMenu m = crew_menu;
    int sel = 0;

    cur = k;
    if (!open_lines("Select Squad for", 1))
        return;
    crew_lines();
    log_printf("CREW %s: %d seated, %d fit", craft_name(&g_craft[cur]), assigned(), craft_crew(cur));
    for (;;) {
        sel = ql_menu(&m, INFO, lines, g_roster_n, sel, crew_key, NULL);
        if (sel < 0)
            break;
        if (moved >= 0)
            sel = moved;
        else
            crew_key(sel, ' ', 0);      /* Enter: as a click */
        moved = -1;
    }
    free(lines);
    lines = NULL;
}

/* ------------------------------------------------------------------ */
/* Select Armor */

static const UkMenu armour_menu = {
    UI_BG_STORES, T_GREEN, UKS_LIST | UKS_CLICK, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    NULL, "Name\tCraft\tArmour", TABS("\x68\xB8"), "Esc=OK", 178, NULL
};

/* What SoldierArmorState lists: None and the armours the stores hold (and
   the one the soldier wears). */
static const UkMenu choose_menu = {
    UI_BG_STORES, T_GREEN, UKS_LIST | UKS_POPUP, 48, 36, 224, 120, 60, 82, 200, 4, 12, 44,
    "Select Armour", "Armour\tQuantity", TABS("\x90"), "Esc=Cancel", 130, NULL
};

static int armour_lines(void)
{
    int i, n = 0;

    for (i = 0; i < g_roster_n; i++) {
        const Soldier *s = &g_roster[i];
        if (s->craft != cur + 1)
            continue;
        sprintf(lines[n], "%s\t%s\t%s", s->name, craft_name(&g_craft[cur]),
                s->armour ? g_armours[s->armour].name : "NONE");
        PICK[n++] = (unsigned char)i;
    }
    return n;
}

/* A soldier's armour from the stores (his old one goes back). */
static void choose(Soldier *s)
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
    sel = uk_menu(&choose_menu, s->name, ln, n, sel, NULL, NULL);
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

/* ------------------------------------------------------------------ */
/* Fitting a weapon (CraftWeaponsState::lstWeaponsClick; chosen on the craft's
   screen, src/hangar.c, and run from here by armour_screen: CR_FIT) */

/* Slot of craft c gets weapon w (CW_NONE: empty): the old one goes back to
   the stores with the rounds it holds, the new launcher comes out of them
   (the craft rearms from the stores by the hour, craft_hour). */
static void fit_weapon(Craft *c, int slot, int w)
{
    int old = c->weapon[slot];

    if (old == w || c->state != CS_HANGAR)
        return;
    if (old != CW_NONE) {
        const CraftWeapon *cw = &g_craft_weapons[old];
        g_base.stores[cw->launcher]++;
        if (cw->ammo_item != NO_ITEM)
            g_base.stores[cw->ammo_item] += c->ammo[slot] / cw->per_item;
    }
    c->weapon[slot] = CW_NONE;
    c->ammo[slot] = 0;
    c->pad &= ~HP_R(slot);
    /* CraftWeaponsState::lstWeaponsClick: the new weapon is empty and
       rearming; a ready craft rearms. */
    if (w != CW_NONE && g_base.stores[g_craft_weapons[w].launcher]) {
        g_base.stores[g_craft_weapons[w].launcher]--;
        c->weapon[slot] = (unsigned char)w;
        c->pad |= HP_R(slot);
        if (c->pad & HP_READY)
            c->pad ^= HP_READY | HP_ARM;
    }
    log_printf("CRAFT %s weapon %d: %s", craft_name(c), slot + 1,
               c->weapon[slot] == CW_NONE ? "-" : g_craft_weapons[c->weapon[slot]].name);
}

void armour_screen(int k)
{
    UkMenu m = armour_menu;
    int sel = 0, n;

    if (k > 31) {                       /* CR_FIT: a weapon chosen on the craft's screen */
        fit_weapon(&g_craft[k & 31], (k >> 5) & 1, (unsigned char)((k >> 6) - 2));
        return;
    }
    cur = k;
    if (!open_lines("Select Armour", 0))
        return;
    m.title = TITLE;
    for (;;) {
        n = armour_lines();
        sprintf(INFO, "%s: %d soldiers", craft_name(&g_craft[cur]), n);
        if (sel >= n)
            sel = n ? n - 1 : 0;
        sel = ql_menu(&m, INFO, lines, n, sel, NULL, NULL);
        if (sel < 0)
            break;
        choose(&g_roster[PICK[sel]]);
    }
    free(lines);
    lines = NULL;
}
