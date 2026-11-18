/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battlescape core: turns, walking, shooting, damage and the services the
   AI uses (the player's commands are in bplayer.c, see battle_in.h). */

#include <bios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "battle.h"
#include "battle_in.h"
#include "batui.h"
#include "bsave.h"
#include "effects.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "panel.h"
#include "path.h"
#include "psi.h"
#include "render.h"
#include "rules.h"
#include "sound.h"
#include "timer.h"
#include "unit.h"
#include "uik.h"
#include "vox.h"

int bt_sel = -1;                /* selected X-COM unit */
int bt_turn = 1;
static int side;                /* faction whose turn it is (reaction fire) */
int bt_reserve;                 /* X-COM TU reserve: 0 none, else SHOT_* + 1 */
int panicking;           /* panicking units act: no TU reserve */
static int fog_dirty;           /* revealed tiles waiting for a full redraw */
int bt_result;                  /* 0 = running, 1 = won, -1 = lost, -2 = quit */
int bt_request;                 /* the options window wanted (bsave.c) */
int g_night;                    /* set before battle_run: night mission */
unsigned char g_bopt[BO_N] = { 5, 2, 5, 5, 5 };    /* Game Options (battle.h) */
/* An X-COM walk (walk): the walker, the units it saw when the walk began
   or has spotted since (a bit each), and whether it spotted an alien
   (also by a door it opened). */
static int walk_u = -1;
static unsigned char walk_new;
static unsigned char walk_saw[(MAX_UNITS + 7) / 8];
int g_civ_saved;                /* civilians alive after a won terror mission */
int g_see_all;                  /* debugging (SEEALL): every unit visible */
int g_shot_log;                 /* debugging (SHOTLOG): shot paths, map dump */
unsigned char g_view_hold;      /* tests (HOLDVIEW n): stays on a picture (bview.c) */

/* ------------------------------------------------------------------ */

void msg(int colour, const char *s)
{
    panel_message(colour, s);
    if (s[0])
        log_printf("T%d: %s", bt_turn, s);
}

/* Events X-COM cannot see: logged, not shown. */
void log_msg(int colour, const char *s)
{
    (void)colour;
    log_printf("T%d: (%s)", bt_turn, s);
}

/* The Game Options' speeds paced as DOS's (docs/design.md, "Pacing"):
   the settings 1-6 spread evenly over OpenXcom's sliders. Movement: ms
   per animation frame (battleXcomSpeed / battleAlienSpeed 40 .. 1; a
   step is 8 frames, 16 on a diagonal, a turn one an eighth). Fire: ms a
   round takes per tile (16 voxels at battleFireSpeed 1, 5, 9, 12, 16, 20
   voxels a 16.7 ms frame). */
static const unsigned char frame_ms[7] = { 0, 40, 32, 24, 17, 9, 1 };
const unsigned g_tile_ms[7] = { 0, 267, 53, 30, 22, 17, 13 };   /* (battle.h) */
#define pace timer_pace         /* (timer.h: least times, the drawing counts) */

/* An animation frame at the movement speed of u's side, in ms. */
static unsigned move_ms(const Unit *u)
{
    return frame_ms[g_bopt[u->faction == FACTION_XCOM ? BO_XCOM : BO_ALIEN]];
}

/* Eighths between two directions. */
static int eighths(int a, int b)
{
    int n = (b - a) & 7;
    return n > 4 ? 8 - n : n;
}

static int count_alive(int faction)
{
    int i, n = 0;
    for (i = 0; i < g_nunits; i++)
        if (g_units[i].alive && g_units[i].faction == faction)
            n++;
    return n;
}

int unit_visible(const Unit *u)
{
    return u->alive && (u->faction == FACTION_XCOM || u->seen);
}

/* Objective tiles (alien base control, the brain): destroying the last
   part of the brain wins the battle; the alien base's control centre
   only says so (DOS objectivePopup) and the battle goes on, but an abort
   after it is a success (DebriefingState: !aborted || objectives done).
   Counted at the start (bturn.c battle_start). */
unsigned bt_obj_left;           /* objective parts left (battle_in.h) */
unsigned char bt_obj_done;

static void objective_check(unsigned char from, unsigned char to)
{
    if (bt_obj_left && g_pak.mcd[from].special == g_pak.objective
        && g_pak.mcd[to].special != g_pak.objective && !--bt_obj_left) {
        if (g_pak.script == SCRIPT_BOSS)
            bt_result = 1;
        else {
            bt_obj_done = 1;
            msg(COL_GOOD, "Control center destroyed! Now abort.");
        }
    }
}

void check_result(void)
{
    if (!count_alive(FACTION_ALIEN))
        bt_result = 1;
    else if (!count_alive(FACTION_XCOM))
        bt_result = -1;
}

/* An alien under X-COM mind control goes back to its side (at the end of
   the X-COM turn, or when it falls). */
void psi_release(int u)
{
    Unit *un = &g_units[u];

    if (!(un->psi_state & PSI_CTRL))
        return;
    un->psi_state &= ~PSI_CTRL;
    un->faction = FACTION_ALIEN;
    un->seen = 1;
    if (bt_sel == u)
        bt_sel = -1;
}

/* ------------------------------------------------------------------ */
/* HUD */

void hud_status(void)
{
    char line[48];
    int i;

    (void)line;
    i = bt_sel >= 0 && g_units[bt_sel].alive ? bt_sel : -1;
    panel_status(i, g_cur_z);
    /* DOS hides the markers while the selected unit is not the player's
       (updateSoldierInfo: playableUnitSelected). */
    panel_marks(side == FACTION_XCOM ? i : -1);
}

/* ------------------------------------------------------------------ */
/* View */

/* Redraws the screen area of a unit standing at (x, y, z): one tile, or
   the four of a 2x2 unit. */
static void render_at(const Unit *un, int x, int y, int z)
{
    if (un->big)
        render_rect(TILE_SX(x, y + 1), TILE_SY(x, y, z) - 24,
                    TILE_SX(x + 1, y) + 32, TILE_SY(x + 1, y + 1, z) + 40);
    else
        render_tile(x, y, z);
}

/* The map again instead of the Hidden Movement screen (ai.c, gfx.h): as
   DOS's Map::draw, when the unit acting is seen, a shot crosses tiles X-COM
   knows or a blast goes off there, and for the player's turn. */
void bt_view_on(void)
{
    if (!g_view_off)
        return;
    g_view_off = 0;
    gfx_present_cells(0, 0, 40, VIEW_H);
}

void redraw_unit(int u)
{
    const Unit *un = &g_units[u];
    render_at(un, un->x, un->y, un->z);
}

/* Unit u turns to dir (UnitTurnBState; d: its body's direction or an HWP's
   turret): one seen takes an animation frame an eighth (drawn once). */
static void turn_unit(int u, unsigned char *d, int dir)
{
    unsigned long t0 = timer_read();
    int n = eighths(*d, dir);

    *d = (unsigned char)dir;
    if (n && unit_visible(&g_units[u])) {
        redraw_unit(u);
        pace(t0, n * move_ms(&g_units[u]), 't');
    }
}

void flush_fog(void)
{
    int x0, y0, x1, y1;

    if (fog_dirty) {
        /* Redraw just the area around the newly revealed tiles. */
        if (los_box_screen(&x0, &y0, &x1, &y1))
            render_rect(x0, y0, x1, y1);
        fog_dirty = 0;
    }
    los_box_reset();
}

int next_soldier(int from)
{
    int i;
    for (i = 1; i <= g_nunits; i++) {
        int u = (from + i) % g_nunits;
        if (g_units[u].alive && g_units[u].faction == FACTION_XCOM)
            return u;
    }
    return -1;
}

/* Updates vision after unit u acted (reveal: also uncover the fog of war,
   which is slower). In an X-COM walk it notes the aliens the walker
   spots that it had not seen in this walk (walk_new; UnitWalkBState: the
   walker's own units spotted). */
void update_vision(int u, int reveal)
{
    int i;

    if (reveal && g_units[u].faction == FACTION_XCOM && los_reveal(u))
        fog_dirty = 1;
    /* Aliens whose visibility changed must be redrawn; when an alien or a
       civilian acted, only its own visibility can have changed. */
    for (i = 0; i < g_nunits; i++) {
        Unit *a = &g_units[i];
        unsigned char was;
        if (a->faction == FACTION_XCOM || !a->alive
            || (g_units[u].faction != FACTION_XCOM && i != u))
            continue;
        was = a->seen;
        a->seen = (unsigned char)g_see_all;
        {
            /* The one who acted first: a walker's own sight (walk). */
            int k, s;
            for (k = -1; k < g_nunits; k++) {
                s = k < 0 ? u : k;
                if (k != u && g_units[s].faction == FACTION_XCOM && LOS_NEAR(&g_units[s], a)
                    && los_unit_sees(s, i)) {
                    a->seen = 1;
                    if (s == walk_u) {
                        unsigned char *p = &walk_saw[i >> 3], m = (unsigned char)(1 << (i & 7));
                        if (!(*p & m)) {
                            *p |= m;
                            if (a->faction == FACTION_ALIEN)
                                walk_new = 1;
                        }
                    }
                    break;
                }
            }
        }
        /* A unit seen on a tile the fog's rays left black (other levels,
           corners) has that tile shown (DOS draws the unit dark there). */
        if (a->seen && !was && g_seen) {
            int j, k;
            for (j = 0; j <= a->big; j++)
                for (k = 0; k <= a->big; k++)
                    g_seen[TIDX(a->x + j, a->y + k, a->z)] |= SEEN_BIT;
        }
        if (a->seen != was)
            redraw_unit(i);
    }
}

/* ------------------------------------------------------------------ */
/* Terrain */

static unsigned char fall_check;    /* terrain changed: someone may fall */

/* A tile's parts changed. Its picture is the caller's: the tile's redraw
   (terrain_changed, battle_in.h), or one redraw of all that a blast broke
   (effects.c blast_part). */
void terrain_parts(int x, int y, int z)
{
    fall_check = 1;
    if (g_shot_log) {
        const unsigned char *t = TILE(x, y, z);
        log_printf("TILE %d,%d,%d %u %u %u %u", x, y, z, t[0], t[1], t[2], t[3]);
    }
    path_update_tile(x, y, z);
    los_update_tile(x, y, z);
}

/* ------------------------------------------------------------------ */
/* Combat */

/* Morale change with bounds; 2x2 units feel no fear. */
void morale_change(Unit *u, int change)
{
    int m = u->morale + change;

    if (!RL_FEARABLE(u))
        return;
    u->morale = (unsigned char)(m < 0 ? 0 : m > 100 ? 100 : m);
}

/* The alien it was born as, for a unit under mind control. */
static int born_faction(const Unit *u)
{
    return u->psi_state & PSI_CTRL ? FACTION_ALIEN : u->faction;
}

/* A soldier hit outside X-COM's turn must be seen (Map::draw _unitDying;
   src/bview.c). */
#define SOLDIER_SEEN(u) ((u)->faction == FACTION_XCOM && side != FACTION_XCOM)

/* A chryssalid where a zombie fell (its race's terror unit). */
static void zombie_burst(int z)
{
    int i, c;

    for (i = 0; i < 8; i++)
        if (g_pak.race[i].valid && (g_pak.race[i].flags & AT_ZOMBIFY))
            break;
    if (i == 8 || (c = unit_spawn_type(&g_pak.race[i], g_units[z].x, g_units[z].y,
                                       g_units[z].z, g_units[z].dir)) < 0)
        return;
    msg(COL_BAD, "A Chryssalid bursts out of the zombie!");
    ai_unit_new(c);
    update_vision(c, 0);
}

/* A unit dies (shooter -1: bled to death). Out of X-COM's sight only the
   log hears it. zombify: a chryssalid's victim rises as a zombie. */
void kill_unit(int victim, int shooter, int zombify)
{
    Unit *v = &g_units[victim];
    const Unit *s = shooter >= 0 ? &g_units[shooter] : NULL;
    char text[48];
    /* down: already lying unconscious (bled to death), off the map. */
    int down = !v->alive, i;
    int was_visible = down ? v->faction == FACTION_XCOM || v->seen : unit_visible(v);
    int quiet = !was_visible && !(s && unit_visible(s));
    void (*say)(int, const char *) = quiet ? log_msg : msg;
    int leader = rl_leader_mod(), rank_mod = rl_rank_mod(v);

    if (down) {
        v->stunned = 0;
        v->hp = 0;
    } else {
        psi_release(victim);
        ai_unit_out(victim);
        unit_kill(victim);
        fall_check = 1;         /* a flyer's body falls */
    }
    if (v->faction == FACTION_CIVILIAN) {
        if (s && s->faction == FACTION_ALIEN)
            g_civ_killed_alien++;
        else
            g_civ_killed_xcom++;
    }
    /* Morale as BattlescapeGame::checkForCasualties: the killer gains
       (loses for friendly fire or a civilian), the fallen's side loses by
       bravery and the fallen's rank (less with a high-ranked leader), the
       other sides gain. */
    if (s && shooter != victim) {
        Unit *k = &g_units[shooter];
        int mod = k->faction == FACTION_XCOM ? leader : 100;
        if (v->faction == FACTION_CIVILIAN)
            morale_change(k, born_faction(k) == FACTION_XCOM ? -(1000 / mod) : 10);
        else if (born_faction(k) == v->faction)
            morale_change(k, -(2000 / mod));
        else if (k->faction != FACTION_CIVILIAN)
            morale_change(k, 20 * mod / 100);
    }
    if (v->faction != FACTION_CIVILIAN)
        for (i = 0; i < g_nunits; i++) {
            Unit *c = &g_units[i];
            if (!c->alive)
                continue;
            if (born_faction(c) == v->faction)
                morale_change(c, -(rank_mod * 200 * ((110 - c->bravery) / 10)
                                   / (v->faction == FACTION_ALIEN ? 100 : leader) / 100));
            else
                morale_change(c, 10 * (v->faction == FACTION_ALIEN ? leader : 100) / 100);
        }
    if (s && v->faction == FACTION_ALIEN && s->faction == FACTION_XCOM)
        g_units[shooter].kills++;
    if (!quiet)
        snd_kill(v->faction == FACTION_ALIEN);
    say(v->faction == FACTION_ALIEN ? COL_GOOD : COL_BAD,
        unit_fmt(text, s ? "%s killed!" : "%s died of wounds", v, 0));
    if (zombify && (i = unit_spawn_zombie(victim)) >= 0) {
        v->special |= SP_GONE;      /* no body: the zombie is it */
        sprintf(text, "%s rises as a zombie!", v->name);
        say(COL_BAD, text);
        ai_unit_new(i);
        update_vision(i, 0);
    } else if (v->special == SP_ZOMBIE)
        zombie_burst(victim);
    if (was_visible)
        redraw_unit(victim);
    if (SOLDIER_SEEN(v))
        bv_fallen(victim);
    if (victim == bt_sel)
        bt_sel = -1;
    check_result();
}

/* A unit falls unconscious (stun at least its health): out of the battle
   until it comes to (stunned aliens are captured if X-COM wins). */
void knock_out(int victim)
{
    Unit *v = &g_units[victim];
    char text[48];
    int was_visible = unit_visible(v), hp = v->hp;

    psi_release(victim);
    ai_unit_out(victim);
    unit_kill(victim);
    v->hp = (unsigned char)hp;
    v->stunned = 1;
    fall_check = 1;             /* a flyer falls */
    unit_fmt(text, "%s is unconscious!", v, 0);
    if (was_visible) {
        snd_hit();
        msg(v->faction == FACTION_ALIEN ? COL_GOOD : COL_BAD, text);
        redraw_unit(victim);
    } else
        log_msg(COL_INFO, text);
    if (SOLDIER_SEEN(v))
        bv_fallen(victim);
    if (victim == bt_sel)
        bt_sel = -1;
    check_result();
}

static unsigned char add255(int a, int b)
{
    return (unsigned char)(a + b > 255 ? 255 : a + b);
}

/* Rolled damage: 0-200 % of the power, 50-150 % for explosives
   (Mod::DAMAGE_RANGE 100, EXPLOSIVE_DAMAGE_RANGE 50). */
static int roll_damage(int power, int dtype)
{
    int r = dtype == DT_HE ? 50 : 100, lo = power * (100 - r) / 100;

    return lo + (int)rnd(power * (100 + r) / 100 - lo + 1);
}

/* A hit on a unit (BattleUnit::damage, TileEngine::hit): power is the
   rolled damage, side SIDE_*, hclass the height hit (2 head, 1 body, 0
   legs). The armour's damage modifier and the side's armour apply; health
   damage adds some stun, wears the armour down and gives soldiers fatal
   wounds; morale falls with the damage. Returns the damage done. */
static int hurt(int victim, int shooter, int power, int dtype, int side, int hclass)
{
    Unit *v = &g_units[victim];
    const Unit *s = shooter >= 0 ? &g_units[shooter] : NULL;
    char text[48];
    int quiet = !unit_visible(v) && !(s && unit_visible(s));
    void (*say)(int, const char *) = quiet ? log_msg : msg;
    int dmg, part = rl_part(side, hclass);

    if (!v->alive)
        return 0;
    ai_hit(victim, shooter);
    v->psi_state |= PSI_HIT;
    dmg = power * rl_dmg_mod(v, dtype) / 100;
    if (dtype == DT_SMOKE)
        dtype = DT_STUN;
    dmg -= rl_armour(v, side);
    if (dmg <= 0) {
        say(COL_INFO, unit_fmt(text, "%s hit, no damage", v, 0));
        return 0;
    }
    if (dtype == DT_STUN)
        v->stun = add255(v->stun, dmg);
    else {
        /* A chryssalid's sting that gets through kills and implants. */
        if (s && s->special == SP_ZOMBIFY && v->faction != FACTION_ALIEN && !v->big) {
            kill_unit(victim, shooter, 1);
            return dmg;
        }
        if (dmg >= v->hp) {
            kill_unit(victim, shooter, 0);
            return dmg;
        }
        v->hp -= dmg;
        if (dtype != DT_IN) {
            if (rl_dmg_mod(v, DT_STUN))
                v->stun = add255(v->stun, rnd(dmg / 4 + 1));
            if (RL_WOUNDABLE(v)) {
                if ((int)rnd(11) < dmg && v->wounds < 200) {
                    int w = 1 + rnd(3);
                    v->wound[part] += (unsigned char)w;
                    v->wounds += (unsigned char)w;
                }
                morale_change(v, -v->wound[part]);
            }
            rl_wear(v, side, dmg);
        }
    }
    morale_change(v, -(100 * (dmg * ((110 - v->bravery) / 10) / 10)
                       / (v->faction == FACTION_XCOM ? rl_leader_mod() : 100)));
    if (!quiet)
        snd_hit();
    say(v->faction == FACTION_ALIEN ? COL_GOOD : COL_BAD,
        unit_fmt(text, dtype == DT_STUN ? "%s stunned (%d)" : "%s hit for %d", v, dmg));
    if (v->stun >= v->hp)
        knock_out(victim);
    return dmg;
}

/* An alien by birth (firing experience is for hits on them). */
#define ALIEN_BORN(u) ((u)->xtype < XT_XCOM || (u)->xtype == XT_ZOMBIE)

/* fire_round: what the projectile does on impact (BLAST_*). */
static int round_blast;

/* Waypoints of the next launch (Blaster Launcher, Fusion Bomb): set by the
   player's waypoint screen (bact.c), used and cleared by shoot. */
unsigned char bt_wp[BT_MAX_WP][3];
int bt_nwp;

void bt_break(unsigned char *t, int pt, int z)
{
    const Mcd *m = &g_pak.mcd[t[pt]];

    objective_check(t[pt], m->die_mcd);
    if (m->flags & MCD_EXPL)
        bt_boom_add(t, t[pt]);
    if (m->tile_type & MCD_LIGHT)
        g_pak.relight = 1;      /* the terrain's light again (pak_light_map) */
    /* a ground floor without a destroyed version becomes scorched earth
       (older packs: none, the floor stays where blasts and fire are
       concerned) */
    t[pt] = m->die_mcd || pt != PART_FLOOR || z ? m->die_mcd : g_pak.scorched;
}

/* A bullet hitting terrain: 25-75 % of its power against the part's
   armour (TileEngine::hit); an explosive part blows up (bturn.c). */
static void damage_terrain(int x, int y, int z, int part, int power, int s)
{
    unsigned char *t = TILE(x, y, z);
    const Mcd *m;

    if (!t[part])
        return;
    m = &g_pak.mcd[t[part]];
    if (m->armor == 255 || power / 4 + (int)rnd(power * 3 / 4 - power / 4 + 1) < m->armor)
        return;
    bt_break(t, part, z);
    terrain_changed(x, y, z);
    bt_fall();
    if (g_n_boom)
        bt_boom_run(s);
}

int quiet_blast;                /* the crash's blasts: no sound, view stays */

/* Height of a unit's body in voxels (24 a level); kneeling is lower. */
static int body_height(const Unit *u)
{
    const UnitXStat *x = unit_xstat(u);
    return u->kneeling ? x->kneel : x->height;
}

/* Where unit s aims at the tile (tx, ty, tz) from its muzzle o
   (ProjectileFlyBState::init): a voxel of the unit there that a line from
   o reaches (canTargetUnit; X-COM aims at units it sees), else of the
   object, the north wall, the west wall or the floor (canTargetTile;
   without one, a point of that part), else the middle of the tile; a
   berserk unit and a launched missile aim at the middle. 0 if no line
   reaches the unit there (no line of fire). */
static int aim_at(int s, const int *o, int tx, int ty, int tz, int *t)
{
    /* the fallback points of floor, west wall, north wall, object */
    static const unsigned char fb[4][3] = { { 8, 8, 2 }, { 0, 8, 9 }, { 8, 0, 9 }, { 8, 8, 10 } };
    static const unsigned char order[4] = { PART_OBJECT, PART_NORTHWALL, PART_WESTWALL, PART_FLOOR };
    const unsigned char *tl = TILE(tx, ty, tz);
    unsigned char occ = UNIT_AT(tx, ty, tz);
    int k, p;

    t[0] = tx * 16 + 8;
    t[1] = ty * 16 + 8;
    t[2] = tz * 24 + 12;
    if (g_units[s].panicked == PANIC_BERSERK)
        return 1;
    if (occ && (g_units[s].faction != FACTION_XCOM || unit_visible(&g_units[occ - 1]))) {
        if (occ - 1 == s) {
            t[2] = tz * 24;     /* not at itself: at the floor */
            return 1;
        }
        return vox_target_unit(o, occ - 1, t);
    }
    for (k = 0; k < 4; k++)
        if (tl[p = order[k]]) {
            if (!vox_target_tile(o, tx, ty, tz, p, t)) {
                t[0] = tx * 16 + fb[p][0];
                t[1] = ty * 16 + fb[p][1];
                t[2] = tz * 24 + fb[p][2];
            }
            break;
        }
    return 1;
}

/* X-COM's first round needs a line of fire (Projectile::calculateTrajectory):
   the line to the aim point may stop only at the target tile, at the near
   wall of the tile beyond it, or at the target unit. */
static int xcom_lof(const int *o, const int *t, int tx, int ty, int tz)
{
    int r = vox_line(o, t, 0), hx = g_vox_hit[0] >> 4, hy = g_vox_hit[1] >> 4,
        hz = g_vox_hit[2] < 0 ? -1 : g_vox_hit[2] / 24;

    if (r == V_EMPTY)
        return 1;
    if (r == V_UNIT && IN_MAP(hx, hy, hz) && !UNIT_AT(hx, hy, hz))
        hz--;               /* a head from the tile below */
    if (hx == tx && hy == ty && hz == tz)
        return 1;
    if (r == V_NORTHWALL)
        return hy - 1 == ty;
    if (r == V_WESTWALL)
        return hx - 1 == tx;
    return r == V_UNIT && UNIT_AT(hx, hy, hz) == UNIT_AT(tx, ty, tz);
}

/* 1 if unit s has a line of fire on unit t: a line from its muzzle reaches
   t before any terrain or other unit (TileEngine::canTargetUnit, which
   reaction fire and the AI check: nobody fires through a comrade). */
static int line_of_fire(int s, int t)
{
    int o[3], v[3];

    vox_origin(s, g_units[t].x, g_units[t].y, 0, o);
    g_vox_excl = s;
    return vox_target_unit(o, t, v);
}

/* A unit hit at voxel h (TileEngine::hit): the side and height hit from
   where the voxel is against the unit's middle (BattleUnit::damage). */
static void hit_unit_at(int victim, int s, const int *h, int power, int dtype)
{
    Unit *v = &g_units[victim];
    int c = v->big ? 16 : 8, rz;

    rz = h[2] - (v->z * 24 + unit_xstat(v)->floating - vox_level(h[0] >> 4, h[1] >> 4, h[2] / 24));
    if (h[2] / 24 != v->z)
        rz -= 24;           /* the head in the tile above (verticaloffset) */
    if (RL_WOUNDABLE(&g_units[s]) && ALIEN_BORN(v))
        g_units[s].hits++;  /* firing experience */
    hurt(victim, s, roll_damage(power, dtype), dtype,
         rl_side(v, h[0] - v->x * 16 - c, h[1] - v->y * 16 - c),
         rz >= body_height(v) ? 2 : rz > 4 ? 1 : 0);
}

/* One projectile from unit s at its muzzle o, aimed at voxel aim
   (OpenXcom Projectile): the aim moves by rl_deviate for the accuracy and
   the line goes on past it until it hits something; a launched missile
   flies from waypoint to waypoint (its last leg ends at the last one). */
static void fire_round(int s, const int *o0, const int *aim, int acc, int power, int dtype)
{
    Unit *u = &g_units[s];
    int o[3], t[3], h[3], i, n, res, leg = 0, shown = unit_visible(u);

    snd_fire(u->weapon);        /* (seen or not: ProjectileFlyBState::createNewProjectile) */
    o[0] = o0[0];
    o[1] = o0[1];
    o[2] = o0[2];
next_leg:
    if (bt_nwp) {
        /* aimed a bit above the middle of each waypoint's tile
           (ProjectileFlyBState, BA_LAUNCH) */
        t[0] = bt_wp[leg][0] * 16 + 8;
        t[1] = bt_wp[leg][1] * 16 + 8;
        t[2] = bt_wp[leg][2] * 24 + 16;
        /* (BT_WP_FLOOR) the second on one tile: at the floor (targetFloor) */
        if (t[2] > 3000)
            t[2] -= 3086;
    } else {
        t[0] = aim[0];
        t[1] = aim[1];
        t[2] = aim[2];
    }
    rl_deviate(o, t, acc);
    g_vox_excl = s;
    res = vox_line(o, t, !bt_nwp || leg == bt_nwp - 1);
    h[0] = g_vox_hit[0];
    h[1] = g_vox_hit[1];
    h[2] = g_vox_hit[2];
    if (g_shot_log)
        log_printf("SHOT T%d u%d %d,%d,%d org %d,%d,%d aim %d,%d,%d acc %d => %d at %d,%d,%d",
                   bt_turn, s, u->x, u->y, u->z, o[0], o[1], o[2], t[0], t[1], t[2], acc, res,
                   h[0], h[1], h[2]);
    /* Tracer: a dot a tile apart along the line's longest axis (where
       OpenXcom's trajectory has a point per voxel), where X-COM sees (DOS
       draws the round itself), at the Fire Speed's pace. */
    {
        long dx = h[0] - o[0], dy = h[1] - o[1], dz = h[2] - o[2];
        unsigned len = (unsigned)labs(dx), m = (unsigned)labs(dy);
        int px[48], py[48], nd = 0;
        unsigned long t0;
        if (m > len)
            len = m;
        if ((m = (unsigned)labs(dz)) > len)
            len = m;
        n = len / 16 + 1;
        if (n > 48)
            n = 48;
        /* Outside X-COM's turn the view goes to a round X-COM sees that
           ends off the screen or on another level (Map::drawTerrain; the
           section only then: it is loaded from disk). */
        if (side != FACTION_XCOM
            && (h[2] / 24 != g_view_z || !render_tile_visible(h[0] >> 4, h[1] >> 4, h[2] / 24)))
            bv_round(o, h, n);
        for (i = 1; i <= n; i++) {
            int vx = o[0] + (int)(dx * i / n), vy = o[1] + (int)(dy * i / n), vz = o[2] + (int)(dz * i / n);
            int x = vx >> 4, y = vy >> 4, z = vz / 24, sx, sy;
            if (vx < 0 || vy < 0 || vz < 0 || !IN_MAP(x, y, z)
                || (g_seen && !(g_seen[TIDX(x, y, z)] & SEEN_BIT) && !shown))
                continue;
            sx = vx - vy + g_cam_x + 16;
            sy = ((vx + vy) >> 1) - vz + g_cam_y + 24;
            if (sx >= 0 && sx < 320 && sy >= 0 && sy < VIEW_H) {
                px[nd] = sx;
                py[nd++] = sy;
            }
        }
        if (nd)
            bt_view_on();
        t0 = timer_read();
        for (i = 0; i < nd; i++) {
            gfx_dot(px[i], py[i], COL_TRACER);
            pace(t0, (i + 1) * g_tile_ms[g_bopt[BO_FIRE]], i + 1 < nd ? 0 : 'f');
        }
        if (nd) {
            /* the hit: 4 frames of 50 ms (ExplosionBState, Explosion) */
            pace(timer_read(), 200, 'h');
            for (i = 0; i < nd; i++)
                gfx_dot(px[i], py[i], COL_TRACER);
        }
    }
    if (++leg < bt_nwp && res == V_EMPTY) {
        o[0] = h[0];            /* on to the next waypoint from here */
        o[1] = h[1];
        o[2] = h[2];
        log_printf("T%d: missile at waypoint %d (%d,%d,%d)", bt_turn, leg, h[0] >> 4, h[1] >> 4,
                   h[2] / 24);
        goto next_leg;
    }

    if (round_blast) {
        /* At the unit hit, else a step before what stopped it; nothing
           when it left the map (ProjectileFlyBState::think). */
        int clip, b[3], bx, by, bz;
        if (res == V_UNIT) {
            b[0] = h[0];
            b[1] = h[1];
            b[2] = h[2];
        } else
            vox_prev(b);
        bx = b[0] >> 4;
        by = b[1] >> 4;
        bz = b[2] < 0 ? -1 : b[2] / 24;
        if (res == V_OUT || b[0] < 0 || b[1] < 0 || !IN_MAP(bx, by, bz))
            return;
        clip = u->load ? u->load : g_items[u->weapon].clip_item;
        fx_bv = (unsigned char)((b[0] & 15) << 4 | (b[1] & 15));    /* (big walls) */
        if (round_blast == BLAST_STUN)
            explode(bx, by, bz, power, DT_STUN, rl_blast_radius(clip, power), s);
        else if (round_blast == BLAST_FIRE) {
            bv_blast(bx, by, bz);                   /* seen and heard as any */
            fx_incendiary(bx, by, bz, power, s);    /* fire damage only */
        }
        else
            explode(bx, by, bz, power, DT_HE, rl_blast_radius(clip, power), s);
        fx_bv = 0x88;
    } else if (res == V_UNIT) {
        int x = h[0] >> 4, y = h[1] >> 4, z = h[2] / 24;
        unsigned char occ = UNIT_AT(x, y, z);
        if (!occ && z)
            occ = UNIT_AT(x, y, z - 1);
        if (occ)
            hit_unit_at(occ - 1, s, h, power, dtype);
    } else if (res >= V_FLOOR && res <= V_OBJECT)
        damage_terrain(h[0] >> 4, h[1] >> 4, h[2] / 24, res, power, s);
    else if (u->faction == FACTION_XCOM)
        msg(COL_INFO, "Missed");
}

/* Fires at a tile in a SHOT_ mode. Returns 1 if a shot was fired. A
   berserk unit aims half as well. Enemies may react to the shooter. */
int shoot(int s, int tx, int ty, int tz, int mode)
{
    Unit *u = &g_units[s];
    const ItemDef *w;
    int cost = unit_shot_tu(u, mode), rounds = mode == SHOT_AUTO ? 3 : 1, i, acc, ok;
    int xcom = u->faction == FACTION_XCOM, o[3], aim[3];

    if (u->weapon == NO_ITEM || !cost) {
        if (xcom)
            msg(COL_BAD, u->weapon == NO_ITEM ? "No weapon" : "This weapon cannot do that");
        return 0;
    }
    w = &g_items[u->weapon];
    if (u->tu < cost) {
        if (xcom)
            msg(COL_BAD, "Not enough time units");
        return 0;
    }
    if (w->clip_size && !u->ammo) {
        if (xcom)
            msg(COL_BAD, inv_clips(u) ? "Out of ammo: R to reload" : "Out of ammo");
        return 0;
    }
    if (tx == u->x && ty == u->y && tz == u->z)
        return 0;
    if (u->weapon == IT_STUNROD) {
        int dx = tx - u->x, dy = ty - u->y;
        unsigned char occ = UNIT_AT(tx, ty, tz);
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || tz != u->z || !occ) {
            if (xcom)
                msg(COL_BAD, "Stun rod: target must be next to you");
            return 0;
        }
        u->tu -= cost;
        turn_unit(s, &u->dir, dir_towards(u->x, u->y, tx, ty));
        if ((int)rnd(100) < unit_shot_acc(u, mode)) {
            Unit *v = &g_units[occ - 1];
            hurt(occ - 1, s, roll_damage(w->power, DT_STUN), DT_STUN,
                 rl_side(v, u->x - v->x, u->y - v->y), 1);
        } else
            msg(COL_INFO, "Missed");
        if (u->alive && !bt_result)
            reaction_fire(s);
        return 1;
    }
    /* An HWP turns its turret (BattleUnit::lookAt with a turret). */
    turn_unit(s, UNIT_HWP(u) ? &u->turret : &u->dir, dir_towards(u->x, u->y, tx, ty));
    /* The aim point once for all rounds; X-COM's shot needs a line of
       fire, else it costs nothing (ProjectileFlyBState::createNewProjectile). */
    vox_origin(s, tx, ty, 0, o);
    g_vox_excl = s;
    ok = bt_nwp || aim_at(s, o, tx, ty, tz, aim);       /* (a missile: at its waypoints) */
    if (xcom && !bt_nwp && u->panicked != PANIC_BERSERK && (!ok || !xcom_lof(o, aim, tx, ty, tz))) {
        msg(COL_BAD, "No line of fire");
        bt_nwp = 0;
        return 0;
    }
    u->tu -= cost;
    round_blast = fx_round_blast(u);
    acc = unit_shot_acc(u, mode);
    if (bt_nwp)
        acc = xcom ? 60 : 55;   /* guided: Projectile::calculateTrajectory */
    if (u->panicked == PANIC_BERSERK)
        acc /= 2;
    for (i = 0; i < rounds && ok && u->alive && !bt_result; i++) {
        if (w->clip_size) {
            if (!u->ammo)
                break;
            u->ammo--;
        }
        fire_round(s, o, aim, acc, fx_round_power(u), rl_round_dtype(u));
    }
    round_blast = BLAST_NONE;
    bt_nwp = 0;
    if (u->alive && !bt_result)
        reaction_fire(s);
    return 1;
}

/* Armour of a body (its corpse item, items.rul; 20 when not given): a
   stronger HE blast destroys it and kills the unconscious. Soldiers by
   their armour 20 / 30 / 40, civilians 12, the five races' corpses (and
   the celatid's, the silacoid's) 26, the other terror units and the HWPs'
   wrecks 20. */
static int body_armour(const Unit *v)
{
    if (v->xtype >= XT_XCOM && v->xtype < XT_CIVILIAN)
        return v->xtype == XT_XCOM + ARM_FLYING ? 40 : 20 + 10 * (v->xtype - XT_XCOM);
    if (v->xtype == XT_CIVILIAN)
        return 12;
    /* live 24-29: cyberdisc, sectopod, celatid, chryssalid, silacoid, reaper */
    return v->xtype < 8 && v->live != 24 && v->live != 25 && v->live != 27 && v->live != 29
           ? 26 : 20;
}

int bt_blast_power(int cx, int cy, int cz, int power, int radius, int x, int y)
{
    int d = fx_blast_dist(x - cx, y - cy), p = power - 10 * d;

    if (d > radius || p <= 0 || !IN_MAP(x, y, cz) || (p -= 2 * fx_line_he(cx, cy, cz, x, y)) <= 0)
        return 0;
    return p;
}

/* Explosions as OpenXcom TileEngine::explode with flat blasts (the DOS
   default): the power falls by 10 a tile out to the radius, on the level
   of the blast, and by twice the HE blockage of every wall and object it
   passes (fx_line_he: the straight tile line from the centre; walls that
   block sight too, broken or not). Items, devices and bodies on the floor
   that the power there exceeds the armour of are destroyed, before the
   units are hurt (what the dead drop comes after the blast); grenades
   among them do not go off. Every tile of a unit in the blast takes
   50-150 % of the power there (stun 0-200 %), against the under armour at
   ground zero (the centre and its 8 neighbours), else the side facing the
   blast. The terrain comes last, as DOS's detonate after the rays: every
   tile reached gives its power to its object, its four walls (the east
   and south ones are its neighbours'), its floor and the floor above, and
   to a big wall beside it (detonate), outer rings first, so that each
   tile's power comes from the terrain as it stood. */
void explode(int cx, int cy, int cz, int power, int dtype, int radius, int thrower)
{
    int x, y, i, d, held = 0, gone = 0, shown = 0;
    const Unit *th = thrower >= 0 ? &g_units[thrower] : NULL;
    unsigned long t0 = timer_read();

    if (!quiet_blast)
        shown = bv_blast(cx, cy, cz);   /* 1: no picture, its time still to keep */
    if (dtype == DT_HE)
        gone = fx_blast_items(cx, cy, cz, power, radius);
    for (i = 0; i < g_nunits; i++) {
        Unit *v = &g_units[i];
        int s = v->big ? 2 : 1, j, k, was = v->alive;
        if (v->z != cz || (!v->alive && ((v->special & SP_CARRIED) || (!v->stunned && unit_body(v) < 0))))
            continue;           /* (a carried body is in an inventory, off the map) */
        for (k = 0; k < s; k++)
            for (j = 0; j < s; j++) {
                int ux = v->x + j, uy = v->y + k, dx = ux - cx, dy = uy - cy, p;
                if ((p = bt_blast_power(cx, cy, cz, power, radius, ux, uy)) == 0)
                    continue;
                if (!v->alive) {
                    /* a body on the ground (one of a 2x2 unit's quarters
                       destroys it all) */
                    if (was || dtype != DT_HE || p <= body_armour(v) || (v->special & SP_GONE))
                        continue;
                    if (v->stunned) {
                        char text[48];
                        v->stunned = 0;
                        v->hp = 0;
                        if (v->faction == FACTION_CIVILIAN)
                            g_civ_killed_xcom++;
                        log_msg(COL_INFO, unit_fmt(text, "%s killed!", v, 0));
                    }
                    v->special |= SP_GONE;
                    redraw_unit(i);
                    gone++;
                    continue;
                }
                if (th && th->faction != v->faction && RL_WOUNDABLE(th))
                    g_units[thrower].hits++;    /* firing experience */
                hurt(i, thrower, roll_damage(p, dtype), dtype,
                     dx > -2 && dx < 2 && dy > -2 && dy < 2 ? SIDE_UNDER : rl_side(v, -dx, -dy), 1);
            }
    }
    if (dtype == DT_HE) {
        for (d = radius; d >= 0; d--)
            for (y = cy - d; y <= cy + d; y++, busy_tick())     /* (input.h) */
                for (x = cx - d; x <= cx + d; x++) {
                    int p;
                    if (fx_blast_dist(x - cx, y - cy) != d
                        || (p = bt_blast_power(cx, cy, cz, power, radius, x, y)) == 0)
                        continue;
                    held += p < power - 10 * d;
                    if (g_n_boom)       /* a tile that is to blow up: the stronger */
                        p = bt_boom_take(x, y, cz, p);
                    fx_detonate(x, y, cz, p);
                }
        log_printf("blast at %d,%d,%d power %d: %d tiles behind HE blockage, %d items destroyed",
                   cx, cy, cz, power, held, gone);
        fx_after_explode();
    }
    bt_fall();
    if (shown)                  /* its 8 frames of 50 ms (ExplosionBState) */
        pace(t0, 400, 'x');
    fx_bv = 0x88;               /* (the terrain's blasts: the middle) */
    if (g_n_boom)
        bt_boom_run(thrower);   /* terrain that blew up (bturn.c) */
}

/* A unit that fits at level z of its column: standing there (a 2x2 unit
   on any quarter). */
static int stands_at(const Unit *u, int z)
{
    int h, s = u->big ? 2 : 1, i, j;

    for (j = 0; j < s; j++)
        for (i = 0; i < s; i++)
            if (tile_standable(u->x + i, u->y + j, z, &h))
                return 1;
    return 0;
}

/* After terrain changed: units over a hole fall to the first floor below
   (UnitFallBState: no damage), flyers stay where they are; one that lands
   on a unit pushes it to a free neighbouring tile. Unconscious units,
   items on the ground and devices fall too (TileEngine::applyGravity). */
void bt_fall(void)
{
    int i, z, h, d, k;

    if (!fall_check)
        return;
    fall_check = 0;
    for (i = 0; i < g_nunits; i++) {
        Unit *u = &g_units[i];
        int oz = u->z;
        /* The dead fall as their bodies (corpse items in DOS); flyers
           stay up while they are conscious. */
        if (u->alive ? unit_flies(u)
                     : (unit_body(u) < 0 && !u->stunned) || (u->special & SP_CARRIED))
            continue;
        for (z = oz; z > 0 && !stands_at(u, z); z--)
            ;
        if (z == oz)
            continue;
        if (!u->alive) {
            u->z = (unsigned char)z;
            continue;
        }
        k = UNIT_AT(u->x, u->y, z);
        if (k && !u->big) {
            /* UnitFallBState: the unit below steps aside. */
            Unit *b = &g_units[k - 1];
            for (d = 0; d < 8; d++) {
                int nx = b->x + g_dir_dx[d], ny = b->y + g_dir_dy[d];
                if (!b->big && tile_standable(nx, ny, z, &h) && !UNIT_AT(nx, ny, z)) {
                    render_at(b, b->x, b->y, z);
                    unit_move_to(k - 1, nx, ny, z);
                    redraw_unit(k - 1);
                    break;
                }
            }
            if (d == 8)
                continue;       /* nowhere to go: it stays */
        } else if (u->big ? !unit_free_at(i, u->x, u->y, z) : k != 0)
            continue;
        unit_move_to(i, u->x, u->y, z);
        if (unit_visible(u)) {
            render_at(u, u->x, u->y, oz);
            redraw_unit(i);
        }
        log_printf("T%d: %s falls to %d,%d,%d", bt_turn, u->name, u->x, u->y, z);
        update_vision(i, u->faction == FACTION_XCOM);
    }
    for (k = 0; k < g_max_floor; k++) {
        FloorItem *f = &g_floor[k];
        while (f->item != NO_ITEM && f->z > 0 && !tile_standable(f->x, f->y, f->z, &h))
            f->z--;
    }
    fx_fall();
    flush_fog();
}

/* Throws the belt grenade at a tile: belt to hand 4 TU, priming 50 %
   (not a flare), the throw 25 %; range, arc and miss as DOS (vox_throw,
   vox_throw_land). Grenades go off at the end of the turn
   (src/effects.c). */
int throw_grenade(int s, int tx, int ty, int tz)
{
    Unit *u = &g_units[s];
    int item = u->grenade, cost, c[3], x, y, z, h, acc, curv, fuse;

    if (item == NO_ITEM || !u->n_grenades) {
        msg(COL_BAD, "No grenades");
        return 0;
    }
    /* A grenade primed in hand (bplayer.c) costs only the throw. */
    cost = u->fuse ? u->tu_max / 4 : rl_throw_tu(u, item);
    if (u->tu < cost) {
        msg(COL_BAD, "Not enough time units");
        return 0;
    }
    curv = vox_throw(s, item, tx, ty, tz);
    if (curv <= 0) {
        msg(COL_BAD, curv ? "Too far to throw" : "Unable to throw here");
        if (!curv)
            log_printf("T%d: %s at %d,%d,%d cannot throw at %d,%d,%d", bt_turn, u->name, u->x,
                       u->y, u->z, tx, ty, tz);
        return 0;
    }
    u->tu -= cost;
    u->n_grenades--;
    turn_unit(s, &u->dir, dir_towards(u->x, u->y, tx, ty));
    acc = u->throwing * unit_acc_mod(u, NO_ITEM) / 100;
    if (u->panicked == PANIC_BERSERK)
        acc /= 2;
    vox_throw_land(s, tx, ty, tz, curv, acc, c);
    x = c[0];
    y = c[1];
    z = c[2];
    /* It drops to the first floor below. */
    while (z > 0 && !tile_standable(x, y, z, &h))
        z--;
    if (RL_WOUNDABLE(u))
        u->exp_throw++;
    if (!u->n_grenades)
        u->grenade = NO_ITEM;
    msg(COL_INFO, "Grenade away!");
    log_printf("T%d: %s throws at %d,%d,%d, lands %d,%d,%d", bt_turn, u->name, tx, ty, tz, x, y, z);
    fuse = u->fuse ? u->fuse - 1 : 0;
    u->fuse = 0;
    if (!fx_throw(s, item, x, y, z, fuse))
        explode(x, y, z, g_items[item].power, rl_item_dtype(item),
                rl_blast_radius(item, g_items[item].power), s);
    if (u->alive && !bt_result)
        reaction_fire(s);
    return 1;
}

static int adjacent(const Unit *a, const Unit *b);
static int melee_attack(int a, int t);

static int melee_tu(const Unit *u)
{
    (void)u;
    return 15;                  /* tuMelee 15, flat rate (items.rul) */
}

/* What unit e could do to the mover as a reaction (TileEngine::
   determineReactionType): 2 melee (next to it, more TU than the strike
   costs), 1 a snap shot (more TU than it costs, ammunition, a line of
   fire past its comrades), 0 nothing. It must see the mover: in its view
   cone or, in the hit state, all round (getSpottingUnits: checkViewSector
   or gotHit). */
static int reaction_type(int e, int mover)
{
    Unit *en = &g_units[e];
    const Unit *m = &g_units[mover];
    int snap, d = en->dir;

    if (!en->alive || en->stun >= en->hp || !LOS_NEAR(en, m))
        return 0;
    if (en->psi_state & PSI_HIT)
        en->dir = (unsigned char)dir_towards(en->x, en->y, m->x, m->y);
    snap = los_unit_sees(e, mover);
    en->dir = (unsigned char)d;
    if (!snap)
        return 0;
    if (en->melee && en->weapon == NO_ITEM)
        return adjacent(en, m) && en->tu > melee_tu(en)
               && (en->big || dir_towards(en->x, en->y, m->x, m->y) == en->dir) ? 2 : 0;
    snap = unit_shot_tu(en, SHOT_SNAP);
    if (!snap || en->tu <= snap || en->weapon == IT_STUNROD
        || (g_items[en->weapon].clip_size && !en->ammo))
        return 0;
    return line_of_fire(e, mover);
}

/* The hand a soldier reacts with (BattleUnit::getMainHandWeapon(false)):
   the left hand's loaded gun when the right holds none, or when both do
   and the left is the active hand; it is mirrored into the right hand
   (inv_mirror; 1 if so). */
static int react_hand(int e)
{
    const Unit *en = &g_units[e];
    int n, l;

    if (!UNIT_SOLDIER(en) || (!(en->special & SP_LEFT) && en->weapon != NO_ITEM
                              && g_items[en->weapon].tu_snap
                              && (en->ammo || !g_items[en->weapon].clip_size)))
        return 0;
    l = inv_hand(en, 1, &n);
    if (l >= N_ITEMS || g_items[l].kind != IK_WEAPON || !g_items[l].tu_snap
        || (!n && g_items[l].clip_size))
        return 0;
    return inv_mirror(e);
}

/* Reaction fire (TileEngine::checkReactionFire) after a unit of the side
   whose turn it is moved, turned, knelt, fired or threw: the enemies that
   could react line up by reaction score (reactions x TU left / max TU,
   rl_reaction_score); while the best beats the mover's own score it
   strikes or fires a snap shot (it may go again, its score now lower).
   Civilians never react, nothing reacts in the civilians' turn or to a
   panicking unit. Returns 1 if anyone did. */
int reaction_fire(int mover)
{
    Unit *m = &g_units[mover];
    unsigned char skip[MAX_UNITS];
    int fired = 0, guard, e;

    if (!m->alive || m->faction != side || side == FACTION_CIVILIAN || panicking)
        return 0;
    memset(skip, 0, sizeof skip);
    for (guard = 0; guard < 24 && m->alive && !bt_result; guard++) {
        int best = -1, bs = -1, type = 0;
        for (e = 0; e < g_nunits; e++) {
            const Unit *en = &g_units[e];
            int t, sc, h;
            if (skip[e] || en->faction == m->faction || en->faction == FACTION_CIVILIAN
                || !en->alive)
                continue;
            sc = rl_reaction_score(en);
            if (sc <= bs)
                continue;
            h = react_hand(e);
            t = reaction_type(e, mover);
            if (h)
                inv_mirror(e);
            if (!t)
                continue;
            best = e;
            bs = sc;
            type = t;
        }
        if (best < 0 || (long)m->reactions * m->tu > (long)bs * m->tu_max)
            break;
        {
            Unit *en = &g_units[best];
            if (RL_WOUNDABLE(en))
                en->reacted++;          /* reaction experience */
            /* An alien firing at X-COM gives itself away. */
            if (en->faction == FACTION_ALIEN && !en->seen && m->faction == FACTION_XCOM) {
                en->seen = 1;
                redraw_unit(best);
            }
            if (unit_visible(en) || unit_visible(m))
                msg(COL_BAD, "Reaction fire!");
            else
                log_msg(COL_BAD, "Reaction fire!");
            if (type == 2)
                type = melee_attack(best, mover);
            else {
                int h = react_hand(best);
                type = shoot(best, m->x, m->y, m->z, SHOT_SNAP);
                if (h && en->alive)
                    inv_mirror(best);
            }
            if (type)
                fired = 1;
            else
                skip[best] = 1;
        }
    }
    return fired;
}

/* ------------------------------------------------------------------ */
/* Movement */

/* X-COM's TU reserve (BattlescapeGame::checkReservedTU): an action of
   the given cost is refused if it would leave less than the reserved shot
   (auto falls back to snap, snap to aimed) while the shot is affordable. */
int reserve_ok(const Unit *u, int cost)
{
    int r = 0, mode;

    if (!bt_reserve || u->faction != FACTION_XCOM || side != FACTION_XCOM || panicking)
        return 1;
    mode = bt_reserve - 1;
    r = unit_shot_tu(u, mode);
    if (!r && mode == SHOT_AUTO)
        r = unit_shot_tu(u, mode = SHOT_SNAP);
    if (!r && mode == SHOT_SNAP)
        r = unit_shot_tu(u, SHOT_AIMED);
    if (r && cost + r > u->tu && r <= u->tu) {
        msg(COL_BAD, "Time units reserved");
        return 0;
    }
    return 1;
}

/* Walks a unit towards a tile along the searched path (path_search must
   have been run for it). Each step costs TU and half as much energy.
   Stops early when spotting aliens, under reaction fire, or short of TU
   or energy. */
void walk(int u, int tx, int ty, int tz)
{
    unsigned char dirs[96];
    int n = path_build(tx, ty, tz, dirs, sizeof dirs), i;
    Unit *un = &g_units[u];

    if (n && un->kneeling) {
        if (un->tu < 8) {
            if (un->faction == FACTION_XCOM)
                msg(COL_BAD, "Not enough time units");
            return;
        }
        un->tu -= 8;            /* stand up first */
        un->kneeling = 0;
    }

    if (!n) {
        /* (its own tile: nothing) */
        if (un->faction == FACTION_XCOM && !(tz == un->z && tx >= un->x && ty >= un->y
                                             && tx <= un->x + un->big && ty <= un->y + un->big))
            msg(COL_BAD, "Cannot get there");
        return;
    }
    if (un->faction == FACTION_XCOM) {
        /* DOS stops a soldier who spots an alien he has not seen yet
           (this turn there; here: in this walk), whoever else sees it. */
        memset(walk_saw, 0, sizeof walk_saw);
        walk_u = u;
        update_vision(u, 0);    /* notes those he sees now */
        walk_new = 0;
    }
    for (i = 0; i < n && un->alive && !bt_result; i++) {
        int dir = dirs[i], nz, c, ox = un->x, oy = un->y, oz = un->z, nx = ox, ny = oy;
        int was_shown = unit_visible(un), shown, od = un->dir;
        unsigned long t0 = timer_read();

        c = step_cost(ox, oy, oz, dir, &nz);
        if (c < 0)
            break;
        if (c + g_walk_reserve > un->tu) {
            /* A path longer than the TU: as far as they go
               (UnitWalkBState, STR_NOT_ENOUGH_TIME_UNITS). */
            if (un->faction == FACTION_XCOM)
                msg(COL_BAD, "Not enough time units");
            break;
        }
        if (dir < 8 && c / 2 > un->energy) {
            if (un->faction == FACTION_XCOM)
                msg(COL_BAD, "Not enough energy");
            break;
        }
        if (!reserve_ok(un, c))
            break;
        if (dir < 8) {
            /* Doors (UnitWalkBState: unitOpensDoor without rClick, bdoor.c):
               a UFO door charges its TU and the step is looked at again
               (the door crossed no longer costs anything). */
            if (door_try(un, dir, 0) == 9) {
                i--;
                continue;
            }
            nx += g_dir_dx[dir];
            ny += g_dir_dy[dir];
        }
        if (un->big ? !unit_free_at(u, nx, ny, nz) : UNIT_AT(nx, ny, nz) != 0)
            break;
        un->tu -= c;
        if (dir < 8) {
            un->energy -= (unsigned char)(c / 2);
            un->dir = un->turret = (unsigned char)dir;
        }
        unit_move_to(u, nx, ny, nz);
        if (un->faction == FACTION_ALIEN)
            un->moved = 1;
        /* walk slots 4, 2, 4, 2, ...; 4, 1, 2, 3, ... with the walk frames of
           upper or extended memory (floorld.c walk_load) */
        un->phase = (unsigned char)((((unsigned)i << (g_walk2 >> un->sprite_set & 1)) + 3 & 3) + 1);
        update_vision(u, 0);
        if (walk_new && !un->panicked) {    /* (a panic run goes on, as in DOS) */
            msg(COL_BAD, "Alien spotted!");
            n = i + 1;          /* stop after this step */
        }
        if (ai_step(u))
            n = i + 1;          /* an AI unit spotted someone: it thinks again */
        shown = unit_visible(un);
        if (was_shown && !shown)
            render_at(un, ox, oy, oz);
        if (shown) {
            bt_view_on();
            snd_step();
            if (was_shown)
                render_step(un->big, ox, oy, oz, nx, ny, nz);
            else
                render_at(un, nx, ny, nz);
            /* the view follows a unit leaving it (UnitWalkBState; not a
               civilian here: compromises.md) */
            if (un->faction != FACTION_CIVILIAN)
                render_follow(nx, ny, nz);
            /* X-COM / Alien Movement Speed: a step shown takes at least 8
               frames (16 on a diagonal), after a frame an eighth turned. */
            pace(t0, (dir < 8 ? eighths(od, dir) + 8 + (dir & 1) * 8 : 8) * move_ms(un), 'w');
        }
        if (g_fx_count && fx_step(u))
            n = i + 1;          /* burnt or blown up: stop after this step */
        if (reaction_fire(u))
            n = i + 1;          /* shot at: the walk stops */
    }
    un->phase = 0;
    walk_u = -1;
    walk_new = 0;
    if (un->alive && un->faction == FACTION_XCOM)
        update_vision(u, 1);
    if (un->alive && unit_visible(un))
        redraw_unit(u);
    flush_fog();
}

/* ------------------------------------------------------------------ */
/* 1 if the footprints of two units touch (melee range). */
static int adjacent(const Unit *a, const Unit *b)
{
    int as = a->big ? 2 : 1, bs = b->big ? 2 : 1;
    return a->z == b->z && b->x <= a->x + as && a->x <= b->x + bs
        && b->y <= a->y + as && a->y <= b->y + bs;
}

/* Claws, stings and fists (MeleeAttackBState): 15 TU, hit chance the
   melee skill x the health modifier, damage 0-200 % of the strength
   against the side facing the attacker. */
static int melee_attack(int a, int t)
{
    Unit *al = &g_units[a];
    Unit *v = &g_units[t];
    char text[48];

    if (!al->melee || al->tu < melee_tu(al) || !adjacent(al, v))
        return 0;
    al->tu -= melee_tu(al);
    turn_unit(a, &al->dir, dir_towards(al->x, al->y, v->x, v->y));
    if (SOLDIER_SEEN(v))
        bv_melee(t);
    sprintf(text, "%s attacks %s!", unit_title(al), unit_title(v));
    if (unit_visible(al) || unit_visible(v)) {
        snd_hit();
        msg(COL_BAD, text);
    } else
        log_msg(COL_BAD, text);
    if ((int)rnd(100) < unit_xstat(al)->melee * unit_acc_mod(al, NO_ITEM) / 100)
        hurt(t, a, roll_damage(al->melee, DT_MELEE), DT_MELEE,
             rl_side(v, al->x - v->x, al->y - v->y), 1);
    else if (unit_visible(al) || unit_visible(v))
        msg(COL_INFO, "Missed");
    return 1;
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Psionics (strengths and rolls in psibat.c) */

int battle_run(void)
{
    /* A saved battle (bsave.c restored it) goes on in the player's turn
       where it was saved: turn, selection, objectives, effects and the AI
       as they were. */
    int resume = g_bs_resume;

    if (!resume) {
        bt_sel = -1;
        bt_turn = 1;
        bt_reserve = 0;
        bt_vlast = -1;
        g_all_layers = 0;
        fx_reset();
    }
    fog_dirty = 0;
    g_xcom_range = g_night ? 9 : VIEW_RANGE;
    if (g_night)
        bu_night();             /* registers for the panel's colours */
    gfx_night(g_night);
    bt_result = 0;
    bt_request = 0;
    side = FACTION_XCOM;
    panicking = 0;
    log_loads("setup");
    if (!resume) {
        battle_start();         /* (bturn.c: objectives too) */
        ai_battle_start();
        if (g_roster)
            inv_screen(-1);     /* DOS's inventory before the battle (invscr.c) */
    }
    battle_view();              /* (reads g_bs_resume) */
    g_bs_resume = 0;

    /* Each side's units get their TU, energy and panic roll at the start
       of its own turn (SavedBattleGame::endTurn): aliens react to X-COM
       with what they left over from their turn. */
    while (!bt_result) {
        check_result();         /* nobody to fight (e.g. no alien could spawn) */
        bt_view_on();           /* the player's turn shows the map */
        if (bt_result)
            break;
        side = FACTION_XCOM;
        if (bt_turn > 1 && !resume)
            bt_side_start(FACTION_XCOM);    /* bturn.c: TU, bleeding, panic */
        resume = 0;
        /* The options window (Save, Load, Abandon) belongs to the player's
           turn; player_turn hands it to bsave.c, which shares the area
           with the core alone, and goes on afterwards. */
        while (!bt_result) {
            player_turn();
            if (!bt_request)
                break;
            if (bt_request == BR_INVENTORY)
                inv_screen(bt_sel);
            else
                bs_options();
            bt_request = 0;
        }
        if (bt_result == BT_LOAD || bt_result == BT_ABANDON)
            break;
        log_loads("player");
        psi_release_all();
        bt_side_end(FACTION_XCOM);  /* primed grenades, UFO doors */
        if (bt_result)
            break;
        side = FACTION_ALIEN;
        bt_side_start(FACTION_ALIEN);
        if (!bt_result)
            ai_turn(bt_turn);
        log_loads("alien");
        bt_side_end(FACTION_ALIEN); /* the aliens' grenades */
        side = FACTION_CIVILIAN;
        if (!bt_result && g_civ_total) {
            bt_side_start(FACTION_CIVILIAN);
            ai_civilians();
        }
        log_loads("end of round");
        if (!bt_result)
            bt_new_round();         /* smoke and fire, the next turn */
    }
    bt_view_on();
    ai_battle_end();
    if (bt_result == BT_LOAD || bt_result == BT_ABANDON) {
        /* Left for Load Game or Abandon Game: nothing of it counts. */
        panel_close();
        gfx_night(0);
        bu_day();
        g_xcom_range = VIEW_RANGE;
        return bt_result;
    }
    if (bt_result == -2 || bt_result == -1)
        abort_evacuate();
    if (bt_result == -2 && bt_obj_done)
        bt_result = 1;          /* the soldiers left behind are lost all the same */
    hud_status();
    g_civ_saved = bt_result == 1 ? count_alive(FACTION_CIVILIAN) : 0;
    battle_over(g_pak.objective && !bt_obj_left);
    panel_close();
    gfx_night(0);
    bu_day();
    g_xcom_range = VIEW_RANGE;
    return bt_result;
}

/* ------------------------------------------------------------------ */
/* Services for src/effects.c */

/* Devices (src/effects.c): grenades, proximity grenades and high
   explosive, all with a blast radius of 5 (items.rul). */
void bt_explode(int x, int y, int z, int power, int thrower)
{
    explode(x, y, z, power, DT_HE, 5, thrower);
}

void bt_burn(int u, int dmg)
{
    Unit *v = &g_units[u];
    char text[48];

    if (!v->alive || dmg <= 0)
        return;
    if (dmg >= v->hp) {
        kill_unit(u, u, 0);
        return;
    }
    v->hp -= dmg;
    unit_fmt(text, "%s burns (%d)", v, dmg);
    if (unit_visible(v))
        msg(v->faction == FACTION_ALIEN ? COL_GOOD : COL_BAD, text);
    else
        log_msg(COL_INFO, text);
}

void bt_vision(int u)
{
    int i;

    if (u >= 0 && g_units[u].faction == FACTION_XCOM) {
        update_vision(u, 1);
        flush_fog();
    } else
        for (i = 0; i < g_nunits; i++)
            if (g_units[i].alive && g_units[i].faction == FACTION_XCOM) {
                update_vision(i, 0);
                break;
            }
    hud_status();
}

/* ------------------------------------------------------------------ */
/* Services for src/ai.c */

int bt_over(void)
{
    if (!bt_result)
        check_result();
    return bt_result;
}

void bt_say(int shown, int colour, const char *s)
{
    if (shown)
        msg(colour, s);
    else
        log_msg(colour, s);
}

/* The hit state ends when the player selects a unit and before every
   move of the AI (SavedBattleGame::resetUnitHitStates). */
void bt_hit_reset(void)
{
    int i;

    for (i = 0; i < g_nunits; i++)
        g_units[i].psi_state &= ~PSI_HIT;
}

void bt_walk(int u, int x, int y, int z)
{
    bt_hit_reset();
    walk(u, x, y, z);
}

int bt_shoot(int u, int x, int y, int z, int mode)
{
    bt_hit_reset();
    return shoot(u, x, y, z, mode);
}

int bt_throw_ok(int u, int x, int y, int z)
{
    const Unit *un = &g_units[u];

    return un->grenade != NO_ITEM && un->n_grenades && un->tu >= rl_throw_tu(un, un->grenade)
        && vox_throw(u, un->grenade, x, y, z) > 0;
}

int bt_throw(int u, int x, int y, int z)
{
    /* The engine's throw cost (rl_throw_tu) already holds the pick-up and
       the priming: the AI pays nothing on top; nothing if it cannot throw. */
    bt_hit_reset();
    return throw_grenade(u, x, y, z);
}

int bt_strike(int u, int t)
{
    bt_hit_reset();
    return melee_attack(u, t);
}

int bt_line_of_fire(int s, int t)
{
    return line_of_fire(s, t);
}

void bt_redraw(int u)
{
    if (unit_visible(&g_units[u]))
        redraw_unit(u);
}

void bt_face(int u, int dir)
{
    turn_unit(u, &g_units[u].dir, dir);
}

void bt_flush(void)
{
    flush_fog();
    hud_status();
}
