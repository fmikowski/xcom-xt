/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AI_H
#define AI_H

/* The alien side's (and the civilians') battle AI, after the DOS game as
   OpenXcom's AIModule reproduces it: patrol along the map's route nodes,
   ambush, combat and escape modes chosen by odds from each unit's
   aggression and intelligence, knowledge of X-COM units limited to those
   seen in the last turns, the DOS choice of attack (snap / aimed / auto by
   distance, grenades, melee, psionics) and mind-controlled soldiers fighting
   for the aliens. */

/* Battle start: per-unit AI state, nobody spotted yet. */
void ai_battle_start(void);

/* A unit that joined the battle (zombie, chryssalid): fresh AI state. */
void ai_unit_new(int u);

/* The alien turn (and the mind-controlled soldiers'); turn = battle turn. */
void ai_turn(int turn);

/* The civilians' turn on terror sites. */
void ai_civilians(void);

/* After each step of unit u (walk): aliens watching a moving X-COM unit or
   civilian learn where it is; an AI unit that spots a new enemy stops.
   Returns 1 if the walk should stop. */
int ai_step(int u);

/* Unit u was hit by unit s (shot, blast, melee): an alien hit by X-COM
   knows where the shooter is. */
void ai_hit(int u, int s);

/* Unit u dies or falls unconscious: a soldier under alien control is
   X-COM's again; route nodes near a fallen alien become dangerous. */
void ai_unit_out(int u);

/* End of the battle: soldiers under alien control come back. */
void ai_battle_end(void);

/* Spawning (unit.c): a free route node for an alien of the given rank
   (nodes ranked as in the DOS game; outside: scout nodes outside the UFO
   only), or for a civilian (rank -1). Returns the node or -1. */
int ai_spawn_node(int rank, int big, int outside);

/* TU a walk must leave unspent (aliens reserving time for reaction fire
   while they patrol); walk() in battle.c honours it. */
extern int g_walk_reserve;

/* Services of src/battle.c. */
int bt_over(void);                          /* battle decided */
void bt_say(int shown, int colour, const char *s);  /* HUD + log, or log only */
void bt_walk(int u, int x, int y, int z);   /* along the last path search */
int bt_shoot(int u, int x, int y, int z, int mode);
int bt_throw(int u, int x, int y, int z);    /* 1 if thrown (rl_throw_tu paid) */
int bt_throw_ok(int u, int x, int y, int z); /* grenade, TU and range for it */
int bt_strike(int u, int t);                /* melee_attack: 1 if struck */
int bt_line_of_fire(int s, int t);          /* nobody in the way of a shot */
void bt_redraw(int u);
void bt_face(int u, int dir);               /* turns, 1 TU per eighth */
void bt_flush(void);                        /* fog and HUD after the turn */
void bt_view_on(void);                      /* the map instead of Hidden Movement */

#endif
