/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BRIEF_H
#define BRIEF_H

/* The mission briefing before a campaign battle (BriefingState): the
   mission type's background, title, target, craft and the original
   briefing text (UI_BRIEFING in the UI pack). */

/* Set by the geoscape before a UFO site battle: 1 = crash site (UFO crash
   recovery), 0 = landed UFO (UFO ground assault). */
extern int g_brief_crashed;
/* The site's number ("CRASH SITE-1"): the geoscape sets it before the battle,
   0 = none (quick battles). */
extern int g_brief_site;

/* Shows the briefing for mission pack pak (the type follows from its name
   and g_brief_crashed); craft: the transport's name, NULL for none.
   Returns after a key or click on OK. */
void brief_show(const char *pak, const char *craft);

#endif
