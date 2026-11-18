/* SPDX-License-Identifier: GPL-3.0-or-later */
/* After a battle, in the geoscape: what the squad brings home and the
   soldiers' experience (unit.c and inv.c keep what the battle needs).
   An overlay section next to the geoscape's (build.ps1): this code is
   not resident during a battle. */

#include <string.h>
#include "base.h"
#include "facil.h"
#include "input.h"
#include "inv.h"
#include "items.h"
#include "map.h"
#include "pak.h"
#include "unit.h"

/* ------------------------------------------------------------------ */
/* The squad (from unit.c) */

/* Stat gain from an experience count (BattleUnit::improveStat). */
static int improve(int exp)
{
    return exp > 10 ? 2 + rnd(5) : exp > 5 ? 1 + rnd(4) : exp > 2 ? 1 + rnd(3) : exp > 0 ? rnd(2) : 0;
}

/* A stat below its cap gains improve(exp). */
static void grow(unsigned char *stat, int cap, int exp)
{
    if (exp && *stat < cap) {
        int v = *stat + improve(exp);
        *stat = (unsigned char)(v > 255 ? 255 : v);
    }
}

/* Physical stats after any experience: 0 .. (cap - stat) / div + 2. */
static void grow_body(unsigned char *stat, int cap, int div)
{
    int v = cap - *stat;
    if (v > 0)
        *stat += (unsigned char)rnd(v / div + 3);
}

void roster_after_battle(void)
{
    int i;

    g_carry_n = 0;              /* a second stage that never started */
    inv_unpack_survivors();
    for (i = 0; i < g_nunits; i++) {
        const Unit *u = &g_units[i];
        Soldier *s;
        if (u->faction != FACTION_XCOM || u->roster < 0)
            continue;
        s = &g_roster[u->roster];
        s->missions++;
        s->kills += u->kills;
        if (!u->alive && !u->stunned) {
            s->alive = 0;           /* and the kit is lost with them */
            continue;
        }
        /* Wounds: health lost x 0.5-1.5 days in recovery
           (BattleUnit::postMissionProcedures). */
        if (u->hp < s->hp) {
            int loss = s->hp - u->hp, lo = loss / 2;
            s->recovery = (unsigned char)(lo + rnd(loss * 3 / 2 - lo + 1));
            log_printf("%s wounded: %d days, craft %d", s->name, s->recovery, s->craft);
            /* (he keeps his craft seat: the transport flies without him until
               he is fit, craft_crew) */
        }
        /* Experience as OpenXcom BattleUnit::postMissionProcedures (soldiers.rul
           statCaps): hits on aliens train firing, being picked for reaction
           fire reactions, throws throwing, turns of resisted panic bravery,
           psi attacks the psi skill. */
        if (u->exp_brave && s->bravery < 100 && u->exp_brave > (int)rnd(11))
            s->bravery += 10;
        grow(&s->reactions, 100, u->reacted);
        grow(&s->accuracy, 120, u->hits);
        grow(&s->throwing, 120, u->exp_throw);
        if (s->psi_skill)
            grow(&s->psi_skill, 100, u->psi_exp);
        if (u->exp_brave || u->reacted || u->hits || u->psi_exp) {
            if (s->rank == RANK_ROOKIE)
                s->rank = RANK_SQUADDIE;
            grow_body(&s->tu, 80, 10);
            grow_body(&s->hp, 60, 10);
            grow_body(&s->strength, 70, 10);
            grow_body(&s->stamina, 100, 15);
        }
        /* Psionics: probed strength is known. */
        if (u->psi_state & PSI_PROBED)
            s->psi_flags |= PSF_KNOWN;
    }
    /* The records go home with the bases (base_unpark), the dead leave
       there. */
    g_squad_home = 1;
}

int roster_stage_carry(void)
{
    int i, k, n = 0;

    for (i = 0; i < g_nunits; i++) {
        Unit *u = &g_units[i];
        /* Soldiers by birth (also under alien control); not civilians or
           aliens under X-COM control. */
        if (u->xtype < XT_XCOM || u->xtype >= XT_CIVILIAN || u->roster < 0)
            continue;
        /* After an abort abort_evacuate has already lost those outside
           the exit area (the map is gone by now). */
        if (!u->alive && !u->stunned) {
            g_roster[u->roster].alive = 0;
            continue;
        }
        /* What it carries goes with it (its new index, marked 0x40);
           bodies stay (their units are not carried on). */
        for (k = 0; k < g_max_carry; k++)
            if (g_carry[k].unit == i && g_carry[k].item != IT_BODY)
                g_carry[k].unit = (unsigned char)(n | 0x40);
        u->special &= ~SP_CARRIED;
        g_units[n++] = *u;          /* n <= i */
    }
    for (k = 0; k < g_max_carry; k++)     /* (the loot of a full floor list goes along) */
        g_carry[k].unit = g_carry[k].unit < CARRY_ROSTER && (g_carry[k].unit & 0x40)
                          ? g_carry[k].unit & 0x3F : g_carry[k].unit == CARRY_LOOT ? CARRY_LOOT
                          : CARRY_FREE;
    /* Items on the ground go along (after an abort abort_evacuate kept
       those in the exit area); units_spawn puts them at the first
       soldier's feet. */
    g_nunits = g_carry_n = n;
    return n;
}

/* ------------------------------------------------------------------ */
/* What comes home (from inv.c) */

/* The corpse a dead crew member leaves (race packs name units "Sectoid",
   "Sectoid Cdr", ...); terror units and zombies leave none. */
static int corpse_of(const Unit *u)
{
    static const char *const races[5] = { "Sectoid", "Floater", "Snakeman", "Muton", "Ethereal" };
    int r;

    for (r = 0; r < 5; r++)
        if (!strncmp(u->name, races[r], strlen(races[r])))
            return IT_CORPSE_SECTOID + r;
    return NO_ITEM;
}

static void add_corpse(const Unit *u)
{
    int c = corpse_of(u);
    if (c != NO_ITEM && !(u->special & SP_GONE)) {     /* not blown up */
        g_base.stores[c]++;
        g_corpses++;
    }
}

int inv_recover(void)
{
    int i;

    /* Stunned aliens are taken alive if Alien Containment has room; the
       dead (and captives that die for want of room) leave corpses. */
    g_captured = g_lost_captives = g_corpses = 0;
    for (i = 0; i < g_nunits; i++) {
        const Unit *u = &g_units[i];
        if (u->faction != FACTION_ALIEN || u->alive)
            continue;
        if (!u->stunned) {
            add_corpse(u);
            continue;
        }
        if (u->special & SP_GONE)
            continue;               /* left outside by an abort */
        if (!facil_capacity(CAP_ALIENS)) {      /* no containment: they die (more than it holds: the screen) */
            g_lost_captives++;
            add_corpse(u);
            continue;
        }
        /* The live alien of its race and rank (the race pack's member:
           a muton at the commander's rank is a Muton Soldier). */
        if (u->live)
            g_base.stores[IT_LIVE + u->live - 1]++;
        g_captured++;
    }
    return inv_floor_home();
}

int inv_floor_home(void)
{
    int i, weapons = 0;

    for (i = 0; i < g_max_floor; i++) {
        FloorItem *f = &g_floor[i];
        if (f->item != NO_ITEM)
            weapons += inv_home(f->item, f->n);
        f->item = NO_ITEM;
    }
    /* The loot of a full floor list (floor_add), as if it lay there. */
    for (i = 0; i < g_max_carry; i++)
        if (g_carry[i].unit == CARRY_LOOT) {
            weapons += inv_home(g_carry[i].item, g_carry[i].n);
            g_carry[i].unit = CARRY_FREE;
        }
    return weapons;
}

/* Rounds left in weapons go back as whole clips (DebriefingState: the
   rounds of each clip type summed); r[weapon] adds up its standard clip's
   rounds, a full special clip comes back as it is. */
static void rounds_home(unsigned *r, int weapon, int load, int ammo)
{
    const ItemDef *d = &g_items[weapon];

    if (load) {
        if (ammo == d->clip_size)
            g_base.stores[load]++;
    } else if (d->clip_item != NO_ITEM)
        r[weapon] += ammo;
}

void inv_unpack_survivors(void)
{
    unsigned r[N_ITEMS];
    int i;

    memset(r, 0, sizeof r);
    for (i = 0; i < g_nunits; i++) {
        Unit *u = &g_units[i];
        if (u->xtype < XT_XCOM || u->xtype >= XT_CIVILIAN || u->roster < 0
            || (!u->alive && !u->stunned))
            continue;
        if (u->weapon != NO_ITEM) {
            g_base.stores[u->weapon]++;
            rounds_home(r, u->weapon, u->load, u->ammo);
            u->weapon = NO_ITEM;
        }
        if (u->grenade != NO_ITEM && u->n_grenades)
            g_base.stores[u->grenade]++;
        u->grenade = NO_ITEM;
    }
    for (i = 0; i < g_max_carry; i++) {
        Carry *c = &g_carry[i];
        const Unit *u = &g_units[c->unit];
        if (c->unit >= MAX_UNITS || u->xtype < XT_XCOM || u->xtype >= XT_CIVILIAN
            || u->roster < 0 || (!u->alive && !u->stunned) || c->item == IT_BODY)
            continue;           /* bodies: inv_recover, by their units */
        /* Elerium-115 goes with the loot, counted there (base.c, inv_home) */
        c->unit = c->item == IT_ELERIUM ? CARRY_LOOT : CARRY_FREE;
        if (c->unit == CARRY_LOOT)
            continue;
        if (g_items[c->item].kind == IK_CLIP && c->n) {
            /* a part-used clip (Unload): its rounds, as a gun's */
            int w;
            for (w = 0; w < N_ITEMS && g_items[w].clip_item != c->item; w++)
                ;
            if (w < N_ITEMS)
                r[w] += c->n;
            continue;
        }
        g_base.stores[c->item]++;
        if (g_items[c->item].kind == IK_WEAPON)
            rounds_home(r, c->item, inv_wload(c->item, c->n), c->n & 63);
        c->unit = CARRY_FREE;
    }
    for (i = 0; i < N_ITEMS; i++)
        if (r[i])
            g_base.stores[g_items[i].clip_item] += r[i] / g_items[i].clip_size;
}
