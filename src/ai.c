/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The battle AI (see ai.h). Decisions follow OpenXcom's AIModule, which
   reproduces the DOS game's behaviour: one path search per spot covers
   the unit's time units (findReachable), walks follow an A* path to the
   goal (Pathfinding::calculate), escape, ambush and fire point candidates
   are judged by the lines of fire DOS uses (getSpottingUnits,
   canTargetUnit). What an 8088 cannot afford is cut down unless g_exact
   (EXACTAI): a line of fire is a tile sight line, then voxel lines to the
   middle of the body only, and the candidates that need lines are counted
   (EVALS). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "ai_in.h"
#include "battle.h"
#include "effects.h"
#include "geo.h"
#include "gfx.h"
#include "input.h"
#include "los.h"
#include "map.h"
#include "menu.h"
#include "pak.h"
#include "path.h"
#include "psi.h"
#include "render.h"
#include "rules.h"
#include "timer.h"
#include "uik.h"
#include "unit.h"
#include "vox.h"

#define COL_BAD     12

/* Modes; also the index of each mode's destination in wx/wy/wz/wd. */
#define AI_PATROL   0
#define AI_AMBUSH   1
#define AI_COMBAT   2
#define AI_ESCAPE   3

#define AF_DONE     1       /* no more actions this turn */
#define AF_PSI      2       /* the last action was a psi attack */
#define AF_STAY     4       /* waits in ambush where it stands */

/* What a think decides (OpenXcom BA_*). */
#define A_NONE      0
#define A_RETHINK   1
#define A_WALK      2
#define A_SHOOT     3       /* act_mode: SHOT_* */
#define A_THROW     4
#define A_HIT       5
#define A_PANIC     6
#define A_CONTROL   7
#define A_LAUNCH    8       /* to bt_wp, wp_n waypoints */

/* How an action went, for the turn loop. */
#define R_OK        0
#define R_IDLE      1       /* nothing to do: next unit */
#define R_FREE      2       /* does not count (psi, walk to a fire point) */
#define R_FINAL     3       /* ends the unit's turn (escape, ambush) */
#define R_RETHINK   4

#define AI_REACH    255     /* most TU of one path search (its time grows with the square) */
#define NO_COST     0xFFFF
#define EVALS       150     /* candidate tiles checked for lines of fire (escape, fire point) */
#define AMB_MAX     24      /* route nodes an ambush considers */
#define SLACK       32      /* TU a walk's path may cost above the straight estimate */

static unsigned short spent[MAX_UNITS];    /* ms this turn (LOG) */
int g_walk_reserve;

/* #define AI_PROFILE */
#ifdef AI_PROFILE
/* Time by part (reach search, escape, ambush, fire point, walk A*, the walk,
   nearest_target, the spotters where U stands, setup_patrol, the thinks
   before their action), LOF checks and voxel lines, logged after each
   side's turn. */
static unsigned long pf[10], pfs[10];
static unsigned pf_lof, pf_vox, pf_n[10];
#define PF_S(k)     (pfs[k] = timer_read(), pf_n[k]++)
#define PF_E(k)     (pf[k] += timer_read() - pfs[k])
#else
#define PF_S(k)
#define PF_E(k)
#endif

#define INTEL(u)    (ai_unit[u].mind & 15)
#define AGGR(u)     (ai_unit[u].mind >> 4)

/* ------------------------------------------------------------------ */
/* The acting unit and what it found out this think */

static int turn_no, cheat;
static int me;
static Unit *U;
static int tfac;                /* whom it hunts: X-COM (aliens), aliens (civilians) */
static int known, visible, spotting, target, closest;
static int gun, armed, melee, nade;     /* OpenXcom _rifle, _rifle or _blaster, _melee, a grenade */
static int wp_key;              /* this think's ai_way: its answer + 1, 0 not asked */
int ai_searched;                /* path search done where U stands */
#define searched ai_searched
static int tried;               /* escape (1) / ambush (2) looked for where U stands */
static int fp;                  /* this think's fire point: -1 not looked for, 0 none, 1 found */
static int esc_tu, amb_tu, pat_ok, charge;
static int act, act_mode, act_unit, psi_act, psi_unit, psi_est, psi_gate;
#define FP_SLOT     4       /* this think's fire point */
unsigned char ai_wx[5], ai_wy[5], ai_wz[5];    /* destination by mode, fire point */
#define wx ai_wx
#define wy ai_wy
#define wz ai_wz
static signed char wd[5];                   /* facing there, -1 none */
static int sp_n;                /* known enemies: who might fire at a tile */
static unsigned char sp_u[MAX_UNITS];

static int distance(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0), r = dx > dy ? dx : dy, d2 = dx * dx + dy * dy;

    while (r * r < d2)
        r++;
    return r;                   /* ceil(sqrt()), as OpenXcom */
}

/* A goal one A* search may not get to (254 TU, SLACK above the estimate). */
static int beyond(int x, int y)
{
    int dx = abs(x - U->x), dy = abs(y - U->y);

    return (dx > dy ? 4 * dx + 2 * dy : 4 * dy + 2 * dx) + SLACK > 254;
}

static int udist(const Unit *t)
{
    return distance(U->x, U->y, t->x, t->y);
}

static int in_range(int x0, int y0, int x1, int y1)
{
    x1 -= x0;
    y1 -= y0;
    return x1 * x1 + y1 * y1 <= VIEW_RANGE * VIEW_RANGE;
}

/* TileEngine::visible: U sees t whichever way U faces: the tile sight
   line, then DOS's canTargetUnit from the eyes (los.c, vox.c vox_sees,
   kept by pair as for the units' own sight). */
static int sees(const Unit *t)
{
    busy_tick();                /* thinking: the busy pointer (input.h) */
    return in_range(U->x, U->y, t->x, t->y) && (g_exact ? los_smoke : los_clear)(U->x, U->y, U->z, t->x, t->y, t->z)
        && vox_sees(me, (int)(t - g_units));
}

/* TU to the tile (findReachable over the unit's TU), NO_COST if beyond;
   the caller only looks at tiles within need TU. The search goes only as
   far as that (its time grows with the square of the TU): a tile within
   it costs the same as in a search over all the TU, so the answers are
   DOS's. A later caller that looks further searches again. */
static unsigned reach_in(int x, int y, int z, int need)
{
    /* (a patrol walk only spends what it does not keep in reserve) */
    int tu = U->tu - g_walk_reserve;

    if (tu > AI_REACH)
        tu = AI_REACH;
    if (need > tu)
        need = tu;
    if (need < 0)
        need = 0;
    if (searched <= need) {     /* searched: the TU searched + 1, 0 none */
        PF_S(0);
        path_search(me, need);
        PF_E(0);
        searched = need + 1;
        busy_tick();
    }
    return path_cost(x, y, z);
}

#define reach(x, y, z)  reach_in(x, y, z, AI_REACH)

/* Where unit e looks from (getSightOriginVoxel), lower voxels lower. */
static void eyes(int e, int *o, int lower)
{
    vox_eyes(e, o);             /* the units' own sight origin (vox.c) */
    o[2] -= lower;
}

/* Where U would fire from at a tile (findFirePoint: 4 below its top). */
static void muzzle(int x, int y, int z, int *o)
{
    o[0] = x * 16 + 8;
    o[1] = y * 16 + 8;
    o[2] = z * 24 - vox_level(x, y, z) + vox_height(me) + unit_xstat(U)->floating - 4;
}

/* TileEngine::canTargetUnit from voxel o (unit excl not in the way) at
   unit pu, at its own tile or, with hyp, thought to stand at (x, y, z): a
   voxel line that hits it (or, thought there, gets to the end). With
   g_exact all of DOS's lines (up to 37, vox_target_at). Cut down for the
   8088: a tile sight line first (1 ms), then lines to the middle of the
   body and 4 and 8 voxels higher (about 6 ms each; all 37 take 150 ms and
   more when none is reached). */
static int lof_at(const int *o, int excl, int pu, int x, int y, int z, int hyp)
{
    const Unit *t = &g_units[pu];
    int v[3], i, r, top;

#ifdef AI_PROFILE
    pf_lof++;
#endif
    if (g_exact) {
        g_vox_excl = excl;
        return vox_target_at(o, pu, x, y, z, hyp);
    }
    if (!los_clear(o[0] >> 4, o[1] >> 4, o[2] / 24, x, y, z))
        return 0;
    v[0] = x * 16 + 8;
    v[1] = y * 16 + 8;
    v[2] = z * 24 - vox_level(x, y, z) + unit_xstat(t)->floating;
    top = v[2] + vox_height(pu);
    v[2] = (v[2] + top) / 2;
    g_vox_excl = excl;
    for (i = 0; i < 3 && v[2] <= top; i++, v[2] += 4) {
#ifdef AI_PROFILE
        pf_vox++;
#endif
        r = vox_line(o, v, 0);
        if (r == V_EMPTY ? hyp
            : r == V_UNIT && (unsigned)((g_vox_hit[0] >> 4) - t->x) <= t->big
              && (unsigned)((g_vox_hit[1] >> 4) - t->y) <= t->big
              && (unsigned)(g_vox_hit[2] / 24 - t->z) <= 1)
            return 1;
    }
    return 0;
}

static int melee_tu(void)
{
    return 15;                  /* tuMelee 15, flat (as battle.c's melee_attack) */
}

/* U's footprint at (x, y, z) touches t's (melee range). */
static int touching(int x, int y, int z, const Unit *t)
{
    int s = U->big + 1, ts = t->big + 1;
    return z == t->z && t->x <= x + s && x <= t->x + ts && t->y <= y + s && y <= t->y + ts;
}

/* TU the unit needs for its attack (reachableWithAttack); -1 unarmed. */
static int attack_tu(void)
{
    if (armed)
        return unit_shot_tu(U, unit_shot_tu(U, SHOT_SNAP) ? SHOT_SNAP : SHOT_AIMED);
    return melee ? melee_tu() : -1;
}

/* A wall part by the tile that does not block sight (a window): the way
   it faces, or -1 (TileEngine::faceWindow). */
static int face_window(int x, int y, int z)
{
    const unsigned char *t = TILE(x, y, z), *f = g_los_flags + TIDX(x, y, z);

    if (t[PART_NORTHWALL] && !(*f & LF_NORTH))
        return 0;
    if (x + 1 < (int)g_mx && t[4 + PART_WESTWALL] && !(f[1] & LF_WEST))
        return 2;
    if (y + 1 < (int)g_my && t[4 * g_mx + PART_NORTHWALL] && !(f[g_mx] & LF_NORTH))
        return 4;
    return t[PART_WESTWALL] && !(*f & LF_WEST) ? 6 : -1;
}

/* BattleUnit::checkViewSector: the tile lies in t's 90-degree view (two
   half planes a * dx + b * dy >= 0 per direction, y pointing north). */
static int in_view(const Unit *t, int x, int y)
{
    static const signed char k[8][4] = {
        { 1, 1, -1, 1 }, { 1, 0, 0, 1 }, { 1, 1, 1, -1 }, { 0, -1, 1, 0 },
        { -1, -1, 1, -1 }, { -1, 0, 0, -1 }, { -1, -1, -1, 1 }, { 0, 1, -1, 0 }
    };
    const signed char *h = k[t->dir & 7];
    int dx = x - t->x, dy = t->y - y;

    return h[0] * dx + h[1] * dy >= 0 && h[2] * dx + h[3] * dy >= 0;
}

static void set_dest(int m, int x, int y, int z, int dir)
{
    wx[m] = (unsigned char)x;
    wy[m] = (unsigned char)y;
    wz[m] = (unsigned char)z;
    wd[m] = (signed char)dir;
}

/* Shuffled walk over the 11x11 square around U: the k-th tile's offset. */
static int sq_start, sq_step;
static const unsigned char steps[4] = { 13, 23, 37, 53 };

static void sq_shuffle(void)
{
    sq_start = rnd(121);
    sq_step = steps[rnd(4)];
}

static int sq_tile(int k, int *x, int *y)
{
    k = (sq_start + k * sq_step) % 121;
    *x = U->x + k % 11 - 5;
    *y = U->y + k / 11 - 5;
    return IN_MAP(*x, *y, U->z);
}

/* ------------------------------------------------------------------ */
/* Knowledge and targets */

/* AIModule::validTarget: an enemy, and for aliens one they know about
   (seen within as many turns as their intelligence). */
int ai_valid(int i, int civs)
{
    const Unit *t = &g_units[i];

    if (!t->alive || !HOSTILE(t->faction, U->faction)
        || (U->faction == FACTION_ALIEN && ai_spotted[i] > INTEL(me)))
        return 0;
    return civs || t->faction == tfac;
}

#define valid ai_valid

/* The known enemies (X-COM for the aliens), who might fire at a tile. */
static void list_spotters(void)
{
    int i;

    sp_n = 0;
    for (i = 0; i < g_nunits; i++)
        if (valid(i, 0))
            sp_u[sp_n++] = (unsigned char)i;
}

/* AIModule::getSpottingUnits: how many of them within 20 tiles have a line
   of fire on U at the tile (from 2 below their eyes; counting stops at
   stop). */
static int spotters(int x, int y, int z, int stop)
{
    int k, n = 0, o[3], hyp = x != U->x || y != U->y || z != U->z;

    busy_tick();
    for (k = 0; k < sp_n && n < stop; k++)
        if (in_range(g_units[sp_u[k]].x, g_units[sp_u[k]].y, x, y)) {
            eyes(sp_u[k], o, 2);
            n += lof_at(o, sp_u[k], me, x, y, z, hyp);
        }
    return n;
}

/* setupEscape's score for being seen by sp of them (U by spotting now). */
static int exposure(int sp)
{
    return !spotting && !sp ? 0 : spotting <= sp ? -10 * (1 + sp - spotting) : 10 * (spotting - sp);
}

/* selectPointNearTarget: the cheapest tile next to t that U reaches within
   maxtu, as the combat destination. */
static int point_near(const Unit *t, int maxtu)
{
    int x, y;
    unsigned c, best = NO_COST;

    for (y = t->y - 1 - U->big; y <= t->y + 1 + t->big; y++)
        for (x = t->x - 1 - U->big; x <= t->x + 1 + t->big; x++)
            if ((c = reach_in(x, y, t->z, maxtu)) < best && (int)c <= maxtu
                && (x != t->x || y != t->y)) {
                best = c;
                set_dest(AI_COMBAT, x, y, t->z, -1);
            }
    return best != NO_COST;
}

/* nearest_target's answer, kept for the think (and its rethink: nothing
   moved in between): a think asks up to three times, the same question
   with the same answer. nt_key: (gun || !melee) + 1 of the call, 0 none. */
static int nt_key, nt_keep, nt_n, nt_target, nt_closest, nt_dest;
static unsigned char nt_w[3];
static signed char nt_wd;

/* AIModule::selectNearestTarget: the closest enemy in sight; returns how
   many are in sight (0 when none of them can be attacked). The units in
   sight are checked nearest first (lowest number among equals, as the
   loop over them did), so the first one that can be attacked is it. */
static int nearest_target(void)
{
    int i, n = 0, k, j, lof = gun || !melee;
    unsigned char su[MAX_UNITS], sd[MAX_UNITS];

    if (nt_key == lof + 1) {
        target = nt_target;
        closest = nt_closest;
        if (nt_dest)            /* point_near's destination, as it set it */
            set_dest(AI_COMBAT, nt_w[0], nt_w[1], nt_w[2], nt_wd);
        return nt_n;
    }
    PF_S(6);
    target = -1;
    closest = 100;
    for (i = 0; i < g_nunits; i++) {
        const Unit *t = &g_units[i];
        if (!valid(i, U->faction == FACTION_ALIEN) || !sees(t))
            continue;
        su[n] = (unsigned char)i;
        sd[n++] = (unsigned char)udist(t);
    }
    nt_dest = 0;
    for (;;) {
        for (j = -1, k = 0; k < n; k++)
            if (sd[k] < closest && (j < 0 || sd[k] < sd[j]))
                j = k;
        if (j < 0)
            break;
        /* A gun needs a line of fire past the unit's comrades
           (canTargetUnit), a claw a way there. */
        if (lof ? bt_line_of_fire(me, su[j]) : (nt_dest = point_near(&g_units[su[j]], U->tu))) {
            closest = sd[j];
            target = su[j];
            break;
        }
        sd[j] = 255;            /* checked */
    }
    nt_key = lof + 1;
    nt_target = target;
    nt_closest = closest;
    nt_n = target >= 0 ? n : 0;
    nt_w[0] = wx[AI_COMBAT];
    nt_w[1] = wy[AI_COMBAT];
    nt_w[2] = wz[AI_COMBAT];
    nt_wd = wd[AI_COMBAT];
    PF_E(6);
    return nt_n;
}

/* selectClosestKnownEnemy (no civilians); with rnd: selectRandomTarget. */
static int pick_target(int random)
{
    int i, d, best = -255;

    target = -1;
    for (i = 0; i < g_nunits; i++)
        if (valid(i, random && U->faction == FACTION_ALIEN)
            && (d = (random ? (int)rnd(21) : 0) - udist(&g_units[i])) > best) {
            best = d;
            target = i;
        }
    return target >= 0;
}

/* ------------------------------------------------------------------ */
/* Route nodes */

/* A node U may patrol to (SavedBattleGame::getPatrolNode). */
static int patrol_ok(unsigned n, int scout, unsigned from)
{
    const MapNode *c = &g_node[n];

    return c->x != 255 && c->x && c->y && (scout || (c->pf & 15) || NODE_RANK(c))
        && !(c->rank & (NODE_TAKEN | NODE_DANGER)) && (!(c->rank & NODE_FLY) || unit_flies(U))
        && !(c->rank & U->big << 4)
        && !(scout && n == from) && !(g_los_flags[TIDX(c->x, c->y, c->z)] & LF_FIRE)
        && unit_fits(me, c->x, c->y, c->z);
}

#define MAX_NODES   512     /* patrol_node's list (more: asked again) */

/* Scouts pick any node at random, the others the most desirable node
   linked to theirs (so the crew stays in the UFO, going from room to room). */
static unsigned patrol_node(int scout)
{
    unsigned from = ai_unit[me].from, i, c, n = 0, pref = NO_NODE;
    unsigned char ok[(MAX_NODES + 7) / 8];     /* scouts: the nodes that passed */

    if (from == NO_NODE)
        from = rnd(g_n_node);           /* lost: start from anywhere */
    memset(ok, 0, sizeof ok);
    for (i = 0; i < (scout ? g_n_node : 5); i++) {
        c = scout ? i : map_node_link(from, i);
        if (c == NO_NODE || (!scout && !c) || !patrol_ok(c, scout, from))
            continue;
        if (pref == NO_NODE || (g_node[pref].pf & 15) < (g_node[c].pf & 15))
            pref = c;
        if (c < MAX_NODES)
            ok[c >> 3] |= (unsigned char)(1 << (c & 7));
        n++;
    }
    if (!n)
        return U->big && !scout ? patrol_node(1) : NO_NODE;
    if (scout)                  /* the n-th of them at random (none changed meanwhile) */
        for (n = rnd(n), i = 0; i < g_n_node; i++)
            if ((i < MAX_NODES ? ok[i >> 3] & 1 << (i & 7) : patrol_ok(i, 1, from)) && !n--)
                return i;
    return pref;
}

/* Base defence (AIModule::setupPatrol): the smaller aliens with a gun that
   fires snap shots straight (no arcing, no blast, no stun) go for the
   base's machinery. At a target node (the RMP "reserved" 5) of a room
   with base objects left, the first one in the room's tiles (x, then y,
   level 1, not the last row and column) is shot at, a snap shot, every
   think it has the time units for: pat_ok 2, at wx/wy/wz[AI_PATROL].
   Elsewhere the closest free target node of such a room is the patrol
   goal (a->to). */
static int room_left(int x, int y)
{
    int m = y / 10 * 6 + x / 10;

    return g_xbase_now[m >> 3] >> (m & 7) & 1;
}

/* Does room m (10 x 10 tiles, 6 to a row) still hold a base object, any
   part on any level (map_base_modules)? */
static int room_scan(int m)
{
    unsigned x, y, z, p, x0 = m % 6 * 10, y0 = m / 6 * 10;
    const unsigned char *t;

    for (z = 0; z < g_mz; z++)
        for (y = y0; y < y0 + 10 && y < g_my; y++)
            for (x = x0; x < x0 + 10 && x < g_mx; x++)
                for (t = TILE(x, y, z), p = 0; p < 4; p++)
                    if (t[p] && g_pak.mcd[t[p]].special == SPECIAL_XBASE)
                        return 1;
    return 0;
}

/* g_xbase_now (during a base defence): the rooms with base objects left,
   found at the start of each alien turn among the ones the battle began
   with (OpenXcom counts them down as they break; the same, cheaper than a
   hook in the battle core, which has no room left in its overlay), and
   after each shot at them. */
static void rooms_set(int m, int left)
{
    if (left)
        g_xbase_now[m >> 3] |= (unsigned char)(1 << (m & 7));
    else
        g_xbase_now[m >> 3] &= (unsigned char)~(1 << (m & 7));
}

static void rooms_refresh(void)
{
    int m;

    for (m = 0; m < 36; m++)
        rooms_set(m, (g_xbase_had[m >> 3] >> (m & 7) & 1) && room_scan(m));
}

static void module_patrol(void)
{
    AiUnit *a = &ai_unit[me];
    const MapNode *n;
    const unsigned char *t;
    unsigned i, found = NO_NODE;
    int x, y, dx, dy, dz, d, best = 0;

    if (!g_base_layout || U->big || !gun || U->weapon == NO_ITEM || !g_items[U->weapon].acc_snap
        || fx_round_blast(U) == BLAST_HE || fx_round_blast(U) == BLAST_STUN)
        return;
    if (a->from != NO_NODE && g_node[a->from].target
        && room_left(g_node[a->from].x, g_node[a->from].y)) {
        for (x = U->x / 10 * 10; x < U->x / 10 * 10 + 9 && g_mz > 1; x++)
            for (y = U->y / 10 * 10; y < U->y / 10 * 10 + 9; y++) {
                t = TILE(x, y, 1);
                if (t[PART_OBJECT] && g_pak.mcd[t[PART_OBJECT]].special == SPECIAL_XBASE) {
                    set_dest(AI_PATROL, x, y, 1, -1);
                    pat_ok = 2;
                    return;
                }
            }
        return;
    }
    for (i = 0, n = g_node; i < g_n_node; i++, n++) {
        if (n->x == 255 || !n->target || (n->rank & NODE_TAKEN) || !room_left(n->x, n->y))
            continue;
        d = UNIT_AT(n->x, n->y, n->z);
        if (d && g_units[d - 1].faction == U->faction)
            continue;
        dx = n->x - U->x;
        dy = n->y - U->y;
        dz = n->z - U->z;
        d = dx * dx + dy * dy + dz * dz;
        if (found == NO_NODE || (d < best && i != a->from)) {
            found = i;
            best = d;
        }
    }
    a->to = (unsigned short)found;
}

static void setup_patrol(void)
{
    AiUnit *a = &ai_unit[me];
    unsigned i, d, best = 0xFFFF;
    int tries, scout;

    pat_ok = 0;
    if (!g_n_node)
        return;
    if (a->to != NO_NODE && g_node[a->to].x == U->x && g_node[a->to].y == U->y
        && g_node[a->to].z == U->z) {
        a->from = a->to;            /* there: on to the next node */
        ai_free_node(a->to);
        a->to = NO_NODE;
        if ((tries = face_window(U->x, U->y, U->z)) >= 0)
            bt_face(me, tries);     /* a peek through the window */
    }
    if (a->from == NO_NODE)         /* the closest node on its level */
        for (i = 0; i < g_n_node; i++) {
            const MapNode *n = &g_node[i];
            int dx = n->x - U->x, dy = n->y - U->y;
            if (n->x == 255 || n->z != U->z || (n->rank & U->big << 4) || abs(dx) > 60
                || abs(dy) > 60 || (d = dx * dx + dy * dy) >= best)
                continue;
            best = d;
            a->from = i;
        }
    for (tries = 5; a->to == NO_NODE && tries; tries--) {
        /* Outside the UFO (rank 0 nodes), in fire, at a base or after
           turn 20 everyone scouts. */
        scout = cheat || g_base_layout || a->from == NO_NODE || !NODE_RANK(&g_node[a->from])
             || (g_los_flags[TIDX(U->x, U->y, U->z)] & LF_FIRE);
        module_patrol();
        if (pat_ok)
            return;
        if (a->to == NO_NODE && (a->to = patrol_node(scout)) == NO_NODE)
            a->to = patrol_node(!scout);
    }
    if (a->to != NO_NODE) {
        g_node[a->to].rank |= NODE_TAKEN;
        set_dest(AI_PATROL, g_node[a->to].x, g_node[a->to].y, g_node[a->to].z, -1);
        pat_ok = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Escape and ambush */

/* AIModule::setupEscape: a reachable tile out of sight, from the 11x11
   square (in random order), then further away at random. */
static void setup_escape(void)
{
    int tries, best = -32000, score, d0 = 0, x, y, z, sp, stop, evals = 0;
    unsigned c;

    PF_S(1);
    nearest_target();
    esc_tu = 0;
    tried |= 1;
    sq_shuffle();
    if (target >= 0)
        d0 = udist(&g_units[target]);
    for (tries = -1; tries < 150 && evals < EVALS; tries++) {
        z = U->z;
        x = U->x;               /* tries < 0: stay put */
        y = U->y;
        score = 0;
        if (tries >= 121) {     /* desperate: anywhere */
            score = 110;
            x += (int)rnd(21) - 10;
            y += (int)rnd(21) - 10;
            z += (int)rnd(3) - 1;
            if ((unsigned)z >= g_mz)
                z = U->z;
        } else if (tries >= 0) {
            score = 100;
            sq_tile(tries, &x, &y);
            if (x == U->x && y == U->y) {
                if (spotting) {
                    x += (int)rnd(41) - 20;
                    y += (int)rnd(41) - 20;
                } else
                    score += 15;
            }
        }
        if (!IN_MAP(x, y, z) || (c = reach(x, y, z)) == NO_COST)
            continue;
        if (target >= 0)        /* (OpenXcom counts any change as a gain) */
            score += 10 * abs(distance(g_units[target].x, g_units[target].y, x, y) - d0);
        if (g_los_flags[TIDX(x, y, z)] & LF_FIRE)
            score -= 40;
        if (score + exposure(0) <= best)
            continue;           /* cannot win, whoever sees it */
        evals++;
        /* Count the spotters only until the tile cannot win. */
        for (stop = 1; stop <= sp_n && score + exposure(stop) > best; stop++)
            ;
        if ((sp = spotters(x, y, z, stop)) >= stop)
            continue;
        score += exposure(sp);
        if (score > best) {
            best = score;
            set_dest(AI_ESCAPE, x, y, z, -1);
            esc_tu = c ? c : 1;
            if (best > 100)
                break;
        }
    }
    PF_E(1);
}

#ifdef AMB_PATH
/* setupAmbush's facing: walk the enemy t along its path to the ambush at
   (x, y, z) (the last search) and face the first tile where U would have
   a line of fire on him; else his way. */
static int ambush_facing(int x, int y, int z, int t)
{
    unsigned char dirs[64];
    int n = path_build(x, y, z, dirs, sizeof dirs), i, o[3], px = g_units[t].x,
        py = g_units[t].y, pz = g_units[t].z, nz;

    muzzle(x, y, z, o);
    for (i = 0; i < n && step_cost(px, py, pz, dirs[i], &nz) >= 0; i++) {
        path_apply(dirs[i], &px, &py, &pz, nz);
        if (lof_at(o, me, t, px, py, pz, 1))
            return dir_towards(x, y, px, py);
    }
    return dir_towards(x, y, g_units[t].x, g_units[t].y);
}
#endif

/* Whether think() will call evaluate_mode (and with known enemies its
   setup_escape, which looks at tiles over all the TU), as far as that is
   known before the ambush: in patrol and escape mode it does, in combat
   mode mostly, in ambush mode with an enemy in sight or no gun (or no
   ambush found), and when hurt, cheating or seen by three. */
static int mode_check_likely(void)
{
    return ai_unit[me].mode != AI_AMBUSH || visible || !gun || spotting > 2
        || U->hp < 2 * U->hp_max / 3 || cheat;
}

/* AIModule::setupAmbush: a route node near U that the closest known enemy
   cannot fire at, nor anyone else, and he can walk to (a window is a
   bonus), facing where he will come into its line of fire. */
static void setup_ambush(void)
{
    int best = 0, score, atk = attack_tu(), n = 0, k, t, o[3], x, y, z, ok, need;
    unsigned i, c;
    unsigned short nd[AMB_MAX];
    unsigned char tu[AMB_MAX];

    amb_tu = 0;
    tried |= 2;
    if (!pick_target(0) || atk < 0)
        return;
    t = target;
    PF_S(2);
    /* Its nodes cost at most U->tu - atk: the search goes that far, or
       over all the TU when the think will evaluate the mode (as far as
       that is known now), whose escape looks that far. */
    need = U->tu - atk;
    if (mode_check_likely())
        need = AI_REACH;
    /* The nodes within reach first: the enemy's path search below
       replaces U's. */
    for (i = 0; i < g_n_node && n < AMB_MAX; i++) {
        const MapNode *m = &g_node[i];
        if (m->x == 255 || m->z != U->z || distance(m->x, m->y, U->x, U->y) > 10
            || (c = reach_in(m->x, m->y, m->z, need)) == NO_COST || (int)c > U->tu - atk)
            continue;
        nd[n] = (unsigned short)i;
        tu[n++] = (unsigned char)c;
    }
    eyes(t, o, 0);
    for (k = 0; k < n; k++) {
        const MapNode *m = &g_node[nd[k]];
        score = 100 - tu[k] + (face_window(m->x, m->y, m->z) >= 0 ? 25 : 0);
        if (score <= best
            || lof_at(o, t, me, m->x, m->y, m->z, m->x != U->x || m->y != U->y)
            || spotters(m->x, m->y, m->z, 1))
            continue;
#ifdef AMB_PATH
        /* DOS: the enemy can walk there, facing where he comes into the
           line of fire. Off: an A* search for X-COM that finds no way
           (a node inside the UFO) took seconds a try on the 8088. */
        searched = 0;
        U->alive = U->seen;     /* (X-COM's paths go through aliens it has not seen) */
        ok = path_search_to(t, m->x, m->y, m->z, SLACK, &x, &y, &z);
        U->alive = 1;
        if (!ok)
            continue;
        set_dest(AI_AMBUSH, m->x, m->y, m->z, ambush_facing(m->x, m->y, m->z, t));
#else
        set_dest(AI_AMBUSH, m->x, m->y, m->z, dir_towards(m->x, m->y, g_units[t].x, g_units[t].y));
#endif
        best = score;
        amb_tu = tu[k] ? tu[k] : 1;
        if (best > 80)
            break;
    }
    target = t;
    PF_E(2);
}

/* ------------------------------------------------------------------ */
/* Attacks */

/* AIModule::explosiveEfficacy: worth blowing up the tile? Grenades only
   for two enemies or more, unless desperate. */
int ai_efficacy(int tx, int ty, int tz, int radius, int grenade)
{
    int i, des, eff, hit = 0, tgt;

    if (turn_no < 3)
        return 0;
    des = (100 - U->morale) / 10 + (U->hp_max - U->hp > U->hp_max / 3 * 2 ? 3 : 0);
    eff = des + (g_difficulty > 0 ? g_difficulty / 2 : 0);
    if (U->z == tz && distance(U->x, U->y, tx, ty) <= radius)
        eff -= 4;               /* no kamikaze */
    if ((tgt = UNIT_AT(tx, ty, tz) - 1) >= 0) {
        hit++;
        eff++;
    }
    for (i = 0; i < g_nunits; i++) {
        const Unit *o = &g_units[i];
        if (!o->alive || i == me || i == tgt || o->z != tz
            || distance(o->x, o->y, tx, ty) > radius
            || (o->faction == tfac && ai_spotted[i] > INTEL(me))
            || !los_reach(tx, ty, tz, o->x, o->y, o->z))
            continue;
        if (o->faction == tfac) {
            hit++;
            eff++;
        } else if (o->faction == U->faction)
            eff -= 2;
    }
    if (grenade && des < 6 && hit < 2)
        return 0;
    return eff > 0 || hit >= 10;
}

/* getNodeOfBestEfficacy: a route node where a blast would catch more known
   enemies than friends. */
static int blast_node(int r)
{
    int best = 2, pts, j, d;
    unsigned i;

    if (turn_no < 3)
        return 0;
    for (i = 0; i < g_n_node; i++) {
        const MapNode *n = &g_node[i];
        if (n->x == 255 || (d = distance(n->x, n->y, U->x, U->y)) > 20 || d <= r)
            continue;
        for (pts = j = 0; j < g_nunits; j++) {
            const Unit *o = &g_units[j];
            if (o->alive && o->z == n->z && distance(o->x, o->y, n->x, n->y) < r)
                pts += !HOSTILE(o->faction, U->faction) ? -2 : ai_spotted[j] <= INTEL(me);
        }
        if (pts > best && los_clear(U->x, U->y, U->z, n->x, n->y, n->z)) {
            best = pts;
            set_dest(AI_COMBAT, n->x, n->y, n->z, -1);
        }
    }
    return best > 2;
}

static void grenade_action(const Unit *t)
{
    int r = g_items[U->grenade].power / 20;

    /* Pick up (4), prime (50 %) and throw (25 %): the engine's throw cost
       (rl_throw_tu), paid once by the throw itself. */
    if (U->tu < rl_throw_tu(U, U->grenade))
        return;
    if (ai_efficacy(t->x, t->y, t->z, r, 1))
        set_dest(AI_COMBAT, t->x, t->y, t->z, -1);
    else if (!blast_node(r))
        return;
    if (!bt_throw_ok(me, wx[AI_COMBAT], wy[AI_COMBAT], wz[AI_COMBAT]))
        return;                 /* out of throwing range (strength, weight) */
    act = A_THROW;
    gun = melee = 0;
}

/* AIModule::selectFireMethod: auto close in, aimed far away, else snap. */
static void fire_method(int d)
{
    int tu = U->tu, m = -1, ta = unit_shot_tu(U, SHOT_AUTO), ts = unit_shot_tu(U, SHOT_SNAP);
    int tm = unit_shot_tu(U, SHOT_AIMED);

    ta = ta && tu >= ta;
    ts = ts && tu >= ts;
    tm = tm && tu >= tm;
    if (d < 4)
        m = ta ? SHOT_AUTO : ts ? SHOT_SNAP : tm ? SHOT_AIMED : -1;
    else {
        if (d > 12)
            m = tm ? SHOT_AIMED : d < 20 && ts ? SHOT_SNAP : -1;
        if (m < 0)
            m = ts ? SHOT_SNAP : tm ? SHOT_AIMED : ta ? SHOT_AUTO : -1;
    }
    if (m >= 0) {
        act = A_SHOOT;
        act_mode = m;
    }
}

static void projectile_action(const Unit *t)
{
    set_dest(AI_COMBAT, t->x, t->y, t->z, -1);
    act_unit = target;
    /* Explosive rounds: only where they pay. */
    if (fx_round_blast(U) == BLAST_NONE || ai_efficacy(t->x, t->y, t->z, fx_round_power(U) / 20, 0))
        fire_method(udist(t));
}

/* AIModule::meleeAction: strike a neighbour, or charge the closest known
   enemy within reach. */
static void melee_action(void)
{
    int cost = melee_tu(), i, d, maxd;

    if (U->tu < cost)
        return;
    if (target < 0 || !touching(U->x, U->y, U->z, &g_units[target])) {
        maxd = (U->tu - cost) / 4 + 1;
        target = -1;
        for (i = 0; i < g_nunits; i++) {
            const Unit *t = &g_units[i];
            if (!valid(i, U->faction == FACTION_ALIEN) || (d = udist(t)) > 20
                || (d >= maxd && d != 1) || (d != 1 && !point_near(t, U->tu - cost)))
                continue;
            target = charge = i;
            act = A_WALK;
            maxd = d;
        }
        if (target < 0 || !touching(U->x, U->y, U->z, &g_units[target]))
            return;
    }
    act = A_HIT;
    act_unit = target;
}

/* AIModule::psiAction: psionic aliens attack the known soldier they are
   likeliest to break, by panic or (a strong attack) mind control. */
static int psi_action(void)
{
    int i, c, best = 0, pick = -1, odds = 40, brav, gate;
    const Unit *t;

    if (!U->psi || (U->psi_state & PSI_ALIEN) || U->tu <= esc_tu + PSI_AMP_TU
        || (ai_unit[me].flags & AF_PSI))
        return 0;
    for (i = 0; i < g_nunits; i++) {
        t = &g_units[i];
        if (t->big || !valid(i, 0) || (t->psi_state & PSI_CTRL))
            continue;
        c = psi_strength(U) * psi_skill(U) / 50 - psi_skill(t) * 4 / 10 - udist(t)
          - psi_strength(t) + 55 + (int)rnd(51);
        if (c > best) {
            best = c;
            pick = i;
        }
    }
    if (pick < 0)
        return 0;
    target = pick;
    /* The threshold: the gun's power when an enemy is in sight, else a
       35-155 roll; the estimate must beat it. */
    gate = visible && armed ? fx_round_power(U) : 35 + (int)rnd(121);
    if (gate >= best)
        return 0;               /* the gun will do, or not this time */
    psi_est = best;
    psi_gate = gate;
    psi_unit = pick;
    psi_act = A_PANIC;
    if (best >= 30) {
        t = &g_units[pick];
        brav = (110 - t->bravery) / 10;
        odds += brav > 6 ? -15 : brav < 4 ? 15 : 0;
        if (t->morale < 40)
            odds += 15;
        else if (t->morale - 10 * brav < 50)
            odds -= 15;
        if (!t->morale || (int)rnd(100) < odds)
            psi_act = A_CONTROL;
    }
    return 1;
}

/* AIModule::findFirePoint: a tile near U with a line of fire to the
   closest known enemy, as unseen as possible. */
static int find_fire_point(void)
{
    int i, x, y, best = 0, score, evals = 0, atk = attack_tu(), o[3], sp, stop;
    unsigned c;
    const Unit *t;

    if (!pick_target(0) || atk < 0)
        return 0;
    if (fp < 0) {               /* once per think */
        fp = 0;
        t = &g_units[target];
        PF_S(3);
        sq_shuffle();
        for (i = 0; i < 121 && evals < EVALS; i++) {
            if (!sq_tile(i, &x, &y) || (c = reach_in(x, y, U->z, U->tu - atk)) == NO_COST
                || (int)c > U->tu - atk
                || (score = 100 + U->tu - c + (in_view(t, x, y) ? 0 : 10)) <= best)
                continue;
            evals++;
            muzzle(x, y, U->z, o);
            if (!lof_at(o, me, target, t->x, t->y, t->z, 0))
                continue;
            /* each one who could fire at it costs 10 */
            stop = (score - best + 9) / 10;
            if ((sp = spotters(x, y, U->z, stop)) >= stop)
                continue;
            best = score - 10 * sp;
            set_dest(FP_SLOT, x, y, U->z, dir_towards(x, y, t->x, t->y));
            if (best > 125)
                break;
        }
        PF_E(3);
        fp = best > 70;
    }
    act = A_RETHINK;
    if (!fp)
        return 0;
    act = A_WALK;
    set_dest(AI_COMBAT, wx[FP_SLOT], wy[FP_SLOT], wz[FP_SLOT], wd[FP_SLOT]);
    return 1;
}

static void setup_attack(void)
{
    act = A_RETHINK;
    psi_act = A_NONE;
    if (known) {
        if (psi_action())
            return;
        /* AIModule::wayPointAction (aiway.c), once a think: nothing moved
           on a rethink. */
        if (armed && !gun) {
            if (!wp_key)
                wp_key = ai_way(me, 0) + 1;
            if (wp_key > 1)
                act = A_LAUNCH;
        }
    }
    if (nearest_target()) {
        if (nade)
            grenade_action(&g_units[target]);
        if (melee)
            melee_action();
        if (gun)
            projectile_action(&g_units[target]);
    }
    if (act == A_RETHINK && (spotting || AGGR(me) < (int)rnd(4))) {
        /* The fire point only counts in combat (execute, and the think's
           eval when it is in combat already); when evaluate_mode picks
           combat, it looks for it there. Not looked for in vain here
           (about 0.2 s): the same choices. */
        if (ai_unit[me].mode == AI_COMBAT)
            find_fire_point();
        else
            pick_target(0);     /* the target it would have set */
    }
}

/* ------------------------------------------------------------------ */
/* Mode */

static int pc(int v, int p)
{
    return v * p / 100;
}

/* Odds o[mode] scaled by percentages for ambush, combat, escape. */
static void scale(int *o, const unsigned char *p)
{
    o[AI_AMBUSH] = pc(o[AI_AMBUSH], p[0]);
    o[AI_COMBAT] = pc(o[AI_COMBAT], p[1]);
    o[AI_ESCAPE] = pc(o[AI_ESCAPE], p[2]);
}

/* AIModule::evaluateAIMode: patrol, ambush, combat or escape by odds. */
static void evaluate_mode(void)
{
    static const unsigned char by_hp[3][3] = { { 75, 60, 170 }, { 80, 80, 140 }, { 100, 100, 110 } };
    static const unsigned char by_aggr[3][3] = { { 100, 70, 140 }, { 110, 100, 100 }, { 100, 140, 70 } };
    static const unsigned char order[4] = { AI_ESCAPE, AI_AMBUSH, AI_COMBAT, AI_PATROL };
    AiUnit *a = &ai_unit[me];
    int o[4], d, i = 0;

    if (charge >= 0 && act != A_RETHINK) {
        a->mode = AI_COMBAT;
        return;
    }
    o[AI_PATROL] = visible ? 15 : 30;
    o[AI_AMBUSH] = gun && amb_tu ? 12 : 0;
    o[AI_COMBAT] = !(gun && amb_tu) && melee ? 26 : 20;
    o[AI_ESCAPE] = U->faction == FACTION_ALIEN && (U->tu > U->tu_max / 2 || charge >= 0) ? 5
                 : melee ? 12 : 15;
    if (spotting) {
        o[AI_PATROL] = 0;
        if (!esc_tu && !(tried & 1))
            setup_escape();
    }
    if (known) {
        if (known == 1)
            o[AI_COMBAT] = pc(o[AI_COMBAT], 120);
        if (!esc_tu) {
            if (!pick_target(0))
                o[AI_ESCAPE] = 0;
            else if (!(tried & 1))
                setup_escape();
        }
    } else if (U->faction == FACTION_ALIEN)
        o[AI_COMBAT] = o[AI_ESCAPE] = 0;
    o[a->mode] = pc(o[a->mode], 110);
    if (a->mode == AI_COMBAT)
        o[AI_AMBUSH] = pc(o[AI_AMBUSH], 150);
    d = U->hp < U->hp_max / 3 ? 0 : U->hp < 2 * (U->hp_max / 3) ? 1 : U->hp < U->hp_max ? 2 : 3;
    if (d < 3)
        scale(o, by_hp[d]);
    scale(o, by_aggr[AGGR(me) > 2 ? 2 : AGGR(me)]);
    if (spotting) {
        o[AI_ESCAPE] = pc(10 * o[AI_ESCAPE], spotting + 10);
        o[AI_COMBAT] = pc(5 * o[AI_COMBAT], spotting + 20);
    } else
        o[AI_ESCAPE] /= 2;
    if (visible) {
        o[AI_COMBAT] = pc(10 * o[AI_COMBAT], visible + 10);
        if (closest < 5)
            o[AI_AMBUSH] = 0;
    }
    o[AI_AMBUSH] = amb_tu ? pc(o[AI_AMBUSH], 170) : 0;
    if (g_base_layout) {
        o[AI_ESCAPE] = pc(o[AI_ESCAPE], 75);
        o[AI_AMBUSH] = pc(o[AI_AMBUSH], 60);
    }
    if (!melee && !armed && !nade && !U->psi)
        o[AI_COMBAT] = o[AI_AMBUSH] = 0;
    d = o[0] + o[1] + o[2] + o[3];
    d = 1 + (int)rnd(d > 1 ? d : 1);
    for (a->mode = order[0]; d > o[a->mode] && a->mode != AI_PATROL; a->mode = order[++i])
        d -= o[a->mode];
    if ((U->faction == FACTION_ALIEN && cheat) || charge >= 0)
        a->mode = AI_COMBAT;
    /* Fall back when the chosen mode has nothing lined up. */
    if (a->mode == AI_COMBAT) {
        /* (a launch's target is its first waypoint) */
        if (act == A_LAUNCH ? ai_way_unit
            : act >= A_SHOOT && (act != A_THROW || UNIT_AT(wx[2], wy[2], wz[2])))
            return;
        if (pick_target(1) && find_fire_point())
            return;
        a->mode = AI_PATROL;
    }
    if (a->mode == AI_PATROL && a->to == NO_NODE && pat_ok != 2)
        a->mode = AI_AMBUSH;
    if (a->mode == AI_AMBUSH && !amb_tu)
        a->mode = AI_ESCAPE;
}

/* ------------------------------------------------------------------ */
/* Doing it */

/* Before a unit X-COM sees acts, the view goes to it (BattlescapeGame::
   handleAI centerOnPosition; here only when it is off the screen; its walk
   follows it). Returns the unit, for the call. */
static int look(void)
{
    if (U->seen && U->faction == FACTION_ALIEN)     /* (not a civilian) */
        render_follow(U->x, U->y, U->z);
    return me;
}

static void strike(int t)
{
    /* battle.c's melee_attack: 15 TU, faces the victim, says so, rolls the
       melee skill and the damage. */
    bt_strike(look(), t);
}

static void do_psi(void)
{
    Unit *t = &g_units[psi_unit];
    int control = psi_act == A_CONTROL;
    char text[48];

    U->tu -= PSI_AMP_TU;
    log_printf("T%d: psi %s by %s on %s (%d%%, estimate %d > %d)", turn_no,
               control ? "control" : "panic", U->name, t->name, psi_chance(U, t, control),
               psi_est, psi_gate);
    if (!psi_roll(U, t, control))
        return;
    if (!control) {             /* it shows when the soldier panics */
        t->morale = (unsigned char)(t->morale > 110 - t->bravery ? t->morale - (110 - t->bravery) : 0);
        return;
    }
    sprintf(text, "%s is under alien control!", t->name);
    bt_say(1, COL_BAD, text);
    t->faction = FACTION_ALIEN;
    t->psi_state |= PSI_ALIEN;
    t->tu = t->tu_max;
    t->panicked = 0;
    t->seen = 1;
    ai_unit_new(psi_unit);
    bt_redraw(psi_unit);
}

static int execute(void)
{
    AiUnit *a = &ai_unit[me];
    int m = a->mode, type, ox = U->x, oy = U->y, oz = U->z, i;
    char text[40];

    g_walk_reserve = 0;
    if (m != AI_COMBAT)
        charge = -1;
    if (m == AI_PATROL && pat_ok == 2) {
        /* Base defence: a snap shot at the machinery (setup_patrol). */
        log_printf("T%d: alien %d shoots at base machinery %d,%d,%d", turn_no, me, wx[0], wy[0],
                   wz[0]);
        if (!bt_shoot(look(), wx[0], wy[0], wz[0], SHOT_SNAP))
            return R_IDLE;
        i = wy[0] / 10 * 6 + wx[0] / 10;
        if (!room_scan(i)) {
            rooms_set(i, 0);
            log_printf("T%d: base room %d,%d has no machinery left", turn_no, wx[0] / 10 * 10,
                       wy[0] / 10 * 10);
        }
        return R_OK;
    }
    type = m == AI_COMBAT ? act : (m == AI_PATROL ? pat_ok : m == AI_AMBUSH ? amb_tu : esc_tu)
         ? A_WALK : A_RETHINK;
    if (m == AI_PATROL && armed)  /* reserve for reaction fire: 50 / 40 / 33 % */
        g_walk_reserve = AGGR(me) == 0 ? U->tu_max / 2 : AGGR(me) == 1 ? U->tu_max / 5 * 2
                       : U->tu_max / 3;
    if (type == A_WALK) {
        if (wx[m] == ox && wy[m] == oy && wz[m] == oz) {
            if (m == AI_AMBUSH)
                a->flags |= AF_STAY;
            return R_IDLE;
        }
        esc_tu = amb_tu = 0;
        if (U->tu < g_walk_reserve + 4)
            return R_IDLE;      /* nothing left above the reserve */
        if (!searched || path_cost(wx[m], wy[m], wz[m]) == NO_COST) {
            /* Beyond this turn's reach: the path there (A*). */
            int x, y, z, k = 0;
            searched = 0;
#ifdef ASTAR_CHECK
            {
                unsigned long t0 = timer_read();
                int f = path_search_to(me, wx[m], wy[m], wz[m], SLACK, &x, &y, &z);
                unsigned ca = path_cost(x, y, z), cd, ms = (unsigned)timer_ms(timer_read() - t0);
                t0 = timer_read();
                path_search(me, 254);
                cd = path_cost(x, y, z);
                log_printf("T%d: astar %d %d,%d,%d -> %d,%d,%d to %d,%d,%d found %d cost %u dijkstra %u%s, %u / %lu ms",
                           turn_no, me, U->x, U->y, U->z, wx[m], wy[m], wz[m], x, y, z, f, ca, cd,
                           ca != cd ? " MISMATCH" : "", ms, timer_ms(timer_read() - t0));
            }
#endif
            PF_S(4);
            if (!path_search_to(me, wx[m], wy[m], wz[m], SLACK, &x, &y, &z)) {
                if (m == AI_PATROL && !beyond(wx[m], wy[m])) {
                    /* No way to the node (setupPatrol): another one next
                       time (a failing search here costs up to 0.5 s; DOS
                       tries 5 nodes at once). */
                    ai_free_node(a->to);
                    a->to = NO_NODE;
                }
                set_dest(m, x, y, z, wd[m]);    /* toward it */
            }
            (void)k;
            PF_E(4);
        }
        if (wx[m] != ox || wy[m] != oy || wz[m] != oz) {
            /* Escape and a charge go on whatever they see on the way. */
            ai_no_stop = m == AI_ESCAPE || charge >= 0;
            for (i = 0; i < g_nunits; i++)
                ai_watch[i] = (unsigned char)(g_units[i].alive
                                           && HOSTILE(g_units[i].faction, U->faction)
                                           && los_unit_sees(me, i));
            ai_walker = me;
            PF_S(5);
            bt_walk(look(), wx[m], wy[m], wz[m]);
            PF_E(5);
            ai_walker = -1;
            g_walk_reserve = 0;
            searched = tried = 0;
        }
        if (U->x == ox && U->y == oy && U->z == oz) {
            if (m == AI_PATROL) {
                ai_free_node(a->to);       /* no way there: another node next time */
                a->to = NO_NODE;
            }
            return R_IDLE;
        }
        if (!U->alive)
            return R_FINAL;
        if (charge >= 0 && g_units[charge].alive && U->tu >= melee_tu()
            && touching(U->x, U->y, U->z, &g_units[charge]))
            strike(charge);
        else if (m == AI_ESCAPE)
            bt_face(me, (U->dir + 4) & 7);  /* hidden: facing the way it came */
        else if (wd[m] >= 0)
            bt_face(me, wd[m]);
        return m == AI_ESCAPE || m == AI_AMBUSH ? R_FINAL
             : m == AI_COMBAT && gun && U->tu > unit_shot_tu(U, SHOT_SNAP) ? R_FREE : R_OK;
    }
    switch (type) {
    case A_RETHINK:
        return R_RETHINK;
    case A_LAUNCH:
        /* BA_LAUNCH: the aimed shot's TU, to its waypoints (bt_nwp, ai_wx..);
           nothing shows the launcher but X-COM's own sight lines */
        act_mode = ai_way(me, 1);
        /* fall through */
    case A_SHOOT:
        i = bt_shoot(look(), wx[2], wy[2], wz[2], act_mode);
        bt_nwp = 0;
        return i ? R_OK : R_IDLE;
    case A_THROW:
        if (!bt_throw_ok(me, wx[2], wy[2], wz[2]))
            return R_IDLE;
        sprintf(text, "%s throws a grenade!", unit_title(U));
        bt_say(1, COL_BAD, text);
        if (!bt_throw(look(), wx[2], wy[2], wz[2]))
            return R_IDLE;
        if (U->seen) {
            /* Its flight, not drawn: a tile's time at the Fire Speed over
               the longer way across (ProjectileFlyBState). */
            ox = abs(wx[2] - ox);
            oy = abs(wy[2] - oy);
            timer_pace(timer_read(), (ox > oy ? ox : oy) * g_tile_ms[g_bopt[BO_FIRE]], 'g');
        }
        return R_OK;
    case A_HIT:
        strike(act_unit);
        return R_OK;
    }
    return R_IDLE;
}

/* One decision (AIModule::think) and its action. */
static int think(void)
{
    AiUnit *a = &ai_unit[me];
    int eval, i;

    act_unit = -1;
    fp = -1;
    if (!nt_keep)
        nt_key = wp_key = 0;    /* a new think: ask nearest_target again */
    nt_keep = 0;
    PF_S(9);
    /* BattleUnit::think: checkAmmo reloads an empty weapon (15 TU). */
    if (U->weapon != NO_ITEM && !U->ammo && U->clips && U->tu >= 15
        && g_items[U->weapon].clip_size) {
        U->ammo = g_items[U->weapon].clip_size;
        U->clips--;
        U->load = 0;
        U->tu -= 15;
    }
    gun = U->weapon != NO_ITEM && U->weapon != IT_STUNROD
       && (!g_items[U->weapon].clip_size || U->ammo)
       && (unit_shot_tu(U, SHOT_SNAP) || unit_shot_tu(U, SHOT_AIMED));
    /* AIModule::think: a waypoint weapon is no rifle. */
    armed = gun;
    if (U->weapon == IT_BLASTER)
        gun = 0;
    melee = U->melee != 0;
    nade = U->grenade != NO_ITEM && U->n_grenades;
    /* Tests (BOOMAIM, voxx.c): an alien's own shot sets off explosive terrain:
       bt_boom_run in bturn's section with the AI's frames on the stack. */
    if ((g_debug & 2) && gun && U->faction == FACTION_ALIEN && ai_boom_aim(me)
        && bt_shoot(me, wx[2], wy[2], U->z, SHOT_SNAP))
        return R_OK;
    for (known = i = 0; i < g_nunits; i++)
        known += U->faction == FACTION_ALIEN && valid(i, 1);
    list_spotters();
    visible = nearest_target();
    PF_S(7);
    spotting = spotters(U->x, U->y, U->z, MAX_UNITS);
    PF_E(7);
    if (spotting && !esc_tu && !(tried & 1))
        setup_escape();
    if (known && !melee && !amb_tu && !(tried & 2))
        setup_ambush();
    setup_attack();
    PF_S(8);
    setup_patrol();
    PF_E(8);
    if (psi_act && !(a->flags & AF_PSI)) {
        a->flags |= AF_PSI;
        do_psi();
        return R_FREE;
    }
    a->flags &= ~AF_PSI;
    switch (a->mode) {
    case AI_PATROL: eval = spotting || visible || known || rnd(100) < 10; break;
    case AI_AMBUSH: eval = !gun || !amb_tu || visible; break;
    case AI_COMBAT: eval = act == A_RETHINK; break;
    default:        eval = !spotting || !known; break;
    }
    if (eval || spotting > 2 || U->hp < 2 * U->hp_max / 3
        || (target >= 0 && ai_spotted[target] > INTEL(me)) || (cheat && a->mode != AI_COMBAT))
        evaluate_mode();
    PF_E(9);
    return execute();
}

/* A unit's go: up to two actions (BattlescapeGame::handleAI). */
static void unit_go(int u)
{
    int count = 0, guard = 0, r;

    me = u;
    U = &g_units[u];
    tfac = U->faction == FACTION_CIVILIAN ? FACTION_ALIEN : FACTION_XCOM;
    searched = tried = esc_tu = amb_tu = nt_key = nt_keep = 0;
    g_walk_reserve = 0;         /* not the last unit's patrol reserve */
    charge = -1;
    if (ai_unit[u].flags & AF_STAY) {
        /* Waiting in ambush since the first round: it would decide the
           same unless an enemy came into sight (or psionics). */
        ai_unit[u].flags &= ~AF_STAY;
        gun = U->weapon != NO_ITEM;
        melee = U->melee != 0;
        if (!U->psi && !nearest_target())
            return;
    }
    while (count < 2 && guard++ < 6 && U->alive && !bt_over()) {
        if (U->tu <= 5) {
            ai_unit[u].flags |= AF_DONE;
            break;
        }
        count++;
        if ((r = think()) == R_RETHINK && U->alive && !bt_over()) {
            nt_keep = 1;        /* nothing happened: the same targets */
            r = think();
        }
        if (r == R_IDLE)
            break;
        if (r == R_FREE)
            count--;
        if (r == R_FINAL) {
            ai_unit[u].flags |= AF_DONE;
            break;
        }
    }
}

/* DOS's Hidden Movement screen (OpenXcom Map::draw, BattlescapeMessage):
   while a unit X-COM does not see acts, TAC00.SCR in a window over the map
   with the big "Hidden Movement"; the panel stays. The view goes on being
   drawn into the back buffer (gfx.h g_view_off) until bt_view_on shows it
   (battle.c: a seen unit acts, a shot or blast X-COM can see, the player's
   turn). A black window instead of TAC00 (compromises.md): the picture
   took seconds to draw on an XT. */
static void view_off(void)
{
    if (g_view_off)
        return;
    uk_theme(T_GREY);
    uk_fill(0, 0, 320, 144, 0);
    uk_window(0, 0, 320, 144);
    uk_center(0, 320, 64, UK_BIG, g_uk->title, "HIDDEN MOVEMENT");
    g_view_off = 1;
}

/* Everyone of a side in two rounds, as the DOS game moves its units (a
   soldier taken over in the second round still gets a go). */
static void side_go(int faction)
{
    int pass, u, any = 1;
    const Unit *un;

    memset(spent, 0, sizeof spent);
    for (pass = 0; pass < 4 && any; pass++)
        for (u = any = 0; u < g_nunits && !bt_over(); u++) {
            unsigned long t0;
            un = &g_units[u];
            if (!un->alive || un->faction != faction || (ai_unit[u].flags & AF_DONE) || un->panicked)
                continue;
            t0 = timer_read();
            if (un->seen)
                bt_view_on();
            else
                view_off();
            busy_tick();
            unit_go(u);
            any = 1;
            if (pass)
                ai_unit[u].flags |= AF_DONE;
            spent[u] += (unsigned)timer_ms(timer_read() - t0);
        }
    /* The patrol reserve is the AI's: walk() checks it for every walker,
       so it must not outlast the side's turn (X-COM's walks). */
    g_walk_reserve = 0;
#ifdef AI_PROFILE
    log_printf("T%d: ai ms: reach %lu (%u) escape %lu (%u) ambush %lu (%u) fire point %lu (%u)"
               " walk A* %lu (%u) walk %lu (%u) targets %lu (%u) spotted %lu (%u) patrol %lu (%u)"
               " think %lu (%u), %u lof, %u voxel lines",
               turn_no, timer_ms(pf[0]), pf_n[0], timer_ms(pf[1]), pf_n[1], timer_ms(pf[2]), pf_n[2],
               timer_ms(pf[3]), pf_n[3], timer_ms(pf[4]), pf_n[4], timer_ms(pf[5]), pf_n[5],
               timer_ms(pf[6]), pf_n[6], timer_ms(pf[7]), pf_n[7], timer_ms(pf[8]), pf_n[8],
               timer_ms(pf[9]), pf_n[9], pf_lof, pf_vox);
    memset(pf, 0, sizeof pf);
    memset(pf_n, 0, sizeof pf_n);
    pf_lof = pf_vox = 0;
#endif
    for (u = 0; u < g_nunits; u++) {
        un = &g_units[u];
        if (un->faction == faction && (un->alive || spent[u]))
            log_printf("T%d: %s %d at %d,%d,%d hp %d tu %d mode %d seen %d, %u ms", turn_no,
                       faction == FACTION_CIVILIAN ? "civilian" : "alien", u, un->x, un->y,
                       un->z, un->hp, un->tu, ai_unit[u].mode, un->seen, spent[u]);
    }
}

/* ------------------------------------------------------------------ */
/* The turns (the hooks the battle core calls are in aihook.c) */

void ai_turn(int turn)
{
    unsigned long t0 = timer_read();
    int a, s, aliens = 0;
    Unit *u;

    turn_no = turn;
    bt_say(1, COL_BAD, "Hidden movement...");
    for (a = 0; a < g_nunits; a++) {
        u = &g_units[a];
        u->moved = 0;
        ai_unit[a].flags &= ~(AF_DONE | AF_PSI);
        if (u->psi_state & PSI_ALIEN) {
            /* Last turn's mind control wears off, with no time left. */
            u->psi_state &= ~PSI_ALIEN;
            u->faction = FACTION_XCOM;
            u->tu = 0;
            bt_redraw(a);
        }
        aliens += u->alive && u->faction == FACTION_ALIEN;
    }
    /* After turn 20 (10 with two aliens left) the aliens know everything;
       before, what they see where they stand. */
    cheat = turn > 20 || (turn > 10 && aliens <= 2);
    for (s = 0; s < g_nunits; s++) {
        u = &g_units[s];
        if (u->alive && u->faction != FACTION_ALIEN && ai_spotted[s])
            for (a = 0; a < g_nunits; a++)
                if (g_units[a].alive && g_units[a].faction == FACTION_ALIEN
                    && ((cheat && u->faction == FACTION_XCOM) || los_unit_sees(a, s))) {
                    ai_spotted[s] = 0;
                    break;
                }
    }
    if (g_base_layout)
        rooms_refresh();
    side_go(FACTION_ALIEN);
    bt_flush();
    log_printf("T%d: alien turn %lu ms", turn, timer_ms(timer_read() - t0));
}

void ai_civilians(void)
{
    unsigned long t0 = timer_read();

    /* Time units: battle.c's prepare_side, as for every side. */
    side_go(FACTION_CIVILIAN);
    bt_flush();
    log_printf("T%d: civilian turn %lu ms", turn_no, timer_ms(timer_read() - t0));
}

