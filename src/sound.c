/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "inv.h"
#include "items.h"
#include "map.h"
#include "music.h"
#include "sound.h"
#include "timer.h"
#include "unit.h"

int g_sound_on = 1;

#define PIT_HZ 1193182UL

static int quiet;
static unsigned char fm_sfx;    /* effects as AdLib FM sounds */
static unsigned noise_seed = 1;

/* Own random numbers: sound must not change the game's rnd() sequence. */
static unsigned noise_rnd(unsigned n)
{
    noise_seed = noise_seed * 25173u + 13849u;
    return (noise_seed >> 4) % n;
}

/* ------------------------------------------------------------------ */
/* PC speaker */

static void tone(unsigned hz)
{
    unsigned div = (unsigned)(PIT_HZ / hz);

    outp(0x43, 0xB6);           /* channel 2, lo/hi, square wave */
    outp(0x42, div & 0xFF);
    outp(0x42, div >> 8);
    outp(0x61, inp(0x61) | 3);  /* gate + speaker on */
}

void snd_off(void)
{
    outp(0x61, inp(0x61) & ~3);
}

static void wait_us(unsigned long us)
{
    unsigned long t0 = timer_read(), lim = us * 1193UL / 1000UL;
    while (timer_read() - t0 < lim)
        ;
}

/* Frequency sweep from hz0 to hz1 in steps of step_us. */
static void sweep(unsigned hz0, unsigned hz1, int steps, unsigned step_us)
{
    int i;

    for (i = 0; i < steps; i++) {
        long hz = hz0 + ((long)hz1 - hz0) * i / steps;
        tone((unsigned)hz);
        wait_us(step_us);
    }
    snd_off();
}

/* Random tones make a passable noise burst on a speaker. */
static void noise(unsigned lo, unsigned hi, int steps, unsigned step_us)
{
    int i;

    for (i = 0; i < steps; i++) {
        tone(lo + noise_rnd(hi - lo));
        wait_us(step_us);
    }
    snd_off();
}

static void speaker(int sfx)
{
    switch (sfx) {
    case SFX_RIFLE: case SFX_CANNON: case SFX_CRAFT_CANNON:
        noise(150, 1600, 12, 2500);
        break;
    case SFX_LASER: case SFX_PLASMA: case SFX_UFO_FIRE: case SFX_CRAFT_MISSILE:
        sweep(2400, 600, 14, 3000);
        break;
    case SFX_HIT_BULLET: case SFX_HIT_BEAM: case SFX_GEO_POPUP: case SFX_UFO_HIT:
        noise(80, 400, 10, 4000);
        break;
    case SFX_EXPLOSION: case SFX_UFO_CRASH: case SFX_CRAFT_EXPLODE:
        noise(80, 400, 10, 4000);
        sweep(500, 90, 25, 8000);
        break;
    case SFX_STEP:
        tone(90);
        wait_us(3000);
        snd_off();
        break;
    case SFX_DOOR: case SFX_THROW:
        break;
    default:                    /* deaths */
        sweep(500, 90, 25, 8000);
    }
}

/* ------------------------------------------------------------------ */
/* Sound Blaster: DSP at port, 8-bit DMA, single-cycle transfers. */

unsigned g_sb_port, g_sb_irq, g_sb_dma = 1, g_sb_dsp;
volatile unsigned g_sb_irqs, g_sb_started;

static void (__interrupt __far *old_sb_vec)(void);
static unsigned char old_mask;
static volatile unsigned char sb_busy, sb_cmd, sb_prio;
static volatile unsigned long sb_phys, sb_end;
static volatile unsigned sb_left;

static const unsigned char dma_addr[4] = { 0x00, 0x02, 0x04, 0x06 };
static const unsigned char dma_page[4] = { 0x87, 0x83, 0x81, 0x82 };

static int dsp_write(unsigned char v)
{
    unsigned i;

    for (i = 0; i < 4000; i++)
        if (!(inp(g_sb_port + 0xC) & 0x80)) {
            outp(g_sb_port + 0xC, v);
            return 0;
        }
    return -1;
}

static int dsp_read(void)
{
    unsigned i;

    for (i = 0; i < 4000; i++)
        if (inp(g_sb_port + 0xE) & 0x80)
            return inp(g_sb_port + 0xA);
    return -1;
}

static int dsp_reset(unsigned port)
{
    unsigned i;

    g_sb_port = port;
    outp(port + 6, 1);
    for (i = 0; i < 16; i++)    /* >= 3 us */
        inp(port + 6);
    outp(port + 6, 0);
    for (i = 0; i < 2000; i++)
        if ((inp(port + 0xE) & 0x80) && inp(port + 0xA) == 0xAA)
            return 1;
    return 0;
}

static int irq_vector(unsigned irq)
{
    return irq < 8 ? 8 + irq : 0x70 + irq - 8;
}

/* Raises a DSP interrupt and looks for it in the PIC's request register. */
static unsigned detect_irq(void)
{
    static const unsigned char cand[] = { 7, 5, 2, 3 };
    unsigned before, after, i, irq = 0, mask;
    unsigned long t0;

    /* Masked lines still show up in the request register. */
    mask = inp(0x21);
    outp(0x21, mask | 0xAC);
    outp(0x20, 0x0A);
    before = inp(0x20);
    dsp_write(0xF2);
    t0 = timer_read();
    do {
        outp(0x20, 0x0A);
        after = inp(0x20);
    } while (!((after & ~before) & 0xAC) && timer_read() - t0 < 2400);
    inp(g_sb_port + 0xE);       /* acknowledge */
    outp(0x21, mask);
    for (i = 0; i < sizeof cand; i++)
        if ((after & ~before) & (1 << cand[i])) {
            irq = cand[i];
            break;
        }
    return irq;
}

/* Programs the DMA channel and the DSP for the next piece of the effect
   (a transfer cannot cross a 64 KB page). Interrupts off. */
static void sb_next(void)
{
    unsigned long room = 0x10000UL - (sb_phys & 0xFFFF);
    unsigned n = sb_left < room ? sb_left : (unsigned)room, d = g_sb_dma;
    unsigned off = (unsigned)(sb_phys & 0xFFFF);

    outp(0x0A, 4 | d);          /* mask */
    outp(0x0C, 0);              /* clear flip-flop */
    outp(0x0B, 0x48 | d);       /* single, increment, memory -> device */
    outp(dma_addr[d], off & 0xFF);
    outp(dma_addr[d], off >> 8);
    outp(dma_page[d], (unsigned)(sb_phys >> 16));
    outp(dma_addr[d] + 1, (n - 1) & 0xFF);
    outp(dma_addr[d] + 1, (n - 1) >> 8);
    outp(0x0A, d);              /* unmask */
    dsp_write(sb_cmd);
    dsp_write((n - 1) & 0xFF);
    dsp_write((n - 1) >> 8);
    sb_phys += n;
    sb_left -= n;
    if (sb_cmd == 0x75)
        sb_cmd = 0x74;          /* ADPCM goes on from the last sample */
}

/* The compiler reaches DGROUP through SS, so the interrupt handler switches
   to its own stack (the interrupted code may be DOS on its stack). */
static unsigned char sb_stack[256];

void sb_stack_enter(unsigned seg, unsigned top);
#pragma aux sb_stack_enter =     "mov bx, ss"     "mov cx, sp"     "mov ss, ax"     "mov sp, dx"     "push bx"     "push cx"     parm [ax] [dx] modify [bx cx];
void sb_stack_leave(void);
#pragma aux sb_stack_leave =     "pop cx"     "pop bx"     "mov ss, bx"     "mov sp, cx"     modify [bx cx];

static void sb_irq(void)
{
    inp(g_sb_port + 0xE);       /* acknowledge the DSP */
    g_sb_irqs++;
    if (sb_busy) {
        if (sb_left)
            sb_next();
        else
            sb_busy = 0;
    }
    if (g_sb_irq >= 8)
        outp(0xA0, 0x20);
    outp(0x20, 0x20);
}

static void __interrupt __far sb_isr(void)
{
    sb_stack_enter(FP_SEG(sb_stack), FP_OFF(sb_stack) + sizeof sb_stack);
    sb_irq();
    sb_stack_leave();
}

static int sb_init(void)
{
    /* Without BLASTER only the two common ports: 230h would read the bus
       mouse cards' ports (23Ch-23Fh). */
    static const unsigned ports[] = { 0x220, 0x240 };
    const char *env = getenv("BLASTER");
    unsigned i, port = 0;
    int v;

    if (env) {
        const char *p;
        for (p = env; *p; p++) {
            char c = *p | 0x20;
            if (p > env && p[-1] != ' ')
                continue;
            if (c == 'a')
                port = (unsigned)strtoul(p + 1, NULL, 16);
            else if (c == 'i')
                g_sb_irq = (unsigned)strtoul(p + 1, NULL, 10);
            else if (c == 'd')
                g_sb_dma = (unsigned)strtoul(p + 1, NULL, 10) & 3;
        }
    }
    if (!(port && dsp_reset(port))) {
        port = 0;
        for (i = 0; i < sizeof ports / sizeof ports[0] && !port; i++)
            if (dsp_reset(ports[i]))
                port = ports[i];
    }
    if (!port) {
        g_sb_port = 0;
        return -1;
    }
    dsp_write(0xE1);
    v = dsp_read();
    g_sb_dsp = (v << 8) | (dsp_read() & 0xFF);
    dsp_write(0xD1);            /* speaker on */
    if (!g_sb_irq)
        g_sb_irq = detect_irq();
    if (g_sb_irq == 2 && (*(unsigned char __far *)MK_FP(0xF000, 0xFFFE) == 0xFC))
        g_sb_irq = 9;           /* AT: IRQ 2 arrives as IRQ 9 */
    if (g_sb_irq) {
        old_sb_vec = _dos_getvect(irq_vector(g_sb_irq));
        _dos_setvect(irq_vector(g_sb_irq), sb_isr);
        _disable();
        if (g_sb_irq < 8) {
            old_mask = (unsigned char)inp(0x21);
            outp(0x21, old_mask & ~(1 << g_sb_irq));
        } else {
            old_mask = (unsigned char)inp(0xA1);
            outp(0xA1, old_mask & ~(1 << (g_sb_irq - 8)));
        }
        _enable();
    }
    return 0;
}

static void sb_halt(void)
{
    _disable();
    if (sb_busy)
        dsp_write(0xD0);
    outp(0x0A, 4 | g_sb_dma);
    sb_busy = 0;
    sb_left = 0;
    _enable();
}

static void sb_shutdown(void)
{
    sb_halt();
    dsp_write(0xD3);            /* speaker off */
    if (g_sb_irq) {
        _disable();
        if (g_sb_irq < 8)
            outp(0x21, (inp(0x21) & ~(1 << g_sb_irq)) | (old_mask & (1 << g_sb_irq)));
        else
            outp(0xA1, (inp(0xA1) & ~(1 << (g_sb_irq - 8))) | (old_mask & (1 << (g_sb_irq - 8))));
        _enable();
        _dos_setvect(irq_vector(g_sb_irq), old_sb_vec);
    }
}

/* ------------------------------------------------------------------ */
/* Where the effects live. Digital effects never take conventional memory
   (the game needs it): they go to an upper memory block (UMB, below 1 MB,
   so the DMA reads them in place) or to XMS extended memory (286+), from
   which each effect is copied into a small DMA buffer when it plays. With
   neither, the Sound Blaster plays no effects (the speaker does) and its
   AdLib part plays the music. */

#define RESERVE     (15 * 1024L)    /* conventional memory music leaves free */
#define UMB_MIN     (8 * 1024L)     /* smallest UMB worth using for effects */

typedef struct {
    unsigned long file_off;
    unsigned len, rate;
    unsigned char fmt;
} SfxFile;

static SfxFile sfx_file[N_SFX];
static unsigned long sfx_pos[N_SFX];    /* in the store */
static unsigned sfx_len[N_SFX];         /* 0 = not loaded */
static int ctx;                 /* 0 none, 1 geoscape, 2 battle */
static int last_hit = SFX_HIT_BULLET, race_death = SFX_DIE_SECTOID;
static unsigned scream;

/* The UMBs, found by startup.c umb_plan, freed here at the end (sound.h). */
unsigned umb_seg, umb_paras;        /* the sound's UMB, 0 = none */
unsigned char umb_game;             /* its top is the game's */
unsigned char umb_xms;              /* from the XMS driver (else DOS) */
unsigned umb_more[UMB_MORE];        /* the game's other UMBs */
unsigned char n_more;
static unsigned xms_handle;         /* extended memory block, 0 = none */
static unsigned bounce_seg;         /* DMA buffer for XMS effects */
static unsigned bounce_len;
static unsigned fixed_end;          /* UMB bytes kept for good (effects or
                                       DMA buffer); per screen after it */
static unsigned char all_loaded;    /* every effect stays loaded */
static unsigned sfx_total, sfx_max; /* SOUNDS.PAK: all effects, largest */
static unsigned char in_bounce = 0xFF;  /* effect in the DMA buffer */
static int music_umb;               /* music track in the UMB */
static unsigned char per_screen;    /* effects loaded per screen (UMB) */
unsigned char xms_entry_ok;         /* 1 XMS driver, 2 none (umb_plan) */
int g_hma_track = -1;               /* the track xmem.c read into the HMA at FFFF:g_hma_lo */
unsigned g_hma_lo;

static const unsigned char prio[N_SFX] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  /* shots, hits, explosion, deaths */
    1, 0, 1,                                /* door, step, throw */
    2, 2, 2, 2, 2, 2, 2                     /* geoscape */
};

void __cdecl xms_call(unsigned __far *r);

static unsigned xr[5];          /* XMS call: AX BX DX SI DS */

/* XMS function ah with DX and DS:SI; returns AX (1 = done), BX/DX in xr. */
static unsigned xms(unsigned ah, unsigned dx, void __far *p)
{
    xr[0] = ah << 8;
    xr[1] = 0;
    xr[2] = dx;
    xr[3] = FP_OFF(p);
    xr[4] = FP_SEG(p);
    xms_call(xr);
    return xr[0];
}

static long dos_free(void)
{
    union REGS r;

    r.h.ah = 0x48;
    r.w.bx = 0xFFFF;
    int86(0x21, &r, &r);
    return (long)r.w.bx * 16;
}

static void read_index(void)
{
    FILE *f = fopen("SOUNDS.PAK", "rb");
    unsigned char rec[10];
    unsigned n = 0, i;

    if (!f)
        return;
    if (fread(rec, 1, 6, f) == 6 && !memcmp(rec, "XSN1", 4))
        n = *(unsigned *)(rec + 4);
    for (i = 0; i < n && i < N_SFX; i++) {
        if (fread(rec, 1, 10, f) != 10)
            break;
        sfx_file[i].file_off = *(unsigned long *)rec;
        sfx_file[i].len = *(unsigned *)(rec + 4);
        sfx_file[i].rate = *(unsigned *)(rec + 6);
        sfx_file[i].fmt = rec[8];
        sfx_total += (sfx_file[i].len + 1) & ~1u;
        if (sfx_file[i].len > sfx_max)
            sfx_max = sfx_file[i].len;
    }
    fclose(f);
}

static unsigned long phys(unsigned seg)
{
    return (unsigned long)seg << 4;
}

/* An effect must not cross a 64 KB DMA page: moves pos past the edge. */
static unsigned long no_page_cross(unsigned long base, unsigned long pos, unsigned len)
{
    unsigned long a = base + pos;

    if ((a & 0xFFFF) + len > 0x10000UL)
        pos += 0x10000UL - (a & 0xFFFF);
    return pos;
}

static void umb_free(unsigned seg)
{
    if (umb_xms)
        xms(0x11, seg, NULL);
    else
        _dos_freemem(seg);
}

static void umb_release(void)
{
    while (n_more)
        umb_free(umb_more[--n_more]);
    if (!umb_seg)
        return;
    umb_free(umb_seg);
    umb_seg = 0;
}

/* Copies len bytes (even) between memory and the extended memory block. */
static int xms_move(unsigned long len, int to_xms, unsigned long xoff, void __far *p)
{
    static struct {
        unsigned long len;
        unsigned src_h;
        unsigned long src_off;
        unsigned dst_h;
        unsigned long dst_off;
    } mv;
    unsigned long conv = ((unsigned long)FP_SEG(p) << 16) | FP_OFF(p);

    mv.len = len;
    if (to_xms) {
        mv.src_h = 0;
        mv.src_off = conv;
        mv.dst_h = xms_handle;
        mv.dst_off = xoff;
    } else {
        mv.src_h = xms_handle;
        mv.src_off = xoff;
        mv.dst_h = 0;
        mv.dst_off = conv;
    }
    return xms(0x0B, 0, &mv) == 1 ? 0 : -1;
}

/* Conventional DMA buffer that does not cross a 64 KB page. */
static unsigned bounce_alloc(unsigned len)
{
    unsigned paras = (len + 15) >> 4, seg, gap;

    if (_dos_allocmem(paras, &seg))
        return 0;
    if (((phys(seg) & 0xFFFF) + len) <= 0x10000UL)
        return seg;
    /* Crosses: fill the room up to the page edge, then allocate again. */
    _dos_freemem(seg);
    gap = (unsigned)((0x10000UL - (phys(seg) & 0xFFFF)) >> 4);
    if (_dos_allocmem(gap, &gap))
        return 0;
    if (_dos_allocmem(paras, &seg))
        seg = 0;
    _dos_freemem(gap);
    return seg;
}

/* Reads every effect into the store (UMB at offset 0 or the XMS block). */
static int load_all(void)
{
    unsigned long pos = 0, base = phys(umb_seg);
    unsigned char __far *tmp = MK_FP(bounce_seg, 0);
    FILE *f = fopen("SOUNDS.PAK", "rb");
    int i;

    if (!f)
        return -1;
    for (i = 0; i < N_SFX; i++) {
        unsigned len = sfx_file[i].len;
        if (!len)
            continue;
        if (!xms_handle)
            pos = no_page_cross(base, pos, len);
        if (fseek(f, sfx_file[i].file_off, SEEK_SET))
            break;
        if (xms_handle) {
            if (fread(tmp, 1, len, f) != len ||
                xms_move((len + 1) & ~1u, 1, pos, tmp))
                break;
        } else if (fread((unsigned char __far *)MK_FP(umb_seg, 0) + (unsigned)pos, 1, len, f) != len) {
            break;
        }
        sfx_pos[i] = pos;
        sfx_len[i] = len;
        pos += (len + 1) & ~1u;
    }
    fclose(f);
    if (i < N_SFX) {
        memset(sfx_len, 0, sizeof sfx_len);
        return -1;
    }
    if (!xms_handle)
        fixed_end = (unsigned)pos;
    all_loaded = 1;
    return 0;
}

/* Finds memory for the effects outside conventional memory; logs where. */
static void store_init(void)
{
    unsigned need = (unsigned)(((unsigned long)sfx_total + 0x1000 + 15) >> 4);  /* + page edges */
    unsigned kb = (sfx_total + 1023) / 1024;
    const char *why = NULL;

    if (!sfx_total) {
        log_printf("sound: no digital effects (SOUNDS.PAK missing)");
        return;
    }
    if (umb_seg && umb_paras >= need && load_all() == 0) {
        log_printf("sound: all %u effects in the UMB (%u bytes)", N_SFX, fixed_end);
        return;
    }
    bounce_len = (sfx_max + 1) & ~1u;
    if (xms_entry_ok == 1) {
        if (xms(0x09, kb, NULL) == 1) {
            xms_handle = xr[2];
            /* The DMA buffer goes to the UMB if there is one. */
            if (umb_seg && umb_paras >= ((bounce_len + 15) >> 4) * 2) {
                unsigned long p = no_page_cross(phys(umb_seg), 0, bounce_len);
                bounce_seg = umb_seg + (unsigned)(p >> 4);
                fixed_end = (unsigned)p + bounce_len;
            } else {
                bounce_seg = bounce_alloc(bounce_len);
            }
            if (bounce_seg && load_all() == 0) {
                log_printf("sound: all %u effects in XMS (%u KB), %u-byte DMA buffer at %04X (%s)",
                           N_SFX, kb, bounce_len, bounce_seg,
                           bounce_seg >= 0xA000 ? "UMB" : "conventional");
                return;
            }
            why = "XMS copy failed";
            if (bounce_seg && bounce_seg < 0xA000)
                _dos_freemem(bounce_seg);
            bounce_seg = 0;
            fixed_end = 0;
            xms(0x0A, xms_handle, NULL);
            xms_handle = 0;
        } else {
            why = "no free XMS";
        }
    }
    if (umb_seg && (long)umb_paras * 16 >= UMB_MIN) {
        per_screen = 1;
        log_printf("sound: effects loaded per screen into the UMB");
        return;
    }
    log_printf("sound: no digital effects: %s (needs a UMB of %u KB+ or XMS); PC speaker effects",
               why ? why : (umb_seg ? "UMB too small" :
                            xms_entry_ok == 1 ? "no UMB" : "no UMB, no XMS driver"),
               (unsigned)(UMB_MIN / 1024));
}

static void store_shutdown(void)
{
    if (xms_handle)
        xms(0x0A, xms_handle, NULL);
    xms_handle = 0;
    if (bounce_seg && bounce_seg < 0xA000)
        _dos_freemem(bounce_seg);
    bounce_seg = 0;
    umb_release();
}

static void free_effects(void)
{
    sb_halt();
    if (!all_loaded)
        memset(sfx_len, 0, sizeof sfx_len);
}

/* UMB bytes from fixed_end on that the screen's music does not use. */
static unsigned umb_music_end;

/* Loads the listed effects, in order, into the UMB as far as it goes (per
   screen; nothing to do when every effect stays loaded). */
static void load_effects(const unsigned char *list, int n)
{
    unsigned long pos = umb_music_end, end = (unsigned long)umb_paras * 16, base;
    int i, k;
    FILE *f;

    free_effects();
    if (!per_screen)
        return;
    base = phys(umb_seg);
    f = fopen("SOUNDS.PAK", "rb");
    if (!f)
        return;
    for (k = 0; k < n; k++) {
        unsigned long p;
        i = list[k];
        if (sfx_len[i] || !sfx_file[i].len)
            continue;
        p = no_page_cross(base, pos, sfx_file[i].len);
        if (p + sfx_file[i].len > end)
            break;
        if (fseek(f, sfx_file[i].file_off, SEEK_SET) ||
            fread((unsigned char __far *)MK_FP(umb_seg, 0) + (unsigned)p, 1,
                  sfx_file[i].len, f) != sfx_file[i].len)
            break;
        sfx_pos[i] = p;
        sfx_len[i] = sfx_file[i].len;
        pos = p + ((sfx_len[i] + 1) & ~1u);
    }
    fclose(f);
    log_printf("sound: %d effects in the UMB, %lu bytes", k, pos - umb_music_end);
}

/* Adds sfx to a list once. */
static int add(unsigned char *list, int n, int sfx)
{
    int k;

    for (k = 0; k < n; k++)
        if (list[k] == sfx)
            return n;
    list[n] = (unsigned char)sfx;
    return n + 1;
}

static int fire_sfx(int weapon)
{
    switch (weapon) {
    case IT_HCANNON:
        return SFX_CANNON;
    case IT_LPISTOL: case IT_LRIFLE: case IT_HLASER:
        return SFX_LASER;
    case IT_PPISTOL: case IT_PRIFLE: case IT_HPLASMA:
        return SFX_PLASMA;
    }
    return SFX_RIFLE;
}

static int hit_sfx(int weapon)
{
    return fire_sfx(weapon) == SFX_LASER || fire_sfx(weapon) == SFX_PLASMA
           ? SFX_HIT_BEAM : SFX_HIT_BULLET;
}

void snd_free(void)
{
    mus_stop();
    music_umb = 0;
    umb_music_end = fixed_end;
    free_effects();
    ctx = 0;
}

/* The screen's track: in the UMB when it fits there (leaving room for the
   effects when they load per screen), else in conventional memory only
   when the game keeps RESERVE free (sound gives way first). */
static void start_music(int track, int loop)
{
    unsigned m = mus_size(track);
    unsigned long room = umb_seg ? (unsigned long)umb_paras * 16 - fixed_end : 0;

    if (!m)
        return;
    if (umb_seg && room >= m + (per_screen ? UMB_MIN : 0)) {
        if (mus_play_at(track, loop, (unsigned char __far *)MK_FP(umb_seg, 0) + fixed_end) == 0) {
            music_umb = 1;
            umb_music_end = (fixed_end + m + 15) & ~15u;
            log_printf("music: track %d in the UMB, %u bytes", track, m);
            return;
        }
    }
    if (track == g_hma_track && !mus_play_at(track, loop, (unsigned char __far *)MK_FP(0xFFFF, g_hma_lo)))
        return;
    if (dos_free() - (long)m >= RESERVE) {
        if (mus_play(track, loop) == 0)
            log_printf("music: track %d, %u bytes, %ld KB free", track, m, dos_free() / 1024);
    } else {
        log_printf("music: track %d (%u bytes) left out: %ld KB free", track, m, dos_free() / 1024);
    }
}

void snd_set_battle(int race)
{
    unsigned char list[N_SFX];
    int n = 0, i, grenades = 0;

    snd_free();
    ctx = 2;
    if (quiet)
        return;
    switch (race) {
    case 'N': race_death = SFX_DIE_SNAKEMAN; break;
    case 'M': race_death = SFX_DIE_MUTON; break;
    case 'E': case 'X': race_death = SFX_DIE_ETHEREAL; break;
    default: race_death = SFX_DIE_SECTOID;
    }
    start_music(race == 'X' ? MUS_MARS : MUS_TACTIC, 1);
    /* Priority: shots of the weapons present, deaths, impacts, the rest. */
    for (i = 0; i < g_nunits; i++) {
        if (g_units[i].weapon != NO_ITEM)
            n = add(list, n, fire_sfx(g_units[i].weapon));
        if (g_units[i].n_grenades)
            grenades = 1;
    }
    for (i = 0; i < g_max_carry; i++)
        if (g_carry[i].unit != CARRY_FREE && g_carry[i].item < N_ITEMS) {
            if (g_items[g_carry[i].item].kind == IK_GRENADE)
                grenades = 1;
            /* a gun in the left hand fires too */
            if (g_items[g_carry[i].item].kind == IK_WEAPON && SLOT_SEC(g_carry[i].slot) == SEC_LHAND)
                n = add(list, n, fire_sfx(g_carry[i].item));
        }
    n = add(list, n, race_death);
    if (race == 'X')
        n = add(list, n, SFX_DIE_MUTON);
    n = add(list, n, SFX_DIE_MALE);
    for (i = 0; i < g_nunits; i++)
        if (g_units[i].weapon != NO_ITEM)
            n = add(list, n, hit_sfx(g_units[i].weapon));
    if (grenades)
        n = add(list, n, SFX_EXPLOSION);
    n = add(list, n, SFX_DIE_FEMALE);
    n = add(list, n, SFX_DOOR);
    n = add(list, n, SFX_STEP);
    load_effects(list, n);
}

void snd_result(int res)
{
    snd_free();
    if (!quiet)
        start_music(res == 1 ? MUS_WIN : MUS_LOSE, 0);
}

static int geo_track;            /* the geoscape's track: MUS_GEO1 or MUS_INTER */

static void geo_load(int track)
{
    static const unsigned char list[] = {
        SFX_GEO_POPUP, SFX_CRAFT_CANNON, SFX_UFO_HIT, SFX_UFO_CRASH,
        SFX_CRAFT_MISSILE, SFX_UFO_FIRE, SFX_CRAFT_EXPLODE
    };

    snd_free();
    ctx = 1;
    geo_track = track;
    if (quiet)
        return;
    start_music(track, 1);
    load_effects(list, sizeof list);
}

void snd_set_geo(void)
{
    geo_load(MUS_GEO1);
}

/* GeoscapeState: GMINTER while a dogfight is on (minimised ones too),
   GMGEO again when none is left. The effects reload after the track,
   which may take the UMB room they had. */
void snd_dogfight(int on)
{
    int track = on ? MUS_INTER : MUS_GEO1;

    if (ctx == 1 && track != geo_track)
        geo_load(track);
}

/* ------------------------------------------------------------------ */

void snd_init(int flags)
{
    int sb, music;

    quiet = flags & SND_QUIET;
    if (quiet) {
        g_sound_on = 0;
        return;
    }
    sb = !(flags & SND_NO_SB) && sb_init() == 0;
    if (sb)
        log_printf("sound: Sound Blaster at %X IRQ %u DMA %u, DSP %u.%02u",
                   g_sb_port, g_sb_irq, g_sb_dma, g_sb_dsp >> 8, g_sb_dsp & 0xFF);
    else
        log_printf("sound: PC speaker");
    music = !(flags & SND_NO_MUSIC);
    if ((music || !(flags & SND_NO_FMSFX)) && mus_init(music) == 0) {
        fm_sfx = !(flags & SND_NO_FMSFX);
        if (fm_sfx)
            log_printf("sound: AdLib FM effects (where no digital effect is loaded)");
    } else {
        music = 0;
    }
    if (music)
        log_printf("sound: AdLib music, %u bytes for the battle track", mus_size(MUS_TACTIC));
    if (sb) {
        read_index();
        store_init();
    }
    if (umb_seg && !umb_game && !music && !per_screen && !(all_loaded && !xms_handle) &&
        !(bounce_seg >= 0xA000))
        umb_release();          /* nothing uses it */
    umb_music_end = fixed_end;
}

void snd_shutdown(void)
{
    if (g_mus_ticks)
        log_printf("music: %lu ticks, %lu us average, %u us longest",
                   g_mus_ticks, g_mus_counts / g_mus_ticks * 1000UL / 1193UL,
                   (unsigned)(g_mus_max * 1000UL / 1193UL));
    if (g_sb_port)
        log_printf("sound: %u effects started, %u interrupts", g_sb_started, g_sb_irqs);
    snd_free();
    mus_shutdown();
    if (g_sb_port)
        sb_shutdown();
    store_shutdown();
    snd_off();
}

void snd_play(int sfx)
{
    unsigned long now;

    if (!g_sound_on || sfx < 0 || sfx >= N_SFX)
        return;
    if (!sfx_len[sfx]) {
        if (fm_sfx)
            /* (no LOG line: with LOG on, each line reopens the file, about
               150 ms on the XT's disk, on every step's sound) */
            mus_fx(sfx);
        else {
            speaker(sfx);
        }
        return;
    }
    now = timer_read();
    _disable();
    if (sb_busy && now > sb_end)
        sb_busy = 0;            /* lost interrupt */
    if (sb_busy && prio[sfx] < sb_prio) {
        _enable();
        return;
    }
    if (sb_busy)
        dsp_write(0xD0);        /* stop the current effect */
    outp(0x0A, 4 | g_sb_dma);
    sb_busy = 0;
    sb_left = 0;
    /* Its end may already be waiting as an interrupt: acknowledge it. */
    if (g_sb_irq && g_sb_irq < 8) {
        outp(0x20, 0x0A);
        if (inp(0x20) & (1 << g_sb_irq))
            inp(g_sb_port + 0xE);
    }
    if (xms_handle) {
        /* Into the DMA buffer (the transfer is stopped; the copy may turn
           interrupts on). */
        if (in_bounce != sfx) {
            _enable();
            if (xms_move((sfx_len[sfx] + 1) & ~1u, 0, sfx_pos[sfx], MK_FP(bounce_seg, 0))) {
                in_bounce = 0xFF;
                return;
            }
            in_bounce = (unsigned char)sfx;
            _disable();
        }
        sb_phys = phys(bounce_seg);
    } else {
        sb_phys = phys(umb_seg) + sfx_pos[sfx];
    }
    dsp_write(0x40);            /* time constant */
    dsp_write((unsigned char)(256 - (unsigned)(1000000UL / sfx_file[sfx].rate)));
    sb_left = sfx_len[sfx];
    sb_cmd = sfx_file[sfx].fmt ? 0x75 : 0x14;
    sb_prio = prio[sfx];
    sb_end = now + (unsigned long)sfx_len[sfx] * (sfx_file[sfx].fmt ? 2 : 1) *
                   (PIT_HZ / sfx_file[sfx].rate) + PIT_HZ / 10;
    sb_busy = 1;
    g_sb_started++;
    sb_next();
    _enable();
}

/* ------------------------------------------------------------------ */
/* Game events */

void snd_fire(int weapon)
{
    last_hit = hit_sfx(weapon);
    snd_play(fire_sfx(weapon));
}

void snd_shot(int alien)
{
    if (ctx == 1) {
        snd_play(SFX_CRAFT_CANNON);
        return;
    }
    snd_fire(alien ? IT_PRIFLE : IT_RIFLE);
}

void snd_hit(void)
{
    snd_play(ctx == 1 ? SFX_GEO_POPUP : last_hit);
}

void snd_kill(int alien)
{
    if (alien)
        snd_play(race_death);
    else {
        int s = ++scream & 1 ? SFX_DIE_MALE : SFX_DIE_FEMALE;
        if (!sfx_len[s])
            s = SFX_DIE_MALE;
        snd_play(s);
    }
}

void snd_death(void)
{
    snd_play(race_death);
}

void snd_explode(void)
{
    snd_play(ctx == 1 ? SFX_UFO_CRASH : SFX_EXPLOSION);
}

void snd_door(void)
{
    snd_play(SFX_DOOR);
}

void snd_step(void)
{
    snd_play(SFX_STEP);
}
