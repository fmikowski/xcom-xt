/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Psionics in battle: attack and defence strength and the Psi-Amp roll
   (see psi.h). The actions and the Mind Probe screen are in bplayer.c
   (psi_amp, mind_probe), the aliens' attacks in ai.c. */

#include <stdio.h>
#include <string.h>
#include "hud.h"
#include "input.h"
#include "map.h"
#include "psi.h"
#include "uik.h"
#include "unit.h"

/* An alien by birth: also one under X-COM control. */
#define IS_ALIEN(u) ((u)->faction == FACTION_ALIEN || ((u)->psi_state & PSI_CTRL))

/* Psi strength of the races and terror units (OpenXcom units.rul); the
   officers' is given as a bonus by rank below. */
static const struct { const char *name; unsigned char strength; } races[] = {
    { "Sectoid", 40 }, { "Floater", 35 }, { "Snakeman", 40 }, { "Muton", 25 },
    { "Ethereal", 50 }, { "Cyberdisc", 100 }, { "Sectopod", 100 }, { "Reaper", 35 },
    { "Chryssalid", 50 }, { "Zombie", 80 }, { "Silacoid", 80 }, { "Celatid", 80 },
};

int psi_strength(const Unit *u)
{
    int i;

    if (!IS_ALIEN(u))
        return u->psi;
    for (i = 0; i < (int)(sizeof races / sizeof races[0]); i++)
        if (!strncmp(u->name, races[i].name, strlen(races[i].name)))
            return races[i].strength + (u->rank == 0 ? 15 : u->rank == 1 ? 10 : 0);
    return 40;
}

/* Aliens: psionic ones (Unit.psi = their attack strength) have half of it
   as skill, about the ethereals' and sectoid officers' 40-60. */
int psi_skill(const Unit *u)
{
    return IS_ALIEN(u) ? u->psi / 2 : u->psi_skill;
}

/* Attack minus defence before the 0-55 roll. */
static int psi_margin(const Unit *a, const Unit *v, int control)
{
    int dx = a->x - v->x, dy = a->y - v->y, dz = a->z - v->z, d;
    int def = psi_strength(v) + 10 + psi_skill(v) / 5 + (control ? 20 : 0);

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dz < 0) dz = -dz;
    d = dx > dy ? dx + dy / 2 : dy + dx / 2;    /* about the straight distance */
    return (int)((long)psi_strength(a) * psi_skill(a) / 50) - d - dz - def;
}

int psi_chance(const Unit *a, const Unit *v, int control)
{
    int m = psi_margin(a, v, control);

    /* Success when margin + roll > 0, roll 0..55. */
    if (m > 0)
        return 100;
    if (m <= -55)
        return 0;
    return (55 + m) * 100 / 56;
}

int psi_roll(const Unit *a, const Unit *v, int control)
{
    return psi_margin(a, v, control) + (int)rnd(56) > 0;
}
