/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The VGA prototype's start (option VGA; an overlay section with
   src/vgaasm.asm, it runs once from gfx_init): checks for a VGA, takes
   the 8-bit back buffer, copies the battle view's code (vgaasm.asm) into
   a block of its own, points the resident CGA routines at it (their first
   bytes become a far jump there), sets mode 13h and the battlescape
   palette from a VGA mission pack ("XPL1", tools/convert.py --vga). The
   game then runs as on CGA (g_video = VIDEO_CGA) with the VGA packs: the
   battle view is drawn in 256 colours, and the interface (uik.c with
   UIV.PAK, hud.c, the mouse pointer, the unit's arrow) through the
   routines of vgaasm.asm's "interface" (docs/design.md, "VGA prototype").
   Nothing of this is resident: an EGA or CGA run never loads it. */
#include <i86.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "render.h"
#include "uik.h"
#include "fx_in.h"
#include "map.h"
#include "pak.h"
#include "unit.h"
#include "vgageo.h"
#include "video.h"

#define CODE_OFF    0xB70       /* block layout: src/vgaasm.asm */
#define VMAP        VGA_VMAP
#define P_MHR       0x830       /* far pointers, in this order */
#define CURSOR      0x960

extern int g_uk_fd;
extern char g_uk_name[];
extern const unsigned char __near *g_ms_pm, __near *g_ms_pf;

unsigned __cdecl vga_blob(unsigned seg);
void far * __cdecl vga_near(int k);
void __cdecl fxtile_cga(unsigned seg, unsigned off, const unsigned char *shape,
                        unsigned r0, unsigned r1, unsigned q01, unsigned q23,
                        unsigned colour, unsigned cols);

/* The first bytes of a resident routine: a far jump to the copied code's
   entry k, or (k < 0) a far return (cdecl and register calls without
   stack arguments: the caller cleans up). */
static void point(void far *fn, unsigned seg, int k)
{
    unsigned char far *p = (unsigned char far *)fn;

    if (k < 0) {
        *p = 0xCB;              /* retf */
        return;
    }
    p[0] = 0xEA;                /* jmp far seg:CODE_OFF + 4k */
    *(unsigned far *)(p + 1) = CODE_OFF + 4 * k;
    *(unsigned far *)(p + 3) = seg;
}

/* A resident routine's first bytes call entry k and return as it would:
   near or far, popping its pop bytes of stack arguments. */
static void hook(void far *fn, unsigned seg, int k, int far_fn, unsigned pop)
{
    unsigned char far *p = (unsigned char far *)fn;

    p[0] = 0x9A;                /* call far seg:CODE_OFF + 4k */
    *(unsigned far *)(p + 1) = CODE_OFF + 4 * k;
    *(unsigned far *)(p + 3) = seg;
    p[5] = (unsigned char)((far_fn ? 0xCA : 0xC2) + (pop ? 0 : 1));
    *(unsigned far *)(p + 6) = pop;
}

/* The interface's tables in the block: VMAP (the palette's colours
   nearest the 16 EGA colours the screens name), the C side's addresses,
   OpenXcom's mouse pointer (Cursor::draw: four lines of colours 144-147,
   9 x 13). */
static void interface(unsigned seg)
{
    static const unsigned char ega[16][3] = {
        { 0, 0, 0 }, { 0, 0, 42 }, { 0, 42, 0 }, { 0, 42, 42 }, { 42, 0, 0 }, { 42, 0, 42 },
        { 42, 21, 0 }, { 42, 42, 42 }, { 21, 21, 21 }, { 21, 21, 63 }, { 21, 63, 21 },
        { 21, 63, 63 }, { 63, 21, 21 }, { 63, 21, 63 }, { 63, 63, 21 }, { 63, 63, 63 }
    };
    const unsigned char far *pal = (const unsigned char far *)MK_FP(seg, 0);
    unsigned char far *b = (unsigned char far *)MK_FP(seg, 0);
    void far * far *p = (void far * far *)MK_FP(seg, P_MHR);
    const UkTheme *themes;
    int c, i, k;

    for (c = 0; c < 16; c++) {
        long best = 0x7FFFFFFFL;
        for (i = 0; i < 256; i++) {
            long d = 0;
            for (k = 0; k < 3; k++) {
                int e = pal[i * 3 + k] - ega[c][k];
                d += (long)e * e;
            }
            if (d < best) {
                best = d;
                b[VMAP + c] = (unsigned char)i;
            }
        }
    }
    /* The EGA themes: uk_theme's choice on an EGA (they are static). */
    g_video = VIDEO_EGA;
    uk_theme(0);
    themes = g_uk;
    g_video = VIDEO_CGA;
    p[0] = (void far *)mouse_hide_rect;
    p[1] = (void far *)&g_uk_fd;
    p[2] = (void far *)&g_uk;
    p[3] = (void far *)themes;
    p[4] = (void far *)&g_ms_pm;
    p[5] = (void far *)&g_ms_pf;
    p[6] = (void far *)g_uk_name;
    p[7] = (void far *)g_arrow;
    p[8] = (void far *)&g_view_z;
    p[9] = (void far *)&g_cam_x;
    p[10] = (void far *)&g_cam_y;
    p[11] = (void far *)&g_bufseg;
    _fmemset(b + CURSOR, 0, 9 * 13);
    for (i = 0; i < 4; i++) {
        for (k = 2 * i; k <= 12 - i; k++)          /* down the left */
            b[CURSOR + k * 9 + i] = (unsigned char)(144 + i);
        for (k = 0; i + k <= 8 - i; k++)           /* down to the right */
            b[CURSOR + (2 * i + k) * 9 + i + k] = (unsigned char)(144 + i);
    }
    b[CURSOR + 8 * 9 + 4] = 147;
    g_uk_name[2] = 'V';                         /* UIV.PAK */
}

/* The battlescape palette into the block (offset 0) from the pack named
   after MISSION on the command line, else FA1V.PAK: the trailer's last
   section, "XPL1" (after "XLF1", where the loaders stop). Returns 1 if
   found. Every VGA mission pack has the same one. */
static int palette(unsigned seg)
{
    const char *name = "FA1V.PAK";
    unsigned short n;
    char m[4];
    FILE *f;
    int i, ok = 0;

    for (i = 1; i + 1 < __argc; i++)
        if (!stricmp(__argv[i], "MISSION"))
            name = __argv[i + 1];
    if ((f = fopen(name, "rb")) == NULL)
        return 0;
    if (!fseek(f, -6L - 6 - 768, SEEK_END) && fread(m, 4, 1, f) == 1 && fread(&n, 2, 1, f) == 1
        && !memcmp(m, "XPL1", 4) && n == 768)
        ok = fread(MK_FP(seg, 0), 768, 1, f) == 1;
    fclose(f);
    return ok;
}

/* The night lights' far pointers (vgaasm.asm "Night lights", 0B20): the
   C side's addresses and the layouts it reads. */
static void lights(unsigned seg)
{
    void far * far *p = (void far * far *)MK_FP(seg, VGA_LIGHTS);
    unsigned far *w = (unsigned far *)MK_FP(seg, VGA_LIGHTS + 56);

    p[0] = (void far *)g_units;
    p[1] = (void far *)&g_nunits;
    p[2] = (void far *)&fire;
    p[3] = (void far *)&n_fire;
    p[4] = (void far *)&dev;
    p[5] = (void far *)&n_dev;
    p[6] = (void far *)&g_pak.lit;
    p[7] = (void far *)g_pak.lights;
    p[8] = (void far *)&g_pak.n_lights;
    p[9] = (void far *)&g_map;
    p[10] = (void far *)&g_mx;
    p[11] = (void far *)&g_my;
    p[12] = (void far *)&g_mz;
    p[13] = (void far *)&g_present_hook;
    w[0] = sizeof(Unit);
    w[1] = offsetof(Unit, x);
    w[2] = offsetof(Unit, faction);
    w[3] = offsetof(Unit, alive);
    w[4] = offsetof(Unit, burning);
    w[5] = sizeof(Device);
    w[6] = g_bufseg;
}

/* gfx_init's want 3: 0 on, 2 no memory, 3 no VGA (said why). */
/* DOS's smoke and fire pictures (SMOKEV.PAK, tools/convert.py build_smoke)
   for vgaasm.asm's op_fx and pics_next: a block of far &smoke,
   &n_smoke, &g_seen, &g_all_layers and the pack's bytes, its far address
   at P_SMOKE (09D8); 0 without the pack (the dithered shapes then). */
static void smoke_pics(unsigned seg)
{
    void far * far *at = (void far * far *)MK_FP(seg, 0x9D8);
    unsigned char h[8];
    unsigned n;
    void far * far *b;
    void *p;
    FILE *f;

    *at = 0;
    if ((f = fopen("SMOKEV.PAK", "rb")) == NULL)
        return;
    if (fread(h, 8, 1, f) == 1 && !memcmp(h, "XSM1", 4) && h[4] == 2 && h[5] == 14) {
        n = h[6] | h[7] << 8;
        if ((p = malloc(n + 16 + 15)) != NULL) {
            b = (void far * far *)MK_FP(FP_SEG(p) + ((FP_OFF(p) + 15) >> 4), 0);
            if (fread(b + 4, n, 1, f) == 1) {
                b[0] = (void far *)&smoke;
                b[1] = (void far *)&n_smoke;
                b[2] = (void far *)&g_seen;
                b[3] = (void far *)&g_all_layers;
                *at = b;
            } else
                free(p);
        }
    }
    fclose(f);
}

int vga_init(void)
{
    union REGS r;
    unsigned seg;
    void *p;

    r.x.ax = 0x1A00;            /* display combination: AL 1Ah, BL 7 / 8 on a VGA */
    int86(0x10, &r, &r);
    if (r.h.al != 0x1A || (r.h.bl != 7 && r.h.bl != 8)) {
        printf("VGA: no VGA found.\n");
        return 3;
    }
    g_video = VIDEO_CGA;
    p = malloc(VGA_BYTES + 256 + 16);       /* + the night's shade table */
    if (!p)
        return 2;
    g_bufseg = FP_SEG(p) + ((FP_OFF(p) + 15) >> 4);
    _fmemset(MK_FP(g_bufseg, 0), 0, VGA_BYTES);
    p = malloc(CODE_OFF + vga_blob(0) + 15);
    if (!p)
        return 2;
    seg = FP_SEG(p) + ((FP_OFF(p) + 15) >> 4);
    vga_blob(seg);
    lights(seg);
    smoke_pics(seg);
    point((void far *)blitc_masked, seg, 0);
    point((void far *)blitc_masked_clip, seg, 1);
    point((void far *)blitc_or, seg, 2);
    point((void far *)blitc_or_clip, seg, 3);
    point((void far *)bufc_clear_rect, seg, 4);
    point((void far *)bufc_present_rect, seg, 5);
    point((void far *)buf_shift, seg, 6);
    point((void far *)buf_box, seg, 7);
    point((void far *)fxtile_cga, seg, 23);     /* smoke, fire, flares */
    vid_setmode(0x13);
    if (palette(seg))
        ((void (__cdecl far *)(void))MK_FP(seg, CODE_OFF + 4 * 8))();
    interface(seg);
    hook(vga_near(0), seg, 9, 0, 2);            /* uk_pic_cga */
    hook(vga_near(1), seg, 10, 0, 0);           /* uk_cga_c */
    hook(vga_near(2), seg, 11, 0, 6);           /* uk_glyph_cga */
    hook((void far *)uk_pic_clean, seg, 12, 1, 0);
    hook((void far *)uk_save, seg, 13, 1, 0);
    hook((void far *)uk_restore, seg, 14, 1, 0);
    hook((void far *)uk_fill, seg, 15, 1, 2);
    hook((void far *)uk_theme, seg, 16, 1, 0);
    hook((void far *)uk_copy, seg, 17, 1, 4);
    hook(vga_near(3), seg, 18, 0, 6);           /* hud_text_cga */
    hook((void far *)hud_clear_rows, seg, 19, 1, 0);
    hook(vga_near(4), seg, 20, 0, 0);           /* ms_draw_cga */
    hook(vga_near(5), seg, 21, 0, 0);           /* ms_erase_cga */
    hook((void far *)render_arrow, seg, 22, 1, 0);
    hook((void far *)gfx_night, seg, 24, 1, 0); /* the night's shade */
    *(void far * far *)MK_FP(seg, VGA_VOFF) = (void far *)&g_view_off;
    hook((void far *)gfx_dot, seg, 25, 1, 0);   /* tracer dots */
    point((void far *)uk_cga_palette, seg, -1); /* (the DAC is set) */
    g_vga = seg;
    log_printf("video: VGA prototype (mode 13h), code at %04X, buffer at %04X", seg, g_bufseg);
    vgx_init(seg);              /* the screens outside the battle (vgageo.c) */
    return 0;
}
