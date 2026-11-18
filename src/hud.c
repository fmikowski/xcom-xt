/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <conio.h>
#include <i86.h>
#include <string.h>
#include "gfx.h"
#include "hud.h"
#include "input.h"

static const unsigned char *font;

int hud_init(void)
{
    union REGPACK r;

    memset(&r, 0, sizeof r);
    if (g_video == VIDEO_EGA) {
        r.w.ax = 0x1130;        /* EGA BIOS: get font pointer */
        r.h.bh = 3;             /* 8x8 font, characters 0-127 */
        intr(0x10, &r);
        font = (const unsigned char *)MK_FP(r.w.es, r.w.bp);
    } else {
        /* The CGA graphics font lives at a fixed place in the system BIOS. */
        font = (const unsigned char *)MK_FP(0xF000, 0xFA6E);
    }
    return 0;
}

/* CGA palette 0: 1 green, 2 red, 3 brown. */
static int cga_colour(int ega)
{
    switch (ega) {
    case 12: case 4: return 2;
    case 15: case 14: return 3;
    default: return 1;
    }
}

/* (VGA: src/vgainit.c points this and hud_clear_rows at its own code) */
void __near hud_text_cga(int col, int row, int colour, const char *s, int len)
{
    unsigned char expand[16];
    int c = cga_colour(colour), i, k, b;

    for (i = 0; i < 16; i++) {
        unsigned char v = 0;
        for (b = 0; b < 4; b++)
            if (i & (8 >> b))
                v |= c << (6 - 2 * b);
        expand[i] = v;
    }
    for (i = 0; i < len; i++) {
        const unsigned char *g = font + ((unsigned char)s[i] & 0x7F) * 8;
        for (k = 0; k < 8; k++) {
            unsigned y = row * 8 + k;
            unsigned char *d = (unsigned char *)MK_FP(0xB800,
                (y & 1) * 0x2000 + (y >> 1) * 80 + (col + i) * 2);
            d[0] = expand[g[k] >> 4];
            d[1] = expand[g[k] & 15];
        }
    }
}

void hud_text(int col, int row, int colour, const char *s)
{
    unsigned char *vram = (unsigned char *)MK_FP(0xA000, 0);
    int len = strlen(s), p, i, k;

    if (col + len > 40)
        len = 40 - col;
    mouse_hide_rect(col * 8, row * 8, (col + len) * 8, row * 8 + 8);
    if (g_video == VIDEO_CGA) {
        hud_text_cga(col, row, colour, s, len);
        hgc_flush(0);   /* Hercules: shown at once (the alien turn's lines) */
        return;
    }
    for (p = 0; p < 4; p++) {
        int on = colour & (1 << p);
        outp(0x3C4, 2);
        outp(0x3C5, 1 << p);
        for (i = 0; i < len; i++) {
            const unsigned char *g = font + ((unsigned char)s[i] & 0x7F) * 8;
            unsigned char *d = vram + (unsigned)row * 320 + col + i;
            for (k = 0; k < 8; k++, d += 40)
                *d = on ? g[k] : 0;
        }
    }
    outp(0x3C4, 2);
    outp(0x3C5, 0x0F);
}

void hud_clear_rows(int row, int count)
{
    mouse_hide_rect(0, row * 8, 320, (row + count) * 8);
    if (g_video == VIDEO_CGA) {
        unsigned y;
        for (y = row * 8; y < (unsigned)(row + count) * 8; y++)
            _fmemset(MK_FP(0xB800, (y & 1) * 0x2000 + (y >> 1) * 80), 0, 80);
        return;
    }
    outp(0x3C4, 2);
    outp(0x3C5, 0x0F);
    _fmemset(MK_FP(0xA000, (unsigned)row * 320), 0, (unsigned)count * 320);
}

void hud_field(int col, int row, int width, int colour, const char *s)
{
    char buf[41];
    int n = strlen(s);

    if (width > 40)
        width = 40;
    if (n > width)
        n = width;
    memcpy(buf, s, n);
    memset(buf + n, ' ', width - n);
    buf[width] = 0;
    hud_text(col, row, colour, buf);
}
