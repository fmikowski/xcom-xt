/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef VOX_H
#define VOX_H

/* Shots and sight voxel by voxel, as DOS (OpenXcom's TileEngine:
   calculateLine with voxel checks, voxelCheck, canTargetUnit,
   canTargetTile, getOriginVoxel, getSightOriginVoxel). A tile is 16 x 16
   voxels across and 24 high; each MCD record's shape is 12 layers of
   LOFTEMPS.DAT templates (2 voxels each), the "XLF1" pack section
   (tools/convert.py loft_section), units are their LOFTEMPS shape between
   their feet and their height. */

/* What a voxel holds (OpenXcom VoxelType). */
#define V_EMPTY     (-1)
#define V_FLOOR     0
#define V_WESTWALL  1
#define V_NORTHWALL 2
#define V_OBJECT    3
#define V_UNIT      4
#define V_OUT       5

extern unsigned char *g_vox;    /* the XLF1 blob of the mission pack (pak.c) */
extern unsigned g_vox_stamp;    /* counts the packs loaded (pak.c) */

/* The unit the lines and checks ignore (the shooter, the viewer); -1 none. */
extern int g_vox_excl;
#define VOX_NO_UNITS    (-2)    /* g_vox_excl: units are not checked */

/* After vox_line: the voxel where the line stopped (or its end). */
extern int *g_vox_hit;

/* After vox_line: the voxel one main step before the last one (where an
   explosive round goes off, ProjectileFlyBState: two trajectory steps
   back; the hit itself when there is none). */
void vox_prev(int *p);

/* The straight voxel line from o to t (TileEngine::calculateLine with
   voxel checks): the first voxel that holds something (V_*), V_EMPTY if
   it reaches t. ext: the line goes on past t until it hits something or
   leaves the map (Projectile::applyAccuracy, extendLine). */
int vox_line(const int *o, const int *t, int ext);

/* A throw's arc (TileEngine::calculateParabola) from voxel o towards
   voxel t for unit u (not hit by it), curvature curv / 1024: the first
   voxel that holds something (V_*), V_EMPTY if the arc fell out under
   the map; land = the tile of the voxel before it (where the item drops). */
int vox_arc(int u, const int *o, const int *t, int curv, int *land);

/* A throw of item by unit s at tile (tx, ty, tz): -1 too far
   (ProjectileFlyBState::validThrowRange), 0 no arc lands there or an
   object nothing walks through stands there (validateThrow), else the
   lowest curvature (1/1024) whose arc does. */
int vox_throw(int s, int item, int tx, int ty, int tz);

/* Where that throw lands for the thrower's accuracy acc (percent;
   Projectile::calculateThrow): the tile c. */
void vox_throw_land(int s, int tx, int ty, int tz, int curv, int acc, int *c);

/* A voxel of unit u that the line from o reaches (canTargetUnit): 1 and
   scan = that voxel, else 0. */
int vox_target_unit(const int *o, int u, int *scan);
/* Exact mode (voxx.c): the same for unit u thought to stand at (px, py, pz),
   its real body still in the way where it stands; with hyp a line that gets
   there counts. */
int vox_target_at(const int *o, int u, int px, int py, int pz, int hyp);

/* A voxel of tile part `part` (PART_*) of tile (x, y, z) that the line from
   o reaches (canTargetTile): 1 and scan, else 0. In the section of
   bact.c (src/voxtile.c): the player's turn only. */
int vox_target_tile(const int *o, int x, int y, int z, int part, int *scan);

/* Where unit u fires from at the tile (tx, ty): its muzzle
   (getOriginVoxel; throw: 1 voxel higher). */
void vox_origin(int u, int tx, int ty, int throw_, int *o);

/* Where unit u looks from (getSightOriginVoxel: the middle of its tile,
   one voxel under the top of its head; 2x2 units the middle of their four
   tiles, at the top). */
void vox_eyes(int u, int *o);

/* 1 if unit a sees unit b by DOS's test (TileEngine::visible without its
   range and smoke): canTargetUnit from a's eyes, other units in the way.
   Kept by pair until either unit moves or kneels or the terrain changes
   (g_vox_seen_ok = 0). */
int vox_sees(int a, int b);
extern unsigned char g_vox_seen_ok;

/* The height of unit u's body in voxels (kneeling: lower). */
int vox_height(int u);

/* The terrain height of a tile (Tile::getTerrainLevel: 0 or negative). */
int vox_level(int x, int y, int z);

#endif
