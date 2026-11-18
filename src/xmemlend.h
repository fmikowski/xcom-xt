/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The XMS budget (docs/design.md, "The XMS budget"): on a 286 without upper
   memory the read cache (xmem.c, xmemres.asm) takes all free XMS, and a
   battle's picture store (picplan.c) and canvas (cvinit.c) borrow the top
   of the cache's own block: the cache forgets the blocks it held there and
   works on with fewer slots until they come back. Static functions for the
   two overlay sections that include this file (nothing resident). Lends
   nest: the canvas borrows below the store and gives back first. */
#ifndef XMEMLEND_H
#define XMEMLEND_H

#include <dos.h>
#include <i86.h>
#include "xmem.h"

#define XL_KEEP     8u          /* blocks (4 KB) the cache keeps at least (32 KB: in battle the store
                                   holds what the cache would; the walk frames moved there too) */

unsigned long __cdecl xms_find(void);           /* sndasm.asm */
void __cdecl xms_call(unsigned __far *r);

/* A20 for the HMA's cache (seg FFFF): XMS 05h local enable, 06h disable. */
static void xl_a20(unsigned seg, unsigned fn)
{
    unsigned x[5];

    if (seg != 0xFFFF)
        return;
    x[0] = fn;
    x[1] = x[2] = x[3] = x[4] = 0;
    xms_call((unsigned __far *)x);
}

/* The read cache's header when it caches in XMS (A20 on for the HMA's;
   xl_done after), else NULL. */
static XmemHdr __far *xl_hdr(void)
{
    union REGS r;
    struct SREGS s;
    XmemHdr __far *h;

    if (!xms_find())
        return NULL;
    segread(&s);
    r.x.ax = 0xD700;
    r.x.si = XMEM_MAGIC;
    r.x.di = 0;
    int86x(0x21, &r, &r, &s);
    if (r.x.ax != XMEM_MAGIC)
        return NULL;
    xl_a20(s.es, 0x500);
    h = (XmemHdr __far *)MK_FP(s.es, r.x.di);
    if (h->mode == XM_XMS && h->on && h->sig[0] == 'X' && h->sig[3] == '1')
        return h;
    xl_a20(s.es, 0x600);
    return NULL;
}

#define xl_done(h) xl_a20(FP_SEG(h), 0x600)

/* KB the cache can lend now. */
static unsigned xl_kb(void)
{
    XmemHdr __far *h = xl_hdr();
    unsigned n;

    if (!h)
        return 0;
    n = h->nslot;
    xl_done(h);
    return n > XL_KEEP ? (n - XL_KEEP) * 4 : 0;
}

/* kb (rounded up to 4) of the cache's block: *handle its XMS handle, the
   loan at byte (returned - kb / 4) * 4096 of it; returns the cache's slot
   count before (for xl_back), 0 if refused. The cache's table (xmemres.asm
   M_*) is rebuilt without the slots lent: its chains from what the slots
   below hold. */
static unsigned xl_lend(unsigned kb, unsigned *handle)
{
    XmemHdr __far *h = xl_hdr();
    unsigned n, k = (kb + 3) / 4, ns, i, b;
    unsigned __far *heads, __far *blk, __far *next;
    unsigned char __far *file;

    if (!h)
        return 0;
    n = h->nslot;
    if (n < k + XL_KEEP || ((unsigned long)n << 2) > 1024) {     /* (store pointers: < 1 MB) */
        xl_done(h);
        return 0;
    }
    ns = FP_SEG(h) == 0xFFFF ? 256 : 512;
    heads = (unsigned __far *)MK_FP(h->meta, ns == 256 ? 0xE7E0 + 16 : 16);
    file = (unsigned char __far *)(heads + 256);
    blk = (unsigned __far *)(file + 2 * ns);
    next = blk + ns;
    _fmemset(heads, 0, 512);
    for (i = 0; i < ns; i++) {
        if (i >= n - k)
            file[i] = 0;
        else if (file[i]) {
            b = (file[i] * 37 + blk[i]) & 0xFF;     /* xmemres.asm hash */
            next[i] = heads[b];
            heads[b] = i + 1;
        }
    }
    h->nslot = n - k;
    if (h->hand >= n - k)
        h->hand = 0;
    *handle = h->handle;
    xl_done(h);
    return n;
}

/* The loan of kb back (before: what xl_lend returned); only when it is the
   last one still out (the slots lent are free in the table). */
static void xl_back(unsigned before, unsigned kb)
{
    XmemHdr __far *h = xl_hdr();

    if (!h)
        return;
    if (h->nslot == before - (kb + 3) / 4)
        h->nslot = before;
    xl_done(h);
}

#endif
