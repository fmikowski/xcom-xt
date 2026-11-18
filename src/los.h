/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LOS_H
#define LOS_H

#define VIEW_RANGE  20

/* X-COM's sight range (VIEW_RANGE by day, 9 at night); aliens always see
   VIEW_RANGE. */
extern int g_xcom_range;

/* A tile-by-tile 3D line walk. */
typedef struct {
    int x, y, z;                /* current tile */
    int fx, fy, fz;             /* 8.8 fixed-point position */
    int sx, sy, sz;             /* 8.8 step */
    int steps, i;
    unsigned idx;               /* tile index of x, y, z */
} Ray;

/* What stopped a ray step (ray_next result). */
#define RAY_OK          0
#define RAY_OUT         1       /* left the map */
#define RAY_FLOOR       2       /* floor between levels */
#define RAY_WESTWALL    3
#define RAY_NORTHWALL   4
#define RAY_OBJECT      5       /* object in the entered tile */

/* Starts at (x0,y0,z0) aiming through (x1,y1,z1); with extend the ray keeps
   going past the target up to max_steps. */
void ray_start(Ray *r, int x0, int y0, int z0, int x1, int y1, int z1, int max_steps);

/* Moves to the next tile. Returns RAY_OK, or what blocks entering it;
   r->x/y/z then name the tile that holds the blocking part. Returns RAY_OUT
   at the end. A step to a diagonal neighbour meets the walls in the order
   the straight line crosses the tile edges (either order through a
   corner). */
int ray_next(Ray *r);

/* Per-tile sight flags (TIDX order). The low bits come from the terrain;
   src/effects.c owns the high ones. */
#define LF_WEST     0x01        /* west wall blocks sight and shots */
#define LF_NORTH    0x02
#define LF_OBJECT   0x04
#define LF_FLOOR    0x08        /* floor blocks sight between levels */
#define LF_SMOKE    0x10        /* thick smoke (7+): drawn denser */
#define LF_HAZE     0x20        /* any smoke (drawn thin without LF_SMOKE) */
#define LF_FIRE     0x40        /* burning */
#define LF_DEV      0x80        /* armed grenade or flare on the ground */
#define LF_FX       0xF0        /* all effect bits */
extern unsigned char *g_los_flags;

int los_init(void);                         /* 0 = ok (in spawn.c: the setup) */
void los_update_tile(int x, int y, int z);  /* after terrain changes */

/* 1 if unit a can see unit b (range, 90-degree view cone, clear line). */
int los_unit_sees(int a, int b);
/* 0 if Unit pointer b is more than VIEW_RANGE tiles from a along x or y:
   los_unit_sees's first rejection, made before the call (it then returns
   0 with nothing done, so a call skipped this way changes nothing). In
   bytes: a distance of 236 or more passes too, the call then rejects it. */
#define LOS_NEAR(a, b)  ((unsigned char)((b)->x - (a)->x + VIEW_RANGE) <= 2 * VIEW_RANGE \
                         && (unsigned char)((b)->y - (a)->y + VIEW_RANGE) <= 2 * VIEW_RANGE)

/* 1 if the line between two tiles is clear (ignores view cones): no
   wall, floor or object that stops sight, and not too much smoke on the
   way for the 20-tile view (TileEngine::visible: smoke / 3 a voxel). */
int los_clear(int x0, int y0, int z0, int x1, int y1, int z1);
/* Exact mode (286 and up, or EXACTAI; design.md "Alien AI"): sight of units and
   the AI's lines of fire are DOS's voxel lines alone, without the tile line;
   of that line sight keeps the smoke (voxx.c). */
extern unsigned char g_exact;
int los_smoke(int x0, int y0, int z0, int x1, int y1, int z1);

/* The same, ignoring smoke: whether a blast or fire gets from one tile to
   the other (an object in the target tile does not stop it). */
int los_reach(int x0, int y0, int z0, int x1, int y1, int z1);

/* Marks tiles seen by an X-COM unit in g_seen; returns 1 if new tiles were
   revealed. */
int los_reveal(int unit);

/* Screen rectangle around the tiles revealed since los_box_reset();
   0 if none. */
void los_box_reset(void);
int los_box_screen(int *x0, int *y0, int *x1, int *y1);

#endif
