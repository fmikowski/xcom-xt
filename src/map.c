/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Resident part of the map: tile buffer, route nodes and their links (the
   generator is mapgen.c, random numbers and alloc_para rng.c). */

#include <stdlib.h>
#include <string.h>
#include "pak.h"
#include "map.h"

unsigned g_mx, g_my, g_mz;
unsigned char *g_map;
int g_craft_x, g_craft_y, g_ufo_x, g_ufo_y;

/* Base defence layout (map.h). */
const unsigned char *g_base_layout;

/* Route nodes (built by mapgen.c). */
MapNode *g_node;
unsigned g_n_node;
unsigned char g_node_seg[MAP_GRID][MAP_GRID];
unsigned short g_node_first[MAP_GRID * MAP_GRID + 1];

void map_free_nodes(void)
{
    free(g_node);
    g_node = NULL;
    g_n_node = 0;
}

unsigned map_node_link(unsigned n, int k)
{
    unsigned l = g_node[n].link[k], b = 0, m, q;
    int cx, cy, inv, j;

    if (l == 255)
        return NO_NODE;
    while (g_node_first[b + 1] <= n)
        b++;
    if (l <= 250) {
        m = g_node_first[b] + l;
        return m < g_node_first[b + 1] && g_node[m].x != 255 ? m : NO_NODE;
    }
    /* 254 east, 253 south, 252 west, 251 north (OpenXcom's pairing). */
    cx = g_node[n].x / 10;
    cy = g_node[n].y / 10;
    switch (l) {
    case 254: cx++; inv = 252; break;
    case 253: cy++; inv = 251; break;
    case 252: cx--; inv = 254; break;
    default:  cy--; inv = 253; break;
    }
    if ((unsigned)cx >= g_mx / 10 || (unsigned)cy >= g_my / 10 || !g_node_seg[cy][cx])
        return NO_NODE;
    q = g_node_seg[cy][cx] - 1;
    for (m = g_node_first[q]; m < g_node_first[q + 1]; m++)
        for (j = 0; j < 5; j++)
            if (g_node[m].link[j] == inv && g_node[m].x != 255)
                return m;
    return NO_NODE;
}

int map_alloc(void)
{
    if (!g_map)
        g_map = alloc_para((unsigned)(MAP_MAX_TILES * 4UL));
    return g_map == NULL;
}
