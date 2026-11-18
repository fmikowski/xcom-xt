/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GEO_H
#define GEO_H

/* The geoscape: a flat world map with time, UFOs, the base radar, the
   Skyranger, interception, score and monthly funding. Battles are fought
   through the callback, which returns the battle result (1 won, -1 lost,
   -2 aborted) and fills in aliens killed and soldiers lost. */

typedef int (*BattleFn)(const char *mission_pak, int *aliens_killed, int *soldiers_lost);

/* Race of the last battle (0 sectoid .. 4 ethereal), set by the callback. */
extern int g_last_race;

/* Race for the next battle, set by the geoscape before calling the
   callback: 0 sectoid, 1 floater, 2 snakeman, 3 muton, 4 ethereal,
   5 mixed, -1 random. */
extern int g_next_race;

/* Score for an event at map pixel (x, y), credited to the region and
   country there: X-COM activity (pts < 0 for losses: soldiers, craft), or
   alien activity (UFOs, missions, sites; pts > 0). */
void geo_score(int x, int y, int pts);
void geo_alien(int x, int y, int pts);
/* Funds as DOS prints them ($4 112 000): k in $K, d10 in $10; buf 16 bytes. */
void geo_money(char *buf, long k, int d10);
/* Research points (score of the month; the Council's rating only). */
void geo_research(int pts);

/* The DOS pop-ups (GeoscapeState::popup) raised during a time step,
   shown when it ends: a UFO detected (UFO slot, id), tracking lost (id),
   a craft at its way point or low on fuel (craft), a terror site or an
   alien base located by agents (site), a transport ready to land (craft),
   a craft whose UFO went off radar (craft, the UFO's id), a craft at its
   base short of ammunition or Elerium-115 (craft, the item: CraftErrorState). */
enum { PU_DETECTED, PU_LOST, PU_PATROL, PU_FUEL, PU_TERROR, PU_BASE, PU_LANDING, PU_TARGET,
       PU_CRAFTERR };
void geo_popup(int kind, int idx, int id);

/* DEBUG option: geoscape key X lands the selected UFO under the Skyranger. */
extern int g_debug;
/* Debugging (option TERROR): a terror site appears in the first hour
   (alien_terror_now). */
extern int g_terror_now;

/* 0 = ok; loads WORLDE.PAK or WORLDC.PAK. */
int geo_init(void);

/* Runs the campaign until the player quits or it is won or lost. */
void geo_run(BattleFn battle);

/* Front end (menu.c). g_geo_loaded: geo_run continues the campaign loaded
   by geo_load_body instead of starting a new one. g_geo_abandon, after
   geo_run: 1 = back to the start menu, 0 = quit to DOS. */
#include <stdio.h>
extern int g_geo_loaded, g_geo_abandon;
int geo_save_body(FILE *f);         /* 1 = ok */
int geo_load_body(FILE *f);
/* The geoscape's date and time (month 0-11); during a battle they stay
   as at its start (the slot headers of saves, menu.c and bsave.c). */
extern int year, month, day, hour, minute;

#endif
