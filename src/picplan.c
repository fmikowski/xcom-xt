/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tight battles (docs/design.md): which unit pictures fit, decided before they
   load, and the sets that show another set's pictures instead. Every unit
   comes; only its picture gives way. Battle set-up only. */
#include <dos.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "batui.h"
#include "gfx.h"
#include "hwp.h"
#include "input.h"
#include "items.h"
#include "map.h"
#include "pak.h"
#include "unit.h"
#include "xmemlend.h"

unsigned long __cdecl xms_find(void);           /* sndasm.asm */
void __cdecl xms_call(unsigned __far *r);

#define PIC_RESERVE 1536L       /* free memory a tight battle keeps after what it must load */
#define PIC_SPARE   2560L       /* and on top, after a picture it could do without */
#define SC_MIN      8192L       /* the disk cache at least */
#define XS_BELOW    49152L      /* a 286 takes the XMS store when less would be left (sc_decide) */
#define XS_EMS      38912L      /* EMS: 16 KB + the canvas's 22 KB, after the HWP sets that come later */
#define SUIT_FRAMES (UNIT_FRAMES + 8)
#define RACE_HEAD_BYTES 32L     /* sizeof RaceHeader (pak.c) */

/* With the disk cache: the walls' and objects' pictures stay out (2), floors
   stay in (the floor pass reads their OR pictures itself). What is left. */
static unsigned long page_mark(unsigned char *need)
{
    unsigned r, i;
    unsigned long bytes = 0;

    for (r = 1; r < g_pak.n_mcd; r++) {
        const Mcd *m = &g_pak.mcd[r];
        if (MCD_PART(m) != PART_FLOOR && need[m->sprite])
            need[m->sprite] = 2;
    }
    for (r = 1; r < g_pak.n_mcd; r++) {
        const Mcd *m = &g_pak.mcd[r];
        if (MCD_PART(m) == PART_FLOOR && need[m->sprite])
            need[m->sprite] = 1;
    }
    for (i = 0; i < mission_sprites; i++)
        if (need[i] == 1)
            bytes += spr_size(i);
    return bytes;
}

unsigned long pak_sprite_paged(void)
{
    unsigned long bytes;
    unsigned char *need = malloc(mission_sprites);

    if (!need)
        return 0;
    mark_needed(need);
    bytes = page_mark(need);
    free(need);
    return bytes;
}

/* ------------------------------------------------------------------ */
/* The picture store (docs/design.md, "Tight battles"): with EMS or XMS the
   pictures that do not fit in conventional memory wait there instead of
   being shared or read from the mission pack: the map's walls and objects
   and, when even that is not enough, units' sets ("+name" in g_pack_names).
   EMS: packed into 16 KB logical pages (none crosses a page's end) and drawn
   in place at physical page 2 of the frame (the read cache has pages 0-1,
   the canvas page 3); a picture's table pointer is page 2's segment less its
   logical page, its offset plus 16 per page (the same address), so the
   segment names the page. XMS: one block, each picture on a paragraph; the
   pointer's segment is F000h + its length in words, its offset the
   paragraph; sprite_fault (sndasm.asm) copies it into the buffer. */
#define XS_PAGE     16384u
#define XS_PAGES(b) (unsigned)(((b) + XS_PAGE - 1) / XS_PAGE)
#define XS_KB(b)    (unsigned)(((b) + 1023) >> 10)
#define XS_SAVED    294912UL    /* a saved battle's store (opened before its map is known) */
#define XS_SET      4096UL      /* per unit set: page ends and paragraphs */
#define XS_WALK     34816UL     /* DOS's walk frames (floorld.c walk_load): 4 sets, about 30 KB */

static unsigned long xs_at, xs_cap;     /* the store's next free byte, its size */
static unsigned char *xs_need;          /* sc_decide's marks, for sc_keep */
static unsigned xs_lent, xs_nslot;      /* the read cache's blocks lent; its count before */

/* The XMS budget: the read cache lends the top of its block (xmemlend.h). */

static int ems_ok(void)
{
    union REGS r;
    unsigned seg = *(unsigned __far *)MK_FP(0, 0x67 * 4 + 2), i;
    const char __far *dev = (const char __far *)MK_FP(seg, 10);

    if (!seg)
        return 0;
    for (i = 0; i < 8 && dev[i] == "EMMXXXX0"[i]; i++)
        ;
    r.h.ah = 0x40;
    return i == 8 && (int86(0x67, &r, &r), !r.h.ah);
}

static unsigned xms_kb(void)
{
    unsigned x[5];

    if (!xms_find())
        return 0;
    x[0] = 0x0800;              /* AX = the largest free block in KB */
    x[1] = x[2] = x[3] = x[4] = 0;
    xms_call((unsigned __far *)x);
    return x[0];
}

/* Where a picture of s bytes goes: none crosses an EMS page's end; XMS
   ones start on paragraphs. */
static unsigned long xs_place(unsigned long *at, unsigned long s, int ems)
{
    unsigned long p;

    if (ems && (*at % XS_PAGE) + s > XS_PAGE)
        *at += XS_PAGE - *at % XS_PAGE;
    p = *at;
    *at += ems ? s : (s + 15) & ~15UL;
    return p;
}

/* What the pictures need marks 2 take in either store. */
static unsigned long xs_bytes(const unsigned char *need)
{
    unsigned long e = 0, x = 0;
    unsigned i;

    for (i = 0; i < mission_sprites; i++)
        if (need[i] == 2) {
            xs_place(&e, spr_size(i), 1);
            xs_place(&x, spr_size(i), 0);
        }
    return e > x ? e : x;
}

/* Which store has room for bytes: 1 EMS, 2 XMS, 0 none. */
static int xs_mode(unsigned long bytes)
{
    union REGS r;

    if (g_herc)
        return 0;
    if (ems_ok()) {
        r.h.ah = 0x42;
        int86(0x67, &r, &r);
        if (!r.h.ah && r.x.bx >= XS_PAGES(bytes))
            return 1;
    }
    return xms_kb() >= XS_KB(bytes) || xl_kb() >= XS_KB(bytes) ? 2 : 0;
}

/* The store's EMS pages or XMS block back. */
static void xs_free(void)
{
    unsigned x[5];

    if (xs_lent) {
        xl_back(xs_nslot, xs_lent * 4);
        xs_lent = 0;
    } else if (g_sc_xh && g_sc_top) {
        union REGS r;
        r.h.ah = 0x45;
        r.x.dx = g_sc_xh;
        int86(0x67, &r, &r);
    } else if (g_sc_xh) {
        x[0] = 0x0A00;
        x[2] = g_sc_xh;
        x[1] = x[3] = x[4] = 0;
        xms_call((unsigned __far *)x);
    }
    g_sc_xh = g_sc_top = g_sc_cur = 0;
    g_sc_tag = 0xFFFF;
    xs_at = xs_cap = 0;
    xs_need = NULL;
}

/* atexit: a battle left by exit (global: the linker gives it an overlay vector). */
void sc_exit(void)
{
    xs_free();
}

/* Opens the store with room for bytes (0: a saved battle's, as much of
   XS_SAVED as there is); 1 if it is open. */
static int xs_open(unsigned long bytes)
{
    union REGS r;
    unsigned x[5], n;
    int mode;
    static unsigned char exit_set;

    if (g_sc_xh)
        return 1;
    if (!bytes)
        for (bytes = XS_SAVED; bytes > 65536UL && !xs_mode(bytes); bytes -= 32768UL)
            ;
    mode = xs_mode(bytes);
    if (mode == 1) {
        n = XS_PAGES(bytes);
        r.h.ah = 0x41;
        int86(0x67, &r, &r);
        g_sc_top = r.x.bx + 0x800;      /* physical page 2 */
        r.h.ah = 0x43;
        r.x.bx = n;
        int86(0x67, &r, &r);
        if (r.h.ah) {
            g_sc_top = 0;
            return 0;
        }
        g_sc_xh = r.x.dx;
        g_sc_tag = g_sc_top - (n - 1);
        xs_cap = (unsigned long)n * XS_PAGE;
        log_printf("picture store: EMS, %u pages", n);
    } else if (mode == 2) {
        n = XS_KB(bytes);
        x[0] = 0x0900;
        x[2] = xms_kb() >= n ? n : 0;
        x[1] = x[3] = x[4] = 0;
        if (x[2])
            xms_call((unsigned __far *)x);
        if (x[2] && x[0] == 1)
            g_sc_xh = x[2];
        else if ((xs_nslot = xl_lend(n, &g_sc_xh)) == 0)
            return 0;
        else
            xs_lent = (n = (n + 3) & ~3u) / 4;
        g_sc_tag = 0xF000;
        xs_cap = (unsigned long)n * 1024;
        log_printf("picture store: XMS, %u KB%s", n, xs_lent ? " lent by the read cache" : "");
    } else
        return 0;
    xs_at = 0;
    g_sc_cur = 0;
    g_pic_note |= 8;
    if (!exit_set) {
        exit_set = 1;
        atexit(sc_exit);
    }
    return 1;
}

/* A picture of s bytes, read from f where it stands, into the store: its
   table pointer, g_no_rows if that fails. tmp: s + 1 bytes (XMS). */
static unsigned char *xs_put(FILE *f, unsigned long s, unsigned char *tmp)
{
    unsigned long p = xs_place(&xs_at, s, g_sc_top != 0);
    unsigned mv[8], x[5];

    if (xs_at > xs_cap)
        return g_no_rows;
    if (g_sc_top) {
        union REGS r;
        unsigned lp = (unsigned)(p / XS_PAGE), o = (unsigned)(p % XS_PAGE);
        r.x.ax = 0x4402;
        r.x.bx = lp;
        r.x.dx = g_sc_xh;
        int86(0x67, &r, &r);
        g_sc_cur = 0;
        if (r.h.ah || fread(MK_FP(g_sc_top, o), 1, (size_t)s, f) != (size_t)s)
            return g_no_rows;
        return (unsigned char *)MK_FP(g_sc_top - lp, o + lp * 16);
    }
    if (fread(tmp, 1, (size_t)s, f) != (size_t)s)
        return g_no_rows;
    if (xs_lent)
        p += (unsigned long)(xs_nslot - xs_lent) << 12;     /* above the cache's slots */
    mv[0] = ((unsigned)s + 1) & ~1u;
    mv[1] = 0;
    mv[2] = 0;
    mv[3] = FP_OFF(tmp);
    mv[4] = FP_SEG(tmp);
    mv[5] = g_sc_xh;
    mv[6] = (unsigned)p;
    mv[7] = (unsigned)(p >> 16);
    x[0] = 0x0B00;
    x[3] = FP_OFF((unsigned __far *)mv);
    x[4] = FP_SEG((unsigned __far *)mv);
    xms_call((unsigned __far *)x);
    if (x[0] != 1)
        return g_no_rows;
    return (unsigned char *)MK_FP(0xF000 + (unsigned)((s + 1) >> 1), (unsigned)(p >> 4));
}

unsigned long sc_room(void)
{
    unsigned long slack = g_sc_top ? XS_PAGE : 0;

    return g_sc_xh && xs_at + slack < xs_cap ? xs_cap - xs_at - slack : 0;
}

int sc_walk(void *f, const unsigned long *off, unsigned n, unsigned char **p)
{
    unsigned long big = 0;
    unsigned i, bad = 0;
    unsigned char *tmp = NULL;

    for (i = 0; i < n; i++)
        if (off[i + 1] - off[i] > big)
            big = off[i + 1] - off[i];
    if (!g_sc_xh || (!g_sc_top && (tmp = malloc((size_t)big + 2)) == NULL))
        return 1;
    for (i = 0; i < n; i++)
        bad += (p[i] = xs_put((FILE *)f, off[i + 1] - off[i], tmp)) == g_no_rows;
    if (tmp)
        free(tmp);
    return bad;
}

/* The walk frames' room in the store: with DOS's walk frames (EGA and CGA
   W packs), on a store that takes them. */
static unsigned long xs_walk(void)
{
    const char *e = getenv("XWALK");

    return g_vga || g_herc || (e && !stricmp(e, "OFF")) ? 0 : XS_WALK;
}

/* The map's walls and objects (need 2) into the store (sc_keep). */
static void xs_fill(const unsigned char *need, FILE *f, const unsigned long *off,
                    unsigned long pos, unsigned common, unsigned n)
{
    unsigned long s, next = 0xFFFFFFFFUL, big = 0;
    unsigned i, bad = 0;
    unsigned char *tmp = NULL;

    for (i = 0; i < n; i++)
        if (need[i] == 2 && spr_size(i) > big)
            big = spr_size(i);
    if (!big)
        return;
    if (!g_sc_top && (tmp = malloc((size_t)big + 2)) == NULL) {
        log_printf("picture store: no room to fill it");
        return;
    }
    for (i = 0; i < n; i++) {
        const unsigned long *o = off + (i < common ? i : i + 1);
        if (need[i] != 2)
            continue;
        s = o[1] - o[0];
        if (next != o[0] && fseek(f, (long)(pos + o[0]), SEEK_SET))
            g_pak.sprite[i] = g_no_rows;
        else
            g_pak.sprite[i] = xs_put(f, s, tmp);
        next = g_pak.sprite[i] == g_no_rows ? 0xFFFFFFFFUL : o[1];
        bad += next == 0xFFFFFFFFUL;
    }
    if (tmp)
        free(tmp);
    log_printf("picture store: walls and objects, %lu of %lu bytes%s", xs_at, xs_cap,
               bad ? ", some failed" : "");
}

/* A unit pack's pictures (load_units: an HWP's, or a soldier armour's,
   with the Flying Suit's hovering legs when the store has room: floorld.c
   then needs no conventional memory for them) into the store; *set gets
   its unit set. NULL on success. */
const char *xs_units(const char *path, int suit, unsigned char *set)
{
    unsigned char hb[(unsigned)RACE_HEAD_BYTES], **t = NULL, *tmp = NULL;
    unsigned long *off = NULL, big = 0;
    unsigned i, n, ns, nsets, base = g_pak.n_sprites, bad = 0;
    const char *err = NULL;
    FILE *f;

    if (!xs_open(0))
        return "no picture store";
    f = fopen(path, "rb");
    if (!f)
        return "cannot open unit pack";
    if (fread(hb, 1, sizeof hb, f) != sizeof hb || memcmp(hb, "XRC1", 4) || hb[10] != g_pak.video
        || g_pak.n_unit_sets + (nsets = *(unsigned short *)(hb + 6)) > MAX_UNIT_SETS) {
        fclose(f);
        return "not a unit pack";
    }
    ns = *(unsigned short *)(hb + 8);
    off = malloc((size_t)(ns + 1) * 4);
    t = malloc((size_t)(base + ns + 1) * sizeof(unsigned char *));
    if (!off || !t || fseek(f, RACE_HEAD_BYTES + 8 * (long)sizeof(AlienType), SEEK_SET)
        || fread(off, 4, ns + 1, f) != ns + 1
        || fseek(f, RACE_HEAD_BYTES + 8 * (long)sizeof(AlienType) + 4L * (ns + 1), SEEK_SET)) {
        err = "out of memory (unit pack)";
        goto out;
    }
    n = ns;
    if (suit && ns > SUIT_FRAMES
        && xs_at + off[ns] - off[0] + 16UL * ns + (g_sc_top ? XS_PAGE : 0) > xs_cap)
        n = SUIT_FRAMES;
    for (i = 0; i < n; i++)
        if (off[i + 1] - off[i] > big)
            big = off[i + 1] - off[i];
    if (!g_sc_top && (tmp = malloc((size_t)big + 2)) == NULL) {
        err = "out of memory (unit pack)";
        goto out;
    }
    for (i = 0; i < base; i++)
        t[i] = g_pak.sprite[i];
    for (i = 0; i < n; i++) {
        t[base + i] = xs_put(f, off[i + 1] - off[i], tmp);
        bad += t[base + i] == g_no_rows;
    }
    for (; i < ns; i++)
        t[base + i] = t[base + (i - n) * 5];    /* (hovering legs: floorld.c) */
    free(g_pak.sprite);
    g_pak.sprite = t;
    t = NULL;
    g_pak.n_sprites = base + ns;
    for (i = 0; i < nsets; i++)
        g_pak.unit_base[g_pak.n_unit_sets + i] = base + i * UNIT_FRAMES;
    *set = (unsigned char)g_pak.n_unit_sets;
    g_pak.n_unit_sets += nsets;
    if (bad)
        log_printf("picture store: %u pictures of %s failed", bad, path);
    else if (suit && n > SUIT_FRAMES)
        log_printf("picture store: the hovering legs of %s", path);
out:
    if (t)
        free(t);
    if (off)
        free(off);
    if (tmp)
        free(tmp);
    fclose(f);
    return err;
}

int cpu_below_286(void);
#pragma aux cpu_below_286 = "push sp" "pop ax" "sub ax, sp" value [ax] modify exact [ax];

/* An XMS store worth taking unplanned: on a 286 or better (HIMEM's moves are
   quick there; an XT's XMS driver copies through its EMS window). */
static int xs_cheap(void)
{
    return !cpu_below_286() && xms_find();
}

/* XMS on an XT (v20x: EXMS86 copies through the EMS board): the store as
   with EMS, when under XS_EMS would be left; a picture's fault costs a move
   (BENCH "picture store: 100 XMS moves"), full redraws about 5 % more. */
static int xs_xt(void)
{
    return cpu_below_286() && xms_find();
}

/* The HWP sets that load after the map's pictures (pic_plan: g_pic_early 0)
   into what they leave: their bytes. */
static long xs_later(void)
{
    long n = 0, b;
    int t;

    for (t = 0; t < N_HWP && !g_pic_early; t++) {
        const char *name = g_pack_names[BU_HWP_PACKS + t];
        if (name[0] && name[0] != '*' && name[0] != '+' && (b = unit_pack_bytes(name, 0)) > 0)
            n += b;
    }
    return n;
}

/* pak_load_sprites: whether the map's pictures fit; if not, the picture
   store (EMS / XMS: also when under XS_EMS would be left after the HWP sets
   still to load and EMS is there (no memory) or XMS on an XT (xs_xt), room for the canvas; or under XS_BELOW on a 286 with XMS, which costs a
   buffer of 8-32 KB for 60-90 KB of walls; SET XPIC=OFF never, XPIC=STORE
   always, for comparing), else the disk cache (not on Hercules: its pictures
   are recoloured once, at the start). pic_plan may have planned the store
   for them (g_pic_note 16). */
int sc_decide(unsigned char *need)
{
    union REGS r;
    unsigned h;
    long fits;
    const char *e;

    r.h.ah = 0x48;
    r.w.bx = 0xFFFF;
    int86(0x21, &r, &r);
    fits = (long)r.w.bx * 16 - (long)mark_needed(need);
    e = getenv("XPIC");
    if (g_herc || (e && !stricmp(e, "OFF"))
        || (!(g_pic_note & 16) && fits >= (ems_ok() || xs_xt() ? XS_EMS + xs_later() : xs_cheap() ? XS_BELOW : 1024)
            && !(e && !stricmp(e, "STORE"))))
        return 0;
    page_mark(need);
    if (xs_open(xs_bytes(need) + xs_walk()) && xs_cap - xs_at >= xs_bytes(need)) {
        xs_need = need;         /* (sc_keep fills it, before pak_load_sprites frees need) */
        return 0;
    }
    if (fits >= 1024 || g_sc_xh) {
        /* All in conventional memory after all: never the disk next to a store. */
        mark_needed(need);
        return 0;
    }
    if (_dos_open(block_path, 0, &h))
        return 1;
    g_sc_fd = (int)h;
    log_printf("disk cache: walls and objects from %s", block_path);
    return 0;
}

/* The disk cache keeps the pack's offsets: 1 if so. The picture store is
   filled here. */
int sc_keep(const unsigned long *off, unsigned long pos, unsigned common, unsigned n)
{
    FILE *f;

    if (g_sc_xh && xs_need && (f = fopen(block_path, "rb")) != NULL) {
        xs_fill(xs_need, f, off, pos, common, n);
        fclose(f);
    }
    xs_need = NULL;
    if (g_sc_fd < 0)
        return 0;
    g_sc_off = off;                     /* (pak_free's) */
    g_sc_pos = pos;
    g_sc_common = common;
    g_sc_n = n;
    return 1;
}

/* pak_free's end: the next battle gets all this one held. */
void sc_stop(void)
{
    if (g_sc_fd >= 0)
        _dos_close(g_sc_fd);
    g_sc_fd = -1;
    if (g_sc_buf)
        _dos_freemem(FP_SEG(g_sc_buf));
    g_sc_buf = NULL;
    xs_free();
    g_alias_sets = 0;
    g_pic_note = 0;
    _fheapshrink();
}

/* What a unit pack's pictures take (load_units; a suit without its hovering
   legs), -1 with no such pack. */
long unit_pack_bytes(const char *path, int suit)
{
    unsigned long at = 0;
    unsigned short n = 0;
    FILE *f = fopen(path, "rb");

    if (!f)
        return -1;
    fseek(f, 8, SEEK_SET);              /* RaceHeader.n_sprites (pak.c) */
    if (fread(&n, 2, 1, f) == 1) {
        if (suit && n > SUIT_FRAMES)
            n = SUIT_FRAMES;
        fseek(f, RACE_HEAD_BYTES + 8 * (long)sizeof(AlienType) + 4L * n, SEEK_SET);
        if (fread(&at, 4, 1, f) != 1)
            at = 0;
    }
    fclose(f);
    return (long)at;
}

/* A unit set without pictures of its own: n frames that are another set's
   (docs/design.md, "Tight battles"). */
const char *unit_set_alias(unsigned char from, int how, unsigned n, unsigned char *set)
{
    unsigned i, base = g_pak.n_sprites, f = g_pak.unit_base[from];
    unsigned char **t;

    if (g_pak.n_unit_sets >= MAX_UNIT_SETS)
        return "too many unit sets";
    t = malloc((size_t)(base + n + 1) * sizeof(unsigned char *));
    if (!t)
        return "out of memory (race)";
    for (i = 0; i < base; i++)
        t[i] = g_pak.sprite[i];
    free(g_pak.sprite);
    g_pak.sprite = t;
    for (i = 0; i < n; i++) {
        unsigned from_i = i;
        if (how == ALIAS_SUIT && i >= SUIT_FRAMES)
            from_i = (i - SUIT_FRAMES) * 5;     /* hovering legs: the standing frames */
        else if (how == ALIAS_HOVER && i >= UNIT_FRAMES + 8) {
            t[base + i] = g_no_rows;            /* the propulsion */
            continue;
        } else if (how == ALIAS_HOVER && i >= UNIT_FRAMES)
            from_i = i - UNIT_FRAMES + 24;      /* the quarter with its turret: the hull's */
        t[base + i] = t[f + from_i];
    }
    g_pak.n_sprites = base + n;
    g_alias_sets |= 1u << g_pak.n_unit_sets;
    g_pak.unit_base[g_pak.n_unit_sets] = base;
    *set = (unsigned char)g_pak.n_unit_sets++;
    return NULL;
}

/* The armours of g_pack_names: their packs first ("+name": into the picture
   store), then those that use another armour's pictures ("*n": armour n's, 0
   the plain soldier's). */
const char *armours_load(void)
{
    int a, pass;
    const char *err = NULL;

    memset(g_armour_set, 0, sizeof g_armour_set);
    for (pass = 0; pass < 2 && !err; pass++)
        for (a = 1; a < N_ARMOURS && !err; a++) {
            const char *name = g_pack_names[2 + a];
            if (!name[0] || (name[0] == '*') != pass)
                continue;
            if (!pass && name[0] == '+') {
                if (!xs_units(name + 1, 1, &g_armour_set[a]))
                    log_printf("armour %d: its pictures in the store", a);
                else if (armour_load(name + 1, a))
                    strcpy(g_pack_names[2 + a], "*0");      /* (pass 1) */
            } else if (!pass)
                err = armour_load(name, a);
            else if (g_pic_note |= 1, log_printf("armour %d: the picture of %d", a, name[1] - '0'),
                     a == ARM_FLYING)
                err = unit_set_alias(g_armour_set[name[1] - '0'], ALIAS_SUIT, SUIT_FRAMES + 8,
                                     &g_armour_set[a]);
            else
                g_armour_set[a] = g_armour_set[name[1] - '0'];
        }
    return err;
}

static char *hwp_name(int t)
{
    return g_pack_names[BU_HWP_PACKS + t];
}

/* The buffer of the disk cache, from what is left after the map's pictures.
   An XMS picture store's: SC_MIN there (what pic_plan kept for it), and
   called again by floor_load after the floor pictures and the hovering legs
   (which keep 608 paragraphs free then, not FOB_KEEP): a bigger one from
   what they left, the first one freed. */
const char *sc_start(void)
{
    union REGS r;
    unsigned seg, paras;

    if (g_sc_fd < 0 && (!g_sc_xh || g_sc_top))
        return NULL;
    r.h.ah = 0x48;
    r.w.bx = 0xFFFF;
    int86(0x21, &r, &r);
    paras = r.w.bx > (unsigned)(PIC_RESERVE / 16) ? r.w.bx - (unsigned)(PIC_RESERVE / 16) : 0;
    if (paras > 0x7D0)                  /* 32 KB */
        paras = 0x7D0;
    if (g_sc_buf) {
        if (g_sc_fd >= 0 || g_sc_head || paras <= g_sc_size / 16 || _dos_allocmem(paras, &seg))
            return NULL;
        _dos_freemem(FP_SEG(g_sc_buf));
        g_sc_buf = (unsigned char *)MK_FP(seg, 0);
        g_sc_size = paras * 16;
        log_printf("picture store: buffer: %u bytes", g_sc_size);
        return NULL;
    }
    if (g_sc_fd < 0 && paras > (unsigned)(SC_MIN / 16)) {
        paras = (unsigned)(SC_MIN / 16);
        r.x.ax = 0x5801;                /* at the top (last fit): no hole when it grows */
        r.x.bx = 2;
        intdos(&r, &r);
    }
    seg = paras < 0x100 || _dos_allocmem(paras, &seg) ? 0 : seg;
    r.x.ax = 0x5801;                    /* first fit again */
    r.x.bx = 0;
    intdos(&r, &r);
    if (!seg)
        return "out of memory (disk cache)";
    g_sc_buf = (unsigned char *)MK_FP(seg, 0);
    g_sc_size = paras * 16;
    g_sc_head = 0;
    if (g_sc_fd >= 0)
        g_pic_note |= 4;
    log_printf("%s: %u bytes", g_sc_fd >= 0 ? "disk cache" : "picture store: buffer", g_sc_size);
    return NULL;
}

static void hwp_pack(char *name, int t)
{
    sprintf(name, "H%d%c.PAK", t + 1, PAK_LETTER);
}

/* The armours worn in this battle, with their packs' names; what they take. */
unsigned long pic_armours(void)
{
    int a, k;
    unsigned long sum = 0;

    for (a = 1; a < N_ARMOURS; a++) {
        char *name = g_pack_names[2 + a];
        int worn = !g_roster && g_quick_armour == a;
        for (k = 0; k < g_roster_n && !worn; k++)
            worn = g_roster[k].alive && g_roster[k].armour == a;
        name[0] = 0;
        if (worn) {
            long n;
            sprintf(name, "A%d%c.PAK", a, PAK_LETTER);
            n = unit_pack_bytes(name, 1);
            if (n > 0)
                sum += n;
        }
    }
    return sum;
}

/* main.c's city check: the conventional memory the map's pictures and the
   armours want, plus 12000 bytes; with a picture store that can take the
   walls, the objects and the armours, only the floors (and an XMS store's
   buffer). */
unsigned long pic_want(unsigned long suits)
{
    unsigned long all = pak_sprite_bytes() + suits + 12000UL, floors, bytes;
    unsigned char *need;

    if (g_herc || (need = malloc(mission_sprites)) == NULL)
        return all;
    mark_needed(need);
    floors = page_mark(need);
    bytes = xs_bytes(need) + suits;
    free(need);
    if (g_sc_xh || !xs_mode(bytes))
        return all;
    floors += 12000UL + (ems_ok() ? 0 : SC_MIN);
    return floors < all ? floors : all;
}

static void to_store(char *name)
{
    memmove(name + 1, name, strlen(name) + 1);
    name[0] = '+';
}

/* Tight battles (docs/design.md): before anything is loaded, which pictures
   fit next to the map's. Every unit comes; what gives way is its own
   picture. With EMS or XMS first the walls and objects go to the picture
   store, then the sets that still do not fit ("+name"). Without, first a
   vehicle takes the first vehicle's picture, then armour takes the plain
   soldier's ("*n": whose; armours_load, hwp_load), and last the walls and
   objects come from the disk. */
const char *pic_plan(unsigned free_para)
{
    long room = (long)free_para * 16 - (long)pak_sprite_bytes() - PIC_RESERVE;
    long size[N_HWP], s, all = PIC_SPARE;
    unsigned char wanted[N_HWP], done[N_ARMOURS];
    unsigned frames = 0, sets = 0;
    int i, t, a, p = -1, first = 0, paged, xs = 0;
    char *name;

    g_pic_note = 0;
    memset(wanted, 0, sizeof wanted);
    memset(size, 0, sizeof size);
    for (i = 0; i < g_hwp_n; i++)
        wanted[g_hwp[i].item - IT_TANK_CANNON] = 1;
    for (t = 0; t < N_HWP; t++) {
        name = hwp_name(t);
        name[0] = 0;
        if (wanted[t]) {
            hwp_pack(name, t);
            size[t] = unit_pack_bytes(name, 0);
            frames += t < 3 ? UNIT_FRAMES : UNIT_FRAMES + 32;
            if (size[t] < 0) {
                name[0] = wanted[t] = 0;
                size[t] = 0;
                g_pic_note = 2;
            } else {
                all += size[t];
                sets++;
                if (p < 0)
                    p = t;          /* (the tanks come first) */
            }
        }
    }
    for (a = 1; a < N_ARMOURS; a++)
        if (g_pack_names[2 + a][0]) {
            frames += UNIT_FRAMES + 16;
            all += unit_pack_bytes(g_pack_names[2 + a], 1);
            sets++;
        }
    /* With EMS or XMS, before any picture is shared: the walls and objects
       into the picture store when the units' own pictures do not fit next
       to them (room for the sets too, should they not fit either). */
    if (!g_herc && room - (long)frames * sizeof(unsigned char *) < all) {
        unsigned char *need = malloc(mission_sprites);
        if (need) {
            unsigned long floors, bytes;
            mark_needed(need);
            floors = page_mark(need);
            bytes = xs_bytes(need) + all + sets * XS_SET + xs_walk();
            if (xs_mode(bytes) && xs_open(bytes)) {
                xs = 1;
                room += (long)pak_sprite_bytes() - (long)floors - (g_sc_top ? 0 : SC_MIN);
                g_pic_note |= 16;
            }
            free(need);
        }
    }
    if (!xs && p >= 3 && size[p] > room) {
        /* Only hovertanks, and no room for their picture: a tank's. */
        char h1[13];
        hwp_pack(h1, 0);
        s = unit_pack_bytes(h1, 0);
        if (s >= 0 && s <= room) {
            strcpy(hwp_name(0), h1);
            size[0] = s;
            frames += UNIT_FRAMES;
            p = 0;
        }
    }
    room -= (long)frames * sizeof(unsigned char *);
    /* Not even with every picture shared: the walls and objects from disk. */
    paged = !xs && !g_herc && room < (p >= 0 ? size[p] : 0);
    if (paged) {
        room = (long)free_para * 16 - (long)pak_sprite_paged() - PIC_RESERVE - SC_MIN
               - (long)frames * sizeof(unsigned char *);
        g_pic_note |= 4;
    }
    /* With a store planned every unit set goes there too: a battle this
       tight wants its conventional memory for the floor pictures, the
       hovering legs and the canvas, and the store's buffer draws them
       about as fast (docs/design.md "The 286"). */
    if (p >= 0) {
        if (xs)
            to_store(hwp_name(p));
        else if (size[p] <= room)
            room -= size[p];
        else {
            for (t = 0; t < N_HWP; t++)
                hwp_name(t)[0] = 0;
            p = -1;
            g_pic_note = 2;
        }
    }
    /* What is left goes to the pictures, less a margin (with the cache: to it). */
    room = paged ? -1 : room - PIC_SPARE;
    /* The armour most soldiers wear first. */
    memset(done, 0, sizeof done);
    for (;;) {
        int best = 0, most = 0, k, n;
        for (a = 1; a < N_ARMOURS; a++) {
            if (!g_pack_names[2 + a][0] || done[a])
                continue;
            for (n = !g_roster, k = 0; k < g_roster_n; k++)
                n += g_roster[k].alive && g_roster[k].armour == a;
            if (n > most) {
                most = n;
                best = a;
            }
        }
        if (!best)
            break;
        done[best] = 1;
        name = g_pack_names[2 + best];
        s = unit_pack_bytes(name, 1);
        if (xs)
            to_store(name);     /* (the Flying Suit with its hovering legs: xs_units) */
        else if (s > room) {
            name[0] = '*';
            name[1] = (char)('0' + first);
            name[2] = 0;
            g_pic_note |= 1;
        } else {
            room -= s;
            if (!first)
                first = best;
        }
    }
    /* The other vehicles: theirs, else the same kind's, else the first's. */
    first = p >= 3 ? p : -1;
    for (t = 0; t < N_HWP; t++) {
        if (!wanted[t] || t == p)
            continue;
        name = hwp_name(t);
        if (p >= 0 && xs)
            to_store(name);
        else if (p >= 0 && size[t] > room) {
            name[0] = '*';
            name[1] = (char)('0' + (t >= 3 && first >= 0 ? first : p));
            name[2] = 0;
            g_pic_note |= 1;
        } else if (p >= 0) {
            room -= size[t];
            if (t >= 3 && first < 0)
                first = t;
        }
    }
    if (p == 0 && !wanted[0]) {
        /* The tank's picture alone for the hovertanks: theirs are all "*0". */
        for (t = 3; t < N_HWP; t++)
            if (wanted[t]) {
                name = hwp_name(t);
                name[0] = '*';
                name[1] = '0';
                name[2] = 0;
            }
        g_pic_note |= 1;
    }
    /* With room to spare the HWPs load after the map, into what its blocks left. */
    g_pic_early = (unsigned char)(paged || xs || room + PIC_SPARE < 8192);
    return armours_load();
}
