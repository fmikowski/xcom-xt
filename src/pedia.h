/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PEDIA_H
#define PEDIA_H

/* The UFOpaedia: reference pages unlocked by research, texts from
   UFOPAED.PAK (tools/pedia.py), read only while the screen is open. */

/* UFO types recovered so far (bit per type, 1 small scout ..), saved with
   the game: their pages unlock. */
extern unsigned g_pedia_ufos;

/* The UFOpaedia screens, until Esc on the section list. They use the
   whole screen: the caller redraws its own (the geoscape after U does). */
void pedia_screen(void);

/* The article of a research topic, if there is one (View Reports). */
void pedia_article(const char *topic);

/* Text from p on, words wrapped at w pixels, from y until y_end (the
   original small font); returns the next y. The first skip lines are laid
   out but not drawn (a later page of a long article); *more (if given) says
   whether text is left after y_end. */
int pedia_flow(int x, int y, int w, const char *p, int y_end, int colour, int skip, int *more);

#endif
