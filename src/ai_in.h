/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AI_IN_H
#define AI_IN_H

/* Inside the AI: state shared by ai.c (the alien and civilian turns, an
   overlay section of its own) and aihook.c (what the battle core calls in
   every turn, the player's too: steps, hits, falls; it stays with the
   core, so a soldier's walk never loads the AI). */

typedef struct {
    unsigned char mode, flags;
    unsigned char mind;             /* intelligence | aggression << 4 */
    unsigned short from, to;        /* patrol route nodes */
} AiUnit;

extern AiUnit ai_unit[];
extern unsigned char ai_spotted[];  /* turns since an alien saw the unit */
extern unsigned char bt_wp[][3];    /* battle.c: the next launch's waypoints */
extern int bt_nwp;
/* ai.c for aiway.c */
extern int ai_searched;
int ai_valid(int i, int civs);
int ai_efficacy(int tx, int ty, int tz, int radius, int grenade);
extern unsigned char ai_wx[], ai_wy[], ai_wz[];
/* aiway.c (AIModule::wayPointAction): unit u's way for a blaster bomb to the
   target it picks into bt_wp: how many waypoints, 0 for none or if they do
   not get there; ai_way_unit: a unit stands on the first one. go: sets up the launch (bt_nwp, combat's destination, the log): its shot mode. */
extern int ai_way_unit;
int ai_way(int u, int go);
/* voxx.c, tests (BOOMAIM): the nearest explosive terrain part on u's level into
   ai_wx[2], ai_wy[2] if u can snap-shoot: 1. */
int ai_boom_aim(int u);
extern unsigned char ai_watch[];    /* walker: enemies in sight at the start */
extern int ai_walker, ai_no_stop;

void ai_free_node(unsigned n);

#endif
