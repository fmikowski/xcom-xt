/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Extended memory and the read cache (docs/design.md, "Extended memory and
   the read cache"): finds an EMS driver (LIM 3.2 / 4.0, preferred: a page
   is mapped in microseconds) or an XMS driver (copies: XMS 2.0 function
   0Bh), takes up to 2 MB, and sets up src/xmemres.asm's block, which keeps
   4 KB blocks of the packs and of the program's overlay sections there.
   Part of the startup overlay section: runs once, nothing stays resident,
   no static data (DGROUP is resident). Without a driver, or with NOXMEM,
   nothing changes. */

#include <dos.h>
#include <stdio.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "music.h"
#include "sound.h"
#include "xmem.h"

void __far *__cdecl xmem_blob(void);            /* xmemres.asm */
void __far *__cdecl xmem_stub(void);
void __far *__cdecl xmem_hblob(void);           /* xmemhma.asm: the same for the HMA */
void __far *__cdecl xmem_hstub(void);
unsigned long __cdecl xms_find(void);           /* sndasm.asm */
void __cdecl xms_call(unsigned __far *r);

int cpu_below_286(void);
#pragma aux cpu_below_286 = "push sp" "pop ax" "sub ax, sp" value [ax] modify exact [ax];

#define MAX_KB      1024u       /* what the cache takes at most: half of what is free, at most
                                   1 MB, so the battle view's canvas (canvas.c) finds its 768 KB */
#define SB_KB       64u         /* XMS left for sound.c's effects (BLASTER) */
#define META_PARAS  225u        /* the block table: 3600 bytes (xmemres.asm M_SIZE) */
#define BOUNCE_PARAS 257u       /* XMS: one 4 KB block + the move's struct */
/* The HMA build of xmemres.asm (xmemhma.asm): code at FFFF:HMA_ORG, the
   table at HMA_META (256 slots), the bounce buffer and the move's struct
   from HMA_BNC to the HMA's last byte. */
#define HMA_ORG     0xDA00u     /* HORG */
#define HMA_META    0xE7E0u     /* M_BASE = BO - (16 + 512 + 6 * 256) */
#define HMA_BNC     0xEFF0u     /* BO = 10000h - 4096 - 16 */
#define HMA_SLOTS   256u
#define HMA_META_SZ (16u + 512u + 6u * HMA_SLOTS)
#define HMA_LOW     64u         /* paragraphs: the low buffer DOS reads into (LOWBUF) */

static int ems_call(union REGS *r)
{
    int86(0x67, r, r);
    return r->h.ah;
}

static unsigned xms(unsigned ax, unsigned dx, unsigned *bx)
{
    unsigned r[5];

    r[0] = ax;
    r[1] = 0;
    r[2] = dx;
    r[3] = 0;
    r[4] = 0;
    xms_call((unsigned __far *)r);
    if (bx)
        *bx = r[1];
    return r[0] == 1 ? r[2] : 0;     /* DX: a handle */
}

/* A DOS block of paras paragraphs; high: in upper memory if DOS 5 has
   some (the UMB plan has taken its part by now), else conventional. */
static unsigned dos_block(unsigned paras, int high)
{
    union REGS r;
    unsigned strat = 0, link = 0, seg = 0;
    int linked = 0;

    if (high) {
        r.x.ax = 0x3000;
        intdos(&r, &r);
        if (r.h.al >= 5) {
            r.x.ax = 0x5800;
            intdos(&r, &r);
            strat = r.x.ax;
            r.x.ax = 0x5802;
            intdos(&r, &r);
            link = r.h.al;
            r.x.ax = 0x5803;
            r.x.bx = 1;
            intdos(&r, &r);
            if (!r.x.cflag) {
                linked = 1;
                r.x.ax = 0x5801;
                r.x.bx = 0x80;          /* first fit, high memory first */
                intdos(&r, &r);
            }
        }
    }
    r.h.ah = 0x48;
    r.x.bx = paras;
    intdos(&r, &r);
    if (!r.x.cflag)
        seg = r.x.ax;
    if (linked) {
        r.x.ax = 0x5801;
        r.x.bx = strat;
        intdos(&r, &r);
        r.x.ax = 0x5803;
        r.x.bx = link;
        intdos(&r, &r);
    }
    return seg;
}

static int has_opt(int argc, char **argv, const char *name)
{
    int i;

    for (i = 1; i < argc; i++)
        if (!stricmp(argv[i], name))
            return 1;
    return 0;
}

/* The overlay manager opened the program before the cache existed and keeps
   that handle: find it among the open handles (the program's size) and
   register it, so the overlay sections are cached too. */
static int find_exe(const char *path)
{
    struct find_t ff;
    union REGS r;
    struct SREGS s;
    unsigned char __far *jft = *(unsigned char __far * __far *)MK_FP(_psp, 0x34);
    unsigned n = *(unsigned __far *)MK_FP(_psp, 0x32), h;

    if (_dos_findfirst(path, _A_NORMAL, &ff))
        return -1;
    for (h = 5; h < n && h < 32; h++) {
        unsigned long pos, size;
        if (jft[h] == 0xFF)
            continue;
        r.x.ax = 0x4201;
        r.x.bx = h;
        r.x.cx = r.x.dx = 0;
        intdos(&r, &r);
        if (r.x.cflag)
            continue;
        pos = ((unsigned long)r.x.dx << 16) | r.x.ax;
        r.x.ax = 0x4202;
        r.x.bx = h;
        r.x.cx = r.x.dx = 0;
        intdos(&r, &r);
        size = ((unsigned long)r.x.dx << 16) | r.x.ax;
        r.x.ax = 0x4200;
        r.x.bx = h;
        r.x.cx = (unsigned)(pos >> 16);
        r.x.dx = (unsigned)pos;
        intdos(&r, &r);
        if (size != ff.size)
            continue;
        segread(&s);
        r.x.ax = 0xD701;
        r.x.bx = h;
        r.x.si = XMEM_MAGIC;
        r.x.dx = FP_OFF((char __far *)path);
        s.ds = FP_SEG((char __far *)path);
        intdosx(&r, &r, &s);
        return (int)h;
    }
    return -1;
}

/* An EMS driver by its device name at offset 10 of INT 67h's segment,
   ready and with 4 free pages or more (it takes half). */
static int ems_there(void)
{
    union REGS r;
    unsigned seg = *(unsigned __far *)MK_FP(0, 0x67 * 4 + 2), i;
    char __far *dev = (char __far *)MK_FP(seg, 10);

    if (!seg)
        return 0;
    for (i = 0; i < 8 && dev[i] == "EMMXXXX0"[i]; i++)
        ;
    if (i < 8)
        return 0;
    r.h.ah = 0x40;
    if (ems_call(&r))
        return 0;
    r.h.ah = 0x42;
    return !ems_call(&r) && r.x.bx >= 4;
}

unsigned xmem_need(int argc, char **argv)
{
    unsigned rr[5];

    if (has_opt(argc, argv, "NOXMEM") || (!has_opt(argc, argv, "NOEMS") && ems_there())
        || !xms_find())
        return 0;
    rr[0] = 0x0800;             /* AX = the largest free block in KB */
    rr[1] = rr[2] = rr[3] = rr[4] = 0;
    xms_call((unsigned __far *)rr);
    if (rr[0] < 32)
        return 0;
    return ((((const XmemHdr __far *)xmem_blob())->len + 15) >> 4) + META_PARAS + BOUNCE_PARAS;
}

/* XMS: may the 4 KB buffer (and the move's struct behind it) lie at
   seg:off, a UMB or the HMA? 4 KB there go to the handle and back, and must
   come back the same: EXMS86 `!!` maps its own pages over part of the EMS
   frame while it copies, and its UMB is that frame. */
static int bounce_ok(unsigned seg, unsigned off, unsigned handle, unsigned long xent)
{
    unsigned char __far *p = (unsigned char __far *)MK_FP(seg, off);
    unsigned __far *mv = (unsigned __far *)MK_FP(seg, off + 0x1000);
    unsigned r[5], i;

    if (!xent)
        return 0;
    for (i = 0; i < 0x1000; i++)
        p[i] = (unsigned char)(i * 7 + 1);
    mv[0] = 0x1000;             /* length */
    mv[1] = 0;
    mv[2] = 0;                  /* from seg:off */
    mv[3] = off;
    mv[4] = seg;
    mv[5] = handle;             /* to offset 0 of the handle */
    mv[6] = 0;
    mv[7] = 0;
    r[0] = 0x0B00;
    r[3] = off + 0x1000;
    r[4] = seg;
    xms_call((unsigned __far *)r);
    if (r[0] != 1)
        return 0;
    _fmemset(p, 0, 0x1000);
    mv[0] = 0x1000;
    mv[1] = 0;
    mv[2] = handle;             /* back */
    mv[3] = 0;
    mv[4] = 0;
    mv[5] = 0;
    mv[6] = off;
    mv[7] = seg;
    r[0] = 0x0B00;
    r[3] = off + 0x1000;
    r[4] = seg;
    xms_call((unsigned __far *)r);
    if (r[0] != 1)
        return 0;
    for (i = 0; i < 0x1000; i++)
        if (p[i] != (unsigned char)(i * 7 + 1))
            return 0;
    return 1;
}

/* The high memory area for the XMS cache (docs/design.md "The HMA"), on a
   286 or better without upper memory: the part DOS=HIGH left free (INT 2Fh
   AX=4A01h), borrowed for the run and not allocated (MS-DOS 5 and 6 never
   take an HMA allocation back), when it reaches down to HMA_ORG; else the
   whole HMA from the XMS driver (function 01h, A20 on with 03h), when DOS
   is not in it. Returns 0 (none; *why set when there was a reason), 1
   (DOS's free part) or 2 (ours: A20 off and the HMA released at exit);
   *from / *bytes: what was free. Never asked on an 8088 or V20. */
static int hma_take(const char **why, unsigned *from, unsigned *bytes)
{
    union REGS r;
    struct SREGS s;
    unsigned x[5];

    if (cpu_below_286())
        return 0;
    segread(&s);
    s.es = 0;
    r.x.ax = 0x4A01;
    r.x.bx = 0;
    r.x.di = 0xFFFF;
    int86x(0x2F, &r, &r, &s);
    x[1] = x[2] = x[3] = x[4] = 0;
    if (r.x.bx) {                       /* DOS is in the HMA */
        *from = r.x.di;
        *bytes = r.x.bx;
        if (s.es != 0xFFFF || r.x.di > HMA_ORG || r.x.di + (unsigned long)r.x.bx < 0x10000UL) {
            *why = "too little free HMA";
            return 0;
        }
        x[0] = 0x0700;                  /* A20 on? (DOS needs it on) */
        xms_call((unsigned __far *)x);
        if (x[0] != 1) {
            *why = "A20 off";
            return 0;
        }
        return 1;
    }
    x[0] = 0x0100;                      /* request the HMA */
    x[2] = 0xFFFF;
    xms_call((unsigned __far *)x);
    if (x[0] != 1) {
        *why = "the HMA is in use";
        return 0;
    }
    x[0] = 0x0300;                      /* global enable A20 */
    xms_call((unsigned __far *)x);
    if (x[0] != 1) {
        x[0] = 0x0200;
        xms_call((unsigned __far *)x);
        *why = "A20 refused";
        return 0;
    }
    *from = 0x10;
    *bytes = 0xFFF0;
    return 2;
}

/* The HMA given back (hma_take returned 2) when the cache cannot use it. */
static void hma_drop(int hma)
{
    unsigned x[5];

    if (hma != 2)
        return;
    x[1] = x[2] = x[3] = x[4] = 0;
    x[0] = 0x0400;
    xms_call((unsigned __far *)x);
    x[0] = 0x0200;
    xms_call((unsigned __far *)x);
}

/* What the cache leaves of the HMA below it (FFFF:lo to HMA_ORG; A20 stays
   on while the game runs) holds the battle track for good when it fits
   (sound.c start_music: no conventional memory for it in battle; u286:
   4997 bytes of 5632). Read through the stack: DOS never reads into the
   HMA (a disk BIOS may normalise FFFF:xxxx), nor does the cache (it
   normalises the caller's pointer). */
static void hma_music(unsigned lo)
{
    unsigned char h[6 + 6 * N_MUS], b[256];
    unsigned long off;
    unsigned len, k = 0, n;
    FILE *f = fopen("MUSIC.PAK", "rb");

    if (!f)
        return;
    if (fread(h, 1, sizeof h, f) == sizeof h && !memcmp(h, "XMU1", 4)) {
        off = *(unsigned long *)(h + 6 + 6 * MUS_TACTIC);
        len = *(unsigned *)(h + 10 + 6 * MUS_TACTIC);
        if (len && len <= HMA_ORG - lo && !fseek(f, (long)off, SEEK_SET))
            for (; k < len; k += n) {
                n = len - k < sizeof b ? len - k : sizeof b;
                if (fread(b, 1, n, f) != n)
                    break;
                _fmemcpy(MK_FP(0xFFFF, lo + k), b, n);
            }
        if (len && k >= len) {
            g_hma_track = MUS_TACTIC;
            g_hma_lo = lo;
            log_printf("XMEM: the battle track (%u bytes) in the HMA at FFFF:%04X", len, lo);
        }
    }
    fclose(f);
}

void xmem_start(int argc, char **argv, unsigned umb)
{
    union REGS r;
    XmemHdr __far *hd;
    const XmemHdr __far *blob;
    const XmemStub __far *stub;
    unsigned mode = 0, handle = 0, frame = 0, pages = 0, kb = 0, nslot;
    unsigned seg, bounce = 0, paras, i, code;
    unsigned long xent = 0;
    const char *why = NULL;
    char __far *dev;
    const char *hwhy = NULL;
    int hma = 0;
    unsigned hfrom = 0, hbytes = 0, key = 0;

    if (has_opt(argc, argv, "NOXMEM")) {
        log_printf("XMEM: NOXMEM (no read cache)");
        return;
    }
    /* EMS: the driver's device name at offset 10 of INT 67h's segment. */
    seg = *(unsigned __far *)MK_FP(0, 0x67 * 4 + 2);
    dev = (char __far *)MK_FP(seg, 10);
    if (seg && !has_opt(argc, argv, "NOEMS")) {
        for (i = 0; i < 8 && dev[i] == "EMMXXXX0"[i]; i++)
            ;
        if (i == 8) {
            r.h.ah = 0x40;
            if (ems_call(&r))
                why = "EMS driver not ready";
            else {
                r.h.ah = 0x46;
                if (ems_call(&r) || r.h.al < 0x32)
                    why = "EMS older than 3.2";
                else {
                    r.h.ah = 0x41;
                    ems_call(&r);
                    frame = r.x.bx;
                    r.h.ah = 0x42;
                    if (ems_call(&r) || r.x.bx < 4)
                        why = "no free EMS pages";
                    else {
                        pages = r.x.bx / 2 < MAX_KB / 16 + 1 ? r.x.bx / 2 : MAX_KB / 16 + 1;
                        r.h.ah = 0x43;
                        r.x.bx = pages;
                        if (ems_call(&r))
                            why = "EMS allocation failed";
                        else {
                            handle = r.x.dx;
                            mode = XM_EMS;
                        }
                    }
                }
            }
        }
    }
    if (!mode && (xent = xms_find()) != 0) {
        unsigned largest, err, rr[5];
        rr[0] = 0x0800;         /* AX = the largest free block in KB */
        rr[1] = rr[2] = rr[3] = rr[4] = 0;
        xms_call((unsigned __far *)rr);
        largest = rr[0];
        if (getenv("BLASTER"))
            largest = largest > SB_KB ? largest - SB_KB : 0;
        /* Half for the battle's canvas and pictures; on a 286 without
           upper memory (the HMA's cache) all: the picture store borrows
           what a battle needs from it (picplan.c, "The XMS budget"). */
        kb = !umb && !cpu_below_286() ? largest : largest / 2;
        kb = (kb < MAX_KB ? kb : MAX_KB) & ~3u;
        for (i = 1; i + 1 < (unsigned)argc && stricmp(argv[i], "XMEMKB"); i++)
            ;
        if (i + 1 < (unsigned)argc)
            kb = (unsigned)atoi(argv[i + 1]) < largest ? (unsigned)atoi(argv[i + 1]) & ~3u : kb;
        if (kb < 16)
            why = why ? why : "no free XMS";
        else if ((handle = xms(0x0900, kb, &err)) == 0)
            why = "XMS allocation failed";
        else
            mode = XM_XMS;
    }
    if (!mode) {
        log_printf("XMEM: no read cache (%s)", why ? why : "no EMS or XMS driver");
        return;
    }
    why = NULL;

    blob = (const XmemHdr __far *)xmem_blob();
    paras = (blob->len + 15) >> 4;
    nslot = mode == XM_EMS ? (pages - 1) * 4 : kb / 4;
    if (nslot > 512)
        nslot = 512;
    if (mode == XM_EMS) {
        /* The table and the code in logical page 0 (physical page 0 of
           the frame; the code at 1000h), a stub in conventional memory. */
        stub = (const XmemStub __far *)xmem_stub();
        paras = (stub->len + 15) >> 4;
        seg = dos_block(paras, 1);
        r.x.ax = 0x4400;
        r.x.bx = 0;
        r.x.dx = handle;
        if (!seg || ems_call(&r) || blob->len > 0x3000) {
            if (seg)
                _dos_freemem(seg);
            r.h.ah = 0x45;
            r.x.dx = handle;
            ems_call(&r);
            seg = 0;
        }
    } else if (!umb && (hma = hma_take(&hwhy, &hfrom, &hbytes)) != 0) {
        /* No upper memory on a 286: the code, the table and the buffer in
           the HMA (the HMA build of the block), a stub in conventional
           memory that checks A20 and the HMA's contents first. */
        blob = (const XmemHdr __far *)xmem_hblob();
        stub = (const XmemStub __far *)xmem_hstub();
        paras = (((const XmemHStub __far *)stub)->len + 15) >> 4;
        if (nslot > HMA_SLOTS)
            nslot = HMA_SLOTS;
        seg = 0;
        if (blob->len > HMA_META - HMA_ORG)
            hwhy = "its code is too big";
        else if (!bounce_ok(0xFFFF, HMA_BNC, handle, xent))
            hwhy = "XMS moves to it failed";
        else if ((seg = dos_block(paras + HMA_LOW, 0)) == 0)
            hwhy = "no memory for the stub";
        if (!seg) {
            hma_drop(hma);
            xms(0x0A00, handle, NULL);
        } else
            bounce = 0xFFFF;
    } else {
        /* The code and the table, then the 4 KB buffer: in the UMB the plan
           set aside when moves there come back whole, else low. Never the
           code and table in conventional memory: their 7 KB (11 with the
           buffer) are more than the tightest battles have to spare. */
        seg = umb ? umb : dos_block(paras + META_PARAS, 1);
        if (seg && seg < 0xA000u) {
            _dos_freemem(seg);
            seg = 0;
            why = "no upper memory";
        }
        if (seg && umb && bounce_ok(umb + paras + META_PARAS, 0, handle, xent))
            bounce = umb + paras + META_PARAS;
        else if (seg && (bounce = dos_block(BOUNCE_PARAS, 0)) == 0) {
            if (!umb)
                _dos_freemem(seg);
            seg = 0;
        }
        if (!seg)
            xms(0x0A00, handle, NULL);
    }
    if (!seg) {
        if (hwhy)
            log_printf("XMEM: no read cache (%s for its %u paragraphs; HMA: %s, %u bytes from %04X)",
                       why ? why : "no memory", paras, hwhy, hbytes, hfrom);
        else
            log_printf("XMEM: no read cache (%s for its %u paragraphs)",
                       why ? why : "no memory", paras);
        return;
    }
    code = mode == XM_EMS ? frame + 0x100 : hma ? 0xFFFF : seg;
    i = hma ? HMA_ORG : 0;
    _fmemcpy(MK_FP(code, i), blob, blob->len);
    hd = (XmemHdr __far *)MK_FP(code, i);
    hd->mode = (unsigned char)mode;
    hd->handle = handle;
    hd->frame = hma ? seg + paras : frame;      /* HMA: the low buffer, after the stub */
    hd->xms = (void __far *)xent;
    hd->bounce = bounce;
    hd->nslot = nslot;
    if (hma) {
        unsigned __far *sig = (unsigned __far *)MK_FP(0xFFFF, HMA_META);
        hd->meta = 0xFFFF;
        _fmemset(sig, 0, HMA_META_SZ);
        key = *(unsigned __far *)MK_FP(0x40, 0x6C) | 1;     /* the BIOS clock */
        sig[0] = 0x4D58;        /* "XM" */
        sig[1] = 0x314D;        /* "M1" */
        sig[2] = handle;
        sig[3] = key;
    } else {
        hd->meta = mode == XM_EMS ? frame : seg + paras;
        _fmemset(MK_FP(hd->meta, 0), 0, META_PARAS * 16);
    }
    if (mode == XM_EMS) {
        unsigned __far *sig = (unsigned __far *)MK_FP(frame, 0);
        sig[0] = 0x4D58;        /* "XM" */
        sig[1] = 0x314D;        /* "M1" */
        sig[2] = handle;
    }
    /* INT 21h and the terminate address (every exit frees the memory). */
    _disable();
    hd->old21 = *(void __far * __far *)MK_FP(0, 0x21 * 4);
    hd->old08 = *(void __far * __far *)MK_FP(0, 0x08 * 4);
    hd->old22 = *(void __far * __far *)MK_FP(_psp, 0x0A);
    if (mode == XM_EMS) {
        XmemStub __far *st = (XmemStub __far *)MK_FP(seg, 0);
        _fmemcpy(st, stub, stub->len);
        st->old21 = hd->old21;
        st->old22 = hd->old22;
        st->old08 = hd->old08;
        st->main = MK_FP(code, hd->e21);
        st->frame = frame;
        st->handle = handle;
        *(void __far * __far *)MK_FP(0, 0x21 * 4) = MK_FP(seg, st->e21);
        *(void __far * __far *)MK_FP(_psp, 0x0A) = MK_FP(seg, st->e22);
    } else if (hma) {
        XmemHStub __far *st = (XmemHStub __far *)MK_FP(seg, 0);
        _fmemcpy(st, stub, ((const XmemHStub __far *)stub)->len);
        st->old21 = hd->old21;
        st->old22 = hd->old22;
        st->old08 = hd->old08;
        st->main = MK_FP(code, hd->e21);
        st->xms = (void __far *)xent;
        st->handle = handle;
        st->key = key;
        st->own = hma == 2;
        *(void __far * __far *)MK_FP(0, 0x21 * 4) = MK_FP(seg, st->e21);
        *(void __far * __far *)MK_FP(_psp, 0x0A) = MK_FP(seg, st->e22);
    } else {
        *(void __far * __far *)MK_FP(0, 0x21 * 4) = MK_FP(seg, hd->e21);
        *(void __far * __far *)MK_FP(_psp, 0x0A) = MK_FP(seg, hd->e22);
    }
    hd->on = 1;
    _enable();
    i = find_exe(argv[0]);
    if (mode == XM_EMS)
        log_printf("XMEM: EMS, frame %04X, %u pages: cache %u KB in %u blocks; code in EMS, stub at %04X (%u paragraphs); program handle %d",
                   frame, pages, nslot * 4, nslot, seg, paras, (int)i);
    else if (hma)
        log_printf("XMEM: XMS, %u KB: cache %u KB in %u blocks; code, table and buffer in the HMA at FFFF:%04X (%s: %u bytes from %04X), stub and read buffer at %04X (%u paragraphs); program handle %d",
                   kb, nslot * 4, nslot, HMA_ORG, hma == 2 ? "the XMS driver's" : "DOS=HIGH left",
                   hbytes, hfrom, seg, paras + HMA_LOW, (int)i);
    else
        log_printf("XMEM: XMS, %u KB: cache %u KB in %u blocks; code at %04X (%u paragraphs), buffer at %04X; program handle %d",
                   kb, nslot * 4, nslot, seg, paras + META_PARAS, bounce, (int)i);
    if (hma == 2 && has_opt(argc, argv, "XMEMA20")) {
        /* Test: A20 off behind the cache's back (XMS global disable; the
           HMA is ours, so nothing else turns it on): every call the cache
           wants goes through the stub's local enable and disable. */
        unsigned x[5];
        x[0] = 0x0400;
        x[1] = x[2] = x[3] = x[4] = 0;
        xms_call((unsigned __far *)x);
        log_printf("XMEM: XMEMA20 (A20 off: %u)", x[0]);
    } else if (hma)
        hma_music(hma == 2 ? 0x10 : (hfrom + 15) & ~15u);
    if (has_opt(argc, argv, "XMEMKILL")) {
        /* Test (tools\xmemchk.asm): leave the way Ctrl-Break or a critical
           error's Abort does, without the C run-time's exit, with INT 08h
           pointing elsewhere (the same handler through another segment):
           the terminate address must free the memory and put INT 21h and
           INT 08h back. */
        unsigned __far *v8 = (unsigned __far *)MK_FP(0, 8 * 4);
        log_printf("XMEM: XMEMKILL");
        _disable();
        if (v8[0] < 0xFFF0u) {
            v8[0] += 16;
            v8[1] -= 1;
        }
        _enable();
        r.x.ax = 0x4C03;
        intdos(&r, &r);
    }
}
