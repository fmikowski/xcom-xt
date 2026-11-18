/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battle formulas of the DOS game, after OpenXcom (see rules.h). */

#include "inv.h"
#include "los.h"
#include "map.h"
#include "path.h"
#include "rules.h"

/* items.rul weight, by item (0 for what never goes into battle). */
const unsigned char g_weights[N_ITEMS] = {
    5, 8, 18, 7, 8, 18, 3, 5, 8,        /* pistol .. heavy plasma */
    3, 3, 6, 3, 3, 3,                   /* clips */
    3, 3, 6,                            /* grenade, alien grenade, high explosive */
    0, 0, 0, 3,                         /* armour, alloys, elerium */
    5,                                  /* medi-kit */
    6, 10, 3, 3,                        /* stun rod, small launcher, stun bomb, scanner */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* live aliens */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    19, 5, 10, 6, 16, 3,                /* auto-cannon .. blaster bomb */
    0, 0, 0, 0, 0, 0, 0, 0,             /* craft weapons */
    10, 5,                              /* psi-amp, mind probe */
    0, 0, 0, 0, 0, 0, 0, 0,             /* UFO components */
    0, 0, 0, 0, 0,                      /* corpses */
    0, 0, 0, 0, 0,                      /* fusion ball, craft */
    6, 6, 5, 5, 8, 8,                   /* special ammunition */
    3, 3, 3,                            /* smoke, proximity grenade, flare */
};

int rl_weight(int item)
{
    return item == NO_ITEM || item >= N_ITEMS ? 0 : g_weights[item];
}

int rl_item_dtype(int item)
{
    switch (item) {
    case IT_LPISTOL: case IT_LRIFLE: case IT_HLASER: case IT_TANK_LASER:
        return DT_LASER;
    case IT_PPISTOL: case IT_PRIFLE: case IT_HPLASMA: case IT_HOVER_PLASMA:
    case IT_CLIP_PPISTOL: case IT_CLIP_PRIFLE: case IT_CLIP_HPLASMA:
        return DT_PLASMA;
    case IT_GRENADE: case IT_AGRENADE: case IT_HE: case IT_PROXIMITY:
    case IT_ROCKET: case IT_ROCKET_LARGE: case IT_BLASTER: case IT_BLASTERBOMB:
    case IT_RLAUNCHER: case IT_CLIP_CANNON_HE: case IT_CLIP_ACANNON_HE:
    case IT_HWP_ROCKETS: case IT_HWP_FUSION:
        return DT_HE;
    case IT_CLIP_CANNON_IN: case IT_CLIP_ACANNON_IN: case IT_ROCKET_IN:
        return DT_IN;
    case IT_STUNROD: case IT_STUNBOMB: case IT_LAUNCHER:
        return DT_STUN;
    case IT_SMOKE:
        return DT_SMOKE;
    }
    return DT_AP;
}

int rl_round_dtype(const Unit *u)
{
    if (u->weapon == NO_ITEM)
        return DT_MELEE;
    if (u->load)
        return rl_item_dtype(u->load);
    if (g_items[u->weapon].clip_item != NO_ITEM)
        return rl_item_dtype(g_items[u->weapon].clip_item);
    return rl_item_dtype(u->weapon);
}

int rl_dmg_mod(const Unit *v, int dtype)
{
    return dtype >= DT_AP && dtype <= DT_SMOKE ? unit_xstat(v)->mod[dtype - 1] : 100;
}

int rl_armour(const Unit *v, int side)
{
    switch (side) {
    case SIDE_LEFT:  return v->armor_left;
    case SIDE_UNDER: return v->armor_under;
    }
    return v->armor[side];
}

void rl_wear(Unit *v, int side, int power)
{
    int a = rl_armour(v, side) - power / 10 - 1;
    unsigned char b = (unsigned char)(a < 0 ? 0 : a);

    if (side == SIDE_LEFT)
        v->armor_left = b;
    else if (side == SIDE_UNDER)
        v->armor_under = b;
    else
        v->armor[side] = b;
}

/* BattleUnit::damage: the direction of the source relative to the unit's
   facing; on a diagonal 2 in 3 hits take the front or rear. */
int rl_side(const Unit *v, int rx, int ry)
{
    static const unsigned char sides[8] = {
        SIDE_FRONT, SIDE_FRONT, SIDE_RIGHT, SIDE_REAR, SIDE_REAR, SIDE_REAR, SIDE_LEFT, SIDE_FRONT
    };
    int ax = rx < 0 ? -rx : rx, ay = ry < 0 ? -ry : ry, d, diff;

    if (!rx && !ry)
        return SIDE_UNDER;
    if (ay > 2 * ax)
        d = ry > 0 ? 4 : 0;
    else if (ax > 2 * ay)
        d = rx > 0 ? 2 : 6;
    else
        d = rx < 0 ? (ry > 0 ? 5 : 7) : (ry > 0 ? 3 : 1);
    diff = (d - v->dir + 8) & 7;
    if (diff & 1 && rnd(3) == 2)        /* the third of diagonal hits on the flank */
        return diff == 1 || diff == 3 ? SIDE_RIGHT : SIDE_LEFT;
    return sides[diff];
}

int rl_part(int side, int hclass)
{
    if (hclass >= 2)
        return BP_HEAD;
    if (side == SIDE_RIGHT)
        return hclass ? BP_RARM : BP_RLEG;
    if (side == SIDE_LEFT)
        return hclass ? BP_LARM : BP_LLEG;
    if (hclass || side == SIDE_UNDER)
        return BP_TORSO;
    return rnd(2) ? BP_RLEG : BP_LLEG;
}

int rl_throw_tu(const Unit *u, int item)
{
    /* Flares need no priming; a soldier's grenade is in the hand already
       (the inventory's move cost is paid taking it there, bplayer.c). */
    return (UNIT_SOLDIER(u) ? 0 : 4) + (item == IT_FLARE ? 0 : u->tu_max / 2) + u->tu_max / 4;
}

int rl_reaction_score(const Unit *u)
{
    return u->tu_max ? u->reactions * u->tu / u->tu_max : 0;
}

int rl_throw_range(const Unit *u, int item, int dz)
{
    /* Heights in hundredths of a voxel; the item falls faster the heavier
       it is for the thrower's strength. */
    long z = dz * 100L + 50;
    int v = 100, dec = u->strength ? 50 * rl_weight(item) / u->strength : 200, dist = 0;

    while (dist < 4000) {
        dist += 8;
        if (v < -100)
            z -= 800;
        else
            z += v * 8L;
        if (z < 0 && v < 0) {
            if (v < -100)
                v = -100;
            dist -= (int)(z / v);       /* back to where it lands */
            break;
        }
        v -= dec;
        if (v <= -200)
            break;
    }
    return dist;
}

int rl_blast_radius(int item, int power)
{
    int r;

    switch (item) {
    case IT_GRENADE: case IT_AGRENADE: case IT_HE: case IT_PROXIMITY: case IT_SMOKE:
        return 5;
    case IT_BLASTERBOMB: case IT_BLASTER:
        return 10;
    case IT_HWP_FUSION:
        return 11;
    }
    r = power / 20;
    return r > 11 ? 11 : r;
}

void rl_deviate(const int *o, int *t, int acc)
{
    int xd = o[0] - t[0], yd = o[1] - t[1], zd = o[2] - t[2], xy, zs, dev;

    if (xd < 0) xd = -xd;
    if (yd < 0) yd = -yd;
    if (zd < 0) zd = -zd;
    xy = xd / 2 <= yd ? xd / 4 + yd : (xd + yd) / 2;
    zs = xy <= zd ? xy / 2 + zd : xy + zd / 2;
    dev = (int)rnd(101) - acc;
    dev += dev >= 0 ? 50 : 10;          /* a wider cloud for misses */
    dev = (int)((long)zs * dev / 200);
    if (dev < 1)
        dev = 1;
    t[0] += (int)rnd(dev + 1) - dev / 2;
    t[1] += (int)rnd(dev + 1) - dev / 2;
    t[2] += (int)rnd(dev / 2 + 1) / 2 - dev / 8;
}

/* Turns (x, y) by a radians (Q14, up to 1.2; Taylor sin and cos). */
static void rl_turn(int *x, int *y, long a)
{
    int x2, h, s, c, nx;

    if (a > 19661L)
        a = 19661L;
    if (a < -19661L)
        a = -19661L;
    x2 = rl_mul14((int)a, (int)a);
    h = x2 / 2;
    s = (int)a - rl_mul14((int)a, rl_mul14(x2, 16384 - x2 / 20)) / 6;
    c = 16384 - h + rl_mul14(rl_mul14(h, h), 16384 - x2 / 30) / 6;
    nx = rl_mul14(*x, c) - rl_mul14(*y, s);
    *y = rl_mul14(*y, c) + rl_mul14(*x, s);
    *x = nx;
}

void rl_throw_aim(const int *o, int *t, int acc, int curv)
{
    int v[3], d[3], h, h0, k;
    unsigned ro;

    for (k = 0; k < 3; k++) {
        v[k] = t[k];
        d[k] = (t[k] - o[k]) * 16;
    }
    h0 = h = rl_isqrt(rl_imul(d[0], d[0]) + rl_imul(d[1], d[1]));
    ro = rl_isqrt(rl_imul(h, h) + rl_imul(d[2], d[2]));
    if (!h0)
        return;
    rl_deviate(o, v, acc);
    /* calculateParabola: the direction turns by dev x / ro x pi / 2, the
       steepness by (dev y + dev z) / ro x pi / 14 x curvature; the
       length stays (in 1/16 voxel here). */
    rl_turn(&d[0], &d[1], rl_imul((v[0] - t[0]) * 16, 25736) / ro);
    rl_turn(&d[2], &h, (rl_imul(v[1] - t[1] + v[2] - t[2], curv) >> 4) * 919 / ro);
    d[0] = (int)(rl_imul(d[0], h) / h0);
    d[1] = (int)(rl_imul(d[1], h) / h0);
    for (k = 0; k < 3; k++)
        t[k] = o[k] + ((d[k] + 8) >> 4);
}

int rl_rank_mod(const Unit *u)
{
    static const unsigned char mod[6] = { 100, 100, 120, 130, 150, 175 };
    return u->faction == FACTION_XCOM && u->rank < 6 && RL_WOUNDABLE(u) ? mod[u->rank] : 100;
}

int rl_leader_mod(void)
{
    static const unsigned char mod[6] = { 100, 100, 110, 115, 125, 150 };
    int i, best = 0;

    for (i = 0; i < g_nunits; i++) {
        const Unit *u = &g_units[i];
        if (u->alive && u->faction == FACTION_XCOM && RL_WOUNDABLE(u) && u->rank > best
            && u->rank < 6)
            best = u->rank;
    }
    return mod[best];
}

unsigned rl_isqrt(unsigned long v)
{
    unsigned r = 0, b = 0x8000;

    for (; b; b >>= 1)
        if ((unsigned long)(r | b) * (r | b) <= v)
            r |= b;
    return r;
}
