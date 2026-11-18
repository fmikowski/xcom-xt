/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSI_H
#define PSI_H

/* Psionics as in the DOS game: the Psionic Laboratory trains soldiers'
   psi skill once a month (psi.c, geoscape); in battle the Psi-Amp panics
   or mind-controls aliens and the Mind Probe shows a unit's stats
   (psibat.c, battlescape). */

#include "unit.h"

/* Geoscape (psi.c) */
void psi_screen(void);              /* base screen: assign soldiers to training */
int psi_trainees(void);             /* soldiers in psi training */
const char *psi_month(void);        /* month end: training; a message or NULL */

/* Battlescape (psibat.c) */
#define PSI_AMP_TU      25          /* flat, as the DOS Psi-Amp */
#define PROBE_TU_PCT    50          /* percent of the unit's TU */

int psi_strength(const Unit *u);    /* aliens: their race's strength */
int psi_skill(const Unit *u);

/* Chance in percent that a psi attack by a on v succeeds (control: mind
   control, else panic), and the roll itself. DOS / OpenXcom: attack =
   strength * skill / 50 - distance + 0..55 against defence = strength +
   10 + skill / 5, plus 20 against mind control. */
int psi_chance(const Unit *a, const Unit *v, int control);
int psi_roll(const Unit *a, const Unit *v, int control);

/* Full-screen stats of unit u (the caller redraws the battlescape). */
void psi_probe_screen(const Unit *u);

#endif
