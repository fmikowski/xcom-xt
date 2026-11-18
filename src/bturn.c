/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battlescape between turns: an overlay section of its own next to the
   battle core, loaded at the start and end of each side's turn (as the AI
   and bplayer are, see build.ps1): time units, energy, bleeding and panic
   at the start of a side's turn (BattleUnit::prepareNewTurn), the primed
   grenades and UFO doors at its end (BattlescapeGame::endTurn), smoke and
   fire between rounds (SavedBattleGame::prepareNewTurn). */

#include <stdio.h>
#include <string.h>
#include "ai.h"
#include "ai_in.h"
#include "battle.h"
#include "battle_in.h"
#include "effects.h"
#include "fx_in.h"
#include "gfx.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "render.h"
#include "rules.h"
#include "timer.h"
#include "unit.h"

#define COL_FX_BAD  12

static void fx_new_turn(void);

/* Open UFO doors close at the end of each side's turn (TileEngine::
   closeUfoDoors, Tile::closeUfoDoor), all of a tile's at once, except on
   a tile where a 2x2 unit stands across one of them (the tile north of
   an open north wall, or west of an open west wall, is the same unit's).
   A 1x1 unit in the doorway does not keep a door open. The map is walked
   in its own order by pointer; a tile without walls (both wall bytes 0,
   most of the map) has no door and is passed over at once. */
static void close_ufo_doors(void)
{
    int x, y, z, p, open, u;
    unsigned char *t = g_map;

    for (z = 0; z < (int)g_mz; z++)
        for (y = 0; y < (int)g_my; y++, busy_tick())    /* the busy pointer (input.h) */
            for (x = 0; x < (int)g_mx; x++, t += 4) {
                if (!*(const unsigned *)(t + PART_WESTWALL))    /* west and north wall */
                    continue;
                for (open = 0, p = PART_WESTWALL; p <= PART_NORTHWALL; p++) {
                    const Mcd *m = &g_pak.mcd[t[p]];
                    if (t[p] && m->alt_mcd && !(m->flags & MCD_UFO_DOOR)
                        && (g_pak.mcd[m->alt_mcd].flags & MCD_UFO_DOOR)
                        && MCD_PART(&g_pak.mcd[m->alt_mcd]) == p)
                        open |= p;      /* PART_WESTWALL 1, PART_NORTHWALL 2 */
                }
                if (!open || ((u = UNIT_AT(x, y, z)) != 0 && g_units[u - 1].big
                              && (((open & PART_NORTHWALL) && y && UNIT_AT(x, y - 1, z) == u)
                                  || ((open & PART_WESTWALL) && x && UNIT_AT(x - 1, y, z) == u))))
                    continue;
                for (p = PART_WESTWALL; p <= PART_NORTHWALL; p++)
                    if (open & p)
                        t[p] = g_pak.mcd[t[p]].alt_mcd;
                terrain_changed(x, y, z);
            }
}


/* Start of a side's turn for its units (BattleUnit::prepareNewTurn):
   TU back (less when carrying more than the strength, 10 % less per leg
   wound), energy back (soldiers a third of their recruit TU, aliens their
   race's rate; 10 % of the energy less per body wound), a health point
   lost per fatal wound, a stun point recovered (also lying unconscious),
   and the panic roll: 100 - 2 x morale percent, a third of the panics
   berserk; resisting it is bravery experience. */
/* The weight of everything u carries (here, not in rules.c: only the turn's
   start needs it, and the core had no room). */
static int rl_carried(const Unit *u)
{
    int w = 0;
    const Carry __near *c;

    if (u->weapon != NO_ITEM) {
        w = rl_weight(u->weapon);
        if (u->ammo && g_items[u->weapon].clip_item != NO_ITEM)
            w += rl_weight(u->load ? u->load : g_items[u->weapon].clip_item);
        if (g_items[u->weapon].clip_item != NO_ITEM)
            w += u->clips * rl_weight(g_items[u->weapon].clip_item);
    }
    if (u->grenade != NO_ITEM)
        w += u->n_grenades * rl_weight(u->grenade);
    for (c = g_carry; c < g_carry_end; c++)
        if (c->unit == u - g_units) {
            w += c->item == IT_BODY ? inv_body_weight(c->n) : rl_weight(c->item);
            /* a gun's clip (BattleUnit::getCarriedWeight) */
            if (c->item < N_ITEMS && g_items[c->item].kind == IK_WEAPON && (c->n & 63)
                && g_items[c->item].clip_item != NO_ITEM)
                w += rl_weight(c->n >> 6 ? inv_wload(c->item, c->n) : g_items[c->item].clip_item);
        }
    return w;
}

/* TU at the start of a turn (BattleUnit::prepareTimeUnits): fewer when
   carrying more than its strength, 10 % less per leg wound. */
static int tu_recovery(const Unit *u)
{
    int tu = u->tu_max, w = rl_carried(u);

    if (w > u->strength && u->strength)
        tu = (int)((long)tu * u->strength / w);
    return tu - tu * (u->wound[BP_LLEG] + u->wound[BP_RLEG]) * 10 / 100;
}

static void prepare_side(int faction)
{
    int i;

    for (i = 0; i < g_nunits; i++) {
        Unit *u = &g_units[i];
        int e, chance;
        busy_tick();
        if (u->faction != faction || (!u->alive && !u->stunned))
            continue;
        if (u->stun)
            u->stun--;
        /* Fatal wounds bleed on while unconscious: the unit can die lying
           down, and does not wake while it bleeds faster than it recovers. */
        if (u->wounds) {
            if (u->hp <= u->wounds) {
                kill_unit(i, -1, 0);
                continue;
            }
            u->hp -= u->wounds;
        }
        if (!u->alive)
            continue;
        /* A burning unit takes 5-10 x its armour's incendiary modifier and
           burns a turn less, unless a burning tile hit it this round. */
        if ((u->burning & ~UB_HIT) && !(u->burning & UB_HIT)) {
            u->burning--;
            log_printf("T%d: %s is burning", bt_turn, u->name);
            bt_burn(i, fx_fire_damage(u));
            if (!u->alive)
                continue;
            if (!u->burning)
                bt_redraw(i);   /* the flames go out */
        }
        u->burning &= ~UB_HIT;
        u->panicked = 0;
        u->tu = (unsigned char)tu_recovery(u);
        e = u->energy + u->en_rec - u->energy * u->wound[BP_TORSO] * 10 / 100;
        u->energy = (unsigned char)(e < 0 ? 0 : e > u->stamina ? u->stamina : e);
        if (u->stun >= u->hp) {
            knock_out(i);
            continue;
        }
        chance = 100 - 2 * u->morale;
        if (RL_FEARABLE(u) && (int)rnd(100) < chance)
            u->panicked = rnd(101) <= 33 ? PANIC_BERSERK : PANIC_FREEZE;
        else if (chance > 1 && RL_WOUNDABLE(u))
            u->exp_brave++;
    }
}

/* Panicking units of a side act before anything else in its turn
   (BattlescapeGame::handlePanickingUnit, UnitPanicBState): a panicking
   unit freezes, or half the time drops its weapon and runs up to 5 tiles;
   a berserk one fires up to 10 auto (else snap) shots at the nearest unit
   of another side it sees, else at random, at half its accuracy. Then
   morale +15 and no TU left. */
static void panic_side(int faction)
{
    int i, k;
    char text[48];

    panicking = 1;
    for (i = 0; i < g_nunits && !bt_result; i++) {
        Unit *u = &g_units[i];
        int seen = u->faction == FACTION_XCOM || unit_visible(u);
        busy_tick();
        if (!u->alive || u->faction != faction || !u->panicked)
            continue;
        sprintf(text, u->panicked == PANIC_BERSERK ? "%s has gone berserk!" : "%s has panicked!",
                u->name);
        if (seen)
            msg(COL_BAD, text);
        else
            log_msg(COL_BAD, text);
        if (u->panicked == PANIC_FREEZE && rnd(101) <= 50) {
            if (u->weapon != NO_ITEM) {
                floor_add(u->x, u->y, u->z, u->weapon, u->ammo);
                u->weapon = NO_ITEM;
                u->ammo = 0;
                if (seen)
                    redraw_unit(i);
            }
            path_search(i, u->tu > 32 ? 32 : u->tu);
            for (k = 0; k < 20; k++) {
                int tx = u->x + (int)rnd(11) - 5, ty = u->y + (int)rnd(11) - 5;
                unsigned c = IN_MAP(tx, ty, u->z) ? path_cost(tx, ty, u->z) : 0xFFFF;
                if (c != 0xFFFF && c) {
                    walk(i, tx, ty, u->z);
                    break;
                }
            }
        } else if (u->panicked == PANIC_BERSERK) {
            for (k = 0; k < 10 && u->alive && !bt_result; k++) {
                int mode = unit_shot_tu(u, SHOT_AUTO) ? SHOT_AUTO : SHOT_SNAP;
                int t = -1, bd = 999, j, tx, ty, d, turn_cost;
                if (!unit_shot_tu(u, mode) || u->weapon == IT_STUNROD)
                    break;
                for (j = 0; j < g_nunits; j++) {
                    const Unit *o = &g_units[j];
                    int dx = o->x - u->x, dy = o->y - u->y;
                    if (j == i || !o->alive || o->faction == u->faction || !los_unit_sees(i, j))
                        continue;
                    d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                    if (d < bd) {
                        bd = d;
                        t = j;
                    }
                }
                tx = t >= 0 ? g_units[t].x : u->x + (int)rnd(13) - 6;
                ty = t >= 0 ? g_units[t].y : u->y + (int)rnd(13) - 6;
                if (!IN_MAP(tx, ty, u->z) || (tx == u->x && ty == u->y))
                    continue;
                d = (dir_towards(u->x, u->y, tx, ty) - u->dir + 8) & 7;
                turn_cost = d > 4 ? 8 - d : d;
                if (u->tu < unit_shot_tu(u, mode) + turn_cost)
                    break;
                u->tu -= (unsigned char)turn_cost;
                shoot(i, tx, ty, t >= 0 ? g_units[t].z : u->z, mode);
            }
        }
        if (!u->alive)
            continue;
        morale_change(u, 15);
        u->tu = 0;
    }
    panicking = 0;
}

/* End of a full turn: unconscious units with less stun than health get
   up, smoke and fire change, and the next turn begins. */
static void new_turn(void)
{
    int i;

    for (i = 0; i < g_nunits; i++)
        if (g_units[i].stunned)
            wake_up(i);
    bt_turn++;
    for (i = 0; i < g_nunits; i++)     /* the aliens' memory of X-COM units */
        if (ai_spotted[i] < 255)
            ai_spotted[i]++;
    fx_new_turn();              /* smoke thins, fires burn */
    bt_fall();                  /* over floors that burnt away */
    if (g_n_boom)
        bt_boom_run(-1);        /* explosive parts that burnt (BattlescapeGame::endTurn) */
}


/* ------------------------------------------------------------------ */
/* Effects between turns (lists in src/effects.c) */

/* End of a side's turn (BattlescapeGame::endTurn): primed grenades on the
   ground whose fuse is 0 go off, then every fuse burns down a turn, also
   those of grenades primed in a soldier's hand. */
static void fx_end_side(void)
{
    int i, any = 0;

    for (i = 0; i < n_dev; i++)
        if (dev[i].kind == DEV_TIMER && !dev[i].fuse) {
            dev[i].pending = 1;
            any = 1;
        }
    if (any) {
        bt_msg(COL_FX_BAD, "Explosives detonate!");
        fx_run_chain();
    }
    for (i = 0; i < n_dev; i++)
        if (dev[i].fuse)
            dev[i].fuse--;
    for (i = 0; i < g_nunits; i++)
        if (g_units[i].fuse > 1)
            g_units[i].fuse--;
    /* ... and those primed in an inventory (fuse + 1 in n). */
    for (i = 0; i < g_max_carry; i++)
        if (g_carry[i].unit < CARRY_ROSTER && ITEM_KIND(g_carry[i].item) == IK_GRENADE
            && g_carry[i].n > 1)
            g_carry[i].n--;
}

static const signed char nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

/* Flames pass where the HE blockage is 0 (fx_step_he, DT_IN), smoke where
   the smoke blockage is 0 (fx_step_he, fx_blk_smoke: any wall but an open UFO door,
   big walls, objects with Block_Smoke). */

/* Tile::ignite: a burning neighbour (power: its intensity) sets a tile
   that has a part that burns and fuel alight with power - Flammable / 10
   + 15 %: for its fuel + 1 rounds at 15 - Flammable / 10 (within 1-12). */
static void ignite(int x, int y, int z, int power)
{
    int flam, fuel;

    if (!IN_MAP(x, y, z) || fx_fire_at(x, y, z) || (flam = fx_tile_burn(x, y, z, &fuel)) == 255
        || !fuel)
        return;
    if ((int)rnd(100) >= power - flam / 10 + 15)
        return;
    if (g_shot_log)
        log_printf("T%d: fire spreads to %d,%d,%d: Flammable %d, Fuel %d", bt_turn, x, y, z, flam, fuel);
    flam /= 10;
    fx_fire_set(x, y, z, fuel + 1, 15 - (flam < 1 ? 1 : flam > 12 ? 12 : flam));
}

/* A part burnt away (Tile::destroy, bt_break): its destroyed version or
   nothing, a ground floor without one scorched earth (older packs: it
   stays); grav lifts stay; an explosive part blows up. */
static void burn_part(unsigned char *t, int z, int p)
{
    const Mcd *m = &g_pak.mcd[t[p]];

    if (t[p] && !(m->flags & MCD_GRAVLIFT)
        && (p != PART_FLOOR || z || m->die_mcd || g_pak.scorched))
        bt_break(t, p, z);
}

/* A fire burnt out (SavedBattleGame::prepareNewTurn): an object that burns
   and can be destroyed burns away with the floor under it; without an
   object, a floor that burns burns away. */
static void burn_out(int x, int y, int z)
{
    unsigned char *t = TILE(x, y, z);
    int r = t[t[PART_OBJECT] ? PART_OBJECT : PART_FLOOR];

    if (MCD_FLAM(r) == 255 || g_pak.mcd[r].armor == 255)
        return;
    burn_part(t, z, PART_OBJECT);
    burn_part(t, z, PART_FLOOR);
    terrain_changed(x, y, z);
}

/* Smoke reaching a tile this round (Tile::addSmoke): its entry, NULL on a
   burning tile, inside a solid object or when the list is full. A new
   entry starts empty; it is marked LF_HAZE at once so that the next
   arrival finds it (its flags are set at the end of the round). Spot.z
   counts the arrivals in its high bits meanwhile (ARRIVED). */
#define ARRIVED(s)  ((s)->z >> 2)

static Spot *smoke_into(int x, int y, int z)
{
    Spot *s;
    unsigned i;

    if (!IN_MAP(x, y, z) || (g_los_flags[i = TIDX(x, y, z)] & (LF_OBJECT | LF_FIRE)))
        return NULL;
    if (g_los_flags[i] & LF_HAZE)
        for (s = smoke; s < smoke + n_smoke; s++)
            if (s->x == x && s->y == y && (s->z & 3) == z)
                return s;
    if (n_smoke >= max_smoke)
        return NULL;
    s = &smoke[n_smoke++];
    s->x = (unsigned char)x;
    s->y = (unsigned char)y;
    s->z = (unsigned char)z;
    s->v = 0;
    g_los_flags[i] |= LF_HAZE;
    return s;
}

/* The tile nb[k] away from (x, y, z) if smoke can go there: in the map and
   no smoke blockage between (smoke_into then). */
static Spot *smoke_next(int x, int y, int z, int k)
{
    int nx = x + nb[k][0], ny = y + nb[k][1];

    if (!IN_MAP(nx, ny, z))
        return NULL;
    fx_blk_smoke = 1;
    nx = fx_step_he(x, y, z, nb[k][0], nb[k][1], 1) ? -1 : nx;
    fx_blk_smoke = 0;
    return nx < 0 ? NULL : smoke_into(nx, ny, z);
}

/* The first arrival adds to the tile's smoke (within 1-15), the next ones
   add up; Tile::prepareNewTurn averages them. */
static void smoke_add(Spot *s, int v)
{
    v += s->v;
    if (!ARRIVED(s))
        v = v < 1 ? 1 : v > 15 ? 15 : v;
    s->v = (unsigned char)(v > 255 ? 255 : v);
    s->z += 4;
}

/* Units standing in smoke (not fire) lose (smoke / 4) + 1 stun points
   times their armour's smoke modifier: soldiers without a power or flying
   suit and civilians; aliens and 2x2 units are immune (Tile::prepareNewTurn,
   xcom1 armors.rul). */
static void smoke_stun(int i, int v)
{
    Unit *u = &g_units[i];
    int mod = rl_dmg_mod(u, DT_SMOKE), d;

    if (!u->alive || u->big || !mod)
        return;
    d = (v / 4 + 1) * mod / 100 + u->stun;
    u->stun = (unsigned char)(d > 255 ? 255 : d);
    log_printf("T%d: %s in smoke: stun %d", bt_turn, u->name, u->stun);
    if (u->stun >= u->hp)
        knock_out(i);
}

/* Between rounds, as SavedBattleGame::prepareNewTurn and
   Tile::prepareNewTurn: fires burn a round less and set flammable
   neighbours alight where nothing with an HE blockage stands between
   (a wall, an object in the burning tile, a big wall in the next one),
   or burn out (what burnt goes); smoke not on fire
   thins by 1 and, where some is left, spreads to its four neighbours on
   its level (not through walls, not into fire) that had none or got some
   this round; a burning tile sends half its intensity to its neighbours
   and up a level where no floor stops it; smoke that arrived is
   averaged less 1. Units in the flames take the intensity x their
   incendiary modifier and may catch fire, those in smoke are stunned. */
static void fx_new_turn(void)
{
    unsigned char burnt[MAX_UNITS];
    int i, k, n0, u, thick = 0;
    Spot *s, *t;

    if (!n_smoke && !n_fire)
        return;
    for (i = n_fire - 1; i >= 0; i--) {
        Spot f;
        busy_tick();
        fire[i].v -= 16;
        f = fire[i];
        if (FIRE_TURNS(f)) {
            for (k = 0; k < 4; k++)
                if (IN_MAP(f.x + nb[k][0], f.y + nb[k][1], f.z)
                    && !fx_step_he(f.x, f.y, f.z, nb[k][0], nb[k][1], 1))
                    ignite(f.x + nb[k][0], f.y + nb[k][1], f.z, FIRE_INT(f));
        } else {
            fx_fire_out(i);
            burn_out(f.x, f.y, f.z);
        }
    }
    n0 = n_smoke;
    busy_tick();
    for (i = 0; i < n0; i++) {
        s = &smoke[i];
        if (!--s->v)
            continue;
        for (k = 0; k < 4; k++)
            if ((t = smoke_next(s->x, s->y, s->z & 3, k)) != NULL
                && (!t->v || ARRIVED(t)))
                smoke_add(t, s->v);
    }
    for (i = 0; i < n_fire; i++) {
        Spot f = fire[i];
        if (f.z + 1 < (int)g_mz && !TILE(f.x, f.y, f.z + 1)[PART_FLOOR]
            && (t = smoke_into(f.x, f.y, f.z + 1)) != NULL)
            smoke_add(t, FIRE_INT(f) / 2);
        for (k = 0; k < 4; k++)
            if ((t = smoke_next(f.x, f.y, f.z, k)) != NULL)
                smoke_add(t, FIRE_INT(f) / 2);
    }
    /* The units in fire: once each (a 2x2 unit too); no burn of their own
       this round. */
    memset(burnt, 0, sizeof burnt);
    busy_tick();
    for (i = 0; i < n_fire; i++)
        if ((u = UNIT_AT(fire[i].x, fire[i].y, fire[i].z)) != 0 && g_units[--u].alive
            && !burnt[u]) {
            burnt[u] = 1;
            bt_burn(u, FIRE_INT(fire[i]) * rl_dmg_mod(&g_units[u], DT_IN) / 100);
            g_units[u].burning |= UB_HIT;
            fx_catch_fire(u, 40);
        }
    for (s = smoke; s < smoke + n_smoke; s++) {
        if (ARRIVED(s) && s->v) {
            k = s->v / ARRIVED(s) - 1;
            s->v = (unsigned char)(k < 0 ? 0 : k > 15 ? 15 : k);
        }
        s->z &= 3;
    }
    for (s = smoke; s < smoke + n_smoke; s++)
        if (s->v && (u = UNIT_AT(s->x, s->y, s->z)) != 0)
            smoke_stun(u - 1, s->v);
    /* Cleared smoke leaves the list; flags where thin and thick changed. */
    for (i = n_smoke - 1; i >= 0; i--) {
        Spot c = smoke[i];
        k = g_los_flags[TIDX(c.x, c.y, c.z)] & (LF_HAZE | LF_SMOKE);
        if (!c.v) {
            smoke[i] = smoke[--n_smoke];
            fx_tile_flags(c.x, c.y, c.z);
        } else {
            thick += c.v >= SMOKE_THICK;
            if (k != (c.v >= SMOKE_THICK ? LF_HAZE | LF_SMOKE : LF_HAZE))
                fx_tile_flags(c.x, c.y, c.z);
        }
    }
    /* Room for a smoke grenade's cloud: beyond max_smoke - SMOKE_ROOM
       tiles the thinnest smoke goes (DOS has no limit). */
    while (n_smoke > max_smoke - SMOKE_ROOM) {
        Spot c;
        for (s = t = smoke; t < smoke + n_smoke; t++)
            if (t->v < s->v)
                s = t;
        thick -= s->v >= SMOKE_THICK;
        c = *s;
        *s = smoke[--n_smoke];
        fx_tile_flags(c.x, c.y, c.z);
    }
    fx_count();
    if (n_smoke)
        fx_smoke_changed = 1;       /* thinner or thicker: sight changes (los.c) */
    log_printf("effects: %d smoke (%d thick), %d fire, %d devices", n_smoke, thick, n_fire, n_dev);
    fx_flush();
}

/* BENCH: a full redraw with a smoke cloud and fires in view. */
void fx_bench(FILE *f)
{
    unsigned long t0, ms;
    int x, y, k;

    fx_reset();
    fx_smoke_cloud(20, 20, 0);
    for (y = 22; y < 26; y++)
        for (x = 16; x < 20; x++)
            fx_fire_set(x, y, 0, 3, 5);
    fx_dirty = fx_smoke_changed = 0;
    g_view_z = 0;
    render_center(20, 20, 0);
    for (k = 0; k < 2; k++) {
        fx_no_span = k;
        t0 = timer_read();
        render_full();
        ms = timer_ms(timer_read() - t0);
        fprintf(f, "20,20 level 0 with %d smoke + %d fire tiles%s: %lu ms\n", n_smoke,
                n_fire, k ? ", spans off" : "", ms);
    }
    fx_no_span = 0;
    t0 = timer_read();
    for (k = 0; k < 10; k++)
        render_tile(20, 20 + (k & 1), 0);
    fprintf(f, "tile redraw in smoke: %lu ms\n", timer_ms(timer_read() - t0) / 10);
    for (k = 0; k < n_smoke; k++)
        g_los_flags[TIDX(smoke[k].x, smoke[k].y, 0)] &= ~LF_FX;
    for (k = 0; k < n_fire; k++)
        g_los_flags[TIDX(fire[k].x, fire[k].y, 0)] &= ~LF_FX;
    fx_reset();
}

/* ------------------------------------------------------------------ */
/* Entry points for battle_run (src/battle.c) */

void bt_side_start(int faction)
{
    prepare_side(faction);
    if (faction != FACTION_CIVILIAN)
        panic_side(faction);
}

void bt_side_end(int faction)
{
    if (!bt_result && faction != FACTION_CIVILIAN)
        fx_end_side();
    close_ufo_doors();
}

void bt_new_round(void)
{
    new_turn();
}


/* ------------------------------------------------------------------ */
/* Terrain explosions (battle_in.h) */

/* As OpenXcom: Tile::destroy gives the tile of an explosive part its
   HE_Strength as the tile's explosive value (the stronger, if it had one);
   after each blast (ExplosionBState::explode, explodePowerSources) and
   each end of turn checkForTerrainExplosions takes the first such tile in
   map order and blows it up as an HE blast of that power and radius
   power / 10, centred on the tile, until none is left. A later blast that
   reaches such a tile first detonates it with the stronger of its power
   there and the tile's value and clears the value (TileEngine::explode,
   detonate), so that only one of a console's four quarters blows up when
   the first one's blast reaches the others. The crash's power sources
   come first (strength 0 here: 180-250, radius 10, silent). Here the
   tiles are a list (map.h g_boom); a full list drops the blast (logged). */
unsigned char (__near *g_boom)[3];
int g_n_boom, g_max_boom;
static unsigned char booming;

#define BOOM_IDX(b)     ((b)[0] | (unsigned)(b)[1] << 8)
/* The order they go off in: the crash's power sources first, then the
   first tile in map order (tile indices stay below MAP_MAX_TILES). */
#define BOOM_KEY(b)     (BOOM_IDX(b) | ((b)[2] ? 0x8000u : 0))

void bt_boom_add(const unsigned char *t, int rec)
{
    unsigned idx = (unsigned)(t - g_map) >> 2;
    int i, p = 0;
    unsigned char *b;

    for (i = 0; i < MAX_EXPL && g_pak.expl[i][0]; i++)
        if (g_pak.expl[i][0] == rec)
            p = g_pak.expl[i][1];
    if (!p)
        return;
    for (i = 0; i < g_n_boom; i++)
        if (g_boom[i][2] && BOOM_IDX(g_boom[i]) == idx) {
            if (p > g_boom[i][2])
                g_boom[i][2] = (unsigned char)p;
            return;
        }
    if (g_n_boom == g_max_boom) {
        log_printf("terrain explosion list full: %u lost", idx);
        return;
    }
    b = g_boom[g_n_boom++];
    b[0] = (unsigned char)idx;
    b[1] = (unsigned char)(idx >> 8);
    b[2] = (unsigned char)p;
}

int bt_boom_take(int x, int y, int z, int p)
{
    unsigned idx = TIDX(x, y, z);
    int i;

    for (i = 0; i < g_n_boom; i++)
        if (g_boom[i][2] && BOOM_IDX(g_boom[i]) == idx) {
            if (g_boom[i][2] > p)
                p = g_boom[i][2];
            memcpy(g_boom[i], g_boom[--g_n_boom], 3);
            break;
        }
    return p;
}

void bt_boom_run(int thrower)
{
    int i, best, p, x, y, z;
    unsigned idx;

    if (booming)
        return;                 /* (the blasts below come back here) */
    booming = 1;
    while (g_n_boom) {
        for (best = 0, i = 1; i < g_n_boom; i++)
            if (BOOM_KEY(g_boom[i]) < BOOM_KEY(g_boom[best]))
                best = i;
        idx = BOOM_IDX(g_boom[best]);
        p = g_boom[best][2];
        memcpy(g_boom[best], g_boom[--g_n_boom], 3);
        x = idx % g_mx;
        idx /= g_mx;
        y = idx % g_my;
        z = idx / g_my;
        if (!p)
            explode(x, y, z, 180 + (int)rnd(71), DT_HE, 10, -1);
        else {
            log_printf("T%d: terrain explodes at %d,%d,%d power %d", bt_turn, x, y, z, p);
            explode(x, y, z, p, DT_HE, p / 10, thrower);
        }
    }
    booming = 0;
}

/* The crashed UFO's power sources that blew up (mapgen.c map_crash_power):
   before the first turn (explodePowerSources), silent; much of the crew
   dies or is hurt and the hull opens; what they set off follows. */
void bt_crash_blasts(void)
{
    int i, n = 0;

    if (!g_n_boom)
        return;
    quiet_blast = 1;
    bt_boom_run(-1);
    quiet_blast = 0;
    /* the power sources' Elerium the blasts left (spawn.c, fx_blast_items) */
    for (i = 0; i < g_max_floor; i++)
        n += g_floor[i].item == IT_ELERIUM;
    log_printf("crash: %d Elerium-115 left", n);
}

/* ------------------------------------------------------------------ */
/* The start of a battle and the abort (here, not in bplayer: their strings
   stay in this section's code, -zc) */

/* The start of a battle, after the state in battle_run is reset: the
   soldiers look around (then battle_run readies the AI: the core calls
   one companion section at a time, see build.ps1). */
void battle_start(void)
{
    int i;
    unsigned k, n = g_mx * g_my * g_mz * 4;

    /* The objective's parts (battle.c objective_check; moved here from
       battle_run, out of the core). */
    bt_obj_left = 0;
    if (g_pak.objective)
        for (k = 0; k < n; k++)
            if (g_map[k] && g_pak.mcd[g_map[k]].special == g_pak.objective)
                bt_obj_left++;
    bt_obj_done = 0;
    if (bt_obj_left)
        log_printf("objective: %u parts of special %u", bt_obj_left, g_pak.objective);
    /* The brain: one part destroyed wins (alienDeployments.rul
       objectivesRequired: 1); the alien base's control needs every part. */
    if (bt_obj_left && g_pak.script == SCRIPT_BOSS)
        bt_obj_left = 1;
    if (g_shot_log)
        dump_map();
    for (i = g_carried; i < g_nunits; i++) {   /* Cydonia: the carried keep their wounds */
        g_units[i].armor_left = g_units[i].armor[1];    /* after the difficulty */
        g_units[i].wounds = 0;
        memset(g_units[i].wound, 0, sizeof g_units[i].wound);
    }
    bt_crash_blasts();
    for (i = 0; i < g_nunits; i++)
        if (g_units[i].faction == FACTION_XCOM) {
            busy_tick();        /* the busy pointer (input.h) */
            los_reveal(i);
            log_printf("%s at %d,%d,%d TU %d", g_units[i].name, g_units[i].x,
                       g_units[i].y, g_units[i].z, g_units[i].tu);
        }
    for (i = 0; i < g_nunits; i++)
        if (g_units[i].faction == FACTION_XCOM)
            update_vision(i, 0);
    los_box_reset();
}

/* LOG: file reads since the last call (overlay sections loaded from
   disk: the cost of the overlay area being smaller than all battle code).
   Here, not in the core: battle_run calls it next to this section's work. */
void log_loads(const char *what)
{
    log_printf("T%d: %s: %u reads, %lu KB", bt_turn, what, rd_count() - g_rd_mark,
               (rd_bytes() - g_rd_mark_bytes + 512) / 1024);
    g_rd_mark = rd_count();
    g_rd_mark_bytes = rd_bytes();
}

/* Aborting: only soldiers aboard the craft (its start tiles; on Mars the
   exit area to Cydonia's second stage) get away, conscious or not, a
   carried one where its carrier is (DebriefingState); the others are
   missing in action. A lost battle loses the unconscious too. Alien
   bodies outside are not recovered (SP_GONE), those aboard are: corpses,
   and the unconscious are captured (inv_recover). */
void abort_evacuate(void)
{
    int i, left = 0;
    char text[40];

    inv_bodies_sync();
    for (i = 0; i < g_nunits; i++) {
        Unit *u = &g_units[i];
        if (u->faction == FACTION_ALIEN && !u->alive && !unit_in_exit_area(u))
            u->special |= SP_GONE;
        if ((!u->alive && !u->stunned) || u->faction != FACTION_XCOM)
            continue;
        if (!unit_in_exit_area(u) || bt_result == -1) {
            log_printf("%s left behind", u->name);
            if (u->alive)
                unit_kill(i);
            u->stunned = 0;
            left++;
        }
    }
    /* What lies outside is left too (Cydonia: the exit area's items go on
       to the second stage). */
    {
        /* (a FloorItem starts with x, y, z as a Unit does: inv.c to_loot) */
        FloorItem __near *f;
        Carry __near *c;
        for (f = g_floor; f < g_floor_end; f++)
            if (f->item != NO_ITEM && !unit_in_exit_area((const Unit *)f))
                f->item = NO_ITEM;
        for (c = g_carry; c < g_carry_end; c++)   /* the loot of a full floor list (inv.h) */
            if (c->unit == CARRY_LOOT && !c->slot)
                c->unit = CARRY_FREE;
    }
    if (left) {
        sprintf(text, "%d soldier%s left behind.", left, left > 1 ? "s" : "");
        msg(COL_BAD, text);
        wait_ms(1500);
    }
}
