/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The craft screens of the basescape (OpenXcom's CraftsState,
   CraftInfoState and CraftWeaponsState): the base's craft list, a craft's
   own screen (damage, fuel, weapon boxes, the Crew / Equipment / Armour
   buttons of a transport) and the list of weapons for a slot. The screens
   the buttons open are other sections' (src/crewscr.c: Select Squad and
   Select Armor; src/hwpscr.c: Equipment); the geoscape runs them, next to
   itself, and comes back here. An overlay section of its own, compiled
   with -zc (its strings stay in its code).

   Size: basescr + hangar (7255 bytes) must not grow: the overlay area is
   geo + basescr + pedia plus 1 KB (build.ps1 prints the sets with under
   2 KB left), so every byte more here is a byte more area, for the 8086 and
   the V20 build alike (docs/design.md, craft screens). Less is fine, the
   area follows (for 48 bytes, then the battle's sets hold it). The weapon
   is fitted by crewscr.c (CR_FIT) to make room for the times under the
   damage, fuel and ammo lines. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "craft.h"
#include "geo.h"
#include "hwp.h"
#include "input.h"
#include "qlist.h"
#include "uik.h"

#define TABS(s)     ((const unsigned char *)(s))

/* Select Armament: a window over the craft's screen, None and every
   weapon whose launcher the stores hold (and the one now fitted). */
static const UkMenu weapon_menu = {
    UI_BG_HANGAR, T_PURPLE, UKS_LIST | UKS_POPUP, 32, 16, 256, 168, 44, 66, 232, 8, 11, 24,
    "Select Armament", "Armament\tQuantity\tAmmunition", TABS("\x70\xA8"), "Esc=Cancel", 160, NULL
};

static int weapon_screen(Craft *c, int slot)
{
    char (*ln)[41] = malloc((N_CRAFT_WEAPONS + 1) * sizeof *ln);
    unsigned char map[N_CRAFT_WEAPONS + 1];
    int n = 0, w, sel = 0;

    if (!ln)
        return -1;
    strcpy(ln[n], "None");
    map[n++] = CW_NONE;
    for (w = 0; w < N_CRAFT_WEAPONS; w++) {
        const CraftWeapon *cw = &g_craft_weapons[w];
        if (!g_base.stores[cw->launcher] && c->weapon[slot] != w)
            continue;
        if (c->weapon[slot] == w)
            sel = n;
        if (cw->ammo_item == NO_ITEM)
            sprintf(ln[n], "%s\t%u\t-", cw->name, g_base.stores[cw->launcher]);
        else
            sprintf(ln[n], "%s\t%u\t%u", cw->name, g_base.stores[cw->launcher],
                    g_base.stores[cw->ammo_item]);
        map[n++] = (unsigned char)w;
    }
    sel = uk_menu(&weapon_menu, NULL, ln, n, sel, NULL, NULL);
    if (sel >= 0)
        sel = map[sel];
    free(ln);
    uk_invalidate();
    return sel;
}

/* ------------------------------------------------------------------ */
/* A craft's screen (CraftInfoState) */

/* Where the picture elements of an OpenXcom craft screen stand, in the
   320 x 200 screen: weapon boxes, the three buttons, OK. */
static int hit(int x, int y, int w, int h)
{
    return g_mouse_x >= x && g_mouse_x < x + w && g_mouse_y >= y && g_mouse_y < y + h;
}

#define BOX_Y       48
#define BOX_W       24
#define BOX_H       32
static int box_x(int slot)
{
    return 14 + 268 * slot;
}

#define BTN_X       14
#define BTN_W       64
#define BTN_H       16
static int btn_y(int b)
{
    return 96 + 24 * b;
}

static int assigned(int k)
{
    int i, n = 0;

    for (i = 0; i < g_roster_n; i++)
        n += g_roster[i].craft == k + 1;
    return n;
}

/* The n-th figure of the equipment row (UI_FIGURE type * 4: 4 HWP, 8 equipment). */
static void figure(int n, int type)
{
    if (n < 22)
        uk_pic_in(UI_FIGURE + type + (n & 3), 40 * (n >> 2), 25);
}

/* A line of a craft screen's text, in the screen's text colour. */
static void label(int x, int y, const char *s)
{
    uk_text(x, y, UK_SMALL, g_uk->text, s);
}

static void stat(int x, int y, const char *fmt, int v)
{
    char b[24];

    sprintf(b, fmt, v);
    label(x, y, b);
}

/* a * 100 / b without the 16 bits overflowing (an Avenger's damage). */
static int pct(int a, int b)
{
    return (int)((long)a * 100 / b);
}

/* CraftInfoState::formatTime under a line of the screen: "(1 day/5 hours)"
   for left units at rate units an hour, rounded up (as OpenXcom, days with
   their slash even when no hours follow). */
static char *amount(char *p, int n, const char *unit)
{
    return p + sprintf(p, "%d %s%s", n, unit, "s" + (n < 2));
}

static void when(int x, int y, int left, int rate)
{
    char b[24], *p = b;
    int t = (left + rate - 1) / rate, d = t / 24, h = t - d * 24;

    *p++ = '(';
    if (d) {
        p = amount(p, d, "day");
        *p++ = '/';
    }
    if (h)
        p = amount(p, h, "hour");
    *p++ = ')';
    *p = 0;
    label(x, y, b);
}

static void draw_info(int k)
{
    const Craft *c = &g_craft[k];
    const CraftType *t = &g_craft_types[c->type];
    char b[40];
    int s, q, x, items = 0;

    uk_screen(UI_BG_HANGAR, T_BLUE);
    uk_window(0, 0, 320, 200);
    uk_center(0, 320, 8, UK_BIG, g_uk->title, craft_name(c));
    stat(14, 24, "Damage>%d%%", pct(c->damage, t->hp));
    stat(226, 24, "Fuel>%d%%", pct(c->fuel, t->fuel));
    /* Under them, while the craft is repairing or refuelling (STR_REPAIRS,
       STR_REFUELLING; craft_status). */
    if (c->damage > 0)
        when(14, 33, c->damage, REPAIR_HOUR);
    else if (!(c->pad & (HP_ARM | HP_READY)) && c->fuel < t->fuel)
        when(226, 33, t->fuel - c->fuel, 2 * REFUEL_HALF);
    q = c->damage <= 0 ? 0 : c->damage >= t->hp ? 4 : c->damage * 4 / t->hp + 1;
    uk_pic((c->type == CT_SKYRANGER ? UI_DF_SKYRANGER : UI_DF_CRAFT + c->type * 5) + q,
           0x4000 + 149 - 173, 44 - 92);
    for (s = 0; s < t->weapons; s++) {
        const CraftWeapon *cw = c->weapon[s] == CW_NONE ? NULL : &g_craft_weapons[c->weapon[s]];
        x = box_x(s);
        b[0] = (char)('1' + s);
        b[1] = 0;
        uk_button(x, BOX_Y, BOX_W, BOX_H, b, 0);
        if (!cw)
            continue;
        x = 46 + 140 * s;
        uk_text(x, BOX_Y, UK_SMALL, 14, cw->name);
        x += 20 * s;                    /* the lines under the name: 20 further on the right */
        stat(x, 62, "Ammo>%d", c->ammo[s]);
        stat(x, 71, "Max>%d", cw->ammo_max);
        if (!c->damage && (c->pad & HP_ARM) && c->ammo[s] < cw->ammo_max)    /* STR_REARMING */
            when(x, 80, cw->ammo_max - c->ammo[s], cw->rearm);
        uk_pic(UI_DF_WEAPON + s * 6 + c->weapon[s], 0x4000 + 37 + 5 * s, 63 - 104);
    }
    uk_center(0, 320, 86, UK_SMALL, g_uk->hilite, craft_status(c));
    if (t->soldiers) {
        for (s = 0; s < 3; s++)
            uk_button(BTN_X, btn_y(s), BTN_W, BTN_H,
                      s == 0 ? "Crew" : s == 1 ? "Equipment" : "Armour", 0);
        /* The crew, then the HWPs and the equipment (a figure for four items),
           a figure every 10 pixels from x 85 (CraftInfoState; 22 fit). */
        for (s = 0; s < assigned(k) && s < 22; s++)
            uk_pic_in(UI_FIGURE + (s & 3), 40 * (s >> 2), 0);
        for (s = 0; s < MAX_HWP && c->hwp[s]; s++)
            figure(s, 4);
        for (q = 0; q < N_ITEMS; q++)
            items += c->items[q];
        for (q = 0; q < items; q += 4)
            figure(s++, 8);
        log_printf("FIGURES %d %d %d", assigned(k), s - (items + 3) / 4, (items + 3) / 4);
    }
    uk_button(128, 168, 64, 24, "OK", 0);
}

/* 0: back to the list, else the screen to open (CR_*). */
static int craft_info(int k)
{
    Craft *c = &g_craft[k];
    const CraftType *t = &g_craft_types[c->type];

    uk_open();
    for (;;) {
        unsigned key;
        int ch, s, want = -1;

        uk_invalidate();
        draw_info(k);
        for (;;) {
            key = input_key();
            ch = key & 0xFF;
            if (ch >= 'A' && ch <= 'Z')
                ch += 32;
            want = -1;
            if (key == KEY_RCLICK || ch == 27 || ch == 13) {
                uk_close();
                return 0;
            }
            if (key == KEY_LCLICK) {
                if (hit(128, 168, 64, 24)) {
                    uk_close();
                    return 0;
                }
                for (s = 0; s < t->weapons; s++)
                    if (hit(box_x(s), BOX_Y, BOX_W, BOX_H))
                        want = s;
                for (s = 0; s < 3 && t->soldiers; s++)
                    if (hit(BTN_X, btn_y(s), BTN_W, BTN_H))
                        want = 10 + s;
            } else if (ch == '1' || ch == '2')
                want = ch - '1' < t->weapons ? ch - '1' : -1;
            else if (ch == 'x' && g_debug) {
                /* DEBUG (tests/CRAFTTM.TXT): damaged by 29, a third of the fuel,
                   two rounds in each weapon. */
                c->damage = 29;
                c->fuel = t->fuel / 3;
                c->ammo[0] = c->ammo[1] = 2;
                c->pad = HP_ARM | HP_R(0) | HP_R(1);
                break;                  /* and draw it again */
            }
            else if (t->soldiers)
                want = ch == 'c' ? 10 : ch == 'e' ? 11 : ch == 'a' ? 12 : -1;
            if (want >= 0)
                break;
        }
        if (want < 0)
            continue;
        if (want >= 10) {
            uk_close();
            return want == 10 ? CR_CREW : want == 11 ? CR_EQUIP : CR_ARMOUR;
        }
        s = weapon_screen(c, want);
        if (s >= 0) {
            uk_close();
            return CR_FIT(want, s);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The list of craft (CraftsState) */

#define LIST_LINES  MAX_CRAFT
static char (*lines)[41];
static signed char line_craft[LIST_LINES];

static int list_lines(void)
{
    int i, n = 0;

    for (i = 0; i < MAX_CRAFT; i++) {
        const Craft *c = &g_craft[i];
        const CraftType *t = &g_craft_types[c->type];
        int s, w = 0, h = 0;
        if (!CRAFT_HERE(c))
            continue;
        for (s = 0; s < t->weapons; s++)
            w += c->weapon[s] != CW_NONE;
        for (s = 0; s < MAX_HWP && c->hwp[s]; s++)
            h++;
        sprintf(lines[n], "%s\t%s\t%d/%d\t%d\t%d", craft_name(c), craft_status(c), w, t->weapons,
                assigned(i), h);
        line_craft[n++] = (signed char)i;
    }
    return n;
}

static const UkMenu list_menu = {
    UI_BG_HANGAR, T_PURPLE, UKS_LIST | UKS_CLICK, 0, 0, 320, 200, 14, 50, 290, 14, 9, 8,
    "Interception Craft", "NAME\tSTATUS\tWEAPONS\tCREW\tHWPs", TABS("\x5E\xA2\xCE\xFC"),
    "Esc=OK", 178, NULL
};

int craft_screens(int *k)
{
    static int sel;
    char info[24];
    int n, r = CR_DONE, direct = g_base_cell >= 0;

    lines = malloc(LIST_LINES * sizeof *lines);
    if (!lines)
        return CR_DONE;
    sprintf(info, "BASE> %.16s", g_bhdr[g_bcur].name);
    if (*k > 31)
        *k &= 31;                       /* back from CR_FIT */
    if (direct && *k < 0)
        *k = g_base_cell;              /* a right click on a hangar: that craft's screen first */
    for (;;) {
        if (*k < 0) {
            n = list_lines();
            if (sel >= n)
                sel = n ? n - 1 : 0;
            sel = ql_menu(&list_menu, info, lines, n, sel, NULL, NULL);
            if (sel < 0) {
                sel = 0;
                break;
            }
            /* As OpenXcom: a craft that is out has no screen. */
            if (g_craft[line_craft[sel]].state != CS_HANGAR)
                continue;
            *k = line_craft[sel];
        }
        r = craft_info(*k);
        if (r) {
            if (r > CR_ARMOUR) {            /* a weapon chosen (CR_FIT) */
                *k |= r;
                r = CR_ARMOUR;
            }
            break;
        }
        if (direct) {                   /* a right click on a hangar: back to the base */
            r = CR_DONE;
            break;
        }
        *k = -1;
    }
    free(lines);
    lines = NULL;
    if (r == CR_DONE)
        g_base_cell = -1;               /* (basescr.c sets it for the next screen) */
    return r;
}
