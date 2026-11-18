/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RENDER_H
#define RENDER_H

extern int g_cam_x, g_cam_y;    /* screen position of tile (0,0,0); x multiple of 8 */
extern int g_view_z;            /* highest level drawn */
extern int g_all_layers;        /* draw the levels above it too (panel button) */
extern int g_cur_x, g_cur_y, g_cur_z, g_cursor_on;
extern unsigned char *g_seen;   /* per tile: SEEN_BIT (path.h) = discovered by X-COM;
                                   NULL = no fog */

void render_center(int x, int y, int z);

/* Redraws and shows the whole map view. */
void render_full(void);

/* Redraws and shows the screen area a tile's sprites can cover. */
void render_tile(int x, int y, int z);

/* The cursor moved from (ox, oy, oz) to g_cur_x/y/z: puts back what it
   covered and draws it at the new place, only its box (rcursor.c). */
void render_cursor_move(int ox, int oy, int oz);

/* A unit's step between neighbouring tiles (big: 2x2) seen from start to
   end: both areas as one rectangle, drawn once (rcursor.c). */
void render_step(int big, int ox, int oy, int oz, int x, int y, int z);

/* The cursor's sprites: its back half, its front half and a mark drawn
   over it (0: none). pak.c sets the red box, bplayer.c the cursor of the
   action (OpenXcom Map::setCursorType). The frames from g_pak.cursor_base
   (tools/convert.py CURSOR_PICK): */
extern unsigned g_cur_spr[3];
#define CF_BACK         0       /* the box: red */
#define CF_BACK_UNIT    1       /* yellow: a unit in sight on the tile */
#define CF_FRONT        2
#define CF_FRONT_UNIT   3
#define CF_AIM          4       /* the aim crosshairs (back and front) */
#define CF_AIM_UNIT     5
#define CF_PSI          6       /* marks over the box */
#define CF_WAYPOINT     7
#define CF_THROW        8
/* Draws the cursor's back half (front 0) or its front half and mark at a
   tile's sprite box (rcursor.c, resident: the battle core bounds the
   overlay area). */
void render_cursor_sprite(int front, int sx, int sy);

/* The selected unit's arrow (OpenXcom Map::drawTerrain): on, its top left
   pixel in the map's picture (TILE_SX / TILE_SY without the camera) and
   the unit's level (shown up to the view's level). bplayer.c places it
   (arrow_to). Every present of the view (gfx.c gfx_present_cells) draws it
   into the back buffer first, over the cells shown (src/arrow.asm
   render_arrow, resident), so it lies over everything, and the redraws,
   the canvas and the cursor's kept box never hold it in their own
   pictures. */
extern int g_arrow[4];
void render_arrow(int c0, int r0, int cols, int rows);

/* The cursor's box (cells c0, r0, c1, r1) kept without the cursor while
   g_cs_ok; a redraw over it clears g_cs_ok. */
extern int g_cs_ok, g_cs_box[4];

/* Redraws window cells c0..c1 (even), rows r0..r1 in the back buffer only. */
void render_redraw(int c0, int r0, int c1, int r1);

/* Redraws and shows a screen rectangle (pixels, clipped to the view). */
void render_rect(int x0, int y0, int x1, int y1);

/* 1 if the tile sits comfortably inside the view. */
int render_tile_visible(int x, int y, int z);

/* Moves the camera, reusing what is already drawn where possible. */
void render_scroll(int cam_x, int cam_y);

/* Scrolls so the tile is centred if it is near or past the view edge. */
void render_follow(int x, int y, int z);

/* The battlescape's animation frame, 0-7, for a hovertank's propulsion
   (OpenXcom BattlescapeState: a step every 100 ms; src/bplayer.c steps it
   while the player's turn waits for a key). */
extern unsigned char g_anim;

/* Covered sprites are not drawn (packs with cover data) unless g_occ_off
   (BENCH compares both); g_occ_count counts the floors and tiles left out. */
extern int g_occ_off;
extern unsigned g_occ_count;

/* Screen position of a tile's sprite box. */
#define TILE_SX(x, y)       ((((x) - (y)) << 4) + g_cam_x)
#define TILE_SY(x, y, z)    ((((x) + (y)) << 3) - (z) * 24 + g_cam_y)

#endif
