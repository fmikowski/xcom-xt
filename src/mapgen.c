/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Map generation (crash sites, cities, bases, alien bases, Cydonia) and the
   route nodes of the placed blocks. Only needed while a battle is set up:
   an overlay section of its own with the pack loader (build.ps1). */

#include <stdlib.h>
#include <string.h>
#include "pak.h"
#include "map.h"
#include "input.h"
#include "path.h"

#define GRID_MAX MAP_GRID

/* Per 10x10 cell: 0 free, else the number of the placement covering it. */
static unsigned char used[GRID_MAX][GRID_MAX];
static unsigned gx, gy;
static struct { unsigned char block, cx, cy; } placed[GRID_MAX * GRID_MAX];
static unsigned n_placed;

static int collect_nodes(void);

static int read_failed;

/* What X-COM knows of the map at the start (map_reveal, in spawn.c: it runs
   between los_init and units_spawn, so the setup's section stays loaded). */
unsigned g_rev_w, g_rev_h;      /* the craft's block in tiles, 0: no craft */
unsigned char g_lift[2][2];     /* the alien base's entry lifts (tile x, y) */
unsigned g_n_lift;

static void place(unsigned block, unsigned cx, unsigned cy, int overlay)
{
    const Block *b = &g_pak.block[block];
    const unsigned char *src = pak_block_tiles(block);
    unsigned x, y, z, row = b->sx * 4;     /* bytes in a block row */

    busy_tick();                /* the busy pointer (input.h) */
    if (!src) {
        read_failed = 1;
        return;
    }
    for (z = 0; z < b->sz && z < g_mz; z++)
        for (y = 0; y < b->sy; y++) {
            /* A block row is sx tiles side by side in the map: one address. */
            unsigned char *t = TILE(cx * 10, cy * 10 + y, z);
            if (!overlay) {
                memcpy(t, src, row);
                src += row;
                continue;
            }
            for (x = 0; x < row; x++, src++, t++) {
                /* BattlescapeGenerator::loadMAP: a UFO or craft tile with
                   any part removes the terrain's object (crops, cacti),
                   its empty floor and walls keep the terrain's. At a
                   tile's first part: the tile's four parts as two words. */
                if (!(x & 3) && (((const unsigned *)src)[0] | ((const unsigned *)src)[1]))
                    t[PART_OBJECT] = 0;
                if (*src)
                    *t = *src;
            }
        }
    if (n_placed < GRID_MAX * GRID_MAX) {
        placed[n_placed].block = (unsigned char)block;
        placed[n_placed].cx = (unsigned char)cx;
        placed[n_placed].cy = (unsigned char)cy;
        n_placed++;
    }
    for (y = 0; y < b->sy / 10; y++)
        for (x = 0; x < b->sx / 10; x++) {
            used[cy + y][cx + x] = (unsigned char)n_placed;
            if (!overlay)
                g_node_seg[cy + y][cx + x] = (unsigned char)n_placed;
        }
}

static int free_spot(unsigned w, unsigned h, unsigned *ox, unsigned *oy)
{
    unsigned spots[GRID_MAX * GRID_MAX];
    unsigned n = 0, x, y, i, j;

    for (y = 0; y + h <= gy; y++)
        for (x = 0; x + w <= gx; x++) {
            int ok = 1;
            for (j = 0; j < h && ok; j++)
                for (i = 0; i < w; i++)
                    if (used[y + j][x + i])
                        ok = 0;
            if (ok)
                spots[n++] = y * GRID_MAX + x;
        }
    if (!n)
        return 0;
    i = spots[rnd(n)];
    *ox = i % GRID_MAX;
    *oy = i / GRID_MAX;
    return 1;
}

static int place_on_landing_zone(unsigned block, int *tx, int *ty)
{
    const Block *b = &g_pak.block[block];
    unsigned w = b->sx / 10, h = b->sy / 10, cx, cy, i, j;

    if (!free_spot(w, h, &cx, &cy))
        return 0;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            place(g_pak.landing[rnd(g_pak.n_landing)], cx + i, cy + j, 0);
    place(block, cx, cy, 1);
    *tx = cx * 10;
    *ty = cy * 10;
    if (tx == &g_craft_x) {
        g_rev_w = b->sx;
        g_rev_h = b->sy;
    }
    return 1;
}

static unsigned char uses[64];

/* Fills every free grid cell from the pack's fill list, each block at most
   three times while others are left. */
static void fill_rest(void)
{
    unsigned cx, cy;

    for (cy = 0; cy < gy; cy++)
        for (cx = 0; cx < gx; cx++) {
            unsigned pick, tries;
            if (used[cy][cx])
                continue;
            for (tries = 0; tries < 50; tries++) {
                pick = g_pak.fill[rnd(g_pak.n_fill)];
                if (uses[pick] < 3)
                    break;
            }
            uses[pick]++;
            place(pick, cx, cy, 0);
        }
}

static void place_big_blocks(unsigned count)
{
    unsigned k, bx, by;

    for (k = 0; k < count && g_pak.n_big; k++)
        if (free_spot(2, 2, &bx, &by))
            place(g_pak.big[rnd(g_pak.n_big)], bx, by, 0);
}

/* A road along a grid column (vertical: north-south) or row, avoiding the
   craft; crossing blocks where it meets a road already laid. */
static unsigned char road_col = 0xFF, road_row = 0xFF;

static int lay_road(int vertical)
{
    unsigned spots[GRID_MAX], n = 0, i, k, len = vertical ? gy : gx;

    /* Columns 1..4 (rows 1..4) as in the original script, clear of the craft. */
    for (i = 1; i < (vertical ? gx : gy); i++) {
        int ok = 1;
        for (k = 0; k < len && ok; k++) {
            unsigned x = vertical ? i : k, y = vertical ? k : i;
            if (used[y][x] && !(vertical ? (y == road_row) : (x == road_col)))
                ok = 0;
        }
        if (ok)
            spots[n++] = i;
    }
    if (!n)
        return 0;
    i = spots[rnd(n)];
    for (k = 0; k < len; k++) {
        unsigned x = vertical ? i : k, y = vertical ? k : i;
        int cross = vertical ? y == road_row : x == road_col;
        place(cross ? g_pak.road[2] : g_pak.road[vertical ? 1 : 0], x, y, 0);
    }
    if (vertical)
        road_col = (unsigned char)i;
    else
        road_row = (unsigned char)i;
    return 1;
}

/* mapScripts.rul URBAN: the craft, then a north-south road (50 %), else an
   east-west one (50 %), else both; four 20x20 blocks; weighted fill. */
static int urban_generate(void)
{
    road_col = road_row = 0xFF;
    if (!place_on_landing_zone(g_pak.craft_block, &g_craft_x, &g_craft_y))
        return 1;
    g_ufo_x = g_ufo_y = -1;
    if (!(rnd(2) && lay_road(1)) && !(rnd(2) && lay_road(0))) {
        lay_road(1);
        lay_road(0);
    }
    place_big_blocks(g_pak.n_big_blocks);
    fill_rest();
    return read_failed || collect_nodes();
}

#define BASE_GRID   6
#define FILL        0xFF
#define IS_FAC(i, j) (g_base_layout[(j) * BASE_GRID + (i)] != FILL)

/* Two neighbouring blocks of one hangar (XBASE_16-19: top left, top right,
   bottom left, bottom right) need no tunnel between them. */
static int same_hangar(unsigned a, unsigned b, int east)
{
    if (a < 16 || a > 19 || b < 16 || b > 19)
        return 0;
    return east ? (a == 16 && b == 17) || (a == 18 && b == 19)
                : (a == 16 && b == 18) || (a == 17 && b == 19);
}

/* Lays out the base's facility blocks, then digs tunnels three tiles wide
   between neighbouring facilities, as OpenXcom's XBASE map script does. The
   pack's last block holds the walls that line the tunnels. The squad starts
   at the lift ("craft"), the aliens in the hangar farthest from it ("UFO"). */
static int map_generate_base(void)
{
    /* The wall block is read first: place() reuses the read buffer. */
    const unsigned char *wb = pak_block_tiles(g_pak.n_blocks - 1);
    unsigned char walls[4];
    unsigned i, j, k;
    int best = -1;

    if (gx != BASE_GRID || gy != BASE_GRID || !wb)
        return 1;
    memcpy(walls, wb, 4);
    g_craft_x = g_craft_y = 0;
    for (j = 0; j < gy; j++)
        for (i = 0; i < gx; i++) {
            unsigned b = g_base_layout[j * BASE_GRID + i];
            place(b == FILL ? g_pak.fill[0] : b, i, j, 0);
            if (b == 0) {
                g_craft_x = i * 10;
                g_craft_y = j * 10;
            }
        }
    g_ufo_x = g_craft_x;
    g_ufo_y = g_craft_y;
    for (j = 0; j < gy; j++)
        for (i = 0; i < gx; i++) {
            unsigned b = g_base_layout[j * BASE_GRID + i];
            int d = abs((int)i * 10 - g_craft_x) + abs((int)j * 10 - g_craft_y);
            if (b == FILL)
                continue;
            /* Any facility will do, a hangar beats it. */
            if (b == 16)
                d += 100;
            if (d > best) {
                best = d;
                g_ufo_x = i * 10;
                g_ufo_y = j * 10;
            }
            if (i + 1 < gx && IS_FAC(i + 1, j)
                && !same_hangar(b, g_base_layout[j * BASE_GRID + i + 1], 1)) {
                for (k = 3; k < 6; k++) {
                    unsigned char *t = TILE(i * 10 + 9, j * 10 + k, 0);
                    t[PART_WESTWALL] = t[PART_OBJECT] = 0;
                    t = TILE(i * 10 + 10, j * 10 + k, 0);
                    t[PART_WESTWALL] = t[PART_OBJECT] = 0;
                }
                TILE(i * 10 + 9, j * 10 + 3, 0)[PART_NORTHWALL] = walls[PART_NORTHWALL];
                TILE(i * 10 + 9, j * 10 + 6, 0)[PART_NORTHWALL] = walls[PART_NORTHWALL];
            }
            if (j + 1 < gy && IS_FAC(i, j + 1)
                && !same_hangar(b, g_base_layout[(j + 1) * BASE_GRID + i], 0)) {
                for (k = 3; k < 6; k++) {
                    unsigned char *t = TILE(i * 10 + k, j * 10 + 9, 0);
                    t[PART_NORTHWALL] = t[PART_OBJECT] = 0;
                    t = TILE(i * 10 + k, j * 10 + 10, 0);
                    t[PART_NORTHWALL] = t[PART_OBJECT] = 0;
                }
                TILE(i * 10 + 3, j * 10 + 9, 0)[PART_WESTWALL] = walls[PART_WESTWALL];
                TILE(i * 10 + 6, j * 10 + 9, 0)[PART_WESTWALL] = walls[PART_WESTWALL];
            }
        }
    return 0;
}

/* digTunnel of OpenXcom's alien base scripts: opens a 3-tile passage on the
   ground level wherever two different blocks meet, with new side walls. */
static void dig_tunnels(void)
{
    const unsigned char *r = g_pak.tunnel;      /* west, north, corner, floor */
    unsigned i, j, k;

    for (j = 0; j < gy; j++)
        for (i = 0; i < gx; i++) {
            if (i + 1 < gx && used[j][i + 1] != used[j][i]) {
                for (k = 3; k < 6; k++) {
                    unsigned char *t = TILE(i * 10 + 9, j * 10 + k, 0);
                    t[PART_WESTWALL] = t[PART_OBJECT] = 0;
                    if (k > 3)
                        t[PART_NORTHWALL] = 0;
                    if (r[3])
                        t[PART_FLOOR] = r[3];
                    t = TILE(i * 10 + 10, j * 10 + k, 0);
                    t[PART_WESTWALL] = t[PART_OBJECT] = 0;
                }
                if (r[1]) {
                    TILE(i * 10 + 9, j * 10 + 3, 0)[PART_NORTHWALL] = r[1];
                    TILE(i * 10 + 9, j * 10 + 6, 0)[PART_NORTHWALL] = r[1];
                }
                if (r[2] && !TILE(i * 10 + 10, j * 10 + 3, 0)[PART_NORTHWALL])
                    TILE(i * 10 + 10, j * 10 + 3, 0)[PART_NORTHWALL] = r[2];
            }
            if (j + 1 < gy && used[j + 1][i] != used[j][i]) {
                for (k = 3; k < 6; k++) {
                    unsigned char *t = TILE(i * 10 + k, j * 10 + 9, 0);
                    t[PART_NORTHWALL] = t[PART_OBJECT] = 0;
                    if (k > 3)
                        t[PART_WESTWALL] = 0;
                    if (r[3])
                        t[PART_FLOOR] = r[3];
                    t = TILE(i * 10 + k, j * 10 + 10, 0);
                    t[PART_NORTHWALL] = t[PART_OBJECT] = 0;
                }
                if (r[0]) {
                    TILE(i * 10 + 3, j * 10 + 9, 0)[PART_WESTWALL] = r[0];
                    TILE(i * 10 + 6, j * 10 + 9, 0)[PART_WESTWALL] = r[0];
                }
                if (r[2] && !TILE(i * 10 + 3, j * 10 + 10, 0)[PART_WESTWALL])
                    TILE(i * 10 + 3, j * 10 + 10, 0)[PART_WESTWALL] = r[2];
            }
        }
}


/* Route nodes of all placed blocks into g_node, in map coordinates: from
   the full "XRT1" / "XRT2" records, else from older packs' short ones (map scripts:
   every block; crash sites: the UFO's only), without links. */
static int collect_nodes(void)
{
    unsigned short start[64];
    const unsigned char *raw = g_pak.routes ? g_pak.routes + 1 : g_pak.block_nodes, *p;
    unsigned rec = g_pak.routes ? *g_pak.routes : 4, i, k, n = 0;
    MapNode *nd;

    map_free_nodes();
    if (!raw) {
        /* Older crash site packs: the UFO's nodes only. */
        for (i = 0; i < n_placed && placed[i].block != g_pak.ufo_block; i++)
            ;
        if (i == n_placed || !g_pak.n_nodes)
            return 0;
        raw = (const unsigned char *)g_pak.node;
        n = g_pak.n_nodes;
        g_node = malloc(n * sizeof(MapNode));
        if (!g_node)
            return 1;
        for (k = 0; k <= n_placed; k++)
            g_node_first[k] = (unsigned short)(k > i ? n : 0);
        for (k = 0; k < n; k++, raw += 4) {
            nd = &g_node[k];
            nd->x = (unsigned char)(g_ufo_x + raw[0]);
            nd->y = (unsigned char)(g_ufo_y + raw[1]);
            nd->z = raw[2];
            nd->rank = raw[3];
            nd->pf = 0x10;
            nd->target = 0;
            memset(nd->link, 255, 5);
        }
        g_n_node = n;
        return 0;
    }
    if (g_pak.n_blocks > 64)
        return 0;
    p = raw;
    for (i = 0; i < g_pak.n_blocks; i++) {
        start[i] = (unsigned short)(p - raw);
        p += 1 + *p * rec;
    }
    for (i = 0; i < n_placed; i++)
        n += raw[start[placed[i].block]];
    g_node = malloc(n ? n * sizeof(MapNode) : 1);
    if (!g_node)
        return 1;
    n = 0;
    for (i = 0; i < n_placed; i++) {
        g_node_first[i] = (unsigned short)n;
        p = raw + start[placed[i].block];
        for (k = *p++; k > 0; k--, p += rec, n++) {
            nd = &g_node[n];
            nd->x = (unsigned char)(p[0] == 255 ? 255 : placed[i].cx * 10 + p[0]);
            nd->y = (unsigned char)(placed[i].cy * 10 + p[1]);
            nd->z = p[2];
            nd->rank = p[3];
            nd->target = 0;
            if (rec >= 10) {
                nd->pf = p[4];
                memcpy(nd->link, p + 5, 5);
                if (rec > 10)
                    nd->target = p[10];
            } else {
                nd->pf = 0x10;
                memset(nd->link, 255, 5);
            }
        }
    }
    g_node_first[n_placed] = (unsigned short)n;
    g_n_node = n;
    return 0;
}


/* Alien base, Mars surface, Cydonia base (mapScripts.rul ALIENBASE, MARS,
   BOSSBATTLE): entry lifts (Mars: the craft), the main block (command
   centre, pyramid, brain), three 20x20 rooms, weighted fill, tunnels. */
static int script_generate(void)
{
    const Block *b = &g_pak.block[g_pak.ufo_block];
    unsigned i, bx, by;

    if (g_pak.script == SCRIPT_MARS) {
        if (!place_on_landing_zone(g_pak.craft_block, &g_craft_x, &g_craft_y))
            return 1;
    } else
        for (i = 0; i < 2; i++) {
            if (!free_spot(1, 1, &bx, &by))
                return 1;
            place(g_pak.landing[rnd(g_pak.n_landing)], bx, by, 0);
            g_craft_x = bx * 10;
            g_craft_y = by * 10;
            g_lift[g_n_lift][0] = (unsigned char)g_craft_x;
            g_lift[g_n_lift++][1] = (unsigned char)g_craft_y;
        }
    if (!free_spot(b->sx / 10, b->sy / 10, &bx, &by))
        return 1;
    place(g_pak.ufo_block, bx, by, 0);
    g_ufo_x = bx * 10;
    g_ufo_y = by * 10;
    place_big_blocks(g_pak.n_big_blocks);
    fill_rest();
    if (read_failed)
        return 1;
    if (g_pak.tunnel[3])
        dig_tunnels();
    if (g_pak.script == SCRIPT_MARS) {
        /* LOG for test scripts: where the exit area to the second stage is. */
        unsigned long k, n = 0, first = 0, last = 0;
        for (k = 0; k < (unsigned long)g_mx * g_my * g_mz * 4; k++)
            if (g_map[k] && g_pak.mcd[g_map[k]].special == SPECIAL_END) {
                if (!n++)
                    first = k / 4;
                last = k / 4;
            }
        log_printf("exit area: %lu tiles, %lu,%lu,%lu to %lu,%lu,%lu", n, first % g_mx,
                   first / g_mx % g_my, first / g_mx / g_my, last % g_mx,
                   last / g_mx % g_my, last / g_mx / g_my);
    }
    return collect_nodes();
}


int map_generate(void)
{
    unsigned long size;

    g_mx = g_pak.map_x;
    g_my = g_pak.map_y;
    g_mz = g_pak.map_z;
    g_n_boom = 0;               /* (a set-up that failed after map_crash_power) */
    gx = g_mx / 10;
    gy = g_my / 10;
    if (gx > GRID_MAX || gy > GRID_MAX)
        return 1;
    size = (unsigned long)g_mx * g_my * g_mz * 4;
    if (size > MAP_MAX_TILES * 4UL)
        return 1;
    if (map_alloc())
        return 1;
    memset(g_map, 0, (size_t)size);
    memset(used, 0, sizeof used);
    memset(g_node_seg, 0, sizeof g_node_seg);
    n_placed = 0;
    g_rev_w = g_n_lift = 0;
    memset(uses, 0, sizeof uses);
    read_failed = 0;

    if (g_base_layout)
        return map_generate_base() || read_failed || collect_nodes();
    if (g_pak.script == SCRIPT_URBAN)
        return urban_generate() || read_failed;
    if (g_pak.script >= SCRIPT_ALIEN_BASE)
        return script_generate();

    if (!place_on_landing_zone(g_pak.ufo_block, &g_ufo_x, &g_ufo_y))
        return 1;
    if (!place_on_landing_zone(g_pak.craft_block, &g_craft_x, &g_craft_y))
        return 1;

    /* A few 20x20 blocks where the terrain has them. */
    place_big_blocks(3);
    fill_rest();
    return read_failed || collect_nodes();
}

void map_count_loot(unsigned *loot)
{
    unsigned i, n = g_mx * g_my * g_mz * 4, s;

    memset(loot, 0, 10 * sizeof *loot);
    for (i = 0; i < n; i++)
        if (g_map[i] && (s = g_pak.mcd[g_map[i]].special) >= 2 && s <= 11)
            loot[s - 2]++;
}

void map_crash_power(void)
{
    unsigned i, n = g_mx * g_my * g_mz * 4, k = 0, m = 0, t;

    for (i = PART_OBJECT; i < n; i += 4)
        if (g_map[i] && g_pak.mcd[g_map[i]].special == 2) {
            m++;
            if (rnd(100) < 75) {
                g_map[i] = g_pak.mcd[g_map[i]].die_mcd;
                k++;
                if (g_n_boom < g_max_boom) {
                    unsigned char *p = g_boom[g_n_boom++];
                    t = i / 4;
                    p[0] = (unsigned char)t;
                    p[1] = (unsigned char)(t >> 8);
                    p[2] = 0;
                }
            }
        }
    if (m)
        log_printf("crash: %u of %u UFO power sources blown up", k, m);
}

void map_base_modules(unsigned char *mask)
{
    unsigned x, y, z, p, m;

    memset(mask, 0, 5);
    for (z = 0; z < g_mz; z++)
        for (y = 0; y < g_my; y++)
            for (x = 0; x < g_mx; x++) {
                const unsigned char *t = TILE(x, y, z);
                for (p = 0; p < 4; p++)
                    if (t[p] && g_pak.mcd[t[p]].special == SPECIAL_XBASE) {
                        m = y / 10 * 6 + x / 10;
                        mask[m >> 3] |= (unsigned char)(1 << (m & 7));
                    }
            }
}
