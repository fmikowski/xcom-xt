/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PATH_H
#define PATH_H

/* Directions as in X-COM: 0 = north (-y), clockwise to 7 = north-west.
   Vertical moves (grav lifts) use DIR_UP and DIR_DOWN. */
#define DIR_UP      8
#define DIR_DOWN    9

extern const signed char g_dir_dx[8], g_dir_dy[8];

#define TIDX(x, y, z) ((((unsigned)(z) * g_my + (unsigned)(y)) * g_mx) + (unsigned)(x))

/* 1 if a unit can stand at the tile; *h gets its standing height in
   pixels (24 per level). */
int tile_standable(int x, int y, int z, int *h);

/* Cost in TUs of one step from (x,y,z) in direction dir, or -1 if blocked.
   On success *nz is the destination level. Ignores units. */
int step_cost(int x, int y, int z, int dir, int *nz);

/* Reachability from a unit's tile within max_tu, avoiding other units.
   Afterwards path_cost() gives the TU cost to reach a tile (or 0xFFFF). */
void path_search(int unit, int max_tu);
unsigned path_cost(int x, int y, int z);

/* Steps from the searched unit to the tile, written to dirs (start first).
   Returns the step count, 0 if unreachable. */
int path_build(int x, int y, int z, unsigned char *dirs, int max);

#ifdef AMB_PATH
/* Applies a step to a position (only ai.c's AMB_PATH uses it). */
void path_apply(int dir, int *x, int *y, int *z, int nz);
#endif

int path_init(void);    /* allocates and builds tables; 0 = ok */
/* 1 while bs_restore calls path_init and los_init: what its io_state reads
   whole right after (wf's WF_BIG, the sight flags) is not built. */
extern unsigned char g_path_load;

/* Fog of war: a bit in the walk flags, one byte per tile like the map
   (render.h g_seen); path_init clears it for a new map. */
#define SEEN_BIT    0x10
unsigned char *path_fog_table(void);

/* A* search for unit's path to (x, y, z) (OpenXcom Pathfinding::calculate),
   stopping when it gets there: 1 and *bx.. = the tile, then path_build
   gives the path. A path costs at most 254 TU and slack TU more than the
   estimate (4 TU a step, 6 diagonal); without one, 0 and *bx.. = the tile
   the search reached nearest the goal (path_build works for it).
   path_cost of other tiles is meaningless after it. */
int path_search_to(int unit, int x, int y, int z, int slack, int *bx, int *by, int *bz);

/* With g_path_mis = t + 1 path_search_to searches for a missile aimed at
   unit t (Pathfinding::calculate with target t and no TU limit): through the
   air at its level (a flyer's missile also up and down), t's tile not barred,
   greedily toward the goal, each tile reached once (PathfindingNode::
   getTUCost(missile) is 0). path_doors_asm, once before and once after, makes
   the closed doors walls meanwhile (Pathfinding::isBlocked: missiles do not
   pathfind through them). */
extern int g_path_mis;
void __cdecl path_doors_asm(unsigned walkseg, unsigned ntiles, const void *map, const void *mcd);

/* Parameter block for path_search_asm (pathasm.asm). goal 0xFFFF: every
   tile within max_tu (Dijkstra); else A* toward the goal tile, buckets
   by g + h - h0 up to kmax, best = the expanded tile with the lowest h. */
struct PathParams {
    unsigned walkseg, searchseg, n, mx, my, mz, mxy, start, max_tu;
    unsigned char occ, fly;
    unsigned goal, gx, gy, kmax, h0;
    unsigned char hs, mis;      /* mis: a missile's search (g_path_mis): 1 of a flyer,
                                   2 of a walker (no flight up or down) */
    unsigned best;
};

/* The reference search in C, for BENCH alone (vbench.c: in its section, to
   leave the battle core): g_path_asm 0 takes it instead of pathasm.asm.
   path_step_idx: step_cost on a known tile index. */
extern int g_path_asm;
void path_search_c(struct PathParams *p);
int path_step_idx(unsigned idx, int x, int y, int z, int dir, int *nz, unsigned *nidx);

/* Checksum of the last search (for comparing the C and assembly versions). */
unsigned path_checksum(void);

/* Rebuilds a tile's walk data after its terrain changed. */
void path_update_tile(int x, int y, int z);

#endif
