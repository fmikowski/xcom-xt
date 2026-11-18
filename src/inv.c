/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battle inventory: floor items, what units carry (DOS's sections), the
   recovery after a battle. The inventory screen is invscr.c. */

#include <stdio.h>
#include <string.h>
#include "base.h"
#include "effects.h"
#include "facil.h"
#include "input.h"
#include "inv.h"
#include "items.h"
#include "unit.h"

/* In the near heap, sized by lim_init (src/startup.c). */
FloorItem __near *g_floor, __near *g_floor_end;
int g_max_floor;
Carry __near *g_carry, __near *g_carry_end;
int g_max_carry;

/* items.rul invWidth << 2 | invHeight (1 x 1 when not given). */
const unsigned char g_item_inv[N_ITEMS] = {
    6, 7, 11, 6, 7, 11, 6, 7, 11,       /* pistol .. heavy plasma */
    5, 5, 9, 5, 5, 5,                   /* clips (cannon AP 2 x 1) */
    5, 5, 9,                            /* grenade, alien grenade, high explosive */
    5, 5, 5, 5,                         /* armour, alloys, elerium */
    6,                                  /* medi-kit */
    7, 10, 5, 5,                        /* stun rod, small launcher, stun bomb, scanner */
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,    /* live aliens */
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    11, 9, 11, 7, 11, 6,                /* auto-cannon .. blaster bomb */
    5, 5, 5, 5, 5, 5, 5, 5,             /* craft weapons */
    7, 10,                              /* psi-amp, mind probe */
    5, 5, 5, 5, 5, 5, 5, 5,             /* UFO components */
    11, 11, 11, 11, 11,                 /* corpses */
    5, 5, 5, 5, 5,                      /* fusion ball, craft */
    9, 9, 9, 9, 7, 7,                   /* special ammunition */
    5, 5, 5,                            /* smoke, proximity grenade, flare */
    11, 11, 11, 11, 11, 5, 5, 5,        /* HWPs, their ammunition */
    5,                                  /* flying suit */
};

/* items.rul weight of each body's corpseBattle item (BODY_*: the three
   soldiers' corpses, civilians, the HWP wrecks, the live aliens' types);
   0: a 2x2 unit's (invWidth 0, it cannot be picked up). */
const unsigned char g_body_weight[N_BODIES] = {
    22, 24, 26, 30, 50, 0, 0,
    30, 30, 30, 30, 30, 30,             /* sectoids */
    20, 20, 20, 20, 20, 20,             /* floaters */
    40, 40, 40, 40, 40,                 /* snakemen */
    40, 40, 40,                         /* mutons */
    25, 25, 25,                         /* ethereals */
    0, 0, 35, 40, 40, 0,                /* cyberdisc, sectopod, celatid, chryssalid, silacoid, reaper */
};

int inv_body_weight(int unit)
{
    int b = unit_body(&g_units[unit]);

    return b < 0 ? 0 : g_body_weight[b];
}

void floor_clear(void)
{
    FloorItem __near *f;
    for (f = g_floor; f < g_floor_end; f++)
        f->item = NO_ITEM;
}

/* What a floor entry (or loot) brings home to the stores (an Elerium-115
   item: 50, alienFuel); 1 for a gun of alien make (X-COM's own lie at the
   craft too). */
int inv_home(int item, int n)
{
    const ItemDef *d = &g_items[item];

    if (d->kind == IK_CLIP || d->kind == IK_GRENADE)
        g_base.stores[item] += n & 0x80 ? 1 : n;     /* (part-used: as a gun's) */
    else {
        g_base.stores[item] += item == IT_ELERIUM ? 50 : 1;
        /* A loaded gun brings its clip back too. */
        if (d->kind == IK_WEAPON && d->clip_item != NO_ITEM && (n & 63))
            g_base.stores[(n >> 6) ? inv_wload(item, n) : d->clip_item]++;
    }
    return d->kind == IK_WEAPON && !d->buy_k && (item < IT_LPISTOL || item > IT_HLASER);
}

/* An entry off the full floor list (inv.h). A FloorItem starts with x, y,
   z as a Unit does. */
static void to_loot(const FloorItem *f)
{
    int k = carry_add(CARRY_LOOT, unit_in_exit_area((const Unit *)f), f->item, f->n);

    if (k < 0)
        inv_home(f->item, f->n);
    log_printf("floor full: item %d to carry %d", f->item, k);      /* -1: the stores */
}

int floor_add(int x, int y, int z, int item, int n)
{
    int i, kind;
    FloorItem *f, t;

    if (item == NO_ITEM || item >= N_ITEMS)
        return -1;
    kind = g_items[item].kind;
    /* Clips and grenades of one type stack on a tile; a part-used clip
       (n 0x80 | rounds) lies apart. */
    if ((kind == IK_CLIP || kind == IK_GRENADE) && !(n & 0x80))
        for (i = 0; i < g_max_floor; i++) {
            f = &g_floor[i];
            if (f->item == item && f->x == x && f->y == y && f->z == z && f->n < 120) {
                f->n += (unsigned char)n;
                return i;
            }
        }
    for (i = 0; i < g_max_floor && g_floor[i].item != NO_ITEM; i++)
        ;
    /* Full: an entry aboard goes to the loot for it, else the item does. */
    if (i == g_max_floor)
        for (i = 0; i < g_max_floor && !unit_in_exit_area((const Unit *)&g_floor[i]); i++)
            ;
    f = i < g_max_floor ? &g_floor[i] : &t;
    if (i < g_max_floor && f->item != NO_ITEM)
        to_loot(f);
    f->x = (unsigned char)x;
    f->y = (unsigned char)y;
    f->z = (unsigned char)z;
    f->item = (unsigned char)item;
    f->n = (unsigned char)n;
    if (i < g_max_floor)
        return i;
    to_loot(f);
    return -1;
}

int floor_count_at(int x, int y, int z)
{
    int n = 0;
    const FloorItem __near *f;
    for (f = g_floor; f < g_floor_end; f++)
        if (f->item != NO_ITEM && f->x == x && f->y == y && f->z == z)
            n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* What units carry */

void carry_clear(void)
{
    memset(g_carry, CARRY_FREE, g_max_carry * sizeof(Carry));
}

int carry_add(int unit, int slot, int item, int n)
{
    Carry __near *c;

    for (c = g_carry; c < g_carry_end; c++)
        if (c->unit == CARRY_FREE) {
            c->unit = (unsigned char)unit;
            c->slot = (unsigned char)slot;
            c->item = (unsigned char)item;
            c->n = (unsigned char)n;
            return c - g_carry;
        }
    return -1;
}

int carry_find(int unit, int item)
{
    const Carry __near *c;

    for (c = g_carry; c < g_carry_end; c++)
        if (c->unit == unit && c->item == item)
            return c - g_carry;
    return -1;
}

int carry_at(int unit, int sec)
{
    const Carry __near *c;

    for (c = g_carry; c < g_carry_end; c++)
        if (c->unit == unit && SLOT_SEC(c->slot) == sec)
            return c - g_carry;
    return -1;
}

/* A cell of a section (inventories.rul slots): the belt is 4 x 2 without
   the two middle cells of its lower row. */
int carry_cell(int sec, int x, int y)
{
    switch (sec) {
    case SEC_BELT:
        return x < 4 && (!y || (y == 1 && (!x || x == 3)));
    case SEC_PACK:
        return x < 3 && y < 3;
    case SEC_RLEG: case SEC_LLEG: case SEC_RSHLD: case SEC_LSHLD:
        return x < 2 && !y;
    }
    return !x && !y;
}

int carry_fits(int unit, int slot, int item, int skip)
{
    int sec = SLOT_SEC(slot), x = SLOT_X(slot), y = SLOT_Y(slot), w = ITEM_W(item);
    int h = ITEM_H(item), i, j;
    const Carry __near *c;

    if (sec <= SEC_LHAND) {
        if (unit < CARRY_ROSTER && (sec ? g_units[unit].grenade : g_units[unit].weapon) != NO_ITEM)
            return 0;
        w = h = 1;
    }
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (!carry_cell(sec, x + i, y + j))
                return 0;
    for (c = g_carry; c < g_carry_end; c++) {
        int cx, cy;
        if (c->unit != unit || c - g_carry == skip || SLOT_SEC(c->slot) != sec)
            continue;
        if (sec <= SEC_LHAND)
            return 0;
        cx = SLOT_X(c->slot);
        cy = SLOT_Y(c->slot);
        if (cx < x + w && x < cx + ITEM_W(c->item) && cy < y + h && y < cy + ITEM_H(c->item))
            return 0;
    }
    return 1;
}

int carry_free_slot(int unit, int item)
{
    int sec, x, y;

    for (sec = SEC_BELT; sec <= SEC_PACK; sec++)
        for (y = 0; y < 3; y++)
            for (x = 0; x < 4; x++)
                if (carry_fits(unit, SLOT(sec, x, y), item, -1))
                    return SLOT(sec, x, y);
    return -1;
}

int carry_place(int unit, int item, int n)
{
    int s = carry_free_slot(unit, item);

    return s < 0 ? -1 : carry_add(unit, s, item, n);
}

static int hand_item(const Unit *u, int sec)
{
    int k = carry_at((int)(u - g_units), sec);

    return k < 0 ? NO_ITEM : g_carry[k].item;
}

/* The first g_ammo entry of a weapon with special clips (inv.h inv_wn). */
static const AmmoDef *ammo_first(int w)
{
    const AmmoDef *a = g_ammo;

    while (a->weapon != w)
        a++;
    return a;
}

int inv_wn(int w, int load, int ammo)
{
    const AmmoDef *a = load && ammo ? ammo_find(w, load) : NULL;

    return a ? ammo | (int)(a - ammo_first(w)) << 6 : ammo;
}

int inv_wload(int w, int n)
{
    return n >> 6 ? ammo_first(w)[n >> 6].clip : 0;
}

void inv_arm(Unit *u, FloorItem *f)
{
    u->weapon = f->item;
    u->ammo = f->n & 63;
    u->load = (unsigned char)inv_wload(f->item, f->n);
    f->item = NO_ITEM;
}

int carry_rhand(const Unit *u)
{
    return u->weapon != NO_ITEM ? u->weapon : hand_item(u, SEC_RHAND);
}

/* Aliens' Unit.grenade is on their belt. */
int carry_lhand(const Unit *u)
{
    return u->grenade != NO_ITEM && UNIT_SOLDIER(u) ? u->grenade : hand_item(u, SEC_LHAND);
}

int inv_hand(const Unit *u, int left, int *n)
{
    int k;

    if (!left && u->weapon != NO_ITEM) {
        *n = u->ammo;
        return u->weapon;
    }
    if (left && u->grenade != NO_ITEM && UNIT_SOLDIER(u)) {
        *n = u->fuse;
        return u->grenade;
    }
    k = carry_at((int)(u - g_units), left ? SEC_LHAND : SEC_RHAND);
    *n = k < 0 ? 0 : g_carry[k].n;
    if (k >= 0 && ITEM_KIND(g_carry[k].item) == IK_WEAPON)
        *n &= 63;
    return k < 0 ? NO_ITEM : g_carry[k].item;
}

int inv_mirror(int s)
{
    Unit *u = &g_units[s];
    int need;
    Carry __near *c;

    if (!UNIT_SOLDIER(u))
        return 0;
    need = (u->weapon != NO_ITEM) + (u->grenade != NO_ITEM);
    for (c = g_carry; c < g_carry_end && need; c++)
        need -= c->unit == CARRY_FREE;
    if (need)
        return 0;
    /* Both hands as entries, then each to the other hand. */
    if (u->weapon != NO_ITEM)
        carry_add(s, SLOT(SEC_RHAND, 0, 0), u->weapon, inv_wn(u->weapon, u->load, u->ammo));
    if (u->grenade != NO_ITEM)
        carry_add(s, SLOT(SEC_LHAND, 0, 0), u->grenade, u->fuse);
    u->weapon = u->grenade = NO_ITEM;
    u->ammo = u->load = u->n_grenades = u->fuse = 0;
    for (c = g_carry; c < g_carry_end; c++) {
        int sec = SLOT_SEC(c->slot), kind;
        if (c->unit != s || sec > SEC_LHAND)
            continue;
        c->slot ^= SLOT(1, 0, 0);
        sec = !sec;
        kind = ITEM_KIND(c->item);
        /* Back into the unit's fields: the right hand's weapon, the left's
           grenade. */
        if (sec == SEC_RHAND && kind == IK_WEAPON) {
            u->weapon = c->item;
            u->ammo = c->n & 63;
            u->load = inv_wload(c->item, c->n);
        } else if (sec == SEC_LHAND && kind == IK_GRENADE) {
            u->grenade = c->item;
            u->n_grenades = 1;
            u->fuse = c->n;
        } else
            continue;
        c->unit = CARRY_FREE;
    }
    u->special ^= SP_MIRROR;
    return 1;
}

/* TU from each section to the right hand and to the left (inventories.rul). */
static const unsigned char to_hand[2][SEC_PACK + 1] = {
    { 0, 4, 4, 4, 6, 3, 3, 8 }, { 4, 0, 4, 6, 4, 3, 3, 8 }
};

int inv_reach(int u, int item, int *hand, int *cost)
{
    const Unit *un = &g_units[u];
    int h = carry_lhand(un) == NO_ITEM ? SEC_LHAND : carry_rhand(un) == NO_ITEM ? SEC_RHAND : -1;
    const Carry __near *c;

    for (c = g_carry; c < g_carry_end && h >= 0; c++)
        if (c->unit == u && c->item == item && SLOT_SEC(c->slot) > SEC_LHAND
            && SLOT_SEC(c->slot) <= SEC_PACK) {
            *hand = h;
            *cost = to_hand[h][SLOT_SEC(c->slot)];
            return c - g_carry;
        }
    return -1;
}

int inv_clips(const Unit *u)
{
    int n = u->clips, s = (int)(u - g_units);
    const Carry __near *c;

    if (u->weapon == NO_ITEM)
        return 0;
    for (c = g_carry; c < g_carry_end; c++)
        if (c->unit == s && (c->item == g_items[u->weapon].clip_item || ammo_find(u->weapon, c->item)))
            n++;
    return n;
}

/* The kit in use: one in a hand before one elsewhere (it must be in a
   hand to be used, as DOS). */
void inv_medikit(int u)
{
    Unit *un = &g_units[u];
    int i, k = -1, a = -1;

    for (i = 0; i < g_max_carry; i++)
        if (g_carry[i].unit == u && g_carry[i].item == IT_MEDIKIT) {
            if (g_carry[i].n == CARRY_ACTIVE)
                a = i;
            if (k < 0 || (SLOT_SEC(g_carry[i].slot) <= SEC_LHAND
                          && SLOT_SEC(g_carry[k].slot) > SEC_LHAND))
                k = i;
        }
    if (a >= 0 && (a == k || SLOT_SEC(g_carry[a].slot) <= SEC_LHAND))
        return;
    if (a >= 0)
        g_carry[a].n = un->medi;
    un->medi = un->stims = un->pkills = 0;
    if (k >= 0) {
        un->medi = un->stims = un->pkills = g_carry[k].n;
        g_carry[k].n = CARRY_ACTIVE;
    }
}

void inv_bind(int r, int u)
{
    Unit *un = &g_units[u];
    Carry __near *c;

    for (c = g_carry; c < g_carry_end; c++) {
        int sec = SLOT_SEC(c->slot), kind = g_items[c->item].kind;
        if (c->unit != (CARRY_ROSTER | r))
            continue;
        c->unit = (unsigned char)u;
        if (sec == SEC_RHAND && kind == IK_WEAPON) {
            un->weapon = c->item;
            un->ammo = c->n & 63;
            un->load = (unsigned char)inv_wload(c->item, c->n);
        } else if (sec == SEC_LHAND && kind == IK_GRENADE) {
            un->grenade = c->item;
            un->n_grenades = 1;
            un->fuse = c->n;
        } else
            continue;
        c->unit = CARRY_FREE;
    }
    inv_medikit(u);
}

int inv_next_grenade(int u, int *cost)
{
    const unsigned char *to_left = to_hand[1];
    int i, best = -1;

    if (g_units[u].grenade != NO_ITEM || carry_at(u, SEC_LHAND) >= 0)
        return -1;
    for (i = 0; i < g_max_carry; i++)
        if (g_carry[i].unit == u && ITEM_KIND(g_carry[i].item) == IK_GRENADE
            && SLOT_SEC(g_carry[i].slot) <= SEC_PACK
            && (best < 0 || to_left[SLOT_SEC(g_carry[i].slot)]
                            < to_left[SLOT_SEC(g_carry[best].slot)]))
            best = i;
    if (best >= 0)
        *cost = to_left[SLOT_SEC(g_carry[best].slot)];
    return best;
}

void inv_drop_all(int u)
{
    Unit *un = &g_units[u];
    int x = un->x, y = un->y, z = un->z;
    Carry __near *c;

    if (un->weapon != NO_ITEM) {
        floor_add(x, y, z, un->weapon, inv_wn(un->weapon, un->load, un->ammo));
        if (un->clips && g_items[un->weapon].clip_item != NO_ITEM)
            floor_add(x, y, z, g_items[un->weapon].clip_item, un->clips);
    }
    /* A grenade primed in hand lies there primed (src/effects.c). */
    if (un->fuse && un->grenade != NO_ITEM && un->n_grenades
        && fx_drop(u, un->grenade, x, y, z, un->fuse - 1))
        un->n_grenades--;
    un->fuse = 0;
    if (un->grenade != NO_ITEM && un->n_grenades)
        floor_add(x, y, z, un->grenade, un->n_grenades);
    for (c = g_carry; c < g_carry_end; c++) {
        if (c->unit != u)
            continue;
        if (c->item == IT_BODY) {
            inv_body_down(c - g_carry, x, y, z);
            continue;
        }
        c->unit = CARRY_FREE;
        if (g_items[c->item].kind == IK_GRENADE && c->n && c->item != IT_FLARE
            && fx_drop(u, c->item, x, y, z, c->n - 1))
            continue;
        floor_add(x, y, z, c->item, c->n == CARRY_ACTIVE ? un->medi
                  : g_items[c->item].kind == IK_WEAPON || c->item == IT_MEDIKIT ? c->n
                  : g_items[c->item].kind == IK_CLIP && c->n ? 0x80 | c->n : 1);
    }
    un->weapon = un->grenade = NO_ITEM;
    un->ammo = un->load = un->clips = un->n_grenades = un->medi = un->stims = un->pkills = 0;
    un->special &= ~SP_MIRROR;
}

void inv_body_down(int k, int x, int y, int z)
{
    Unit *b = &g_units[g_carry[k].n];

    g_carry[k].unit = CARRY_FREE;
    b->special &= ~SP_CARRIED;
    b->x = (unsigned char)x;
    b->y = (unsigned char)y;
    b->z = (unsigned char)z;
}

int inv_body_entry(int u)
{
    const Carry __near *c;

    for (c = g_carry; c < g_carry_end; c++)
        if (c->unit != CARRY_FREE && c->item == IT_BODY && c->n == u)
            return c - g_carry;
    return -1;
}

void inv_bodies_sync(void)
{
    const Carry __near *c;

    for (c = g_carry; c < g_carry_end; c++)
        if (c->unit < MAX_UNITS && c->item == IT_BODY) {
            const Unit *k = &g_units[c->unit];
            Unit *b = &g_units[c->n];
            b->x = k->x;
            b->y = k->y;
            b->z = k->z;
        }
}

/* ------------------------------------------------------------------ */
/* After the battle */

int g_captured, g_lost_captives;

/* The containment space used at the current base (Base::getUsedContainment):
   the live aliens in the stores, those on their way, and one for each
   interrogation under way (the project holds the alien it used up until it
   ends or is cancelled). */
int live_aliens(void)
{
    int i, n = 0;

    for (i = IT_LIVE; i <= IT_LIVE_LAST; i++)
        n += g_base.stores[i];
    for (i = 0; i < MAX_TRANSFERS; i++)
        if (g_base.arriving[i].what > IT_LIVE && g_base.arriving[i].what <= IT_LIVE_LAST + 1)
            n += g_base.arriving[i].n;
    if (g_bb)
        for (i = 0; i < BASE_REC->n_res; i++) {
            int item = g_topics[BASE_REC->res[i].topic].need_item;
            n += item >= IT_LIVE && item <= IT_LIVE_LAST;
        }
    return n;
}

int g_corpses;
