/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include <conio.h>
#include "gfx.h"
#include "input.h"
#include "pak.h"
#include "render.h"
#include "video.h"

int g_video;
unsigned g_bufseg;
int g_vga;
char g_pakv = 'C';              /* the packs' letter: E, C (Hercules too), V (vgageo.c) */
unsigned g_blit_count, g_or_count;
int g_no_blit;
int g_clip_x0, g_clip_y0, g_clip_x1 = 320, g_clip_y1 = VIEW_H;

static int has_ega(void)
{
    union REGS r;

    r.h.ah = 0x12;
    r.h.bl = 0x10;
    int86(0x10, &r, &r);
    return r.h.bl != 0x10;
}

int g_herc;

int gfx_init(int want)
{
    unsigned long size;
    void *p;
    union REGS r;

    if (want == 3)
        return vga_init();      /* the VGA prototype (vgainit.c) */
    /* Hercules when asked, or perhaps where the BIOS says mono (equipment
       bits 4-5; hgcinit.c decides); else, or without the card, as before. */
    if (want == 2 || (*(unsigned char far *)MK_FP(0x40, 0x10) & 0x30) == 0x30)
        if (hgc_init(want) == 3)
            return 3;
    g_video = (want || g_herc || !has_ega()) ? VIDEO_CGA : VIDEO_EGA;
    size = g_video == VIDEO_EGA ? (unsigned long)PLANE_BYTES * 4 + 16
                                : (unsigned long)BUFC_W * BUF_ROWS + BUFC_SAVE + 16;
    p = malloc((size_t)size);
    if (!p)
        return 2;
    g_bufseg = FP_SEG(p) + ((FP_OFF(p) + 15) >> 4);
    _fmemset(MK_FP(g_bufseg, 0), 0, (size_t)(size - 16));
    if (g_video == VIDEO_EGA) {
        g_pakv = 'E';
        vid_setmode(VMODE_EGA16);
    } else if (!g_herc) {
        vid_setmode(VMODE_CGA4);
        r.h.ah = 0x0B;          /* palette 0 (green/red/brown) */
        r.h.bh = 1;
        r.h.bl = 0;
        int86(0x10, &r, &r);
        r.h.ah = 0x0B;          /* black background, low intensity */
        r.h.bh = 0;
        r.h.bl = 0;
        int86(0x10, &r, &r);
    }
    return 0;
}

void gfx_shutdown(void)
{
    vid_setmode(hgc_off());             /* 3, or 7 after Hercules */
}

void gfx_clear_cells(int c0, int r0, int cols, int rows)
{
    if (g_video == VIDEO_EGA)
        buf_clear_rect(g_bufseg, c0, r0, cols, rows);
    else
        bufc_clear_rect(g_bufseg, c0 * 2, r0, cols * 2, rows);
}

int g_view_off;
void far *g_lit;
void (*g_present_hook)(int r0, int r1);

void gfx_present_cells(int c0, int r0, int cols, int rows)
{
    if (g_view_off)
        return;
    render_arrow(c0, r0, cols, rows);   /* the selected unit's (render.h) */
    mouse_hide_rect(c0 * 8, r0, (c0 + cols) * 8, r0 + rows);
    if (g_lit)
        lit_present(c0, r0, cols, rows);
    else if (g_video == VIDEO_EGA)
        buf_present_rect(g_bufseg, c0, r0, cols, rows);
    else {
        bufc_present_rect(g_bufseg, c0 * 2, r0, cols * 2, rows);
        /* Hercules: converted from the buffer, not read back from B800 */
        hgc_buf(g_bufseg, r0 | rows << 8, c0 * 2, cols * 2);
    }
    if (g_present_hook && c0 + cols > GFX_HOOK_C)
        g_present_hook(r0, r0 + rows);
}

/* The view moves by dc cells (even) and dr rows; the exposed strips hold
   leftovers until they are redrawn. On EGA the screen moves too (latch
   copies), unless the view is hidden: then only the strips need showing. */
void gfx_shift_window(int dc, int dr)
{
    if (g_video == VIDEO_EGA) {
        buf_shift(g_bufseg, PLANE_BYTES, 4, dr * BUF_W + dc);
        if (!g_view_off) {
            mouse_hide_rect(0, 0, 320, VIEW_H);
            vram_shift(PLANE_BYTES, dr * BUF_W + dc);
        }
    } else
        buf_shift(g_bufseg, BUFC_W * BUF_ROWS, 1, dr * BUFC_W + dc * 2);
}

void gfx_dot(int x, int y, int colour)
{
    int r;

    x &= ~1;
    if (x < 0 || x > 318 || y < 0 || y > VIEW_H - 2 || g_view_off)
        return;
    mouse_hide_rect(x, y, x + 2, y + 2);
    if (g_video == VIDEO_CGA) {
        /* Two pixels of colour 3 (brown) XORed in. */
        unsigned char bits = (unsigned char)(0xF0 >> ((x & 3) * 2));
        for (r = 0; r < 2; r++) {
            unsigned row = y + r;
            unsigned char *v = (unsigned char *)MK_FP(0xB800,
                (row & 1) * 0x2000 + (row >> 1) * 80 + (x >> 2));
            *v ^= bits;
        }
        hgc_flush(0);
        return;
    }
    {
        unsigned char bits = (unsigned char)(0xC0 >> (x & 7));
        int p;
        for (r = 0; r < 2; r++) {
            unsigned char *v = (unsigned char *)MK_FP(0xA000, (unsigned)(y + r) * 40 + (x >> 3));
            for (p = 0; p < 4; p++) {
                unsigned char val;
                outp(0x3CE, 4);         /* read map select */
                outp(0x3CF, p);
                val = *v;
                outp(0x3C4, 2);         /* map mask */
                outp(0x3C5, 1 << p);
                *v = (unsigned char)(colour & (1 << p) ? val ^ bits : val);
            }
        }
        outp(0x3C4, 2);
        outp(0x3C5, 0x0F);
        outp(0x3CE, 4);
        outp(0x3CF, 0);
    }
}

/* Returns 0 if the 32x40 box at (x, y) misses the clip rectangle, 1 if it
   lies fully inside, else 2 with the clip parameters for the blitter. */
static int clip_box(int x, int y, unsigned *top, unsigned *bottom, unsigned *wmask)
{
    int t, b, w, n, size;

    if (x >= g_clip_x1 || x + 32 <= g_clip_x0 || y >= g_clip_y1 || y + 40 <= g_clip_y0)
        return 0;
    if (x >= g_clip_x0 && x + 32 <= g_clip_x1 && y >= g_clip_y0 && y + 40 <= g_clip_y1)
        return 1;
    t = g_clip_y0 - y;
    b = g_clip_y1 - y;
    *top = t > 0 ? t : 0;
    *bottom = b < 40 ? b : 40;
    /* EGA words are 16 pixels wide, CGA words 8. */
    size = g_video == VIDEO_EGA ? 16 : 8;
    n = 32 / size;
    *wmask = 0;
    for (w = 0; w < n; w++)
        if (x + w * size >= g_clip_x0 && x + (w + 1) * size <= g_clip_x1)
            *wmask |= 1 << w;
    return *wmask ? 2 : 0;
}

#define DEST_EGA(x, y) EGA_DEST(x, y)
#define DEST_CGA(x, y) CGA_DEST(x, y)

/* The rows a sprite really covers (its header: first row, row count) decide
   whether it misses the clip rectangle or lies fully inside it, not its
   40-row box: partial redraws skip most of the sprites around them. */
#define SPRITE_ROWS(s, y, r0, r1) \
    (r0 = (y) + ((const unsigned char *)(s))[0], r1 = r0 + ((const unsigned char *)(s))[1])

void gfx_sprite(unsigned index, int x, int y)
{
    unsigned top, bottom, wmask;
    int c, r0, r1;

    sprite_in(&g_pak.sprite[index], index);    /* (sndasm.asm: from EMS, XMS or the disk) */
    SPRITE_ROWS(g_pak.sprite[index], y, r0, r1);
    if (r1 <= g_clip_y0 || r0 >= g_clip_y1 || r0 == r1)
        return;
    if (x >= g_clip_x0 && x + 32 <= g_clip_x1 && r0 >= g_clip_y0 && r1 <= g_clip_y1)
        c = 1;
    else {
        c = clip_box(x, y, &top, &bottom, &wmask);
        if (!c)
            return;
    }
    g_blit_count++;
    if (g_no_blit)
        return;
    if (g_video == VIDEO_EGA) {
        if (c == 1)
            blit_masked(g_pak.sprite[index], g_bufseg, DEST_EGA(x, y));
        else
            blit_masked_clip(g_pak.sprite[index], g_bufseg, DEST_EGA(x, y), top, bottom, wmask);
    } else {
        if (c == 1)
            blitc_masked(g_pak.sprite[index], g_bufseg, DEST_CGA(x, y));
        else
            blitc_masked_clip(g_pak.sprite[index], g_bufseg, DEST_CGA(x, y), top, bottom, wmask);
    }
}

void gfx_sprite_or(unsigned index, int x, int y)
{
    unsigned top, bottom, wmask;
    int c, r0, r1;

    sprite_in(&g_pak.sprite[index], index);    /* (sndasm.asm: from EMS, XMS or the disk) */
    SPRITE_ROWS(g_pak.sprite[index], y, r0, r1);
    if (r1 <= g_clip_y0 || r0 >= g_clip_y1 || r0 == r1)
        return;
    if (x >= g_clip_x0 && x + 32 <= g_clip_x1 && r0 >= g_clip_y0 && r1 <= g_clip_y1)
        c = 1;
    else {
        c = clip_box(x, y, &top, &bottom, &wmask);
        if (!c)
            return;
    }
    g_blit_count++;
    g_or_count++;
    if (g_no_blit)
        return;
    if (g_video == VIDEO_EGA) {
        if (c == 1)
            blit_or(g_pak.sprite[index], g_bufseg, DEST_EGA(x, y));
        else
            blit_or_clip(g_pak.sprite[index], g_bufseg, DEST_EGA(x, y), top, bottom, wmask);
    } else {
        if (c == 1)
            blitc_or(g_pak.sprite[index], g_bufseg, DEST_CGA(x, y));
        else
            blitc_or_clip(g_pak.sprite[index], g_bufseg, DEST_CGA(x, y), top, bottom, wmask);
    }
}

/* EGA palette registers in 200-line modes: RGB in bits 0-2, intensity in
   bit 4. Night maps every bright colour to its darker neighbour; light
   and dark grey stay apart (the UFO hulls and doors are drawn in them);
   the battle panel may take registers the map hardly uses (batui.c). */
const unsigned char g_day_pal[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23
};
unsigned char g_night_pal[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 16, 1, 2, 3, 4, 5, 6, 7
};

void gfx_night(int night)
{
    union REGS r;
    int i;

    if (g_video != VIDEO_EGA)
        return;
    for (i = 0; i < 16; i++) {
        r.w.ax = 0x1000;
        r.h.bl = (unsigned char)i;
        r.h.bh = night ? g_night_pal[i] : g_day_pal[i];
        int86(0x10, &r, &r);
    }
}
