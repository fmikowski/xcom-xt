/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Hercules start (an overlay section with src/hgcasm.asm; runs once):
   finds the card, sets 720x348 graphics with both pages, fills the row
   tables and copies the copier (hgcasm.asm) into a heap block, which the
   stubs in src/video.asm call from then on. The game itself runs as on
   CGA: CGA packs, the CGA picture in the second page at B800, shown
   byte for byte in page 0 (640x200: each 2-bit pixel its own 2-pixel
   pattern). */
#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "input.h"
#include "pak.h"
#include "uik.h"

#define LO          0x000       /* block layout: src/hgcasm.asm */
#define HI          0x0C8
#define PMIN        0x190
#define ROWOFF      0x1A0
#define LINE        0x330
#define CODE_OFF    0x4C0
#define TOP         74          /* (348 - 200) / 2 lines above the picture */

/* Hercules line L's offset in page 0, the picture 5 bytes in. */
#define HLINE(L)    (((L) & 3) * 0x2000U + ((L) >> 2) * 90U + 5)

unsigned __cdecl hgc_blob(unsigned seg);

/* A 6845 at 3D4h (CGA, or a colour EGA / VGA) keeps what is written to the
   cursor register: its memory is at B800, where the game would draw. */
static int colour_card(void)
{
    unsigned char old;
    int same;

    outp(0x3D4, 0x0F);
    old = (unsigned char)inp(0x3D5);
    outp(0x3D5, 0x5A);
    same = inp(0x3D5) == 0x5A;
    outp(0x3D5, 0xA5);
    same &= inp(0x3D5) == 0xA5;
    outp(0x3D5, old);
    return same;
}

/* An MDA has no vertical retrace bit (3BAh bit 7); a Hercules card's
   changes 50 times a second. */
static int retrace(void)
{
    unsigned char a = (unsigned char)(inp(0x3BA) & 0x80);
    unsigned i, k;

    for (k = 0; k < 4; k++)
        for (i = 0; i < 0x8000; i++)
            if ((inp(0x3BA) & 0x80) != a)
                return 1;
    return 0;
}

/* With the configuration switch at "full" (3BFh = 3) the second page
   answers at B800, apart from the first. */
static int two_pages(void)
{
    volatile unsigned char far *p0 = (unsigned char far *)MK_FP(0xB000, 0x7FF0);
    volatile unsigned char far *p1 = (unsigned char far *)MK_FP(0xB800, 0x7FF0);

    outp(0x3BF, 3);
    *p0 = 0x5A;
    *p1 = 0xA5;
    if (*p0 != 0x5A || *p1 != 0xA5)
        return 0;
    *p1 = 0x3C;
    return *p0 == 0x5A && *p1 == 0x3C;
}

/* gfx_init calls this when HERC is asked (want 2) or the BIOS says mono. */
int hgc_init(int want)
{
    static const unsigned char crtc[12] = {
        0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00
    };
    const char *why = NULL;
    unsigned char far *b;
    unsigned seg, size, i;
    void *p;
    union REGS r;

    if (want == 1)
        return 1;                       /* CGA asked */
    r.h.ah = 0x12;                      /* an EGA (a mono one too) */
    r.h.bl = 0x10;
    int86(0x10, &r, &r);
    if (r.h.bl != 0x10)
        why = "an EGA or VGA is there";
    else if (colour_card())
        why = "a colour card answers at 3D4h (its memory is at B800)";
    else if (!retrace())
        why = "no vertical retrace at 3BAh (an MDA, not a Hercules card)";
    else if (!two_pages()) {
        outp(0x3BF, 0);
        why = "no second page at B800 (a 64 KB Hercules card is needed)";
    }
    if (!why) {
        size = CODE_OFF + hgc_blob(0);
        p = malloc(size + 15);
        if (!p) {
            outp(0x3BF, 0);
            why = "out of memory";
        }
    }
    if (why) {
        log_printf("video: no Hercules: %s", why);
        if (want != 2)
            return 1;
        printf("HERC: %s.\n", why);
        return 3;
    }
    seg = FP_SEG(p) + ((FP_OFF(p) + 15) >> 4);
    b = (unsigned char far *)MK_FP(seg, 0);
    for (i = 0; i < 200; i++) {
        ((unsigned far *)(b + ROWOFF))[i] = 0x8000U + (i & 1) * 0x2000U + (i >> 1) * 80;
        ((unsigned far *)(b + LINE))[i] = HLINE(i + TOP);
    }
    _fmemset(b + LO, 0xFF, 200);
    _fmemset(b + HI, 0, 200);
    *(unsigned far *)(b + PMIN) = 200;
    *(unsigned far *)(b + PMIN + 2) = 0;
    *(unsigned far *)(b + PMIN + 4) = 0;
    hgc_blob(seg);

    /* 720x348 graphics, page 0 shown; both pages cleared. */
    outp(0x3BF, 3);
    outp(0x3B8, 0x02);                  /* graphics, video off */
    for (i = 0; i < 12; i++) {
        outp(0x3B4, i);
        outp(0x3B5, crtc[i]);
    }
    _fmemset(MK_FP(0xB000, 0), 0, 0x8000);
    _fmemset(MK_FP(0xB800, 0), 0, 0x8000);
    outp(0x3B8, 0x0A);                  /* graphics, video on, page 0 */
    hgc_set(seg, CODE_OFF);
    g_herc = 1;
    /* CGA colours 1 and 2 are the same grey here: a selected list row's
       text (hilite on sel) and button text on the fill would vanish, so
       they turn white (the grey and purple themes). */
    for (i = 0; i < N_THEMES; i++) {
        UkTheme *t = (UkTheme *)&g_cga_themes[i];
        if (t->hilite * t->sel == 2)
            t->hilite = 3;
        if (t->btext * t->btn[2] == 2)
            t->btext = 3;
    }
    log_printf("video: Hercules, 640x200 from the CGA picture (copier %u bytes)", size);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Battle sprites (main.c, after the setup's loads and floor_load, every
   battle): on Hercules a game pixel shows as its 2-bit value, so colours
   1 and 2 are the same half grey and 3 is white; the ground's brown would
   be white, the units no brighter. So terrain and unit sprites get
   colours of their own here: terrain green and red -> 1, brown -> 2 (all
   half grey: the ground dithered with black comes out dark); units green
   and brown -> 3 (white), red, their darkest, -> 0 (black outlines and
   shading). The cursor, floor items,
   effects, the panel and the screens keep theirs. Only the colour bits
   change: the masks are untouched (a unit's red pixel turned 0 stays
   drawn: black). */
static const unsigned char herc_map[2][4] = { { 0, 1, 1, 2 }, { 0, 3, 0, 3 } };

/* Masked format: u8 first row, u8 rows, u16 mask bytes, u16 data bytes,
   ops[rows], masks, data; OR format: u8 first row, u8 rows, rows * 8 data
   bytes (tools/cgasprite.py). */
static void remap(unsigned char *s, const unsigned char *t, int or_fmt)
{
    unsigned n;

    if (or_fmt) {
        n = s[1] * 8;
        s += 2;
    } else {
        n = *(unsigned short *)(s + 4);
        s += 6 + s[1] + *(unsigned short *)(s + 2);
    }
    while (n--) {
        *s = t[*s];
        s++;
    }
}

void hgc_sprites(void)
{
    unsigned char t[2][256], *done;
    unsigned char *seen[96];
    unsigned i, k, j, b, e, m, n = g_pak.n_sprites, terrain = 0, units = 0;

    for (m = 0; m < 2; m++)
        for (i = 0; i < 256; i++) {
            unsigned char v = 0;
            for (k = 0; k < 8; k += 2)
                v |= herc_map[m][(i >> k) & 3] << k;
            t[m][i] = v;
        }
    done = calloc((n + 7) / 8, 1);
    if (!done)
        return;
#define DONE(i)     (done[(i) >> 3] & (1 << ((i) & 7)))
#define SET_DONE(i) (done[(i) >> 3] |= (unsigned char)(1 << ((i) & 7)))
    for (k = 1; k < g_pak.n_mcd; k++) {
        const Mcd *r = &g_pak.mcd[k];
        i = r->sprite;
        if (i < n && g_pak.sprite[i] && !DONE(i)) {
            remap(g_pak.sprite[i], t[0], 0);
            SET_DONE(i);
            terrain++;
        }
        i = OR_SPRITE(r);
        if (MCD_PART(r) == PART_FLOOR && i != OR_NONE && i < n && g_pak.sprite[i] && !DONE(i)) {
            remap(g_pak.sprite[i], t[0], 1);
            SET_DONE(i);
            terrain++;
        }
    }
    /* A unit set runs to the next set, the cursor frames or the end; the
       frames that share a picture (a suit's hovering legs standing in for
       the missing ones) once. */
    for (k = 0; k < g_pak.n_unit_sets; k++) {
        if ((g_alias_sets >> k) & 1)
            continue;                   /* another set's pictures: done there */
        b = g_pak.unit_base[k];
        e = n;
        for (j = 0; j < g_pak.n_unit_sets; j++)
            if (g_pak.unit_base[j] > b && g_pak.unit_base[j] < e)
                e = g_pak.unit_base[j];
        if (g_pak.cursor_base > b && g_pak.cursor_base < e)
            e = g_pak.cursor_base;
        if (e > b + 96)
            e = b + 96;
        for (m = 0, i = b; i < e; i++) {
            unsigned char *s = g_pak.sprite[i];
            if (!s || DONE(i))
                continue;
            for (j = 0; j < m && seen[j] != s; j++)
                ;
            if (j < m)
                continue;
            seen[m++] = s;
            remap(s, t[1], 0);
            SET_DONE(i);
            units++;
        }
    }
    free(done);
    log_printf("video: Hercules colours for %u terrain and %u unit sprites", terrain, units);
}
