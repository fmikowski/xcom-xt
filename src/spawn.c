/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battle setup: the squad, its equipment and the aliens' crew placed on the
   map (units_spawn), the aliens' items and the difficulty's numbers. In
   the HWPs' setup section (build.ps1: with the battle core, whose
   tile_standable and fits it calls between the aliens; never next to the
   geoscape's sections, see alien_quantity), not resident. */

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
#include "los.h"
#include "unit.h"
#include "vox.h"

/* Equipment as in DOS (BattlescapeGenerator::deployAliens): the
   deployment's item set for the rank (spawn entry s) at an item level
   rolled from the month's row of alienItemLevels.rul; one clip loaded,
   the others carried. Terror units (AT_TERROR) keep their built-in
   weapons. Ethereals and sectoid officers have psionics. */
/* los.h: in the setup's section (it runs once a battle; the sight flags
   are los.c's). */
extern unsigned g_los_mxy;

int los_init(void)
{
    const unsigned long *t = (const unsigned long *)g_map;   /* the tile's parts */
    unsigned i = 0;
    int x, y, z;

    g_los_mxy = g_mx * g_my;
    if (!g_los_flags)
        g_los_flags = alloc_para(MAP_MAX_TILES);
    if (!g_los_flags)
        return 1;
    memset(g_los_flags, 0, MAP_MAX_TILES);
    /* A tile with the parts of the one before it gets its flags: they
       depend on nothing else (the flags were just cleared). A load reads
       them whole right after (bs_restore). */
    if (!g_path_load)
        for (z = 0; z < (int)g_mz; z++)
            for (y = 0; y < (int)g_my; y++)
                for (x = 0; x < (int)g_mx; x++, i++, t++)
                    if (i && t[0] == t[-1])
                        g_los_flags[i] = g_los_flags[i - 1];
                    else
                        los_update_tile(x, y, z);
    g_vox_seen_ok = 0;          /* (los_update_tile's) */
    return 0;
}

/* map_reveal (map.h) is here, not in mapgen.c, so that play_mission's los_init,
   map_reveal and units_spawn run in one section (mapgen.c sets these). */
extern unsigned g_rev_w, g_rev_h, g_n_lift;
extern unsigned char g_lift[2][2];

/* What X-COM knows at the start of a battle (BattlescapeGenerator): the
   craft's block and a ring of one tile around it, on every level (loadMAP
   with discovered true for the craft, then the ring); all of a base defence's
   map (addBlock, visible); the alien base's entry lift UBASE_04 on level 1
   (MapBlock::isFloorRevealed, its revealedFloors), the only block with it. The
   map only: units still have to be seen. */
void map_reveal(void)
{
    unsigned char *s = path_fog_table();
    int x, y;
    unsigned z, i;

    if (g_base_layout)
        for (i = 0; i < g_mx * g_my * g_mz; i++)
            s[i] |= SEEN_BIT;
    if (g_rev_w)
        for (z = 0; z < g_mz; z++)
            for (y = g_craft_y - 1; y <= g_craft_y + (int)g_rev_h; y++)
                for (x = g_craft_x - 1; x <= g_craft_x + (int)g_rev_w; x++)
                    if (IN_MAP(x, y, z))
                        s[TIDX(x, y, z)] |= SEEN_BIT;
    for (i = 0; i < g_n_lift && g_mz > 1; i++)
        for (y = g_lift[i][1]; y < g_lift[i][1] + 10; y++)
            for (x = g_lift[i][0]; x < g_lift[i][0] + 10; x++)
                s[TIDX(x, y, 1)] |= SEEN_BIT;
    for (i = z = 0; i < g_mx * g_my * g_mz; i++)
        z += (s[i] & SEEN_BIT) != 0;
    log_printf("known at the start: %u tiles", z);
}

static void alien_loadout(Unit *un, int s)
{
    static const unsigned char weap[5] = {
        IT_PPISTOL, IT_PRIFLE, IT_HPLASMA, IT_LAUNCHER, IT_BLASTER
    };

    if (s < g_pak.n_iset && !(un->xtype < 8 && (g_pak.race[un->xtype].flags & AT_TERROR))) {
        const unsigned char *lv = g_pak.ilevel[g_months_played > 9 ? 9 : g_months_played];
        int r = (int)rnd(10), b;
        b = g_pak.iset[s][r < lv[0] ? 0 : r < lv[0] + lv[1] ? 1 : 2];
        un->weapon = (b & 7) && (b & 7) <= 5 ? weap[(b & 7) - 1] : NO_ITEM;
        un->ammo = un->weapon == NO_ITEM ? 0 : g_items[un->weapon].clip_size;
        un->clips = (unsigned char)(un->weapon == NO_ITEM ? 0 : b >> 3 & 3);
        un->n_grenades = (unsigned char)(b >> 5 & 3);
        un->grenade = un->n_grenades ? IT_AGRENADE : NO_ITEM;
        if (b & 0x80)
            carry_add((int)(un - g_units), SLOT(SEC_PACK, 0, 0), IT_MINDPROBE, 0);
        log_printf("%s: level %d %s +%d, %d grenades%s", un->name,
                   r < lv[0] ? 0 : r < lv[0] + lv[1] ? 1 : 2,
                   un->weapon == NO_ITEM ? "-" : g_items[un->weapon].name, un->clips,
                   un->n_grenades, b & 0x80 ? ", mind probe" : "");
    }
    if (!strncmp(un->name, "Ethereal", 8))
        un->psi = (unsigned char)(70 + rnd(31));
    else if (!strncmp(un->name, "Sectoid", 7) && un->rank <= 1)
        un->psi = (unsigned char)(40 + rnd(31));
}

/* Route node ranks each alien rank spawns on, best first (OpenXcom
   Node::nodeRank, after the DOS game); 0 = outside the UFO. */
static const unsigned char node_rank[8][7] = {
    { 4, 3, 5, 8, 7, 2, 0 }, { 4, 3, 5, 8, 7, 2, 0 }, { 5, 4, 3, 2, 7, 8, 0 },
    { 7, 6, 2, 8, 3, 4, 0 }, { 3, 4, 5, 2, 7, 8, 0 }, { 2, 5, 3, 4, 6, 8, 0 },
    { 2, 5, 3, 4, 6, 8, 0 }, { 2, 5, 3, 4, 6, 8, 0 }
};

/* flags: the AlienType's (AT_BIG, AT_FLY: flying nodes are for flyers). */
static int spawn_ok(unsigned k, int want, int flags)
{
    const MapNode *nd = &g_node[k];
    int big = flags & AT_BIG;

    return nd->x != 255 && NODE_RANK(nd) == want && (nd->pf >> 4)
        && (!(nd->rank & NODE_FLY) || (flags & AT_FLY)) && !((nd->rank & NODE_SMALL) && big)
        && unit_fits_at(-1, big, nd->x, nd->y, nd->z, 0);
}

/* A free spawn node for an alien of the given rank (outside: rank 0 nodes
   only) or a civilian (rank < 0): of the first node rank that has any,
   one of the highest spawn priority at random (getSpawnNode). -1 if none. */
static int spawn_node(int rank, int flags, int outside)
{
    unsigned k, n, best, pick;
    int i, want;

    for (i = 0; i < 7; i++) {
        want = outside || rank < 0 ? 0 : node_rank[rank & 7][i];
        best = n = 0;
        for (k = 0; k < g_n_node; k++)
            if (spawn_ok(k, want, flags)) {
                if (g_node[k].pf >> 4 > best) {
                    best = g_node[k].pf >> 4;
                    n = 0;
                }
                n += g_node[k].pf >> 4 == best;
            }
        if (n) {
            pick = rnd(n);
            for (k = 0; k < g_n_node; k++)
                if (spawn_ok(k, want, flags) && g_node[k].pf >> 4 == best && !pick--)
                    return k;
        }
        if (outside || rank < 0)
            break;
    }
    return -1;
}

/* Random standable spots at least min_d tiles (either axis) from the
   craft, mostly at street level; first a spawn node for the rank (aliens)
   or a civilian's (rank < 0). */
static int scatter(const AlienType *t, int faction, int min_d, int rank)
{
    const Block *cb = &g_pak.block[g_pak.craft_block];     /* (cities: a craft) */
    int tries, cx = g_craft_x + cb->sx / 2, cy = g_craft_y + cb->sy / 2, k;

    k = spawn_node(rank, t ? t->flags : 0, 0);
    if (k >= 0) {
        const MapNode *nd = &g_node[k];
        k = t ? unit_spawn_type(t, nd->x, nd->y, nd->z, rnd(8))
              : add_unit(faction, nd->x, nd->y, nd->z, rnd(8));
        if (k >= 0)
            return k;
    }
    for (tries = 0; tries < 400; tries++) {
        int x = rnd(g_mx), y = rnd(g_my), z = rnd(4) ? 0 : rnd(g_mz), a;
        int dx = x - cx, dy = y - cy;
        if (dx < min_d && dx > -min_d && dy < min_d && dy > -min_d)
            continue;
        if (t) {
            a = unit_spawn_type(t, x, y, z, rnd(8));
        } else {
            if (!unit_standable(x, y, z))
                continue;
            a = add_unit(faction, x, y, z, rnd(8));
        }
        if (a >= 0)
            return a;
    }
    return -1;
}

/* Quick battles: n of an item into the first free slots (belt, legs,
   shoulders, backpack); n: its count. */
static void give(int u, int item, int n)
{
    while (n-- > 0)
        carry_place(u, item, item == IT_MEDIKIT ? 10 : g_items[item].kind == IK_WEAPON ? g_items[item].clip_size : 0);
}

/* GEAR (quick battles): soldiers 2-5 try out the heavy weapons' ammunition
   and the special grenades: weapon, clips, grenade, how many, two clip
   types (two each). */
static void quick_kit(Unit *u, int k)
{
    static const unsigned char kits[4][6] = {
        { IT_HCANNON, 1, IT_SMOKE, 3, IT_CLIP_CANNON_HE, IT_CLIP_CANNON_IN },
        { IT_RLAUNCHER, 1, IT_PROXIMITY, 2, IT_ROCKET_LARGE, IT_ROCKET_IN },
        { IT_ACANNON, 1, IT_HE, 2, IT_CLIP_ACANNON_HE, IT_CLIP_ACANNON_IN },
        { IT_RIFLE, 2, IT_FLARE, 4, IT_SMOKE, NO_ITEM },
    };
    const unsigned char *kit = kits[k - 1];
    int i, n = (int)(u - g_units);

    u->weapon = kit[0];
    give(n, g_items[kit[0]].clip_item, kit[1]);
    give(n, kit[2], kit[3]);
    for (i = 0; i < 2; i++)
        if (kit[4 + i] != NO_ITEM)
            give(n, kit[4 + i], 2);
}

/* A soldier from the first stage arrives at a start tile of the second
   (prepareNewTurn without damage: TU and energy back). */
static void stage_arrive(Unit *u, const Unit *c)
{
    unsigned char x = u->x, y = u->y, z = u->z;

    *u = *c;
    u->x = x;
    u->y = y;
    u->z = z;
    u->dir = 0;
    u->faction = FACTION_XCOM;
    u->psi_state &= PSI_PROBED;
    u->sprite_set = g_armour_set[u->armour];
    u->tu = u->tu_max;
    u->energy = u->stamina;
    u->kneeling = u->phase = u->panicked = u->seen = 0;
    if (u->stunned) {
        u->alive = 0;               /* lies unconscious where it arrives */
        UNIT_AT(x, y, z) = 0;
    } else
        u->alive = 1;
}

/* The next free start tile for a soldier: the craft's seats in order
   (crafts.rul deployment: the Lightning, the Avenger), else start tiles
   searched from the craft's corner on (BattlescapeGenerator::addXCOMUnit;
   a base has them everywhere: the squad starts at the access lift). *it
   counts from 0; 0 when none is left. */
static int next_start(long *it, int *x, int *y, int *z, int *dir)
{
    long n = (long)g_mx * g_my * g_mz;

    *dir = 0;
    for (; g_pak.n_deploy && *it < g_pak.n_deploy; ++*it) {
        const unsigned char *d = g_pak.deploy[(unsigned)*it];
        *x = g_craft_x + d[0];
        *y = g_craft_y + d[1];
        *z = d[2];
        *dir = d[3] & 7;
        if (start_tile(*x, *y, *z) && unit_standable(*x, *y, *z)) {
            ++*it;
            return 1;
        }
    }
    for (; !g_pak.n_deploy && *it < n; ++*it) {
        int sx = (int)(*it % g_mx), sy = (int)(*it / g_mx % g_my);
        *z = (int)(*it / g_mx / g_my);
        *x = (sx + g_craft_x) % g_mx;
        *y = (sy + g_craft_y) % g_my;
        if (start_tile(*x, *y, *z) && unit_standable(*x, *y, *z)) {
            ++*it;
            return 1;
        }
    }
    return 0;
}

int units_spawn(int soldiers, int aliens)
{
    int x, y, z, placed = 0, i, k, next_soldier = 0, dir;
    int carry = g_carry_n;
    long it = 0;

    g_carry_n = g_carried = 0;
    g_nunits = 0;
    if (carry)
        soldiers = carry;
    else if (!g_roster) {
        /* Quick battles; a campaign's squad has its equipment and the
           craft's other items here already (src/hwpscr.c). */
        floor_clear();
        carry_clear();
    }
    g_civ_total = g_civ_killed_alien = g_civ_killed_xcom = 0;
    memset(g_unit_at, 0, g_mx * g_my * g_mz);
    /* Quick battles: SQUAD n, at most the craft's seats. HWPs take their
       room in the craft first (src/hwp.c). */
    if (!carry && !g_roster && soldiers > craft_map_seats())
        soldiers = craft_map_seats();
    if (!carry && g_hwp_n)
        soldiers = hwp_place(soldiers);

    /* X-COM: the craft's seats or start tiles (next_start). */
    while (placed < soldiers && next_start(&it, &x, &y, &z, &dir)) {
        Unit *u;
        if (carry) {
            Unit c = g_units[placed];   /* add_unit takes slot placed */
            stage_arrive(&g_units[add_unit(FACTION_XCOM, x, y, z, 0)], &c);
            placed++;
            continue;
        }
        if (g_roster) {
            while (next_soldier < g_roster_n
                   && (!SOLDIER_READY(&g_roster[next_soldier])
                       || (g_squad_craft && g_roster[next_soldier].craft != g_squad_craft)))
                next_soldier++;
            if (next_soldier >= g_roster_n) {
                soldiers = placed;      /* everyone is aboard */
                continue;
            }
        }
        k = add_unit(FACTION_XCOM, x, y, z, dir);
        u = &g_units[k];
        u->roster = -1;
        u->sprite_set = 0;
        u->armour = g_quick_armour;
        if (g_roster) {
            const Soldier *s = &g_roster[next_soldier];
            u->roster = (signed char)next_soldier;
            strcpy(u->name, s->name);
            u->tu_max = u->tu = s->tu;
            u->hp_max = u->hp = s->hp;
            u->accuracy = s->accuracy;
            u->reactions = s->reactions;
            u->armour = s->armour;
            u->bravery = s->bravery;
            u->rank = s->rank;
            u->psi = s->psi;
            u->psi_skill = s->psi_skill;
            u->strength = s->strength;
            u->throwing = s->throwing;
            u->stamina = s->stamina;
            u->en_rec = (unsigned char)(s->tu0 / 3);
            inv_bind(next_soldier++, k);    /* the equipment the setup gave */
        } else {
            /* Quick battles: a rifle, two clips, a grenade, the
               first soldier a medi-kit; GEAR and PSI kits. */
            u->weapon = IT_RIFLE;
            if (g_quick_gear == 1 && placed && placed <= 4)
                quick_kit(u, placed);
            else {
                give(k, IT_CLIP_RIFLE, 2);
                give(k, IT_GRENADE, 1);
            }
            if (!placed) {
                give(k, IT_MEDIKIT, 1);
                if (g_quick_gear == 1) {
                    give(k, IT_STUNBOMB, 4);
                    give(k, IT_SCANNER, 1);
                    give(k, IT_LAUNCHER, 1);
                    give(k, IT_STUNROD, 1);
                }
            }
            /* BOMBGEAR (tests): a blaster launcher in the first soldier's
               right hand, in the second's left. */
            if (g_quick_gear == 2 && !placed) {
                u->weapon = IT_BLASTER;
                give(k, IT_BLASTERBOMB, 3);
            } else if (g_quick_gear == 2 && placed == 1)
                carry_add(k, SLOT(SEC_LHAND, 0, 0), IT_BLASTER, 1);
            u->ammo = g_items[u->weapon].clip_size;
            u->bravery = (unsigned char)(10 + 10 * rnd(6));
            strcpy(u->name, g_unit_names[placed % N_NAMES]);
            u->tu_max = u->tu = (unsigned char)(50 + rnd(11));
            u->hp_max = u->hp = (unsigned char)(25 + rnd(16));
            u->accuracy = (unsigned char)(40 + rnd(31));
            u->reactions = (unsigned char)(30 + rnd(31));
            u->stamina = (unsigned char)(40 + rnd(31));
            u->throwing = (unsigned char)(50 + rnd(31));
            u->strength = (unsigned char)(20 + rnd(21));
            /* GEAR kits are heavy: those soldiers have the
               strength cap, so the load does not cut their TU. */
            if (g_quick_gear && placed <= 4)
                u->strength = 70;
            u->en_rec = (unsigned char)(u->tu_max / 3);
            u->psi = (unsigned char)rnd(101);
            if (g_quick_psi) {
                /* Trained psionics: a Psi-Amp each in the left hand, one
                   mind probe in the first soldier's right (they work from
                   a hand), his rifle in the backpack. */
                u->psi = (unsigned char)(60 + rnd(31));
                u->psi_skill = (unsigned char)(40 + rnd(41));
                carry_add(k, SLOT(SEC_LHAND, 0, 0), IT_PSIAMP, 0);
                if (!placed && carry_place(k, u->weapon, u->ammo) >= 0) {
                    u->weapon = NO_ITEM;
                    u->ammo = 0;
                    carry_add(k, SLOT(SEC_RHAND, 0, 0), IT_MINDPROBE, 0);
                }
            }
            inv_medikit(k);
        }
        u->armor[0] = g_armours[u->armour].front;
        u->armor[1] = g_armours[u->armour].side;
        u->armor[2] = g_armours[u->armour].rear;
        u->armor_under = g_armours[u->armour].under;
        u->xtype = (unsigned char)(XT_XCOM + u->armour);
        u->energy = u->stamina;
        u->sprite_set = g_armour_set[u->armour];
        u->morale = 100;
        placed++;
    }
    if (!g_nunits)                 /* HWPs alone may go (Cydonia, DOS) */
        return 1;
    if (!carry && g_roster) {
        /* The craft's items nobody took lie at the first soldier's feet
           (BattlescapeGenerator's craft inventory tile); a soldier left
           out (no start tile) leaves his in the stores. */
        for (i = 0; UNIT_HWP(&g_units[i]) && i < g_nunits - 1; i++)
            ;
        for (k = 0; k < g_max_floor; k++)
            if (g_floor[k].item != NO_ITEM && g_floor[k].x == 0xFF) {
                g_floor[k].x = g_units[i].x;
                g_floor[k].y = g_units[i].y;
                g_floor[k].z = g_units[i].z;
            }
        for (k = 0; k < g_max_carry; k++) {
            const Carry *c = &g_carry[k];
            const ItemDef *d = &g_items[c->item];
            if (c->unit < CARRY_ROSTER || c->unit == CARRY_FREE)
                continue;
            g_base.stores[c->item]++;
            /* A loaded gun's clip too (a special one: its n). */
            if (d->kind == IK_WEAPON && c->n && d->clip_item != NO_ITEM)
                g_base.stores[c->n >> 6 ? inv_wload(c->item, c->n) : d->clip_item]++;
        }
        for (k = 0; k < g_max_carry; k++)
            if (g_carry[k].unit >= CARRY_ROSTER)
                g_carry[k].unit = CARRY_FREE;
    }
    if (carry) {
        /* What lay on the ground of the first stage: at the first
           soldier's feet. */
        int k;
        g_carried = placed;
        /* The first stage's HWPs after its soldiers, who keep their slots. */
        if (g_hwp_n)
            hwp_place(placed);
        for (k = 0; k < g_max_floor; k++)
            if (g_floor[k].item != NO_ITEM) {
                g_floor[k].x = g_units[0].x;
                g_floor[k].y = g_units[0].y;
                g_floor[k].z = g_units[0].z;
            }
        log_printf("stage 2: %d soldiers from the first stage", placed);
    }
    if (g_pak.n_deploy)
        log_printf("squad in the %s: %d soldiers", g_pak.craft == CM_AVENGER ? "Avenger"
                   : "Lightning", placed);
    free(g_pak.deploy);         /* pak_free_deploy, without loading the pak section */
    g_pak.deploy = NULL;
    g_pak.n_deploy = 0;
    /* The Elerium-115 of every UFO power source (fuelPowerSources): a floor
       item there that blasts destroy (armour 20, fx_blast_items), 50
       Elerium when it comes home (inv_home). The crash's sources that blew
       up are wrecks by now (map_crash_power): their own blast took theirs. */
    {
        unsigned t, n = g_mx * g_my * g_mz;
        for (t = 0; t < n; t++)
            if (g_map[4 * t + PART_OBJECT] && g_pak.mcd[g_map[4 * t + PART_OBJECT]].special == 2)
                floor_add(t % g_mx, t / g_mx % g_my, t / (g_mx * g_my), IT_ELERIUM, 1);
    }

    /* Terror site: aliens spread through the city, away from the craft,
       and civilians everywhere. */
    if (g_pak.ufo_block == NO_BLOCK) {
        int s, k, a, n;
        for (s = 0; s < g_pak.n_spawn; s++) {
            const Spawn *sp = &g_pak.spawn[s];
            if (sp->rank >= 8 || !g_pak.race[sp->rank].valid)
                continue;
            n = alien_quantity(s);
            for (k = 0; k < n && k < aliens; k++)
                if ((a = scatter(&g_pak.race[sp->rank], FACTION_ALIEN, 12, sp->rank)) >= 0) {
                    g_units[a].rank = sp->rank;
                    alien_loadout(&g_units[a], s);
                    alien_adjust(&g_units[a]);
                }
        }
        for (k = 0; k < g_pak.n_civilians; k++) {
            int c = scatter(NULL, FACTION_CIVILIAN, 6, -1);
            Unit *un;
            if (c < 0)
                break;
            un = &g_units[c];
            strcpy(un->name, "Civilian");
            un->sprite_set = (unsigned char)(1 + (k & 1));    /* CIVM, CIVF */
            un->tu_max = un->tu = 35;
            un->hp_max = un->hp = 30;
            un->accuracy = 30;
            un->reactions = 30;
            un->weapon = un->grenade = NO_ITEM;
            un->morale = 100;
            un->xtype = XT_CIVILIAN;
            un->bravery = 80;
            un->strength = 20;
            un->throwing = 50;
            un->stamina = un->energy = 65;
            un->en_rec = 20;
            g_civ_total++;
        }
        return 0;
    }

    /* Aliens from the pack's crew list: route nodes by rank as in the DOS
       game (some outside the UFO), then open ground near the UFO. */
    {
        const Block *ub = &g_pak.block[g_pak.ufo_block];
        int s, k, n, cxu = g_ufo_x + ub->sx / 2, cyu = g_ufo_y + ub->sy / 2;

        for (s = 0; s < g_pak.n_spawn; s++) {
            const AlienType *t;
            if (g_pak.spawn[s].rank >= 8 || !g_pak.race[g_pak.spawn[s].rank].valid)
                continue;           /* this race has no unit we can draw there */
            t = &g_pak.race[g_pak.spawn[s].rank];
            n = alien_quantity(s);
            for (k = 0; k < n && k < aliens; k++) {
                int a = -1, tries, nd;
                int out = g_pak.script == SCRIPT_CRASH && !g_base_layout && s < 16
                          && (int)rnd(100) < g_pak.outside[s];
                nd = spawn_node(g_pak.spawn[s].rank, t->flags, out);
                if (nd >= 0)
                    a = unit_spawn_type(t, g_node[nd].x, g_node[nd].y, g_node[nd].z, rnd(8));
                for (tries = 0; a < 0 && tries < 300; tries++) {
                    x = cxu - 8 + (int)rnd(17);
                    y = cyu - 8 + (int)rnd(17);
                    a = unit_spawn_type(t, x, y, 0, rnd(8));
                }
                if (a < 0)
                    break;
                g_units[a].rank = g_pak.spawn[s].rank;
                alien_loadout(&g_units[a], s);
                alien_adjust(&g_units[a]);
            }
        }
    }    return 0;
}

/* ------------------------------------------------------------------ */
/* Difficulty (menu.h): resident, because units_spawn calls these once per
   alien, between calls into the battle core (fits, tile_standable). In
   menu.c's overlay section they swapped sections during the setup, and
   setups with several aliens after the DEBUG base grant never ended
   (BASEDEF, CRAFT: the LOG stopped after "rank 5: 8 aliens"). */

/* menu.c's names live in its overlay section (-zc). */
static const char *const diff_names[N_DIFFICULTY] = {
    "Beginner", "Experienced", "Veteran", "Genius", "Superhuman"
};

int alien_quantity(int s)
{
    const unsigned char *q;
    int n;

    if (g_difficulty < 0 || s >= g_pak.n_qty)
        return g_pak.spawn[s].count;
    /* As the DOS game (and OpenXcom): low on Beginner and Experienced,
       midway on Veteran and Genius, high on Superhuman, plus 0..dQty. */
    q = g_pak.spawn_qty[s];
    n = g_difficulty < 2 ? q[0] : g_difficulty < 4 ? q[0] + (q[1] - q[0]) / 2 : q[1];
    n += (int)rnd(q[2] + 1);
    log_printf("rank %d: %d aliens (%s)", g_pak.spawn[s].rank, n, diff_names[g_difficulty]);
    return n;
}

void alien_adjust(Unit *u)
{
    int d = g_difficulty, i;

    if (d < 0)
        return;
    /* statGrowth (difficulty.rul) per level: TU 4 %, stamina 4 %,
       reactions 6 %, firing 6 %, strength 2 %. */
    u->tu_max = u->tu = (unsigned char)(u->tu_max + u->tu_max * 4 * d / 100);
    u->stamina = u->energy = (unsigned char)(u->stamina + u->stamina * 4 * d / 100);
    u->reactions = (unsigned char)(u->reactions + u->reactions * 6 * d / 100);
    u->accuracy = (unsigned char)(u->accuracy + u->accuracy * 6 * d / 100);
    u->strength = (unsigned char)(u->strength + u->strength * 2 * d / 100);
    if (d == 0) {
        u->accuracy /= 2;
        for (i = 0; i < 3; i++)
            u->armor[i] /= 2;
        u->armor_under /= 2;
    }
}
