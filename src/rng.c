/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Random numbers and paragraph-aligned allocations (resident; the map
   generator in mapgen.c is only loaded while a battle is set up). */

#include <dos.h>
#include <i86.h>
#include <string.h>
#include "map.h"

unsigned long g_rng = 1;

void *alloc_para(unsigned size)
{
    /* Straight from DOS, exactly sized: these tables live for the whole
       run, and the C heap wastes a lot around blocks this big. */
    unsigned seg;
    unsigned char *p;

    if (_dos_allocmem((size + 15U) >> 4, &seg))
        return NULL;
    p = (unsigned char *)MK_FP(seg, 0);
    _fmemset(p, 0, size);
    return p;
}

void rng_seed(unsigned long seed)
{
    g_rng = seed ? seed : 1;
}

unsigned rnd(unsigned n)
{
    g_rng = g_rng * 1103515245UL + 12345UL;
    return n ? (unsigned)((g_rng >> 16) % n) : 0;
}
