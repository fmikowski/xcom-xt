/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The AI's hooks in the battle core (see ai.h, ai_in.h); the alien and
   civilian turns are in ai.c. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "ai_in.h"
#include "effects.h"
#include "input.h"
#include "los.h"
#include "map.h"
#include "menu.h"
#include "pak.h"
#include "path.h"
#include "psi.h"
#include "rules.h"
#include "timer.h"
#include "unit.h"

AiUnit ai_unit[MAX_UNITS];
unsigned char ai_spotted[MAX_UNITS];
unsigned char ai_watch[MAX_UNITS];
int ai_walker = -1, ai_no_stop;

/* units.rul: intelligence | aggression << 4 by rank (commander ... soldier,
   terror units); terror units by name. */
static const struct { char name[9]; unsigned char mind[8]; } races[] = {
    { "Sectoid",  { 0x08, 0x17, 0x26, 0x15, 0x24, 0x23, 0x15, 0x15 } },
    { "Snakeman", { 0x26, 0x25, 0x24, 0x22, 0x23, 0x22, 0x14, 0x14 } },
    { "Ethereal", { 0x18, 0x17, 0x17, 0x17, 0x17, 0x16, 0x14, 0x14 } },
    { "Muton",    { 0x22, 0x22, 0x24, 0x22, 0x23, 0x22, 0x23, 0x23 } },
    { "Floater",  { 0x08, 0x08, 0x27, 0x16, 0x25, 0x24, 0x22, 0x22 } },
    { "Mixed",    { 0x16, 0x17, 0x18, 0x14, 0x23, 0x23, 0x14, 0x14 } },
};
static const struct { char name[9]; unsigned char mind; } terrors[] = {
    { "Cyberdis", 0x15 }, { "Reaper", 0x22 }, { "Chryssal", 0x14 }, { "Zombie", 0x23 },
    { "Silacoid", 0x23 }, { "Celatid", 0x23 }, { "Sectopod", 0x14 },
};
/* Psi skill (units.rul) of sectoid and ethereal (mixed: ethereal) ranks. */
static const unsigned char psi_skills[3][8] = {
    { 60, 50 }, { 50, 45, 45, 45, 45, 40 }, { 40, 45, 50 }
};

void ai_free_node(unsigned n)
{
    if (n != NO_NODE)
        g_node[n].rank &= ~NODE_TAKEN;
}

void ai_unit_new(int u)
{
    Unit *un = &g_units[u];
    AiUnit *a = &ai_unit[u];
    unsigned i;

    memset(a, 0, sizeof *a);
    a->from = a->to = NO_NODE;
    a->mind = un->faction == FACTION_CIVILIAN ? 0x04 : 0x14;
    if (un->psi_state & PSI_ALIEN)
        a->mind = 0x12;         /* a soldier's: intelligence 2, aggression 1 */
    else if (un->faction == FACTION_ALIEN) {
        un->psi = 0;
        for (i = 0; i < sizeof races / sizeof races[0]; i++)
            if (!strncmp(un->name, races[i].name, strlen(races[i].name))) {
                a->mind = races[i].mind[un->rank & 7];
                if (i == 0 || i == 2 || i == 5)     /* psi_skill() = skill */
                    un->psi = (unsigned char)(2 * psi_skills[i / 2][un->rank & 7]);
            }
        for (i = 0; i < sizeof terrors / sizeof terrors[0]; i++)
            if (!strncmp(un->name, terrors[i].name, strlen(terrors[i].name)))
                a->mind = terrors[i].mind;
    }
    if (!(un->psi_state & PSI_ALIEN))
        ai_spotted[u] = 255;
    for (i = 0; i < g_n_node; i++)
        if (g_node[i].x == un->x && g_node[i].y == un->y && g_node[i].z == un->z) {
            a->from = i;
            break;
        }
}

void ai_battle_start(void)
{
    int i;

    for (i = 0; i < g_nunits; i++)
        ai_unit_new(i);
    ai_walker = -1;
}

int ai_step(int u)
{
    const Unit *w = &g_units[u];
    int i, stop = 0;

    if (u == ai_walker) {
        busy_tick();            /* an AI unit's walk, often unseen (input.h) */
        /* The walker spots someone new: it stops to think again. */
        for (i = 0; i < g_nunits; i++)
            if (!ai_watch[i] && g_units[i].alive && HOSTILE(g_units[i].faction, w->faction)
                && LOS_NEAR(w, &g_units[i]) && los_unit_sees(u, i)) {
                ai_watch[i] = 1;
                if (w->faction == FACTION_ALIEN)
                    ai_spotted[i] = 0;
                stop = !ai_no_stop;
            }
    }
    /* Aliens watching an X-COM unit or a civilian on the move. */
    if (ai_spotted[u] && w->faction != FACTION_ALIEN)
        for (i = 0; i < g_nunits; i++)
            if (g_units[i].alive && g_units[i].faction == FACTION_ALIEN
                && LOS_NEAR(&g_units[i], w) && los_unit_sees(i, u)) {
                ai_spotted[u] = 0;
                break;
            }
    return stop;
}

void ai_hit(int u, int s)
{
    if (s >= 0 && g_units[u].faction == FACTION_ALIEN && g_units[s].faction != FACTION_ALIEN)
        ai_spotted[s] = 0;         /* the shooter gave himself away */
}

void ai_unit_out(int u)
{
    Unit *un = &g_units[u];
    unsigned i;

    if (un->psi_state & PSI_ALIEN) {
        un->psi_state &= ~PSI_ALIEN;
        un->faction = FACTION_XCOM;
    }
    if (un->faction != FACTION_ALIEN)
        return;
    ai_free_node(ai_unit[u].to);
    ai_unit[u].to = NO_NODE;
    /* An alien fell here: the others keep away from these nodes. */
    for (i = 0; i < g_n_node; i++) {
        MapNode *n = &g_node[i];
        int dx = n->x - un->x, dy = n->y - un->y, dz = n->z - un->z;
        if (n->x != 255 && dx * dx + dy * dy + dz * dz < 4)
            n->rank |= NODE_DANGER;
    }
}

void ai_battle_end(void)
{
    int i;

    for (i = 0; i < g_nunits; i++)
        if (g_units[i].psi_state & PSI_ALIEN) {
            g_units[i].psi_state &= ~PSI_ALIEN;
            g_units[i].faction = FACTION_XCOM;
        }
}
