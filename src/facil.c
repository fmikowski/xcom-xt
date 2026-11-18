/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Base facilities: the 6x6 grid, construction, capacities, upkeep, radar
   (the screens and the base defence are in facilscr.c). Costs, days,
   upkeep and capacities are the DOS game's (OpenXcom standard/xcom1/
   facilities.rul). */

#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>
#include "base.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "inv.h"
#include "gfx.h"
#include "hud.h"
#include "uik.h"
#include "input.h"
#include "map.h"
#include "psi.h"
#include "sound.h"
#include "unit.h"

Facilities g_facil;

/* The hyper-wave decoder: radar 2759 nautical miles, finds every UFO in
   range and tells its mission and race (facilities.rul hyper). */
const FacilDef g_fdefs[N_FACILITIES] = {
    { "",                   "",    0,    0,  0, 1, 0xFF,  0,  0,   0,    0,  0, 20, REQ_NONE, 0 },
    { "Access Lift",        "LFT", 300,  1,  4, 1, 0xFF,  0,  0,   0,    0,  0,  0, REQ_NONE, 15 },
    { "Living Quarters",    "QRT", 400, 16, 10, 1, CAP_QUARTERS, 50, 0, 0, 0, 0, 1, REQ_NONE, 10 },
    { "Laboratory",         "LAB", 750, 26, 30, 1, CAP_LABS, 50, 0, 0,   0,  0,  2, REQ_NONE, 11 },
    { "Workshop",           "WRK", 800, 32, 35, 1, CAP_WORKSHOPS, 50, 0, 0, 0, 0, 3, REQ_NONE, 6 },
    { "General Stores",     "STO", 150, 10,  5, 1, CAP_STORES, 50, 0, 0, 0,  0,  7, REQ_NONE, 7 },
    { "Hangar",             "HANGAR", 200, 25, 25, 2, CAP_HANGARS, 1, 0, 0, 0, 0, 16, REQ_NONE, 9 },
    { "Small Radar System", "RAD", 500, 12, 10, 1, 0xFF,  0, 25, 10,   0,  0,  4, REQ_NONE, 13 },
    { "Large Radar System", "LRD", 800, 25, 15, 1, 0xFF,  0, 38, 20,   0,  0,  5, REQ_NONE, 13 },
    { "Alien Containment",  "ALC", 400, 18, 15, 1, CAP_ALIENS, 10, 0, 0, 0,  0,  8, REQ_NONE, 5 },
    { "Missile Defences",   "MSL", 200, 16,  5, 1, 0xFF,  0,  0,  0, 500, 50,  6, REQ_NONE, 12 },
    { "Laser Defences",     "LAS", 400, 24, 10, 1, 0xFF,  0,  0,  0, 600, 60,  9, RS_LASER_DEF, 12 },
    { "Plasma Defences",    "PLS", 600, 34, 12, 1, 0xFF,  0,  0,  0, 900, 70, 10, RS_PLASMA_DEF, 12 },
    { "Grav Shield",        "GRV", 1200, 38, 15, 1, 0xFF, 0, 0, 0,  0,  0, 12, RS_GRAV_SHIELD, 14 },
    { "Psionic Laboratory", "PSI", 750, 24, 16, 1, CAP_PSI, 10, 0, 0,  0,  0, 14, RS_PSILAB, 3 },
    { "Fusion Ball Defences","FUS", 800, 34, 14, 1, 0xFF,  0,  0,  0, 1200, 80, 11, RS_FUSION_DEF, 12 },
    { "Mind Shield",        "MND", 1300, 33, 5, 1, 0xFF,  0,  0,  0,  0,  0, 13, RS_MIND_SHIELD, 14 },
    { "Hyper-wave Decoder", "HWD", 1400, 26, 30, 1, 0xFF, 0, 41, 100, 0,  0, 15, RS_HYPERWAVE, 13 },
};

/* Storage space per item in tenths (OpenXcom items.rul sizes). */
const unsigned char g_item_size[N_ITEMS] = {
    1, 2, 3, 1, 2, 3, 1, 2, 2,      /* weapons */
    1, 1, 1, 1, 1, 3,               /* clips */
    1, 1, 2,                        /* grenades */
    8, 8,                           /* armour */
    1, 1,                           /* alloys, elerium */
    1,                              /* medi-kit */
    1, 2, 1, 1,                     /* stun rod, small launcher, stun bomb, scanner */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* live aliens: in containment */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    3, 1, 4, 2, 3, 2,               /* auto-cannon .. blaster bomb */
    15, 1, 8, 4, 10, 15, 20, 12,    /* craft weapons and ammunition */
    1, 1,                           /* psi-amp, mind probe */
    7, 2, 2, 2, 2, 2, 1, 2,         /* UFO components */
    4, 4, 4, 4, 4,                  /* corpses */
    20, 6,                          /* fusion ball launcher, fusion ball */
    0, 0, 0,                        /* craft: in hangars */
    1, 1, 1, 1, 2, 2, 1, 1, 1,      /* special ammo, smoke, proximity, flare */
    60, 60, 60, 60, 60,             /* HWPs */
    1, 6, 6,                        /* HWP ammunition */
    8,                              /* flying suit */
};

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
    int i, j, s = g_fdefs[type].size;

    for (j = 0; j < s; j++)
        for (i = 0; i < s; i++)
            g_facil.cell[y + j][x + i] = (unsigned char)(type | (j * 2 + i) << 5);
    g_facil.days[y][x] = (unsigned char)days;
}

/* The current base's header: finished radars and decoders, and the
   chance that a retaliation UFO finds the base. */
void facil_summary(void)
{
    BaseHdr *h;
    int x, y, cells = 0, shields = 0;

    if (!g_bb)
        return;
    h = &g_bhdr[g_bcur];
    memset(h->radar, 0, sizeof h->radar);
    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++) {
            int t = FAC_TYPE(g_facil.cell[y][x]);
            if (!built_at(x, y))
                continue;
            cells++;
            if (FAC_PART(g_facil.cell[y][x]))
                continue;
            if (t == FAC_SRADAR || t == FAC_LRADAR)
                h->radar[t - FAC_SRADAR]++;
            else if (t == FAC_HYPER)
                h->radar[2]++;
            else if (t == FAC_MIND)
                shields++;
        }
    h->detect = (unsigned char)((cells / 6 + 15) / (shields + 1));
    log_printf("BASE %s radars %d/%d/%d, found %d%%", h->name, h->radar[0], h->radar[1],
               h->radar[2], h->detect);
}

void facil_new(void)
{
    memset(&g_facil, 0, sizeof g_facil);
    put(FAC_LIFT, 2, 2, 0);
    put(FAC_HANGAR, 2, 0, 0);
    put(FAC_HANGAR, 0, 4, 0);
    put(FAC_HANGAR, 4, 4, 0);
    put(FAC_QUARTERS, 3, 2, 0);
    put(FAC_STORES, 2, 3, 0);
    put(FAC_LAB, 3, 3, 0);
    put(FAC_WORKSHOP, 4, 3, 0);
    put(FAC_SRADAR, 1, 3, 0);
    facil_summary();
}

static char msg[48];

const char *facil_day(void)
{
    const char *m = NULL;
    int x, y;

    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++)
            if (g_facil.cell[y][x] && !FAC_PART(g_facil.cell[y][x]) && g_facil.days[y][x]
                && --g_facil.days[y][x] == 0) {
                sprintf(msg, "Construction complete: %s", g_fdefs[FAC_TYPE(g_facil.cell[y][x])].name);
                m = msg;
                base_popup(POP_BUILT, FAC_TYPE(g_facil.cell[y][x]));
            }
    if (m)
        facil_summary();
    return m;
}

/* Calls fn for the top-left cell of every finished facility. */
#define EACH_BUILT(x, y, d) \
    for (y = 0; y < BASE_W; y++) \
        for (x = 0; x < BASE_W; x++) \
            if (g_facil.cell[y][x] && !FAC_PART(g_facil.cell[y][x]) && !g_facil.days[y][x] \
                && ((d) = &g_fdefs[g_facil.cell[y][x]]) != NULL)

int facil_capacity(int cap)
{
    const FacilDef *d;
    int x, y, n = 0;

    EACH_BUILT(x, y, d)
        if (d->cap == cap)
            n += d->cap_n;
    return n;
}

/* Staff and soldiers at the base and on their way. */
int facil_personnel(void)
{
    return g_base.scientists + g_base.engineers + roster_alive() + base_arriving(TR_SCIENTIST)
         + base_arriving(TR_ENGINEER) + base_arriving(TR_SOLDIER);
}

/* Items in the stores, aboard the base's craft (Base::getUsedStores) and
   on their way. */
int facil_stores_used(void)
{
    int i, k, n = 0;

    for (i = 0; i < N_ITEMS; i++) {
        unsigned c = g_base.stores[i];
        if (g_bb)
            for (k = 0; k < MAX_CRAFT; k++)
                if (CRAFT_HERE(&g_craft[k]))
                    c += g_craft[k].items[i];
        n += c * g_item_size[i];
    }
    for (i = 0; i < MAX_TRANSFERS; i++)
        if (g_base.arriving[i].what && g_base.arriving[i].what <= N_ITEMS)
            n += g_base.arriving[i].n * g_item_size[g_base.arriving[i].what - 1];
    return n;
}

int facil_upkeep(void)
{
    const FacilDef *d;
    int x, y, n = 0;

    EACH_BUILT(x, y, d)
        n += d->upkeep_k;
    return n;
}

int facil_radar_range(void)
{
    const FacilDef *d;
    int x, y, r = 0;

    EACH_BUILT(x, y, d)
        if (d->radar > r)
            r = d->radar;
    return r;
}

/* Radar facilities in the header's order: small, large, hyper-wave. */
static const unsigned char radar_type[3] = { FAC_SRADAR, FAC_LRADAR, FAC_HYPER };

int facil_in_range(int b, long d2)
{
    const BaseHdr *h = &g_bhdr[b];
    int k, in = 0;

    for (k = 2; k >= 0; k--) {
        int r = g_fdefs[radar_type[k]].radar;
        if (h->radar[k] && d2 <= (long)r * r) {
            if (k == 2)
                return 2;
            in = 1;
        }
    }
    return in;
}

int facil_detect(int b, long d2, int visibility)
{
    const BaseHdr *h = &g_bhdr[b];
    int k, chance = 0;

    for (k = 0; k < 3; k++) {
        const FacilDef *d = &g_fdefs[radar_type[k]];
        if (h->radar[k] && d2 <= (long)d->radar * d->radar) {
            if (k == 2)
                return 2;               /* radarChance 100 */
            chance += d->radar_pct * h->radar[k];
        }
    }
    return chance && (int)rnd(100) < chance * (100 + visibility) / 100;
}

void facil_battle_layout(unsigned char *blocks)
{
    int x, y;

    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++) {
            unsigned char c = g_facil.cell[y][x];
            blocks[y * BASE_W + x] = built_at(x, y)
                ? (unsigned char)(g_fdefs[FAC_TYPE(c)].block + FAC_PART(c)) : 0xFF;
        }
}

/* ------------------------------------------------------------------ */
/* The base seen from above (BASEBITS.PCK), as the DOS basescape: in the
   hourly section so that the basescape and the facility screen each
   need only their own section next to it. */

/* BASEBITS.PCK frames of each facility: outline shape, picture (facilities.rul). */
static const unsigned char shape[N_FACILITIES][2] = {
    { 0, 0 }, { 2, 17 }, { 1, 18 }, { 1, 19 }, { 1, 20 }, { 1, 24 }, { 9, 9 }, { 2, 21 },
    { 1, 22 }, { 1, 25 }, { 2, 23 }, { 3, 26 }, { 3, 27 }, { 2, 29 }, { 1, 31 },
    { 3, 28 }, { 2, 30 }, { 2, 32 }
};
/* Craft pictures (frame - 33) by craft type. */
static const unsigned char craft_sprite[N_CRAFT_TYPES] = { 3, 4, 1, 2, 0 };

void facil_draw_view(int x0, int y0)
{
    int x, y, i, j, k = 0, n_spr = 0;
    unsigned char spr[MAX_CRAFT];
    unsigned key = 0, check = 0;

    /* The craft at home, one a hangar. */
    for (i = 0; i < MAX_CRAFT; i++)
        if (CRAFT_HERE(&g_craft[i]) && g_craft[i].state == CS_HANGAR)
            spr[n_spr++] = craft_sprite[g_craft[i].type];
    /* The same view again (back from a base screen): the copy uik keeps in
       spare video memory, under two hashes of what it shows (cells, days,
       craft), instead of 30 KB of pictures from the pack. */
    for (i = 0; i < 2 * BASE_W * BASE_W + n_spr; i++) {
        key = key * 31 + (i < 2 * BASE_W * BASE_W ? ((unsigned char *)g_facil.cell)[i]   /* then days */
                                                  : spr[i - 2 * BASE_W * BASE_W]);
        check += key;
    }
    key |= 0x8000u;
    if (uk_recall(key, check, x0, y0))
        return;
    uk_hold(1);
    /* One square of earth from the pack, the others copied. */
    uk_pic(UI_BASE_EARTH, x0, y0);
    for (y = 0; y < BASE_W; y++)
        for (x = !y; x < BASE_W; x++)
            uk_copy(x0, y0, x0 + x * 32, y0 + y * 32, 32, 32);
    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++) {
            unsigned char c = g_facil.cell[y][x];
            int s, days, num = 0;
            if (!FAC_TYPE(c) || FAC_PART(c))
                continue;
            s = g_fdefs[FAC_TYPE(c)].size;
            days = g_facil.days[y][x];
            for (j = 0; j < s; j++)
                for (i = 0; i < s; i++, num++)
                    uk_pic(UI_BASEBITS + shape[FAC_TYPE(c)][0] + num + (days ? (s > 1 ? 4 : 3) : 0),
                           x0 + (x + i) * 32, y0 + (y + j) * 32);
        }
    /* Corridors between finished neighbours. */
    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++) {
            unsigned char c = g_facil.cell[y][x];
            int s;
            if (!FAC_TYPE(c) || FAC_PART(c) || g_facil.days[y][x])
                continue;
            s = g_fdefs[FAC_TYPE(c)].size;
            for (i = 0; i < s; i++) {
                if (x + s < BASE_W && built_at(x + s, y + i))
                    uk_pic(UI_BASEBITS + 7, x0 + (x + s) * 32 - 16, y0 + (y + i) * 32);
                if (y + s < BASE_W && built_at(x + i, y + s))
                    uk_pic(UI_BASEBITS + 8, x0 + (x + i) * 32, y0 + (y + s) * 32 - 16);
            }
        }
    for (y = 0; y < BASE_W; y++)
        for (x = 0; x < BASE_W; x++) {
            unsigned char c = g_facil.cell[y][x];
            int t = FAC_TYPE(c), s;
            if (!t || FAC_PART(c))
                continue;
            s = g_fdefs[t].size;
            if (s == 1)
                uk_pic(UI_BASEBITS + shape[t][1], x0 + x * 32, y0 + y * 32);
            if (g_facil.days[y][x]) {
                char d[4];
                sprintf(d, "%d", g_facil.days[y][x]);
                uk_center(x0 + x * 32, s * 32, y0 + y * 32 + s * 16 - 8, UK_BIG, 14, d);
            } else if (t == FAC_HANGAR && k < n_spr)
                uk_pic(UI_BASEBITS + 33 + spr[k++], x0 + x * 32 + 16, y0 + y * 32 + 12);
        }
    uk_hold(0);
    uk_keep(key, check, x0, y0, BASE_W * 32, BASE_W * 32);
}
