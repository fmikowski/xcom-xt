/* SPDX-License-Identifier: GPL-3.0-or-later */
/* VGA outside the battle (vgageo.h): the start, from vga_init once (the
   vgainit overlay section). Copies src/vgxasm.asm's code into a block of
   its own and puts its palette hook in front of the VGA picture code:
   uk_pic_cga's first bytes, a far call into vgaasm.asm's block (vgainit.c
   hook), now call vgxasm.asm's entry 0, which sets the picture's DOS
   palette and goes on to the old target. uk_theme's and uk_fill's go
   through entries 3 and 4 the same way: the DOS interface colours. */
#include <stdlib.h>
#include <i86.h>
#include "gfx.h"
#include "uik.h"
#include "input.h"
#include "vgageo.h"

unsigned __cdecl vgx_blob(unsigned seg);
void far * __cdecl vga_near(int k);

unsigned g_vgx;

#define VX_THEIRS   0x20        /* src/vgxasm.asm's variables */
#define VX_VSEG     0x24
#define VX_THEMT    0x3E
#define VX_FILLT    0x42

/* Points the hook at fn (a far call into vgaasm.asm's block) to entry k,
   its old target kept at var. */
static void chain(unsigned char far *fn, unsigned seg, int k, unsigned var)
{
    *(unsigned long far *)MK_FP(seg, var) = *(unsigned long far *)(fn + 1);
    *(unsigned far *)(fn + 1) = 4 * k;
    *(unsigned far *)(fn + 3) = seg;
}

void vgx_init(unsigned vseg)
{
    unsigned char far *h = (unsigned char far *)vga_near(0);   /* uk_pic_cga */
    unsigned char far *th = (unsigned char far *)uk_theme, far *fi = (unsigned char far *)uk_fill;
    unsigned seg;
    void *p;

    g_pakv = 'V';
    if (h[0] != 0x9A || th[0] != 0x9A || fi[0] != 0x9A || !(p = malloc(vgx_blob(0) + 15)))
        return;
    seg = FP_SEG(p) + ((FP_OFF(p) + 15) >> 4);
    vgx_blob(seg);
    *(unsigned far *)MK_FP(seg, VX_VSEG) = vseg;
    chain(h, seg, 0, VX_THEIRS);
    chain(th, seg, 3, VX_THEMT);
    chain(fi, seg, 4, VX_FILLT);
    g_vgx = seg;
    log_printf("video: VGA screens' palettes and map, code at %04X (%u bytes)", seg, vgx_blob(0));
}
