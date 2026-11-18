/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The battle inventory screen as DOS's (InventoryState, Inventory): the
   paper doll and TAC01.SCR, the sections of inventories.rul with their
   cells (hands, shoulders, legs, belt, backpack, the ground), items of
   their inventory size with the BIGOBS.PCK pictures, the TU each move
   costs. Its own overlay section, next to the battle core alone (the
   player's turn hands it over, bt_request): loaded only while open.

   A click picks an item up: it leaves its place and its picture follows
   the pointer (hold; an outline of its size when the memory for the
   picture is not there); a click puts it where its top left cell is, the
   right button puts it back. Keys: the arrows go through the
   items, Enter picks one up; then the arrows move an outline over the
   cells and the ground, Enter puts it there, Esc puts it back. D drops the item to the
   ground at once, U (the Unload button) unloads a gun. Bodies on the
   ground are listed and carried as DOS's corpse items (inv.h IT_BODY). Tab or > the next soldier, < the previous one; Esc, I or
   OK close. Before a battle (inv_screen(-1), DOS's inventory after the
   briefing) moves cost nothing, every soldier stands at the craft's
   inventory tile, and closing it keeps each soldier's equipment layout
   (InventoryState::saveEquipmentLayout). */

#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "input.h"
#include "inv.h"
#include "items.h"
#include "uik.h"
#include "battle.h"
#include "battle_in.h"
#include "effects.h"
#include "fx_in.h"
#include "map.h"
#include "pak.h"
#include "timer.h"
#include "unit.h"

/* UI_BIGOB + item and UI_ICON + item for every item (src/uipak.h). */
typedef char uipak_item_room[UI_ICON - UI_BIGOB >= N_PICTURED && UI_UP - UI_ICON >= N_PICTURED ? 1 : -1];

#define GRID        (g_video == VIDEO_EGA || g_vga ? 8 : 1)     /* grid lines */

/* Device i (0 .. n_dev - 1) if it lies at the tile: its item, else
   NO_ITEM (a thrown flare or an armed grenade, src/effects.c). */
static int dev_at(int i, int x, int y, int z)
{
    return i < n_dev && dev[i].x == x && dev[i].y == y && dev[i].z == z ? dev[i].item : NO_ITEM;
}
#define GROUND_Y    152
#define MAX_THINGS  40

/* Where each section is (inventories.rul x, y), by SEC_*. */
#define SEC_XY(s, k)    ((const unsigned char *)"\0\077\200\077\300\150\0\170\200\170\020\045\160\045\300\045")[(s) * 2 + (k)]
/* TU from section a to section b (inventories.rul costs; SEC_GROUND is 9). */
#define COST(a, b)      ((const unsigned char *) \
    "\0\4\10\10\12\12\12\16\0\2" "\4\0\10\12\10\12\12\16\0\2" \
    "\4\4\0\12\12\14\14\20\0\6" "\4\6\12\0\12\12\12\22\0\6" \
    "\6\4\12\12\0\12\12\22\0\6" "\3\3\12\14\14\0\10\20\0\4" \
    "\3\3\12\14\14\10\0\20\0\4" "\10\10\14\20\20\16\16\0\0\12" \
    "\0\0\0\0\0\0\0\0\0\0" "\10\10\14\12\12\14\14\24\0\0")[(a) * 10 + (b)]

/* What the screen shows: an item and where it is (T_BODY: a body lying
   on the ground, idx its unit). */
enum { T_WEAPON, T_GRENADE, T_CARRY, T_FLOOR, T_DEV, T_BODY };
typedef struct {
    unsigned char kind, idx, sec;
    short x, y, w, h;
} Thing;

static Thing th[MAX_THINGS];
static int nth;
static int pre;                         /* before the battle: no TU */
static int gx, gy, gz;                  /* the ground's tile */
static int goff, gnext, gfirst;         /* the ground row: its first entry shown, the first
                                           that did not fit (0: none), its first thing */

static void cells(int sec)
{
    int x, y, x0 = SEC_XY(sec, 0), y0 = SEC_XY(sec, 1);

    if (sec <= SEC_LHAND) {
        uk_rect(x0, y0, 33, 49, GRID);
        return;
    }
    for (y = 0; y < 3; y++)
        for (x = 0; x < 4; x++)
            if (carry_cell(sec, x, y))
                uk_rect(x0 + x * 16, y0 + y * 16, 17, 17, GRID);
}

static void count(int x, int y, int n)
{
    char s[6];
    sprintf(s, "%d", n);
    uk_text(x, y, UK_SMALL, 14, s);
}

/* The hand picture of an item (n: a body's unit). */
static int pic(int item, int n)
{
    return item == IT_BODY ? UI_BODY + unit_body(&g_units[n])
         : item >= IT_TANK_CANNON ? UI_HWP + item - IT_TANK_CANNON : UI_BIGOB + item;
}

/* An item picture in a box (the whole picture if it fits, else its small
   one; a body always fits where it can go), noted as thing nth. */
static void add_thing(int kind, int idx, int sec, int item, int n, int x, int y, int w, int h)
{
    Thing *t = &th[nth];
    int pw = 16, ph = 16, p = pic(item, n);

    if (nth >= MAX_THINGS)
        return;
    if (sec <= SEC_LHAND) {
        uk_pic_in(p, x, y);
        pw = 32;
        ph = 48;
    } else if (uk_pic_box(p, &pw, &ph) && ((pw <= w && ph <= h) || item == IT_BODY)) {
        uk_pic(p, x, y);
        if (sec != SEC_GROUND)
            pw = w, ph = h;
    } else {
        if (item != IT_BODY)
            uk_pic(UI_ICON + item, x, y);
        pw = ph = 16;
    }
    t->kind = (unsigned char)kind;
    t->idx = (unsigned char)idx;
    t->sec = (unsigned char)sec;
    t->x = (short)x;
    t->y = (short)y;
    t->w = (short)pw;
    t->h = (short)ph;
    nth++;
}

/* One more thing in the ground row (Inventory::arrangeGround, in one row):
   entries before goff are on earlier pages; when there is no room left,
   gnext is the first entry for the next page. Returns 1 if it was added. */
static int ge, gxp;

static int gadd(int kind, int idx, int item, int minw)
{
    int n0 = nth;

    if (ge++ < goff)
        return 0;
    if (!gnext && gxp + minw <= 320)
        add_thing(kind, idx, SEC_GROUND, item, 0, gxp, GROUND_Y, 320 - gxp, 48);
    if (nth == n0 && !gnext)
        gnext = ge - 1;
    return nth != n0;
}

/* The tile's items, bodies and thrown devices, as many as fit in the 20
   cells from entry goff on. */
static void ground_row(void)
{
    int i;

    ge = gxp = gnext = 0;
    for (i = 0; i < g_max_floor; i++) {
        const FloorItem *f = &g_floor[i];
        if (f->item == NO_ITEM || f->x != gx || f->y != gy || f->z != gz || !gadd(T_FLOOR, i, f->item, 16))
            continue;
        if (f->n > 1 && f->n < 0x80 && g_items[f->item].kind != IK_WEAPON && f->item != IT_MEDIKIT)
            count(gxp + 2, GROUND_Y + 1, f->n);
        gxp += th[nth - 1].w;
    }
    /* Bodies that can be picked up (2 x 3; the 2x2 units' cannot). */
    for (i = 0; i < g_nunits; i++) {
        const Unit *b = &g_units[i];
        if (!b->alive && !(b->special & SP_CARRIED) && b->x == gx && b->y == gy && b->z == gz
            && inv_body_weight(i) && gadd(T_BODY, i, IT_BODY, 32))
            gxp += th[nth - 1].w;
    }
    /* Thrown flares and armed grenades lie there too (src/effects.c). */
    for (i = 0; i < n_dev; i++) {
        int item = dev_at(i, gx, gy, gz);
        if (item != NO_ITEM && gadd(T_DEV, i, item, 16))
            gxp += th[nth - 1].w;
    }
}

static const char *sec_name(int sec)
{
    switch (sec) {
    case SEC_RHAND: return "right hand";
    case SEC_LHAND: return "left hand";
    case SEC_BELT: return "belt";
    case SEC_RLEG: return "right leg";
    case SEC_LLEG: return "left leg";
    case SEC_RSHLD: return "right shoulder";
    case SEC_LSHLD: return "left shoulder";
    case SEC_PACK: return "backpack";
    }
    return "ground";
}

static const char *sec_label(int sec)
{
    switch (sec) {
    case SEC_RHAND: return "RIGHT HAND";
    case SEC_LHAND: return "LEFT HAND";
    case SEC_BELT: return "BELT";
    case SEC_RLEG: return "RIGHT LEG";
    case SEC_LLEG: return "LEFT LEG";
    case SEC_RSHLD: return "RIGHT SHOULDER";
    case SEC_LSHLD: return "LEFT SHOULDER";
    }
    return "BACK PACK";
}

/* The screen; fills th[] in the order the arrows walk it. */
static void draw(const Unit *un, int u)
{
    char t[40];
    int i, s, doll;

    uk_screen(UK_NOBG, T_GREY);
    uk_cga_palette(0);
    doll = un->armour >= 2 ? 14 + un->armour : (un->armour ? 8 : 0) + (u & 1) * 4 + (u >> 1) % 4;
    uk_hold(1);
    uk_pic(UI_DOLL + doll, -1, 0);
    uk_pic(UI_TAC01, -1, 0);
    for (s = SEC_RHAND; s <= SEC_PACK; s++)
        cells(s);
    for (i = 0; i <= 20; i++)
        uk_fill(i * 16, GROUND_Y, 1, 48, GRID);
    for (i = 0; i < 3; i++)
        uk_fill(0, GROUND_Y + i * 16, 320, 1, GRID);
    /* The sections' names above them (Inventory::drawGrid). */
    for (s = SEC_RHAND; s <= SEC_PACK; s++)
        uk_text(SEC_XY(s, 0), SEC_XY(s, 1) - 9, UK_SMALL, 10, sec_label(s));
    uk_text(0, GROUND_Y - 9, UK_SMALL, 10, "GROUND");
    uk_text(28, 6, UK_BIG, 14, unit_title(un));
    sprintf(t, "TUs> %d", un->tu);
    uk_text(245, 24, UK_SMALL, 10, pre ? "" : t);
    sprintf(t, "Acc> %d", un->accuracy);
    uk_text(245, 32, UK_SMALL, 10, t);
    sprintf(t, "React> %d", un->reactions);
    uk_text(245, 40, UK_SMALL, 10, t);
    uk_text(245, 48, UK_SMALL, 7, un->armour ? g_items[g_armours[un->armour].item].name : "No armour");

    nth = 0;
    if (un->weapon != NO_ITEM) {
        add_thing(T_WEAPON, 0, SEC_RHAND, un->weapon, 0, 0, 63, 32, 48);
        if (g_items[un->weapon].clip_size)
            count(2, 64, un->ammo);
    }
    if (un->grenade != NO_ITEM && un->n_grenades)
        add_thing(T_GRENADE, 0, SEC_LHAND, un->grenade, 0, 128, 63, 32, 48);
    for (s = SEC_RHAND; s <= SEC_PACK; s++)
        for (i = 0; i < g_max_carry; i++) {
            const Carry *c = &g_carry[i];
            int cx, cy;
            if (c->unit != u || SLOT_SEC(c->slot) != s)
                continue;
            cx = SEC_XY(s, 0) + SLOT_X(c->slot) * 16;
            cy = SEC_XY(s, 1) + SLOT_Y(c->slot) * 16;
            add_thing(T_CARRY, i, s, c->item, c->n, cx, cy, ITEM_W(c->item) * 16,
                      ITEM_H(c->item) * 16);
            if (c->item < N_ITEMS && g_items[c->item].kind == IK_WEAPON && g_items[c->item].clip_size)
                count(cx + 2, cy + 1, c->n & 63);
        }
    gfirst = nth;
    ground_row();
    if (goff && nth == gfirst) {        /* (nothing left that far) */
        goff = 0;
        ground_row();
    }
    uk_hold(0);
    uk_save(0, 200);
}

/* The thing's item and its n (inv.h Carry.n). */
static int thing_item(const Unit *un, const Thing *t, int *n)
{
    switch (t->kind) {
    case T_WEAPON:
        *n = inv_wn(un->weapon, un->load, un->ammo);
        return un->weapon;
    case T_GRENADE:
        *n = un->fuse;
        return un->grenade;
    case T_CARRY:
        *n = g_carry[t->idx].n;
        return g_carry[t->idx].item;
    case T_FLOOR: {
        const FloorItem *f = &g_floor[t->idx];
        /* clips: a part-used one's rounds (0x80 | rounds), else 0 */
        *n = g_items[f->item].kind != IK_CLIP ? f->n : f->n & 0x80 ? f->n & 0x7F : 0;
        return f->item;
    }
    case T_BODY:
        *n = t->idx;
        return IT_BODY;
    }
    *n = 0;
    return dev_at(t->idx, gx, gy, gz);
}

/* A body's name (InventoryState: an unconscious unit's own name, else
   its corpse item's). */
static const char *body_name(int u)
{
    const Unit *b = &g_units[u];
    int k = unit_body(b) - BODY_LIVE + 1;       /* its live alien, 1 .. N_LIVE */

    if (b->stunned)
        return b->name;
    if (k <= 0)
        return "Corpse";
    if (k <= 23)        /* sectoids 1-6, floaters 7-12, snakemen 13-17, mutons, ethereals */
        return g_items[IT_CORPSE_SECTOID + (k > 6) + (k > 12) + (k > 17) + (k > 20)].name;
    return k == 26 ? "Celatid Corpse" : k == 27 ? "Chryssalid Corpse" : "Silacoid Corpse";
}

static const char *iname(int item, int n)
{
    return item == IT_BODY ? body_name(n) : g_items[item].name;
}

static void item_text(char *out, int item, int n)
{
    int kind = ITEM_KIND(item);

    if (kind == IK_WEAPON)
        sprintf(out, g_items[item].clip_size ? "%s (%d rounds)" : "%s", iname(item, n), n & 63);
    else if ((item == IT_MEDIKIT && n != CARRY_ACTIVE) || (kind == IK_CLIP && n))
        sprintf(out, kind == IK_CLIP ? "%s (%d rounds)" : "%s (%d uses)", iname(item, n), n);
    else
        strcpy(out, iname(item, n));
}

static void show_line(const char *t, const char *note)
{
    uk_restore(0, 140, 320, 9);
    uk_text(48, 140, UK_SMALL, 14, t);         /* (after GROUND) */
    if (note[0])
        uk_text(286 - uk_width(UK_SMALL, note), 140, UK_SMALL, 12, note);    /* (before the arrow) */
}

static void show_thing(const Unit *un, int cur, const char *note)
{
    char t[48];
    const Thing *h = &th[cur];
    int n, item = thing_item(un, h, &n);

    item_text(t, item, n);
    if (ITEM_KIND(item) == IK_WEAPON && inv_wload(item, n))
        sprintf(t + strlen(t), " %s", ammo_find(item, inv_wload(item, n))->tag);
    if (h->kind == T_DEV)
        strcat(t, item == IT_FLARE ? " (lit)" : " (armed)");
    else if (ITEM_KIND(item) == IK_GRENADE && n && item != IT_FLARE)
        strcat(t, " (primed)");
    show_line(t, note);
    uk_rect(h->x, h->y, h->w + 1, h->h + 1, 14);
}

/* ------------------------------------------------------------------ */
/* Moving an item */

/* Takes the thing's item out of where it is: *n as Carry.n. */
static int take(Unit *un, int u, const Thing *t, int *n)
{
    int item;

    switch (t->kind) {
    case T_WEAPON:
        item = un->weapon;
        *n = inv_wn(item, un->load, un->ammo);
        un->weapon = NO_ITEM;
        un->ammo = un->load = 0;
        return item;
    case T_GRENADE:
        item = un->grenade;
        *n = un->fuse;
        un->grenade = NO_ITEM;
        un->n_grenades = un->fuse = 0;
        return item;
    case T_CARRY:
        item = g_carry[t->idx].item;
        *n = g_carry[t->idx].n;
        g_carry[t->idx].unit = CARRY_FREE;
        return item;
    case T_FLOOR: {
        FloorItem *f = &g_floor[t->idx];
        item = f->item;
        if (g_items[item].kind == IK_WEAPON || item == IT_MEDIKIT || (f->n & 0x80)) {
            *n = f->n & (g_items[item].kind == IK_CLIP ? 0x7F : 0xFF);
            f->item = NO_ITEM;
        } else {
            *n = 0;
            if (!--f->n)
                f->item = NO_ITEM;
        }
        return item;
    }
    case T_BODY:
        g_units[t->idx].special |= SP_CARRIED;
        *n = t->idx;
        return IT_BODY;
    }
    (void)u;
    /* a device off the ground (*n: its fuse + 1, 0 for a flare: Unit.fuse) */
    item = dev[t->idx].item;
    *n = dev[t->idx].kind == DEV_FLARE ? 0 : dev[t->idx].fuse + 1;
    fx_dev_remove(t->idx);
    return item;
}

/* Onto the ground: a body lies there, a part-used clip keeps its rounds
   (0x80 | rounds). */
static void to_ground(int item, int n)
{
    int kind = ITEM_KIND(item);

    if (item == IT_BODY) {
        Unit *b = &g_units[n];
        b->special &= ~SP_CARRIED;
        b->x = (unsigned char)gx;
        b->y = (unsigned char)gy;
        b->z = (unsigned char)gz;
    } else
        floor_add(gx, gy, gz, item, kind == IK_WEAPON || item == IT_MEDIKIT ? n
                                    : kind == IK_CLIP && n ? 0x80 | n : 1);
}

/* Puts it into section sec at slot (the ground: sec SEC_GROUND). */
static void put(Unit *un, int u, int slot, int item, int n)
{
    int sec = SLOT_SEC(slot), kind = ITEM_KIND(item);

    if (sec == SEC_GROUND) {
        if (n == CARRY_ACTIVE)
            n = un->medi;
        if (kind == IK_GRENADE && (n || item == IT_FLARE)
            && fx_drop(u, item, gx, gy, gz, n ? n - 1 : 0))
            return;
        to_ground(item, n);
    } else if (sec == SEC_RHAND && kind == IK_WEAPON) {
        un->weapon = (unsigned char)item;
        un->ammo = (unsigned char)(n & 63);
        un->load = (unsigned char)inv_wload(item, n);
    } else if (sec == SEC_LHAND && kind == IK_GRENADE) {
        un->grenade = (unsigned char)item;
        un->n_grenades = 1;
        un->fuse = (unsigned char)n;
    } else if (carry_add(u, slot, item, n) < 0)
        to_ground(item, n);
}

/* Unload (InventoryState::btnUnloadClick, Inventory::unload): the gun to
   the right hand, its clip (a part-used one keeps its rounds) to the left,
   8 TU; both hands must be empty but for the gun. */
static const char *unload(Unit *un, int u, const Thing *t)
{
    int n, item = thing_item(un, t, &n), lh = carry_lhand(un), clip, k;

    if (ITEM_KIND(item) != IK_WEAPON || !g_items[item].clip_size)
        return "";
    if (!(n & 63))
        return "No ammunition loaded!";
    if ((lh != NO_ITEM && !(t->kind == T_CARRY && t->sec == SEC_LHAND))
        || (carry_rhand(un) != NO_ITEM && t->kind != T_WEAPON))
        return "Both hands must be empty!";
    if (!pre && un->tu < 8)
        return "Not enough time units!";
    clip = inv_wload(item, n);
    if (!clip)
        clip = g_items[item].clip_item;
    take(un, u, t, &n);
    n &= 63;
    k = carry_add(u, SLOT(SEC_LHAND, 0, 0), clip, n == g_items[item].clip_size ? 0 : n);
    if (k < 0)
        to_ground(clip, n == g_items[item].clip_size ? 0 : n);
    put(un, u, SLOT(SEC_RHAND, 0, 0), item, 0);
    if (!pre)
        un->tu -= 8;
    log_printf("%s unloaded %s", un->name, g_items[item].name);
    return NULL;
}

/* Moves thing t to slot; a message, or NULL when it went. */
static const char *move(Unit *un, int u, const Thing *t, int slot)
{
    int sec = SLOT_SEC(slot), from = t->sec, cost = COST(from, sec), item, n, w = NO_ITEM, k;
    int skip = t->kind == T_CARRY ? t->idx : -1;
    unsigned char *am = NULL;

    item = thing_item(un, t, &n);
    if (item == NO_ITEM)
        return "";
    /* A clip onto an empty gun loads it (Inventory::mouseClick, "Put item
       in weapon": 15 TU, any clip the gun takes, wherever it is carried; the
       right hand's gun is in the unit, the others' clip type in their n). */
    if (sec == SEC_RHAND && un->weapon != NO_ITEM) {
        w = un->weapon;
        am = &un->ammo;
    } else
        for (k = 0; k < g_max_carry; k++) {
            Carry *c = &g_carry[k];
            int dx = SLOT_X(slot) - SLOT_X(c->slot), dy = SLOT_Y(slot) - SLOT_Y(c->slot);
            if (c->unit == u && SLOT_SEC(c->slot) == sec && dx >= 0 && dy >= 0
                && dx < ITEM_W(c->item) && dy < ITEM_H(c->item)) {
                w = c->item;
                am = &c->n;
                break;
            }
        }
    if (w < N_ITEMS && g_items[w].kind == IK_WEAPON && ITEM_KIND(item) == IK_CLIP && !(*am & 63)
        && (item == g_items[w].clip_item || ammo_find(w, item))) {
        int load = item == g_items[w].clip_item ? 0 : item;
        cost += 15;
        if (!pre && un->tu < cost)
            return "Not enough time units!";
        take(un, u, t, &n);
        n = n ? n : g_items[w].clip_size;
        if (am == &un->ammo) {
            un->ammo = (unsigned char)n;
            un->load = (unsigned char)load;
        } else
            *am = (unsigned char)inv_wn(w, load, n);
        if (!pre)
            un->tu -= (unsigned char)cost;
        log_printf("%s loaded %s %s", un->name, g_items[w].name, load ? ammo_find(w, load)->tag : "");
        return NULL;
    }
    if (sec != SEC_GROUND) {
        /* Within a hand nothing moves; elsewhere the item must fit. */
        if ((t->kind == T_WEAPON && sec == SEC_RHAND) || (t->kind == T_GRENADE && sec == SEC_LHAND))
            return "";
        if (!carry_fits(u, slot, item, skip))
            return "No room";
    } else if (from == SEC_GROUND)
        return "";
    if (!pre && un->tu < cost)
        return "Not enough time units!";
    take(un, u, t, &n);
    put(un, u, slot, item, n);
    if (!pre)
        un->tu -= (unsigned char)cost;
    inv_medikit(u);
    {
        const char *nm = iname(item, n);
        char nb[24];
        int kind = ITEM_KIND(item), c = kind == IK_WEAPON ? inv_wload(item, n) : 0;
        if (c) {                /* (a special clip's tag) */
            sprintf(nb, "%s %s", nm, ammo_find(item, c)->tag);
            nm = nb;
        }
        if (sec == SEC_GROUND)
            log_printf("%s dropped %s", un->name, nm);
        else if (from == SEC_GROUND)
            log_printf("%s picked up %s%s", un->name, nm,
                       kind == IK_GRENADE && n && item != IT_FLARE ? " (armed)" : "");
        else if (sec == SEC_RHAND && kind == IK_WEAPON)
            log_printf("%s armed %s", un->name, nm);
        else if (sec == SEC_LHAND && kind == IK_GRENADE)
            log_printf("%s readied %s", un->name, nm);
        else
            log_printf("%s moved %s to the %s", un->name, nm, sec_name(sec));
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Choosing where it goes: a cell (its top left) or the ground */

static int cell_x(int slot)
{
    return SLOT_SEC(slot) == SEC_GROUND ? 0 : SEC_XY(SLOT_SEC(slot), 0) + SLOT_X(slot) * 16;
}

static int cell_y(int slot)
{
    return SLOT_SEC(slot) == SEC_GROUND ? GROUND_Y : SEC_XY(SLOT_SEC(slot), 1) + SLOT_Y(slot) * 16;
}

/* The cell under a pixel, or -1. */
static int slot_at(int px, int py)
{
    int s, x, y;

    if (py >= GROUND_Y)
        return SLOT(SEC_GROUND, 0, 0);
    for (s = SEC_RHAND; s <= SEC_PACK; s++) {
        int x0 = SEC_XY(s, 0), y0 = SEC_XY(s, 1);
        if (s <= SEC_LHAND) {
            if (px >= x0 && px < x0 + 32 && py >= y0 && py < y0 + 48)
                return SLOT(s, 0, 0);
            continue;
        }
        x = (px - x0) >> 4;
        y = (py - y0) >> 4;
        if (px >= x0 && py >= y0 && x < 4 && y < 3
            && carry_cell(s, x, y))
            return SLOT(s, x, y);
    }
    return -1;
}

/* The nearest cell from slot in an arrow's direction (dx, dy). */
static int slot_step(int slot, int dx, int dy)
{
    int px = cell_x(slot) + 8, py = cell_y(slot) + 8, best = slot, bd = 0x7FFF, s, x, y;

    for (s = SEC_RHAND; s <= SEC_GROUND; s++)
        for (y = 0; y < 3; y++)
            for (x = 0; x < 4; x++) {
                int c = SLOT(s, x, y), qx, qy, d;
                if (s == SEC_LOAD || (s == SEC_GROUND ? x || y
                    : !carry_cell(s, x, y)))
                    continue;
                qx = cell_x(c) + 8 - px;
                qy = cell_y(c) + 8 - py;
                if ((dx && qx * dx <= 0) || (dy && qy * dy <= 0))
                    continue;
                d = (dx ? qx * dx + 2 * (qy < 0 ? -qy : qy) : qy * dy + 2 * (qx < 0 ? -qx : qx));
                if (d < bd) {
                    bd = d;
                    best = c;
                }
            }
    return best;
}

/* ------------------------------------------------------------------ */
/* Holding an item (Inventory::setSelectedItem, mouseOver, mouseClick):
   picked up, it leaves its place and its picture follows the pointer, as
   DOS's. The picture (at most 2 x 3 cells) is read from the pack once
   and kept in memory with a copy of the screen under it, so that a move
   puts that copy back and draws the picture at the new place. Without
   that memory (or picture), and on the keys' cells, an outline of its
   size, drawn with XOR so that it needs no copy of what is under it. */

/* The held picture (one heap block while held; DGROUP is resident): where
   it is drawn, then its pixels in strips for src/heldasm.asm (per row, per
   channel, EGA mask and planes 0-3, CGA mask and data: 0, its bytes, 0
   ...), then the save-under. NULL: the outline. */
typedef struct {
    unsigned voff, nc, rows, sh;        /* (heldasm.asm reads these seven) */
    unsigned pix, save, ega;
    int nb, h, x0, y0;
} Held;
static Held far *hp;
static int hp_logged;
#define STRIP   (g_video == VIDEO_EGA ? 6 : 10)

void __cdecl held_blit(Held far *h, unsigned what);    /* 0 save, 1 draw, 2 restore */

static void held_load(unsigned id)
{
    unsigned short hd[5];
    int ega = g_video == VIDEO_EGA, ch = ega ? 5 : 2, nb, r, k, c;
    unsigned pix, raw, save, need;
    unsigned char far *p, far *s;

    hp = NULL;
    if (!uk_read(id, 0, hd, sizeof hd) || !(hd[4] & UIF_MASKED) || hd[2] > 32 || hd[3] > 48)
        return;                 /* (no picture: the outline) */
    if (g_vga) {                /* UIV.PAK: a byte a pixel (0 clear), then the save-under */
        raw = hd[2] * hd[3];
        hp = malloc(sizeof(Held) + 2 * raw);
        if (!hp) {
            if (!hp_logged)
                log_printf("held item: no memory for its picture (%u bytes), an outline",
                           sizeof(Held) + 2 * raw);
            hp_logged = 1;
            return;
        }
        p = (unsigned char far *)(hp + 1);
        if (!uk_read(id, sizeof hd, p, raw)) {
            free(hp);
            hp = NULL;
            return;
        }
        hp->pix = FP_OFF(p);
        hp->save = FP_OFF(p) + raw;
        hp->nb = hd[2] / 4;     /* (4-pixel units, as CGA: the caller centres it) */
        hp->h = hd[3];
        return;
    }
    nb = hd[2] >> (ega ? 3 : 2);
    pix = hd[3] * ch * STRIP;
    raw = nb * hd[3] * ch;
    save = (nb + 1) * hd[3] * (ega ? 4 : 1);
    need = sizeof(Held) + pix + (raw > save ? raw : save);
#ifndef NOHELDPIC                       /* (test builds: the outline) */
    hp = malloc(need);
#endif
    if (!hp) {
        if (!hp_logged)
            log_printf("held item: no memory for its picture (%u bytes), an outline", need);
        hp_logged = 1;
        return;
    }
    p = (unsigned char far *)(hp + 1);
    s = p + pix;                /* the pack's rows go to the save-under first */
    if (!uk_read(id, sizeof hd, s, raw)) {
        free(hp);
        hp = NULL;
        return;
    }
    _fmemset(p, 0, pix);
    for (r = 0; r < hd[3]; r++)
        for (k = 0; k < nb; k++)
            for (c = 0; c < ch; c++)
                p[(r * ch + c) * STRIP + 1 + k] = *s++;
    hp->pix = FP_OFF(p);
    hp->save = FP_OFF(p) + pix;
    hp->ega = ega;
    hp->nb = nb;
    hp->h = hd[3];
}

/* Draws the picture with its top left at (x, y), what is under it saved
   first (draw), or puts that back (x, y unused). */
static void held_put(int draw, int x, int y)
{
    int ppb = hp->ega ? 8 : 4, nc;

    if (g_vga) {
        unsigned char far *pix = (unsigned char far *)MK_FP(FP_SEG(hp), hp->pix);
        unsigned char far *sv = (unsigned char far *)MK_FP(FP_SEG(hp), hp->save);
        int w = hp->nb * 4, r, k;
        if (draw) {
            hp->x0 = x;
            hp->y0 = y;
            hp->rows = y + hp->h > 200 ? 200 - y : hp->h;
            hp->nc = x + w > 320 ? 320 - x : w;
        }
        mouse_hide_rect(hp->x0, hp->y0, hp->x0 + hp->nc, hp->y0 + hp->rows);
        for (r = 0; r < (int)hp->rows; r++) {
            unsigned char far *d = (unsigned char far *)MK_FP(0xA000, (hp->y0 + r) * 320 + hp->x0);
            if (!draw) {
                _fmemcpy(d, sv + r * w, hp->nc);
                continue;
            }
            _fmemcpy(sv + r * w, d, hp->nc);
            for (k = 0; k < (int)hp->nc; k++)
                if (pix[r * w + k])
                    d[k] = pix[r * w + k];
        }
        return;
    }
    if (draw) {
        hp->x0 = x;
        hp->y0 = y;
        hp->rows = y + hp->h > 200 ? 200 - y : hp->h;
        nc = hp->nb + ((x & (ppb - 1)) != 0);
        if (x / ppb + nc > 320 / ppb)
            nc = 320 / ppb - x / ppb;
        hp->nc = nc;
        hp->sh = (x & (ppb - 1)) << !hp->ega;
        hp->voff = hp->ega ? y * 40 + x / 8 : (y & 1) * 0x2000 + (y >> 1) * 80 + x / 4;
    }
    mouse_hide_rect(hp->x0, hp->y0, hp->x0 + (hp->nb + 1) * ppb, hp->y0 + hp->rows);
    if (draw)
        held_blit(hp, 0);
    held_blit(hp, draw ? 1 : 2);
}

/* XOR colour: light red on black, red on the grid's grey (EGA); CGA
   inverts. Inside the screen only. */
static void xor_fill(int x, int y, int w, int h)
{
    int r, b, b0, b1;
    unsigned char m0, m1;

    if (x + w > 320)
        w = 320 - x;
    if (y + h > 200)
        h = 200 - y;
    if (w <= 0 || h <= 0)
        return;
    if (g_video == VIDEO_EGA) {
        b0 = x >> 3;
        b1 = (x + w - 1) >> 3;
        m0 = (unsigned char)(0xFF >> (x & 7));
        m1 = (unsigned char)(0xFF << (7 - ((x + w - 1) & 7)));
        outpw(0x3C4, 0x0F02);
        outpw(0x3CE, 0x0005);
        outpw(0x3CE, 0x1803);           /* XOR */
        outpw(0x3CE, 0x0C00);
        outpw(0x3CE, 0x0F01);
        for (r = 0; r < h; r++) {
            volatile unsigned char far *d = (volatile unsigned char far *)MK_FP(0xA000, (y + r) * 40);
            for (b = b0; b <= b1; b++) {
                unsigned char m = 0xFF, latch;
                if (b == b0)
                    m &= m0;
                if (b == b1)
                    m &= m1;
                outpw(0x3CE, (m << 8) | 8);
                latch = d[b];
                d[b] = latch;
            }
        }
        outpw(0x3CE, 0xFF08);
        outpw(0x3CE, 0x0001);
        outpw(0x3CE, 0x0003);
        return;
    }
    if (g_vga) {                        /* black to yellow (gfx.h VGA_VMAP) */
        unsigned char v = ((const unsigned char far *)MK_FP(g_vga, VGA_VMAP))[14];
        mouse_hide_rect(x, y, x + w, y + h);
        for (r = y; r < y + h; r++) {
            unsigned char far *d = (unsigned char far *)MK_FP(0xA000, r * 320 + x);
            for (b = 0; b < w; b++)
                d[b] ^= v;
        }
        return;
    }
    hgc_mark(x, y, x + w, y + h);       /* Hercules: shown at the next poll */
    b0 = x >> 2;
    b1 = (x + w - 1) >> 2;
    m0 = (unsigned char)(0xFF >> ((x & 3) * 2));
    m1 = (unsigned char)(0xFF << ((3 - ((x + w - 1) & 3)) * 2));
    for (r = y; r < y + h; r++) {
        unsigned char far *d = (unsigned char far *)MK_FP(0xB800, (r & 1) * 0x2000 + (r >> 1) * 80);
        for (b = b0; b <= b1; b++)
            d[b] ^= (unsigned char)((b == b0 ? m0 : 0xFF) & (b == b1 ? m1 : 0xFF));
    }
}

/* The outline (drawn twice: gone). */
static void xor_rect(int x, int y, int w, int h)
{
    xor_fill(x, y, w, 1);
    xor_fill(x, y + h - 1, w, 1);
    xor_fill(x, y + 1, 1, h - 2);
    xor_fill(x + w - 1, y + 1, 1, h - 2);
}

/* Where a thing lies: its box (bx, by, bw, bh) and the grid there (lines
   every gcw x gch pixels from gx0, gy0). */
static int bx, by, bw, bh, gx0, gy0, gcw, gch;

static void thing_box(const Thing *t, int item)
{
    bx = t->x;
    by = t->y;
    bw = t->w;
    bh = t->h;
    gx0 = 0;
    gy0 = GROUND_Y;
    gcw = gch = 16;
    if (t->sec <= SEC_LHAND) {
        gx0 = bx = SEC_XY(t->sec, 0);
        gy0 = by = SEC_XY(t->sec, 1);
        gcw = bw = 32;
        gch = bh = 48;
    } else if (t->sec != SEC_GROUND) {
        gx0 = SEC_XY(t->sec, 0);
        gy0 = SEC_XY(t->sec, 1);
        bw = ITEM_W(item) * 16;
        bh = ITEM_H(item) * 16;
    }
}

/* The thing's place empty: black with its grid lines (the lines on its
   right and bottom edges are its neighbours' and stay). */
static void blank(void)
{
    int k;

    uk_fill(bx, by, bw, bh, 0);
    for (k = gx0; k < bx + bw; k += gcw)
        if (k >= bx)
            uk_fill(k, by, 1, bh, GRID);
    for (k = gy0; k < by + bh; k += gch)
        if (k >= by)
            uk_fill(bx, k, bw, 1, GRID);
}

/* The outline of an iw x ih item centred on the pointer at (px, py),
   inside the screen: its top left (*ox, *oy); returns the slot of its top
   left cell (Inventory::mouseClick), else the one under the pointer, or -1. */
static int at_pointer(int px, int py, int iw, int ih, int *ox, int *oy)
{
    int x = px - iw / 2, y = py - ih / 2, s;

    *ox = x = x < 0 ? 0 : x > 320 - iw ? 320 - iw : x;
    *oy = y = y < 0 ? 0 : y > 200 - ih ? 200 - ih : y;
    s = slot_at(x + 8, y + 8);
    return s >= 0 ? s : slot_at(px, py);
}

/* The next key, or KEY_MOVE when the pointer moved from (x, y). */
static unsigned hold_key(int x, int y)
{
    for (;;) {
        if (input_ready())
            return input_key();
        if (g_mouse_x != x || g_mouse_y != y) {
            mouse_hide();
            return KEY_MOVE;
        }
    }
}

/* Holds thing t until it is put somewhere or back. mouse: picked up by a
   click (its picture at the pointer; a click puts it where its top left
   cell is, as Inventory::mouseClick; where it cannot go, the
   warning shows and it stays held, as DOS), else by Enter (the outline on
   the cell the keys choose: arrows, Enter; R / L / B / P / G at once).
   A move of the pointer switches to the pointer, an arrow to the cells.
   Right button or Esc: back where it was. Returns the note; *moved = 1
   when it went somewhere. */
static const char *hold(Unit *un, int u, const Thing *t, int mouse, int *moved)
{
    char line[56];
    const char *note = "";
    int n, item = thing_item(un, t, &n), slot, sec, last = -2, px = -1, py = 0, ox, oy, ow, oh;
    int iw = ITEM_W(item) * 16, ih = ITEM_H(item) * 16, c, put, pic_on, tm = 0, nmoves = 0, npic = 0;
    unsigned key;
    unsigned long t0 = 0, tsum = 0;

    *moved = 0;
    thing_box(t, item);
    blank();
    held_load(pic(item, n));
    slot = t->sec != SEC_GROUND ? (t->kind == T_CARRY ? g_carry[t->idx].slot : SLOT(t->sec, 0, 0))
         : (slot = carry_free_slot(u, item)) >= 0 ? slot : SLOT(SEC_BELT, 0, 0);
    if (mouse) {
        px = g_mouse_x;
        py = g_mouse_y;
    }
    log_printf("%s holds %s", un->name, iname(item, n));
    g_track = 1;
    for (;;) {
        if (px >= 0) {
            ow = iw;
            oh = ih;
            slot = at_pointer(px, py, iw, ih, &ox, &oy);
        } else {
            sec = SLOT_SEC(slot);
            ow = (sec >= SEC_BELT && sec <= SEC_PACK ? iw : sec == SEC_GROUND ? 320 : 32) + 1;
            oh = (sec >= SEC_BELT && sec <= SEC_PACK ? ih : 48) + 1;
            if (sec == SEC_GROUND)
                ow = 320;
            ox = cell_x(slot);
            oy = cell_y(slot);
        }
        sec = slot < 0 ? -1 : SLOT_SEC(slot);
        if (sec != last) {
            if (last >= -1)
                note = "";              /* it went on: the warning goes */
            last = sec;
            item_text(line, item, n);
            if (sec >= 0 && !note[0]) {
                sprintf(line + strlen(line), " to the %s", sec_name(sec));
                if (!pre)
                    sprintf(line + strlen(line), " (%d TU)", COST(t->sec, sec));
            }
            show_line(line, note);
        }
        /* The picture centred on the pointer (inside the screen, as the
           place it goes), else the outline; a move's undrawing and drawing
           are timed (LOG at the end). */
        if (tm)
            t0 = timer_read();
        pic_on = px >= 0 && hp;
        if (pic_on)
            held_put(1, ox + (iw - hp->nb * (g_video == VIDEO_EGA ? 8 : 4)) / 2, oy + (ih - hp->h) / 2);
        else
            xor_rect(ox, oy, ow, oh);
        if (tm) {
            tsum += timer_read() - t0;
            nmoves++;
        }
        key = hold_key(g_mouse_x, g_mouse_y);
        tm = key == KEY_MOVE;
        if (tm)
            t0 = timer_read();
        if (pic_on)
            held_put(0, 0, 0);
        else
            xor_rect(ox, oy, ow, oh);
        if (tm) {
            tsum += timer_read() - t0;
            npic += pic_on;
        }
        if (key == KEY_MOVE || key == KEY_LCLICK) {
            px = g_mouse_x;
            py = g_mouse_y;
            if (key == KEY_MOVE)
                continue;
            slot = at_pointer(px, py, iw, ih, &ox, &oy);
            if (slot < 0)
                continue;
            note = move(un, u, t, slot);
            if (!note) {
                *moved = 1;
                break;
            }
            if (!note[0])
                break;                  /* where it was */
            log_printf("%s", note);
            last = -3;                  /* the warning, it stays held */
            continue;
        }
        if (key == KEY_RCLICK || (key & 0xFF) == 27)
            break;
        /* Enter, or the keys for the usual places: R right hand, L left
           hand, B belt, P backpack (the first cell where it fits), G the
           ground. By the keys it is done either way. */
        c = (key & 0xFF) | 0x20;
        put = (key & 0xFF) == '\r' && slot >= 0;
        if (c == 'r' || c == 'l' || c == 'g') {
            slot = SLOT(c == 'r' ? SEC_RHAND : c == 'l' ? SEC_LHAND : SEC_GROUND, 0, 0);
            put = 1;
        } else if (c == 'b' || c == 'p') {
            int s = c == 'b' ? SEC_BELT : SEC_PACK, x, y;
            for (y = 0; y < 3 && !put; y++)
                for (x = 0; x < 4 && !put; x++)
                    if (carry_fits(u, SLOT(s, x, y), item, t->kind == T_CARRY ? t->idx : -1)) {
                        slot = SLOT(s, x, y);
                        put = 1;
                    }
        }
        if (put) {
            note = move(un, u, t, slot);
            *moved = !note;
            break;
        }
        if (slot < 0)
            slot = SLOT(SEC_GROUND, 0, 0);
        switch (key >> 8) {
        case 0x48: slot = slot_step(slot, 0, -1); px = -1; break;
        case 0x50: slot = slot_step(slot, 0, 1); px = -1; break;
        case 0x4B: slot = slot_step(slot, -1, 0); px = -1; break;
        case 0x4D: slot = slot_step(slot, 1, 0); px = -1; break;
        }
    }
    g_track = 0;
    if (nmoves) {
        tsum = tsum * 10 / 1193 / nmoves;       /* tenths of a ms */
        log_printf("held item: %d moves, %d with the picture, %lu.%lu ms a move",
                   nmoves, npic, tsum / 10, tsum % 10);
    }
    free(hp);
    hp = NULL;
    if (!*moved) {
        log_printf("%s put back %s", un->name, iname(item, n));
        uk_restore(bx, by, bw + 1, bh + 1);
    }
    return note;
}

/* ------------------------------------------------------------------ */

/* The next unit of the squad with an inventory (step 1 or -1). */
static int next_unit(int u, int step)
{
    int i, k = u;

    for (i = 0; i < g_nunits; i++) {
        const Unit *v;
        k = (k + step + g_nunits) % g_nunits;
        v = &g_units[k];
        if (v->alive && v->faction == FACTION_XCOM && !UNIT_HWP(v)
            && (!pre || UNIT_SOLDIER(v)))
            return k;
    }
    return u;
}

/* A unit that keeps its clips and grenades as counts (aliens under
   control): they go into its inventory while it is shown. */
static void unpack_counts(Unit *un, int u)
{
    while (un->clips && un->weapon != NO_ITEM
           && carry_place(u, g_items[un->weapon].clip_item, 0) >= 0)
        un->clips--;
    while (un->n_grenades > 1 && carry_place(u, un->grenade, 0) >= 0)
        un->n_grenades--;
}

/* InventoryState::saveEquipmentLayout: what each soldier now carries. */
static void save_layouts(void)
{
    int u, i, n;

    for (u = 0; u < g_nunits; u++) {
        const Unit *un = &g_units[u];
        Soldier *s;
        if (!UNIT_SOLDIER(un) || un->roster < 0)
            continue;
        s = &g_roster[un->roster];
        n = 0;
#define LAY(it, sl) if (n < LAYOUT_N) { s->layout[n][0] = (unsigned char)(it); \
                                        s->layout[n++][1] = (unsigned char)(sl); }
        if (un->weapon != NO_ITEM) {
            LAY(un->weapon, SLOT(SEC_RHAND, 0, 0));
            if (un->load)
                LAY(un->load, SLOT(SEC_LOAD, 0, 0));
        }
        if (un->grenade != NO_ITEM)
            LAY(un->grenade, SLOT(SEC_LHAND, 0, 0));
        for (i = 0; i < g_max_carry; i++)
            if (g_carry[i].unit == u) {
                int c = g_carry[i].item < N_ITEMS && g_items[g_carry[i].item].kind == IK_WEAPON
                        ? inv_wload(g_carry[i].item, g_carry[i].n) : 0;
                LAY(g_carry[i].item, g_carry[i].slot);
                if (c)
                    LAY(c, SLOT(SEC_LOAD, 0, 0));
            }
#undef LAY
        if (n < LAYOUT_N)
            s->layout[n][0] = NO_ITEM;
        log_printf("LAYOUT %s: %d items", s->name, n);
    }
}

void inv_screen(int u)
{
    Unit *un;
    int cur = 0, redraw = 1, i, sel;
    const char *note = "";
    const unsigned char *cm = g_uk_cmap;

    pre = u < 0;
    goff = 0;
    if (pre) {
        /* Before the battle: everyone at the craft's inventory tile, where
           the craft's other items lie (units_spawn). */
        u = next_unit(g_nunits - 1, 1);
        gx = g_units[u].x;
        gy = g_units[u].y;
        gz = g_units[u].z;
        log_printf("WIN Inventory before the battle");
        if (g_popup_auto) {
            save_layouts();
            return;
        }
    }
    un = &g_units[u];
    uk_open();
    gfx_night(0);               /* the screen in day colours */
    g_uk_cmap = NULL;
    for (;;) {
        unsigned key;
        int scan, ch;

        if (redraw) {
            un = &g_units[u];
            if (!pre) {
                gx = un->x;
                gy = un->y;
                gz = un->z;
            }
            unpack_counts(un, u);
            draw(un, u);
            redraw = 0;
        }
        if (cur >= nth)
            cur = nth - 1;
        if (cur >= 0)
            show_thing(un, cur, note);
        else
            show_line("", note);
        note = "";
        key = input_key();
        if (cur >= 0)
            uk_restore(th[cur].x, th[cur].y, th[cur].w + 1, th[cur].h + 1);
        if (key == KEY_RCLICK)
            break;
        sel = -1;
        if (key == KEY_LCLICK) {
            int x = g_mouse_x, y = g_mouse_y;
            if (y < 23 && x >= 237 && x < 273)
                break;                          /* OK */
            if (y < 23 && x >= 273) {
                u = next_unit(u, x < 297 ? -1 : 1);
                redraw = 1;
                cur = goff = 0;
                continue;
            }
            for (i = 0; i < nth; i++)
                if (x >= th[i].x && x < th[i].x + th[i].w && y >= th[i].y && y < th[i].y + th[i].h)
                    sel = i;
            if (x >= 288 && y >= 32 && y < 57)
                key = 'u';                      /* Unload (TAC01's button) */
            else if (x >= 289 && y >= 137 && y < 152)
                key = 0x5100;                   /* the ground's arrow: the next page */
            else if (sel < 0)
                continue;
            else {
                cur = sel;
                key = 0x1C0D;
            }
        }
        scan = key >> 8;
        ch = (key & 0xFF) | 0x20;
        if (scan == 0x01 || ch == 'i')
            break;
        if ((key & 0xFF) == '\t' || ch == '>' || ch == '.' || ch == '<' || ch == ',') {
            u = next_unit(u, ch == '<' || ch == ',' ? -1 : 1);
            redraw = 1;
            cur = goff = 0;
            continue;
        }
        if (scan == 0x47 || scan == 0x4F) {
            cur = scan == 0x47 ? 0 : nth - 1;   /* Home, End */
            continue;
        }
        if (scan == 0x48 || scan == 0x4B) {
            if (cur > 0)
                cur--;
            continue;
        }
        if (scan == 0x49 || scan == 0x51 || ((scan == 0x50 || scan == 0x4D) && cur >= nth - 1 && gnext)) {
            /* The ground row's pages: PgDn, the arrow, or on past the last
               thing (the last page wraps to the first, as DOS); PgUp the first. */
            goff = scan == 0x49 ? 0 : gnext;
            log_printf("ground row from entry %d", goff);
            redraw = 1;
            cur = gfirst;
            continue;
        }
        if (scan == 0x50 || scan == 0x4D) {
            if (cur < nth - 1)
                cur++;
            continue;
        }
        if (cur < 0)
            continue;
        if ((key & 0xFF) == '\r') {
            note = hold(un, u, &th[cur], sel >= 0, &redraw);
            if (!note)
                note = "";
            continue;
        }
        if (ch == 'd' || ch == 'u') {
            note = ch == 'u' ? unload(un, u, &th[cur]) : move(un, u, &th[cur], SLOT(SEC_GROUND, 0, 0));
            if (!note)
                note = "";
            redraw = 1;
        }
    }
    if (pre)
        save_layouts();
    else
        bt_sel = u;
    g_uk_cmap = cm;
    gfx_night(g_night);
    uk_close();
}
