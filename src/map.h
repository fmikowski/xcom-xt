/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MAP_H
#define MAP_H

/* Battlescape map: g_mx * g_my * g_mz tiles of 4 parts (MCD indices). */

/* Largest map the engine handles (50 x 50 x 4); tables are sized for it. */
#define MAP_MAX_TILES 10000U

extern unsigned g_mx, g_my, g_mz;
extern unsigned char *g_map;

#define TILE(x, y, z) \
    (g_map + (((((unsigned)(z) * g_my + (unsigned)(y)) * g_mx) + (unsigned)(x)) << 2))

#define IN_MAP(x, y, z) \
    ((unsigned)(x) < g_mx && (unsigned)(y) < g_my && (unsigned)(z) < g_mz)

/* Top-left tile of the placed craft and UFO blocks. */
extern int g_craft_x, g_craft_y, g_ufo_x, g_ufo_y;

void rng_seed(unsigned long seed);
unsigned rnd(unsigned n);       /* 0 .. n-1 */
extern unsigned long g_rng;     /* its state (battle saves) */

/* Returns 0 on success. */
int map_generate(void);
void map_reveal(void);          /* what X-COM knows at the start (after path_init; spawn.c) */

/* The map buffer (largest map), kept for the whole run. 0 = ok. */
int map_alloc(void);

/* Base defence: BASE_W x BASE_W map blocks of the base pack (0xFF = earth),
   row by row; map_generate lays them out instead of a crash site while set. */
extern const unsigned char *g_base_layout;
/* Recovery from the battle map (DebriefingState::prepareDebriefing): the
   tile parts of each MCD special type 2-11 (UFO power source .. alien
   habitat) left after the battle, for base_loot. */
void map_count_loot(unsigned *loot);
/* A crashed UFO's power sources blew up 3 times in 4
   (BattlescapeGenerator::explodePowerSources), here before the sprites
   are read: the object becomes its wreck, and its place goes into
   g_boom (strength 0) for the blast before the first turn (bturn.c
   bt_crash_blasts). */
void map_crash_power(void);
/* Blasts to come (bturn.c): tile index (TIDX) low, high byte, strength
   (an explosive part destroyed, MCD HE_Strength; 0 a crash power source).
   g_max_boom entries: MAX_BOOM, up to BOOM_TOP with more memory (lim_init,
   src/startup.c). */
#ifndef MAX_BOOM                /* test builds may set them (XT_DEFS) */
#define MAX_BOOM 16
#ifndef BOOM_TOP
#define BOOM_TOP 64
#endif
#endif
#ifndef BOOM_TOP
#define BOOM_TOP MAX_BOOM
#endif
extern unsigned char (__near *g_boom)[3];
extern int g_n_boom, g_max_boom;
/* Base defence: the 6 x 6 blocks holding X-COM base objects (bit y * 6 + x;
   SavedBattleGame::calculateModuleMap), at the start and after the
   battle (main.c); a facility whose block lost them all is destroyed
   (facil_wreck). */
void map_base_modules(unsigned char *mask);
extern unsigned char g_xbase_had[5], g_xbase_now[5];

/* Route nodes of the placed blocks (RMP files, for the alien AI and the
   deployment), in map coordinates; built by map_generate. */
typedef struct {
    unsigned char x, y, z;      /* x = 255: a node outside its block (unused) */
    unsigned char rank;         /* node rank (low nibble) | NODE_ flags */
    unsigned char pf;           /* patrol desirability | spawn priority << 4 */
    unsigned char link[5];      /* block node index, 251-254 exits, 255 none */
    unsigned char target;       /* 1: a base defence target (RMP "reserved" 5) */
} MapNode;

#define NODE_RANK(n)    ((n)->rank & 0x0F)
#define NODE_SMALL      0x10    /* small units only */
#define NODE_FLY        0x20    /* flying units only */
#define NODE_DANGER     0x40    /* an alien fell here */
#define NODE_TAKEN      0x80    /* an alien is on its way there */
#define NO_NODE         0xFFFF

extern MapNode *g_node;
extern unsigned g_n_node;

/* Map grid (10x10 cells) of the largest map. Per cell the terrain block
   placement (+ 1, under any UFO or craft overlay) whose route node exits
   join, as OpenXcom's map segments; the first node of each placement. */
#define MAP_GRID        8
extern unsigned char g_node_seg[MAP_GRID][MAP_GRID];
extern unsigned short g_node_first[MAP_GRID * MAP_GRID + 1];

/* Link k (0-4) of node n as a node index, NO_NODE if none. An exit to a
   neighbouring block leads to the first node there with the opposite exit,
   as OpenXcom joins them. */
unsigned map_node_link(unsigned n, int k);

void map_free_nodes(void);

/* Zeroed allocation starting at offset 0 of its own segment, so assembly
   can index it with a plain 16-bit offset. NULL if out of memory. */
void *alloc_para(unsigned size);

#endif
