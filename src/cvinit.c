/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The canvas's start and end (canvas.h cv_battle; docs/design.md "The
   canvas"): the memory taken when there is enough of it, given back after
   the battle or at exit. An overlay section of its own, run twice a
   battle, so that canvas.c's section fits a small UMB overlay area. */

#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "battle.h"
#include "canvas.h"
#include "effects.h"
#include "gfx.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "path.h"
#include "xmemlend.h"
#include "render.h"
#include "sound.h"
#include "timer.h"
#include "unit.h"
#include "hwp.h"
#include "vgageo.h"
#include "cvpriv.h"

static unsigned dos_alloc(unsigned paras)
{
    union REGS r;

    r.h.ah = 0x48;
    r.x.bx = paras;
    intdos(&r, &r);
    return r.x.cflag ? 0 : r.x.ax;
}

static unsigned dos_largest(void)
{
    union REGS r;

    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;
    intdos(&r, &r);
    return r.x.bx;
}

static void dos_free(unsigned seg)
{
    union REGS r;
    struct SREGS s;

    segread(&s);
    s.es = seg;
    r.h.ah = 0x49;
    intdosx(&r, &r, &s);
}

static int ems(int ah, unsigned bx, unsigned dx, unsigned *out)
{
    union REGS r;

    r.h.ah = (unsigned char)ah;
    r.x.bx = bx;
    r.x.dx = dx;
    int86(0x67, &r, &r);
    if (out)
        *out = ah == 0x43 ? r.x.dx : r.x.bx;
    return r.h.ah;
}

static int ems_there(void)
{
    unsigned seg = *(unsigned far *)MK_FP(0, 0x67 * 4 + 2);
    const char far *dev = (const char far *)MK_FP(seg, 10);

    return seg && !_fmemcmp(dev, "EMMXXXX0", 8) && !ems(0x40, 0, 0, NULL);
}

unsigned g_cv_low;              /* startup.c: paragraphs of the canvas's area in conventional memory (a 286) */

/* The UMB plan's overlay area (startup.c umb_plan: after the sound's part,
   below the tables or the map buffer), in paragraphs; 0 without one. */
static unsigned umb_ovl(void)
{
    unsigned base = umb_seg + umb_paras, end = FP_SEG(g_unit_at) > base ? FP_SEG(g_unit_at)
                                                                     : FP_SEG(g_map);
    return umb_game && umb_seg && FP_SEG(g_map) > base ? end - base : 0;
}

/* The night light (nlit.c): on in a dark battle where its code has a UMB
   overlay area, a few paragraphs here. */
static void lit_battle(int on)
{
    static unsigned char told;
    const char *e = getenv("XLIT"), *why = NULL;

    if (g_lit) {
        Lit far *L = LITD;
        log_printf("night light: %lu times made, %lu changes, %lu cells shown again",
                   L->n_make, L->n_change, L->n_cells);
        dos_free(FP_SEG(g_lit));
        g_lit = NULL;
    }
    if (!on || !g_night)
        return;
    if (e && !stricmp(e, "OFF"))
        why = "XLIT=OFF";
    else if (g_video != VIDEO_EGA)
        why = "EGA only";
    else if (umb_ovl() < LIT_NEED)
        why = "no overlay area in a UMB for it";
    else {
        unsigned need = (sizeof(Lit) + 15) >> 4, seg;
        if (dos_largest() < need + KEEP_FREE / 4 || !(seg = dos_alloc(need)))
            why = "no memory";
        else {
            _fmemset(MK_FP(seg, 0), 0, need * 16);
            g_lit = MK_FP(seg, 0);
            LITD->sum = 1;      /* (not what lit_sum says: made at the first present) */
            if (!told)
                log_printf("night light: on");
        }
    }
    if (why && !told)
        log_printf("night light: off (%s)", why);
    told = 1;
}

/* atexit (global: the linker gives it an overlay vector) */
void cv_exit(void)
{
    cv_battle(0);
}

void cv_battle(int on)
{
    unsigned slots = 0, pages = 0, kb = 0, need, hn, hbits, seg, free, h, i, lend = 0;
    unsigned vga = g_vga && g_video != VIDEO_EGA, bsz = vga ? VBLK_SZ : BLK_SZ, per_pg = vga ? VPER_PG : PER_PG;
    unsigned r[5];
    const char *why = NULL, *e = getenv("XCV");
    unsigned char far *p;
    static unsigned char exit_set, told;  /* told: "off" logged once a run (a loaded battle's LOG matches the saved one's) */

    if (!on) {
        lit_battle(0);
        if (!cvd)
            return;
        log_printf("canvas: %lu reads: %lu copied, %lu in part, %lu drawn; %lu kept, "
                   "%lu drawn ahead, %lu taken back; checks %lu ms%s",
                   cvd->n_read, cvd->n_hit, cvd->n_part, cvd->n_miss, cvd->n_store, cvd->n_fill,
                   cvd->n_evict, timer_ms(cvd->t_sync), cvd->n_bad ? ", ERRORS" : "");
        if (cvd->n_bad)
            log_printf("canvas: %lu errors", cvd->n_bad);
        if (cvd->mode == MODE_EMS)
            ems(0x45, 0, cvd->handle, NULL);
        else if (cvd->lent)
            xl_back(cvd->lent, cvd->lent_kb);   /* (before the picture store's: sc_stop comes later) */
        else {
            r[0] = 0x0A00;
            r[2] = cvd->handle;
            r[1] = r[3] = r[4] = 0;
            xms_call((unsigned far *)r);
        }
        seg = cvd->seg;
        cvd = NULL;
        g_cv = 0;
        dos_free(seg);
        return;
    }
    g_cv = 0;
    if (cvd)
        cv_battle(0);
    lit_battle(1);
    if (e && !stricmp(e, "OFF"))
        why = "XCV=OFF";
    else if (g_video != VIDEO_EGA && !vga)
        why = "EGA or VGA only";
    else if (umb_ovl() < UMB_NEED && g_cv_low < UMB_NEED)
        why = "no overlay area in a UMB for it";
    else if (ems_there() && !ems(0x41, 0, 0, &seg) && !ems(0x42, 0, 0, &pages) && pages >= 4) {
        /* EMS: blocks at physical page 3, the save area on its own page */
        if (pages > MAX_SLOT / per_pg + 2)
            pages = MAX_SLOT / per_pg + 2;
        slots = (pages - 1) * per_pg;
        if (slots > MAX_SLOT)
            slots = MAX_SLOT;
        pages = (slots + per_pg - 1) / per_pg + 1;
        kb = MODE_EMS;
    } else if (xms_find()) {
        r[0] = 0x0800;
        r[1] = r[2] = r[3] = r[4] = 0;
        xms_call((unsigned far *)r);
        if (r[0] < 64 && (lend = xl_kb()) >= 64)
            r[0] = lend;        /* the read cache lends it (the 286's XMS budget) */
        else
            lend = 0;
        if (r[0] >= 64) {
            slots = (unsigned)(((unsigned long)r[0] * 1024 - (vga ? VSAVE_SZ : SAVE_SZ)) / bsz);
            if (slots > MAX_SLOT)
                slots = MAX_SLOT;
            kb = MODE_XMS;
        }
    }
    if (!why && !kb)
        why = "no EMS or XMS";
    else if (!why && slots < MIN_SLOT)
        why = "too little EMS or XMS for a view and its edges";
    if (why) {
        if (!told)
            log_printf("canvas: off (%s)", why);
        told = 1;
        return;
    }
    for (hbits = 4; (1u << hbits) < 2 * slots; hbits++)
        ;
    hn = 1u << hbits;
    need = sizeof(Cv) + hn * 2 + slots * 2 + slots * 3 + slots + MAX_UNITS * sizeof(USh)
         + (FLOOR_TOP + FX_DEVS_TOP + FX_MAX_TILES) * sizeof(TSh) + FX_MAX_TILES * 2
         + (MAP_MAX_TILES + 7) / 8 + (kb == MODE_XMS ? bsz : 0);
    need = (need + 15) >> 4;
    free = dos_largest();
    if (free < need + KEEP_FREE || (seg = dos_alloc(need)) == 0 || seg >= 0xA000) {
        if (free >= need + KEEP_FREE && seg)
            dos_free(seg);
        log_printf("canvas: off (needs %u KB of conventional memory, %u KB free)",
                   (need + KEEP_FREE + 63) / 64, free / 64);
        return;
    }
    p = (unsigned char far *)MK_FP(seg, 0);
    _fmemset(p, 0, need * 16);
    cvd = (Cv far *)p;
    cvd->seg = seg;
    cvd->mode = kb;
    cvd->slots = slots;
    cvd->hbits = hbits;
    cvd->bsz = bsz;
    cvd->per_pg = per_pg;
    if (vga) {
        cvd->vga = CV_8BPP;
        cvd->bw = 320;
    } else {
        cvd->bw = BUF_W;
        cvd->psh = 3;
    }
    p += sizeof(Cv);
    cvd->hash = (unsigned short far *)p;  p += hn * 2;
    cvd->key = (unsigned short far *)p;   p += slots * 2;
    cvd->mask = p;                        p += slots * 3;
    cvd->ref = p;                         p += slots;
    cvd->us = (USh far *)p;               p += MAX_UNITS * sizeof(USh);
    cvd->fl = (TSh far *)p;               p += FLOOR_TOP * sizeof(TSh);
    cvd->dv = (TSh far *)p;               p += FX_DEVS_TOP * sizeof(TSh);
    cvd->fx = (TSh far *)p;               p += FX_MAX_TILES * sizeof(TSh);
    cvd->keys = (unsigned far *)p;        p += FX_MAX_TILES * 2;
    cvd->seen = p;                        p += (MAP_MAX_TILES + 7) / 8;
    cvd->bounce = p;
    if (kb == MODE_EMS) {
        if (ems(0x43, pages, 0, &h)) {
            dos_free(seg);
            cvd = NULL;
            log_printf("canvas: off (EMS refused %u pages)", pages);
            return;
        }
        cvd->handle = h;
        cvd->frame = seg = 0;
        ems(0x41, 0, 0, &cvd->frame);
        cvd->frame += 0xC00;         /* physical page 3 */
        cvd->save_pg = pages - 1;
        log_printf("canvas: EMS, %u pages: %u blocks (%u KB), frame page at %04X, %u KB here",
                   pages, slots, (unsigned)((unsigned long)slots * bsz / 1024), cvd->frame, (need + 63) / 64);
    } else {
        kb = (unsigned)(((unsigned long)slots * bsz + (vga ? VSAVE_SZ : SAVE_SZ) + 1023) / 1024);
        r[0] = 0x0900;
        r[2] = kb;
        r[1] = r[3] = r[4] = 0;
        if (lend) {
            r[0] = 1;
            if ((cvd->lent = xl_lend(kb, &r[2])) == 0)
                r[0] = 0;
            cvd->lent_kb = kb;
            cvd->xbase = (unsigned long)(cvd->lent - (kb + 3) / 4) << 12;
        } else
            xms_call((unsigned far *)r);
        if (r[0] != 1) {
            dos_free(seg);
            cvd = NULL;
            log_printf("canvas: off (XMS refused %u KB)", kb);
            return;
        }
        cvd->handle = r[2];
        log_printf("canvas: XMS, %u KB%s: %u blocks, %u KB here", kb, lend ? " lent by the read cache" : "",
                   slots, (need + 63) / 64);
    }
    if (!exit_set) {
        exit_set = 1;
        atexit(cv_exit);
    }
    cvd->check = e && !stricmp(e, "CHECK");
    cvd->page = 0xFFFF;
    cvd->idle_gen = 0xFFFF;
    /* the shadows from the battle as it is */
    for (i = 0; i < FX_DEVS_TOP + FLOOR_TOP; i++)
        cvd->fl[i].f = NO_ITEM;
    cvd->fob = g_fob;
    cvd->seenp = g_seen;
    if (g_seen)
        for (i = 0; i < (unsigned)g_mx * g_my * g_mz; i++)
            if (g_seen[i] & SEEN_BIT)
                cvd->seen[i >> 3] |= (unsigned char)(1 << (i & 7));
    g_cv = 1;
}

/* The player is told in the battle's title line (bplayer.c). Here, not in
   hwp.c: main.c calls it right after cv_battle(1), so this section is still
   loaded. */
void hwp_notice(void)
{
    if (g_vgx) {                /* VGA: the battle drawn in its own colours from */
        mouse_hide();           /* the first frame, PAL set before gfx_night */
        vgx_battle();
    }
    if (g_pic_note & 7) {
        strcpy(g_pak.title, g_pic_note & 2 ? "Low memory: tank aboard"
                            : g_pic_note & 4 ? "Low memory: slow redraw" : "Low memory: units alike");
        log_printf("%s", g_pak.title);
    }
}
