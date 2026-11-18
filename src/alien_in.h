/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ALIEN_IN_H
#define ALIEN_IN_H

/* Inside the alien side: the world pack tables and the countries' state,
   shared by alien.c (hourly overlay section) and report.c (the month-end
   report and the funding screen, in the rarely used section). */

#define MAX_REGIONS     16
#define MAX_COUNTRIES   20
#define MAX_WAVES       8
#define RACE_MONTHS     5
#define N_RACES         6

#pragma pack(1)
typedef struct { unsigned short x0, x1; unsigned char y0, y1, city, pad; } Rect;
typedef struct {
    char name[16];
    unsigned char weight, home, mission_weight[5], pad;
    unsigned short area, n_area, zone[6], n_zone[6];
} Region;
typedef struct {
    char name[14];
    unsigned short funding_base, funding_cap, label_x;
    unsigned char label_y, region;
    unsigned short area, n_area;
} Country;
typedef struct { unsigned short ground; unsigned char n, pad, wp[10][3]; } Traj;
typedef struct { unsigned char ufo, count, traj, objective; unsigned short timer; } Wave;
typedef struct {
    unsigned short points;
    unsigned char objective, spawn_zone, n_waves, pad;
    Wave wave[MAX_WAVES];
    unsigned char race_month[RACE_MONTHS], race_weight[RACE_MONTHS][N_RACES];
} MissionRule;
typedef struct { unsigned short speed, score; } UfoRule;
#pragma pack()

typedef struct {
    long funding;               /* $K a month */
    long change;                /* at the last month end */
    int xcom, alien;            /* activity this month */
    unsigned char pact;         /* 0 none, 1 signed, 2 signed this month */
    unsigned char new_pact;     /* signs at the month end */
} CountryState;

extern unsigned char *al_world;         /* NULL while freed (battles) */
extern const Region *al_regions;
extern const Country *al_countries;
extern int al_n_countries, al_n_regions;
extern CountryState al_cstate[MAX_COUNTRIES];
extern long al_month_xcom, al_month_alien, al_month_research;
extern unsigned char al_warned;
extern int al_last_rating;
int al_difficulty(void);

#endif
