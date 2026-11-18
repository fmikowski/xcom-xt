/* SPDX-License-Identifier: GPL-3.0-or-later */
/* VGA outside the battle (src/vgageo.c, src/vgxasm.asm; docs/design.md,
   "VGA outside the battle"): the screens' DOS palettes, the geoscape's
   map and markers in mode 13h. g_vgx is the block's segment (0 without
   VGA); the geoscape calls its entries far. */
#ifndef VGAGEO_H
#define VGAGEO_H

#include <i86.h>

extern unsigned g_vgx;
extern char g_pakv;             /* the packs' video letter: E, C or V (gfx.c) */

void vgx_init(unsigned vseg);   /* from vga_init (vgainit.c), once */

typedef void (__far __cdecl *VgxWorld)(const unsigned char __far *map, unsigned vx, unsigned vy,
                                       unsigned zoom);
typedef void (__far __cdecl *VgxXor)(int x0, int y0, int x1, int y1, int colour);
#define vgx_world   ((VgxWorld)MK_FP(g_vgx, 4))
#define vgx_xor     ((VgxXor)MK_FP(g_vgx, 8))
/* The screen black, the battlescape's palette set (before a battle). */
#define vgx_battle  ((void (__far __cdecl *)(void))MK_FP(g_vgx, 20))
/* The end slides (cut.c): the DAC from 768 bytes (NULL: black), the
   caption's palette index as EGA colour 14. */
#define vgx_slide   ((void (__far __cdecl *)(const char __far *dac, int caption))MK_FP(g_vgx, 24))

#endif
