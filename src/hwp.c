/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Heavy weapons platforms in battle, the setup part (overlay section of
   pak.c; compiled with -zc: the table stays in the section): the unit sets
   of the HWPs on the mission and their places in the craft, before the
   soldiers (OpenXcom BattlescapeGenerator::addXCOMVehicle; the DOS stats
   of units.rul and armors.rul). */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "batui.h"
#include "battle_in.h"
#include "bsave.h"
#include "gfx.h"
#include "hwp.h"
#include "input.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "unit.h"

Hwp g_hwp[MAX_BHWP];
int g_hwp_n;
unsigned char g_hwp_hurt[MAX_HWP][6];

#define NO_SET      0xFF

static unsigned char sets[N_HWP];
unsigned char g_pic_early;      /* tight: the HWPs before the map's pictures (main.c) */
unsigned char g_pic_note;       /* pictures shared (1), or a vehicle left aboard (2) */

/* units.rul and armors.rul: the battle's name, TU, reactions, armour front,
   side, rear, under; all have health 90, firing 60, strength 60, stamina
   100 (50 back a turn), bravery 110. */
static const struct {
    char name[13];
    unsigned char tu, reactions, front, side, rear, under;
} types[N_HWP] = {
    { "Tank/Cannon",   70, 20,  90,  75,  60,  60 },
    { "Tank/Rocket",   70, 20,  90,  75,  60,  60 },
    { "Tank/Laser",    70, 20,  90,  75,  60,  60 },
    { "Hover/Plasma", 100, 30, 130, 130, 130, 100 },
    { "Hover/Launch", 100, 30, 130, 130, 130, 100 },
};

static unsigned free_kb(void)
{
    union REGS r;

    r.h.ah = 0x48;
    r.w.bx = 0xFFFF;
    int86(0x21, &r, &r);
    return r.w.bx / 64;
}

/* The HWP sets of the names pic_plan left (of a saved battle's: the same
   order, so the units' sets come out the same): first the packs ("+name":
   into the picture store, picplan.c), then those that use another type's
   pictures. */
void hwp_load(void)
{
    char *name;
    int i, t, n;

    memset(sets, NO_SET - 1, sizeof sets);
    for (n = 0; n < g_hwp_n + N_HWP; n++) {
        t = n < g_hwp_n ? g_hwp[n].item - IT_TANK_CANNON : n - g_hwp_n;
        name = g_pack_names[BU_HWP_PACKS + t];
        if (sets[t] != NO_SET - 1 || !name[0] || name[0] == '*')
            continue;
        if (name[0] == '+' && !xs_units(name + 1, 0, &sets[t]))
            log_printf("HWP %s: its pictures in the store", types[t].name);
        else if (unit_set_load(name + (name[0] == '+'), &sets[t])) {
            log_printf("HWP %s: %s does not load (free %u KB)", types[t].name, name, free_kb());
            sets[t] = NO_SET - 1;
            name[0] = 0;
        }
    }
    for (t = 0; t < N_HWP; t++) {
        name = g_pack_names[BU_HWP_PACKS + t];
        if (name[0] == '*') {
            i = name[1] - '0';
            if (sets[i] >= NO_SET - 1
                || unit_set_alias(sets[i], t >= 3 && i < 3 ? ALIAS_HOVER : ALIAS_SAME,
                                  t >= 3 ? UNIT_FRAMES + 32 : UNIT_FRAMES, &sets[t])) {
                sets[t] = NO_SET - 1;
                name[0] = 0;
            } else {
                g_pic_note |= 1;
                log_printf("HWP %s: the picture of %s", types[t].name, types[i].name);
            }
        }
    }
    for (i = 0; i < g_hwp_n; i++) {
        t = g_hwp[i].item - IT_TANK_CANNON;
        if (sets[t] >= NO_SET - 1) {
            log_printf("HWP %s left aboard: no room (free %u KB)", g_items[g_hwp[i].item].name,
                       free_kb());
            g_pic_note |= 2;
        }
    }
}

int start_tile(int x, int y, int z)
{
    const unsigned char *t = TILE(x, y, z);
    int p;

    for (p = 0; p < 4; p++)
        if (t[p] && g_pak.mcd[t[p]].special == SPECIAL_START)
            return 1;
    return 0;
}

/* Room for a 2x2 HWP with its first quarter at x, y, z: a start tile, the
   four standable at about the same height and free (all four start tiles
   if strict). */
static int room(int x, int y, int z, int strict)
{
    int i, j, h, h0 = 0;

    if (x + 1 >= (int)g_mx || y + 1 >= (int)g_my || !start_tile(x, y, z))
        return 0;
    for (j = 0; j < 2; j++)
        for (i = 0; i < 2; i++) {
            if (!tile_standable(x + i, y + j, z, &h) || UNIT_AT(x + i, y + j, z)
                || (strict && !start_tile(x + i, y + j, z)))
                return 0;
            if (!i && !j)
                h0 = h;
            else if (h - h0 > 8 || h0 - h > 8)
                return 0;
        }
    return 1;
}

static void stats(Unit *u, const Hwp *h)
{
    int t = h->item - IT_TANK_CANNON;

    strcpy(u->name, types[t].name);
    u->roster = -1;
    u->sprite_set = sets[t];
    u->tu_max = u->tu = types[t].tu;
    u->hp_max = u->hp = 90;
    u->accuracy = 60;
    u->reactions = types[t].reactions;
    u->armor[0] = types[t].front;
    u->armor[1] = u->armor_left = types[t].side;
    u->armor[2] = types[t].rear;
    u->armor_under = types[t].under;
    if (h < g_hwp + MAX_HWP && g_hwp_hurt[h - g_hwp][0]) {
        /* Cydonia's second stage: the damage of the first (nextStage
           keeps the unit as it is). */
        const unsigned char *d = g_hwp_hurt[h - g_hwp];
        u->hp = d[0];
        u->armor[0] = d[1];
        u->armor[1] = d[2];
        u->armor[2] = d[3];
        u->armor_left = d[4];
        u->armor_under = d[5];
    }
    u->weapon = h->item;
    u->ammo = h->ammo;
    u->grenade = NO_ITEM;
    u->big = 1;
    u->xtype = (unsigned char)(t < 3 ? XT_TANK : XT_HOVER);
    u->strength = 60;
    u->stamina = u->energy = 100;
    u->en_rec = 50;
    u->bravery = 110;
    u->morale = 100;
    u->psi = 100;
}

void hwp_option(const char *p)
{
    for (; *p && g_hwp_n < MAX_HWP; p++)
        if (*p >= '1' && *p <= '5') {
            g_hwp[g_hwp_n].craft = -1;
            g_hwp[g_hwp_n].item = (unsigned char)(IT_TANK_CANNON + *p - '1');
            g_hwp[g_hwp_n++].ammo = g_items[IT_TANK_CANNON + *p - '1'].clip_size;
        }
}

int hwp_place(int soldiers)
{
    int i, strict, x, y, z, sx, sy, n, dir = 0;

    for (i = 0; i < g_hwp_n; i++) {
        Hwp *h = &g_hwp[i];
        h->unit = -1;
        if (sets[h->item - IT_TANK_CANNON] >= NO_SET - 1)
            continue;
        /* The craft's seats in order, all four quarters on start tiles
           (crafts.rul deployment, BattlescapeGenerator::addXCOMUnit). */
        for (n = 0; n < g_pak.n_deploy; n++) {
            const unsigned char *d = g_pak.deploy[n];
            x = g_craft_x + d[0];
            y = g_craft_y + d[1];
            z = d[2];
            dir = d[3] & 7;
            if (room(x, y, z, 1))
                goto found;
        }
        /* Else start tiles searched from the craft's corner as for the
           soldiers; inside the craft if the four fit there. */
        for (strict = 1; strict >= 0 && !g_pak.n_deploy; strict--)
            for (z = 0; z < (int)g_mz; z++)
                for (sy = 0; sy < (int)g_my; sy++)
                    for (sx = 0; sx < (int)g_mx; sx++) {
                        x = (sx + g_craft_x) % g_mx;
                        y = (sy + g_craft_y) % g_my;
                        if (!room(x, y, z, strict))
                            continue;
found:
                        n = add_unit(FACTION_XCOM, x, y, z, dir);
                        if (n < 0)
                            return soldiers;
                        stats(&g_units[n], h);
                        unit_move_to(n, x, y, z);       /* the four quarters */
                        h->unit = (signed char)n;
                        log_printf("HWP %s at %d,%d,%d, %d rounds, health %d", g_units[n].name,
                                   x, y, z, h->ammo, g_units[n].hp);
                        goto placed;
                    }
        log_printf("HWP %s left aboard: no room in the craft", g_items[h->item].name);
placed:
        ;
    }
    /* Quick battles (no roster): the soldiers asked for (8), at most the
       craft's seats less 4 an HWP, at least one. */
    if (!g_roster) {
        n = craft_map_seats() - HWP_SEATS * g_hwp_n;
        if (soldiers > n)
            soldiers = n > 0 ? n : 1;
    }
    return soldiers;
}

int craft_map_seats(void)
{
    return g_pak.craft == CM_AVENGER ? 26 : g_pak.craft == CM_LIGHTNING ? 12 : 14;
}
