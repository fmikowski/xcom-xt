/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GFX_H
#define GFX_H

/* Screen layout: 320x200, map view on top, HUD text rows below.
   EGA (16 colours, planar) or CGA (4 colours, packed 2 bits per pixel). */
#define VIEW_H      144
#define HUD_ROW     18          /* first 8x8 text row of the HUD */

#define VIDEO_EGA   0
#define VIDEO_CGA   1

/* EGA back buffer: 4 planes of BUF_ROWS x BUF_W bytes, exactly the view.
   Sprites crossing an edge go through the clipped blitters; their start
   offset may wrap below 0, which 16-bit arithmetic absorbs. */
#define BUF_W       40
#define BUF_TOP     0
#define BUF_LEFT    0
#define BUF_ROWS    VIEW_H
#define PLANE_BYTES (BUF_W * BUF_ROWS)

/* CGA back buffer: BUF_ROWS x BUFC_W bytes, 4 pixels per byte, then
   BUFC_SAVE bytes for the battlescape cursor's box (src/rcursor.c; EGA keeps
   it in spare video memory). */
#define BUFC_SAVE   (8 * 40)
#define BUFC_W      80
#define BUFC_LEFT   0

/* Buffer offset of a sprite box at screen position (x, y). */
#define EGA_DEST(x, y) ((unsigned)(y) * BUF_W + (unsigned)((x) >> 3))
#define CGA_DEST(x, y) ((unsigned)(y) * BUFC_W + (unsigned)((x) >> 2))

extern int g_video;             /* VIDEO_EGA or VIDEO_CGA (also Hercules, VGA) */
extern unsigned g_bufseg;       /* back buffer segment, offset 0 */

/* The VGA prototype (option VGA, gfx_init want 3; src/vgainit.c,
   vgaasm.asm): g_video is VIDEO_CGA, and the battle view is mode 13h, an
   8-bit back buffer of VGA_BYTES (rows of 320), drawn with the VGA packs
   (...V.PAK, video byte 2) by the CGA calls, which vga_init points at
   vgaasm.asm's code: CGA buffer offsets are a quarter of the VGA ones
   (x a multiple of 4). The interface (uik.c, hud.c, the mouse pointer,
   the unit's arrow) is drawn by vgaasm.asm's too, from UIV.PAK; the
   screens outside the battle draw to B800, not shown. g_vga is the
   segment of vgaasm.asm's block (VGA_VMAP: the palette's colours nearest
   the 16 EGA colours), 0: none of it. */
extern int g_vga;
#define VGA_VMAP    0x820
#define VGA_LIGHTS  0xB20     /* the night lights' far pointers (vgaasm.asm "Night lights") */
#define VGA_AUX     332       /* their cells' levels: level << 4 | the one shown (in PAL, at night) */
#define VGA_SHADE   0xB400    /* their shade table, in the buffer's segment */
#define VGA_NIGHT   0xB68     /* the night's shade, 0 by day (vgaasm.asm V_SHADE) */
#define VGA_VOFF    0xB6C     /* &g_view_off (P_VOFF) */
#define VGA_PICS    0xBD8     /* entry 26, pics_next: the smoke and fire pictures' redraw, far x0, y0, x1, y1 or 0 */
#define VGA_BYTES   (320U * BUF_ROWS)
#define PAK_LETTER  (g_vga ? 'V' : g_video == VIDEO_EGA ? 'E' : 'C')
int vga_init(void);

/* Sprites are clipped to this rectangle (x multiples of 16). */
extern int g_clip_x0, g_clip_y0, g_clip_x1, g_clip_y1;

/* want 0: EGA if present, else Hercules on a mono-only machine, else CGA;
   1: CGA; 2: Hercules. Returns 0 ok, 1 no video, 2 memory, 3 no Hercules
   (want 2; hgcinit.c says why). */
int gfx_init(int want);
void gfx_shutdown(void);

/* Hercules (src/hgcinit.c, src/hgcasm.asm): the game runs as on CGA and
   draws into the card's second page (B800, not shown); a copier in a
   heap block copies what changed into the page shown (B000, 640x200
   letterboxed in 720x348, a CGA byte one Hercules byte). g_herc is 1
   then. The calls below (video.asm, resident) do nothing on EGA and CGA.
   hgc_mark: the pixel rectangle is about to change (mouse_hide_rect);
   hgc_show: it changed, copy it and everything marked; hgc_flush: copy
   everything marked, then compare scan rows (round robin) with page 0
   and copy those that differ (writes nobody marked). */
extern int g_herc;
void hgc_mark(int x0, int y0, int x1, int y1);
void hgc_show(int x0, int y0, int x1, int y1);
void hgc_flush(int scan);
void hgc_runs(const void far *frame);   /* an intro frame's runs (cut.c) */
/* The view's present: rows r0 .. r0 + rows (rows_r0 = r0 | rows << 8), CGA
   bytes [b0, b0 + n) copied from the back buffer at bufseg. */
void hgc_buf(unsigned bufseg, unsigned rows_r0, unsigned b0, unsigned n);
/* Colour 2 made 3 in n bytes of a CGA picture (the geoscape's poles: 1
   and 2 are one grey on Hercules). */
void hgc_white2(void far *p, unsigned n);
void hgc_set(unsigned seg, unsigned off);   /* the copier's entry */
/* Hercules off (text mode next: returns 7), else returns 3. */
unsigned char hgc_off(void);
/* gfx_init's want (src/hgcinit.c, an overlay section): 0 on, 1 not
   (CGA asked, an EGA, no Hercules card), 3 HERC asked and no card. */
int hgc_init(int want);
/* Hercules: the battle's terrain and unit sprites recoloured, once after a
   battle's packs are loaded (hgcinit.c; main.c calls it only on Hercules). */
void hgc_sprites(void);

/* Darker EGA palette for night missions (no effect on CGA): register i
   shows g_night_pal[i] (EGA attribute values; g_day_pal by day). */
void gfx_night(int night);
extern const unsigned char g_day_pal[16];
extern unsigned char g_night_pal[16];

/* Night light (src/nlit.c, docs/design.md "Night and light"): non-NULL in a
   dark battle on an EGA machine with a UMB overlay area for it: day palette,
   and gfx_present_cells shows the map view through lit_present, which draws
   the dark cells darker. NULL everywhere else: nothing changes. */
extern void far *g_lit;
void lit_present(int c0, int r0, int cols, int rows);

/* 1 while DOS's Hidden Movement screen covers the map view (src/ai.c):
   the view is still drawn into the back buffer, but not shown, and no
   tracer dots; bt_view_on (battle.c) shows the buffer again. */
extern int g_view_off;

/* Window areas in 8-pixel cells (columns) and pixel rows. */
void gfx_clear_cells(int c0, int r0, int cols, int rows);
void gfx_present_cells(int c0, int r0, int cols, int rows);
/* Called after a present that reaches cell GFX_HOOK_C or right of it, with
   its rows: the panel's visible-alien markers lie there (panel.c). */
#define GFX_HOOK_C  37
extern void (*g_present_hook)(int r0, int r1);
/* Moves the view by dcells (even) and drows; EGA: on the screen too. */
void gfx_shift_window(int dcells, int drows);

/* Draws a sprite at screen position (x multiple of 16). */
void gfx_sprite(unsigned index, int x, int y);

/* Same for an OR-format sprite (ground floors on a cleared area). */
void gfx_sprite_or(unsigned index, int x, int y);

/* XORs a 2x2 dot onto the screen (map view only); drawing it again erases it. */
void gfx_dot(int x, int y, int colour);

/* Assembly back ends (blit.asm for EGA, blitc.asm for CGA). */
void __cdecl blit_masked(const void *spr, unsigned bufseg, unsigned dest);
void __cdecl blit_or(const void *spr, unsigned bufseg, unsigned dest);
void __cdecl blit_masked_clip(const void *spr, unsigned bufseg, unsigned dest,
                              unsigned top, unsigned bottom, unsigned wmask);
void __cdecl blit_or_clip(const void *spr, unsigned bufseg, unsigned dest,
                          unsigned top, unsigned bottom, unsigned wmask);
void __cdecl vram_box(unsigned bufseg, unsigned off, unsigned bytes, unsigned rows, int out);
void __cdecl buf_box(unsigned bufseg, unsigned off, unsigned stride, unsigned bytes,
                     unsigned rows, void *save, int out);
void __cdecl buf_shift(unsigned bufseg, unsigned plane_bytes, unsigned planes, int delta);
void __cdecl vram_shift(unsigned bytes, int delta);
void __cdecl buf_clear(unsigned bufseg);
void __cdecl buf_clear_rect(unsigned bufseg, unsigned col, unsigned row,
                            unsigned cols, unsigned rows);
void __cdecl buf_present_rect(unsigned bufseg, unsigned col, unsigned row,
                              unsigned cols, unsigned rows);
void __cdecl blitc_masked(const void *spr, unsigned bufseg, unsigned dest);
void __cdecl blitc_or(const void *spr, unsigned bufseg, unsigned dest);
void __cdecl blitc_masked_clip(const void *spr, unsigned bufseg, unsigned dest,
                               unsigned top, unsigned bottom, unsigned wmask);
void __cdecl blitc_or_clip(const void *spr, unsigned bufseg, unsigned dest,
                           unsigned top, unsigned bottom, unsigned wmask);
void __cdecl bufc_clear_rect(unsigned bufseg, unsigned col, unsigned row,
                             unsigned cols, unsigned rows);
void __cdecl bufc_present_rect(unsigned bufseg, unsigned col, unsigned row,
                               unsigned cols, unsigned rows);
void __cdecl vid_setmode(unsigned char mode);

#endif
