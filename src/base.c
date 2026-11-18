/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The X-COM bases: stores, staff, research, manufacturing, transfers and
   the bases block (several bases, the current one in g_base, g_facil and
   g_roster). The base screens are in basescr.c. */

#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alien.h"
#include "base.h"
#include "bsave.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "inv.h"
#include "map.h"
#include "psi.h"
#include "uik.h"
#include "unit.h"

Base g_base;
BaseBlock *g_bb;
int g_nbases, g_bcur;

/* ------------------------------------------------------------------ */
/* The bases block */

#define PARK_FILE   "BASES.TMP"
static int parked;              /* the block is in PARK_FILE */

/* The pool: base b's soldiers follow those of the bases before it. */
static unsigned pool_start(int b)
{
    unsigned n = 0;

    while (b-- > 0)
        n += g_bb->rec[b].roster_n;
    return n;
}

#define pool_used()     pool_start(g_nbases)
/* BASES.TMP and the saves: the block up to the pool, then the soldiers. */
#define BB_HEAD         offsetof(BaseBlock, pool)

/* g_roster, g_roster_n: the current base's soldiers in the pool. */
static void roster_view(void)
{
    if (g_bb && g_bcur >= 0) {
        g_roster = &g_bb->pool[pool_start(g_bcur)];
        g_roster_n = g_bb->rec[g_bcur].roster_n;
    } else {
        g_roster = NULL;
        g_roster_n = 0;
    }
}

Soldier *base_soldier_new(int b)
{
    unsigned at, used = pool_used();
    Soldier *s;

    if (used >= MAX_SOLDIERS || g_bb->rec[b].roster_n >= MAX_ROSTER)
        return NULL;
    at = pool_start(b) + g_bb->rec[b].roster_n;
    s = &g_bb->pool[at];
    memmove(s + 1, s, (used - at) * sizeof *s);
    memset(s, 0, sizeof *s);
    g_bb->rec[b].roster_n++;
    roster_view();
    return s;
}

void base_soldier_remove(int b, int i)
{
    unsigned at = pool_start(b) + i;
    Soldier *s = &g_bb->pool[at];

    memmove(s, s + 1, (pool_used() - at - 1) * sizeof *s);
    g_bb->rec[b].roster_n--;
    roster_view();
}

int base_soldier_room(int b)
{
    int i, k, here = 0, all = 0;

    for (i = 0; i < g_nbases; i++) {
        const Transfer *a = i == g_bcur ? g_base.arriving : base_rec(i)->arriving;
        for (k = 0; k < MAX_TRANSFERS; k++)
            if (a[k].what == TR_SOLDIER) {
                all += a[k].n;
                if (i == b)
                    here += a[k].n;
            }
    }
    here = MAX_ROSTER - g_bb->rec[b].roster_n - here;
    all = MAX_SOLDIERS - (int)pool_used() - all;
    return here < all ? here : all;
}

int base_squad_pick(unsigned char *idx)
{
    int i, n = 0;

    for (i = 0; i < g_roster_n && n < g_squad_max && n < MAX_SQUAD; i++)
        if (SOLDIER_READY(&g_roster[i]) && (!g_squad_craft || g_roster[i].craft == g_squad_craft))
            idx[n++] = (unsigned char)i;
    return n;
}

/* Before a battle: the squad's records (base_squad_pick, as the setup's
   equipment took them: hwpscr.c) leave the pool for a block of their own,
   which the battle keeps; g_squad_idx says where they came from. A saved
   battle brings its own (bs_restore). */
static void squad_out(void)
{
    Soldier *pool = g_roster;
    int i, n;

    memset(g_squad_idx, 0xFF, sizeof g_squad_idx);
    if (g_bs_resume) {
        roster_squad_free();
        return;
    }
    n = base_squad_pick(g_squad_idx);
    if (roster_squad_alloc(n)) {
        log_printf("BASE no memory for the squad");
        return;
    }
    for (i = 0; i < n; i++)
        g_roster[i] = pool[g_squad_idx[i]];
    log_printf("BASE squad of %d in the battle's roster", n);
}

/* After it: the squad's records go home (if the battle counted:
   roster_after_battle), the dead leave. */
static void squad_home(void)
{
    int i;

    if (g_squad_home && roster_squad_on() && g_bcur >= 0) {
        Soldier *pool = &g_bb->pool[pool_start(g_bcur)];
        for (i = 0; i < g_roster_n; i++)
            if (g_squad_idx[i] < g_bb->rec[g_bcur].roster_n)
                pool[g_squad_idx[i]] = g_roster[i];
    }
    g_squad_home = 0;
    roster_squad_free();
    roster_view();
    for (i = g_roster_n; i-- > 0; )
        if (!g_roster[i].alive)
            base_soldier_remove(g_bcur, i);
}

int base_block_alloc(void)
{
    unsigned seg;

    if (g_bb)
        return 1;
    if (_dos_allocmem((sizeof(BaseBlock) + 15) >> 4, &seg))
        return 0;
    g_bb = MK_FP(seg, 0);
    memset(g_bb, 0, sizeof *g_bb);
    return 1;
}

void base_block_free(void)
{
    if (g_bb)
        _dos_freemem(FP_SEG(g_bb));
    g_bb = NULL;
}

int base_park(void)
{
    FILE *f;
    int ok;

    if (!g_bb || parked)
        return 0;
    base_sync();
    squad_out();
    f = fopen(PARK_FILE, "wb");
    if (!f)
        ok = 0;
    else {
        ok = fwrite(g_bb, BB_HEAD + pool_used() * sizeof(Soldier), 1, f) == 1;
        ok = !fclose(f) && ok;
    }
    if (!ok) {
        log_printf("BASE bases kept in memory: %s not written", PARK_FILE);
        return 1;
    }
    base_block_free();
    parked = 1;
    return 0;
}

int base_unpark(void)
{
    FILE *f;
    int ok;

    if (!base_block_alloc())
        return 1;
    if (parked) {
        f = fopen(PARK_FILE, "rb");
        ok = f && fread(g_bb, BB_HEAD, 1, f) == 1;
        ok = ok && fread(g_bb->pool, sizeof(Soldier), pool_used(), f) == pool_used();
        if (f)
            fclose(f);
        if (!ok)
            return 1;
        remove(PARK_FILE);
        parked = 0;
    }
    squad_home();
    return 0;
}

Base *base_rec(int b)
{
    return (Base *)g_bb->rec[b].base;
}

unsigned *base_stores(int b)
{
    return b == g_bcur ? g_base.stores : base_rec(b)->stores;
}

Soldier *base_soldier(int b, int i)
{
    return &g_bb->pool[pool_start(b) + i];
}

int base_roster_n(int b)
{
    return g_bb->rec[b].roster_n;
}

void base_sync(void)
{
    BaseRec *r;

    if (!g_bb || g_bcur < 0)
        return;
    r = &g_bb->rec[g_bcur];
    memcpy(r->base, &g_base, BASE_LOCAL);
    r->facil = g_facil;
}

static void base_load(int b)
{
    BaseRec *r = &g_bb->rec[b];

    memcpy(&g_base, r->base, BASE_LOCAL);
    g_facil = r->facil;
    g_bcur = b;
    roster_view();
}

void base_select(int b)
{
    if (b == g_bcur || b < 0 || b >= g_nbases)
        return;
    base_sync();
    base_load(b);
}

/* Saved games: the bases, the way points, the craft and each
   base's record (the current base synced first). 1 = ok. */
int base_save_body(FILE *f)
{
    int ok;

    base_sync();
    ok = fwrite(&g_nbases, sizeof g_nbases, 1, f) == 1
      && fwrite(&g_bcur, sizeof g_bcur, 1, f) == 1
      && fwrite(g_bhdr, sizeof g_bhdr[0], g_nbases, f) == (size_t)g_nbases
      && fwrite(g_wp, sizeof g_wp, 1, f) == 1
      && fwrite(&g_bb->wp_next, sizeof g_bb->wp_next, 1, f) == 1
      && fwrite(g_craft, sizeof g_craft, 1, f) == 1
      && fwrite(g_bb->rec, sizeof g_bb->rec[0], g_nbases, f) == (size_t)g_nbases
      && fwrite(g_bb->pool, sizeof(Soldier), pool_used(), f) == pool_used();
    return ok;
}

int base_load_body(FILE *f, int old)
{
    int n, cur, i, ok;

    ok = fread(&n, sizeof n, 1, f) == 1 && fread(&cur, sizeof cur, 1, f) == 1
      && n > 0 && n <= MAX_BASES && cur >= 0 && cur < n;
    if (!ok)
        return 0;
    memset(g_bb, 0, sizeof *g_bb);
    ok = fread(g_bhdr, sizeof g_bhdr[0], n, f) == (size_t)n && base_load_lists(f, n, old);
    g_nbases = n;
    for (i = 0; ok && i < n; i++)
        ok = g_bb->rec[i].roster_n <= MAX_ROSTER;
    ok = ok && pool_used() <= MAX_SOLDIERS
      && fread(g_bb->pool, sizeof(Soldier), pool_used(), f) == pool_used();
    if (!ok)
        memset(g_bb->rec, 0, sizeof g_bb->rec);     /* (no pool beyond its end) */
    base_load(cur);
    return ok;
}

/* The DOS starting base (startingBase.rul): the stores here, the
   Skyranger's load aboard it (craft_new); the funds are the countries'
   first month (alien_new_game). The bases block starts with this base
   alone. */
void base_new(void)
{
    memset(&g_base, 0, sizeof g_base);
    memset(g_bb, 0, sizeof *g_bb);
    g_nbases = 1;
    g_bcur = 0;
    g_base.funds = 1000;
    g_base.scientists = 10;
    g_base.engineers = 10;
    g_base.stores[IT_PISTOL] = 2;
    g_base.stores[IT_RIFLE] = 2;
    g_base.stores[IT_HCANNON] = 1;
    g_base.stores[IT_CLIP_PISTOL] = 8;
    g_base.stores[IT_CLIP_RIFLE] = 8;
    g_base.stores[IT_CLIP_CANNON] = 6;
    g_base.stores[IT_GRENADE] = 5;
    g_base.stores[IT_SMOKE] = 5;
    g_base.stores[IT_ACANNON] = 1;
    g_base.stores[IT_CLIP_ACANNON] = 6;
    g_base.stores[IT_RLAUNCHER] = 1;
    g_base.stores[IT_ROCKET] = 4;
    roster_view();
    craft_new();
    facil_new();
}

static int name_seq;

int roster_recruit(void)
{
    Soldier *s = g_bb && g_bcur >= 0 ? base_soldier_new(g_bcur) : NULL;
    int n = N_NAMES;

    if (!s)
        return -1;
    strncpy(s->name, g_unit_names[name_seq % n], sizeof s->name - 1);
    name_seq++;
    /* soldiers.rul minStats .. maxStats (Soldier::Soldier) */
    s->tu0 = s->tu = (unsigned char)(50 + rnd(11));
    s->stamina = (unsigned char)(40 + rnd(31));
    s->hp = (unsigned char)(25 + rnd(16));
    s->bravery = (unsigned char)(10 + 10 * rnd(6));     /* 10-60 in tens */
    s->reactions = (unsigned char)(30 + rnd(31));
    s->accuracy = (unsigned char)(40 + rnd(31));
    s->throwing = (unsigned char)(50 + rnd(31));
    s->strength = (unsigned char)(20 + rnd(21));
    s->psi = (unsigned char)rnd(101);                   /* hidden psi strength 0-100 */
    s->alive = 1;
    s->layout[0][0] = NO_ITEM;          /* no layout: equipped automatically */
    return g_roster_n - 1;
}

/* The new campaign's soldiers at the current base (it has none yet). */
void roster_new(int n)
{
    name_seq = rnd(12);
    while (n-- > 0)
        roster_recruit();
}

int base_add(int x, int y, const char *name)
{
    int b = g_nbases;

    if (b >= MAX_BASES)
        return -1;
    base_sync();
    memset(&g_bb->rec[b], 0, sizeof g_bb->rec[b]);
    memset(&g_bhdr[b], 0, sizeof g_bhdr[b]);
    strncpy(g_bhdr[b].name, name, sizeof g_bhdr[b].name - 1);
    g_bhdr[b].x = x;
    g_bhdr[b].y = y;
    g_nbases++;
    base_load(b);
    facil_summary();
    log_printf("BASE %s built at %d,%d", g_bhdr[b].name, x, y);
    return b;
}

void base_remove(int b)
{
    int i, n = g_nbases - b - 1;

    for (i = 0; i < MAX_CRAFT; i++) {
        Craft *c = &g_craft[i];
        if (c->state == CS_EMPTY)
            continue;
        if (c->base == b)
            craft_lose(c);
        else if (c->base > b)
            c->base--;
    }
    log_printf("BASE %s lost", g_bhdr[b].name);
    /* Its research projects are lost with it, progress and all. */
    if (b != g_bcur)
        base_sync();
    {
        /* its soldiers too */
        unsigned at = pool_start(b), k = g_bb->rec[b].roster_n;
        memmove(&g_bb->pool[at], &g_bb->pool[at + k], (pool_used() - at - k) * sizeof(Soldier));
    }
    memmove(&g_bhdr[b], &g_bhdr[b + 1], n * sizeof g_bhdr[0]);
    memmove(&g_bb->rec[b], &g_bb->rec[b + 1], n * sizeof g_bb->rec[0]);
    g_nbases--;
    memset(&g_bb->rec[g_nbases], 0, sizeof g_bb->rec[0]);
    if (b == g_bcur) {
        g_bcur = -1;
        if (g_nbases)
            base_load(0);
    } else if (b < g_bcur)
        g_bcur--;
    roster_view();
}

/* New base sites cost by region (regions.rul cost, $10K; the world
   pack's regions are in that file's order). */
static const unsigned char region_cost[15] = {
    80, 95, 90, 60, 100, 65, 55, 50, 75, 80, 75, 60, 50, 50, 50
};

int base_site_cost(int x, int y)
{
    int r = alien_region(x, y);
    return (r >= 0 && r < 15 ? region_cost[r] : 50) * 10;
}

/* ------------------------------------------------------------------ */
/* Money and deliveries */

int base_pay10(unsigned long amount)
{
    if (g_base.funds * 100 + g_base.funds10 < (long)amount)
        return 0;
    base_add10(-(long)amount);
    return 1;
}

void base_add10(long amount)
{
    long t = g_base.funds * 100 + g_base.funds10 + amount;
    long k = t >= 0 ? t / 100 : -((99 - t) / 100);

    g_base.funds = k;
    g_base.funds10 = (unsigned char)(t - k * 100);
}

int base_transfer_to(int b, int what, unsigned n, int hours)
{
    Transfer *a = b == g_bcur ? g_base.arriving : base_rec(b)->arriving;
    int i;

    /* The same thing due at the same hour joins it (buying HWP shells one
       by one used up the list at 24). */
    for (i = 0; i < MAX_TRANSFERS; i++)
        if (a[i].what == what && a[i].hours == hours && what < TR_SCIENTIST) {
            a[i].n += n;
            return 1;
        }
    for (i = 0; i < MAX_TRANSFERS; i++)
        if (!a[i].what) {
            a[i].what = (unsigned char)what;
            a[i].hours = (unsigned char)hours;
            a[i].n = n;
            return 1;
        }
    return 0;
}

int base_transfer(int what, unsigned n, int hours)
{
    return base_transfer_to(g_bcur, what, n, hours);
}

unsigned base_arriving(int what)
{
    unsigned n = 0;
    int i;

    for (i = 0; i < MAX_TRANSFERS; i++)
        if (g_base.arriving[i].what == what)
            n += g_base.arriving[i].n;
    return n;
}

static char msgbuf[48];

/* Hourly: deliveries arrive (the stores, the staff, recruits) and
   transferred soldiers reach the base. */
static const char *deliveries(void)
{
    const char *msg = NULL;
    int i, k;

    for (i = 0; i < MAX_TRANSFERS; i++) {
        Transfer *t = &g_base.arriving[i];
        if (!t->what || --t->hours)
            continue;
        base_arrival(t->what, t->n);
        if (t->what == TR_SCIENTIST)
            g_base.scientists += (unsigned char)t->n;
        else if (t->what == TR_ENGINEER)
            g_base.engineers += (unsigned char)t->n;
        else if (t->what == TR_SOLDIER) {
            for (; t->n > 0; t->n--)
                if ((k = roster_recruit()) >= 0)
                    base_name_soldier(g_roster[k].name);
                else
                    log_printf("BASE no room for a recruit at %s", g_bhdr[g_bcur].name);
            log_printf("BASE recruits arrived at %s: %d soldiers", g_bhdr[g_bcur].name,
                       g_roster_n);
        } else
            g_base.stores[t->what - 1] += t->n;
        t->what = 0;
        msg = "";
    }
    for (i = 0; i < g_roster_n; i++)
        if (g_roster[i].transit && !--g_roster[i].transit) {
            base_arrival(TR_SOLDIER, 1);
            msg = "";
        }
    if (msg) {
        sprintf(msgbuf, "Transfer arrived at %.16s.", g_bhdr[g_bcur].name);
        msg = msgbuf;
    }
    return msg;
}

/* ------------------------------------------------------------------ */
/* Research and manufacturing */

/* Research uses up one sample of a UFO component or corpse, and an
   interrogation its live alien, when the project starts; a cancelled
   project gives it back (ResearchInfoState, Base::removeResearch). */
static int sample_of(int t)
{
    int item = g_topics[t].need_item;

    return (item >= IT_UFO_POWER && item <= IT_CORPSE_ETHEREAL)
           || (item >= IT_LIVE && item <= IT_LIVE_LAST) ? item : -1;
}

int base_sci_allocated(void)
{
    const BaseRec *r = BASE_REC;
    int i, n = 0;

    for (i = 0; i < r->n_res; i++)
        n += r->res[i].sci;
    return n;
}

int base_eng_allocated(void)
{
    const BaseRec *r = BASE_REC;
    int i, n = 0;

    for (i = 0; i < r->n_prod; i++)
        n += r->prod[i].eng;
    return n;
}

int base_workshops_used(void)
{
    const BaseRec *r = BASE_REC;
    int i, n = 0;

    for (i = 0; i < r->n_prod; i++)
        n += r->prod[i].eng + g_recipes[PROD_RCP(&r->prod[i])].space;
    return n;
}

int base_ships_in_production(void)
{
    const BaseRec *r;
    int i, n = 0;

    if (!g_bb)
        return 0;
    r = BASE_REC;
    for (i = 0; i < r->n_prod; i++)
        if (g_items[g_recipes[PROD_RCP(&r->prod[i])].item].kind == IK_SHIP)
            n += r->prod[i].total - r->prod[i].made;
    return n;
}

int base_research_start(int t)
{
    BaseRec *r = BASE_REC;
    int item = sample_of(t);

    ResProj *p;

    if (r->n_res >= MAX_RESEARCH)
        return -1;
    p = &r->res[r->n_res];
    /* The cost of this attempt: the rules' cost x 50-150 %
       (ResearchInfoState); kept while the project runs. */
    p->cost = (unsigned)((long)g_topics[t].cost * (50 + rnd(101)) / 100);
    if (!p->cost)
        p->cost = 1;
    p->spent = 0;
    if (item >= 0 && g_base.stores[item])
        g_base.stores[item]--;
    p->topic = (unsigned char)t;
    p->sci = 0;
    log_printf("BASE research %s (%u days)", g_topics[t].name, p->cost);
    return r->n_res++;
}

static void res_remove(BaseRec *r, int i)
{
    r->n_res--;
    _fmemmove(&r->res[i], &r->res[i + 1], (r->n_res - i) * sizeof r->res[0]);
}

void base_research_cancel(int i)
{
    BaseRec *r = BASE_REC;
    int t = r->res[i].topic, item = sample_of(t);

    if (item >= 0)
        g_base.stores[item]++;
    log_printf("BASE research %s cancelled", g_topics[t].name);
    res_remove(r, i);
}

/* 0 if one unit of recipe rc can be paid for, else what is missing:
   1 money, 2 materials (Production::haveEnoughMoneyForOneMoreUnit, then
   haveEnoughMaterialsForOneMoreUnit). */
static int cannot_pay(const Recipe *rc)
{
    if (g_base.funds * 100 + g_base.funds10 < rc->cost100 * 10L)
        return 1;
    if (g_base.stores[IT_ALLOYS] < rc->alloys || g_base.stores[IT_ELERIUM] < rc->elerium
        || g_base.stores[IT_UFO_POWER] < rc->power || g_base.stores[IT_UFO_NAV] < rc->nav)
        return 2;
    return 0;
}

/* Production::startItem: the money and materials for the next unit. */
int base_pay_item(int r)
{
    const Recipe *rc = &g_recipes[r];

    if (cannot_pay(rc))
        return 0;
    base_add10(-10L * rc->cost100);
    g_base.stores[IT_ALLOYS] -= rc->alloys;
    g_base.stores[IT_ELERIUM] -= rc->elerium;
    g_base.stores[IT_UFO_POWER] -= rc->power;
    g_base.stores[IT_UFO_NAV] -= rc->nav;
    return 1;
}

int base_prod_start(int rcp)
{
    BaseRec *r = BASE_REC;
    ProdRun *p;

    if (r->n_prod >= MAX_PRODUCTION)
        return -1;
    p = &r->prod[r->n_prod];
    p->recipe = (unsigned char)rcp;
    p->eng = 0;
    p->total = 1;
    p->made = p->hours = 0;
    return r->n_prod++;
}

void base_prod_stop(int i)
{
    BaseRec *r = BASE_REC;

    r->n_prod--;
    _fmemmove(&r->prod[i], &r->prod[i + 1], (r->n_prod - i) * sizeof r->prod[0]);
}

void base_popup(int kind, int id)
{
    if (g_bb->n_pop < MAX_POPUPS) {
        PopEvent *e = &g_bb->pop[g_bb->n_pop++];
        e->kind = (unsigned char)kind;
        e->base = (unsigned char)g_bcur;
        e->id = (unsigned char)id;
    }
}

void base_arrival(int what, unsigned n)
{
    int k = g_bb->n_pop;

    base_popup(POP_ARRIVAL, what);
    if (g_bb->n_pop > k)
        g_bb->pop[k].n = n;
}

/* An interrogation done whose name holds the text ("" = any). */
static int int_done(const char *what)
{
    int t;

    for (t = RS_INT; t <= RS_INT_LAST; t++)
        if (g_base.done[t] && strstr(g_topics[t].name, what))
            return 1;
    return 0;
}

int base_topic_available(int t)
{
    const Topic *tp = &g_topics[t];
    int i;

    if (g_base.done[t])
        return 0;
    for (i = 0; i < BASE_REC->n_res; i++)
        if (BASE_REC->res[i].topic == t)
            return 0;
    /* UFO Construction and the Power Suit have more prerequisites
       (research.rul). */
    if (t == RS_UFO_CONSTR && !(g_base.done[RS_ALLOYS] && g_base.done[RS_ELERIUM]))
        return 0;
    if (t == RS_POWERSUIT && !g_base.done[RS_POWER_SOURCE])
        return 0;
    if (tp->req1 != REQ_NONE && !g_base.done[tp->req1])
        return 0;
    if (tp->req2 != REQ_NONE && !g_base.done[tp->req2])
        return 0;
    if (tp->need_item != NO_ITEM && !g_base.stores[tp->need_item])
        return 0;
    if (tp->race >= 0 && !(g_base.races_met & (1 << tp->race)))
        return 0;
    /* Topics the interrogations unlock (research.rul): any opens Alien
       Origins; leaders and commanders The Martian Solution (LEADER_PLUS),
       commanders Cydonia or Bust (COMMANDER_PLUS), navigators the
       Hyper-wave Decoder; sectoid leaders and commanders and every
       ethereal the Psi Lab. */
    if (t == RS_ORIGINS && !int_done(""))
        return 0;
    if (t == RS_MARTIAN && !int_done("Leader") && !int_done("Commander"))
        return 0;
    if (t == RS_CYDONIA && !int_done("Commander"))
        return 0;
    if (t == RS_HYPERWAVE && !int_done("Navigator"))
        return 0;
    if (t == RS_PSILAB && !int_done("Ethereal") && !int_done("Sectoid L")
        && !int_done("Sectoid C"))
        return 0;
    return 1;
}

int base_recipe_known(int r)
{
    int it = g_recipes[r].item;

    return g_base.done[g_recipes[r].topic]
        && (!(it == IT_HOVER_PLASMA || it == IT_HOVER_LAUNCHER || it == IT_HWP_FUSION)
            || g_base.done[RS_NEW_FIGHTER]);
}

/* Hourly: each run's engineers add their hours (Production::step); every
   unit done starts and pays for the next. A run ends when its units are
   made, or when the next unit cannot be paid for (ProductionCompleteState:
   the message, the run removed). */
static void production_hour(void)
{
    BaseRec *r = BASE_REC;
    char m[64];
    int i = 0;

    while (i < r->n_prod) {
        ProdRun *p = &r->prod[i];
        const Recipe *rc = &g_recipes[PROD_RCP(p)];
        int why = -1;

        p->hours += p->eng;
        while (p->hours >= rc->hours) {
            p->hours -= rc->hours;
            if (g_items[rc->item].kind == IK_SHIP)     /* the hangar was kept free */
                craft_add(CT_FIRESTORM + rc->item - IT_SHIP_FIRESTORM);
            else if (p->recipe & PROD_SELL) {   /* Production::step: sold, not stored */
                unsigned long v = rc->item == IT_HOVER_PLASMA ? 98000UL
                    : rc->item == IT_HOVER_LAUNCHER ? 104300UL : g_sell10[rc->item];
                base_add10((long)v);
                log_printf("BASE sold %s for $%lu", g_items[rc->item].name, v * 10);
            } else
                g_base.stores[rc->item]++;
            p->made++;
            if (p->total != PROD_INFINITE && p->made >= p->total) {
                why = 0;
                break;
            }
            if ((why = cannot_pay(rc)) != 0)
                break;
            base_pay_item(PROD_RCP(p));
            why = -1;
        }
        if (why < 0) {
            i++;
            continue;
        }
        if (!why)
            sprintf(m, "Production complete: %s", g_items[rc->item].name);
        else
            sprintf(m, "Not enough %s to produce %s", why == 1 ? "money" : "materials",
                    g_items[rc->item].name);
        geo_alert(10, m);
        base_popup(POP_PROD_DONE + why, PROD_RCP(p));
        base_prod_stop(i);
    }
}

/* Hourly: manufacturing, deliveries. */
const char *base_hour(void)
{
    const char *msg = deliveries();

    production_hour();
    return msg;
}

/* Midnight: a day of each research project (ResearchProject::step: its
   scientists add a scientist-day each); the finished ones leave the
   list. */
static void research_day(void)
{
    BaseRec *r = BASE_REC;
    char m[64];
    int i = 0;

    while (i < r->n_res) {
        int t = r->res[i].topic, b, k;
        r->res[i].spent += r->res[i].sci;
        if (r->res[i].spent < r->res[i].cost) {
            i++;
            continue;
        }
        g_base.done[t] = 1;
        res_remove(r, i);
        /* The same topic at other bases ends there (its sample back). */
        for (b = 0; b < g_nbases; b++) {
            BaseRec *o = &g_bb->rec[b];
            if (b == g_bcur)
                continue;
            for (k = 0; k < o->n_res; k++)
                if (o->res[k].topic == t) {
                    int item = sample_of(t);
                    if (item >= 0)
                        base_stores(b)[item]++;
                    log_printf("BASE research %s ends at %s", g_topics[t].name,
                               g_bhdr[b].name);
                    res_remove(o, k);
                    break;
                }
        }
        if (t == RS_CYDONIA)
            g_base.cydonia = 1;
        geo_research(g_topics[t].points);
        sprintf(m, "Research complete: %s", g_topics[t].name);
        geo_alert(10, m);
        base_popup(POP_RESEARCH, t);
    }
}

/* Midnight: construction, then a day of research, and a day of the
   wounded's recovery (Soldier::heal). */
const char *base_day(void)
{
    const char *msg = facil_day();
    int i;

    for (i = 0; i < g_roster_n; i++)
        if (g_roster[i].recovery && !--g_roster[i].recovery)
            log_printf("BASE %s has recovered", g_roster[i].name);
    research_day();
    return msg;
}

/* Something for base_hour to do at base b (bases without deliveries,
   transfers or production are not swapped in every hour). */
static int base_busy(int b)
{
    const Base *r = b == g_bcur ? &g_base : base_rec(b);
    int i;

    if (g_bb->rec[b].n_prod)
        return 1;
    for (i = 0; i < MAX_TRANSFERS; i++)
        if (r->arriving[i].what)
            return 1;
    {
        const Soldier *s = base_soldier(b, 0);
        for (i = base_roster_n(b); i-- > 0; )
            if (s[i].transit)
                return 1;
    }
    return 0;
}

const char *base_hour_all(void)
{
    const char *msg = NULL, *m;
    int b, cur = g_bcur;

    for (b = 0; b < g_nbases; b++)
        if (base_busy(b)) {
            base_select(b);
            if ((m = base_hour()) != NULL) {
                geo_alert(10, m);
                msg = m;
            }
        }
    base_select(cur);
    return msg;
}

const char *base_day_all(void)
{
    const char *msg = NULL, *m;
    int b, cur = g_bcur;

    for (b = 0; b < g_nbases; b++) {
        base_select(b);
        if ((m = base_day()) != NULL) {
            geo_alert(10, m);
            msg = m;
        }
    }
    base_select(cur);
    return msg;
}

/* Salaries $30K a scientist, $25K an engineer, $20K a soldier (also those
   on their way), craft rent (Skyranger $500K, Interceptors $600K, ordered
   ones too) and facility upkeep. */
long base_monthly_costs(void)
{
    return (g_base.scientists + (long)base_arriving(TR_SCIENTIST)) * 30
         + (g_base.engineers + (long)base_arriving(TR_ENGINEER)) * 25
         + (roster_alive() + (long)base_arriving(TR_SOLDIER)) * 20
         + craft_rent() + facil_upkeep();
}

/* Month end at every base: psi training; returns the costs of all. */
long base_month_all(void)
{
    long costs = 0;
    int b, cur = g_bcur;

    for (b = 0; b < g_nbases; b++) {
        const char *m;
        base_select(b);
        costs += base_monthly_costs();
        if ((m = psi_month()) != NULL)
            geo_alert(10, m);
    }
    base_select(cur);
    return costs;
}

/* Promotion score (OpenXcom SavedGame::getSoldierScore; DOS has no melee
   stat, so that term is left out). */
static int soldier_score(const Soldier *s)
{
    int v = 2 * s->hp + 2 * s->stamina + 4 * s->reactions + 4 * s->bravery
          + 3 * (s->tu + 2 * s->accuracy) + s->throwing + s->strength;

    if (s->psi_skill)
        v += s->psi + 2 * s->psi_skill;
    return v + 10 * (s->missions + s->kills);
}

/* Promotions as in the DOS game (SavedGame::handlePromotions, all bases):
   rookies with any experience became squaddies after the mission; above
   that, the best by soldier_score fill the slots the size of the force
   allows: a sergeant per 5 soldiers, a captain per 11, a colonel per 23,
   one commander from 30. */
void base_promote_all(void)
{
    static const unsigned char per[6] = { 0, 0, 5, 11, 23, 30 };
    Soldier *pool = g_bb->pool;
    int r, i, total = pool_used();

    /* The pool holds every base's soldiers, base by base. */
    for (r = RANK_COMMANDER; r >= RANK_SERGEANT; r--) {
        int slots = r == RANK_COMMANDER ? total >= 30 : total / per[r], have = 0;
        for (i = 0; i < total; i++)
            if (pool[i].rank >= r)
                have++;
        while (have < slots) {
            Soldier *best = NULL;
            int score = -1;
            for (i = 0; i < total; i++) {
                Soldier *s = &pool[i];
                int sc = soldier_score(s);
                if (s->rank == r - 1 && sc > score) {
                    best = s;
                    score = sc;
                }
            }
            if (!best)
                break;
            best->rank = (unsigned char)r;
            log_printf("%s promoted to %s", best->name, g_rank_names[r]);
            have++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* After a mission */

int g_loot_score;

/* UFO components and alloys from the battle map's tiles (g_tile_loot by
   special type 2-11, DebriefingState): an item per tile part with its
   recovery points (items.rul: power source 20, navigation 5, the Alien
   Habitat 1, the others 2; UFO construction, special 4, only its points).
   Alloys: their parts / 10 at a UFO, / 150 elsewhere, a point each. The
   Elerium-115 is items on the floor (artifact). Returns how many
   components. */
static int tile_loot(int ufo, int *alloys, DebStats *ds)
{
    static const unsigned char item[10] = {
        IT_UFO_POWER, IT_UFO_NAV, NO_ITEM, IT_ALIEN_FOOD, IT_ALIEN_REPRO, IT_ALIEN_ENTERT,
        IT_ALIEN_SURGERY, IT_EXAM_ROOM, IT_ALLOYS, IT_ALIEN_HABITAT
    };
    int k, n = 0, pts;
    unsigned q;

    for (k = 0; k < 10; k++) {
        q = g_tile_loot[k];
        if (k == 8) {
            q /= ufo ? 10 : 150;
            *alloys = q;
            pts = q;
        } else {
            pts = q * (k == 0 ? 20 : k == 1 ? 5 : k == 9 ? 1 : 2);
            if (item[k] != NO_ITEM)
                n += q;
        }
        ds->qty[DS_POWER + k] = q;
        ds->score[DS_POWER + k] = pts;
        g_loot_score += pts;
        if (item[k] != NO_ITEM)
            g_base.stores[item[k]] += q;
    }
    return n;
}

/* Alien artifacts (DebriefingState::recoverItems): the recovery points
   (items.rul) of an alien item on the battlefield whose research is not
   done. */
static int artifact_points(int it)
{
    int t, p;

    switch (it) {
    case IT_PPISTOL: t = RS_PPISTOL; p = 3; break;
    case IT_PRIFLE: t = RS_PRIFLE; p = 4; break;
    case IT_HPLASMA: t = RS_HPLASMA; p = 5; break;
    case IT_CLIP_PPISTOL: t = RS_PPISTOL; p = 1; break;
    case IT_CLIP_PRIFLE: t = RS_PRIFLE; p = 1; break;
    case IT_CLIP_HPLASMA: t = RS_HPLASMA; p = 1; break;
    case IT_AGRENADE: t = RS_AGRENADE; p = 1; break;
    case IT_LAUNCHER: t = RS_LAUNCHER; p = 4; break;
    case IT_STUNBOMB: t = RS_STUNBOMB; p = 1; break;
    case IT_BLASTER: t = RS_BLASTER; p = 5; break;
    case IT_BLASTERBOMB: t = RS_BLASTERBOMB; p = 1; break;
    case IT_MINDPROBE: t = RS_MINDPROBE; p = 1; break;
    default: return 0;
    }
    return g_base.done[t] ? 0 : p;
}

static int artifact(DebStats *ds, int item, int n)
{
    int p = artifact_points(item), k = g_items[item].kind;

    if (!p)
        return 0;
    if (k != IK_CLIP && k != IK_GRENADE)
        n = 1;
    ds->qty[DS_ARTIFACTS] += n;
    return p * n;
}

static int artifacts(DebStats *ds)
{
    int pts = 0;
    const FloorItem __near *f;
    const Carry __near *c;

    for (f = g_floor; f < g_floor_end; f++)
        if (f->item != NO_ITEM)
            pts += artifact(ds, f->item, f->n);
    for (c = g_carry; c < g_carry_end; c++)     /* the loot of a full floor list (inv.h) */
        if (c->unit == CARRY_LOOT)
            pts += artifact(ds, c->item, c->n);
    ds->score[DS_ARTIFACTS] = pts;
    return pts;
}

/* The Elerium-115 items on the floor and in the loot (what the squad
   carried, postbat.c): the power sources' fuel (recoverItems: alienFuel,
   50 each, 5 points; inv_home puts it in the stores). */
static void elerium(DebStats *ds)
{
    int n = 0;
    const FloorItem __near *f;
    const Carry __near *c;

    for (f = g_floor; f < g_floor_end; f++)
        n += f->item == IT_ELERIUM;
    for (c = g_carry; c < g_carry_end; c++)
        n += c->unit == CARRY_LOOT && c->item == IT_ELERIUM;
    ds->qty[DS_ELERIUM] = 50 * n;
    ds->score[DS_ELERIUM] = 5 * n;
}

const char *base_loot(int ufo_type, int crashed, int kills, int race, DebStats *ds)
{
    /* Weapons, gear and corpses come from the battlefield (inv.c); the
       artifacts' points before they go to the stores. */
    int arts = artifacts(ds), weapons, parts, alloys = 0;

    elerium(ds);
    weapons = inv_recover();

    (void)crashed;              /* the crash took its power sources (main.c) */
    (void)kills;
    g_loot_score = arts + ds->score[DS_ELERIUM];
    parts = tile_loot(ufo_type > 0, &alloys, ds);
    if (race >= 0 && race < 5)
        g_base.races_met |= 1 << race;
    log_printf("LOOT alloys %d elerium %d weapons %d parts %d corpses %d live %d died %d"
               " artifacts %d points %d", alloys, ds->qty[DS_ELERIUM], weapons, parts, g_corpses, g_captured,
               g_lost_captives, arts, g_loot_score);
    if (g_lost_captives)
        sprintf(msgbuf, "%d live alien%s died: no containment room", g_lost_captives,
                g_lost_captives > 1 ? "s" : "");
    else if (g_captured)
        sprintf(msgbuf, "Loot:%d guns %d parts %d corpses %d live", weapons, parts,
                g_corpses, g_captured);
    else
        sprintf(msgbuf, "Loot: %d guns, %d UFO parts, %d corpses", weapons, parts, g_corpses);
    return msgbuf;
}


