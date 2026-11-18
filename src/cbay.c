/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Craft::rearm (OpenXcom Craft.cpp), an hour of a craft in its hangar: the
   one part of the hangar's work with some size and almost never called, in
   the air combat's overlay section so that it costs the geoscape's none
   (src/craft.c calls it). */

#include <string.h>
#include "base.h"
#include "craft.h"
#include "geo.h"
#include "input.h"
#include "unit.h"

/* Craft::rearm, an hour: the first weapon still rearming loads up to its
   rate. Stores short of the ammunition: the weapon stops rearming and
   the base is told (Not enough ...); none rearming: refuelling. */
void craft_rearm(Craft *c, int k)
{
    unsigned *st = base_stores(c->base);
    int s, add;
    const CraftWeapon *w;

    for (s = 0; s < 2 && !(c->weapon[s] != CW_NONE && (c->pad & HP_R(s))); s++)
        ;
    if (s == 2) {
        c->pad &= ~(HP_ARM | HP_R(0) | HP_R(1));
        return;
    }
    w = &g_craft_weapons[c->weapon[s]];
    add = w->ammo_max - c->ammo[s];
    if (add > w->rearm)
        add = w->rearm;
    if (w->ammo_item != NO_ITEM) {
        int items = (add + w->per_item - 1) / w->per_item, have = st[w->ammo_item];
        if (items > have)
            items = have;
        st[w->ammo_item] -= items;
        add = items * w->per_item;
        if (!have || (items == have && c->ammo[s] + add < w->ammo_max)) {
            c->pad &= ~HP_R(s);
            geo_popup(PU_CRAFTERR, k, w->ammo_item);
            log_printf("CRAFT %s weapon %d: no %s left, ammo %d", craft_name(c), s + 1,
                       g_items[w->ammo_item].name, c->ammo[s] + add);
        }
    }
    c->ammo[s] += add;
    if (c->ammo[s] >= w->ammo_max) {
        c->ammo[s] = w->ammo_max;
        c->pad &= ~HP_R(s);
    }
}

/* DEBUG option, geoscape key K: the current base's first transport is shot
   down (craft_lose) with its first four soldiers aboard (the others are
   not in any crew). */
void craft_debug_lose(void)
{
    int i, k, n = g_roster_n;
    char name[24];

    for (k = 0; k < MAX_CRAFT && !(CRAFT_HERE(&g_craft[k]) && g_craft_types[g_craft[k].type].soldiers);
         k++)
        ;
    if (k == MAX_CRAFT)
        return;
    for (i = 0; i < n; i++)
        g_roster[i].craft = (unsigned char)(i < 4 ? k + 1 : 0);
    strcpy(name, craft_name(&g_craft[k]));
    craft_lose(&g_craft[k]);
    log_printf("GEO debug: %s lost, soldiers %d -> %d", name, n, g_roster_n);
}
