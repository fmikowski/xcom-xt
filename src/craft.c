/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Craft: types, weapons, hangars, the troop transport. */

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
#include "gfx.h"
#include "hud.h"
#include "hwp.h"
#include "input.h"
#include "map.h"
#include "sound.h"
#include "timer.h"
#include "uik.h"

/* Speeds: 1 world map pixel per hour is about 69 knots (the Skyranger's
   760 knots are 11 pixels per hour). Fuel: speed / 100 per 10 minutes;
   Elerium craft burn one unit per 10 minutes and keep fuel in tenths.
   Rent (cost_k): Skyranger $500K, Interceptor $600K a month, alien craft
   nothing (crafts.rul costRent; bought for the same). Soldiers as in DOS:
   Skyranger 14, Lightning 12, Avenger 26; each lands with its own craft
   map and seats (src/pak.c, "XCR1"). */
const CraftType g_craft_types[N_CRAFT_TYPES] = {
    /* name,        fuel,  hp, speed, fuel/h, $K, score, weapons, soldiers, elerium, HWPs */
    { "INTERCEPTOR", 1000,  100,  30,    126, 600, 250, 2,  0, 0, 0 },
    { "FIRESTORM",    200,  500,  61,     60,   0, 250, 2,  0, 1, 0 },
    { "LIGHTNING",    300,  800,  45,     60,   0, 300, 1, 12, 1, 0 },
    { "AVENGER",      600, 1200,  78,     60,   0, 400, 2, 26, 1, 4 },
    { "SKYRANGER",   1500,  150,  11,     42, 500, 200, 0, 14, 0, 3 },
};


/* name, launcher, ammo, damage, range, accuracy, reload c/s/a, ammo, per item, rearm/h */
const CraftWeapon g_craft_weapons[N_CRAFT_WEAPONS] = {
    { "CANNON",       IT_CRAFT_CANNON, IT_CANNON_ROUNDS,  10, 10, 25, {  4,  4,  4 }, 200, 50, 100 },
    { "STINGRAY",     IT_STINGRAY_L,   IT_STINGRAY_M,     70, 30, 70, { 64, 48, 32 },   6,  1,   1 },
    { "AVALANCHE",    IT_AVALANCHE_L,  IT_AVALANCHE_M,   100, 60, 80, { 96, 72, 48 },   3,  1,   1 },
    { "LASER CANNON", IT_LASER_CANNON, NO_ITEM,           70, 21, 35, { 24, 24, 24 }, 100,  0,  50 },
    { "PLASMA BEAM",  IT_PLASMA_BEAM,  NO_ITEM,          140, 52, 50, { 24, 24, 24 }, 100,  0,  50 },
    { "FUSION BALL",  IT_FUSION_L,     IT_FUSION_BALL,   230, 65,100, { 64, 48, 32 },   2,  1,   1 },
};

/* hp, power, range, reload, break-off ticks, score, top speed, size
   (0 very small .. 4 very large) */
const UfoCombat g_ufo_combat[N_UFO_COMBAT] = {
    {    0,   0,  0,  0,    0,   0,  0,  0 },
    {   50,   0,  0, 56,  200,  50, 32,  0 },   /* small scout */
    {  200,  20, 15, 56,  250,  75, 35,  1 },   /* medium scout */
    {  250,  20, 34, 48,  300, 125, 39,  1 },   /* large scout */
    {  500,  40, 22, 48,  500, 250, 58,  2 },   /* harvester */
    {  500,  40, 20, 32,  500, 250, 62,  2 },   /* abductor */
    { 1200, 120, 42, 24, 2000, 500, 70,  3 },   /* terror ship */
    { 3000, 140, 65, 24, 4000, 700, 72,  4 },   /* battleship */
    { 2200,  60, 36, 24, 3000, 400, 46,  3 },   /* supply ship */
};

/* REPAIR_HOUR, REFUEL_HALF, ELERIUM_FUEL: craft.h */

static char msgbuf[48];

const char *craft_name(const Craft *c)
{
    static char name[24];
    sprintf(name, "%s-%d", g_craft_types[c->type].name, c->num);
    return name;
}


void craft_new(void)
{
    int i;
    Craft *c;

    memset(g_craft, 0, sizeof g_craft);
    for (i = 0; i < MAX_CRAFT; i++) {
        g_craft[i].weapon[0] = g_craft[i].weapon[1] = CW_NONE;
        g_craft[i].target = -1;
    }
    c = craft_new_slot(CT_SKYRANGER);
    c->state = CS_HANGAR;
    c->pad = HP_READY;
    c->fuel = g_craft_types[CT_SKYRANGER].fuel;
    /* Its load (startingBase.rul): base_new left these out of the stores. */
    c->items[IT_GRENADE] = 8;
    c->items[IT_CLIP_CANNON] = 2;
    c->items[IT_CLIP_CANNON_HE] = 2;
    c->items[IT_HCANNON] = 1;
    c->items[IT_PISTOL] = 3;
    c->items[IT_CLIP_PISTOL] = 5;
    c->items[IT_RIFLE] = 6;
    c->items[IT_CLIP_RIFLE] = 12;
    for (i = 0; i < 2; i++) {
        c = craft_new_slot(CT_INTERCEPTOR);
        c->state = CS_HANGAR;
        c->pad = HP_READY;
        c->fuel = g_craft_types[0].fuel;
        c->weapon[0] = CW_STINGRAY;
        c->ammo[0] = g_craft_weapons[CW_STINGRAY].ammo_max;
        c->weapon[1] = CW_CANNON;
        c->ammo[1] = g_craft_weapons[CW_CANNON].ammo_max;
    }
    /* The original starting stores. */
    g_base.stores[IT_CRAFT_CANNON] += 2;
    g_base.stores[IT_CANNON_ROUNDS] += 1;
    g_base.stores[IT_STINGRAY_L] += 1;
    g_base.stores[IT_STINGRAY_M] += 25;
    g_base.stores[IT_AVALANCHE_L] += 1;
    g_base.stores[IT_AVALANCHE_M] += 10;
}

/* ------------------------------------------------------------------ */
/* Hangar */

static int needs_rearm(const Craft *c, int slot)
{
    int w = c->weapon[slot];
    return w != CW_NONE && c->ammo[slot] < g_craft_weapons[w].ammo_max;
}

void craft_checkup(Craft *c)
{
    int s;

    c->pad = 0;
    for (s = 0; s < 2; s++)
        if (needs_rearm(c, s))
            c->pad |= HP_R(s);
    if (c->pad)
        c->pad |= HP_ARM;
}

/* Craft::checkup's order, repairs (an hour at a time), rearming (one
   weapon an hour), refuelling (every half hour: 50 units, or 5 units for
   one Elerium-115). */
const char *craft_hour(void)
{
    const char *msg = NULL;
    int i;

    for (i = 0; i < MAX_CRAFT; i++) {
        Craft *c = &g_craft[i];
        if (c->state == CS_ORDERED && --c->timer <= 0) {
            c->state = CS_HANGAR;
            craft_checkup(c);
            sprintf(msgbuf, "%s has arrived at %.16s.", craft_name(c), g_bhdr[c->base].name);
            msg = msgbuf;
        }
        if (c->state != CS_HANGAR)
            continue;
        if (c->damage > 0) {
            if ((c->damage -= REPAIR_HOUR) <= 0) {
                c->damage = 0;
                c->pad |= HP_ARM;
            }
        } else if (c->pad & HP_ARM)
            craft_rearm(c, i);
    }
    return msg;
}

/* Craft::refuel for the craft refuelling. A craft short of Elerium-115
   is told once (Not enough Elerium-115 ...); it is ready if it has fuel
   left, else it keeps waiting. */
void craft_half_hour(void)
{
    int i;

    for (i = 0; i < MAX_CRAFT; i++) {
        Craft *c = &g_craft[i];
        const CraftType *t = &g_craft_types[c->type];
        unsigned *st = base_stores(c->base);

        if (c->state != CS_HANGAR || c->damage || (c->pad & (HP_ARM | HP_READY)))
            continue;
        if (c->fuel < t->fuel) {
            if (!t->elerium)
                c->fuel += REFUEL_HALF;
            else if (st[IT_ELERIUM]) {
                st[IT_ELERIUM]--;
                c->fuel += ELERIUM_FUEL;
                c->pad &= ~HP_LOW;
            } else if (!(c->pad & HP_LOW)) {
                geo_popup(PU_CRAFTERR, i, IT_ELERIUM);
                c->pad |= c->fuel > 0 ? HP_READY : HP_LOW;
            }
        }
        if (c->fuel >= t->fuel) {
            c->fuel = t->fuel;
            c->pad |= c->pad & (HP_R(0) | HP_R(1)) ? HP_ARM : HP_READY;
        }
    }
}

/* Ready (STR_READY). */
int craft_ready(const Craft *c)
{
    return c->state == CS_HANGAR && c->damage == 0 && (c->pad & HP_READY);
}

int craft_can_launch(const Craft *c)
{
    return craft_ready(c) && (c->weapon[0] != CW_NONE || c->weapon[1] != CW_NONE);
}

long craft_rent(void)
{
    long rent = 0;
    int i;

    for (i = 0; i < MAX_CRAFT; i++)
        if (CRAFT_HERE(&g_craft[i]))
            rent += g_craft_types[g_craft[i].type].cost_k;
    return rent;
}

void craft_lose(Craft *c)
{
    int i, n = 0, k = (int)(c - g_craft) + 1;

    geo_score(c->x / 16, c->y / 16, -g_craft_types[c->type].score);
    /* GeoscapeState::time5Seconds: every soldier aboard dies (killSoldier:
       the roster drops them as after a battle), the HWPs go with the
       craft. */
    for (i = base_roster_n(c->base); i-- > 0; ) {
        Soldier *s = base_soldier(c->base, i);
        if (s->craft != k)
            continue;
        if (s->recovery)
            s->craft = 0;               /* wounded: seated, not aboard */
        else {
            base_soldier_remove(c->base, i);
            n++;
        }
    }
    log_printf("CRAFT %s lost, %d killed", craft_name(c), n);
    c->state = CS_EMPTY;
}

void craft_crew_clear(const Craft *c)
{
    int i, k = (int)(c - g_craft) + 1;

    for (i = 0; i < base_roster_n(c->base); i++)
        if (base_soldier(c->base, i)->craft == k)
            base_soldier(c->base, i)->craft = 0;
}

int craft_crew(int k)
{
    int i, n = 0;

    for (i = 0; k >= 0 && i < g_roster_n; i++)
        n += SOLDIER_READY(&g_roster[i]) && g_roster[i].craft == k + 1;
    return n;
}

const char *craft_status(const Craft *c)
{
    static char buf[12];
    switch (c->state) {
    case CS_OUT: return "OUT";
    case CS_RETURN: return "OUT";
    case CS_ORDERED:
        sprintf(buf, "Due %dh", c->timer);
        return buf;
    }
    if (c->damage > 0)
        return "REPAIRS";
    if (c->pad & HP_ARM)
        return "REARMING";
    return c->pad & HP_READY ? "READY" : "REFUELLING";
}

/* Next number for a craft of this type (Firestorm-1, Firestorm-2...),
   over every base. */
static int next_num(int type)
{
    int k, num = 1;
    for (k = 0; k < MAX_CRAFT; k++)
        if (g_craft[k].state != CS_EMPTY && g_craft[k].type == type && g_craft[k].num >= num)
            num = g_craft[k].num + 1;
    return num;
}

/* An empty craft slot set up for a new craft of this type at the current
   base, or NULL. */
Craft *craft_new_slot(int type)
{
    Craft *c;
    int i;

    for (i = 0; i < MAX_CRAFT && g_craft[i].state != CS_EMPTY; i++)
        ;
    if (i == MAX_CRAFT)
        return NULL;
    c = &g_craft[i];
    memset(c, 0, sizeof *c);
    c->type = (unsigned char)type;
    c->base = (unsigned char)g_bcur;
    c->num = next_num(type);
    c->weapon[0] = c->weapon[1] = CW_NONE;
    c->target = -1;
    c->state = CS_ORDERED;          /* holds the slot; callers set the state */
    return c;
}

/* Craft still to be built at the base keep their hangars free. */
#define ship_in_production base_ships_in_production

int craft_hangars_used(void)
{
    int i, n = ship_in_production();
    for (i = 0; i < MAX_CRAFT; i++)
        if (CRAFT_HERE(&g_craft[i]))
            n++;
    return n;
}

int craft_hangar_free(void)
{
    int i, slots = -ship_in_production();
    for (i = 0; i < MAX_CRAFT; i++)
        if (g_craft[i].state == CS_EMPTY)
            slots++;
    return slots > 0 && craft_hangars_used() < facil_capacity(CAP_HANGARS);
}

int craft_add(int type)
{
    Craft *c = craft_new_slot(type);

    if (!c)
        return 0;
    /* Delivered with an empty tank: Elerium craft refuel from stores. */
    c->state = CS_HANGAR;
    log_printf("CRAFT %s added to the hangar", craft_name(c));
    return 1;
}

/* ------------------------------------------------------------------ */
/* The squad's transport */

int g_troop_craft = -1;

int craft_best_transport(void)
{
    int i, best = -1;

    for (i = 0; i < MAX_CRAFT; i++) {
        const Craft *c = &g_craft[i];
        const CraftType *t = &g_craft_types[c->type];
        if (CRAFT_HERE(c) && t->soldiers && craft_ready(c)
            && (best < 0 || t->soldiers > g_craft_types[g_craft[best].type].soldiers))
            best = i;
    }
    return best;
}

int craft_for_cydonia(void)
{
    int i, pass, cur = g_bcur, fit;

    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < MAX_CRAFT; i++) {
            const Craft *c = &g_craft[i];
            if (c->type != CT_AVENGER || !craft_ready(c) || (c->base == cur) == pass)
                continue;
            base_select(c->base);
            fit = craft_crew(i) || c->hwp[0];   /* its crew or HWPs aboard */
            base_select(cur);
            if (fit || c->hwp[0])     /* DOS: soldiers or vehicles aboard */
                return i;
        }
    return -1;
}

const char *craft_troop_name(int b)
{
    /* The briefing asks after free_world has parked the bases block (and
       g_craft with it): the name as it was last read. */
    static char name[24];

    (void)b;
    if (g_troop_craft < 0)
        return "Transport";
    if (g_bb)
        strcpy(name, craft_name(&g_craft[g_troop_craft]));
    return name;
}

int craft_seats(const Craft *c)
{
    int k, n = g_craft_types[c->type].soldiers;

    for (k = 0; k < MAX_HWP && c->hwp[k]; k++)
        n -= HWP_SEATS;
    return n;
}

int craft_low_fuel(const Craft *c)
{
    const CraftType *t = &g_craft_types[c->type];
    int home = geo_distance(c->x / 16, c->y / 16, g_bhdr[c->base].x, g_bhdr[c->base].y);

    return c->fuel <= (int)((long)home * t->fuel_hour / t->speed);
}
