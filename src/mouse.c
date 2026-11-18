/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mouse: the DOS driver (INT 33h) for position and buttons, and a software
   pointer drawn by the engine. The driver's own pointer is never shown: it
   is drawn from the mouse interrupt and would change EGA registers in the
   middle of our own planar writes (EGA registers cannot be read back).

   The pointer is drawn only while the engine polls for input; code that
   writes video memory calls mouse_hide_rect first (hud.c, gfx.c, geo.c),
   and input_key hides it before returning an event. It reappears at the
   next poll. */

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "input.h"
#include "timer.h"

int g_mouse;                    /* 1 = driver present */
int g_mouse_x, g_mouse_y;       /* pointer, screen pixels */

#define CUR_H 14

/* Arrow: outline (mask) and white fill, 8 pixels wide. */
static const unsigned char __near cur_mask[CUR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFC, 0xDE, 0x8F, 0x0F, 0x06
};
static const unsigned char __near cur_fill[CUR_H] = {
    0x00, 0x00, 0x40, 0x60, 0x70, 0x78, 0x7C, 0x7E, 0x70, 0x58, 0x0C, 0x06, 0x06, 0x00
};

/* Busy pointer: an hourglass, black frame and sand, white glass; the two
   frames are each other upside down (the sand above, then below). */
static const unsigned char __near busy_mask[CUR_H] = {
    0xFF, 0xFF, 0x7E, 0x7E, 0x3C, 0x18, 0x18, 0x3C, 0x7E, 0x7E, 0xFF, 0xFF, 0, 0
};
static const unsigned char __near busy_fill[2][CUR_H] = {
    { 0x00, 0x7E, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3C, 0x3C, 0x7E, 0x00, 0, 0 },
    { 0x00, 0x7E, 0x3C, 0x3C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x7E, 0x00, 0, 0 }
};
/* the shape (also src/vgaasm.asm) */
const unsigned char __near *g_ms_pm = cur_mask, __near *g_ms_pf = cur_fill;
#define pm g_ms_pm
#define pf g_ms_pf

static int shown, cur_x, cur_y, buttons;
static unsigned char save[CUR_H][3];        /* CGA background */

void ms_call(union REGS *r)            /* (also src/vbench.c) */
{
    int86(0x33, r, r);
}

int mouse_init(void)
{
    union REGS r;
    void far *vec = _dos_getvect(0x33);
    unsigned char far *p = (unsigned char far *)vec;

    g_mouse = 0;
    shown = 0;
    if (!vec || *p == 0xCF) {           /* no handler, or a bare IRET */
        log_printf("mouse: no driver (INT 33h at %04X:%04X)", FP_SEG(vec), FP_OFF(vec));
        return 0;
    }
    r.x.ax = 0x21;                      /* software reset: no video access */
    ms_call(&r);
    if (r.x.ax != 0xFFFF) {
        /* Older drivers (before Microsoft's 6.0 and many clones) have no
           software reset: the full reset, which also hides the pointer. */
        log_printf("mouse: no software reset (AX %04X), full reset", r.x.ax);
        r.x.ax = 0;
        ms_call(&r);
        if (r.x.ax != 0xFFFF) {
            log_printf("mouse: driver says no mouse (AX %04X)", r.x.ax);
            return 0;
        }
    }
    g_mouse = 1;
    r.x.ax = 0x24;                      /* version, if the driver knows it */
    r.x.bx = 0;
    r.x.cx = 0;
    ms_call(&r);
    log_printf("mouse: driver found (version %x.%02x, type %u, IRQ %u)",
               r.h.bh, r.h.bl, r.h.ch, r.h.cl);
    r.x.ax = 2;                         /* keep the driver's pointer hidden */
    ms_call(&r);
    r.x.ax = 7;                         /* drivers count 640 columns in modes 4 and 0Dh */
    r.x.cx = 0;
    r.x.dx = 639;
    ms_call(&r);
    r.x.ax = 8;
    r.x.cx = 0;
    r.x.dx = 199;
    ms_call(&r);
    mouse_set(160, 100);
    return 1;
}

void mouse_shutdown(void)
{
    union REGS r;

    mouse_hide();
    if (!g_mouse)
        return;
    r.x.ax = 0;
    ms_call(&r);
    g_mouse = 0;
}

int mouse_down(void)
{
    mouse_poll();
    return buttons & 1;
}

void mouse_set(int x, int y)
{
    union REGS r;

    g_mouse_x = x;
    g_mouse_y = y;
    if (!g_mouse)
        return;
    r.x.ax = 4;
    r.x.cx = x * 2;
    r.x.dx = y;
    ms_call(&r);
}

/* ------------------------------------------------------------------ */
/* The pointer */

/* EGA: the background goes to unused video memory (beyond the 8000-byte
   page) and back with write mode 1, which copies all four planes through
   the latches; the arrow is drawn with set/reset and the bit mask. */
#define SAVE_OFF 0xFF00U

static void draw_ega(int x, int y)
{
    volatile unsigned char far *vram = (volatile unsigned char far *)MK_FP(0xA000, 0);
    unsigned off = y * 40 + (x >> 3), s = SAVE_OFF;
    int sh = x & 7, r, two = (x >> 3) < 39, rows = y + CUR_H > 200 ? 200 - y : CUR_H;
    unsigned char latch;

    outp(0x3C4, 2);
    outp(0x3C5, 0x0F);
    outpw(0x3CE, 0x0105);               /* write mode 1: latch copy */
    for (r = 0; r < rows; r++, s += 2) {
        latch = vram[off + r * 40];
        vram[s] = latch;
        latch = vram[off + r * 40 + 1];
        vram[s + 1] = latch;
    }
    outpw(0x3CE, 0x0005);               /* write mode 0 */
    outpw(0x3CE, 0x0F01);               /* set/reset on all planes */
    outpw(0x3CE, 0x0000);               /* black outline */
    for (r = 0; r < rows; r++) {
        unsigned m = (unsigned)pm[r] << 8 >> sh, o = off + r * 40;
        outpw(0x3CE, (m & 0xFF00) | 8); /* bit mask */
        latch = vram[o];
        vram[o] = latch;
        if (two) {
            outpw(0x3CE, (m << 8) | 8);
            latch = vram[o + 1];
            vram[o + 1] = latch;
        }
    }
    outpw(0x3CE, 0x0F00);               /* white fill */
    for (r = 0; r < rows; r++) {
        unsigned f = (unsigned)pf[r] << 8 >> sh, o = off + r * 40;
        if (!f)
            continue;
        outpw(0x3CE, (f & 0xFF00) | 8);
        latch = vram[o];
        vram[o] = latch;
        if (two) {
            outpw(0x3CE, (f << 8) | 8);
            latch = vram[o + 1];
            vram[o + 1] = latch;
        }
    }
    outpw(0x3CE, 0xFF08);
    outpw(0x3CE, 0x0001);
}

static void erase_ega(int x, int y)
{
    volatile unsigned char far *vram = (volatile unsigned char far *)MK_FP(0xA000, 0);
    unsigned off = y * 40 + (x >> 3), s = SAVE_OFF;
    int r, two = (x >> 3) < 39, rows = y + CUR_H > 200 ? 200 - y : CUR_H;
    unsigned char latch;

    outp(0x3C4, 2);
    outp(0x3C5, 0x0F);
    outpw(0x3CE, 0x0105);
    for (r = 0; r < rows; r++, s += 2, off += 40) {
        latch = vram[s];
        vram[off] = latch;
        if (two) {
            latch = vram[s + 1];
            vram[off + 1] = latch;
        }
    }
    outpw(0x3CE, 0x0005);
}

/* 8 pixels of a 1-bit row as 2-bit CGA pixels (colour 3 where set). */
static unsigned expand2(unsigned char b)
{
    unsigned w = 0;
    int i;
    for (i = 0; i < 8; i++)
        if (b & (0x80 >> i))
            w |= 0xC000 >> (2 * i);
    return w;
}

static unsigned char far *cga_row(int x, int y)
{
    return (unsigned char far *)MK_FP(0xB800, (y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 2));
}

/* (VGA: src/vgainit.c points these two at its own code) */
void __near ms_draw_cga(int x, int y)
{
    int sh = (x & 3) * 2, r, i, nb = 80 - (x >> 2);

    if (nb > 3)
        nb = 3;
    for (r = 0; r < CUR_H && y + r < 200; r++) {
        unsigned char far *v = cga_row(x, y + r);
        unsigned long m = (unsigned long)expand2(pm[r]) << 8 >> sh;
        unsigned long f = (unsigned long)expand2(pf[r]) << 8 >> sh;
        for (i = 0; i < nb; i++) {
            int s = 16 - 8 * i;
            save[r][i] = v[i];
            v[i] = (unsigned char)((v[i] & ~(m >> s)) | (f >> s));
        }
    }
}

void __near ms_erase_cga(int x, int y)
{
    int r, i, nb = 80 - (x >> 2);

    if (nb > 3)
        nb = 3;
    for (r = 0; r < CUR_H && y + r < 200; r++) {
        unsigned char far *v = cga_row(x, y + r);
        for (i = 0; i < nb; i++)
            v[i] = save[r][i];
    }
}

/* Hercules: the pointer's box is copied at once (src/gfx.h). */
#define hgc_box() hgc_show(cur_x, cur_y, cur_x + 12, cur_y + CUR_H)

static void draw(void)
{
    if (g_video == VIDEO_EGA)
        draw_ega(cur_x, cur_y);
    else {
        ms_draw_cga(cur_x, cur_y);
        hgc_box();
    }
    shown = 1;
}

static void erase(void)
{
    if (!shown)
        return;
    if (g_video == VIDEO_EGA)
        erase_ega(cur_x, cur_y);
    else {
        ms_erase_cga(cur_x, cur_y);
        hgc_box();
    }
    shown = 0;
}

void cursor_show(void)                  /* (also src/vbench.c) */
{
    if (shown || !g_mouse)
        return;
    cur_x = g_mouse_x;
    cur_y = g_mouse_y;
    pm = cur_mask;
    pf = cur_fill;
    draw();
}

#ifdef GAPDIAG
/* XT_RDDIAG=gap builds: nearly everything that writes video memory hides
   the pointer first, and input_key hides it before it returns a key, so
   these calls mark each change of the screen and each wait for input. A
   stretch of 9 BIOS ticks (half a second) or more between two marks is
   logged as a GAP, with the file reads in it (what = the mark ending it:
   0 drawing, 1 input, 2 the busy pointer; 3 the input came: the time
   waiting for it is not counted). */
void gap_mark(int what)
{
    static unsigned long t0;
    static unsigned r0;
    static unsigned long b0;
    unsigned long now = *(volatile unsigned long far *)MK_FP(0x40, 0x6C);

    if (t0 && now - t0 >= 9 && what != 3)
        log_printf("GAP %lu ms%s, %u reads %lu KB", (now - t0) * 55,
                   what == 1 ? " before input" : what == 2 ? " before busy" : "",
                   rd_count() - r0, (rd_bytes() - b0) / 1024);
    t0 = now;
    r0 = rd_count();
    b0 = rd_bytes();
}
#define GAP(w) gap_mark(w)
#else
#define GAP(w)
#endif

void mouse_hide(void)
{
    GAP(0);
    erase();
}

void mouse_hide_rect(int x0, int y0, int x1, int y1)
{
    int px;

    GAP(0);
    hgc_mark(x0, y0, x1, y1);   /* Hercules: copied at the next flush */
    if (!shown)
        return;
    /* The pointer covers whole bytes: 16 pixels on EGA, 12 on CGA. */
    px = g_video == VIDEO_EGA ? cur_x & ~7 : cur_x & ~3;
    if (x1 <= px || x0 >= px + 16 || y1 <= cur_y || y0 >= cur_y + CUR_H)
        return;
    erase();
}

/* ------------------------------------------------------------------ */
/* Busy pointer (see input.h) */

#define TICKS (*(volatile unsigned far *)MK_FP(0x40, 0x6C))

static unsigned char busy;      /* hourglass frames since the last input */
static unsigned busy_t;         /* BIOS tick of the last input or frame */

/* The driver's position (buttons left for mouse_poll's clicks). */
static int where(union REGS *r)
{
    r->x.ax = 3;
    ms_call(r);
    g_mouse_x = r->x.cx >> 1 > 319 ? 319 : r->x.cx >> 1;
    g_mouse_y = r->x.dx > 199 ? 199 : r->x.dx;
    return r->x.bx & 3;
}

void busy_tick(void)
{
    union REGS r;

    if (TICKS - busy_t < (busy ? 5u : 9u))
        return;
    busy_t = TICKS;
    erase();
    if (g_mouse) {
        where(&r);
        cur_x = g_mouse_x;
        cur_y = g_mouse_y;
    } else {
        cur_x = 312;            /* no mouse: the top right corner */
        cur_y = 1;
    }
    pm = busy_mask;
    pf = busy_fill[busy & 1];
    draw();
#ifdef GAPDIAG
    if (!busy)
        log_printf("BUSY on");
#endif
    if (!++busy)
        busy = 2;
    GAP(2);
}

void busy_show(void)
{
    busy_t = TICKS - 9;
    busy_tick();
}

void busy_idle(void)
{
    busy_t = TICKS;
    if (!busy)
        return;
#ifdef GAPDIAG
    log_printf("BUSY off after %u frames", busy);
#endif
    busy = 0;
    if (pm == busy_mask)
        erase();
}

/* ------------------------------------------------------------------ */
/* Polling */

/* A click that comes and goes between two polls is not lost: INT 33h
   function 5 counts the presses of a button since its last call and says
   where the last one was (function 3 alone sees only the button's state,
   and a slow 8088 busy drawing is often away for longer than a click). A
   count older than a second (no poll meanwhile: a load, a scene) is
   dropped. The counts are the only source of clicks: also taking a newly
   held button from function 3 gave one press twice when it fell between
   the two calls (the hand menu's choice then fired the shot at once). */
static unsigned poll_t;         /* BIOS tick of the last poll */

unsigned mouse_poll(void)
{
    union REGS r;
    int x, y, i, got = 0, px[2], py[2];
    unsigned t = TICKS;

    busy_idle();
    hgc_flush(10);      /* Hercules: what was drawn before waiting */
    if (!g_mouse)
        return 0;
    buttons = where(&r);
    x = g_mouse_x;
    y = g_mouse_y;
    for (i = 0; i < 2; i++) {
        r.x.ax = 5;
        r.x.bx = i;
        ms_call(&r);
        if (r.x.bx && t - poll_t <= 18u) {
            got |= 1 << i;
            px[i] = r.x.cx >> 1 > 319 ? 319 : r.x.cx >> 1;
            py[i] = r.x.dx > 199 ? 199 : r.x.dx;
        }
    }
    poll_t = t;
    if (shown && (x != cur_x || y != cur_y))
        mouse_hide();
    cursor_show();
    if (!got)
        return 0;
    i = !(got & 1);
    g_mouse_x = px[i];          /* where the click was, not where the pointer is now */
    g_mouse_y = py[i];
    return i ? KEY_RCLICK : KEY_LCLICK;
}
