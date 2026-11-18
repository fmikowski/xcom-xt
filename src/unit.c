/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "map.h"
#include "menu.h"
#include "pak.h"
#include "path.h"
#include "input.h"
#include "hwp.h"
#include "inv.h"
#include "unit.h"

Unit g_units[MAX_UNITS];
int g_nunits;
unsigned char *g_unit_at;
int g_civ_total, g_civ_killed_alien, g_civ_killed_xcom;

Soldier *g_roster;
unsigned char g_squad_idx[MAX_SQUAD];
int g_squad_home;
int g_squad_craft;
int g_roster_n;
int g_squad_max = 8;
int g_carry_n;                  /* soldiers carried to Cydonia's second stage */
int g_carried;

const char *const g_unit_names[N_NAMES] = {
    "Kowalski", "Tanaka", "Okafor", "Novak", "Silva", "Brennan",
    "Laurent", "Ivanova", "Moreau", "Haddad", "Lindqvist", "Ortega",
};

unsigned char g_armour_set[N_ARMOURS];     /* unit sprite set per armour */
unsigned char g_quick_armour;              /* quick battles: ARMOUR option */
unsigned char g_quick_gear;                /* quick battles: GEAR option */
unsigned char g_quick_psi;                 /* quick battles: PSI option */

const char *const g_rank_names[6] = { "Rookie", "Squaddie", "Sergeant", "Captain", "Colonel", "Commander" };

int unit_shot_tu(const Unit *u, int mode)
{
    const ItemDef *w;
    int pct;

    if (u->weapon == NO_ITEM)
        return 0;
    w = &g_items[u->weapon];
    pct = mode == SHOT_AIMED ? w->tu_aimed : mode == SHOT_AUTO ? w->tu_auto : w->tu_snap;
    return pct ? u->tu_max * pct / 100 : 0;
}

int item_two_handed(int item)
{
    switch (item) {
    case IT_PISTOL: case IT_LPISTOL: case IT_PPISTOL: case NO_ITEM:
        return 0;
    }
    return g_items[item].kind == IK_WEAPON || item == IT_PSIAMP;
}

int unit_acc_mod(const Unit *u, int item)
{
    int w = u->wound[BP_HEAD], m, arm = u->special & SP_MIRROR ? BP_LARM : BP_RARM;

    if (item != NO_ITEM)
        w += u->wound[arm] + (item_two_handed(item) ? u->wound[BP_RARM + BP_LARM - arm] : 0);
    m = 25 * u->hp / (u->hp_max ? u->hp_max : 1) + 75 - 10 * w;
    return m < 10 ? 10 : m;
}

int unit_shot_acc(const Unit *u, int mode)
{
    const ItemDef *w;
    int a;

    if (u->weapon == NO_ITEM)
        return 0;
    w = &g_items[u->weapon];
    /* The stun rod does not use the firing skill (skillApplied: false). */
    if (u->weapon == IT_STUNROD)
        return unit_acc_mod(u, u->weapon);
    a = u->accuracy * (mode == SHOT_AIMED ? w->acc_aimed : mode == SHOT_AUTO
                       ? w->acc_auto : w->acc_snap) / 100;
    if (u->kneeling)
        a = a * 115 / 100;              /* kneeling steadies the aim */
    /* A two-handed weapon with the other hand full: x 80 / 100
       (BattleUnit::getFiringAccuracy); a mirrored unit's left hand holds
       the right hand's item. */
    if (item_two_handed(u->weapon) && carry_lhand(u) != NO_ITEM)
        a = a * 80 / 100;
    return a * unit_acc_mod(u, u->weapon) / 100;
}

/* Battle rules of the types not in race packs (OpenXcom armors.rul and
   units.rul): X-COM in coveralls, personal armour, power suit and flying
   suit (their strength and so on come from the soldier), civilians,
   zombies. */
static const UnitXStat fixed_x[8] = {
    /* str sta brv thr und enr   h  kn fl  w mel   AP  IN  HE  LA  PL  ST  ME  AC  SM */
    {   0,  0,  0,  0,  2,  0, 22, 14, 0, 7,  0, { 100,100,100,100,100,100,120,160,100 } },
    {   0,  0,  0,  0, 30,  0, 22, 14, 0, 7,  0, { 100, 80,100,100,100, 90,100,110,100 } },
    {   0,  0,  0,  0, 60,  0, 22, 14, 0, 7,  0, { 100,  0,100,100,100, 80,100,100,  0 } },
    {   0,  0,  0,  0, 70,  0, 22, 14, 0, 7,  0, { 100,  0,100,100,100, 80,100,100,  0 } },
    {  20, 65, 80, 50,  0, 20, 21, 14, 0, 7, 50, { 100,100,100,100,100,100,120,160,100 } },
    {  84,110,110,  0,  4, 20, 18, 18, 0, 7, 80, {  60,100, 80, 70, 70,  0,100,100,  0 } },
    /* HWPs: tanks, hovertanks (TANK_ARMOR, HOVERTANK_ARMOR) */
    {  60,100,110,  0, 60, 50, 16, 16, 0, 0, 70, { 100, 40, 75,100,100,  0, 90, 40,  0 } },
    {  60,100,110,  0,100, 50, 12, 12, 6, 0, 70, { 100, 40, 75,100,100,  0, 90, 40,  0 } },
};

const char *unit_title(const Unit *u)
{
    return u->live ? g_items[IT_LIVE + u->live - 1].name : u->name;
}

char *unit_fmt(char *text, const char *fmt, const Unit *v, int n)
{
    sprintf(text, fmt, unit_title(v), n);
    return text;
}

const UnitXStat *unit_xstat(const Unit *u)
{
    return u->xtype < XT_XCOM ? &g_pak.race_x[u->xtype] : &fixed_x[u->xtype - XT_XCOM];
}

int unit_flies(const Unit *u)
{
    return u->xtype < XT_XCOM ? (g_pak.race[u->xtype].flags & AT_FLY) != 0
           : u->xtype == XT_XCOM + ARM_FLYING || u->xtype == XT_HOVER;
}

int dir_towards(int fx, int fy, int tx, int ty)
{
    int dx = tx - fx, dy = ty - fy, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0), sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    int d;

    if (ax > 2 * ay)
        sy = 0;
    else if (ay > 2 * ax)
        sx = 0;
    for (d = 0; d < 8; d++)
        if (g_dir_dx[d] == sx && g_dir_dy[d] == sy)
            return d;
    return 0;
}

/* Writes v into the unit's footprint (1 or 4 tiles); clearing (v = 0)
   only touches tiles that are still the unit's. */
static void footprint(int u, unsigned char v)
{
    const Unit *un = &g_units[u];
    int s = un->big ? 2 : 1, i, j;

    for (j = 0; j < s; j++)
        for (i = 0; i < s; i++) {
            unsigned char *p;
            if (!IN_MAP(un->x + i, un->y + j, un->z))
                continue;
            p = &UNIT_AT(un->x + i, un->y + j, un->z);
            if (v || *p == u + 1)
                *p = v;
        }
}

void unit_move_to(int u, int x, int y, int z)
{
    Unit *un = &g_units[u];

    footprint(u, 0);
    un->x = (unsigned char)x;
    un->y = (unsigned char)y;
    un->z = (unsigned char)z;
    footprint(u, (unsigned char)(u + 1));
}

void unit_kill(int u)
{
    Unit *un = &g_units[u];

    un->alive = 0;
    un->hp = 0;
    footprint(u, 0);
    if (!UNIT_HWP(un))
        inv_drop_all(u);        /* a destroyed HWP leaves nothing to recover */
}

int unit_body(const Unit *u)
{
    if (u->special & SP_GONE)
        return -1;
    if (u->xtype < 8)               /* aliens: by their live alien type */
        return u->live ? BODY_LIVE - 1 + u->live : -1;
    if (u->xtype < XT_CIVILIAN)     /* the soldier's armour (the flying
                                       suit's corpse is the power suit's) */
        return u->xtype - XT_XCOM - (u->xtype == XT_XCOM + ARM_FLYING);
    if (u->xtype == XT_CIVILIAN)
        return BODY_CIVM + (u->sprite_set == 2);
    if (u->xtype == XT_ZOMBIE)
        return -1;
    return BODY_TANK + (u->xtype == XT_HOVER);
}

/* A free slot: a new one, else a dead civilian's (they are counted when
   they die). */
static int free_slot(void)
{
    int i;

    if (g_nunits < MAX_UNITS)
        return g_nunits++;
    for (i = 0; i < g_nunits; i++)
        if (!g_units[i].alive && g_units[i].faction == FACTION_CIVILIAN
            && !(g_units[i].special & SP_CARRIED))
            return i;
    return -1;
}

int add_unit(int faction, int x, int y, int z, int dir)
{
    Unit *u;
    int n = free_slot();

    if (n < 0)
        return -1;
    u = &g_units[n];
    memset(u, 0, sizeof *u);
    u->faction = (unsigned char)faction;
    u->weapon = u->grenade = NO_ITEM;
    u->alive = 1;
    u->dir = u->turret = (unsigned char)dir;
    u->x = (unsigned char)x;
    u->y = (unsigned char)y;
    u->z = (unsigned char)z;
    UNIT_AT(x, y, z) = (unsigned char)(n + 1);
    return n;
}

int unit_standable(int x, int y, int z)
{
    int h;
    return IN_MAP(x, y, z) && !UNIT_AT(x, y, z) && tile_standable(x, y, z, &h);
}

/* Footprint check for a unit of the given size (u = the unit itself, or
   -1): standable tiles at about the same height, free of others. loose
   (a 2x2 unit walking where the path search let it, path.c big_update):
   only free of others. */
int unit_fits_at(int u, int big, int x, int y, int z, int loose)
{
    int s = big ? 2 : 1, i, j, h0 = 0, h;

    for (j = 0; j < s; j++)
        for (i = 0; i < s; i++) {
            unsigned char o;
            if (!IN_MAP(x + i, y + j, z))
                return 0;
            if (!loose) {
                if (!tile_standable(x + i, y + j, z, &h))
                    return 0;
                if (!i && !j)
                    h0 = h;
                else if (h - h0 > 8 || h0 - h > 8)
                    return 0;
            }
            o = UNIT_AT(x + i, y + j, z);
            if (o && o != u + 1)
                return 0;
        }
    return 1;
}

int unit_fits(int u, int x, int y, int z)
{
    return unit_fits_at(u, g_units[u].big, x, y, z, 0);
}

int unit_free_at(int u, int x, int y, int z)
{
    return unit_fits_at(u, g_units[u].big, x, y, z, 1);
}

static void set_type(Unit *un, const AlienType *t)
{
    strncpy(un->name, t->name, sizeof un->name - 1);
    un->sprite_set = t->sprite_set;
    un->tu_max = un->tu = t->tu;
    un->hp_max = un->hp = t->hp;
    un->accuracy = t->accuracy;
    un->reactions = t->reactions;
    un->armor[0] = t->armor[0];
    un->armor[1] = t->armor[1];
    un->armor[2] = t->armor[2];
    un->weapon = t->weapon == 1 ? IT_PPISTOL : t->weapon == 2 ? IT_PRIFLE
               : t->weapon == 3 ? IT_HPLASMA : NO_ITEM;
    un->ammo = un->weapon == NO_ITEM ? 0 : g_items[un->weapon].clip_size;
    un->grenade = NO_ITEM;
    un->morale = 100;
    un->melee = t->melee;
    un->big = (unsigned char)(t->flags & AT_BIG ? 1 : 0);
    un->special = (unsigned char)(t->flags & AT_ZOMBIFY ? SP_ZOMBIFY : 0);
    un->xtype = (unsigned char)(t >= g_pak.race && t < g_pak.race + 8 ? t - g_pak.race : XT_ZOMBIE);
    {
        const UnitXStat *x = unit_xstat(un);
        un->bravery = x->bravery;
        un->strength = x->strength;
        un->throwing = x->throwing;
        un->stamina = un->energy = x->stamina;
        un->en_rec = x->en_rec;
        un->armor_under = x->under;
        un->armor_left = un->armor[1];
        un->live = x->live;
    }
}

int unit_spawn_type(const AlienType *t, int x, int y, int z, int dir)
{
    int a;

    if (!unit_fits_at(-1, t->flags & AT_BIG, x, y, z, 0) || (a = add_unit(FACTION_ALIEN, x, y, z, dir)) < 0)
        return -1;
    set_type(&g_units[a], t);
    footprint(a, (unsigned char)(a + 1));
    return a;
}

int unit_spawn_zombie(int u)
{
    static const AlienType zombie = {
        "Zombie", 0, 40, 84, 0, 40, { 4, 4, 4 }, 0, 1, 0, 84
    };
    AlienType t = zombie;
    const Unit *v = &g_units[u];
    int z;

    if (g_pak.zombie_set == 0xFF)
        return -1;
    t.sprite_set = g_pak.zombie_set;
    z = unit_spawn_type(&t, v->x, v->y, v->z, v->dir);
    if (z >= 0)
        g_units[z].special = SP_ZOMBIE;
    return z;
}

/* The squad's block: from DOS (first fit) once the world has left, so it
   takes the bottom of a hole the battle's packs would fill anyway. */
static unsigned squad_seg;

void roster_squad_free(void)
{
    if (squad_seg)
        _dos_freemem(squad_seg);
    squad_seg = 0;
    g_roster = NULL;
    g_roster_n = 0;
}

int roster_squad_on(void)
{
    return squad_seg != 0;
}

int roster_squad_alloc(int n)
{
    roster_squad_free();
    if (_dos_allocmem(((n > 0 ? n : 1) * sizeof(Soldier) + 15) >> 4, &squad_seg)) {
        squad_seg = 0;
        return 1;
    }
    g_roster = MK_FP(squad_seg, 0);
    g_roster_n = n;
    return 0;
}

int roster_alive(void)
{
    int i, n = 0;
    for (i = 0; i < g_roster_n; i++)
        if (g_roster[i].alive)
            n++;
    return n;
}

int roster_ready(void)
{
    int i, n = 0;
    for (i = 0; i < g_roster_n; i++)
        if (SOLDIER_READY(&g_roster[i]))
            n++;
    return n;
}

int unit_in_exit_area(const Unit *u)
{
    const unsigned char *t = TILE(u->x, u->y, u->z);
    int p, want = g_pak.script == SCRIPT_MARS ? SPECIAL_END : SPECIAL_START;

    for (p = 0; p < 4; p++)
        if (t[p] && g_pak.mcd[t[p]].special == want)
            return 1;
    return 0;
}
