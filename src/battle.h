/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BATTLE_H
#define BATTLE_H

/* Runs a battle until it is won, lost or abandoned.
   Returns 1 won, -1 lost, -2 aborted. */
int battle_run(void);

/* Night mission (set before battle_run): X-COM sees less, darker view. */
extern int g_night;

/* Terror sites: civilians alive at the end of a won battle. */
extern int g_civ_saved;

/* Debugging option SEEALL: no fog of war, all units visible. */
extern int g_see_all;

/* Debugging option SHOTLOG: MAPDUMP.BIN at the start, every shot's tile path
   and every terrain change in the LOG (checked by tools/shotcheck.py). */
extern int g_shot_log;

/* DOS's battlescape Game Options (the technical supplement's Battlescape
   Options screen, src/bsave.c): scroll speed 1-5, scroll type (1 trigger:
   the left button at a screen edge, 2 auto: the pointer at an edge), fire
   speed, X-COM and alien movement speed 1-6 (1 slowest). Kept in
   XCOMXT.CFG (DOS: GEOGRAPH\DP_PREFS), read at each battle's setup. */
enum { BO_SCROLL, BO_STYPE, BO_FIRE, BO_XCOM, BO_ALIEN, BO_N };
extern unsigned char g_bopt[BO_N];
/* Fire Speed 1-6: the ms a round takes per tile, as DOS (src/battle.c). */
extern const unsigned g_tile_ms[7];
#define BOPT_FILE   "XCOMXT.CFG"

extern unsigned char g_view_hold;   /* tests (HOLDVIEW n): src/bview.c */

#endif
