/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Geoscape: world map, time, UFOs, radar, Skyranger, score and funding.
   The aliens' missions, sites and the countries live in alien.c. */

#include <bios.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "geo.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "map.h"
#include "sound.h"
#include "timer.h"
#include "unit.h"
#include "base.h"
#include "battle.h"
#include "craft.h"
#include "cut.h"
#include "facil.h"
#include "hwp.h"
#include "menu.h"
#include "pedia.h"
#include "psi.h"
#include "uik.h"
#include "vgageo.h"
#include "alien.h"
#include "inv.h"
#include "brief.h"
#include "geowin.h"
#include "gpop.h"
#include "bsave.h"

#define WMAP_W       320
#define WMAP_H       160
#define VIEW_W       256        /* map view; the button panel takes the rest */
#define GRID_W      80
#define GRID_H      40
#define FP          16          /* positions in 1/16 pixel */
#define RADAR       40          /* base radar range, pixels */
#define CRAFT_RADAR 10          /* craft radar, 672 nautical miles */
#define CRAFT_SIGHT 25          /* craft sight for alien bases, 1696 nautical miles */
#define UFO_SIGHT   4           /* UFO sight for the X-COM base, 268 nautical miles */
#define MAX_UFOS    12

#define COL_TEXT    15
#define COL_INFO    7
#define COL_ALERT   12
#define COL_GOOD    10

enum { UFO_NONE, UFO_FLYING, UFO_LANDED, UFO_CRASHED, UFO_LEAVING, UFO_TERROR };

typedef struct {
    int state, type;            /* UFO_TERROR: a terror site, type 0, city in tx */
    int x, y, tx, ty;           /* position and destination, 1/16 pixel */
    int speed;                  /* 1/16 pixel per hour */
    int timer;                  /* minutes left landed / crashed */
    int detected, id;           /* detected: 1 radar, 2 hyper-wave decoder */
    int damage;                 /* from air combat */
    int mission, wave, wp;      /* alien mission (-1 none), its wave, trajectory point */
    int race;
    int alt;                    /* 0 ground, 1 very low .. 4 very high */
    int escape;                 /* air combat break-off countdown, 0 = not set */
    int site;                   /* Landing Site-n / Crash Site-n, 0 = none yet */
} Ufo;

static const char *const ufo_names[N_UFO_TYPES] = {
    "", "Small Scout", "Medium Scout", "Large Scout", "Harvester", "Abducter",
    "Terror ship", "Battleship", "Supply ship"
};
#define UFO_BATTLESHIP 7        /* OpenXcom's order; only attacks the base so far */
static const char *const mission_names[N_MISSION_TYPES] = {
    "Alien Research", "Alien Harvest", "Alien Abduction", "Alien Infiltration", "Alien Base",
    "Alien Terror", "Alien Retaliation", "Alien Supply"
};
static const char *const terrain_codes[7] = { "", "FA", "FO", "JU", "MO", "DE", "PO" };
static const char *const months[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const int month_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static unsigned char *bitmap;   /* device format, see convert.py build_world */
static unsigned char (*grid)[GRID_W];  /* after the bitmap in its block (load_world) */
static unsigned bitmap_bytes;

static Ufo ufos[MAX_UFOS];
static int next_id = 1, selected = -1;
static int next_site[2];        /* the last landing site's and crash site's number */
static int sel_site = -1;       /* selected terror site / alien base */
static int half_hour;           /* minutes towards the next half-hour tick */
int year = 1999, month, day = 1, hour = 12, minute;    /* (geo.h) */

/* Bases (base.h): position in map pixels; the troop transports' flights. */
#define BASE_X(b)   g_bhdr[b].x
#define BASE_Y(b)   g_bhdr[b].y

static int score, month_score, bad_months;
int g_months_played;                /* unit.h: alien item levels */
static int speed = 2, paused;   /* index into speed_secs */

/* GeoscapeState::timerReset: the clock at 5 seconds, running. */
static void timer_reset(void)
{
    speed = 0;
    paused = 0;
}
static int second;              /* clock seconds (not saved) */
static int view_x;              /* world x at the left edge of the view, multiple of 8 */
static int view_y;              /* world y at the top (0 unless zoomed in) */
static int zoom;                /* 0 1:1, 1 2x, 2 4x (the DOS zoom buttons) */
#define ZOOM_MAX    2
static int cyd_due = -1;        /* the craft sent to Cydonia from Select Destination */

/* The DOS time buttons, in seconds per step (about 8 steps a second). */
static const long speed_secs[6] = { 5, 60, 300, 1800, 3600, 86400L };
static int game_over;
int g_debug;
static int geo_leave;           /* options: abandon or quit ends geo_run */
int g_last_race = -1;
int g_next_race = -1;
int g_geo_loaded, g_geo_abandon;

static int attacker = -1;       /* UFO on its way to attack a base */
/* Pop-ups raised during a time step (geo_popup), shown when it ends. */
#define MAX_POPS 8
static struct { unsigned char kind, idx; int id; } pops[MAX_POPS];
static int n_pops;
static int attacker_base;       /* the base it attacks */

static void retaliation(void);
static int attack_base(int i, BattleFn battle);

/* Markers drawn with XOR, so drawing them again erases them. */
#define MAX_MARKS 48
static struct { int x, y; unsigned char size, colour; } marks[MAX_MARKS];
static int n_marks;

/* ------------------------------------------------------------------ */
/* World data and drawing */

/* Run-length coded data (convert.py run_length). 0 = ok. */
static int unpack(FILE *f, unsigned char *dst, unsigned size)
{
    unsigned n = 0;

    while (n < size) {
        int c = getc(f), k;
        if (c == EOF)
            return 1;
        if (c < 128) {
            if (n + c + 1 > size || fread(dst + n, 1, c + 1, f) != (unsigned)c + 1)
                return 1;
            n += c + 1;
        } else {
            k = getc(f);
            c = 257 - c;
            if (k == EOF || n + c > size)
                return 1;
            memset(dst + n, k, c);
            n += c;
        }
    }
    return 0;
}

static int load_world(void)
{
    char name[13];
    FILE *f;
    unsigned char h[16];

    sprintf(name, "WORLD%c.PAK", g_pakv);
    f = fopen(name, "rb");
    if (!f)
        return 1;
    bitmap_bytes = g_vga ? WMAP_W * WMAP_H : g_video == VIDEO_EGA ? WMAP_W / 8 * WMAP_H * 4 : WMAP_W / 4 * WMAP_H;
    if (!bitmap) {
        /* From DOS, not the C heap: freed during battles, the space can
           then hold sprite banks (pak.c allocates those from DOS too). The
           terrain grid and the zoom table (draw_world) share the block. */
        unsigned seg;
        if (!_dos_allocmem((bitmap_bytes + GRID_H * GRID_W + 16 + 15) >> 4, &seg)) {
            bitmap = MK_FP(seg, 0);
            grid = (unsigned char (*)[GRID_W])(bitmap + bitmap_bytes);
        }
    }
    if (!bitmap || fread(h, 1, 16, f) != 16 || memcmp(h, "XWD2", 4)
        || unpack(f, bitmap, bitmap_bytes) || unpack(f, &grid[0][0], GRID_H * GRID_W)
        || alien_load_world(f)) {
        fclose(f);
        return 1;
    }
    fclose(f);
    hgc_white2(bitmap, bitmap_bytes);   /* Hercules: the poles white */
    return base_unpark();       /* the bases come back after a battle */
}

/* Before a battle: the world map, the alien tables and the other bases'
   data leave memory. */
static void free_world(void)
{
    if (bitmap)
        _dos_freemem(FP_SEG(bitmap));
    bitmap = NULL;
    grid = NULL;
    alien_free_world();
    craft_troop_name(0);        /* kept for the briefing: the craft are parked */
    base_park();
}

/* Zoomed source bytes: n bytes at src (its segment also holds the 16-byte
   table at offset tab) become 2 (z 1: a nibble each) or 4 (z 2: a bit
   pair each) bytes at dst through the table (XLAT). */
static void zoom_bytes(unsigned char far *dst, const unsigned char far *src, unsigned n,
                       unsigned tab, unsigned z);
#pragma aux zoom_bytes = \
    "push ds" \
    "mov ds, dx" \
    "cld" \
    "cmp al, 1" \
    "jne z4" \
"z2: lodsb" \
    "mov ah, al" \
    "shr al, 1" "shr al, 1" "shr al, 1" "shr al, 1" \
    "xlat" "stosb" \
    "mov al, ah" "and al, 15" "xlat" "stosb" \
    "loop z2" \
    "jmp zx" \
"z4: lodsb" \
    "mov ah, al" \
    "rol al, 1" "rol al, 1" "and al, 3" "xlat" "stosb" \
    "mov al, ah" "shr al, 1" "shr al, 1" "shr al, 1" "shr al, 1" "and al, 3" "xlat" "stosb" \
    "mov al, ah" "shr al, 1" "shr al, 1" "and al, 3" "xlat" "stosb" \
    "mov al, ah" "and al, 3" "xlat" "stosb" \
    "loop z4" \
"zx: pop ds" \
    parm [es di] [dx si] [cx] [bx] [ax] \
    modify [ax cx si di];

static void draw_detail(void);
static void centre_on(int x, int y);

/* Copies the visible part of the world to the screen: the VIEW_W x WMAP_H
   view from world (view_x, view_y), wrapping round at the date line.
   Zoomed in, each world pixel is 2 x 2 or 4 x 4 screen pixels: a source
   byte's pixels are doubled through a table (a nibble or a pixel pair to
   a byte) into one screen row, which is copied to the rows it covers.
   8088 + EGA: 140 ms at 1:1, about 160 ms zoomed in. */
static void draw_world(void)
{
    unsigned char row[64];
    unsigned char *tab;
    int ega = g_video == VIDEO_EGA;
    unsigned p, y, i, k, s, v, bpr = ega ? WMAP_W / 8 : WMAP_W / 4;
    unsigned vb = ega ? VIEW_W / 8 : VIEW_W / 4, skip = ega ? view_x / 8 : view_x / 4;
    unsigned first = bpr - skip < vb ? bpr - skip : vb, w = ega ? 1 : 2, bits = 8 >> zoom;

    tab = bitmap + bitmap_bytes + GRID_H * GRID_W;
    for (i = 0; i < 16; i++) {
        for (v = 0, k = bits; k; k -= w)
            for (s = 1 << zoom; s; s--)
                v = v << w | (i >> (k - w) & (w * 2 - 1));
        tab[i] = (unsigned char)v;
    }
    mouse_hide_rect(0, 0, VIEW_W, WMAP_H);
    if (g_vga)                  /* a byte a pixel (vgageo.h) */
        vgx_world(bitmap, view_x, view_y, zoom);
    for (p = 0; p < (ega ? 4u : !g_vga); p++) {
        if (ega) {
            outp(0x3C4, 2);
            outp(0x3C5, 1 << p);
        }
        for (y = 0; y < WMAP_H; y++) {
            const unsigned char *src = bitmap + p * (WMAP_W / 8 * WMAP_H)
                                       + (view_y + (y >> zoom)) * bpr;
            unsigned char far *d = (unsigned char far *)(ega ? MK_FP(0xA000, y * 40)
                : MK_FP(0xB800, (y & 1) * 0x2000 + (y >> 1) * 80));
            if (!zoom) {
                _fmemcpy(d, src + skip, first);
                if (first < vb)
                    _fmemcpy(d + first, src, vb - first);
                continue;
            }
            if (!(y & ((1 << zoom) - 1))) {
                /* vb >> zoom source bytes from skip, across the date line */
                k = bpr - skip < vb >> zoom ? bpr - skip : vb >> zoom;
                zoom_bytes(row, src + skip, k, FP_OFF(tab), zoom);
                if (k < vb >> zoom)
                    zoom_bytes(row + (k << zoom), src, (vb >> zoom) - k, FP_OFF(tab), zoom);
            }
            _fmemcpy(d, row, vb);
        }
    }
    if (ega) {
        outp(0x3C4, 2);
        outp(0x3C5, 0x0F);
    }
    if (zoom)
        draw_detail();
}

static int to_screen(int x, int y, int *sx, int *sy);

/* A name centred over screen point (sx, sy), only if all of it fits in
   the view (Globe::drawDetail's labels). */
static void label(int sx, int sy, int colour, const char *s)
{
    int w = uk_width(UK_SMALL, s);

    sx -= w / 2;
    if (sx >= 0 && sx + w <= VIEW_W && sy >= 0 && sy + 9 <= WMAP_H)
        uk_text(sx, sy, UK_SMALL, colour, s);
}

#define BORDER_COL  5           /* magenta (CGA: red): no land or sea colour */

/* A clipped run of border pixels. */
static void border_run(int x, int y, int w, int h)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > VIEW_W)
        w = VIEW_W - x;
    if (y + h > WMAP_H)
        h = WMAP_H - y;
    uk_fill(x, y, w, h, BORDER_COL);
}

/* One line segment in screen pixels (Bresenham), as runs along its
   longer axis: a few fills a segment, not one a pixel. */
static void border_seg(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0, sx = 1, sy = 1, err, i, r;

    if (dx < 0) {
        dx = -dx;
        sx = -1;
    }
    if (dy < 0) {
        dy = -dy;
        sy = -1;
    }
    if (dx >= dy) {
        err = dx >> 1;
        r = x0;
        for (i = 0; i <= dx; i++, x0 += sx) {
            err -= dy;
            if (err < 0 || i == dx) {
                border_run(sx > 0 ? r : x0, y0, (x0 - r) * sx + 1, 1);
                y0 += sy;
                err += dx;
                r = x0 + sx;
            }
        }
    } else {
        err = dy >> 1;
        r = y0;
        for (i = 0; i <= dy; i++, y0 += sy) {
            err -= dx;
            if (err < 0 || i == dy) {
                border_run(x0, sy > 0 ? r : y0, 1, (y0 - r) * sy + 1);
                x0 += sx;
                err += dy;
                r = y0 + sy;
            }
        }
    }
}

/* The country borders (globe.rul's polylines, Globe::drawDetail): points
   in 1/4 map pixels, drawn with the view's offset, a line near the date
   line also a map width to either side. */
static void draw_borders(void)
{
    const int *p = alien_borders();
    int vx = view_x * 4, vy = view_y * 4, k, n, px, py, sx, sy, qx, qy, off;

    if (!p)
        return;
    for (; (n = *p++) > 0; p += n * 2) {
        for (off = -WMAP_W * 4; off <= WMAP_W * 4; off += WMAP_W * 4) {
            px = py = 0;
            for (k = 0; k < n; k++) {
                qx = (((p[k * 2] + off - vx) << zoom) >> 2);
                qy = (((p[k * 2 + 1] - vy) << zoom) >> 2);
                if (k && !((qx < 0 && px < 0) || (qx >= VIEW_W && px >= VIEW_W)
                           || (qy < 0 && py < 0) || (qy >= WMAP_H && py >= WMAP_H))) {
                    sx = px;
                    sy = py;
                    border_seg(sx, sy, qx, qy);
                }
                px = qx;
                py = qy;
            }
        }
    }
}

/* Zoomed in, the details DOS shows at its higher zoom levels
   (Globe::drawDetail): the country borders and names at 2x, the cities
   (a dot and the name) and the bases' names too at 4x. */
static void draw_detail(void)
{
    const char *s;
    int i, x, y, sx, sy;

    draw_borders();
    for (i = 0; (s = alien_country_label(i, &x, &y)) != NULL; i++)
        if (to_screen(x, y, &sx, &sy))
            label(sx, sy, 15, s);
    if (zoom < 2)
        return;
    for (i = 0; (s = alien_place(i, &x, &y)) != NULL; i++)
        if (*s && to_screen(x, y, &sx, &sy)) {
            if (sx > 0 && sy > 0 && sx < VIEW_W - 1 && sy < WMAP_H - 1)
                uk_fill(sx - 1, sy - 1, 3, 3, 14);
            label(sx, sy + 2, 14, s);
        }
    for (i = 0; i < g_nbases; i++)
        if (to_screen(BASE_X(i), BASE_Y(i), &sx, &sy))
            label(sx, sy + 2, 10, g_bhdr[i].name);
}

static void draw_panel(void);
static void show_status(void);

/* The map, the panel and the status lines (the clock too: windows over
   the map are drawn after this). */
static int fight_icons(int draw, int want);

static void draw_map(void)
{
    draw_world();
    uk_fill(0, WMAP_H, VIEW_W, 200 - WMAP_H, 0);
    draw_panel();
    n_marks = 0;
    show_status();
    fight_icons(1, -1);
}

/* Screen position of world pixel (x, y) (the middle of its square when
   zoomed in; x < 0 just left of the view, across the date line); 1 if it
   is inside the view. */
static int to_screen(int x, int y, int *sx, int *sy)
{
    x -= view_x;
    if (x < 0)
        x += WMAP_W;
    if (x >= WMAP_W - 8)
        x -= WMAP_W;
    *sx = (x << zoom) + (1 << zoom >> 1);
    *sy = ((y - view_y) << zoom) + (1 << zoom >> 1);
    return *sx >= 0 && *sx < VIEW_W && *sy >= 0 && *sy < WMAP_H;
}

/* XORs a size x size box round world pixel (x, y), the part of it inside
   the view, so that drawing it again erases it. Colour bits 4-7: the DOS
   marker (MK), drawn instead in VGA (vgxasm.asm vx_xor, 3 x 3). */
static void xor_box(int x, int y, int size, int colour)
{
    int x0, y0, x1, y1, i, j, p;

    to_screen(x, y, &x0, &y0);
    x0 -= size / 2;
    y0 -= size / 2;
    x1 = x0 + size;
    y1 = y0 + size;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > VIEW_W)
        x1 = VIEW_W;
    if (y1 > WMAP_H)
        y1 = WMAP_H;
    if (x0 >= x1 || y0 >= y1)
        return;
    mouse_hide_rect(x0, y0, x1, y1);
    if (g_vga) {
        vgx_xor(x0, y0, x1, y1, colour);
        return;
    }
    if (g_video == VIDEO_CGA) {
        colour &= 15;
        p = colour == 12 ? 2 : (colour == 14 || colour == 15 ? 3 : 1);
        for (j = y0; j < y1; j++)
            for (i = x0; i < x1; i++)
                *(unsigned char far *)MK_FP(0xB800, (j & 1) * 0x2000 + (j >> 1) * 80 + (i >> 2))
                    ^= (unsigned char)(p << (6 - 2 * (i & 3)));
        return;
    }
    /* EGA: plane by plane, read through the read map, written through the
       map mask. */
    for (p = 0; p < 4; p++) {
        if (!(colour & (1 << p)))
            continue;
        outp(0x3CE, 4);
        outp(0x3CF, p);
        outp(0x3C4, 2);
        outp(0x3C5, 1 << p);
        for (j = y0; j < y1; j++)
            for (i = x0; i < x1; i++)
                *(unsigned char far *)MK_FP(0xA000, j * 40 + (i >> 3)) ^= (unsigned char)(0x80 >> (i & 7));
    }
    outp(0x3C4, 2);
    outp(0x3C5, 0x0F);
    outp(0x3CE, 4);
    outp(0x3CF, 0);
}

static void erase_marks(void)
{
    while (n_marks > 0) {
        n_marks--;
        xor_box(marks[n_marks].x, marks[n_marks].y, marks[n_marks].size, marks[n_marks].colour);
    }
}

static void mark(int x, int y, int size, int colour)
{
    if (n_marks >= MAX_MARKS)
        return;
    marks[n_marks].x = x;
    marks[n_marks].y = y;
    marks[n_marks].size = (unsigned char)size;
    marks[n_marks].colour = (unsigned char)colour;
    n_marks++;
    xor_box(x, y, size, colour);
}

/* A DOS marker (OpenXcom's GlobeMarkers frame f) in a mark's colour. */
#define MK(f)   (((f) + 1) << 4)

static void draw_marks(void)
{
    int i;

    erase_marks();
    for (i = 0; i < g_nbases; i++)
        mark(BASE_X(i), BASE_Y(i), i == g_bcur ? 4 : 3, 15 | MK(0));
    for (i = 0; i < MAX_UFOS; i++) {
        const Ufo *u = &ufos[i];
        if (u->state == UFO_NONE || !u->detected)
            continue;
        mark(u->x / FP, u->y / FP, i == selected ? 4 : 2, u->state == UFO_FLYING ? 12 | MK(2)
             : 13 | MK(3));
    }
    for (i = 0; i < MAX_SITES; i++)
        if (g_sites[i].kind == SITE_TERROR || (g_sites[i].kind == SITE_BASE && g_sites[i].known))
            mark(g_sites[i].x, g_sites[i].y, i == sel_site ? 5 : 3,
                 g_sites[i].kind == SITE_TERROR ? 14 | MK(5) : 11 | MK(7));
    for (i = 0; i < MAX_WAYPOINTS; i++)
        if (g_wp[i].id)
            mark(g_wp[i].x, g_wp[i].y, 3, 7 | MK(6));
    /* Transports yellow, interceptors green. */
    for (i = 0; i < MAX_CRAFT; i++)
        if (g_craft[i].state == CS_OUT || g_craft[i].state == CS_RETURN)
            mark(g_craft[i].x / FP, g_craft[i].y / FP, 2,
                 g_craft_types[g_craft[i].type].soldiers ? 14 | MK(1) : 10 | MK(1));
}

/* ------------------------------------------------------------------ */
/* HUD */

/* The DOS geoscape buttons down the right side (GEOGRAPH/GEOBORD.SCR):
   functions, the clock box (funds, time, date), the time speeds and the
   map rotation (all of GEOBORD.SCR, as DOS); under the flat map one line
   of text (the last message) and no buttons: the keys Tab, A, Enter, P, R
   and S have none, as in DOS. Tab and A open the selection's window. */
#define PANEL_X     256
#define BTN_H       12          /* function buttons, from the top */
#define CLOCK_Y     72
#define SPEED_Y     113         /* time buttons: 2 x 3, 14 pixels apart */
#define STATUS_Y    161         /* the last message */

static const struct { char label[10]; unsigned char key; } panel_buttons[6] = {
    { "INTERCEPT", 0 }, { "BASES", 'b' }, { "GRAPHS", 'g' },
    { "UFOPAEDIA", 'u' }, { "OPTIONS", 27 }, { "FUNDING", 'f' }
};
static const char *const speed_names[6] = {
    "5Sec", "1Min", "5Min", "30Min", "1Hr", "1Day"
};

static char clock_was[64];
/* EGA: the panel is GEOBORD.SCR as DOS shows it, its buttons' labels its
   own (drawn once); only the pressed time button is drawn over it. CGA and
   Hercules write the labels again (the picture's are lost in 4 colours). */
static unsigned char bord_pic;

/* Funds as DOS prints them: $4 112 000 (k in $K, d10 in $10). */
void geo_money(char *buf, long k, int d10)
{
    long v = k * 1000L + d10 * 10L;

    if (v >= 1000000L)
        sprintf(buf, "$%ld %03ld %03ld", v / 1000000L, v / 1000 % 1000, v % 1000);
    else if (v >= 1000)
        sprintf(buf, "$%ld %03ld", v / 1000, v % 1000);
    else
        sprintf(buf, "$%ld", v);
}

static void panel_label(int x, int y, int w, int h, const char *s, int on)
{
    uk_fill(x + 2, y + 2, w - 4, h - 4, on ? 1 : 9);
    /* CGA has one colour for light and dark blue: black text there. */
    uk_center(x - 1, w + 2, y + (h - 8) / 2, UK_SMALL,
              on ? 15 : (g_video == VIDEO_CGA ? 0 : 1) | UK_FLAT, s);
}

static void draw_speeds(void)
{
    int i, x, y;
    for (i = 0; i < 6; i++) {
        x = PANEL_X + 1 + (i & 1) * 32;
        y = SPEED_Y + (i >> 1) * 14;
        if (!bord_pic)
            panel_label(x, y, 31, 13, speed_names[i], i == speed && !paused);
        else if (i == speed && !paused) {
            /* pressed (TextButton: inverted, light grey with dark text;
               the short label: GEOBORD's own do not fit the small font) */
            uk_fill(x + 2, y + 2, 27, 9, 7);
            uk_center(x - 1, 33, y + 3, UK_SMALL, 8 | UK_FLAT, speed_names[i]);
        }
    }
}

static void draw_panel(void)
{
    int i;

    uk_theme(T_BLUE);
    i = uk_pic(UI_GEOBORD, -1, 0);
    bord_pic = i && g_video == VIDEO_EGA;
    if (!i)
        uk_fill(PANEL_X, 0, 64, 200, 1);
    if (!bord_pic) {
        for (i = 0; i < 6; i++)
            panel_label(PANEL_X + 1, i * BTN_H, 63, BTN_H, panel_buttons[i].label, 0);
    }
    uk_fill(PANEL_X + 3, CLOCK_Y + 2, 59, 36, 1);
    clock_was[0] = 0;
    draw_speeds();
}

/* The clock box as GeoscapeState draws it (Text objects at the panel's
   x 3: funds, hh:mm big with :ss small, weekday, day, month, year). */
static void show_clock(void)
{
    static const char *const wdays[7] = {
        "SATURDAY", "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY"
    };
    char now[64], money[16];
    int yday = day - 1, m;

    for (m = 0; m < month; m++)
        yday += month_days[m];
    geo_money(money, g_base.funds, g_base.funds10);
    /* 1 Jan 1999 was a Friday. */
    sprintf(now, "%s|%02d:%02d|%c%02d|%s|%d%s|%s|%d", money, hour, minute, paused ? 'P' : ':', second,
            wdays[(yday + (year - 1999) * 365 + 6) % 7], day,
            day % 10 == 1 && day != 11 ? "st" : day % 10 == 2 && day != 12 ? "nd"
            : day % 10 == 3 && day != 13 ? "rd" : "th", months[month], year);
    if (!strcmp(clock_was, now))
        return;
    strcpy(clock_was, now);
    /* The box in one go (the funds line and the big time overlap). */
    uk_fill(PANEL_X + 3, CLOCK_Y + 2, 59, 36, 1);
    uk_center(PANEL_X + 3, 59, CLOCK_Y + 1, UK_SMALL, 15 | UK_FLAT, money);
    sprintf(now, "%02d:%02d:%02d", hour, minute, second);
    uk_center(PANEL_X + 3, 59, CLOCK_Y + 8, UK_SMALL, (paused ? 12 : 15) | UK_FLAT, now);   /* red: paused */
    uk_center(PANEL_X + 3, 59, CLOCK_Y + 15, UK_SMALL, 15 | UK_FLAT, wdays[(yday + (year - 1999) * 365 + 6) % 7]);
    sprintf(now, "%d%s", day, day % 10 == 1 && day != 11 ? "st" : day % 10 == 2 && day != 12 ? "nd"
            : day % 10 == 3 && day != 13 ? "rd" : "th");
    uk_center(PANEL_X + 3, 29, CLOCK_Y + 22, UK_SMALL, 15 | UK_FLAT, now);
    uk_center(PANEL_X + 32, 29, CLOCK_Y + 22, UK_SMALL, 15 | UK_FLAT, months[month]);
    sprintf(now, "%d", year);
    uk_center(PANEL_X + 3, 59, CLOCK_Y + 29, UK_SMALL, 15 | UK_FLAT, now);
}

static void show_status(void)
{
    static int shown_speed = -1, shown_paused;

    if (speed != shown_speed || paused != shown_paused) {
        if (bord_pic)
            draw_panel();       /* the picture again under the button let go */
        else
            draw_speeds();
        shown_speed = speed;
        shown_paused = paused;
    }
    show_clock();
}

static void message(int colour, const char *s)
{
    uk_fill(0, STATUS_Y, VIEW_W, 9, 0);
    uk_text(1, STATUS_Y, UK_SMALL, colour, s);
    if (*s)
        log_printf("GEO %d %s: %s", day, months[month], s);
}

/* ------------------------------------------------------------------ */
/* Simulation */

static int dist2(int x0, int y0, int x1, int y1)
{
    long dx = x1 - x0, dy = y1 - y0, d = dx * dx + dy * dy;
    return d > 30000L ? 30000 : (int)d;
}

static int land_at(int px, int py)
{
    if (px < 0 || px >= WMAP_W || py < 0 || py >= WMAP_H || !grid)
        return 0;
    return grid[py / 4][px / 4];
}

int geo_land(int x, int y)
{
    return land_at(x, y) != 0;
}

/* Integer square root (bit by bit). */
static long isqrt(unsigned long n)
{
    unsigned long r = 0, b = 1UL << 30;

    while (b > n)
        b >>= 2;
    while (b) {
        if (n >= r + b) {
            n -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return (long)r;
}

int geo_distance(int x0, int y0, int x1, int y1)
{
    long dx = x1 - x0, dy = y1 - y0;
    return (int)isqrt(dx * dx + dy * dy);
}

static void ufo_next_leg(int i);

/* A UFO of an alien mission enters at the first point of its trajectory. */
int geo_spawn_ufo(int type, int mission, int wave, int race)
{
    int i, x, y, speed, alt;
    Ufo *u = NULL;

    for (i = 0; i < MAX_UFOS; i++)
        if (ufos[i].state == UFO_NONE) {
            u = &ufos[i];
            break;
        }
    if (!u || !alien_waypoint(mission, wave, 0, type, &x, &y, &speed, &alt))
        return -1;
    memset(u, 0, sizeof *u);
    u->type = type;
    u->mission = mission;
    u->wave = wave;
    u->race = race;
    u->x = x * FP;
    u->y = y * FP;
    u->id = next_id++;
    log_printf("GEO UFO-%d %s mission %d wave %d at %d,%d", u->id, ufo_names[type], mission,
               wave, x, y);
    ufo_next_leg(i);
    return u->state == UFO_NONE ? -1 : i;
}

/* A retaliation battleship flies straight at an X-COM base, very high
   and at top speed (the assault run trajectory). */
int geo_spawn_assault(int mission, int race, int x, int y, int b)
{
    int i;

    if (attacker >= 0)
        return -1;
    for (i = 0; i < MAX_UFOS && ufos[i].state != UFO_NONE; i++)
        ;
    if (i == MAX_UFOS)
        return -1;
    memset(&ufos[i], 0, sizeof ufos[i]);
    ufos[i].type = UFO_BATTLESHIP;
    ufos[i].mission = mission;
    ufos[i].race = race;
    ufos[i].x = x * FP;
    ufos[i].y = y * FP;
    ufos[i].tx = BASE_X(b) * FP;
    ufos[i].ty = BASE_Y(b) * FP;
    ufos[i].speed = g_ufo_combat[UFO_BATTLESHIP].speed * FP;
    ufos[i].alt = 4;
    ufos[i].state = UFO_FLYING;
    ufos[i].id = next_id++;
    attacker = i;
    attacker_base = b;
    log_printf("GEO battleship UFO-%d sent against %s", ufos[i].id, g_bhdr[b].name);
    return i;
}

int geo_mission_ufos(int mission)
{
    int i, n = 0;
    for (i = 0; i < MAX_UFOS; i++)
        if (ufos[i].state != UFO_NONE && ufos[i].mission == mission)
            n++;
    return n;
}

static int hour_alert;

void geo_alert(int colour, const char *s)
{
    message(colour, s);
    hour_alert = 1;
}

void geo_popup(int kind, int idx, int id)
{
    if (n_pops < MAX_POPS && idx >= 0) {
        pops[n_pops].kind = (unsigned char)kind;
        pops[n_pops].idx = (unsigned char)idx;
        pops[n_pops++].id = id;
    }
    hour_alert = 1;
}

void geo_score(int x, int y, int pts)
{
    score += pts;
    month_score += pts;
    alien_credit(x, y, pts, 0);
}

void geo_alien(int x, int y, int pts)
{
    score -= pts;
    month_score -= pts;
    alien_credit(x, y, pts, 1);
}

void geo_research(int pts)
{
    score += pts;
    month_score += pts;
    alien_research_points(pts);
}

static void remove_ufo(int i)
{
    ufos[i].state = UFO_NONE;
    if (selected == i)
        selected = -1;
    if (attacker == i)
        attacker = -1;
}

void geo_drop_mission(int mission)
{
    int i;
    for (i = 0; i < MAX_UFOS; i++)
        if (ufos[i].state != UFO_NONE && ufos[i].mission == mission)
            remove_ufo(i);
}

/* Heads for the next point of the trajectory, or leaves Earth. */
static void ufo_next_leg(int i)
{
    Ufo *u = &ufos[i];
    int x, y, speed, alt, k;

    u->wp++;
    k = alien_waypoint(u->mission, u->wave, u->wp, u->type, &x, &y, &speed, &alt);
    if (!k) {
        remove_ufo(i);
        return;
    }
    u->tx = x * FP;
    u->ty = y * FP;
    u->speed = speed;
    u->alt = alt;
    u->state = k == 2 ? UFO_LEAVING : UFO_FLYING;
}

/* The UFO reached its waypoint: fly on, land, or become a site. */
static void ufo_arrive(int i)
{
    Ufo *u = &ufos[i];
    char line[48];
    int minutes = 0, x = u->x / FP, y = u->y / FP;

    switch (alien_reached(u->mission, u->wave, u->wp, x, y, land_at(x, y) != 0, &minutes)) {
    case REACH_GONE:
        remove_ufo(i);
        break;
    case REACH_LAND:
        u->state = UFO_LANDED;
        u->timer = minutes;
        u->alt = 0;
        if (u->detected && minutes) {
            sprintf(line, "UFO-%d has landed", u->id);
            geo_alert(COL_ALERT, line);
        }
        break;
    default:
        ufo_next_leg(i);
    }
}

/* Moves a point towards a target; returns 1 on arrival. */
static int move_towards(int *x, int *y, int tx, int ty, long step)
{
    long dx = tx - *x, dy = ty - *y;
    long d2 = dx * dx + dy * dy, d;

    if (d2 <= step * step) {
        *x = tx;
        *y = ty;
        return 1;
    }
    d = isqrt(d2);
    *x += (int)(dx * step / (d + 1));
    *y += (int)(dy * step / (d + 1));
    return 0;
}

/* UFO visibility to radar (Ufo::getVisibility): size (very small -30 ..
   very large +30) by altitude; on the ground -30. */
static int visibility(const Ufo *u)
{
    int size = 15 * g_ufo_combat[u->type].size - 30;

    switch (u->alt) {
    case 0: return -30;
    case 1: return size - 20;
    case 2: return size - 10;
    case 3: return size;
    }
    return size - 10;
}

static int craft_radar(const Ufo *u)
{
    int i;
    for (i = 0; i < MAX_CRAFT; i++)
        if ((g_craft[i].state == CS_OUT || g_craft[i].state == CS_RETURN)
            && dist2(u->x / FP, u->y / FP, g_craft[i].x / FP, g_craft[i].y / FP)
               <= CRAFT_RADAR * CRAFT_RADAR)
            return 1;
    return 0;
}

static void hour_tick(void)
{
    const char *cm = craft_hour();

    if (cm)
        message(COL_INFO, cm);
    base_hour_all();            /* its messages stop the clock (geo_alert) */
    retaliation();
    if (g_terror_now) {
        g_terror_now = 0;
        alien_terror_now();
    }
}

/* Midnight: construction, research and recovery at every base (base_day),
   alien bases. */
static void day_tick(void)
{
    base_day_all();
    if (g_base.cydonia == 1) {
        geo_alert(COL_ALERT, "Cydonia located! Press C to launch the final assault.");
        g_base.cydonia = 2;
    }
    alien_day();
}

static void launch_attack(int nearby);

/* Debug X: a transport of the current base is at the selected UFO (the
   ready one with the most seats, else any in the hangar or in flight). */
static void debug_troops(void)
{
    Craft *c;
    int i = craft_best_transport();

    for (c = g_craft; i < 0 && c < g_craft + MAX_CRAFT; c++)
        if (CRAFT_HERE(c) && (c->state == CS_HANGAR || c->state == CS_OUT)
            && g_craft_types[c->type].soldiers)
            i = (int)(c - g_craft);
    for (c = g_craft; i < 0 && c < g_craft + MAX_CRAFT; c++)
        if (CRAFT_HERE(c) && c->state == CS_RETURN && g_craft_types[c->type].soldiers)
            i = (int)(c - g_craft);
    if (i < 0) {
        message(COL_INFO, "No transport at the base.");
        return;
    }
    c = &g_craft[i];
    c->state = CS_OUT;
    c->target = selected;
    c->target_id = ufos[selected].id;
    c->timer = 0;
    c->x = ufos[selected].x;
    c->y = ufos[selected].y;
    paused = 0;
}

/* Every 10 minutes: fuel, low fuel, patrols finding alien bases,
   retaliation UFOs looking for the base. */
static void ten_minute_tick(void)
{
    int i, b;

    for (i = 0; i < MAX_CRAFT; i++) {
        Craft *c = &g_craft[i];
        const CraftType *t = &g_craft_types[c->type];
        if (c->state != CS_OUT && c->state != CS_RETURN)
            continue;
        c->fuel -= t->fuel_hour / 6;
        if (c->fuel < 0)
            c->fuel = 0;
        if (c->state == CS_OUT && craft_low_fuel(c)) {
            char line[48];
            sprintf(line, "%s low on fuel, returning.", craft_name(c));
            message(COL_INFO, line);
            c->state = CS_RETURN;
            geo_popup(PU_FUEL, i, 0);           /* LowFuelState */
        }
        /* Patrolling (no destination): its radar sweeps for alien bases. */
        if (c->state == CS_OUT && c->target == CT_PATROL)
            alien_scan(c->x / FP, c->y / FP, CRAFT_SIGHT);
    }
    /* Retaliation UFOs near a base may locate it (sight 268 nautical
       miles; chance 15 % + 1 % per 6 facility squares, divided by mind
       shields + 1). */
    for (i = 0; i < MAX_UFOS; i++) {
        const Ufo *u = &ufos[i];
        if (u->state == UFO_NONE || u->state == UFO_CRASHED || i == attacker
            || !alien_retal_scanner(u->mission, u->wave, u->wp))
            continue;
        for (b = 0; b < g_nbases; b++)
            if (dist2(u->x / FP, u->y / FP, BASE_X(b), BASE_Y(b)) < UFO_SIGHT * UFO_SIGHT
                && (int)rnd(100) < g_bhdr[b].detect)
                alien_base_spotted(b);
    }
}

/* Craft on their way to UFO slot i. */
static int ufo_followed(int i)
{
    int k;

    for (k = 0; k < MAX_CRAFT; k++)
        if (g_craft[k].state == CS_OUT && g_craft[k].target == i
            && g_craft[k].target_id == ufos[i].id)
            return 1;
    return 0;
}

/* Every half hour: alien missions, crash sites, refuelling, and for every
   UFO activity points (one in the air, two landed) and radar detection:
   a new contact needs a radar roll (the chances of the radars covering it
   added up, times its visibility), a tracked UFO stays tracked while some
   radar covers it. */
static void half_hour_tick(void)
{
    char line[48];
    unsigned followed = 0;
    int i, b;

    for (b = 0; b < MAX_CRAFT; b++)
        if (g_craft[b].state == CS_OUT && g_craft[b].target == CT_SITE)
            followed |= 1u << g_craft[b].pad;
    alien_half_hour(g_months_played, followed);
    for (i = 0; i < MAX_UFOS; i++) {
        Ufo *u = &ufos[i];
        int x = u->x / FP, y = u->y / FP, was = u->detected, det = 0;
        if (u->state == UFO_NONE)
            continue;
        if (u->state == UFO_CRASHED) {
            if ((u->timer -= 30) <= 0)      /* gone when the timer is out */
                remove_ufo(i);
            continue;
        }
        geo_alien(x, y, u->state == UFO_LANDED ? 2 : 1);
        for (b = 0; b < g_nbases && det < 2; b++) {
            int d2 = dist2(x, y, BASE_X(b), BASE_Y(b)), k;
            k = was ? facil_in_range(b, d2) : facil_detect(b, d2, visibility(u));
            if (k > det)
                det = k;
        }
        if (!det && craft_radar(u))
            det = 1;
        if (det == 1 && was == 2)
            det = 2;            /* stays decoded while tracked */
        u->detected = det;
        if (u->detected && !was) {
            sprintf(line, "UFO-%d detected: %s", u->id, ufo_names[u->type]);
            message(COL_ALERT, line);
            if (selected < 0)
                selected = i;
            snd_hit();
            geo_popup(PU_DETECTED, i, u->id);   /* UfoDetectedState */
        } else if (!u->detected && was) {
            sprintf(line, "UFO-%d: contact lost", u->id);
            message(COL_INFO, line);
            if (selected == i)
                selected = -1;
            if (ufo_followed(i))
                geo_popup(PU_LOST, i, u->id);   /* UfoLostState */
        }
    }
}

/* The month's missions, psi training, funding less the monthly costs, the
   report (rating, the countries' decisions, game over), agents finding an
   alien base. */
/* The Graphs' income and expenditure (SavedGame::setFunds): the funds
   gone up since the last count are income, gone down spending. */
static void track_funds(void)
{
    Graphs *g = &g_bb->gr;
    long d = g_base.funds - g->funds;

    if (d > 0)
        g->income[0] += d;
    else
        g->expend[0] -= d;
    g->funds = g_base.funds;
}

static void month_end(void)
{
    char line[48];
    long costs, funding;
    int last = (month + 11) % 12, lose;

    g_months_played++;
    alien_month_start(g_months_played);
    costs = base_month_all();   /* and psi training, logged; the month line shows over it */
    funding = alien_funding();
    track_funds();
    graphs_month(costs, funding, month_score);
    g_base.funds += funding - costs;
    erase_marks();
    lose = alien_month_report(g_months_played, g_base.funds, costs, last,
                              last == 11 ? year - 1 : year);
    draw_map();
    alien_month_discover();
    bad_months = lose;
    sprintf(line, "Month: score %d, costs $%ldK", month_score, costs);
    message(month_score >= 0 ? COL_GOOD : COL_ALERT, line);
    timer_reset();                          /* time1Month: timerReset before the report */
    month_score = 0;
    if (lose) {
        message(COL_ALERT, "The Council has disbanded X-COM.");
        game_over = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Interceptors */

/* The end of craft c's fight with the UFO in slot (result res). */
static int troops_aboard(int k);

static void fight_over(Craft *c, int slot, int res)
{
    Ufo *u = &ufos[slot];
    char line[48];
    int gain, k;

    c->timer = 0;
    switch (res) {
    case DF_UFO_DOWN:
    case DF_UFO_DESTROYED:
        /* The UFO's score, twice for one destroyed in the air. */
        gain = g_ufo_combat[u->type].score * (res == DF_UFO_DESTROYED ? 2 : 1);
        geo_score(u->x / FP, u->y / FP, gain);
        alien_shot_down(u->mission, u->race);
        if (res == DF_UFO_DOWN && land_at(u->x / FP, u->y / FP)) {
            u->state = UFO_CRASHED;
            u->site = ++next_site[1];           /* Crash Site-n (DOS: its own count) */
            u->timer = (24 + rnd(73)) * 60;     /* 24-96 hours */
            u->alt = 0;
            u->detected = 1;
            if (attacker == slot)
                attacker = -1;
            sprintf(line, "UFO-%d shot down! Crash site marked.", u->id);
            /* DogfightState: the craft after it with nobody to land return
               (the shooter too); one with soldiers or HWPs aboard keeps the
               crash site as its target. */
            for (k = 0; k < MAX_CRAFT; k++)
                if (g_craft[k].state == CS_OUT && g_craft[k].target == slot
                    && !troops_aboard(k))
                    g_craft[k].state = CS_RETURN;
        } else {
            remove_ufo(slot);
            sprintf(line, res == DF_UFO_DOWN ? "UFO-%d shot down over the sea."
                                             : "UFO-%d destroyed.", u->id);
            c->state = CS_RETURN;
        }
        message(COL_GOOD, line);
        break;
    case DF_UFO_ESCAPED:
        /* It runs at top speed; the craft follows while it has contact. */
        u->speed = g_ufo_combat[u->type].speed * FP;
        sprintf(line, "UFO-%d is outrunning %s.", u->id, craft_name(c));
        message(COL_ALERT, line);
        break;
    case DF_DISENGAGED:
        sprintf(line, "%s disengaged.", craft_name(c));
        message(COL_INFO, line);
        c->state = CS_RETURN;
        break;
    case DF_CRAFT_LOST:
        sprintf(line, "%s was shot down.", craft_name(c));
        message(COL_ALERT, line);
        craft_lose(c);
        break;
    case DF_MINIMISED:
        c->timer = 2;                   /* follows the UFO, its icon on the map */
        sprintf(line, "%s: dogfight minimised.", craft_name(c));
        message(COL_INFO, line);
        break;
    }
}

/* A minimised fight is on (Craft.timer 2). */
static int fights_minimised(void)
{
    int k;

    for (k = 0; k < MAX_CRAFT; k++)
        if (g_craft[k].state == CS_OUT && g_craft[k].timer == 2)
            return 1;
    return 0;
}

/* The fights of the craft waiting for one (Craft.timer 3: reached their
   UFO this step, or a minimised fight brought back), up to four at once
   (DogfightState; GeoscapeState allows four interceptions). */
static void air_combats(void)
{
    Fight f[MAX_FIGHTS];
    int k, n = 0, slot[MAX_FIGHTS], z = zoom, vx = view_x, vy = view_y;

    for (k = 0; k < MAX_CRAFT; k++) {
        Craft *c = &g_craft[k];
        Ufo *u = &ufos[c->target >= 0 ? c->target : 0];
        if (c->state != CS_OUT || c->timer < 3)
            continue;
        if (n == MAX_FIGHTS) {
            c->timer = 0;               /* the next step */
            continue;
        }
        memset(&f[n], 0, sizeof f[n]);
        f[n].c = c;
        f[n].ufo_type = u->type;
        f[n].ufo_id = u->id;
        f[n].ufo_name = ufo_names[u->type];
        f[n].ufo_damage = &u->damage;
        f[n].escape = &u->escape;
        f[n].result = -1;
        f[n].resume = c->timer == 4;
        f[n].off[0] = c->pad & 1;
        f[n].off[1] = (c->pad >> 1) & 1;
        slot[n++] = c->target;
    }
    if (!n) {
        snd_dogfight(fights_minimised());
        return;
    }
    snd_dogfight(1);
    timer_reset();                          /* a fight starts at 5 seconds (time5Seconds, startDogfight) */
    erase_marks();
    /* GeoscapeState::startDogfight: the map zooms in on the fight, and
       back out to the view before when it is over (or minimised). */
    zoom = ZOOM_MAX;
    centre_on(f[0].c->x / FP, f[0].c->y / FP);
    do
        draw_map();
    while (craft_dogfights(f, n));
    erase_marks();
    zoom = z;
    centre_on(vx + ((VIEW_W / 2) >> z), vy + ((WMAP_H / 2) >> z));
    draw_map();
    for (k = 0; k < n; k++) {
        f[k].c->pad = (unsigned char)(f[k].off[0] | f[k].off[1] << 1);
        fight_over(f[k].c, slot[k], f[k].result);
    }
    snd_dogfight(fights_minimised());
}

/* The icons of the minimised fights down the left of the map
   (DogfightState's _btnMinimizedIcon: 32 x 20 at 5, 5n + 16(n - 1)),
   numbered; *n: how many. Returns the craft of icon want (0-based), -1. */
static int fight_icons(int draw, int want)
{
    char s[4];
    int k, n = 0, y;

    for (k = 0; k < MAX_CRAFT; k++) {
        if (g_craft[k].state != CS_OUT || g_craft[k].timer != 2)
            continue;
        y = 5 + n * 21;
        if (n == want)
            return k;
        if (draw) {
            uk_fill(5, y, 32, 20, 0);
            uk_window(5, y, 32, 20);
            sprintf(s, "%d", n + 1);
            uk_text(23, y + 6, UK_SMALL, 15, s);
            uk_fill(10, y + 7, 9, 5, 10);
        }
        n++;
    }
    return -1;
}

/* Brings back the minimised fight of icon i (all of them: -1). */
static void restore_fights(int i)
{
    int k, want = i < 0 ? -1 : fight_icons(0, i);

    for (k = 0; k < MAX_CRAFT; k++)
        if (g_craft[k].state == CS_OUT && g_craft[k].timer == 2 && (i < 0 || k == want))
            g_craft[k].timer = 4;
    air_combats();
}

/* Way points: a new one at map pixel (x, y) (its slot, -1 if all are in
   use); wp_gc frees those no craft flies to any more. */
static int wp_new(int x, int y)
{
    int i;

    for (i = 0; i < MAX_WAYPOINTS; i++)
        if (!g_wp[i].id) {
            g_wp[i].x = x;
            g_wp[i].y = y;
            g_wp[i].id = ++g_bb->wp_next;
            return i;
        }
    return -1;
}

static void wp_gc(void)
{
    int i, k;

    for (i = 0; i < MAX_WAYPOINTS; i++) {
        for (k = 0; k < MAX_CRAFT; k++)
            if (g_craft[k].state == CS_OUT && g_craft[k].target == CT_WAYPOINT
                && g_craft[k].pad == i)
                break;
        if (k == MAX_CRAFT)
            g_wp[i].id = 0;
    }
}

/* Soldiers (or HWPs) aboard transport k: its fit crew (DOS and OpenXcom's
   getNumSoldiers / getNumVehicles: soldiers go only as the craft's crew,
   CraftSoldiersState; a transport with nobody aboard does not land). */
static int troops_aboard(int k)
{
    const Craft *c = &g_craft[k];
    int cur = g_bcur, n;

    if (!g_craft_types[c->type].soldiers)
        return 0;
    if (c->hwp[0])
        return 1;
    base_select(c->base);
    n = craft_crew(k);
    base_select(cur);
    return n;
}

/* Craft k at a landed or crashed UFO, a terror site or an alien base
   (GeoscapeState::time5Seconds): with soldiers aboard it asks to land
   (the pop-up, then landing), else it turns back there (DOS), except over
   a landed UFO, where it waits. */
static void at_target(int k, int landed_ufo)
{
    Craft *c = &g_craft[k];

    if (troops_aboard(k)) {
        c->timer = 1;
        hour_alert = 1;                 /* ConfirmLandingState: landings */
    } else if (!landed_ufo) {
        c->state = CS_RETURN;
        log_printf("GEO %s has nobody aboard: returning", craft_name(c));
    }
}

/* Moves the craft (GeoscapeState::time5Seconds): pursuit, air combat,
   way points, the landing at a site, return. A craft that cannot catch
   its UFO follows it. Returns 1 if something needs the player. */
static int fly_craft(int minutes)
{
    int i, alert = 0, w;
    char line[48];

    for (i = 0; i < MAX_CRAFT; i++) {
        Craft *c = &g_craft[i];
        const CraftType *t = &g_craft_types[c->type];
        long step = (long)t->speed * FP * minutes / 60;

        if (c->state == CS_RETURN) {
            if (move_towards(&c->x, &c->y, BASE_X(c->base) * FP, BASE_Y(c->base) * FP, step)) {
                c->state = CS_HANGAR;
                craft_checkup(c);
                /* The wounded leave the crew once home (DOS: at the
                   debriefing, Soldier::setWoundRecovery). */
                for (w = base_roster_n(c->base); w--; ) {
                    Soldier *s = base_soldier(c->base, w);
                    if (s->craft == i + 1 && s->recovery) {
                        s->craft = 0;
                        log_printf("CREW %s wounded, off %s", s->name, craft_name(c));
                    }
                }
                sprintf(line, "%s back at base.", craft_name(c));
                message(COL_INFO, line);
            }
            continue;
        }
        if (c->state != CS_OUT || c->target == CT_PATROL || c->timer == 1 || c->timer >= 3)
            continue;
        if (c->target == CT_WAYPOINT) {
            const WayPt *p = &g_wp[c->pad];
            if (!p->id)
                c->target = CT_PATROL;
            else if (move_towards(&c->x, &c->y, p->x * FP, p->y * FP, step)) {
                /* CraftPatrolState: patrols there (destination 0). */
                c->target = CT_PATROL;
                sprintf(line, "%s is patrolling.", craft_name(c));
                message(COL_INFO, line);
                geo_popup(PU_PATROL, i, p->id);
                wp_gc();
                alert = 1;
            }
        } else if (c->target == CT_SITE) {
            const Site *s = &g_sites[c->pad];
            if (s->kind == SITE_NONE) {
                sprintf(line, "%s: target lost, returning.", craft_name(c));
                message(COL_INFO, line);
                c->state = CS_RETURN;
            } else if (move_towards(&c->x, &c->y, s->x * FP, s->y * FP, step)) {
                at_target(i, 0);
                alert |= c->timer;
            }
        } else {
            Ufo *u = &ufos[c->target];
            if (u->id != c->target_id || u->state == UFO_NONE || u->state == UFO_TERROR) {
                sprintf(line, "%s: target lost, returning.", craft_name(c));
                c->state = CS_RETURN;
                message(COL_INFO, line);
                alert = 1;
            } else if (!u->detected) {
                /* Out of radar: a way point at the last known position
                   and the craft's window (DOS). */
                c->timer = 0;
                w = wp_new(u->x / FP, u->y / FP);
                sprintf(line, "%s: UFO-%d lost.", craft_name(c), u->id);
                message(COL_INFO, line);
                if (w < 0)
                    c->state = CS_RETURN;
                else {
                    c->target = CT_WAYPOINT;
                    c->pad = (unsigned char)w;
                    geo_popup(PU_TARGET, i, u->id);
                }
                alert = 1;
            } else if (move_towards(&c->x, &c->y, u->x, u->y, step)) {
                if (u->state == UFO_CRASHED || u->state == UFO_LANDED) {
                    c->timer = 0;
                    at_target(i, u->state == UFO_LANDED);
                    alert |= c->timer;
                } else if (!c->timer && t->weapons && u->speed <= t->speed * FP) {
                    c->timer = 3;               /* a fight (air_combats) */
                    alert = 1;
                }
                /* A minimised fight (timer 2): the craft follows. */
            }
        }
    }
    return alert;
}

/* Debriefing points: civilians saved 30, killed by aliens -30, by X-COM
   -50; the recovered components, Elerium and alloys (base_loot), the
   objective (an alien base 500); soldiers, HWPs and aliens in gpop.c
   (gpop_soldier_points, gpop_alien_points). */
static void stat(DebStats *ds, int k, int qty, int score)
{
    ds->qty[k] += qty;
    ds->score[k] += score;
}

/* Battle saves (src/bsave.h): the campaign as it is at the start of a
   battle goes to CAMPAIGN.TMP before the world leaves memory, with the
   battle's context; a battle save copies it. Without it (no disk room)
   the battle is fought all the same, without saving. */
static void snapshot(int kind)
{
    FILE *f;
    int ok;

    g_bctx.kind = (unsigned char)kind;
    busy_show();                /* the battle's setup begins (input.h) */
    f = fopen(BS_CAMPAIGN, "wb");
    ok = f && geo_save_body(f);
    if (f && fclose(f))
        ok = 0;
    if (!ok) {
        g_bctx.kind = BK_NONE;
        log_printf("GEO no saving in this battle: %s not written", BS_CAMPAIGN);
    }
}

/* The battle is over: nothing left to save. */
static void battle_done(void)
{
    g_bctx.kind = BK_NONE;
    remove(BS_CAMPAIGN);
}

#define BT_LEFT(r)  ((r) == BT_LOAD || (r) == BT_ABANDON || (r) == BT_NOROOM)
/* A quick battle's save was loaded (menu.c): its battle comes next. */
#define QUICK_RESUME    (g_bs_resume && g_bctx.kind == BK_QUICK)

/* A battle left for Load Game or Abandon Game (src/bsave.h): nothing of it
   counts, the world comes back for what follows (battle_exits). Returns
   res, or -9 if the world map cannot be reloaded. */
static int battle_left(int res)
{
    g_next_race = -1;
    g_squad_max = 8;
    g_squad_craft = 0;
    g_troop_craft = -1;
    g_hwp_n = 0;
    g_base_layout = NULL;
    battle_done();
    if (load_world()) {
        game_over = -2;
        return -9;
    }
    return res;
}

/* sin of t degrees (0-180) x 1000 (Bhaskara: within 0.002). */
static long sin1000(long t)
{
    t = t * (180 - t);
    return 4000L * t / (40500L - t);
}

/* The battle is dark at map pixel (x, y) (the map starts at 180 W, 90 S
   or N at its edges) when the sun's shade there is 10 or more: X-COM then
   sees units beyond 9 tiles only in light (TileEngine::visible). The
   shade is Globe::getPolygonTextureAndShade's without seasons: from
   -250 x sin(local solar angle) x cos(latitude), 10 or more from 6, that
   is once the sun is below the horizon by sin x cos <= -0.024 (about
   18:06 to 5:54 local at the equator). Twilight shades (1-9) only darken
   the DOS picture; they see as by day. */
static int site_dark(int x, int y)
{
    /* minutes since 18:00 local solar time */
    long m = ((long)hour * 60 + minute + ((long)x * 360 / WMAP_W - 180) * 4 + 3 * 1440 - 1080)
             % 1440;
    int lat = y * 180 / WMAP_H - 90;

    if (lat < 0)
        lat = -lat;
    /* the sun below the horizon for 12 hours: angle (m / 4) degrees */
    return m < 720 && sin1000(m / 4) * sin1000(90 - lat) >= 24000L;
}

/* The next battle lands transport k with its own craft map (pak.h CM_*,
   crafts.rul battlescapeTerrainData and deployment). */
static void craft_map(int k)
{
    int t = g_craft[k].type;

    g_pak_craft = (unsigned char)(t == CT_AVENGER ? CM_AVENGER
                                  : t == CT_LIGHTNING ? CM_LIGHTNING : CM_SKYRANGER);
}

/* A ground battle of craft k's squad at map pixel (x, y): squad,
   equipment, the battle, the debriefing and the score. night: -1 = from
   the local time. Returns the battle result, -9 if the world map cannot be
   reloaded. A loaded battle save (g_bs_resume) has its squad and
   equipment already: its campaign is the one of the battle's start. */
static int ground_battle(int k, BattleFn battle, const char *pak, int x, int y, int race,
                         int night, int win_pts, int loot_type, int crashed, int terror)
{
    char line[56];
    DebStats ds;
    int kills = 0, lost = 0, res, delta, cur = g_bcur, survivors, craft_lost = 0;

    memset(&ds, 0, sizeof ds);
    busy_show();
    g_troop_craft = k;
    if (g_bs_resume)
        cur = g_bctx.cur;
    else {
        craft_map(k);                   /* it lands with its own map */
        base_select(g_craft[k].base);
        hwp_board(k);                   /* its HWPs (src/hwpscr.c) */
        /* The transport's fit crew (the basescape's soldier list assigns
           it, as CraftSoldiersState), as many as it seats (less four an
           HWP); none with HWPs alone. */
        g_squad_max = craft_crew(k);
        g_squad_craft = k + 1;
        if (g_squad_max > craft_seats(&g_craft[k]))
            g_squad_max = craft_seats(&g_craft[k]);
        if (night < 0)
            night = site_dark(x, y);
        g_night = night;
        equip_deploy(k, night);         /* the craft's items to the squad (hwpscr.c) */
        sprintf(line, g_night ? "%s at the site. Night mission!"
                              : "%s at the site. Mission begins!", craft_troop_name(0));
        message(COL_ALERT, line);
        log_printf("GEO %s carries %d of %d soldiers", craft_troop_name(0), g_squad_max,
                   roster_ready());
        if (g_hwp_n)
            log_printf("GEO %s carries %d HWPs", craft_troop_name(0), g_hwp_n);
    }
    g_bctx.k = (signed char)k;
    g_bctx.cur = (signed char)cur;
    snapshot(g_bctx.kind);
    free_world();
    g_next_race = race;
    g_brief_crashed = crashed;          /* the briefing: crash recovery or ground assault */
    res = battle(pak, &kills, &lost);
    if (BT_LEFT(res))
        return battle_left(res);
    battle_done();
    g_next_race = -1;
    g_squad_max = 8;
    g_squad_craft = 0;
    survivors = gpop_survivors();
    delta = gpop_soldier_points(res, &ds);
    roster_after_battle();
    if (res == -2 && survivors) {
        inv_recover();                  /* what lies aboard comes home: items, bodies */
        log_printf("LOOT aboard: corpses %d live %d", g_corpses, g_captured);
    }
    if (res == 1)
        base_loot(loot_type, crashed, kills, g_last_race, &ds);
    delta += gpop_alien_points(res == 1 || (res == -2 && survivors), g_last_race, &ds)
             + (res == 1 ? g_loot_score + win_pts : 0);
    if (res == 1 && win_pts)
        stat(&ds, DS_BASE_CONTROL, 1, win_pts);
    if (terror) {
        /* Civilians: after a failure the aliens have the rest. */
        int lost_civ = res == 1 ? 0 : g_civ_total - g_civ_killed_alien - g_civ_killed_xcom;
        stat(&ds, DS_CIV_ALIENS, g_civ_killed_alien + lost_civ,
             -30 * (g_civ_killed_alien + lost_civ));
        stat(&ds, DS_CIV_XCOM, g_civ_killed_xcom, -50 * g_civ_killed_xcom);
        if (res == 1)
            stat(&ds, DS_CIV_SAVED, g_civ_saved, 30 * g_civ_saved);
        delta += (res == 1 ? g_civ_saved * 30 : -lost_civ * 30)
                 - g_civ_killed_alien * 30 - g_civ_killed_xcom * 50;
    }
    if (load_world()) {
        game_over = -2;
        return -9;
    }
    base_promote_all();
    if (!survivors && lost) {
        /* Nobody came back: the transport is lost too (STR_CRAFT_IS_LOST). */
        Craft *c = &g_craft[k];
        delta -= g_craft_types[c->type].score;
        stat(&ds, DS_CRAFT, 1, -g_craft_types[c->type].score);
        log_printf("GEO %s lost with its squad", craft_name(c));
        craft_crew_clear(c);
        c->state = CS_EMPTY;
        craft_lost = 1;
    }
    hwp_after_battle();                 /* HWPs back to the stores, re-equipped */
    if (!craft_lost)
        equip_reequip(k);               /* its equipment from the stores again */
    gpop_debrief(loot_type ? DB_UFO : terror ? DB_TERROR : win_pts ? DB_ALIEN_BASE : DB_TERROR,
                 res, craft_lost, &ds);
    hud_clear_rows(0, 25);
    hwp_reequip_report();
    contain_check();
    g_troop_craft = -1;
    geo_score(x, y, delta);
    base_select(cur);
    draw_map();
    if (res == 1 && g_captured)
        sprintf(line, "Success: %d aliens killed, %d captured, %d lost", kills, g_captured,
                lost);
    else
        sprintf(line, "%s: %d aliens killed, %d soldiers lost",
                res == 1 ? "Success" : res == -1 ? "Defeat" : "Retreat", kills, lost);
    message(res == 1 ? COL_GOOD : COL_ALERT, line);
    return res;
}

/* Craft k landed at a terror site or an alien base. */
static int site_battle(int k, BattleFn battle)
{
    int site = g_craft[k].pad;
    Site *t = &g_sites[site];
    char pak[13], v = g_pakv;
    int res;

    if (t->kind == SITE_NONE) {
        message(COL_INFO, "Target lost. Transport returning.");
        return 0;
    }
    if (t->kind == SITE_TERROR)
        sprintf(pak, "UR0%c.PAK", v);       /* the city map */
    else
        sprintf(pak, "UBASE%c.PAK", v);
    /* Alien bases are dark inside (shade 15 in the rules). */
    g_bctx.kind = BK_SITE;
    g_brief_site = site + 1;
    res = ground_battle(k, battle, pak, t->x, t->y, t->race, t->kind == SITE_BASE ? 1 : -1,
                        t->kind == SITE_BASE ? 500 : 0, 0, 0,
                        t->kind == SITE_TERROR);
    if (res == -9)
        return 1;
    if (BT_LEFT(res))
        return 0;
    if (res == 1 && t->kind == SITE_BASE) {
        message(COL_GOOD, "The alien base has been destroyed!");
        alien_base_destroyed(site);
    }
    /* A terror site ends with the battle; a base survives a failed raid. */
    if (res == 1 || t->kind == SITE_TERROR) {
        t->kind = SITE_NONE;
        if (sel_site == site)
            sel_site = -1;
    }
    return 0;
}

/* Craft k landed at its UFO (ConfirmLandingState: Yes): the battle there.
   Returns 1 if the campaign cannot go on. */
static int ufo_battle(int k, BattleFn battle)
{
    int slot = g_craft[k].target;
    Ufo *t = &ufos[slot];
    int gx = t->x / FP / 4, gy = t->y / FP / 4, res;
    char pak[13];

    sprintf(pak, "%s%d%c.PAK", terrain_codes[grid[gy][gx] ? grid[gy][gx] : 1],
            t->type, g_pakv);
    g_bctx.kind = BK_UFO;
    g_brief_site = t->site;
    res = ground_battle(k, battle, pak, t->x / FP, t->y / FP, t->race, -1, 0,
                        t->type, t->state == UFO_CRASHED, 0);
    if (res == -9)
        return 1;
    if (BT_LEFT(res))
        return 0;
    if (res == 1)
        g_pedia_ufos |= 1u << t->type;
    if (res == 1 || t->state == UFO_CRASHED)
        remove_ufo(slot);
    return 0;
}

/* The clock and everything that runs on it (GeoscapeState::timeAdvance):
   every 10 minutes, half hour, hour, day and month, in that order after
   the moves of the step. minutes never crosses a 10-minute mark. Returns
   1 if something needs the player. */
static int landing(int k, BattleFn battle);

static int attack_due = -1;     /* the attacker reached its base this step */

static int advance(int minutes, BattleFn battle)
{
    int i, alert = 0;

    (void)battle;
    for (i = 0; i < MAX_UFOS; i++) {
        Ufo *u = &ufos[i];
        long step = (long)u->speed * minutes / 60;

        if (u->state == UFO_FLYING) {
            if (move_towards(&u->x, &u->y, u->tx, u->ty, step)) {
                if (i == attacker) {
                    /* The attack follows the step (geo_loop), as the
                       landings do. */
                    attack_due = i;
                    alert = 1;
                    continue;
                }
                ufo_arrive(i);
            }
        } else if (u->state == UFO_LEAVING) {
            if (move_towards(&u->x, &u->y, u->tx, u->ty, step))
                remove_ufo(i);
        } else if (u->state == UFO_LANDED && (u->timer -= minutes) <= 0) {
            alien_lifted(u->mission, u->x / FP, u->y / FP);
            ufo_next_leg(i);
        }
    }

    if (fly_craft(minutes))
        alert = 1;
    air_combats();
    /* Transports at their target ask to land when the step is over
       (geo_loop: ConfirmLandingState comes up after the 5-second tick, as
       DOS's pop-ups do), so every battle runs from the geoscape's loop
       and a battle save can go back into it (src/bsave.h). */

    minute += minutes;
    if (minute % 10 == 0)
        ten_minute_tick();
    if (minute % 30 == 0)
        half_hour_tick();
    if (minute >= 60) {
        minute -= 60;
        if (++hour == 24) {
            hour = 0;
            if (++day > month_days[month]) {
                day = 1;
                if (++month == 12) {
                    month = 0;
                    year++;
                }
                month_end();
                alert = 1;
            }
            day_tick();
        }
        hour_tick();
    }
    if (minute % 30 == 0)
        craft_half_hour();              /* after the hour's rearming, as GeoscapeState */
    if (hour_alert) {
        hour_alert = 0;
        alert = 1;
    }
    return alert;
}

int g_terror_now;

/* ------------------------------------------------------------------ */
/* Player commands */

/* Tab: the next detected UFO, then terror sites and known alien bases. */
static void select_next(void)
{
    int i, n = MAX_UFOS + MAX_SITES;
    int cur = selected >= 0 ? selected : sel_site >= 0 ? MAX_UFOS + sel_site : n - 1;

    selected = sel_site = -1;
    for (i = 1; i <= n; i++) {
        int k = (cur + i) % n;
        if (k < MAX_UFOS) {
            if (ufos[k].state != UFO_NONE && ufos[k].detected) {
                selected = k;
                return;
            }
        } else if (g_sites[k - MAX_UFOS].kind == SITE_TERROR
                   || (g_sites[k - MAX_UFOS].kind == SITE_BASE && g_sites[k - MAX_UFOS].known)) {
            sel_site = k - MAX_UFOS;
            return;
        }
    }
}

/* A: the next terror site or known alien base. */
static void select_site(void)
{
    int i;
    for (i = 1; i <= MAX_SITES; i++) {
        int k = (sel_site + i + MAX_SITES) % MAX_SITES;
        if (g_sites[k].kind == SITE_TERROR || (g_sites[k].kind == SITE_BASE && g_sites[k].known)) {
            sel_site = k;
            selected = -1;
            return;
        }
    }
    message(COL_INFO, "No terror site or alien base known.");
}

/* Craft i (ready, or in flight) goes after the UFO in slot. */
static void intercept_with(int i, int slot)
{
    Ufo *u = &ufos[slot];
    Craft *c = &g_craft[i];
    char line[48];

    if (c->state == CS_HANGAR) {
        c->x = BASE_X(c->base) * FP;
        c->y = BASE_Y(c->base) * FP;
    }
    c->state = CS_OUT;
    c->target = slot;
    c->target_id = u->id;
    c->timer = 0;
    c->pad = 0;                         /* both weapons on */
    sprintf(line, g_craft_types[c->type].soldiers ? "%s launched." : "%s launched to UFO-%d.",
            craft_name(c), u->id);
    message(COL_TEXT, line);
}

static int intercept_ok(void)
{
    if (selected < 0 || (ufos[selected].state != UFO_FLYING
                         && ufos[selected].state != UFO_LEAVING
                         && ufos[selected].state != UFO_LANDED)) {
        message(COL_INFO, "Interceptors need a UFO.");
        return 0;
    }
    return 1;
}

/* I: the current base's first ready armed craft. */
static void intercept(void)
{
    int i;

    if (!intercept_ok())
        return;
    for (i = 0; i < MAX_CRAFT && !(g_craft[i].base == g_bcur && craft_can_launch(&g_craft[i]));
         i++)
        ;
    if (i == MAX_CRAFT) {
        message(COL_INFO, "No interceptor ready (B: base, hangar).");
        return;
    }
    intercept_with(i, selected);
}


/* ------------------------------------------------------------------ */
/* Alien retaliation: a battleship attacks the base */

/* Debug key !: a battleship 30 pixels east of the current base flies at
   it. */
static void launch_attack(int nearby)
{
    int i;

    (void)nearby;
    if (attacker >= 0)
        return;
    i = geo_spawn_assault(-1, -1, BASE_X(g_bcur) + 30, BASE_Y(g_bcur), g_bcur);
    if (i >= 0)
        ufos[i].speed = 10 * FP;
}

/* Hourly: forgets an attacker shot down or driven off. Attacks come from
   retaliation missions that found the base (alien.c spawn_next). */
static void retaliation(void)
{
    if (attacker >= 0 && ufos[attacker].state != UFO_FLYING)
        attacker = -1;
}

/* Base b is lost (BaseDestroyedState): its craft (also those in the air),
   soldiers, staff and stores; with the last base the game is lost. */
static void lose_base(int b)
{
    char line[48];

    sprintf(line, "The aliens have destroyed %.16s.", g_bhdr[b].name);
    alien_base_lost(b);
    base_remove(b);
    if (attacker >= 0 && attacker_base > b)
        attacker_base--;
    message(COL_ALERT, line);
    if (!g_nbases) {
        message(COL_ALERT, "The last X-COM base is lost.");
        game_over = -1;
    }
}

/* The attacker has reached its base: defences, then a battle in the base
   with every fit soldier there (none: the base is lost). Returns 1 if the
   campaign cannot go on (world map lost). */
static int base_defence(BattleFn battle);

static int attack_base(int i, BattleFn battle)
{
    Ufo *u = &ufos[i];
    char pak[13], line[48];
    int mi = u->mission, hp = g_ufo_combat[u->type].hp;
    int b = attacker_base, cur = g_bcur;
    FILE *f;

    remove_ufo(i);
    timer_reset();                          /* time5Seconds: timerReset before BaseDefenseState */
    alien_assault_done(mi, b, 0, 0);    /* the mission's next wave waits */
    erase_marks();
    base_select(b);
    log_printf("GEO %s attacked", g_bhdr[b].name);
    sprintf(line, "UFO-%d destroyed by the base defences.", u->id);
    if (facil_defend(ufo_names[u->type], hp - u->damage, hp)) {
        sprintf(pak, "BASE%c.PAK", g_pakv);
        f = fopen(pak, "rb");
        if (f) {
            fclose(f);
            goto defend;
        }
        strcpy(line, "No base defence pack: the aliens withdraw.");
    }
    hud_clear_rows(0, 25);
    base_select(cur);
    draw_map();
    message(COL_GOOD, line);
    return 0;
defend:
    if (!roster_ready()) {
        hud_clear_rows(0, 25);
        draw_map();
        lose_base(b);           /* "Please garrison your bases in future" */
        if (g_nbases)
            base_select(cur > b ? cur - 1 : cur == b ? 0 : cur);
        return 0;
    }
    busy_show();                /* the battle's setup begins (input.h) */
    hwp_board_base();           /* HWPs of the base's craft and stores */
    g_night = 0;
    /* Every fit soldier at the base defends it, as in DOS, with what the
       stores and the craft at home hold; here at most MAX_SQUAD, fewer next
       to many HWPs (MAX_UNITS keeps room for a battleship's 28 aliens). */
    g_squad_max = roster_ready();
    if (g_squad_max > MAX_SQUAD)
        g_squad_max = MAX_SQUAD;
    if (g_squad_max > MAX_UNITS - 28 - g_hwp_n)
        g_squad_max = MAX_UNITS - 28 - g_hwp_n;
    g_squad_craft = 0;
    equip_deploy(-1, 0);
    g_bctx.mi = mi;
    g_bctx.b = (signed char)b;
    g_bctx.cur = (signed char)cur;
    return base_defence(battle);
}

/* The battle in base g_bctx.b, also a saved one (g_bs_resume: the squad
   and the HWPs are in the save). Returns 1 if the campaign cannot go on. */
static int base_defence(BattleFn battle)
{
    unsigned char layout[BASE_W * BASE_W];
    char pak[13], line[48];
    DebStats ds;
    int kills = 0, lost = 0, res, delta, mi = g_bctx.mi, b = g_bctx.b, cur = g_bctx.cur;

    sprintf(pak, "BASE%c.PAK", g_pakv);
    facil_battle_layout(layout);
    g_base_layout = layout;
    g_bctx.k = -1;
    snapshot(BK_BASE);
    free_world();
    res = battle(pak, &kills, &lost);
    if (BT_LEFT(res))
        return battle_left(res) == -9;
    battle_done();
    g_squad_max = 8;
    g_base_layout = NULL;
    memset(&ds, 0, sizeof ds);
    delta = gpop_soldier_points(res, &ds);
    if (res == 1) {
        /* A lost base takes its soldiers along: no wounds, experience or
           promotions for them (DOS: no promotions after STR_BASE_IS_LOST). */
        roster_after_battle();
        base_loot(0, 0, kills, g_last_race, &ds);
        delta += g_loot_score;
    }
    delta += gpop_alien_points(res == 1, g_last_race, &ds);
    if (load_world()) {
        game_over = -2;
        return 1;
    }
    if (res == 1) {
        hwp_after_battle();     /* the craft are back in memory */
        equip_reequip(-1);
    }
    g_hwp_n = 0;
    if (res == 1) {
        base_promote_all();
        facil_wreck(g_xbase_had, g_xbase_now);  /* blocks without base objects left */
    }
    gpop_debrief(DB_BASE, res, 0, &ds);
    hud_clear_rows(0, 25);
    hwp_reequip_report();
    contain_check();
    alien_assault_done(mi, b, 1, res == 1);
    geo_score(BASE_X(b), BASE_Y(b), delta);
    draw_map();
    if (res == 1) {
        sprintf(line, "%.16s has been defended!", g_bhdr[b].name);
        message(COL_GOOD, line);
        base_select(cur);
    } else {
        lose_base(b);
        if (g_nbases)
            base_select(cur > b ? cur - 1 : cur == b ? 0 : cur);
    }
    return 0;
}

/* Campaign state for the save slots (menu.c writes the header). XS25:
   as XS24 with the bases block's lists at DOS's sizes: 44 craft and way
   points, every research topic and recipe a base (XS24 saves load, the
   lists widened: base_load_lists, startup.c). XS24:
   as XS23 with the Flying Suit: one more item (the stores, the craft's
   equipment) and research topic (XS23 saves do not load). XS23:
   as XS22, the soldiers of every base in one pool after the bases'
   records (BaseBlock.pool, as many as there are), none in the records
   (XS22 saves do not load). XS22:
   as XS21, Soldier.layout holds 26 entries instead of 8 (XS21 saves do not
   load). XS21:
   as XS20, the map's view and zoom and the Graphs' history at the end (XS20 saves
   do not load). XS20:
   each craft's equipment aboard (Craft.items) and the soldiers' layouts
   (Soldier.layout); the battle section keeps the inventories. XS19:
   as XS18; its battle section keeps smoke and fire as DOS (smoke 1-15,
   fire intensity, 144 smoke entries of 4 bytes), so XS18 saves do not load. XS18:
   as XS17; a battle save follows it with its battle section (src/bsave.h),
   so XS17 saves do not load. XS17:
   way points instead of the transports' flights (every craft flies its
   own mission) and XS16 together; XS16:
   each research project's own cost and progress (ResProj), none per
   topic in Base. XS15:
   HWP items in the stores, HWPs aboard the craft (Craft.hwp); XS14:
   research projects and production runs per base (BaseRec); XS12:
   several bases (base_save_body). */
static FILE *io_f;
static int io_w;

/* One field of the campaign state written or read (geo_body). */
static int io(void *p, unsigned n)
{
    return (io_w ? fwrite(p, n, 1, io_f) : fread(p, n, 1, io_f)) == 1;
}

/* The save body written (w) or read: one list for both. */
static int geo_body(FILE *f, int w)
{
    char magic[4] = "XS25";
    int ok, old;

    io_f = f;
    io_w = w;
    if (!io(magic, 4))
        return 0;
    old = !memcmp(magic, "XS24", 4);        /* read as XS25 (startup.c) */
    if (!old && memcmp(magic, "XS25", 4))
        return 0;
    ok = io(ufos, sizeof ufos);
#define IO(v) ok &= io(&(v), sizeof(v))
    IO(next_id); IO(selected); IO(next_site);
    IO(year); IO(month); IO(day); IO(hour); IO(minute);
    IO(g_base); IO(score); IO(month_score); IO(bad_months); IO(g_months_played);
    IO(attacker); IO(attacker_base); IO(sel_site); IO(half_hour);
    IO(view_x); IO(view_y); IO(zoom);
    if (!w) {
        n_pops = 0;
        attack_due = -1;
    }
    ok = ok && (w ? base_save_body(f) : base_load_body(f, old));
    ok = ok && (w ? alien_save(f) : alien_load(f));
    IO(g_bb->gr);
#undef IO
    return ok;
}

int geo_save_body(FILE *f)
{
    return geo_body(f, 1);
}

int geo_load_body(FILE *f)
{
    return geo_body(f, 0);
}


/* Resets the clock, UFOs, score and craft for a new campaign. */
static void new_campaign(void)
{
    memset(ufos, 0, sizeof ufos);
    next_id = 1;
    next_site[0] = next_site[1] = 0;
    selected = sel_site = attacker = attack_due = -1;
    half_hour = 0;
    year = 1999;
    month = 0;
    day = 1;
    hour = 12;                  /* the DOS game starts at noon */
    minute = 0;
    score = month_score = bad_months = g_months_played = 0;
    speed = 1;
    paused = 0;
    n_pops = 0;
}

/* The current base's soldiers (the DOS Soldiers list, basescr.c). */
static void show_roster(void)
{
    erase_marks();
    soldiers_screen();
    draw_map();
    message(COL_INFO, "");
}

/* ------------------------------------------------------------------ */
/* Panel screens and the mouse */

/* The view centred on world pixel (x, y) (DOS: the globe turned to the
   point), as far as the map reaches up and down. */
static void centre_on(int x, int y)
{
    int h = WMAP_H >> zoom;

    erase_marks();
    view_x = ((x - (VIEW_W >> zoom) / 2 + 2 * WMAP_W) % WMAP_W) & ~7;
    view_y = y - h / 2;
    if (view_y > WMAP_H - h)
        view_y = WMAP_H - h;
    if (view_y < 0)
        view_y = 0;
    draw_world();
    fight_icons(1, -1);
    draw_marks();
}

/* Turns the globe: the view moves by dx, dy screen pixels (dx a multiple
   of 32). */
static void scroll_map(int dx, int dy)
{
    centre_on(view_x + ((VIEW_W / 2 + dx) >> zoom), view_y + ((WMAP_H / 2 + dy) >> zoom));
}

/* Zoom level z (0 .. ZOOM_MAX) round the middle of the view. */
static void zoom_map(int z)
{
    int x = view_x + ((VIEW_W / 2) >> zoom), y = view_y + ((WMAP_H / 2) >> zoom);

    if (z < 0 || z > ZOOM_MAX || z == zoom)
        return;
    erase_marks();              /* where they are drawn at this zoom */
    zoom = z;
    centre_on(x, y);
}

/* The rotate and zoom buttons under the time buttons, x - 256, y, w, h
   (GeoscapeState / SelectDestinationState: left, right, up, down, zoom
   in, zoom out). */
static const unsigned char nav_box[6][4] = {
    { 3, 176, 12, 10 }, { 27, 176, 12, 10 }, { 15, 162, 13, 12 }, { 15, 187, 13, 12 },
    { 39, 156, 23, 23 }, { 44, 182, 13, 17 }
};

/* A click on those buttons or their keys (arrows, + and -): the map
   scrolls (while a rotate button is held) or zooms; a right click on a
   zoom button zooms all the way. 1 if key was one of them. */
static int map_nav(unsigned key)
{
    int i = 6, sc = key >> 8, ch = key & 0xFF;

    if (IS_CLICK(key)) {
        while (--i >= 0 && !(g_mouse_x >= PANEL_X + nav_box[i][0] && g_mouse_y >= nav_box[i][1]
                             && g_mouse_x < PANEL_X + nav_box[i][0] + nav_box[i][2]
                             && g_mouse_y < nav_box[i][1] + nav_box[i][3]))
            ;
    } else if (ch == '+' || ch == '=')
        i = 4;
    else if (ch == '-')
        i = 5;
    else if (!ch || ch == 0xE0)
        i = sc == 0x4B ? 0 : sc == 0x4D ? 1 : sc == 0x48 ? 2 : sc == 0x50 ? 3 : -1;
    else
        i = -1;
    if (i < 0)
        return 0;
    if (i >= 4)
        zoom_map(key == KEY_RCLICK ? (i == 4 ? ZOOM_MAX : 0) : zoom + (i == 4 ? 1 : -1));
    else if (i < 2 || zoom)
        do
            scroll_map(i < 2 ? i * 64 - 32 : 0, i < 2 ? 0 : i * 64 - 160);
        while (key == KEY_LCLICK && input_held(120));
    log_printf("MAP view %d,%d zoom %d", view_x, view_y, 1 << zoom);
    return 1;
}

/* The Graphs button (graphs.c). */
static void graphs_page(void)
{
    erase_marks();
    track_funds();
    graphs_screen(month, year, month_score);
    draw_map();
    message(COL_INFO, "");
}

/* Funding by country and this month's activity. */
static void funding_screen(void)
{
    erase_marks();
    alien_funding_screen();
    draw_map();
    message(COL_INFO, "");
}

/* ------------------------------------------------------------------ */
/* The DOS game's mouse actions on the map (GeoscapeState::globeClick):
   a click opens the window of the base, craft, UFO or site under it
   (several: a list first, MultipleTargetsState); the windows are in
   geowin.c. */

static void base_screens(void);

/* World pixel (x, y) within 5 screen pixels of (mx, my) (Globe::targetNear,
   NEAR_RADIUS 25), across the date line. */
static int near_pt(int x, int y, int mx, int my)
{
    int dx, dy;

    to_screen(x, y, &dx, &dy);
    dx -= mx;
    dy -= my;
    return abs(dx) <= 5 && abs(dy) <= 5 && dx * dx + dy * dy <= 25;
}

static void add_target(GeoTarget *t, int *n, int kind, int idx, int id, int sub)
{
    if (*n >= GW_MAX_TARGETS)
        return;
    t += (*n)++;
    t->kind = (unsigned char)kind;
    t->idx = (unsigned char)idx;
    t->sub = (unsigned char)sub;
    t->id = id;
}

/* Where craft k is (map pixels); 0 if it is not in flight. */
static int craft_at(int k, int *x, int *y)
{
    const Craft *c = &g_craft[k];

    if (c->state == CS_OUT || c->state == CS_RETURN) {
        *x = c->x / FP;
        *y = c->y / FP;
        return 1;
    }
    return 0;
}

/* UFO slot i as a target: the n-th of t. */
static void ufo_target(GeoTarget *t, int *n, int i)
{
    Ufo *u = &ufos[i];
    int sub = u->state == UFO_CRASHED ? 2 : u->state == UFO_LANDED;

    /* Landing and crash sites have numbers of their own (DOS getId). */
    if (sub == 1 && !u->site)
        u->site = ++next_site[0];
    add_target(t, n, GT_UFO, i, sub ? u->site : u->id, sub);
}

/* The targets at screen point (wx, wy) (Globe::getTargets): bases and
   their craft in flight (not when choosing a destination: dest), detected
   UFOs, terror sites, known alien bases and way points. */
static int map_targets(int wx, int wy, int dest, GeoTarget *t)
{
    int i, k, n = 0, x, y;

    for (i = 0; i < g_nbases && !dest; i++) {
        if (near_pt(BASE_X(i), BASE_Y(i), wx, wy))
            add_target(t, &n, GT_BASE, i, 0, 0);
        for (k = 0; k < MAX_CRAFT; k++)
            if (g_craft[k].base == i && craft_at(k, &x, &y) && near_pt(x, y, wx, wy))
                add_target(t, &n, GT_CRAFT, k, 0, 0);
    }
    for (i = 0; i < MAX_UFOS; i++)
        if (ufos[i].state != UFO_NONE && ufos[i].detected
            && near_pt(ufos[i].x / FP, ufos[i].y / FP, wx, wy))
            ufo_target(t, &n, i);
    for (i = 0; i < MAX_SITES; i++)
        if ((g_sites[i].kind == SITE_TERROR || (g_sites[i].kind == SITE_BASE && g_sites[i].known))
            && near_pt(g_sites[i].x, g_sites[i].y, wx, wy))
            add_target(t, &n, GT_SITE, i, 0, 0);
    for (i = 0; i < MAX_WAYPOINTS; i++)
        if (g_wp[i].id && near_pt(g_wp[i].x, g_wp[i].y, wx, wy))
            add_target(t, &n, GT_POINT, i, g_wp[i].id, 0);
    return n;
}

/* Craft k flies to target t (ConfirmDestinationState::btnOkClick): a UFO,
   a site, a way point (a new one at (px, py) when t has no id; several
   craft can share one). Every craft flies its own mission, transports
   too (DOS: several troop missions a base). */
static void set_destination(int k, const GeoTarget *t, int px, int py)
{
    Craft *c = &g_craft[k];
    char line[48];
    int w = t->idx;

    if (t->kind == GT_UFO)
        intercept_with(k, t->idx);
    else {
        if (t->kind == GT_POINT && !t->id && (w = wp_new(px, py)) < 0) {
            message(COL_INFO, "Too many way points.");
            return;
        }
        if (c->state == CS_HANGAR) {
            c->x = BASE_X(c->base) * FP;
            c->y = BASE_Y(c->base) * FP;
        }
        c->state = CS_OUT;
        c->target = t->kind == GT_SITE ? CT_SITE : CT_WAYPOINT;
        c->pad = (unsigned char)w;
        c->timer = 0;
        sprintf(line, "%s launched.", craft_name(c));
        message(COL_TEXT, line);
    }
    wp_gc();
}

/* Enter: the current base's ready transport with the most seats (else
   one of its transports out on patrol) flies its troops to the selected
   UFO or site. */
static void send_troops(void)
{
    GeoTarget t;
    int k = craft_best_transport(), n = 0;

    if (selected < 0 && sel_site < 0) {
        message(COL_INFO, "Select a UFO or site first (Tab, A).");
        return;
    }
    if (!roster_ready()) {
        message(COL_INFO, "No soldiers fit for a mission at the base.");
        return;
    }
    for (n = 0; k < 0 && n < MAX_CRAFT; n++)
        if (CRAFT_HERE(&g_craft[n]) && g_craft[n].state == CS_OUT
            && g_craft_types[g_craft[n].type].soldiers && g_craft[n].target < 0
            && g_craft[n].target != CT_SITE && !craft_low_fuel(&g_craft[n]))
            k = n;
    if (k < 0) {
        message(COL_INFO, "No transport ready (B: base, hangar).");
        return;
    }
    n = 0;
    if (selected >= 0)
        ufo_target(&t, &n, selected);
    else
        add_target(&t, &n, GT_SITE, sel_site, 0, 0);
    set_destination(k, &t, 0, 0);
}

/* SelectDestinationState: craft k's target is the next click on the map (a
   UFO or site; several: the list; none: a way point), then Confirm
   Destination. Keys: Tab selects a UFO or site, Enter takes it, Esc
   cancels. Time stands still meanwhile, as in DOS. The rotate and zoom
   buttons work as on the geoscape; the Cydonia button (or C) is there for
   the Avenger once Cydonia or Bust is researched: with soldiers or HWPs
   aboard it asks to confirm (ConfirmCydoniaState), then the geoscape's
   loop starts the mission (cyd_due). */
static void select_destination(int k)
{
    GeoTarget t[GW_MAX_TARGETS];
    int n, i = -1, wx = 0, wy = 0, cyd = g_base.cydonia && g_craft[k].type == CT_AVENGER;

    draw_map();
    gw_select_bar(cyd);
    for (;;) {
        unsigned key;
        int ch;
        draw_marks();
        key = input_key();
        ch = key & 0xFF;
        n = 0;
        if (IS_CLICK(key) && (g_mouse_x >= VIEW_W || g_mouse_y >= WMAP_H)) {
            n = key == KEY_LCLICK ? gw_select_click() : 0;
            if (n == 1)
                break;
            if (n != 2) {
                map_nav(key);
                continue;
            }
            n = 0;
            ch = 'c';
        } else if (IS_CLICK(key)) {
            wx = (view_x + (g_mouse_x >> zoom)) % WMAP_W;
            wy = view_y + (g_mouse_y >> zoom);
            if (key == KEY_RCLICK) {
                centre_on(wx, wy);
                continue;
            }
            n = map_targets(g_mouse_x, g_mouse_y, 1, t);
            if (!n) {
                t[0].kind = GT_POINT;
                t[0].idx = 0;
                t[0].id = 0;
                n = 1;
            }
        }
        if (ch == 27)
            break;
        if (ch == '\t') {
            select_next();
            continue;
        }
        if ((ch | 32) == 'c') {
            if (!cyd || !troops_aboard(k))
                continue;
            if (gw_cydonia()) {
                cyd_due = k;
                break;
            }
        } else if (ch == '\r' && selected >= 0)
            ufo_target(t, &n, selected);
        else if (ch == '\r' && sel_site >= 0)
            add_target(t, &n, GT_SITE, sel_site, 0, 0);
        else if (!n) {
            map_nav(key);
            continue;
        }
        if (n) {
            i = n > 1 ? gw_pick(t, n) : 0;
            if (i >= 0 && gw_confirm(&t[i]))
                break;
        }
        i = -1;
        draw_map();
        gw_select_bar(cyd);
    }
    draw_map();
    if (i >= 0)
        set_destination(k, &t[i], wx, wy);
}

/* InterceptState (base -1: every base's craft; target: the UFO or site
   it was opened for, else the craft's destination is chosen next). */
static void intercept_window(int base, const GeoTarget *target)
{
    int k = 0, r = gw_intercept(base, &k), x, y;

    draw_map();
    if (r == GW_GOTO_BASE) {
        base_select(base);
        base_screens();
    } else if (r == GW_CENTRE && craft_at(k, &x, &y))
        centre_on(x, y);
    else if (r == GW_OK && !target)
        select_destination(k);
    else if (r == GW_OK) {
        r = gw_confirm(target);
        draw_map();
        if (r)
            set_destination(k, target, 0, 0);
    }
}

/* GeoscapeCraftState for craft k in flight; lost: the UFO it followed
   went off radar (its way point is the last known position, kept by
   Cancel: "Go to Last Known UFO Position"). */
static void craft_window(int k, int lost)
{
    Craft *c = &g_craft[k];
    const CraftType *ct = &g_craft_types[c->type];
    GeoTarget t;
    int status = GWS_PATROL, alt = 1, knots = gw_craft_knots(c->type), x, y, r, n = 0;
    char line[48];

    craft_at(k, &x, &y);
    t.kind = GT_POINT;
    t.id = lost;
    if (lost)
        status = GWS_LOST;
    else if (c->state == CS_RETURN)
        status = craft_low_fuel(c) ? GWS_LOW_FUEL : GWS_RETURN;
    else if (c->target >= 0) {
        ufo_target(&t, &n, c->target);
        status = t.sub ? GWS_DEST : GWS_INTERCEPT;
        if (ufos[c->target].alt)
            alt = ufos[c->target].alt;
    } else if (c->target == CT_SITE) {
        add_target(&t, &n, GT_SITE, c->pad, 0, 0);
        status = GWS_DEST;
    } else if (c->target == CT_WAYPOINT) {
        add_target(&t, &n, GT_POINT, c->pad, g_wp[c->pad].id, 0);
        status = GWS_DEST;
    }
    if (status == GWS_PATROL)
        knots /= 2;                     /* Craft::setDestination(0) */
    r = gw_craft(k, status, &t, alt, knots);
    draw_map();
    if (r == GW_RETURN || r == GW_PATROL) {
        sprintf(line, r == GW_RETURN ? "%s returning to base." : "%s patrolling.",
                craft_name(c));
        message(COL_TEXT, line);
    }
    if (r == GW_TARGET)
        select_destination(k);
    else if (r == GW_RETURN)
        c->state = CS_RETURN;
    else if (r == GW_PATROL) {
        c->state = CS_OUT;
        c->target = CT_PATROL;
    }
    wp_gc();
}

/* UfoDetectedState for UFO slot i (opened by a click, or by its
   detection: detected). */
static void ufo_window(int i, int detected)
{
    Ufo *u = &ufos[i];
    GwUfo w;
    int mt = alien_mission_type(u->mission), r, n = 0;

    ufo_target(&w.t, &n, i);
    w.size = g_ufo_combat[u->type].size;
    w.alt = u->alt;
    w.dx = w.dy = 0;
    w.knots = 0;
    if (u->state == UFO_FLYING || u->state == UFO_LEAVING) {
        w.dx = u->tx - u->x;
        w.dy = u->ty - u->y;
        w.knots = (int)((long)u->speed * 135 / 32);
    }
    w.decoded = u->detected == 2;
    w.detected = detected;
    w.type = ufo_names[u->type];
    w.race = u->race >= 0 && u->race < 6 ? g_race_names[u->race] : "";
    w.mission = mt >= 0 ? mission_names[mt] : "";
    w.zone = alien_region_name(u->x / FP, u->y / FP);
    r = gw_ufo(&w);
    draw_map();
    if (r != GW_CANCEL)
        centre_on(u->x / FP, u->y / FP);
    if (r == GW_CENTRE || (r == GW_INTERCEPT && detected)) {
        timer_reset();                      /* time 5 seconds (UfoDetectedState: both buttons) */
    }
    if (r == GW_INTERCEPT)
        intercept_window(-1, &w.t);
}

/* TargetInfoState for a terror site, an alien base or a way point: the
   craft on their way there. */
static void site_window(const GeoTarget *g)
{
    int follow[4], n = 0, k, ct = g->kind == GT_SITE ? CT_SITE : CT_WAYPOINT;
    const Site *t = &g_sites[g->idx];

    for (k = 0; k < MAX_CRAFT && n < 4; k++) {
        const Craft *c = &g_craft[k];
        if (c->state == CS_OUT && c->target == ct && c->pad == g->idx)
            follow[n++] = k;
    }
    k = gw_site(g, g->kind != GT_SITE ? ""
                   : t->kind == SITE_TERROR && t->city < 255 ? alien_city_name(t->city)
                   : alien_region_name(t->x, t->y), follow, n);
    draw_map();
    if (k == GW_INTERCEPT)
        intercept_window(-1, g);
}

/* Tab and A: the selected UFO's or site's window (TargetInfoState,
   UfoDetectedState), as a click on it opens (not under the script token
   AUTO: the old scripts' Tab only selects). */
static void open_selected(void)
{
    GeoTarget g;
    int n = 0;

    if (g_popup_auto)
        return;
    if (selected >= 0)
        ufo_window(selected, 0);
    else if (sel_site >= 0) {
        add_target(&g, &n, GT_SITE, sel_site, 0, 0);
        site_window(&g);
    }
}

/* A target picked on the map (MultipleTargetsState::popupTarget). A base
   also becomes the current base (Enter, P and I act for it). */
static void open_target(const GeoTarget *t)
{
    char line[48];

    switch (t->kind) {
    case GT_BASE:
        base_select(t->idx);
        sprintf(line, "%.16s selected.", g_bhdr[g_bcur].name);
        message(COL_TEXT, line);
        intercept_window(t->idx, NULL);
        break;
    case GT_CRAFT:
        craft_window(t->idx, 0);
        break;
    case GT_UFO:
        selected = t->idx;
        sel_site = -1;
        ufo_window(t->idx, 0);
        break;
    case GT_SITE:
        sel_site = t->idx;
        selected = -1;
        /* fall through */
    default:
        site_window(t);
    }
}

/* ConfirmLandingState for craft k at its target: Yes = the battle there,
   No = it returns. Returns 1 if the campaign cannot go on. */
static int landing(int k, BattleFn battle)
{
    Craft *c = &g_craft[k];
    GeoTarget t;
    int n = 0, yes;

    if (c->state != CS_OUT || c->timer != 1)
        return 0;
    c->timer = 0;
    if (c->target >= 0 && (ufos[c->target].state == UFO_NONE
                           || ufos[c->target].id != c->target_id)) {
        /* Gone during the rest of the step (a crash site expired). */
        message(COL_INFO, "Target lost. Transport returning.");
        c->state = CS_RETURN;
        return 0;
    }
    if (c->target == CT_SITE)
        add_target(&t, &n, GT_SITE, c->pad, 0, 0);
    else
        ufo_target(&t, &n, c->target);
    timer_reset();                          /* time 5 seconds (time5Seconds: timerReset before the pop-up) */
    yes = gw_landing(k, &t);
    draw_map();
    c->state = CS_RETURN;
    if (!yes)
        return 0;
    return c->target == CT_SITE ? site_battle(k, battle) : ufo_battle(k, battle);
}

/* MissionDetectedState for terror site i. */
static void terror_popup(int i)
{
    const Site *t = &g_sites[i];
    GeoTarget g;
    int k = 0, r;

    r = gw_terror(t->city < 255 ? alien_city_name(t->city) : alien_region_name(t->x, t->y));
    draw_map();
    if (r == GW_CANCEL)
        return;
    sel_site = i;
    selected = -1;
    centre_on(t->x, t->y);
    timer_reset();                          /* time 5 seconds (MissionDetectedState: both buttons) */
    if (r != GW_CENTRE) {
        add_target(&g, &k, GT_SITE, i, 0, 0);
        intercept_window(-1, &g);
    }
}

/* The pop-ups of the last time step, in the order they came. */
static void show_popups(BattleFn battle)
{
    GeoTarget t;
    int kind, i, id;

    while (n_pops && !game_over && !geo_leave) {
        kind = pops[0].kind;
        i = pops[0].idx;
        id = pops[0].id;
        memmove(pops, pops + 1, --n_pops * sizeof pops[0]);
        switch (kind) {
        case PU_DETECTED:
            if (ufos[i].state != UFO_NONE && ufos[i].id == id && ufos[i].detected)
                ufo_window(i, 1);
            break;
        case PU_LOST:
            gw_lost(id);
            draw_map();
            break;
        case PU_PATROL:
            t.kind = GT_POINT;
            t.id = id;
            i = gw_patrol(i, &t) ? i : -1;
            draw_map();
            if (i >= 0)
                craft_window(i, 0);
            break;
        case PU_TARGET:
            if (g_craft[i].state == CS_OUT && g_craft[i].target == CT_WAYPOINT)
                craft_window(i, id);
            break;
        case PU_FUEL:
            if (gw_low_fuel(i)) {
                timer_reset();              /* OK - 5 Secs */
            }
            draw_map();
            break;
        case PU_CRAFTERR:
            if (gw_craft_error(i, id)) {
                timer_reset();              /* OK - 5 Secs */
            }
            draw_map();
            break;
        case PU_TERROR:
            if (g_sites[i].kind == SITE_TERROR)
                terror_popup(i);
            break;
        case PU_BASE:
            gw_base_found(alien_region_name(g_sites[i].x, g_sites[i].y));
            timer_reset();                  /* AlienBaseState: OK is timerReset */
            draw_map();
            break;
        case PU_LANDING:
            if (landing(i, battle))
                return;
            break;
        }
    }
}

/* A click on the geoscape: map or panel. Returns a key for the
   command loop, or 0 if handled here. */
static unsigned geo_click(unsigned key)
{
    int i, mx = g_mouse_x, my = g_mouse_y;

    if (mx < VIEW_W && my < WMAP_H) {
        GeoTarget t[GW_MAX_TARGETS];
        int n;
        /* DOS turns the globe to the point: the map scrolls to it. */
        if (key == KEY_RCLICK) {
            centre_on(view_x + (mx >> zoom), view_y + (my >> zoom));
            return 0;
        }
        /* A minimised fight's icon brings it back. */
        if (mx >= 5 && mx < 37 && my >= 5 && (my - 5) % 21 < 20 && fight_icons(0, (my - 5) / 21) >= 0) {
            restore_fights((my - 5) / 21);
            return 0;
        }
        n = map_targets(mx, my, 0, t);
        if (n > 1) {
            i = gw_pick(t, n);
            draw_map();
        } else
            i = n - 1;
        if (i >= 0)
            open_target(&t[i]);
        return 0;
    }
    if (map_nav(key) || key == KEY_RCLICK)
        return 0;
    if (mx >= PANEL_X) {
        if (my < 6 * BTN_H) {
            if (my < BTN_H) {
                intercept_window(-1, NULL);
                return 0;
            }
            return panel_buttons[my / BTN_H].key;
        }
        if (my >= SPEED_Y && my < SPEED_Y + 3 * 14) {
            speed = (my - SPEED_Y) / 14 * 2 + (mx >= PANEL_X + 32);
            paused = 0;
        }
        return 0;
    }
    return 0;
}

/* A map point chosen with the arrows or the mouse and Enter (on land if
   land is set); Esc cancels when cancel is set. Returns 1 if chosen. */
static int pick_point(int *px, int *py, int land, int cancel, const char *prompt)
{
    centre_on(*px, *py);
    message(COL_TEXT, prompt);
    for (;;) {
        unsigned key;
        erase_marks();
        draw_marks();
        mark(*px, *py, 3, 15);
        key = input_key();
        if (IS_CLICK(key) || (key & 0xFF) == '+' || (key & 0xFF) == '-') {
            int sx, sy;
            if (!IS_CLICK(key) || g_mouse_x >= VIEW_W || g_mouse_y >= WMAP_H) {
                map_nav(key);
                continue;
            }
            /* A second click on the marker confirms, like Enter. */
            to_screen(*px, *py, &sx, &sy);
            if (key == KEY_LCLICK && abs(sx - g_mouse_x) <= 3 && abs(sy - g_mouse_y) <= 3)
                key = 13;
            else {
                *px = (view_x + (g_mouse_x >> zoom)) % WMAP_W;
                *py = view_y + (g_mouse_y >> zoom);
            }
        }
        if ((key & 0xFF) == 13 && (!land || land_at(*px, *py)))
            break;
        if ((key & 0xFF) == 27 && cancel)
            return 0;
        switch (key >> 8) {
        case 0x48: *py -= 4; break;
        case 0x50: *py += 4; break;
        case 0x4B: *px -= 4; break;
        case 0x4D: *px += 4; break;
        }
        if (*px < 4) *px = 4;
        if (*px > WMAP_W - 5) *px = WMAP_W - 5;
        if (*py < 4) *py = 4;
        if (*py > WMAP_H - 5) *py = WMAP_H - 5;
        {
            int sx, sy;
            if (!to_screen(*px, *py, &sx, &sy) || sx < 8 || sx >= VIEW_W - 8)
                centre_on(*px, *py);
        }
    }
    erase_marks();
    return 1;
}

/* The first base, named after its region (the DOS game asks for a name). */
static void place_base(void)
{
    int x = 170, y = 36;

    pick_point(&x, &y, 1, 0, "Place your base: arrows or click, Enter.");
    BASE_X(0) = x;
    BASE_Y(0) = y;
    strncpy(g_bhdr[0].name, alien_region_name(x, y), sizeof g_bhdr[0].name - 1);
    if (!g_bhdr[0].name[0])
        strcpy(g_bhdr[0].name, "X-COM Base");
    message(COL_GOOD, "X-COM base operational.");
}

/* Build New Base (from the base screen): a site on land, its region's
   cost, a name, then the access lift on the new base's grid. */
static void new_base(void)
{
    char line[48], name[16];
    int x = BASE_X(g_bcur), y = BASE_Y(g_bcur), cost;

    if (g_nbases >= MAX_BASES) {
        message(COL_INFO, "X-COM has 8 bases already.");
        return;
    }
    if (!pick_point(&x, &y, 1, 1, "Site of the new base? Arrows/click, Enter; Esc.")) {
        message(COL_INFO, "");
        return;
    }
    cost = base_site_cost(x, y);
    sprintf(line, "Build a base in %.16s for $%dK?", alien_region_name(x, y), cost);
    if (!uk_confirm(line))
        return;
    if (g_base.funds < cost) {
        message(COL_ALERT, "Not enough money for a new base.");
        return;
    }
    strncpy(name, alien_region_name(x, y), sizeof name - 1);
    name[sizeof name - 1] = 0;
    if (!uk_edit("Name of the new base:", name, 15) || !name[0])
        return;
    g_base.funds -= cost;
    base_add(x, y, name);
    facil_screen(1);
    hud_clear_rows(0, 25);
    draw_map();
    sprintf(line, "%.16s: access lift built.", name);
    message(COL_GOOD, line);
}

/* The craft screens (Equip Craft): the buttons of a craft's screen open
   screens in other overlay sections, run from here (see craft_screens). */
static void craft_menus(void)
{
    int k = -1, r;

    while ((r = craft_screens(&k)) != CR_DONE) {
        if (r == CR_EQUIP)
            hwp_equip_screen(k);                /* src/hwpscr.c */
        else if (r == CR_CREW)
            crew_screen(k);                     /* src/crewscr.c */
        else
            armour_screen(k);
        uk_invalidate();
    }
}

/* B: the basescape; the screens it opens in other overlay sections run
   from here, with the basescape's section free to leave the area. */
static void base_screens(void)
{
    int r, k;

    erase_marks();
    do {
        r = base_screen();
        if (r == BS_FACIL)
            facil_screen(0);
        else if (r == BS_PSI)
            psi_screen();
        else if (r == BS_SOLDIERS) {
            while (soldiers_list())
                psi_screen();
        }
        else if (r == BS_INFO) {
            while ((k = base_info_screen()) != 0)
                if (k == 1)
                    stores_screen();
                else if (k == 3)
                    costs_screen();
                else
                    transfers_screen();
        } else if (r == BS_DISMANTLE)
            facil_dismantle(g_base_cell);
        else if (r == BS_TRANSFER)
            transfer_screen();
        else if (r == BS_BUY)
            purchase_screen();
        else if (r == BS_SELL)
            sell_screen();
        else if (r == BS_STORES)
            stores_screen();
        else if (r == BS_XFERS)
            transfers_screen();
        else if (r == BS_ALIENS)
            contain_screen();
        else if (r == BS_RESEARCH)
            research_screen();
        else if (r == BS_MANUF)
            manufacture_screen();
        else if (r == BS_EQUIP)
            craft_menus();
        else if (r == BS_SELECT && (k = base_list("Select Base", -1)) >= 0) {
            base_select(k);
            log_printf("BASE %s selected", g_bhdr[g_bcur].name);
        }
        uk_invalidate();
    } while (r != BS_DONE && r != BS_NEW_BASE);
    hud_clear_rows(0, 25);
    draw_map();
    message(COL_INFO, "");
    if (r == BS_NEW_BASE)
        new_base();
}

/* The completion pop-ups of the last step (GeoscapeState::popup), in the
   order they came: research (then what it opened to research and to
   produce), production and construction. Their second buttons open the
   UFOpaedia article, that base's research or manufacture screen or its
   basescape (Go to Base: the base stays current). */
static void popups(void)
{
    char (*ln)[41] = malloc(N_TOPICS * sizeof *ln);
    int k, cur = g_bcur;

    erase_marks();
    memset(g_bb->popped, 0, sizeof g_bb->popped);
    for (k = 0; k < g_bb->n_pop; k++) {
        PopEvent e = g_bb->pop[k];
        if (e.base >= g_nbases || e.kind == POP_USED)
            continue;
        base_select(e.base);
        if (e.kind == POP_ARRIVAL) {
            int r = gpop_arrivals(k);       /* ItemsArrivingState */
            if (r >= 0) {
                timer_reset();              /* (Go to Base: timerReset) */
                base_select(r);
                base_screens();
                cur = g_bcur;
            }
        } else if (e.kind == POP_RESEARCH) {
            timer_reset();                  /* timerReset: time 5 seconds */
            if (gpop_research(e.id))
                pedia_article(g_topics[e.id].name);
            hud_clear_rows(0, 25);
            draw_map();
            if (ln && gpop_new_research(e.id, ln) == 1)
                research_screen();
            hud_clear_rows(0, 25);
            draw_map();
            if (ln && gpop_new_manufacture(e.id, ln) == 1)
                manufacture_screen();
        } else if (gpop_production(e.kind, e.id, e.base)) {
            timer_reset();                  /* (Go to Base: timerReset) */
            if (e.kind == POP_BUILT) {
                base_screens();
                cur = g_bcur;
            } else
                manufacture_screen();
        }
        hud_clear_rows(0, 25);
        draw_map();
    }
    g_bb->n_pop = 0;
    log_printf("GEO pop-ups done: clock %s", paused ? "paused" : "running");
    free(ln);
    base_select(cur);
    uk_invalidate();
    message(COL_INFO, "");
}

/* P: the current base's ready transport with the most seats flies to a
   way point and patrols there until it runs low on fuel; there its radar
   may find alien bases (ten_minute_tick). */
static void patrol(void)
{
    GeoTarget t;
    int k = craft_best_transport(), x = BASE_X(g_bcur), y = BASE_Y(g_bcur);

    if (k < 0) {
        message(COL_INFO, "No transport ready (B: base, hangar).");
        return;
    }
    if (!pick_point(&x, &y, 0, 1, "Patrol where? Arrows/click, Enter; Esc.")) {
        message(COL_INFO, "");
        return;
    }
    t.kind = GT_POINT;
    t.id = 0;
    set_destination(k, &t, x, y);
    message(COL_TEXT, "Transport on patrol.");
}

/* The final mission as in the DOS game: an Avenger with its soldiers
   lands on Mars (CYD1x.PAK); those who come through (all survivors after
   a win, those in the exit area after an abort) go on into the alien base
   with the brain (CYD2x.PAK) as they are, with the items on the ground
   (BattlescapeGenerator::nextStage). CYDx.PAK alone when the two-stage
   packs are missing. Winning the second stage (the brain destroyed or
   every alien dead) wins the game; anything else loses it. */
/* The HWPs that came through the first stage go on with the rounds,
   health and armour they have left (BattlescapeGenerator::nextStage),
   before the soldiers' units are packed up. */
static void hwp_stage_carry(void)
{
    int i, n = 0;

    for (i = 0; i < g_hwp_n; i++) {
        int u = g_hwp[i].unit;
        if (u < 0 || !g_units[u].alive)
            continue;
        if (n == MAX_HWP)
            break;                  /* (an Avenger carries four) */
        g_hwp[n] = g_hwp[i];
        g_hwp[n].ammo = (unsigned char)g_units[u].ammo;
        g_hwp_hurt[n][0] = g_units[u].hp;
        g_hwp_hurt[n][1] = g_units[u].armor[0];
        g_hwp_hurt[n][2] = g_units[u].armor[1];
        g_hwp_hurt[n][3] = g_units[u].armor[2];
        g_hwp_hurt[n][4] = g_units[u].armor_left;
        g_hwp_hurt[n][5] = g_units[u].armor_under;
        g_hwp[n++].unit = -1;
    }
    g_hwp_n = n;
}

static void cyd_battle(BattleFn battle);

/* Craft c (confirmed in Select Destination), or with c < 0 (key C) the
   first ready Avenger with soldiers, after ConfirmCydoniaState. */
static void cydonia(BattleFn battle, int c)
{
    int cur = g_bcur;

    cyd_due = -1;
    if (c < 0) {
        c = craft_for_cydonia();
        if (c < 0) {
            message(COL_INFO, "Cydonia needs a ready Avenger with soldiers or HWPs.");
            return;
        }
        if (!gw_cydonia()) {
            draw_map();
            return;
        }
    }
    busy_show();                /* the battle's setup begins (input.h) */
    base_select(g_craft[c].base);
    g_troop_craft = c;
    craft_map(c);
    hwp_board(c);
    /* The Avenger's fit crew. */
    g_squad_max = craft_crew(c);
    g_squad_craft = c + 1;
    if (g_squad_max > craft_seats(&g_craft[c]))
        g_squad_max = craft_seats(&g_craft[c]);
    log_printf("GEO %s carries %d of %d soldiers to Cydonia", craft_name(&g_craft[c]),
               g_squad_max, roster_ready());
    message(COL_ALERT, "The final assault on Cydonia begins.");
    equip_deploy(c, 1);
    g_bctx.k = (signed char)c;
    g_bctx.cur = (signed char)cur;
    g_bctx.stage = 1;
    g_bctx.kills1 = g_bctx.lost1 = 0;
    cyd_battle(battle);
}

/* The stages from g_bctx.stage on, also of a saved battle (g_bs_resume:
   the squad, the HWPs and in the second stage those carried over are in
   the save; the first stage's result in g_bctx). */
static void cyd_battle(BattleFn battle)
{
    char v = g_pakv, pak1[13], pak2[13];
    int kills = 0, lost = 0, k2, l2, res = 1, stages = 2, c = g_bctx.k, cur = g_bctx.cur;
    int carried = 0, go = 0;
    FILE *f;

    sprintf(pak1, "CYD1%c.PAK", v);
    sprintf(pak2, "CYD2%c.PAK", v);
    f = fopen(pak2, "rb");
    if (f)
        fclose(f);
    else {
        sprintf(pak1, "CYD%c.PAK", v);
        stages = 1;
    }
    g_troop_craft = c;
    snapshot(BK_CYD);
    free_world();
    if (g_bctx.stage == 1) {
        g_night = 1;
        g_next_race = stages == 2 ? 0 : 5;      /* the landing site: sectoids */
        res = battle(pak1, &kills, &lost);
        if (BT_LEFT(res)) {
            battle_left(res);
            return;
        }
        g_squad_craft = 0;
        g_captured = 0;             /* nothing is recovered from Mars */
        if (stages == 2 && (res == 1 || res == -2)) {
            hwp_stage_carry();
            carried = roster_stage_carry();
            go = carried || g_hwp_n;    /* HWPs alone go on too */
        }
        log_printf("GEO Cydonia stage 1: result %d, %d soldiers go on", res, carried);
        if (go && g_hwp_n)
            log_printf("GEO Cydonia stage 1: %d HWPs go on", g_hwp_n);
        g_squad_max = carried;
        g_bctx.stage = 2;           /* a save in the second stage keeps the first's result */
        g_bctx.kills1 = kills;
        g_bctx.lost1 = lost;
    } else
        go = 1;                     /* a saved second stage */
    if (go) {
        g_night = 0;
        g_next_race = 5;
        k2 = l2 = 0;
        res = battle(pak2, &k2, &l2);
        if (BT_LEFT(res)) {
            battle_left(res);
            return;
        }
        kills = g_bctx.kills1 + k2;
        lost = g_bctx.lost1 + l2;
        log_printf("GEO Cydonia stage 2: result %d", res);
    } else if (res == -2)
        res = -1;               /* nobody reached the exit area: lost */
    battle_done();
    g_squad_max = 8;
    g_hwp_n = 0;
    g_troop_craft = -1;
    roster_after_battle();
    g_next_race = -1;
    if (load_world()) {
        game_over = -2;
        return;
    }
    /* The game ends here, won or lost: no promotions or score. DOS shows
       no debriefing either (the slides follow): only without them. */
    if (!cut_will_end()) {
        DebStats ds;
        memset(&ds, 0, sizeof ds);
        ds.qty[DS_KILLED] = kills;
        ds.qty[DS_SOLDIERS] = lost;
        gpop_debrief(DB_CYDONIA, res, 0, &ds);
    }
    base_select(cur);
    hud_clear_rows(0, 25);
    draw_map();
    if (res == 1) {
        message(COL_GOOD, "Cydonia has fallen. Earth is saved!");
        game_over = 2;
    } else {
        message(COL_ALERT, "The assault on Cydonia has failed.");
        game_over = -1;
    }
}

/* Save (S), UFOpaedia (U) and the options menu (Esc). Sets geo_leave to
   end geo_run (g_geo_abandon says where to). */
static void geo_front_end(int ch)
{
    int opt = ch == 's' ? OPT_CANCEL : ch == 'u' ? OPT_PEDIA : -1;

    erase_marks();
    if (ch == 's')
        menu_save();
    else if (ch == 27)
        opt = menu_options();
    if (opt == OPT_PEDIA)
        pedia_screen();
    menu_free_lines();
    if (opt == OPT_ABANDON || opt == OPT_QUIT) {
        g_geo_abandon = opt == OPT_ABANDON;
        geo_leave = 1;
        return;
    }
    if (opt == OPT_LOADED && g_bs_resume)
        return;                 /* a battle save: its battle next (battle_exits) */
    hud_clear_rows(0, 25);
    draw_map();
    if (opt == OPT_LOADED) {
        paused = 1;
        message(COL_GOOD, "Campaign loaded.");
    } else
        message(COL_INFO, "");
}

void options_menu(void)
{
    geo_front_end(27);
}

int geo_init(void)
{
    return bitmap ? 0 : load_world();   /* (loaded already: a campaign loaded in a quick battle) */
}

/* Runs one time step at the current speed; returns 1 if something needs
   the player (as advance does). */
static int time_step(BattleFn battle)
{
    long mins;
    int alert = 0, chunk;

    if (speed_secs[speed] < 60) {
        second += (int)speed_secs[speed];
        if (second < 60)
            return 0;
        second -= 60;
        return advance(1, battle);
    }
    /* Long steps go 10 minutes at a time (the DOS game's ticks); an alert
       stops the rest. */
    for (mins = speed_secs[speed] / 60; mins > 0 && !alert && !game_over; mins -= chunk) {
        chunk = 10 - minute % 10;
        if (chunk > mins)
            chunk = (int)mins;
        busy_tick();            /* a day's step: the busy pointer (input.h) */
        alert = advance(chunk, battle);
    }
    return alert;
}

static void geo_loop(BattleFn battle);

/* A quick battle's save (OpenXcom's New Battle) loaded on the start menu,
   the geoscape or in a battle: the battle without a campaign; after it
   the start menu (DebriefingState of a New Battle: MainMenuState). */
static void quick_battle(BattleFn battle)
{
    int kills, lost, res;

    free_world();
    res = battle("", &kills, &lost);
    battle_left(res);
    if (!BT_LEFT(res))
        g_geo_abandon = geo_leave = 1;
}

/* A battle save was loaded: back into its battle, the way the geoscape
   started it (g_bctx), with the campaign as it was at its start. */
static void geo_resume(BattleFn battle)
{
    paused = 1;                 /* (as after a load on the geoscape) */
    switch (g_bctx.kind) {
    case BK_QUICK:
        quick_battle(battle);
        break;
    case BK_UFO:
        ufo_battle(g_bctx.k, battle);
        break;
    case BK_SITE:
        site_battle(g_bctx.k, battle);
        break;
    case BK_BASE:
        base_defence(battle);
        break;
    case BK_CYD:
        cyd_battle(battle);
        break;
    }
    g_bs_resume = 0;
}

/* A loaded battle save goes on; a battle left by Load Game loads that
   slot (maybe another battle), one left by Abandon Game goes back to the
   start menu (AbandonGameState). */
static void battle_exits(BattleFn battle)
{
    while (!game_over && !geo_leave) {
        if (g_bs_exit == BX_ABANDON) {
            g_bs_exit = BX_NONE;
            log_printf("GEO game abandoned in battle");
            g_geo_abandon = 1;
            geo_leave = 1;
            return;
        }
        if (g_bs_exit == BX_LOAD) {
            g_bs_exit = BX_NONE;
            if (!menu_load_slot(g_bs_slot)) {
                /* The campaign in memory is no longer whole. */
                g_geo_abandon = 1;
                geo_leave = 1;
                return;
            }
            if (!g_bs_resume) {
                /* (a battle save goes straight into its battle) */
                paused = 1;
                hud_clear_rows(0, 25);
                draw_map();             /* at the saved view */
                show_status();
                message(COL_GOOD, "Campaign loaded.");
            }
        }
        if (!g_bs_resume)
            return;
        geo_resume(battle);
        if (!g_bs_exit)
            return;
    }
}

void geo_run(BattleFn battle)
{
    uk_open();                  /* the fonts while the geoscape is shown */
    geo_loop(battle);
    uk_close();
}

static void geo_loop(BattleFn battle)
{
    g_geo_abandon = 0;
    game_over = geo_leave = 0;
    if (QUICK_RESUME)
        goto run;               /* a quick battle's save: no campaign to show (battle_exits) */
    if (!g_geo_loaded)
        new_campaign();
    hud_clear_rows(0, 25);
    draw_map();
    show_status();
    if (g_geo_loaded) {
        paused = 1;                     /* the view as saved */
        draw_marks();
        show_status();
        message(COL_GOOD, "Campaign loaded.");
    } else {
        int i;
        base_new();
        roster_new(8);
        for (i = 0; i < g_roster_n; i++) {
            base_name_soldier(g_roster[i].name);
            g_roster[i].craft = 1;      /* Skyranger-1's crew (craft_new) */
        }
        place_base();
        alien_new_game(BASE_X(0), BASE_Y(0));
        g_bb->gr.funds = g_base.funds;
    }

run:
    while (!game_over && !geo_leave) {
        unsigned long t0 = timer_read();
        int i;

        battle_exits(battle);           /* battle saves (src/bsave.h) */
        track_funds();
        if (game_over || geo_leave)
            break;
        if (input_ready()) {
            unsigned key = input_key();
            int ch;
            if (IS_CLICK(key))
                key = geo_click(key);
            ch = key & 0xFF;
            if (ch >= 'A' && ch <= 'Z')
                ch += 32;
            /* 1-4 keep their old meaning (5 min .. 4 h, now 1 day). */
            if (ch >= '1' && ch <= '4') {
                speed = ch - '1' + 2;
                paused = 0;
            } else if (map_nav(key))
                ;
            else if (ch == 'g')
                graphs_page();
            else if (ch == 'f')
                funding_screen();
            else if (ch == ' ')
                paused = !paused;
            else if (ch == '\t') {
                select_next();
                open_selected();
            } else if (ch == '\r')
                send_troops();
            else if (ch == 'i')
                intercept();
            else if (ch == 'd')
                restore_fights(-1);     /* the minimised fights back */
            else if (ch == 'x' && g_debug && selected >= 0) {
                /* Debug: the selected UFO lands (a crash or terror site
                   stays one) and the Skyranger is there. */
                if (ufos[selected].state == UFO_FLYING || ufos[selected].state == UFO_LEAVING) {
                    ufos[selected].state = UFO_LANDED;
                    ufos[selected].timer = 48 * 60;
                    ufos[selected].alt = 0;
                }
                log_printf("GEO debug: troops at UFO-%d %s", ufos[selected].id,
                           ufos[selected].state == UFO_TERROR ? "terror site"
                           : ufo_names[ufos[selected].type]);
                debug_troops();
            }
            else if (ch == '!' && g_debug)
                launch_attack(1);       /* debug: a base attack */
            else if (ch == 'z' && g_debug) {
                /* Debug: the base's first soldier is wounded for 5 days
                   and keeps his craft seat (as after a battle). */
                g_roster[0].recovery = 5;
                log_printf("GEO debug: %s wounded", g_roster[0].name);
            }
            else if (ch == 'k' && g_debug)
                craft_debug_lose();     /* debug: a transport shot down */
            else if (ch == 'r')
                show_roster();
            else if (ch == 'p')
                patrol();
            else if (ch == 'a') {
                select_site();
                open_selected();
            }
            else if (ch == 'b') {
                timer_reset();              /* GeoscapeState::btnBasesClick: timerReset */
                base_screens();
            }
            else if (ch == 'c' && g_base.cydonia == 2)
                cydonia(battle, -1);
            if (cyd_due >= 0)
                cydonia(battle, cyd_due);   /* Select Destination's Cydonia button */
            else if (ch == 's' || ch == 'u')
                geo_front_end(ch);
            else if (ch == 27)
                options_menu();
            if (geo_leave)
                break;
            if (g_bs_exit || g_bs_resume)
                continue;
        }
        /* An alert (a pop-up) only breaks the step; the clock goes on at its
           speed unless the pop-up's button resets it (GeoscapeState::popup
           sets _pause for the rest of the step, timerReset is the buttons').
           The test scripts' token OLDCLOCK (AUTO) stops it, as before. */
        if (!paused && time_step(battle) && g_alert_pause)
            paused = 1;
        /* After the step: the landings (ConfirmLandingState) with their
           battles, a base under attack. */
        for (i = 0; i < MAX_CRAFT && !game_over && !geo_leave && !g_bs_exit; i++)
            if (landing(i, battle))
                break;
        if (attack_due >= 0 && !game_over && !geo_leave && !g_bs_exit) {
            i = attack_due;
            attack_due = -1;
            if (i == attacker && ufos[i].state == UFO_FLYING)
                attack_base(i, battle);
        }
        if (g_bs_exit)
            continue;
        show_popups(battle);
        if (g_bb->n_pop && !game_over)
            popups();
        draw_marks();
        show_status();
        /* Pace the clock: about 8 steps per second. */
        while (timer_read() - t0 < 1193UL * 120)
            if (input_ready())
                break;
    }
    if (game_over) {
        /* DOS: the win or lose slides (CutsceneState), then the start menu. */
        if (game_over != -2 && cut_will_end()) {
            message(game_over > 0 ? COL_GOOD : COL_ALERT, game_over > 0 ? "Victory!" : "Game over.");
            cut_end(game_over > 0);
        } else {
            message(game_over > 0 ? COL_GOOD : COL_ALERT,
                    game_over > 0 ? "Victory! Press a key." : "Game over. Press a key.");
            input_key();
        }
        g_geo_abandon = game_over != -2;    /* not when the world map is gone */
    }
}
