/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The alien side of the geoscape: missions, terror sites, alien bases,
   countries and their funding, the month-end report. Follows the DOS game
   as described by OpenXcom's rules (alienMissions, missionScripts, regions,
   countries, ufoTrajectories, alienDeployments) and code (AlienMission,
   AlienStrategy, Country::newMonth, MonthlyReportState), baked into the
   world pack by tools/world.py. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alien.h"
#include "base.h"
#include "geo.h"
#include "hud.h"
#include "input.h"
#include "map.h"
#include "menu.h"
#include "alien_in.h"

#define MAX_MISSIONS    10
#define ZONE_CITY       3
#define ZONE_BASE       4
#define ZONE_OUT        5
#define TABLE_MISSIONS  5           /* the regional mission table: research .. alien base */

#define COL_TEXT    15
#define COL_INFO    7
#define COL_ALERT   12
#define COL_GOOD    10

/* World tables (freed during battles). */
unsigned char *al_world;
static int *borders;                /* alien_borders */
static const Rect *rects;
static unsigned n_rect;
const Region *al_regions;
const Country *al_countries;
static const char (*cities)[16];
static const Traj *trajs;
static const MissionRule *rules;
static const UfoRule *ufo_rules;
#define n_regions al_n_regions      /* alien_in.h: the Graphs screen */
static int n_cities, n_traj;
int al_n_regions;
int al_n_countries;

/* Campaign state (saved). */
typedef struct {
    signed char type;           /* MIS_*, -1 = free */
    unsigned char region, race, wave, spawned, done;
    long countdown;             /* minutes to the next UFO */
    int sx, sy;                 /* terror city or supplied alien base, -1 = none */
} Mission;

static Mission missions[MAX_MISSIONS];
Site g_sites[MAX_SITES];
CountryState al_cstate[MAX_COUNTRIES];
long al_month_xcom, al_month_alien, al_month_research;
unsigned char al_warned;
/* An X-COM base's mission region (missions use the mission regions). */
#define BASE_REGION(b)  mission_region(region_at(g_bhdr[b].x, g_bhdr[b].y))
static unsigned char table[MAX_REGIONS];    /* mission types left per region (bits) */
static unsigned char retal_mark;            /* bit per X-COM base: found by a
                                               retaliation UFO */
int al_last_rating;                         /* the previous month's rating */
int g_alien_test;

const char *const g_race_names[6] = {
    "Sectoid", "Floater", "Snakeman", "Muton", "Ethereal", "Mixed"
};

/* ------------------------------------------------------------------ */
/* World data */

int alien_load_world(FILE *f)
{
    unsigned char h[12];
    unsigned n_rects, n_rules, n_ufos;
    unsigned long size;
    unsigned char *p;

    alien_free_world();
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "XAL1", 4))
        return 1;
    n_regions = h[4];
    al_n_countries = h[5];
    n_traj = h[6];
    n_rules = h[7];
    n_cities = h[8];
    n_ufos = h[9];
    n_rects = h[10] | (h[11] << 8);
    if (n_regions > MAX_REGIONS || al_n_countries > MAX_COUNTRIES || n_rules < N_MISSION_TYPES
        || n_ufos < N_UFO_TYPES)
        return 1;
    size = (unsigned long)n_rects * sizeof(Rect) + n_regions * sizeof(Region)
         + al_n_countries * sizeof(Country) + n_cities * 16UL + n_traj * sizeof(Traj)
         + n_rules * sizeof(MissionRule) + n_ufos * sizeof(UfoRule);
    al_world = malloc((size_t)size);
    if (!al_world || fread(al_world, 1, (size_t)size, f) != (size_t)size) {
        alien_free_world();
        return 1;
    }
    p = al_world;
    rects = (const Rect *)p;            p += n_rects * sizeof(Rect);
    al_regions = (const Region *)p;        p += n_regions * sizeof(Region);
    al_countries = (const Country *)p;     p += al_n_countries * sizeof(Country);
    cities = (const char (*)[16])p;     p += n_cities * 16;
    trajs = (const Traj *)p;            p += n_traj * sizeof(Traj);
    rules = (const MissionRule *)p;     p += n_rules * sizeof(MissionRule);
    ufo_rules = (const UfoRule *)p;
    n_rect = n_rects;
    /* The country borders ("XBD1", after the rest): optional, a pack
       without them just shows none. */
    if (fread(h, 1, 6, f) == 6 && !memcmp(h, "XBD1", 4)) {
        unsigned words = h[4] | (h[5] << 8);
        if (words > 1 && words <= 1024 && (borders = malloc(words * 2)) != NULL
            && fread(borders, 2, words, f) != words) {
            free(borders);
            borders = NULL;
        }
        if (borders)
            borders[words - 1] = 0;
    }
    return 0;
}

void alien_free_world(void)
{
    if (al_world)
        free(al_world);
    al_world = NULL;
    if (borders)
        free(borders);
    borders = NULL;
}

const int *alien_borders(void)
{
    return borders;
}

static int in_rects(unsigned first, unsigned n, int x, int y)
{
    const Rect *r = rects + first;
    for (; n > 0; n--, r++)
        if (x >= (int)r->x0 && x <= (int)r->x1 && y >= r->y0 && y <= r->y1)
            return 1;
    return 0;
}

static int region_at(int x, int y)
{
    int i;
    for (i = 0; al_world && i < n_regions; i++)
        if (in_rects(al_regions[i].area, al_regions[i].n_area, x, y))
            return i;
    return -1;
}

int alien_region(int x, int y)
{
    return region_at(x, y);
}

static int country_at(int x, int y)
{
    int i;
    for (i = 0; al_world && i < al_n_countries; i++)
        if (in_rects(al_countries[i].area, al_countries[i].n_area, x, y))
            return i;
    return -1;
}

const char *alien_region_name(int x, int y)
{
    int r = region_at(x, y);
    return r >= 0 ? al_regions[r].name : "";
}

const char *alien_place(unsigned r, int *x, int *y)
{
    if (!al_world || r >= n_rect)
        return NULL;
    *x = rects[r].x0;
    *y = rects[r].y0;
    return rects[r].city < n_cities ? cities[rects[r].city] : "";
}

const char *alien_country_label(int i, int *x, int *y)
{
    if (!al_world || i >= al_n_countries)
        return NULL;
    *x = al_countries[i].label_x;
    *y = al_countries[i].label_y;
    return al_countries[i].name;
}

const char *alien_city_name(int city)
{
    return al_world && city < n_cities ? cities[city] : "";
}

/* Random point in one of the region's mission zone rectangles. */
static int zone_point(int region, int zone, int *x, int *y, unsigned char *city)
{
    const Region *r = &al_regions[region];
    const Rect *q;

    if (zone > 5 || !r->n_zone[zone])
        return 0;
    q = &rects[r->zone[zone] + rnd(r->n_zone[zone])];
    *x = q->x0 + rnd(q->x1 - q->x0 + 1);
    *y = q->y0 + rnd(q->y1 - q->y0 + 1);
    if (city)
        *city = q->city;
    return 1;
}

/* A point to land on (AlienMission::getLandPoint): on land and in the
   region, up to 100 tries; cities (point zones) are always good. */
static int land_point(int region, int zone, int *x, int *y)
{
    const Rect *q;
    int tries;

    if (zone > 5 || !al_regions[region].n_zone[zone])
        return 0;
    q = &rects[al_regions[region].zone[zone]];
    for (tries = 0; tries < 100; tries++) {
        zone_point(region, zone, x, y, NULL);
        if ((q->x0 == q->x1 && q->y0 == q->y1)
            || (geo_land(*x, *y) && in_rects(al_regions[region].area, al_regions[region].n_area, *x, *y)))
            break;
    }
    return 1;
}

/* Ocean regions borrow the missions of a land region. */
static int mission_region(int region)
{
    return region < 0 ? 0 : al_regions[region].home;
}

/* 0 beginner .. 4 superhuman (menu.c; experienced without the menu). */
int al_difficulty(void)
{
    return g_difficulty >= 0 && g_difficulty < N_DIFFICULTY ? g_difficulty : 1;
}

/* ------------------------------------------------------------------ */
/* Score */

void alien_credit(int x, int y, int pts, int alien)
{
    int c = country_at(x, y), r = region_at(x, y);

    if (alien)
        al_month_alien += pts;
    else
        al_month_xcom += pts;
    if (r >= 0 && r < GR_REGIONS && g_bb)       /* the Graphs' areas */
        (alien ? g_bb->gr.ralien : g_bb->gr.rxcom)[0][r] += pts;
    if (c < 0)
        return;
    if (alien)
        al_cstate[c].alien += pts;
    else
        al_cstate[c].xcom += pts;
}

void alien_research_points(int pts)
{
    al_month_research += pts;
}

/* ------------------------------------------------------------------ */
/* Missions */

static int new_site(int kind, int race, int region, int x, int y, int city)
{
    int i;
    for (i = 0; i < MAX_SITES; i++)
        if (g_sites[i].kind == SITE_NONE) {
            Site *s = &g_sites[i];
            memset(s, 0, sizeof *s);
            s->kind = (unsigned char)kind;
            s->race = (unsigned char)race;
            s->region = (unsigned char)region;
            s->city = (unsigned char)city;
            s->x = x;
            s->y = y;
            log_printf("ALIEN site %d kind %d race %d at %d,%d", i, kind, race, x, y);
            return i;
        }
    return -1;
}

/* Spawn timer of a wave, as OpenXcom: half the timer plus a random part. */
static long wave_timer(const Wave *w)
{
    unsigned t = w->timer / 30;
    return (long)(t / 2 + rnd(t + 1)) * 30;
}

static int mission_active(int type, int region)
{
    int i;
    for (i = 0; i < MAX_MISSIONS; i++)
        if (missions[i].type == type && (region < 0 || missions[i].region == region))
            return 1;
    return 0;
}

static int mission_start(int type, int region, int race, long delay)
{
    const MissionRule *r = &rules[type];
    Mission *m;
    int i;

    for (i = 0; i < MAX_MISSIONS && missions[i].type >= 0; i++)
        ;
    if (i == MAX_MISSIONS)
        return -1;
    m = &missions[i];
    memset(m, 0, sizeof *m);
    m->type = (signed char)type;
    m->region = (unsigned char)region;
    m->race = (unsigned char)race;
    m->countdown = delay ? delay : wave_timer(&r->wave[0]);
    m->sx = m->sy = -1;
    /* The terror city is chosen when the mission starts. */
    if (type == MIS_TERROR && !zone_point(region, r->spawn_zone, &m->sx, &m->sy, NULL))
        m->sx = m->sy = -1;
    log_printf("ALIEN mission %d type %d region %s race %s", i, type, al_regions[region].name,
               g_race_names[race]);
    return i;
}

static int pick_race(int type, int month)
{
    const MissionRule *r = &rules[type];
    int k, b = -1, total = 0, roll;

    for (k = 0; k < RACE_MONTHS && r->race_month[k] != 255; k++)
        if (r->race_month[k] <= month)
            b = k;
    if (b < 0)
        return 0;
    for (k = 0; k < N_RACES; k++)
        total += r->race_weight[b][k];
    if (!total)
        return 0;
    roll = rnd(total);
    for (k = 0; k < N_RACES; k++) {
        roll -= r->race_weight[b][k];
        if (roll < 0)
            return k;
    }
    return 0;
}

/* The regional mission table (AlienStrategy): a region by weight among
   those with missions left, then a mission type by the region's weights;
   the type is struck from the region's table, and when every region is
   used up the table starts again. */
static void table_init(void)
{
    int i, k;

    for (i = 0; i < n_regions; i++) {
        table[i] = 0;
        for (k = 0; k < TABLE_MISSIONS; k++)
            if (al_regions[i].mission_weight[k])
                table[i] |= (unsigned char)(1 << k);
    }
}

static int table_pick(const unsigned char *weights, int n, unsigned mask)
{
    int i, total = 0, roll;

    for (i = 0; i < n; i++)
        if (mask & (1u << i))
            total += weights[i];
    if (!total)
        return -1;
    roll = rnd(total);
    for (i = 0; i < n; i++)
        if ((mask & (1u << i)) && (roll -= weights[i]) < 0)
            break;
    return i;
}

static void mission_from_table(int month)
{
    unsigned char w[MAX_REGIONS];
    unsigned mask = 0;
    int i, region, type;

    for (i = 0; i < n_regions; i++) {
        w[i] = al_regions[i].weight;
        if (table[i])
            mask |= 1u << i;
    }
    region = table_pick(w, n_regions, mask);
    if (region < 0) {
        table_init();               /* no more missions to choose from: refresh */
        for (i = 0; i < n_regions; i++)
            if (table[i])
                mask |= 1u << i;
        region = table_pick(w, n_regions, mask);
        if (region < 0)
            return;
    }
    type = table_pick(al_regions[region].mission_weight, TABLE_MISSIONS, table[region]);
    if (type < 0)
        return;
    table[region] &= (unsigned char)~(1 << type);
    mission_start(type, mission_region(region), pick_race(type, month), 0);
}

void alien_month_start(int month)
{
    int i, n = 0, pick;

    if (!al_world)
        return;
    /* recurringTerror: a terror mission every month, in a region with
       cities and no terror mission running, chosen evenly. */
    for (i = 0; i < n_regions; i++)
        if (al_regions[i].n_zone[ZONE_CITY] && !mission_active(MIS_TERROR, i))
            n++;
    pick = n ? rnd(n) : -1;
    for (i = 0; i < n_regions && pick >= 0; i++)
        if (al_regions[i].n_zone[ZONE_CITY] && !mission_active(MIS_TERROR, i) && pick-- == 0)
            mission_start(MIS_TERROR, i, pick_race(MIS_TERROR, month), 150);
    if (month >= 1)
        mission_from_table(month);      /* recon */
    if (month >= 6)
        mission_from_table(month);      /* invasion */
    /* Retaliation at the X-COM base's region from month 14 (beginner) to 10
       (superhuman), or every month once X-COM has the Martian Solution;
       not while one runs there. */
    if ((month >= 14 - al_difficulty() || g_base.done[RS_MARTIAN]) && g_nbases) {
        /* targetBaseOdds 100: the region of one of the X-COM bases. */
        int region = BASE_REGION(rnd(g_nbases));
        if (!mission_active(MIS_RETALIATION, region))
            mission_start(MIS_RETALIATION, region, pick_race(MIS_RETALIATION, month), 150);
    }
}

void alien_new_game(int base_x, int base_y)
{
    int i;
    long total = 0, missing;

    memset(missions, 0, sizeof missions);
    for (i = 0; i < MAX_MISSIONS; i++)
        missions[i].type = -1;
    memset(g_sites, 0, sizeof g_sites);
    memset(al_cstate, 0, sizeof al_cstate);
    /* Country funding: fundingBase .. twice that, then adjusted so the
       total is $6M (Mod::newSave); the starting funds are that total. */
    for (i = 0; al_world && i < al_n_countries; i++) {
        al_cstate[i].funding = al_countries[i].funding_base + rnd(al_countries[i].funding_base + 1);
        total += al_cstate[i].funding;
    }
    if (al_n_countries) {
        missing = (6000 - total) / al_n_countries;
        for (i = 0, total = 0; i < al_n_countries; i++) {
            if (al_cstate[i].funding + missing >= 0)
                al_cstate[i].funding += missing;
            total += al_cstate[i].funding;
        }
        g_base.funds = total;
    }
    al_month_xcom = al_month_alien = al_month_research = 0;
    al_warned = retal_mark = 0;
    al_last_rating = 0;
    if (!al_world)
        return;
    table_init();
    i = mission_region(region_at(base_x, base_y));
    if (g_alien_test & 2) {
        /* CYDONIA (debug): the final mission researched and the Skyranger
           an Avenger, the only craft that goes to Mars (DOS). */
        g_base.cydonia = 2;
        g_craft[0].type = CT_AVENGER;
        g_craft[0].fuel = g_craft_types[CT_AVENGER].fuel;
    }
    if (g_alien_test & 1)
        new_site(SITE_BASE, 0, i, base_x + 16, base_y + 8, 255);
    /* gameStart: sectoid research at the base's region, two hours in. */
    mission_start(MIS_RESEARCH, i, 0, 120);
    alien_month_start(0);
}

/* An alien base at a land point of the region's base zone; the site or -1. */
static int build_base(const Mission *m, int *x, int *y)
{
    return land_point(m->region, ZONE_BASE, x, y)
           ? new_site(SITE_BASE, m->race, m->region, *x, *y, 255) : -1;
}

/* Infiltration complete: a country of the region (by its label) signs a
   pact at the month end and the aliens build a base in the region. */
static void infiltrate(const Mission *m)
{
    int i, x, y;

    for (i = 0; i < al_n_countries; i++)
        if (al_countries[i].region == m->region && !al_cstate[i].pact && !al_cstate[i].new_pact) {
            al_cstate[i].new_pact = 1;
            log_printf("ALIEN %s infiltrated", al_countries[i].name);
            build_base(m, &x, &y);
            break;
        }
}

static void spawn_next(int mi)
{
    Mission *m = &missions[mi];
    const MissionRule *r = &rules[m->type];
    const Wave *w = &r->wave[m->wave];
    int slot = 0, b;

    for (b = 0; m->type == MIS_RETALIATION && b < g_nbases; b++)
        if ((retal_mark >> b & 1) && BASE_REGION(b) == m->region)
            break;
    if (m->type == MIS_RETALIATION && b < g_nbases) {
        /* A base in the region is known: a battleship flies straight at
           it. */
        int x, y;
        if (!zone_point(m->region, trajs[w->traj].wp[0][0], &x, &y, NULL)) {
            x = g_bhdr[b].x;
            y = 4;
        }
        slot = geo_spawn_assault(mi, m->race, x, y, b);
    } else if (m->type != MIS_SUPPLY || m->sx >= 0)     /* no base left to supply */
        slot = geo_spawn_ufo(w->ufo, mi, m->wave, m->race);
    if (slot < 0) {
        m->countdown = 30;              /* the sky is full: try again */
        return;
    }
    if (++m->spawned >= w->count) {
        m->spawned = 0;
        m->wave++;
    }
    if (m->wave >= r->n_waves) {
        if (m->type == MIS_INFILTRATION) {
            infiltrate(m);
            m->wave = 0;                /* infiltrations go on for ever */
        } else if (m->type == MIS_BASE) {
            int x, y;
            if (build_base(m, &x, &y) >= 0)     /* the base mission scores now */
                geo_alien(x, y, r->points);
        }
    }
    if (m->wave < r->n_waves)
        m->countdown = wave_timer(&r->wave[m->wave]);
    else
        m->done = 1;
}

void alien_half_hour(int month, unsigned followed)
{
    int i;
    char line[48];

    (void)month;
    if (!al_world)
        return;
    for (i = 0; i < MAX_MISSIONS; i++) {
        Mission *m = &missions[i];
        if (m->type < 0)
            continue;
        if (!m->done) {
            m->countdown -= 30;
            if (m->countdown <= 0)
                spawn_next(i);
        } else if (!geo_mission_ufos(i))
            m->type = -1;
    }
    for (i = 0; i < MAX_SITES; i++) {
        Site *s = &g_sites[i];
        if (s->kind != SITE_TERROR)
            continue;
        if (s->minutes >= 30)
            s->minutes -= 30;
        else if (!(followed >> i & 1)) {
            /* Terror unanswered (and no craft on its way): the despawn
               penalty of the rules. */
            geo_alien(s->x, s->y, 1000);
            sprintf(line, "The aliens have left %s.", s->city < 255 ? alien_city_name(s->city)
                                                                   : al_regions[s->region].name);
            geo_alert(COL_ALERT, line);
            s->kind = SITE_NONE;
        }
    }
}

void alien_day(void)
{
    int i, k;

    if (!al_world)
        return;
    for (i = 0; i < MAX_SITES; i++) {
        Site *s = &g_sites[i];
        if (s->kind != SITE_BASE)
            continue;
        geo_alien(s->x, s->y, 5);
        /* Supply ships call on the base, 6 % a day (genMissionFreq). */
        if (rnd(100) < 6) {
            k = mission_start(MIS_SUPPLY, s->region, s->race, 0);
            if (k >= 0) {
                missions[k].sx = s->x;
                missions[k].sy = s->y;
            }
        }
    }
}

static void base_found(Site *s, const char *how)
{
    char line[48];

    s->known = 1;
    sprintf(line, "Alien base %s: %s!", how, al_world ? al_regions[s->region].name : "");
    geo_alert(COL_ALERT, line);
    if (how[0] == 'l')
        geo_popup(PU_BASE, (int)(s - g_sites), 0);     /* AlienBaseState (agents) */
}

int alien_scan(int x, int y, int range)
{
    int i, d;

    for (i = 0; i < MAX_SITES; i++) {
        Site *s = &g_sites[i];
        if (s->kind != SITE_BASE || s->known)
            continue;
        d = geo_distance(s->x, s->y, x, y);
        if (d <= range && (int)rnd(100) < 50 - d * 50 / range) {
            base_found(s, "found");
            return i;
        }
    }
    return -1;
}

void alien_month_discover(void)
{
    int i, any = 0;

    for (i = 0; i < MAX_SITES; i++)
        any |= g_sites[i].kind == SITE_BASE;
    if (!any || rnd(100) >= 20)
        return;
    for (i = 0; i < MAX_SITES; i++)
        if (g_sites[i].kind == SITE_BASE && !g_sites[i].known) {
            base_found(&g_sites[i], "located by agents");
            return;
        }
}

void alien_base_destroyed(int site)
{
    int i;

    for (i = 0; i < MAX_MISSIONS; i++)
        if (missions[i].type == MIS_SUPPLY && missions[i].sx == g_sites[site].x
            && missions[i].sy == g_sites[site].y)
            missions[i].sx = missions[i].sy = -1;
}

void alien_terror_now(void)
{
    int tries, r, x, y, k;
    unsigned char city;
    char line[48];

    for (tries = 0; al_world && tries < 50; tries++) {
        r = rnd(n_regions);
        if (!zone_point(r, ZONE_CITY, &x, &y, &city))
            continue;
        k = new_site(SITE_TERROR, rnd(2), r, x, y, city);
        if (k < 0)
            return;
        g_sites[k].minutes = 12 * 60;
        g_sites[k].known = 1;
        sprintf(line, "Terror site: %s!", city < 255 ? alien_city_name(city) : al_regions[r].name);
        geo_alert(COL_ALERT, line);
        geo_popup(PU_TERROR, k, 0);
        return;
    }
}

int alien_mission_type(int mi)
{
    return mi >= 0 && mi < MAX_MISSIONS ? missions[mi].type : -1;
}

/* ------------------------------------------------------------------ */
/* Retaliation */

static const Traj *ufo_traj(int mi, int wave, const Mission **m, const MissionRule **r);

int alien_retal_scanner(int mi, int wave, int wp)
{
    const Mission *m;
    const MissionRule *r;
    const Traj *t = ufo_traj(mi, wave, &m, &r);

    /* wp is the waypoint it flies to: the one reached last is wp - 1. */
    return t && m->type == MIS_RETALIATION && wp > 2 && wp <= t->n
           && t->wp[wp - 1][0] != ZONE_OUT;
}

void alien_base_spotted(int b)
{
    if (!(retal_mark >> b & 1))
        log_printf("ALIEN the X-COM base %s has been located", g_bhdr[b].name);
    retal_mark |= 1 << b;
}

void alien_base_lost(int b)
{
    unsigned low = retal_mark & ((1 << b) - 1);
    retal_mark = (unsigned char)(low | (retal_mark >> (b + 1)) << b);
}

void alien_assault_done(int mi, int b, int battle, int won)
{
    if (battle)
        retal_mark &= ~(1 << b);
    if (mi < 0 || mi >= MAX_MISSIONS || missions[mi].type < 0)
        return;
    if (won) {
        geo_drop_mission(mi);
        missions[mi].type = -1;
    } else if (!missions[mi].done)
        missions[mi].countdown = 30L * (48 + rnd(401));
}

void alien_shot_down(int mi, int race)
{
    int region;

    if (mi >= 0 && mi < MAX_MISSIONS && missions[mi].type >= 0 && !missions[mi].done)
        missions[mi].countdown += 30L * (48 + rnd(401));
    if (!al_world || (int)rnd(100) >= 4 + 4 * al_difficulty())
        return;
    if (!g_nbases)
        return;
    region = mi >= 0 && mi < MAX_MISSIONS && missions[mi].type >= 0
             && (int)rnd(100) < 50 - 6 * al_difficulty() ? missions[mi].region
             : BASE_REGION(rnd(g_nbases));
    if (race < 0 || race > 4)
        race = rnd(5);
    if (!mission_active(MIS_RETALIATION, region))
        mission_start(MIS_RETALIATION, region, race, rules[MIS_RETALIATION].wave[0].timer);
}

/* ------------------------------------------------------------------ */
/* UFO flight */

static const Traj *ufo_traj(int mi, int wave, const Mission **m, const MissionRule **r)
{
    if (!al_world || mi < 0 || mi >= MAX_MISSIONS || missions[mi].type < 0)
        return NULL;
    *m = &missions[mi];
    *r = &rules[(*m)->type];
    if (wave >= (*r)->n_waves)
        return NULL;
    return &trajs[(*r)->wave[wave].traj];
}

int alien_waypoint(int mi, int wave, int wp, int type, int *x, int *y, int *speed, int *alt)
{
    const Mission *m;
    const MissionRule *r;
    const Traj *t = ufo_traj(mi, wave, &m, &r);
    int zone, k;

    if (!t || wp >= t->n)
        return 0;
    zone = t->wp[wp][0];
    if (m->type == MIS_SUPPLY && wp > 0 && m->sx >= 0) {
        *x = m->sx;                     /* supply ships fly to their base */
        *y = m->sy;
    } else if (m->type == MIS_TERROR && r->wave[wave].objective && zone == r->spawn_zone
               && m->sx >= 0) {
        *x = m->sx;                     /* the terror city */
        *y = m->sy;
    } else if (!(wp + 1 < t->n && t->wp[wp + 1][1] == 0 ? land_point(m->region, zone, x, y)
                 : zone_point(m->region, zone, x, y, NULL))
               && !zone_point(m->region, 0, x, y, NULL)) {
        *x = rnd(320);
        *y = 16 + rnd(128);
    }
    if (type <= 0 || type >= N_UFO_TYPES)
        type = 1;
    /* The first leg keeps the spawn point's speed and height; a UFO lifting
       off towards a ground waypoint flies very low. */
    k = wp == 1 ? 0 : wp;
    *alt = t->wp[k][1] ? t->wp[k][1] : 1;
    /* Knots to 1/16 pixel per hour: 21600 nautical miles = 320 pixels. */
    *speed = (int)((long)ufo_rules[type].speed * t->wp[k][2] / 100 * 32 / 135);
    if (*speed < 16)
        *speed = 16;
    return wp + 1 >= t->n ? 2 : 1;
}

int alien_reached(int mi, int wave, int wp, int x, int y, int on_land, int *minutes)
{
    const Mission *m;
    const MissionRule *r;
    const Traj *t = ufo_traj(mi, wave, &m, &r);
    char line[48];
    int s;

    if (!t || wp + 1 >= t->n)
        return REACH_GONE;              /* the way out: it leaves Earth */
    if (t->wp[wp + 1][1])
        return REACH_ON;
    /* The next waypoint is on the ground: the UFO lands here. */
    if (m->type == MIS_TERROR && r->wave[wave].objective && t->wp[wp][0] == r->spawn_zone) {
        unsigned char city = 255;
        int i;
        /* The city of the mission site. */
        for (i = 0; i < al_regions[m->region].n_zone[ZONE_CITY]; i++) {
            const Rect *q = &rects[al_regions[m->region].zone[ZONE_CITY] + i];
            if (x >= (int)q->x0 && x <= (int)q->x1 && y >= q->y0 && y <= q->y1)
                city = q->city;
        }
        s = new_site(SITE_TERROR, m->race, m->region, x, y, city);
        if (s >= 0) {
            g_sites[s].minutes = (4 + rnd(7)) * 60;
            g_sites[s].known = 1;
            sprintf(line, "Terror site: %s!", city < 255 ? alien_city_name(city)
                                                         : al_regions[m->region].name);
            geo_alert(COL_ALERT, line);
            geo_popup(PU_TERROR, s, 0);         /* MissionDetectedState */
        }
        geo_alien(x, y, r->points);
        return REACH_GONE;
    }
    /* groundTimer x 5 seconds on land; at sea it takes off at once. */
    *minutes = on_land ? (int)(t->ground * 5UL / 60) : 0;
    return REACH_LAND;
}

void alien_lifted(int mi, int x, int y)
{
    const MissionRule *r;
    if (!al_world || mi < 0 || mi >= MAX_MISSIONS || missions[mi].type < 0)
        return;
    r = &rules[missions[mi].type];
    /* Base missions score when the base is built; infiltration's pact score
       is a special case (OpenXcom AlienMission::addScore). */
    if (r->points && missions[mi].type != MIS_BASE && missions[mi].type != MIS_INFILTRATION)
        geo_alien(x, y, r->points);
}

/* ------------------------------------------------------------------ */
/* Month end */

long alien_funding(void)
{
    long total = 0;
    int i;

    for (i = 0; al_world && i < al_n_countries; i++)
        total += al_cstate[i].funding;
    return total;
}

/* ------------------------------------------------------------------ */
/* Saved games */

#define FIELDS(X) X(missions) X(g_sites) X(al_cstate) X(al_month_xcom) X(al_month_alien) \
                  X(al_warned) X(al_month_research) \
                  X(table) X(retal_mark) X(al_last_rating)

int alien_save(FILE *f)
{
    int ok = 1;
#define PUT(v) ok = ok && fwrite(&(v), sizeof(v), 1, f) == 1;
    FIELDS(PUT)
#undef PUT
    return ok;
}

int alien_load(FILE *f)
{
    int ok = 1;
#define GET(v) ok = ok && fread(&(v), sizeof(v), 1, f) == 1;
    FIELDS(GET)
#undef GET
    return ok;
}
