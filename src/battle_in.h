/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BATTLE_IN_H
#define BATTLE_IN_H

/* Inside the battlescape: shared by battle.c (rules and turns, the core
   overlay section the alien turn needs) and bplayer.c (the player's
   commands, setup and end, a section of its own: it and ai.c take turns
   next to the core in the overlay area). */

#include "render.h"
#include "unit.h"

#define KEY_ESC     0x01
#define KEY_UP      0x48
#define KEY_DOWN    0x50
#define KEY_LEFT    0x4B
#define KEY_RIGHT   0x4D
#define KEY_HOME    0x47
#define KEY_PGUP    0x49
#define KEY_END     0x4F
#define KEY_PGDN    0x51
#define KEY_F1      0x3B
#define KEY_F2      0x3C

#define COL_TITLE   15
#define COL_UNIT    14
#define COL_INFO    7
#define COL_GOOD    10
#define COL_BAD     12
#define COL_TRACER  14

extern int bt_sel;              /* selected X-COM unit */
extern int bt_turn;
extern int bt_reserve;          /* X-COM TU reserve: 0 none, else SHOT_* + 1 */
extern int bt_result;           /* 0 = running, 1 = won, -1 = lost, -2 = quit,
                                   BT_LOAD / BT_ABANDON (bsave.h) */
extern int bt_request;          /* player_turn returned for the options window
                                   or the inventory (BR_*) */
#define BR_OPTIONS      1
#define BR_INVENTORY    2
/* bact.c, next to bplayer: throwing (prime 0) or priming the grenade in
   the left hand, a soldier's taken into it first (the cursor's tile). */
void grenade_act(int s, int prime);
/* bact.c: turning a unit towards a tile; one already facing it opens the
   door in front (a right click, Space). */
void bt_turn_to(int u, int x, int y);
/* bact.c: the medi-kit of soldier s (in a hand) on the unit or body at a
   tile next to it, or on itself (the M key). */
void bt_medikit(int s, int tx, int ty, int tz);
extern unsigned bt_obj_left;    /* objective parts left (alien base control, brain) */
extern unsigned char bt_obj_done;   /* the base control is destroyed */
extern int bt_vlast;            /* V (next visible alien): the last one shown */

/* battle.c */
void msg(int colour, const char *s);
#define wait_ms(ms) timer_pace(timer_read(), ms, 0)    /* (timer.h) */
int unit_visible(const Unit *u);
void check_result(void);
void psi_release(int u);
void hud_status(void);
void redraw_unit(int u);
void flush_fog(void);
int next_soldier(int from);
void update_vision(int u, int reveal);
void morale_change(Unit *u, int change);
int shoot(int s, int tx, int ty, int tz, int mode);
int throw_grenade(int s, int tx, int ty, int tz);   /* 1 if thrown */
void bt_hit_reset(void);                    /* ends every unit's hit state (PSI_HIT) */
int reaction_fire(int mover);
int reserve_ok(const Unit *u, int cost);
void walk(int u, int tx, int ty, int tz);
void wake_up(int i);            /* (in batui.c) */

void log_msg(int colour, const char *s);         /* logged, not shown */
/* A tile's parts changed: paths, sight lines and who may fall
   (terrain_parts), then the tile's picture (render.h). A macro, not a
   function: the core has no bytes for the wrapper. */
void terrain_parts(int x, int y, int z);
#define terrain_changed(x, y, z)    (terrain_parts(x, y, z), render_tile(x, y, z))
/* bdoor.c: TileEngine::unitOpensDoor for a step (rclick 0) or a right
   click (1) in direction dir: -1 no door (or it could not open); else 0
   swinging, 1 UFO, 4 short of TU, 5 refused by the reserve, | 8 when it
   opened; the door's tile and part in g_door_x/y/z/p, the TU charged in
   g_door_tu. */
int door_try(Unit *un, int dir, int rclick);
extern int g_door_x, g_door_y, g_door_z, g_door_p, g_door_tu;
void kill_unit(int victim, int shooter, int zombify);
void knock_out(int victim);
extern int panicking;           /* panicking units act: no TU reserve */
void bt_fall(void);             /* units over holes fall (after terrain changes) */
/* TileEngine::explode (dtype DT_*, thrower -1 none); quiet_blast: no
   sound, the view stays (the crash's blasts). */
void explode(int cx, int cy, int cz, int power, int dtype, int radius, int thrower);
extern int quiet_blast;

/* Missile waypoints (BA_LAUNCH): Blaster Launcher and Hovertank/Launcher
   take up to 9 (items.rul); shoot with bt_nwp set flies the missile along
   bt_wp at 60 % accuracy (aliens 55 %) and clears it. */
#define BT_MAX_WP   9
#define BT_WAYPOINTS(u) ((u)->weapon == IT_BLASTER || (u)->weapon == IT_HOVER_LAUNCHER)
#define BT_WP_FLOOR 128         /* in a waypoint's level: the same tile as the one before */
extern unsigned char bt_wp[BT_MAX_WP][3];
extern int bt_nwp;

/* bview.c (a section of its own, beside the alien turn): what X-COM must see
   outside its own turn. bv_blast: the view on a blast (with its picture, if
   there is room, for its time), its sound; 1 if its time is still to keep. */
void bv_round(const int *o, const int *h, int n);
void bv_melee(int t);
void bv_fallen(int u);
int bv_blast(int cx, int cy, int cz);

/* bturn.c (a section of its own: between turns) */
void bt_side_start(int faction);    /* TU, energy, bleeding, panic */
void bt_side_end(int faction);      /* primed grenades, UFO doors */
void bt_new_round(void);            /* smoke, fire, the next turn */
void bt_crash_blasts(void);         /* the crashed UFO's power sources */
/* Terrain explosions (Tile::destroy, TileEngine::checkForTerrainExplosions):
   an explosive part destroyed adds its tile to g_boom (map.h) with its
   strength; bt_boom_take gives a blast's power p at a tile on that list
   the stronger of the two and takes it off (detonate uses the tile's
   explosive value, the strongest); bt_boom_run sets them off one after the
   other once the blast is over (thrower: who caused it). */
void bt_boom_add(const unsigned char *t, int rec);
int bt_boom_take(int x, int y, int z, int p);
void bt_boom_run(int thrower);
void log_loads(const char *what);   /* LOG: file reads since the last call */

/* bact.c (a section of its own next to bplayer): the hand menu
   (panel_actions, panel.h) and the grenade timer. The hand menu's actions
   without a key of their own: */
#define BK_PANIC    '['         /* Psi-Amp: panic unit */
#define BK_CONTROL  ']'         /* Psi-Amp: mind control */
int bu_timer(void);             /* PrimeGrenadeState: 0-23, -1 = cancelled */
int bu_waypoints(const Unit *u);         /* BA_LAUNCH: waypoints into bt_wp, 0 = cancelled */
void bu_wp_marks(void);         /* their numbers on the map */
unsigned bp_map_key(void);      /* bplayer.c: the map's key wait for that screen */
/* The view scrolls towards the screen edge under the pointer, at the Game
   Options' scroll speed: for a click there, a step and more while the
   button stays down; else (Auto-Scroll) one step when its time has come.
   0 if the pointer is not at an edge. */
int bu_scroll(int click);       /* (in batui.c) */
void bu_motion_scan(int s);      /* the motion scanner in a hand (batui.c) */

/* bplayer.c */
void bp_set_cursor(int x, int y, int z);
int bp_pick_tile(int px, int py, int z, int *tx, int *ty);
void player_turn(void);
void psi_release_all(void);
void abort_evacuate(void);
void dump_map(void);
void battle_start(void);
void battle_view(void);
void battle_over(int objective_done);

#endif
