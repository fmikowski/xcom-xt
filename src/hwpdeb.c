/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Heavy weapons platforms after a battle (in the debriefing's overlay
   section, gpop; -zc), as OpenXcom's DebriefingState: the HWPs that came
   back go to the stores with the rounds they have left, then each craft
   takes its HWPs and their full loads from the stores again
   (reequipCraft); what the stores lack is listed after the debriefing
   (CannotReequipState). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "craft.h"
#include "hwp.h"
#include "input.h"
#include "uik.h"
#include "uitext.h"
#include "unit.h"

/* CannotReequipState's list: HWPs and ammunition missing per craft
   (reequipCraft: at most one line an item and craft); on the heap from
   the re-equipment to the report, so that the battle does not pay for
   it. */
#define MAX_MISS    12
static struct Miss {
    unsigned char item;
    signed char craft;
    unsigned qty;
} *miss;
static unsigned char n_miss;

static void missing(int item, unsigned qty, int craft)
{
    if (!miss)
        miss = malloc(MAX_MISS * sizeof *miss);
    if (!miss || n_miss == MAX_MISS)
        return;
    miss[n_miss].item = (unsigned char)item;
    miss[n_miss].craft = (signed char)craft;
    miss[n_miss++].qty = qty;
}

void hwp_reequip_report(void)
{
    char t[8], head[56], *second;
    int i, y = 58;

    if (!n_miss)
        return;
    uk_page(UI_BG_MENU, T_GREEN, "");
    ui_text(UI_MSG, head, sizeof head, MSG_NO_EQUIP);
    second = strchr(head, '\n');
    if (second)
        *second++ = 0;
    else
        second = head + strlen(head);
    uk_center(50, 220, 8, UK_BIG, g_uk->btext, head);
    uk_center(50, 220, 24, UK_BIG, g_uk->btext, second);
    uk_text(10, 50, UK_SMALL, g_uk->text, "ITEM");
    uk_text(152, 50, UK_SMALL, g_uk->text, "QUANTITY");
    uk_text(218, 50, UK_SMALL, g_uk->text, "CRAFT");
    for (i = 0; i < n_miss; i++, y += 8) {
        const struct Miss *m = &miss[i];
        sprintf(t, "%u", m->qty);
        uk_text(10, y, UK_SMALL, g_uk->list, g_items[m->item].name);
        uk_text(172, y, UK_SMALL, g_uk->list, t);
        uk_text(218, y, UK_SMALL, g_uk->list, craft_name(&g_craft[m->craft]));
        log_printf("REEQUIP %s %u %s", g_items[m->item].name, m->qty,
                   craft_name(&g_craft[m->craft]));
    }
    n_miss = 0;
    free(miss);
    miss = NULL;
    uk_page_wait();
}

/* DebriefingState::reequipCraft for craft k's HWPs: those that came along
   (all were put in the stores) come out again by type (OpenXcom's order:
   the types' names), each with a full load; HWPs or rounds the stores
   lack are listed. Those that did not come along stay aboard. */
static void reequip(int k)
{
    Craft *c = &g_craft[k];
    unsigned char n[N_HWP], m[N_HWP];
    unsigned have, load;
    int i, j = 0, t, item, clip, can, kept;

    memset(n, 0, sizeof n);
    for (i = 0; i < g_hwp_n; i++)
        if (g_hwp[i].craft == k)
            n[g_hwp[i].item - IT_TANK_CANNON]++;
    memcpy(m, n, sizeof m);
    for (i = 0; i < MAX_HWP; i++) {
        t = c->hwp[i] - IT_TANK_CANNON;
        if (c->hwp[i] && !m[t])
            c->hwp[j++] = c->hwp[i];
        else if (c->hwp[i])
            m[t]--;
    }
    for (kept = j; j < MAX_HWP; j++)
        c->hwp[j] = 0;
    if (c->state == CS_EMPTY)
        return;                     /* lost with its squad */
    for (j = kept, i = 0; i < N_HWP; i++) {
        t = "\4\3\0\2\1"[i];
        if (!n[t])
            continue;
        item = IT_TANK_CANNON + t;
        clip = g_items[item].clip_item;
        have = g_base.stores[item];
        can = have < n[t] ? have : n[t];
        if (have < n[t])
            missing(item, n[t] - have, k);
        if (clip != NO_ITEM) {
            /* The rounds of a full load (lasers and plasma need none). */
            load = g_items[item].clip_size;
            have = g_base.stores[clip];
            if (have < n[t] * load)
                missing(clip, n[t] * load - have, k);
            if ((unsigned)can > have / load)
                can = have / load;
            g_base.stores[clip] -= can * load;
        }
        g_base.stores[item] -= can;
        log_printf("CRAFT %s HWP %s: %d of %d re-armed, %u in the stores", craft_name(c),
                   g_items[item].name, can, n[t], g_base.stores[item]);
        while (can--)
            c->hwp[j++] = (unsigned char)item;
    }
}

void hwp_after_battle(void)
{
    int i, item, clip, left;

    n_miss = 0;
    /* The HWPs that came back go to the stores with their rounds (one that
       could not be placed with its full load). */
    for (i = 0; i < g_hwp_n; i++) {
        item = g_hwp[i].item;
        clip = g_items[item].clip_item;
        left = g_hwp[i].unit < 0 ? g_hwp[i].ammo : g_units[g_hwp[i].unit].ammo;
        if (g_hwp[i].unit >= 0 && !g_units[g_hwp[i].unit].alive) {
            log_printf("HWP %s lost", g_items[item].name);
            continue;
        }
        g_base.stores[item]++;
        if (clip != NO_ITEM) {
            g_base.stores[clip] += left;
            log_printf("HWP %s back with %d of %d rounds: %u %s in the stores",
                       g_items[item].name, left, g_items[item].clip_size, g_base.stores[clip],
                       g_items[clip].name);
        }
    }
    for (i = 0; i < MAX_CRAFT; i++)
        for (item = 0; item < g_hwp_n; item++)
            if (g_hwp[item].craft == i) {
                reequip(i);
                break;
            }
    g_hwp_n = 0;
}

/* The craft's items (Craft.items) back out of the stores; see hwp.h. */
void equip_reequip(int k)
{
    int j, i;
    unsigned long n = 0;

    /* Equal to equip_deploy's "EQUIP stores" after a battle that used
       and lost nothing (tests: no item lost; alien items are not bought). */
    for (i = 0; i < N_ITEMS; i++)
        if (g_items[i].buy_k)
            n += g_base.stores[i];
    log_printf("REEQUIP stores: %lu bought items", n);
    for (j = 0; j < MAX_CRAFT; j++) {
        Craft *c = &g_craft[j];
        if (k >= 0 ? j != k : !CRAFT_HERE(c) || c->state != CS_HANGAR)
            continue;
        for (i = 0; i < N_ITEMS; i++) {
            unsigned want = c->items[i], have = g_base.stores[i];
            if (!want)
                continue;
            if (have < want) {
                missing(i, want - have, j);
                c->items[i] = (unsigned char)have;
                want = have;
            }
            g_base.stores[i] -= want;
        }
    }
}
