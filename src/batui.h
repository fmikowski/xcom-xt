/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BATUI_H
#define BATUI_H

/* Battle screens kept out of the battle overlay section (they share the
   inventory's section): the minimap, the options and abort windows, and
   the night colours of the control panel. */

/* Packs of the current battle (main.c fills them in as it loads them):
   mission, race, terror units, armours, HWPs (src/hwp.c, BU_HWP_PACKS +
   type); "" = none. Their trailers (tools/convert.py) hold the minimap
   pictures and the colour use. */
#define BU_PACKS    11
#define BU_HWP_PACKS 6
extern char g_pack_names[BU_PACKS][13];

/* Night battles (EGA): picks palette registers the map hardly uses to show
   the panel's bright colours, sets g_night_pal and g_uk_cmap. bu_day undoes
   it after the battle. */
void bu_night(void);
void bu_day(void);

/* The minimap (MiniMapState) of level *z, centred on *x, *y. Returns 1 if
   a tile was picked (then in *x, *y, *z), 0 if it was just closed. The
   caller redraws the map and the panel. */
int bu_minimap(int *x, int *y, int *z);

/* The options window is src/bsave.c's (bs_options: PauseState with save,
   load, abandon and sound). */

/* The abort window (AbortMissionState, TAC00.SCR): 1 = abort. */
int bu_abort(void);

/* A mind probe in a hand (bplayer.c): its screen on the unit at the
   cursor, 1 if shown (here beside the screen: memory, see build.ps1). */
int bu_mind_probe(int s);

/* The minimap's frames and view (batui.c), drawn in 256 colours by
   src/batuiv.c (VGA). */
extern unsigned char *g_mm;
extern int g_mm_x0, g_mm_y0, g_mm_z, g_mm_anim;
void bu_mm_vga(int ty0, int ty1, int c0, int c1);

#endif
