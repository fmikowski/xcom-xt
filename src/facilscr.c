/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The base layout screen (build, dismantle, place a new base's lift),
   the base seen from above and the base defence: screens of facil.c's
   data in the base screens' overlay section. Calls into the hourly
   section (capacities) happen per action, not per drawn cell. */

#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alien.h"
#include "base.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "inv.h"
#include "gfx.h"
#include "hud.h"
#include "uik.h"
#include "uitext.h"
#include "input.h"
#include "map.h"
#include "sound.h"
#include "unit.h"

#define defs g_fdefs

static unsigned char cell_at(int x, int y)
{
    if (x < 0 || y < 0 || x >= BASE_W || y >= BASE_W)
        return FAC_NONE;
    return g_facil.cell[y][x];
}

/* Top-left cell of the facility covering x, y. */
static void root_of(int *x, int *y)
{
    unsigned char c = g_facil.cell[*y][*x];
    *x -= FAC_PART(c) & 1;
    *y -= FAC_PART(c) >> 1;
}

static int built_at(int x, int y)
{
    if (FAC_TYPE(cell_at(x, y)) == FAC_NONE)
        return 0;
    root_of(&x, &y);
    return g_facil.days[y][x] == 0;
}

static void put(int type, int x, int y, int days)
{
    int i, j, s = defs[type].size;

    for (j = 0; j < s; j++)
        for (i = 0; i < s; i++)
            g_facil.cell[y + j][x + i] = (unsigned char)(type | (j * 2 + i) << 5);
    g_facil.days[y][x] = (unsigned char)days;
}

/* Calls fn for the top-left cell of every finished facility. */
#define EACH_BUILT(x, y, d) \
    for (y = 0; y < BASE_W; y++) \
        for (x = 0; x < BASE_W; x++) \
            if (g_facil.cell[y][x] && !FAC_PART(g_facil.cell[y][x]) && !g_facil.days[y][x] \
                && ((d) = &defs[g_facil.cell[y][x]]) != NULL)

/* ------------------------------------------------------------------ */
/* Base defence */

int facil_defend(const char *ufo_name, int hp, int hp_max)
{
    const FacilDef *d;
    char line[48];
    int x, y, row = 4, shots = 1, s;

    uk_page(UI_BG_SOLDIERS, T_BLUE, "Base Defence");
    sprintf(line, "%s approaching the base!", ufo_name);
    uk_center(0, 320, 26, UK_SMALL, 12, line);
    EACH_BUILT(x, y, d)
        if (d == &g_fdefs[FAC_GRAV])
            shots = 2;          /* the grav shield gives every defence a second shot */
    for (s = 0; s < shots && hp > 0; s++)
        EACH_BUILT(x, y, d) {
            int hit;
            if (!d->defence || hp <= 0 || row > 16)
                continue;
            snd_shot(0);
            hit = (int)rnd(100) < d->hit;
            if (hit) {
                int dmg = d->defence / 2 + (int)rnd(d->defence + 1);
                hp = hp > dmg ? hp - dmg : 0;
                sprintf(line, "%s\thit: %d, hull %d%%", d->name, dmg,
                        (int)((long)hp * 100 / hp_max));
            } else
                sprintf(line, "%s\tmissed", d->name);
            uk_line(30, 20 + row * 9, 260, hit ? 10 : 7, line, (const unsigned char *)"\x78");
            row++;
            log_printf("DEFENCE %s", line);
        }
    if (row == 4)
        uk_text(30, 20 + row++ * 9, UK_SMALL, 7, "The base has no defences.");
    uk_center(0, 320, 29 + row * 9, UK_SMALL, hp ? 12 : 10,
              hp ? "The UFO lands. Aliens are entering the base!" : "UFO destroyed!");
    uk_page_wait();
    return hp;
}

/* ------------------------------------------------------------------ */
/* Base layout screens: Build Facilities as DOS has it (BuildFacilitiesState:
   the list of installations; PlaceFacilityState: its cost and a square of
   the base view), dismantling (a click on a facility) and Base Information.
   The base seen from above is facil_draw_view in facil.c; the base's name,
   region, funds and small views at the right are drawn as the basescape
   does (basescr.c). */

#define VIEW_X      0
#define VIEW_Y      8
#define RX          192         /* the right side */
#define MINI_Y      41

/* The eight small bases: a base's finished facilities green, those under
   construction red, the selected base framed in white (MiniBaseView), at
   x0, y0 (the basescape's at the right, Base Information's at the top). */
static void draw_mini(int x0, int y0)
{
    int i, x, y;

    for (i = 0; i < MAX_BASES; i++) {
        int bx = x0 + i * 16;
        uk_fill(bx, y0, 16, 16, i == g_bcur ? 15 : 0);
        uk_fill(bx + 1, y0 + 1, 14, 14, g_video == VIDEO_CGA ? 0 : 6);
        if (i >= g_nbases)
            continue;
        {
            const Facilities *f = i == g_bcur ? &g_facil : &g_bb->rec[i].facil;
            for (y = 0; y < BASE_W; y++)
                for (x = 0; x < BASE_W; x++) {
                    unsigned char c = f->cell[y][x];
                    if (FAC_TYPE(c) != FAC_NONE && !FAC_PART(c)) {
                        int s = g_fdefs[FAC_TYPE(c)].size * 2;
                        uk_fill(bx + 2 + x * 2, y0 + 2 + y * 2, s, s, f->days[y][x] ? 12 : 10);
                    }
                }
        }
    }
}

/* The base screen without its buttons: the view, the name, region and funds,
   the small bases; tip: a line at the top left. */
static void draw_top(const char *tip)
{
    char t[24], m[16];
    const BaseHdr *h = &g_bhdr[g_bcur];

    uk_cga_palette(2);
    uk_screen(UK_NOBG, T_GOLD);
    facil_draw_view(VIEW_X, VIEW_Y);
    uk_text(RX + 2, 0, uk_width(UK_BIG, h->name) > 124 ? UK_SMALL : UK_BIG, 13, h->name);
    uk_text(RX + 2, 16, UK_SMALL, 5, alien_region_name(h->x, h->y));
    geo_money(m, g_base.funds, g_base.funds10);
    sprintf(t, "FUNDS> %s", m);
    uk_text(RX + 2, 24, UK_SMALL, 9, t);
    draw_mini(RX, MINI_Y);
    if (tip)
        uk_text(2, 0, UK_SMALL, 11, tip);
    uk_save(0, 200);
}

/* 1 if every facility left is connected to the access lift when the one at
   x, y (top-left) is gone. */
static int connected_without(int gx, int gy, int size)
{
    unsigned char seen[BASE_W][BASE_W];
    unsigned char qx[BASE_W * BASE_W], qy[BASE_W * BASE_W];
    int head = 0, tail = 0, x, y, k;
    static const signed char dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };

    memset(seen, 0, sizeof seen);
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++)
            seen[gy + y][gx + x] = 1;       /* removed */
    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++)
            if (FAC_TYPE(g_facil.cell[y][x]) == FAC_LIFT && !seen[y][x]) {
                seen[y][x] = 2;
                qx[tail] = (unsigned char)x;
                qy[tail++] = (unsigned char)y;
            }
    while (head < tail) {
        x = qx[head];
        y = qy[head++];
        for (k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (FAC_TYPE(cell_at(nx, ny)) != FAC_NONE && !seen[ny][nx]) {
                seen[ny][nx] = 2;
                qx[tail] = (unsigned char)nx;
                qy[tail++] = (unsigned char)ny;
            }
        }
    }
    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++)
            if (FAC_TYPE(g_facil.cell[y][x]) != FAC_NONE && !seen[y][x])
                return 0;
    return 1;
}

/* Why the facility at x, y cannot be dismantled (BasescapeState::
   viewLeftClick), or NULL; the text of the link refusal (the game's) is
   read into buf. */
static const char *dismantle_refusal(int x, int y, char *buf)
{
    const FacilDef *d;
    int type = FAC_TYPE(g_facil.cell[y][x]), i;

    root_of(&x, &y);
    d = &g_fdefs[type];
    if (type == FAC_LIFT)
        return "The access lift cannot be dismantled.";
    /* BaseFacility::inUse: nothing under construction is. */
    if (!g_facil.days[y][x] && d->cap != 0xFF) {
        int left = facil_capacity(d->cap) - d->cap_n, used = 0;
        switch (d->cap) {
        case CAP_QUARTERS: used = facil_personnel(); break;
        case CAP_LABS: used = base_sci_allocated(); break;
        case CAP_WORKSHOPS: used = base_workshops_used(); break;
        case CAP_STORES: used = (facil_stores_used() + 9) / 10; break;
        case CAP_HANGARS: used = craft_hangars_used(); break;
        case CAP_PSI:           /* psi_trainees, without a call into labscr's section */
            for (i = 0; i < g_roster_n; i++)
                used += g_roster[i].alive && (g_roster[i].psi_flags & PSF_TRAIN);
            break;
        }
        if (used > left)
            return "FACILITY IN USE";
    }
    if (!connected_without(x, y, d->size)) {
        ui_text(UI_MSG, buf, 96, MSG_NO_DISMANTLE);
        return buf;
    }
    return NULL;
}

/* DismantleFacilityState: a window of 152 x 80 at 20, 60 with the facility's
   name and OK / Cancel; 1 = OK (Enter), 0 = Cancel (Esc). */
static int okcancel(const char *l1, const char *l2)
{
    int ok = 0;

    uk_theme(T_PURPLE);
    uk_fill(20, 60, 152, 80, 0);
    uk_window(20, 60, 152, 80);
    uk_center(20, 152, 75, UK_SMALL, 9, l1);
    uk_center(20, 152, 85, UK_SMALL, 9, l2);
    uk_button(36, 115, 44, 16, "OK", 0);
    uk_button(112, 115, 44, 16, "CANCEL", 0);
    for (;;) {
        unsigned key = input_key();
        if (key == KEY_LCLICK && g_mouse_y >= 115 && g_mouse_y < 131) {
            if (g_mouse_x >= 36 && g_mouse_x < 80)
                key = 13;
            else if (g_mouse_x >= 112 && g_mouse_x < 156)
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

/* A click on a facility in the base view: asks, then dismantles it (no
   refund). */
void facil_dismantle(int cell)
{
    int x = cell & 7, y = cell >> 3, i, j;
    const char *why;
    char note[96];
    const FacilDef *d;

    if (FAC_TYPE(cell_at(x, y)) == FAC_NONE)
        return;
    uk_open();
    uk_cga_palette(2);
    why = dismantle_refusal(x, y, note);
    d = &g_fdefs[FAC_TYPE(g_facil.cell[y][x])];
    if (why) {
        uk_notice(why);
        log_printf("BASE %.40s", why);
    } else {
        if (okcancel("Dismantle", d->name)) {
            root_of(&x, &y);
            for (j = 0; j < d->size; j++)
                for (i = 0; i < d->size; i++)
                    g_facil.cell[y + j][x + i] = FAC_NONE;
            g_facil.days[y][x] = 0;
            facil_summary();
            log_printf("BASE %s dismantled.", d->name);
        }
    }
    uk_close();
}

/* Why a facility of this type cannot go at x, y (as isPlaceable and the
   funds check do): 0 it can, BR_HERE or BR_MONEY; a facility next to a
   finished one only. */
#define BR_HERE     1
#define BR_MONEY    2
static int build_refusal(int type, int x, int y)
{
    const FacilDef *d = &g_fdefs[type];
    int i, j, next = 0;

    if (x + d->size > BASE_W || y + d->size > BASE_W)
        return BR_HERE;
    for (j = 0; j < d->size; j++)
        for (i = 0; i < d->size; i++)
            if (g_facil.cell[y + j][x + i])
                return BR_HERE;
    for (i = 0; i < d->size; i++)
        if (built_at(x + i, y - 1) || built_at(x + i, y + d->size)
            || built_at(x - 1, y + i) || built_at(x + d->size, y + i))
            next = 1;
    if (!next)
        return BR_HERE;
    if (g_base.funds < (long)d->cost_k)
        return BR_MONEY;
    return 0;
}

static void do_build(int type, int x, int y)
{
    const FacilDef *d = &g_fdefs[type];

    g_base.funds -= d->cost_k;
    put(type, x, y, d->days);
    log_printf("BASE Building %s: %d days.", d->name, d->days);
}

static int buildable(int type)
{
    return type != FAC_LIFT && (g_fdefs[type].need == REQ_NONE || g_base.done[g_fdefs[type].need]);
}

/* BuildFacilitiesState: the window "Installation" with the names, OK. A
   list too long for its window gets Up and Down buttons (DOS scrolls with
   a bar). Strings and column offsets as literals: in this -zc section a
   named table that the menu points at would get an overlay vector. */
static const UkMenu build_menu = {
    UI_BG_FACIL, T_GOLD, UKS_LIST | UKS_POPUP, 192, 40, 128, 160, 198, 64, 114, 11, 9, 48,
    "Installation", NULL, NULL, "Esc=OK", 176, NULL
};
static const UkMenu build_menu_long = {
    UI_BG_FACIL, T_GOLD, UKS_LIST | UKS_POPUP, 192, 40, 128, 160, 198, 64, 114, 11, 9, 48,
    "Installation", NULL, NULL, "Up=Up|Esc=OK|Down=Down", 176, NULL
};

/* DEBUG: F2 asks for construction to finish. */
static int finish_req;

static int choose_key(int sel, int ch, int scan)
{
    (void)sel;
    if (g_debug && !ch && scan == 0x3C) {
        finish_req = 1;
        return 2;
    }
    return 0;
}

/* Picks a facility type to build; 0 = none, -1 = F2 (DEBUG). The list
   lines are on the heap while the list is open (738 bytes of DGROUP that
   the battle would carry otherwise). */
static int choose_type(int *last)
{
    char (*lines)[41];
    /* The ruleset's order: the hangar last, behind the defences researched later. */
    static const unsigned char order[N_FACILITIES - 2] = {
        FAC_QUARTERS, FAC_LAB, FAC_WORKSHOP, FAC_SRADAR, FAC_LRADAR, FAC_MISSILE, FAC_STORES,
        FAC_CONTAIN, FAC_LASER, FAC_PLASMA, FAC_FUSION, FAC_GRAV, FAC_MIND, FAC_PSILAB, FAC_HYPER,
        FAC_HANGAR
    };
    int map[N_FACILITIES], n = 0, t, sel, k;

    if ((lines = malloc(N_FACILITIES * sizeof *lines)) == NULL)
        return 0;
    for (k = 0; k < N_FACILITIES - 2; k++)
        if (buildable(t = order[k])) {
            strcpy(lines[n], g_fdefs[t].name);
            map[n++] = t;
        }
    uk_invalidate();
    finish_req = 0;
    sel = uk_menu(n > 11 ? &build_menu_long : &build_menu, NULL, lines, n, *last < n ? *last : 0,
                  g_debug ? choose_key : NULL, NULL);     /* (a key handler makes a click select first) */
    free(lines);
    if (finish_req)
        return -1;
    if (sel < 0)
        return 0;
    *last = sel;
    return map[sel];
}

/* PlaceFacilityState: the facility's cost, construction time and upkeep in
   the window, a square of the base view for it (the pointer or the arrows
   move it, a click or Enter builds); Cancel or Esc back to the list. */
static int cx, cy;                      /* the square last used (2, 2 when Build Facilities opens) */

static void place_screen(int type)
{
    const FacilDef *d = &g_fdefs[type];
    char t[24];
    int s = d->size, x = cx, y = cy, px, py, ok, mx = -1, my = -1;
    int why;

    draw_top(NULL);
    uk_pic_clean(UI_BG_PLACE);
    uk_restore(192, 40, 128, 160);
    uk_theme(T_BLUE);
    uk_window(192, 40, 128, 160);
    uk_text(202, 50, UK_SMALL, 9, d->name);
    uk_text(202, 62, UK_SMALL, 9, "COST>");
    geo_money(t, d->cost_k, 0);
    uk_text(202, 70, UK_BIG, 15, t);
    uk_text(202, 90, UK_SMALL, 9, "CONSTRUCTION TIME>");
    sprintf(t, d->days == 1 ? "%d day" : "%d days", d->days);
    uk_text(202, 98, UK_BIG, 15, t);
    uk_text(202, 118, UK_SMALL, 9, "MAINTENANCE>");
    geo_money(t, d->upkeep_k, 0);
    uk_text(202, 126, UK_BIG, 15, t);
    uk_button(200, 176, 112, 16, "Cancel", 0);
    uk_save(0, 200);
    for (;;) {
        unsigned key;
        int ch, scan;

        /* The square the facility would take, yellow where it fits. */
        px = x > BASE_W - s ? BASE_W - s : x;
        py = y > BASE_W - s ? BASE_W - s : y;
        why = build_refusal(type, x, y);
        ok = !why || why == BR_MONEY;           /* (only the money is missing) */
        uk_rect(VIEW_X + px * 32, VIEW_Y + py * 32, s * 32, s * 32, ok ? 14 : 12);
        while (!input_ready())
            if (g_mouse && g_mouse_x < RX && g_mouse_y >= VIEW_Y && (g_mouse_y - VIEW_Y) / 32 < BASE_W
                && (g_mouse_x / 32 != mx || (g_mouse_y - VIEW_Y) / 32 != my))
                break;
        if (!input_ready()) {
            /* The pointer moved over another square: the square follows it. */
            uk_restore(VIEW_X + px * 32, VIEW_Y + py * 32, s * 32, s * 32);
            mx = x = g_mouse_x / 32;
            my = y = (g_mouse_y - VIEW_Y) / 32;
            continue;
        }
        key = input_key();
        uk_restore(VIEW_X + px * 32, VIEW_Y + py * 32, s * 32, s * 32);
        ch = key & 0xFF;
        scan = key >> 8;
        if (key == KEY_RCLICK || ch == 27)
            return;
        if (key == KEY_LCLICK) {
            if (g_mouse_x < RX && g_mouse_y >= VIEW_Y && (g_mouse_y - VIEW_Y) / 32 < BASE_W) {
                x = g_mouse_x / 32;
                y = (g_mouse_y - VIEW_Y) / 32;
                ch = 13;
                scan = 0;
            } else if (g_mouse_x >= 200 && g_mouse_x < 312 && g_mouse_y >= 176 && g_mouse_y < 192)
                return;
            else
                continue;
        }
        if (ch == 13) {
            int why = build_refusal(type, x, y);
            char note[96];

            cx = x;
            cy = y;
            if (!why) {
                do_build(type, x, y);
                return;                 /* the list comes back, to build more */
            }
            if (why == BR_MONEY)
                strcpy(note, "NOT ENOUGH MONEY!");
            else
                ui_text(UI_MSG, note, sizeof note, MSG_NO_BUILD);
            uk_notice(note);
            log_printf("BASE %.40s", note);
            if (why == BR_MONEY)
                return;                 /* DOS closes the window */
        } else if (scan == 0x48 && y > 0)
            y--;
        else if (scan == 0x50 && y < BASE_W - 1)
            y++;
        else if (scan == 0x4B && x > 0)
            x--;
        else if (scan == 0x4D && x < BASE_W - 1)
            x++;
    }
}

/* Place the access lift of a new base: a click or Enter on a square. */
static void place_lift(void)
{
    int x = 2, y = 2;

    draw_top("Place the access lift: click a square.");
    for (;;) {
        unsigned key;

        uk_rect(VIEW_X + x * 32, VIEW_Y + y * 32, 32, 32, 14);
        key = input_key();
        uk_restore(VIEW_X + x * 32, VIEW_Y + y * 32, 32, 32);
        if (key == KEY_LCLICK && g_mouse_x < RX && g_mouse_y >= VIEW_Y && (g_mouse_y - VIEW_Y) / 32 < BASE_W) {
            x = g_mouse_x / 32;
            y = (g_mouse_y - VIEW_Y) / 32;
            key = 0x1C0D;
        }
        if ((key >> 8) == 0x48 && y > 0)
            y--;
        else if ((key >> 8) == 0x50 && y < BASE_W - 1)
            y++;
        else if ((key >> 8) == 0x4B && x > 0)
            x--;
        else if ((key >> 8) == 0x4D && x < BASE_W - 1)
            x++;
        else if ((key & 0xFF) == 13) {
            put(FAC_LIFT, x, y, 0);         /* the new base's first facility */
            facil_summary();
            log_printf("BASE access lift at %c%d", 'A' + x, y + 1);
            return;
        }
    }
}

/* Build Facilities (from the basescape): the list, a facility picked, a
   square; the new building appears and the list comes back. lift: a new
   base's first facility, the access lift, is placed instead. DEBUG: F2 in
   the list finishes all construction. */
void facil_screen(int lift)
{
    int last = 0, t;

    uk_open();
    cx = cy = 2;
    if (lift)
        place_lift();
    else
        for (;;) {
            draw_top(NULL);
            t = choose_type(&last);
            if (!t)
                break;
            if (t < 0) {
                memset(g_facil.days, 0, sizeof g_facil.days);
                facil_summary();
                log_printf("BASE Construction finished (debug).");
            } else
                place_screen(t);
        }
    uk_close();
}

/* ------------------------------------------------------------------ */
/* Facility damage after a base defence (DebriefingState: a block whose
   base objects are all gone; Base::destroyFacility). The access lift
   stays (the base needs it here). Hangars take craft along when too few
   are left, labs their scientists' places, workshops their engineers and
   then their runs. */

static void wreck(int x, int y)
{
    const FacilDef *d = &g_fdefs[FAC_TYPE(g_facil.cell[y][x])];
    int i, j;

    log_printf("BASE %s destroyed", d->name);
    for (j = 0; j < d->size; j++)
        for (i = 0; i < d->size; i++)
            g_facil.cell[y + j][x + i] = FAC_NONE;
    g_facil.days[y][x] = 0;
}

void facil_wreck(const unsigned char *had, const unsigned char *now)
{
    int x, y, m, n = 0, i;
    BaseRec *r = BASE_REC;

    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++) {
            unsigned char c = g_facil.cell[y][x];
            m = y * BASE_W + x;
            if (c && !FAC_PART(c) && FAC_TYPE(c) != FAC_LIFT && !g_facil.days[y][x]
                && (had[m >> 3] >> (m & 7)) & 1 && !((now[m >> 3] >> (m & 7)) & 1)) {
                wreck(x, y);
                n++;
            }
        }
    if (!n)
        return;
    /* What is cut off from the lift goes too. */
    {
        unsigned char seen[BASE_W][BASE_W], qx[BASE_W * BASE_W], qy[BASE_W * BASE_W];
        int head = 0, tail = 0, k, rx, ry;

        memset(seen, 0, sizeof seen);
        for (y = 0; y < BASE_W; y++)
            for (x = 0; x < BASE_W; x++)
                if (FAC_TYPE(g_facil.cell[y][x]) == FAC_LIFT) {
                    seen[y][x] = 1;
                    qx[tail] = (unsigned char)x;
                    qy[tail++] = (unsigned char)y;
                }
        while (head < tail) {
            x = qx[head];
            y = qy[head++];
            for (k = 0; k < 4; k++) {
                int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
                if (FAC_TYPE(cell_at(nx, ny)) != FAC_NONE && !seen[ny][nx]) {
                    seen[ny][nx] = 1;
                    qx[tail] = (unsigned char)nx;
                    qy[tail++] = (unsigned char)ny;
                }
            }
        }
        for (y = 0; y < BASE_W; y++)
            for (x = 0; x < BASE_W; x++)
                if (g_facil.cell[y][x] && !seen[y][x]) {
                    rx = x;
                    ry = y;
                    root_of(&rx, &ry);
                    wreck(rx, ry);
                }
    }
    for (i = MAX_CRAFT; i-- > 0 && craft_hangars_used() > facil_capacity(CAP_HANGARS);)
        if (CRAFT_HERE(&g_craft[i]) && g_craft[i].state == CS_HANGAR) {
            log_printf("BASE %s destroyed", craft_name(&g_craft[i]));
            craft_crew_clear(&g_craft[i]);
            g_craft[i].state = CS_EMPTY;
        }
    for (i = 0; i < r->n_res && base_sci_allocated() > facil_capacity(CAP_LABS); i++) {
        int over = base_sci_allocated() - facil_capacity(CAP_LABS);
        r->res[i].sci -= (unsigned char)(over < r->res[i].sci ? over : r->res[i].sci);
    }
    i = 0;
    while (i < r->n_prod && base_workshops_used() > facil_capacity(CAP_WORKSHOPS)) {
        int over = base_workshops_used() - facil_capacity(CAP_WORKSHOPS);
        if (r->prod[i].eng > over) {
            r->prod[i].eng -= (unsigned char)over;
            break;
        }
        base_prod_stop(i);
    }
    facil_summary();
}
