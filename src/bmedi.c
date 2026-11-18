/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The medi-kit (the M key): an overlay section of its own next to the
   battle core and bplayer (-zc: the strings stay here); bplayer's section
   had no room for the selected unit's arrow. */

#include <stdio.h>
#include "battle.h"
#include "battle_in.h"
#include "input.h"
#include "map.h"
#include "panel.h"
#include "render.h"
#include "rules.h"
#include "unit.h"

/* A unit at a tile: standing there, else lying unconscious there (not
   one carried). */
static int unit_or_body_at(int x, int y, int z)
{
    int i;

    if (!IN_MAP(x, y, z))
        return -1;
    if (UNIT_AT(x, y, z))
        return UNIT_AT(x, y, z) - 1;
    for (i = 0; i < g_nunits; i++)
        if (g_units[i].stunned && g_units[i].x == x && g_units[i].y == y && g_units[i].z == z
            && !(g_units[i].special & SP_CARRIED))
            return i;
    return -1;
}

/* Medi-kit (MedikitState, items.rul) on yourself or a unit next to you,
   also one lying unconscious, 10 TU a use: H heals one fatal wound of the
   worst wounded body part and 3 health (10 uses); S stimulant: 10 energy
   and 4 stun off, which may wake the unconscious (10 uses); P painkiller:
   morale back by the health lost (10 uses). */
void bt_medikit(int s, int tx, int ty, int tz)
{
    Unit *u = &g_units[s];
    int pi = unit_or_body_at(tx, ty, tz), key, k, best = -1;
    Unit *p = pi >= 0 ? &g_units[pi] : u;
    char text[48];
    int dx = p->x - u->x, dy = p->y - u->y;

    if (pi < 0)
        pi = s;
    if (!u->medi && !u->stims && !u->pkills) {
        msg(COL_BAD, "No medi-kit");
        return;
    }
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || p->z != u->z) {
        msg(COL_BAD, "Patient must be next to you");
        return;
    }
    if (u->tu < 10) {
        msg(COL_BAD, "Not enough time units");
        return;
    }
    sprintf(text, "%s: H heal %d  S stim %d  P pain %d", p->name, u->medi, u->stims, u->pkills);
    msg(COL_INFO, text);
    key = (input_key() & 0xFF) | 0x20;
    panel_unmessage();
    if (key == 'h') {
        for (k = 0; k < 6; k++)
            if (p->wound[k] && (best < 0 || p->wound[k] > p->wound[best]))
                best = k;
        if (!u->medi || best < 0) {
            msg(COL_INFO, u->medi ? "No fatal wounds to treat" : "No heals left");
            return;
        }
        u->medi--;
        p->wound[best]--;
        p->wounds--;
        p->hp = (unsigned char)(p->hp + 3 > p->hp_max ? p->hp_max : p->hp + 3);
        sprintf(text, "%s: wound treated, %d left", p->name, p->wounds);
    } else if (key == 's') {
        if (!u->stims) {
            msg(COL_INFO, "No stimulants left");
            return;
        }
        u->stims--;
        p->energy = (unsigned char)(p->energy + 10 > p->stamina ? p->stamina : p->energy + 10);
        p->stun = (unsigned char)(p->stun > 4 ? p->stun - 4 : 0);
        sprintf(text, "%s: stimulant given", p->name);
    } else if (key == 'p') {
        int lost = p->hp_max - p->hp;
        if (!u->pkills) {
            msg(COL_INFO, "No painkillers left");
            return;
        }
        u->pkills--;
        if (lost > p->morale_restored) {
            morale_change(p, lost - p->morale_restored);
            p->morale_restored = (unsigned char)lost;
        }
        sprintf(text, "%s: painkiller given, morale %d", p->name, p->morale);
    } else {
        msg(COL_INFO, "");
        return;
    }
    u->tu -= 10;
    msg(COL_INFO, text);
    if (p->stunned && p->stun < p->hp) {
        wake_up(pi);
        p->tu = 0;
    }
}

