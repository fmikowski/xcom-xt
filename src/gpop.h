/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GPOP_H
#define GPOP_H

/* gpop.c: the DOS completion pop-ups over the geoscape (an overlay section
   of its own). Each returns 1 for its second button (View Reports,
   Allocate Research / Manufacture, Go to Base), 0 for OK; the NewPossible
   windows -1 when there is nothing new (no window). ln: lines for the
   lists (N_TOPICS rows). Work on the current base. */
int gpop_research(int topic);
int gpop_new_research(int topic, char (*ln)[41]);
int gpop_new_manufacture(int topic, char (*ln)[41]);
int gpop_production(int kind, int id, int base);
/* ItemsArrivingState: the arrivals of the step (the POP_ARRIVAL events from
   index k of the pop-up list, all marked POP_USED); returns the base of the
   last for Go to Base, else -1 (OK). */
int gpop_arrivals(int k);

/* DebriefingState: the title by mission (DB_*) and result, the stats with
   a quantity (score per line), UFO recovery, the total and the rating. */
enum { DB_UFO, DB_TERROR, DB_ALIEN_BASE, DB_BASE, DB_CYDONIA };
void gpop_debrief(int mission, int res, int craft_lost, const void *stats);

/* Its points (before the roster drops the dead): soldiers and HWPs lost;
   aliens killed and captured, corpses (after base_loot: inv_recover
   decided the captures, in unit order); race 0 sectoid .. 5 mixed. The
   soldiers and HWPs of the squad (not mind-controlled aliens) alive after
   the battle: none = the transport is lost with them (DOS). stats: a
   DebStats (base.h). */
int gpop_soldier_points(int res, void *stats);
int gpop_alien_points(int won, int race, void *stats);
int gpop_survivors(void);

#endif
