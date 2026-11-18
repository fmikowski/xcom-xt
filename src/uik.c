/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The original game's look: pictures from UIE.PAK / UIC.PAK, the original
   fonts, windows, buttons, lists and message boxes (see uik.h). */

#include <conio.h>
#include <dos.h>
#include <fcntl.h>
#include <i86.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "uik.h"

#define SAVE_OFF    0x2000u     /* EGA: the clean screen (page 1) */
#define FONT_OFF    0x8000u     /* EGA: fonts in plane 0 (pages 4-5) */
#define FONT_GAP    0x0800u     /* small font size, rounded up */

#define VGA         ((volatile unsigned char far *)MK_FP(0xA000, 0))

/* Edge, middle, centre; title, text, hilite, sel, dim; button light edge,
   light, fill, dark, dark edge; button text. */
static const UkTheme ega_themes[N_THEMES] = {
    { { 2, 2, 10 }, 14, 10, 15, 2, 2, { 10, 10, 2, 8, 0 }, 10, 14 },      /* green */
    { { 1, 1, 9 }, 11, 9, 15, 1, 1, { 9, 9, 1, 8, 0 }, 11, 13 },          /* blue */
    { { 8, 8, 7 }, 15, 7, 14, 8, 8, { 7, 7, 8, 0, 0 }, 15, 7 },           /* grey */
    { { 5, 5, 13 }, 13, 11, 15, 5, 5, { 13, 13, 5, 8, 0 }, 15, 11 },      /* purple */
    { { 14, 6, 14 }, 15, 14, 15, 6, 6, { 14, 14, 6, 0, 0 }, 14, 14 },     /* gold: the base screens */
};
/* CGA palettes 0 (green, red, brown) and 1 (cyan, magenta, white);
   Hercules changes a few (hgcinit.c). */
const UkTheme g_cga_themes[N_THEMES] = {
    { { 1, 1, 3 }, 3, 1, 3, 2, 1, { 3, 3, 1, 0, 0 }, 3, 3 },
    { { 1, 1, 3 }, 3, 1, 3, 2, 1, { 3, 3, 1, 0, 0 }, 3, 3 },
    { { 2, 2, 3 }, 3, 3, 1, 2, 2, { 3, 3, 2, 0, 0 }, 3, 3 },
    { { 2, 2, 3 }, 3, 1, 3, 2, 2, { 3, 3, 2, 0, 0 }, 3, 1 },
    { { 1, 1, 3 }, 3, 1, 3, 2, 1, { 3, 3, 1, 0, 0 }, 3, 3 },
};

const UkTheme *g_uk = &ega_themes[0];
int g_uk_ok;
int g_uk_row = -1;
int g_uk_bh = 16;

/* The VGA prototype (src/vgainit.c) finds the pack's handle and name and
   the CGA routines marked VGA below: it points them at its own code. */
int g_uk_fd = -1;
char g_uk_name[] = "UIC.PAK";           /* (VGA: UIV.PAK) */
#define fd g_uk_fd
static int nest, cga_pal;
static int fonts_in_vram;               /* EGA fonts loaded into video memory */
static unsigned char far *cfont[2];     /* CGA: fonts on the heap */
static unsigned char far *csave;        /* CGA: the clean rows */
static int save_y0, save_y1;
static unsigned char buf[240];
/* Picture reads go through bp: buf, or 2 KB while held (uk_hold), when
   the pack's offset table is also kept (table). */
static unsigned char *bp = buf;
static unsigned bsize = sizeof buf;
static unsigned long *table;

/* The EGA registers as the drawing code here expects them: all planes,
   replace, write mode 0, plane 0 read (other code may leave them set). */
static void ega_reset(void)
{
    outpw(0x3C4, 0x0F02);
    outpw(0x3CE, 0x0003);
    outpw(0x3CE, 0x0005);
    outpw(0x3CE, 0x0004);
    outpw(0x3CE, 0xFF08);
}

/* Night battles: day colours to the registers that show them (uik.h). */
const unsigned char *g_uk_cmap;
void __cdecl uk_remap4(unsigned char far *planes, const unsigned char far *cmap);
void __cdecl uk_remap_vram(unsigned dst, unsigned bytes, unsigned rows,
                           const unsigned char far *cmap);

static const unsigned char far *font_ptr(int f)
{
    if (g_video == VIDEO_EGA)
        return fonts_in_vram ? (const unsigned char far *)(VGA + FONT_OFF + f * FONT_GAP) : NULL;
    return cfont[f];
}

/* ------------------------------------------------------------------ */
/* Pack */

static long entry_offset(unsigned id)
{
    unsigned long off;

    if (table)
        return (long)table[id];
    if (fd < 0 || lseek(fd, 8L + 4L * id, SEEK_SET) < 0 || read(fd, &off, 4) != 4)
        return 0;
    return (long)off;
}

static int seek_entry(unsigned id)
{
    long off = entry_offset(id);
    return off && lseek(fd, off, SEEK_SET) == off;
}

/* Loads font f: EGA into video memory plane 0, CGA onto the heap. */
static void load_font(int f)
{
    unsigned n = f == UK_SMALL ? 2 + 96 + 96 * 18 : 2 + 96 + 96 * 64, k;

    if (!seek_entry(UI_FONT_SMALL + f))
        return;
    if (g_video == VIDEO_CGA) {
        cfont[f] = malloc(n);
        if (cfont[f] && read(fd, cfont[f], n) != (int)n) {
            free(cfont[f]);
            cfont[f] = NULL;
        }
        return;
    }
    outpw(0x3C4, 0x0102);               /* plane 0 */
    for (k = 0; k < n; k += sizeof buf) {
        unsigned c = n - k < sizeof buf ? n - k : sizeof buf;
        read(fd, buf, c);
        _fmemcpy((void far *)(VGA + FONT_OFF + f * FONT_GAP + k), buf, c);
    }
    outpw(0x3C4, 0x0F02);
}

static int hold;

/* The pack is open only while drawing (DOS gives programs few handles). */
static int pack_fd(void)
{
    if (fd < 0)
        fd = open(g_video == VIDEO_EGA ? "UIE.PAK" : g_uk_name, O_RDONLY | O_BINARY);
    return fd >= 0;
}

static void pack_done(void)
{
    if (!hold && fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void uk_hold(int on)
{
    hold = on;
    if (on && !table && pack_fd()) {
        table = malloc(UI_COUNT * 4);
        if (table && (lseek(fd, 8L, SEEK_SET) != 8L
                      || read(fd, table, UI_COUNT * 4) != UI_COUNT * 4)) {
            free(table);
            table = NULL;
        }
        bp = malloc(2048);
        bsize = 2048;
        if (!bp) {
            bp = buf;
            bsize = sizeof buf;
        }
    } else if (!on) {
        free(table);
        table = NULL;
        if (bp != buf)
            free(bp);
        bp = buf;
        bsize = sizeof buf;
    }
    pack_done();
}

int uk_open(void)
{
    if (nest++)
        return g_uk_ok;
    g_uk_ok = pack_fd();
    if (!g_uk_ok)
        return 0;
    if (g_video == VIDEO_CGA || !fonts_in_vram) {
        load_font(UK_SMALL);
        load_font(UK_BIG);
        fonts_in_vram = g_video == VIDEO_EGA;
    }
    pack_done();
    return 1;
}

void uk_close(void)
{
    if (!nest || --nest)
        return;
    uk_hold(0);
    pack_done();
    free(cfont[0]);
    free(cfont[1]);
    free(csave);
    cfont[0] = cfont[1] = csave = NULL;
    save_y0 = save_y1 = 0;
    uk_cga_palette(0);
    uk_invalidate();
}

static int suspended;

void uk_suspend(void)
{
    suspended = nest;
    if (nest) {
        nest = 1;
        uk_close();
    }
}

void uk_resume(void)
{
    if (suspended) {
        uk_open();
        nest = suspended;
        suspended = 0;
    }
}

void uk_theme(int t)            /* (VGA) */
{
    g_uk = g_video == VIDEO_EGA ? &ega_themes[t] : &g_cga_themes[t];
}

void uk_cga_palette(int p)      /* (VGA) */
{
    union REGS r;

    if (g_video != VIDEO_CGA || p == cga_pal)
        return;
    cga_pal = p;
    if (g_herc)         /* Hercules: the greys are the same for both */
        return;
    r.h.ah = 0x0B;
    r.h.bh = 1;
    r.h.bl = (unsigned char)(p >> 1);
    int86(0x10, &r, &r);
    r.h.ah = 0x0B;
    r.h.bh = 0;
    r.h.bl = (unsigned char)((p & 1) << 4);     /* intensity */
    int86(0x10, &r, &r);
}

/* ------------------------------------------------------------------ */
/* Pictures */

#pragma pack(1)
typedef struct {
    unsigned short x, y, w, h;
    unsigned char flags, reserved;
} PicHead;
#pragma pack()

static unsigned char far *cga_addr(int x, int y)
{
    return (unsigned char far *)MK_FP(0xB800, (y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 2));
}

/* EGA: the plain pictures that frame screens (backgrounds, the geoscape's
   button panel, windows: 1 KB a plane or more) stay in spare video memory
   once drawn, one a slot (rows of 40 bytes, like the screen), so a screen
   that comes back is copied through the latches instead of read again:
   the base and research screens' backgrounds were most of the geoscape's
   disk reads. Video memory, a plane: 0000 the screen, 2000 the clean
   copy, 4000 and 6000 slots, 8000 the fonts (to A062), A080, BFC0 and DF00
   slots, FF00 the mouse pointer's background (mouse.c). The video mode is
   set once, so the slots last as long as the fonts. */
#define N_SLOTS     5
#define SLOT_MIN    1024u               /* bytes a plane */

static const unsigned slot_off[N_SLOTS] = { 0x4000u, 0x6000u, 0xA080u, 0xBFC0u, 0xDF00u };
static PicHead slot_head[N_SLOTS];
static unsigned slot_id[N_SLOTS];       /* entry + 1; 0 = free */
static unsigned slot_use[N_SLOTS], slot_clock;

static void latch_copy(unsigned dst, unsigned src, unsigned n, unsigned rows);

/* The slot holding entry id, or -1 (CGA: none). */
static int slot_find(unsigned id)
{
    int s;

    if (g_video != VIDEO_EGA)
        return -1;
    for (s = 0; s < N_SLOTS; s++)
        if (slot_id[s] == id + 1) {
            slot_use[s] = ++slot_clock;
            return s;
        }
    return -1;
}

/* The picture just drawn at dst into id's slot or the least recently used. */
static void slot_keep(unsigned id, const PicHead *h, unsigned dst)
{
    int s, old = 0;

    for (s = 0; s < N_SLOTS; s++) {
        if (slot_id[s] == id + 1) {
            old = s;
            break;
        }
        if (slot_use[s] < slot_use[old])
            old = s;
    }
    latch_copy(slot_off[old], dst, h->w >> 3, h->h);
    slot_head[old] = *h;
    slot_id[old] = id + 1;
    slot_use[old] = ++slot_clock;
}

/* s: the picture's slot, or -1 to read it from the pack. */
static void pic_ega(const PicHead *h, int x, int y, unsigned base, unsigned id, int s)
{
    unsigned nb = h->w >> 3, dst = base + y * 40 + (x >> 3), r, k, rows;
    int p, whole = 1;

    ega_reset();
    if (!(h->flags & UIF_MASKED)) {
        if (s >= 0)
            latch_copy(dst, slot_off[s], nb, h->h);
        else {
            for (p = 0; p < 4; p++) {
                outpw(0x3C4, (0x100 << p) | 2);
                if (nb == 40) {
                    whole &= read(fd, (void far *)(VGA + dst), h->h * 40) == h->h * 40;
                    continue;
                }
                for (r = 0; r < h->h; r += rows) {
                    rows = bsize / nb;
                    if (rows > h->h - r)
                        rows = h->h - r;
                    whole &= read(fd, bp, rows * nb) == rows * nb;
                    for (k = 0; k < rows; k++)
                        _fmemcpy((void far *)(VGA + dst + (r + k) * 40), bp + k * nb, nb);
                }
            }
            if (whole && nb * h->h >= SLOT_MIN)
                slot_keep(id, h, dst);
        }
        outpw(0x3C4, 0x0F02);
        if (g_uk_cmap)
            uk_remap_vram(dst, nb, h->h, g_uk_cmap);
        return;
    }
    /* Masked: per byte the mask goes to the bit mask register, the planes
       one by one through the map mask; the latches keep what is under. */
    for (r = 0; r < h->h; r += rows) {
        unsigned char *s = bp;
        unsigned j;
        rows = bsize / (nb * 5);
        if (rows > h->h - r)
            rows = h->h - r;
        read(fd, bp, rows * nb * 5);
        for (j = 0; j < rows; j++) {
            volatile unsigned char far *d = VGA + dst + (r + j) * 40;
            for (k = 0; k < nb; k++, s += 5, d++) {
                unsigned char latch;
                if (!s[0])
                    continue;
                if (g_uk_cmap)
                    uk_remap4(s + 1, g_uk_cmap);
                outpw(0x3CE, (s[0] << 8) | 8);
                latch = *d;
                outpw(0x3C4, 0x0102);
                *d = s[1];
                outpw(0x3C4, 0x0202);
                *d = s[2];
                outpw(0x3C4, 0x0402);
                *d = s[3];
                outpw(0x3C4, 0x0802);
                *d = s[4];
                (void)latch;
            }
        }
    }
    outpw(0x3C4, 0x0F02);
    outpw(0x3CE, 0xFF08);
}

void __near uk_pic_cga(const PicHead *h, int x, int y, int to_save)       /* (VGA) */
{
    unsigned nb = h->w >> 2, r, k, j, rows, rb;
    int masked = h->flags & UIF_MASKED;

    rb = masked ? nb * 2 : nb;
    for (r = 0; r < h->h; r += rows) {
        rows = bsize / rb;
        if (rows > h->h - r)
            rows = h->h - r;
        read(fd, bp, rows * rb);
        for (j = 0; j < rows; j++) {
            const unsigned char *s = bp + j * rb;
            int yy = y + (int)(r + j);
            unsigned char far *d = to_save ? csave + (yy - save_y0) * 80 + (x >> 2)
                                           : cga_addr(x, yy);
            if (to_save && (yy < save_y0 || yy >= save_y1))
                continue;
            if (!masked) {
                _fmemcpy(d, s, nb);
                continue;
            }
            for (k = 0; k < nb; k++)
                if (s[2 * k])
                    d[k] = (unsigned char)((d[k] & ~s[2 * k]) | s[2 * k + 1]);
        }
    }
}

/* Draws entry id on the screen, or (to_save) into the clean copy; x < 0:
   at its stored position, x >= 0x4000: offset by it. */
static int pic(unsigned id, int x, int y, int to_save)
{
    PicHead h;
    int ok = 0, s = slot_find(id);

    if (s >= 0)
        h = slot_head[s];
    else if (!pack_fd())
        return 0;
    if (s >= 0 || (seek_entry(id) && read(fd, &h, sizeof h) == sizeof h)) {
        if (x < 0) {
            x = h.x;
            y = h.y;
        } else if (x >= 0x2000) {      /* uk_pic_in: 0x4000 + an offset (may be < 0) */
            x += h.x - 0x4000;
            y += h.y;
        }
        if (h.flags & UIF_SCREEN)
            uk_cga_palette((h.flags >> 1) & 3);
        if (!to_save)
            mouse_hide_rect(x, y, x + h.w, y + h.h);
        if (g_video == VIDEO_EGA)
            pic_ega(&h, x, y, to_save ? SAVE_OFF : 0, id, s);
        else if (!to_save || csave || g_vga)    /* (VGA: vgxasm.asm keeps it) */
            uk_pic_cga(&h, x, y, to_save);
        ok = 1;
    }
    pack_done();
    return ok;
}

int uk_pic(unsigned id, int x, int y)
{
    return pic(id, x, y, 0);
}

void uk_keep(unsigned key, unsigned check, int x, int y, int w, int h)
{
    PicHead hd;

    if (g_video != VIDEO_EGA)
        return;
    hd.x = check;                       /* a kept picture's x, y: unused */
    hd.w = w;
    hd.h = h;
    hd.flags = 0;
    mouse_hide();
    slot_keep(key, &hd, y * 40 + (x >> 3));
}

int uk_recall(unsigned key, unsigned check, int x, int y)
{
    int s = slot_find(key);

    return s >= 0 && slot_head[s].x == check && pic(key, x, y, 0);
}

int uk_pic_clean(unsigned id)   /* (VGA) */
{
    return pic(id, -1, 0, 1);
}

int uk_pic_box(unsigned id, int *w, int *h)
{
    unsigned short wh[2];
    int s = slot_find(id);

    if (s >= 0) {
        *w = slot_head[s].w;
        *h = slot_head[s].h;
        return 1;
    }
    if (!uk_read(id, 4, wh, 4))
        return 0;
    *w = wh[0];
    *h = wh[1];
    return 1;
}

int uk_pic_in(unsigned id, int x, int y)
{
    return pic(id, x + 0x4000, y, 0);
}

int uk_read(unsigned id, unsigned off, void *dst, unsigned n)
{
    long at;
    int ok = 0;

    if (!pack_fd())
        return 0;
    at = entry_offset(id);
    if (at && lseek(fd, at + off, SEEK_SET) == at + off && read(fd, dst, n) == (int)n)
        ok = 1;
    pack_done();
    return ok;
}

/* ------------------------------------------------------------------ */
/* Shapes */

static const unsigned char cga_map[2][16] = {
    /* palette 0: green 1, red 2, brown 3 */
    { 0, 1, 1, 1, 2, 2, 3, 3, 0, 1, 1, 1, 2, 2, 3, 3 },
    /* palette 1: cyan 1, magenta 2, white 3 */
    { 0, 1, 1, 1, 2, 2, 3, 3, 0, 1, 1, 1, 2, 2, 3, 3 },
};

/* Theme colours are already CGA colours; the 8-15 range of EGA text
   colours given by screens is mapped. */
int __near uk_cga_c(int c)      /* (VGA) */
{
    return c < 4 ? c : cga_map[cga_pal >> 1][c & 15];
}

void uk_fill(int x, int y, int w, int h, int c)        /* (VGA) */
{
    int r;

    if (w <= 0 || h <= 0)
        return;
    mouse_hide_rect(x, y, x + w, y + h);
    if (g_video == VIDEO_EGA) {
        int b0 = x >> 3, b1 = (x + w - 1) >> 3;
        unsigned char m0 = (unsigned char)(0xFF >> (x & 7)),
                      m1 = (unsigned char)(0xFF << (7 - ((x + w - 1) & 7)));
        if (g_uk_cmap)
            c = g_uk_cmap[c & 15];
        ega_reset();
        outpw(0x3CE, (c << 8) | 0);
        outpw(0x3CE, 0x0F01);
        for (r = 0; r < h; r++) {
            volatile unsigned char far *d = VGA + (y + r) * 40 + b0;
            unsigned char latch;
            if (b0 == b1) {
                outpw(0x3CE, ((m0 & m1) << 8) | 8);
                latch = *d;
                *d = latch;
                continue;
            }
            outpw(0x3CE, (m0 << 8) | 8);
            latch = *d;
            *d = latch;
            outpw(0x3CE, (m1 << 8) | 8);
            latch = d[b1 - b0];
            d[b1 - b0] = latch;
            if (b1 - b0 > 1) {
                outpw(0x3CE, 0xFF08);
                _fmemset((void far *)(d + 1), 0xFF, b1 - b0 - 1);
            }
        }
        outpw(0x3CE, 0xFF08);
        outpw(0x3CE, 0x0001);
        return;
    }
    {
        unsigned char pat = (unsigned char)(uk_cga_c(c) * 0x55);
        int b0 = x >> 2, b1 = (x + w - 1) >> 2, b;
        unsigned char m0 = (unsigned char)(0xFF >> ((x & 3) * 2)),
                      m1 = (unsigned char)(0xFF << ((3 - ((x + w - 1) & 3)) * 2));
        for (r = 0; r < h; r++) {
            unsigned char far *d = cga_addr(0, y + r);
            for (b = b0; b <= b1; b++) {
                unsigned char m = 0xFF;
                if (b == b0)
                    m &= m0;
                if (b == b1)
                    m &= m1;
                d[b] = (unsigned char)((d[b] & ~m) | (pat & m));
            }
        }
    }
}

void uk_rect(int x, int y, int w, int h, int c)
{
    uk_fill(x, y, w, 1, c);
    uk_fill(x, y + h - 1, w, 1, c);
    uk_fill(x, y, 1, h, c);
    uk_fill(x + w - 1, y, 1, h, c);
}

/* As OpenXcom's Window: five rectangles, darker outside, lighter inside. */
void uk_window(int x, int y, int w, int h)
{
    static const unsigned char order[5] = { 0, 1, 2, 1, 0 };
    int i;

    for (i = 0; i < 5; i++)
        uk_rect(x + i, y + i, w - 2 * i, h - 2 * i, g_uk->frame[order[i]]);
}

/* As OpenXcom's TextButton: light top-left, dark bottom-right, filled. */
void uk_button(int x, int y, int w, int h, const char *label, int pressed)
{
    const unsigned char *b = g_uk->btn;
    int fill = pressed ? b[0] : b[2], tc = pressed ? b[2] : g_uk->btext;

    uk_fill(x, y, w, h, pressed ? b[4] : b[0]);
    uk_fill(x + 1, y + 1, w - 1, h - 1, pressed ? b[0] : b[4]);
    uk_fill(x + 1, y + 1, w - 2, h - 2, pressed ? b[3] : b[1]);
    uk_fill(x + 2, y + 2, w - 3, h - 3, pressed ? b[1] : b[3]);
    uk_fill(x + 2, y + 2, w - 4, h - 4, fill);
    if (label)
        uk_center(x, w, y + (h - 8) / 2, UK_SMALL, tc | (pressed ? UK_FLAT : 0), label);
}

/* ------------------------------------------------------------------ */
/* Text */

static void glyph_ega(int x, int y, const unsigned char far *g, int h, int bpr, int c)
{
    volatile unsigned char far *v = VGA + y * 40 + (x >> 3);
    int sh = x & 7, r;

    outpw(0x3CE, (c << 8) | 0);
    for (r = 0; r < h; r++, v += 40, g += bpr) {
        unsigned bits = bpr == 2 ? (g[0] << 8) | g[1] : g[0] << 8;
        unsigned char b, latch;
        if (!bits)
            continue;
        b = (unsigned char)(bits >> 8 >> sh);
        if (b) {
            outpw(0x3CE, (b << 8) | 8);
            latch = v[0];
            v[0] = latch;
        }
        b = (unsigned char)(bits >> sh);
        if (b) {
            outpw(0x3CE, (b << 8) | 8);
            latch = v[1];
            v[1] = latch;
        }
        if (sh) {
            b = (unsigned char)(bits << (8 - sh));
            if (b) {
                outpw(0x3CE, (b << 8) | 8);
                latch = v[2];
                v[2] = latch;
            }
        }
    }
}

/* 4 pixels of a nibble as a 2-bit CGA mask. */
static const unsigned char exp4[16] = {
    0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
    0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
};

void __near uk_glyph_cga(int x, int y, const unsigned char far *g, int h, int bpr, int c)     /* (VGA) */
{
    unsigned char pat = (unsigned char)(c * 0x55);
    int s = (x & 3) * 2, r, k;

    for (r = 0; r < h; r++, g += bpr) {
        unsigned bits = bpr == 2 ? (g[0] << 8) | g[1] : g[0] << 8;
        unsigned char mb[5];
        unsigned char far *d;
        if (!bits)
            continue;
        memset(mb, 0, sizeof mb);
        for (k = 0; k < 4; k++) {
            unsigned char e = exp4[(bits >> (12 - 4 * k)) & 15];
            mb[k] |= (unsigned char)(e >> s);
            if (s)
                mb[k + 1] |= (unsigned char)(e << (8 - s));
        }
        d = cga_addr(x, y + r);
        for (k = 0; k < 5; k++)
            if (mb[k] && (x >> 2) + k < 80)
                d[k] = (unsigned char)((d[k] & ~mb[k]) | (pat & mb[k]));
    }
}

int uk_width(int f, const char *s)
{
    const unsigned char far *fp = font_ptr(f);
    int w = 0;

    if (!fp)
        return 8 * strlen(s);
    if (g_video == VIDEO_EGA)
        outpw(0x3CE, 0x0004);           /* read plane 0 */
    for (; *s; s++)
        if ((unsigned char)*s >= 32 && (unsigned char)*s < 128)
            w += fp[2 + *s - 32] + (f == UK_BIG ? 1 : 0);
    return w;
}

int uk_text(int x, int y, int f, int c, const char *s)
{
    const unsigned char far *fp = font_ptr(f);
    int h, bpr, body = c & 15, shadow;

    if (!fp) {
        hud_text(x >> 3, y >> 3, body, s);
        return x + 8 * strlen(s);
    }
    shadow = c & UK_FLAT ? -1 : body >= 8 ? body - 8 : 0;
    if (g_video == VIDEO_CGA) {
        body = uk_cga_c(body);
        if (shadow >= 0)
            shadow = uk_cga_c(shadow) == body ? 0 : uk_cga_c(shadow);
        /* Hercules: text in half grey is a broken pattern; white, its
           shadow black (not a pressed button's: on a white fill). */
        if (g_herc && !(c & UK_FLAT) && (body == 1 || body == 2))
            body = 3, shadow = 0;
    } else {
        if (g_uk_cmap) {
            body = g_uk_cmap[body];
            if (shadow >= 0)
                shadow = g_uk_cmap[shadow];
        }
        ega_reset();
    }
    h = fp[0];
    bpr = fp[1];
    mouse_hide_rect(x, y, 320, y + h);
    if (g_video == VIDEO_EGA)
        outpw(0x3CE, 0x0F01);
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        const unsigned char far *g;
        int w;
        if (ch < 32 || ch >= 128)
            continue;
        w = fp[2 + ch - 32];
        if (x + w > 320)
            break;
        g = fp + 2 + 96 + (ch - 32) * 2 * h * bpr;
        if (ch != ' ') {
            if (g_video == VIDEO_EGA) {
                if (shadow >= 0)
                    glyph_ega(x, y, g + h * bpr, h, bpr, shadow);
                glyph_ega(x, y, g, h, bpr, body);
            } else {
                if (shadow >= 0)
                    uk_glyph_cga(x, y, g + h * bpr, h, bpr, shadow);
                uk_glyph_cga(x, y, g, h, bpr, body);
            }
        }
        x += w + (f == UK_BIG ? 1 : 0);
    }
    if (g_video == VIDEO_EGA) {
        outpw(0x3CE, 0xFF08);
        outpw(0x3CE, 0x0001);
        outpw(0x3CE, 0x0000);
    }
    return x;
}

void uk_center(int x, int w, int y, int f, int c, const char *s)
{
    uk_text(x + (w - uk_width(f, s)) / 2, y, f, c, s);
}

const char *g_uk_ext;           /* see uik.h */

void uk_line(int x, int y, int w, int c, const char *s, const unsigned char *tabs)
{
    char part[41];
    const char *t;
    int col = 0, n;

    for (;;) {
        t = s;
        if (s[0] == 1 && g_uk_ext) {    /* "" + '0'+k: the k-th text of g_uk_ext */
            t = g_uk_ext;
            for (n = s[1] - '0'; n > 0; n--)
                t += strlen(t) + 1;
        }
        for (n = 0; t[n] && t[n] != '	' && n < 40; n++)
            part[n] = t[n];
        part[n] = 0;
        /* Cut to the room left in this column. */
        {
            int room = (tabs && tabs[col] ? tabs[col] : w) - (col ? tabs[col - 1] : 0);
            while (n > 0 && uk_width(UK_SMALL, part) > room)
                part[--n] = 0;
        }
        uk_text(x + (col ? tabs[col - 1] : 0), y, UK_SMALL, c, part);
        while (*s && *s != '\t')
            s++;
        if (!*s || !tabs || !tabs[col])
            break;
        s++;
        col++;
    }
}

/* ------------------------------------------------------------------ */
/* The clean screen */

static void latch_copy(unsigned dst, unsigned src, unsigned n, unsigned rows)
{
    volatile unsigned char far *v = VGA;
    unsigned r, i;

    ega_reset();
    outpw(0x3CE, 0x0105);               /* write mode 1 */
    for (r = 0; r < rows; r++, dst += 40, src += 40)
        for (i = 0; i < n; i++) {
            unsigned char latch = v[src + i];
            v[dst + i] = latch;
        }
    outpw(0x3CE, 0x0005);
}

void uk_copy(int sx, int sy, int dx, int dy, int w, int h)     /* (VGA) */
{
    int r;

    mouse_hide_rect(dx, dy, dx + w, dy + h);
    if (g_video == VIDEO_EGA) {
        latch_copy(dy * 40 + (dx >> 3), sy * 40 + (sx >> 3), w >> 3, h);
        return;
    }
    for (r = 0; r < h; r++)
        _fmemcpy(cga_addr(dx, dy + r), cga_addr(sx, sy + r), w >> 2);
}

void uk_save(int y0, int y1)    /* (VGA) */
{
    int y;

    mouse_hide();
    save_y0 = y0;
    save_y1 = y1;
    if (g_video == VIDEO_EGA) {
        latch_copy(SAVE_OFF + y0 * 40, y0 * 40, 40 * (y1 - y0), 1);
        return;
    }
    free(csave);
    csave = malloc((y1 - y0) * 80);
    if (csave)
        for (y = y0; y < y1; y++)
            _fmemcpy(csave + (y - y0) * 80, cga_addr(0, y), 80);
}

void uk_restore(int x, int y, int w, int h)    /* (VGA) */
{
    int r;

    if (y < save_y0) {
        h -= save_y0 - y;
        y = save_y0;
    }
    if (y + h > save_y1)
        h = save_y1 - y;
    if (w <= 0 || h <= 0)
        return;
    mouse_hide_rect(x, y, x + w, y + h);
    if (g_video == VIDEO_EGA) {
        int b0 = x >> 3, b1 = (x + w + 7) >> 3;
        latch_copy(y * 40 + b0, SAVE_OFF + y * 40 + b0, b1 - b0, h);
        return;
    }
    if (!csave)
        return;
    {
        int b0 = x >> 2, b1 = (x + w + 3) >> 2;
        for (r = y; r < y + h; r++)
            _fmemcpy(cga_addr(b0 * 4, r), csave + (r - save_y0) * 80 + b0, b1 - b0);
    }
}

void uk_screen(unsigned bg, int theme)
{
    mouse_hide();
    uk_theme(theme);
    if (bg == UK_NOBG || !uk_pic(bg, 0, 0))
        hud_clear_rows(0, 25);
    uk_theme(theme);
}

void uk_page(unsigned bg, int theme, const char *title)
{
    uk_open();
    uk_invalidate();
    uk_screen(bg, theme);
    uk_window(0, 0, 320, 200);
    uk_center(0, 320, 8, UK_BIG, g_uk->title, title);
}

unsigned uk_page_wait(void)
{
    unsigned key;

    uk_buttons("Enter=OK", 112, 96, 178);
    key = input_key();
    uk_close();
    return key;
}

/* ------------------------------------------------------------------ */
/* Message boxes */

#define BOX_X   32
#define BOX_W   256

/* Splits s into lines of at most w pixels at spaces; returns the count and
   fills starts / lens. */
static int wrap(const char *s, int w, int *starts, int *lens, int max)
{
    int n = 0, i = 0;

    while (s[i] && n < max) {
        int last = -1, j = i;
        char tmp[41];
        while (s[j]) {
            int k = j - i;
            if (k >= 40)
                break;
            memcpy(tmp, s + i, k + 1);
            tmp[k + 1] = 0;
            if (uk_width(UK_SMALL, tmp) > w)
                break;
            if (s[j] == ' ')
                last = j;
            j++;
        }
        if (s[j] && last > i)
            j = last;
        starts[n] = i;
        lens[n++] = j - i;
        i = j;
        while (s[i] == ' ')
            i++;
    }
    return n;
}

/* Box with the text and, if spec, buttons; returns the y of the buttons. */
static int box(const char *s, const char *spec, int *bx, int *bw, int *top, int *height)
{
    int starts[4], lens[4], n = wrap(s, BOX_W - 24, starts, lens, 4), i;
    int h = 20 + n * 10 + (spec ? 20 : 12), y = (200 - h) / 2;

    uk_fill(BOX_X, y, BOX_W, h, 0);
    uk_window(BOX_X, y, BOX_W, h);
    for (i = 0; i < n; i++) {
        char line[41];
        memcpy(line, s + starts[i], lens[i]);
        line[lens[i]] = 0;
        uk_center(BOX_X, BOX_W, y + 10 + i * 10, UK_SMALL, g_uk->title, line);
    }
    *bx = BOX_X + 16;
    *bw = BOX_W - 32;
    *top = y;
    *height = h;
    if (spec)
        uk_buttons(spec, *bx, *bw, y + h - 22);
    return y + h - 22;
}

void uk_notice(const char *s)
{
    int bx, bw, top, h;

    uk_open();
    box(s, "Enter=OK", &bx, &bw, &top, &h);
    input_key();
    uk_restore(BOX_X, top, BOX_W, h);
    uk_close();
}

int uk_confirm(const char *s)
{
    int bx, bw, top, h, by, yes = 0;

    uk_open();
    by = box(s, "Y=Yes|N=No", &bx, &bw, &top, &h);
    for (;;) {
        unsigned key = input_key();
        if (key == KEY_LCLICK) {
            key = uk_buttons_key("Y=Yes|N=No", bx, bw, by);
            if (!key)
                continue;
        }
        yes = ((key & 0xFF) | 0x20) == 'y';
        break;
    }
    uk_restore(BOX_X, top, BOX_W, h);
    uk_close();
    return yes;
}

int uk_edit(const char *prompt, char *name, int max)
{
    int bx, bw, top, h, len = strlen(name), ok;

    uk_open();
    box(prompt, NULL, &bx, &bw, &top, &h);
    for (;;) {
        unsigned key;
        int ch;
        uk_fill(BOX_X + 8, top + h - 14, BOX_W - 16, 9, 0);
        uk_text(uk_text(BOX_X + 16, top + h - 14, UK_SMALL, g_uk->hilite, name), top + h - 14,
                UK_SMALL, g_uk->text, "_");
        key = input_key();
        ch = key & 0xFF;
        if (key == KEY_RCLICK || ch == 27) {
            ok = 0;
            break;
        }
        if (ch == 13) {
            ok = len > 0;
            break;
        }
        if (ch == 8 && len > 0)
            name[--len] = 0;
        else if (ch >= ' ' && ch < 127 && len < max) {
            name[len++] = (char)ch;
            name[len] = 0;
        }
    }
    uk_close();
    return ok;
}

/* ------------------------------------------------------------------ */
/* Buttons along the bottom and menus */

unsigned uk_key_named(const char *k, int len)
{
    static const struct { char name[6]; unsigned key; } names[] = {
        { "Esc", 0x011B }, { "Enter", 0x1C0D }, { "Tab", 0x0F09 }, { "Space", 0x3920 },
        { "Up", 0x4800 }, { "Down", 0x5000 }, { "Left", 0x4B00 }, { "Right", 0x4D00 },
        { "+", 0x0D2B }, { "-", 0x0C2D }
    };
    int i;

    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if ((int)strlen(names[i].name) == len && !memcmp(names[i].name, k, len))
            return names[i].key;
    if (len == 1)
        return (unsigned char)(k[0] | 0x20);
    return 0;
}

/* Calls fn for each "Key=Label" of spec with its index, count and parts. */
static int spec_count(const char *spec)
{
    int n = 1;
    for (; *spec; spec++)
        n += *spec == '|';
    return n;
}

static const char *spec_item(const char *spec, int i, int *klen, const char **label, int *llen)
{
    const char *p = spec, *e;

    while (i-- > 0)
        p = strchr(p, '|') + 1;
    e = strchr(p, '=');
    *klen = (int)(e - p);
    *label = e + 1;
    for (*llen = 0; e[1 + *llen] && e[1 + *llen] != '|'; (*llen)++)
        ;
    return p;
}

void uk_buttons(const char *spec, int x, int w, int y)
{
    int n = spec_count(spec), i, bw = (w - (n - 1) * 8) / n;

    for (i = 0; i < n; i++) {
        int klen, llen;
        const char *label;
        char t[32];
        spec_item(spec, i, &klen, &label, &llen);
        if (llen > 31)
            llen = 31;
        memcpy(t, label, llen);
        t[llen] = 0;
        uk_button(x + i * (bw + 8), y, bw, g_uk_bh, t, 0);
    }
}

unsigned uk_buttons_key(const char *spec, int x, int w, int y)
{
    int n, i, bw;

    if (!spec || g_mouse_y < y || g_mouse_y >= y + g_uk_bh)
        return 0;
    n = spec_count(spec);
    bw = (w - (n - 1) * 8) / n;
    for (i = 0; i < n; i++)
        if (g_mouse_x >= x + i * (bw + 8) && g_mouse_x < x + i * (bw + 8) + bw) {
            int klen, llen;
            const char *label;
            const char *k = spec_item(spec, i, &klen, &label, &llen);
            return uk_key_named(k, klen);
        }
    return 0;
}

static const UkMenu *shown;
static char shown_info[48];

void uk_invalidate(void)
{
    shown = NULL;
}

static int win_x(const UkMenu *m)
{
    return m->ww ? m->wx : 0;
}

static int win_w(const UkMenu *m)
{
    return m->ww ? m->ww : 320;
}

static void draw_row(const UkMenu *m, char lines[][41], int n, int top, int i, int sel)
{
    int y = m->ly + (i - top) * m->rowh;

    if (m->style & UKS_BUTTONS) {
        if (i < n)
            uk_button(m->lx, y, m->lw, m->rowh > 21 ? 20 : m->rowh - 1, lines[i], i == sel);
        return;
    }
    uk_restore(m->lx, y, m->lw, m->rowh);
    if (i >= n)
        return;
    if (i == sel)
        uk_fill(m->lx, y, m->lw, m->rowh, g_uk->sel);
    uk_line(m->lx + 2, y, m->lw - 4, i == sel ? g_uk->hilite : g_uk->list, lines[i], m->tabs);
}

/* Type-ahead for letters the list's own keys do not use: they jump to the
   next line whose text (after "* " marks) starts with what was typed;
   more letters, spaces or '-' extend the prefix, the same letter again
   moves on to the next match. Lets players and test scripts pick a
   research topic, facility or base menu line by name ('U 'F 'O SPACE 'C). */
#define TYPED_MAX 12

static int line_starts(const char *s, const char *p, int len)
{
    int i;

    while (*s == ' ' || *s == '*')
        s++;
    for (i = 0; i < len; i++) {
        int c = s[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (!c || c != p[i])
            return 0;
    }
    return 1;
}

static int type_ahead(char lines[][41], int n, int sel, char *typed, int *len, int ch)
{
    int i, k;

    if (*len < TYPED_MAX)
        typed[(*len)++] = (char)ch;
    for (i = 0; i < n; i++) {
        k = (sel + i) % n;
        if (line_starts(lines[k], typed, *len))
            return k;
    }
    *len = 0;
    if (ch < 'a' || ch > 'z')
        return sel;
    typed[(*len)++] = (char)ch;
    for (i = 1; i <= n; i++) {
        k = (sel + i) % n;
        if (line_starts(lines[k], typed, 1))
            return k;
    }
    *len = 0;
    return sel;
}

static int rep_sel = -1;        /* UKS_REPEAT: the row held, the first repeat */
static unsigned char rep_first;

int uk_menu(const UkMenu *m, const char *info, char lines[][41], int n, int sel,
            UkKey on_key, const char *hotkeys)
{
    char typed[TYPED_MAX];
    int ntyped = 0;
    int top = 0, i, shown_top = -1, shown_sel = -1, wx = win_x(m), ww = win_w(m);

    uk_open();
    if (sel >= n)
        sel = n ? n - 1 : 0;
    if (shown != m) {
        uk_theme(m->theme);
        if (m->style & UKS_CLEAR) {
            mouse_hide();
            hud_clear_rows(0, 25);
        }
        if (m->style & (UKS_CLEAR | UKS_POPUP)) {
            /* The picture only inside the window. */
            uk_save(0, 200);
            if (m->bg != UK_NOBG && pic(m->bg, 0, 0, 1))
                uk_restore(m->wx, m->wy, m->ww, m->wh);
            else
                uk_fill(m->wx, m->wy, m->ww, m->wh, 0);
        } else
            uk_screen(m->bg, m->theme);
        if (m->ww)
            uk_window(m->wx, m->wy, m->ww, m->wh);
        if (m->title)
            uk_center(wx, ww, m->ty, UK_BIG, g_uk->title, m->title);
        if (m->header)
            uk_line(m->lx + 2, m->ly - 10, m->lw - 4, g_uk->text, m->header, m->tabs);
        if (m->buttons)
            uk_buttons(m->buttons, wx + 8, ww - 16, m->by);
        if (m->extra)
            m->extra();
        if (g_video == VIDEO_CGA && !(m->style & UKS_BUTTONS)) {
            /* Four colours: the picture under a list at half strength, or
               the text would not stand out. */
            int y;
            for (y = m->ly; y < m->ly + m->rows * m->rowh; y++) {
                unsigned char far *d = cga_addr(m->lx, y), k = (unsigned char)(y & 1 ? 0x33 : 0xCC);
                int b;
                for (b = 0; b < m->lw / 4; b++)
                    d[b] &= k;
            }
        }
        uk_save(0, 200);
        shown = m;
        shown_info[0] = 0;
    } else
        uk_theme(m->theme);
    for (;;) {
        unsigned key;
        int ch, scan, act = 0;

        if (info && strcmp(info, shown_info)) {
            uk_restore(wx + 6, m->ty + 18, ww - 12, 9);
            uk_center(wx, ww, m->ty + 18, UK_SMALL, g_uk->text, info);
            strncpy(shown_info, info, sizeof shown_info - 1);
        }

        if (sel < top)
            top = sel;
        if (sel >= top + m->rows)
            top = sel - m->rows + 1;
        if (top != shown_top) {
            for (i = top; i < top + m->rows; i++)
                draw_row(m, lines, n, top, i, sel);
            if (!n && !(m->style & UKS_BUTTONS))
                uk_text(m->lx + 2, m->ly, UK_SMALL, g_uk->dim, "(nothing)");
            shown_top = top;
        } else if (sel != shown_sel) {
            draw_row(m, lines, n, top, shown_sel, sel);
            draw_row(m, lines, n, top, sel, sel);
        }
        shown_sel = sel;
        if (rep_sel >= 0) {
            /* The row clicked last time is still held: Enter again. */
            if (rep_sel == sel && (m->style & UKS_REPEAT) && input_held(rep_first ? 250 : 50)) {
                rep_first = 0;
                break;
            }
            rep_sel = -1;
        }
        key = input_key();
        if (key == KEY_RCLICK)
            key = 0x011B;
        else if (key == KEY_LCLICK) {
            int r = (g_mouse_y - m->ly) / m->rowh;
            /* A text row is hit across the whole window, not only the
               text or the highlight bar; button columns only on the buttons. */
            int hx0 = m->lx, hx1 = m->lx + m->lw;
            if (m->ww && !(m->style & UKS_BUTTONS)) {     /* (every list lies inside its window) */
                hx0 = wx + 3;
                hx1 = wx + ww - 3;
            }
            g_uk_row = -1;
            if (g_mouse_y >= m->ly && r < m->rows && g_mouse_x >= hx0
                && g_mouse_x < hx1 && top + r < n) {
                g_uk_row = top + r;
                if (on_key == NULL || top + r == sel || (m->style & UKS_CLICK))
                    act = 1;
                sel = top + r;
                if (!act)
                    continue;
                key = 0x1C0D;
                rep_sel = sel;
                rep_first = 1;
            } else if (m->buttons && (key = uk_buttons_key(m->buttons, wx + 8, ww - 16, m->by)) != 0)
                ;
            else
                continue;
        }
        ch = key & 0xFF;
        scan = key >> 8;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch == 27) {
            sel = -1;
            break;
        }
        if (n && ((ch >= 'a' && ch <= 'z') || (ntyped && (ch == ' ' || ch == '-')))
            && !(hotkeys && ch > ' ' && strchr(hotkeys, ch))) {
            /* The screen's own keys first, else type-ahead. */
            int r = on_key ? on_key(sel, ch, scan) : 0;
            if (r == 2)
                break;
            if (r == 1) {
                shown_top = -1;
                ntyped = 0;
            } else
                sel = type_ahead(lines, n, sel, typed, &ntyped, ch);
            continue;
        }
        ntyped = 0;
        if (scan == 0x48 && sel > 0)
            sel--;
        else if (scan == 0x50 && sel < n - 1)
            sel++;
        else if (scan == 0x49)
            sel = sel > m->rows ? sel - m->rows : 0;
        else if (scan == 0x51 && n)
            sel = sel + m->rows < n ? sel + m->rows : n - 1;
        else if (ch == 13 && n)
            break;
        else if (hotkeys && ch > ' ' && strchr(hotkeys, ch) && (int)(strchr(hotkeys, ch) - hotkeys) < n) {
            sel = (int)(strchr(hotkeys, ch) - hotkeys);
            break;
        } else if (on_key && (n || (m->style & UKS_KEYS0))) {
            int r = on_key(sel, ch, scan);
            if (r == 2)
                break;
            if (r == 1)
                shown_top = -1;
        }
    }
    uk_close();
    return sel;
}
