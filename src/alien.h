/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ALIEN_H
#define ALIEN_H

#include <stdio.h>

/* The alien side of the geoscape: missions that fly waves of UFOs, terror
   sites and alien bases, regions and countries with funding, and the
   month-end report. Rules come from the world pack ("XAL1" section, see
   tools/world.py build_alien). Map positions are map pixels (320 x 160). */

/* Mission types, in the order of the rules. */
enum {
    MIS_RESEARCH, MIS_HARVEST, MIS_ABDUCTION, MIS_INFILTRATION, MIS_BASE,
    MIS_TERROR, MIS_RETALIATION, MIS_SUPPLY, N_MISSION_TYPES
};

/* UFO types 1..8 (0 unused). */
#define N_UFO_TYPES 9

enum { SITE_NONE, SITE_TERROR, SITE_BASE };

typedef struct {
    unsigned char kind;         /* SITE_* */
    unsigned char race;         /* 0 sectoid .. 4 ethereal */
    unsigned char region;
    unsigned char known;        /* alien bases: found by X-COM */
    unsigned char city;         /* terror: city, 255 none */
    unsigned char pad;
    int x, y;
    int minutes;                /* terror: time left */
} Site;

#define MAX_SITES 10
extern Site g_sites[MAX_SITES];

extern const char *const g_race_names[6];

/* Debug options: 1 (ALIENBASE) a new campaign starts with a hidden alien
   base next to the X-COM base, 2 (CYDONIA) with Cydonia located. */
extern int g_alien_test;

/* World pack section; 0 = ok. The tables are freed during battles. */
int alien_load_world(FILE *f);
void alien_free_world(void);

const char *alien_region_name(int x, int y);
int alien_region(int x, int y);         /* regions.rul index, -1 none */
const char *alien_city_name(int city);
/* Rectangle r of the zones (r from 0 on): its corner, and the city's name
   for a city's point, else ""; NULL after the last (the map's labels). */
const char *alien_place(unsigned r, int *x, int *y);
/* A country's label point and name (0 .. al_n_countries - 1; NULL past). */
const char *alien_country_label(int i, int *x, int *y);
/* The country borders (globe.rul's polylines): per line n and n x (x, y) in
   1/4 map pixels (x from 180 W, going on past the date line), ended by a 0;
   NULL when the pack has none. */
const int *alien_borders(void);

/* Campaign. month = months since the start (0 = first month). The new
   game sets the countries' funding (OpenXcom: fundingBase..2x, adjusted
   to $6M in all) and the starting funds. */
void alien_new_game(int base_x, int base_y);
/* Half-hour tick: mission countdowns and UFO waves, terror sites
   (followed: bit per site a troop transport flies to). */
void alien_half_hour(int month, unsigned followed);
void alien_day(void);
long alien_funding(void);               /* the countries' funding this month, $K */
void alien_month_start(int month);      /* the month's new missions */
/* Month-end report (after the month's funding was paid, months = months
   played including the one just ended): the rating, the countries'
   funding decisions and pacts, game over. Returns 1 if the Council
   disbands X-COM. */
int alien_month_report(int months, long funds, long costs, int month, int year);
void alien_funding_screen(void);
/* The Graphs screen (graphs.c, GraphsState): graphs_month at the month
   end, before the report (costs and funding paid now, the month's score);
   graphs_screen until it is closed (month 0-11 now). */
void graphs_month(long costs, long funding, int score);
void graphs_screen(int month, int year, int score);
/* Month end: X-COM agents find one hidden alien base, 20 % of months. */
void alien_month_discover(void);

/* Activity at (x, y) for region/country funding: X-COM's (pts may be
   negative: losses) or the aliens' (alien = 1). */
void alien_credit(int x, int y, int pts, int alien);
/* Research points of the month (count for the Council's rating only). */
void alien_research_points(int pts);

/* Debug option TERROR: a terror site in a random city at once. */
void alien_terror_now(void);

/* Craft patrolling at (x, y) (no destination), every 10 minutes: finds
   hidden alien bases within range pixels, 50 % minus 50 % of the distance
   / range (OpenXcom time10Minutes). Returns the site found, or -1. */
int alien_scan(int x, int y, int range);

/* UFO flight. Waypoint wp of a UFO's trajectory: target (x, y), speed
   (1/16 pixel per hour) and altitude (0 ground .. 4 very high) on the way
   there. Returns 0 when the trajectory is over, 2 for the last leg
   (leaving), else 1. */
int alien_waypoint(int mission, int wave, int wp, int type, int *x, int *y, int *speed,
                   int *alt);

enum { REACH_ON, REACH_LAND, REACH_GONE };
/* The UFO reached waypoint wp at (x, y): fly on, land (*minutes on the
   ground; 0 at sea) or vanish (left Earth, or it became a terror site). */
int alien_reached(int mission, int wave, int wp, int x, int y, int on_land, int *minutes);
/* A landed UFO takes off: the mission scores. */
void alien_lifted(int mission, int x, int y);
int alien_mission_type(int mission);

/* Retaliation. A UFO of mission mi flying to waypoint wp may look for
   X-COM bases (retaliation missions, after their second waypoint, not in
   the way-out zone). */
int alien_retal_scanner(int mission, int wave, int wp);
/* X-COM base b was found: the next wave of a retaliation mission in its
   region is a battleship assault on it. */
void alien_base_spotted(int b);
void alien_base_lost(int b);            /* base b is gone: the marks move down */
/* The assault battleship reached base b: the next wave waits 1-9 days;
   won = the base defence was won: the mission ends (its UFOs are removed
   by geo_drop_mission). The mark is cleared when the battle starts. */
void alien_assault_done(int mission, int b, int battle, int won);
/* A UFO of the mission was shot down (crashed or destroyed): the next wave
   waits 1-9 days more, and a retaliation mission may start. */
void alien_shot_down(int mission, int race);
/* X-COM destroyed alien base site s: its supply missions lose their target. */
void alien_base_destroyed(int site);

int alien_save(FILE *f);
int alien_load(FILE *f);

/* Provided by geo.c. */
int geo_spawn_ufo(int type, int mission, int wave, int race);  /* slot or -1 */
int geo_spawn_assault(int mission, int race, int x, int y, int base);  /* battleship at it */
int geo_mission_ufos(int mission);      /* UFOs of a mission still on the map */
void geo_drop_mission(int mission);     /* removes a mission's UFOs */
void geo_alert(int colour, const char *s);  /* message that stops the clock */
int geo_land(int x, int y);             /* 1 if (x, y) is land */
int geo_distance(int x0, int y0, int x1, int y1);   /* map pixels */

#endif
