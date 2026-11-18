/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CANVAS_H
#define CANVAS_H

/* The battle view's canvas (src/canvas.c, docs/design.md "The canvas"):
   with EMS or XMS, what the view showed is kept there by map position and
   levels drawn, and a redraw of a place whose tiles, units, items, effects
   and fog have not changed since is a copy instead of a drawing. Off
   without EMS / XMS (plain 640 KB machines: render.c asks nothing). */

extern int g_cv;                /* 1: on for this battle (render.c, bplayer.c ask) */

#define CV_READ     0   /* r = cells c0, r0, c1, r1 (render_redraw): what the canvas
                           has is copied to the back buffer; returns 0 if that was
                           all, else 1 with r the part still to draw */
#define CV_STORE    1   /* r drawn: kept in the canvas */
int cv_op(int op, int *r);
/* render_tile (terrain_changed): the tile's parts changed. */
void cv_tile(int x, int y, int z);

/* main.c: on = 1 before a battle's first redraw (takes the memory when
   there is enough, LOG "canvas: ..."), 0 after it (gives it back). */
void cv_battle(int on);

/* bplayer.c, while the player's turn waits for a key: one block around
   the view drawn into the canvas (a scroll or jump there is then a copy). */
void cv_idle(void);

#endif
