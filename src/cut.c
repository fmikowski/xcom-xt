/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Cutscenes (cut.h): the intro movie streamed from INTROx.PAK and the end
   slides from SLIDESx.PAK (tools/intro.py describes both). An overlay section
   of its own: nothing of it is resident in a battle. */

#include <bios.h>
#include <conio.h>
#include <dos.h>
#include <fcntl.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cut.h"
#include "gfx.h"
#include "input.h"
#include "music.h"
#include "sound.h"
#include "timer.h"
#include "uik.h"
#include "vgageo.h"
#include "xmem.h"

unsigned __cdecl cut_runs(const unsigned char far *src, unsigned planes, unsigned vseg);
unsigned __cdecl cut_fli(const unsigned char far *frame);      /* (VGA) */

#define FLAG_FULL   1
#define FLAG_MUSIC  2
#define JIFFY       17045UL     /* PIT counts in 1/70 s (FLI frame delays) */
#define TEXT_MAX    512         /* a slide's caption, bytes */

static unsigned char play_end = 1;      /* the end slides may play */

typedef struct {
    unsigned size, due;
    unsigned char flags, music, cga_pal, pad;
} FrameHdr;

/* SLIDESx.PAK's slide record (14 bytes). */
typedef struct {
    unsigned long pic;
    unsigned text, x, y;
    unsigned char colour, cga_pal;
    unsigned pad;
} SlideRec;

/* ------------------------------------------------------------------ */
/* Input without the mouse pointer (DOS hides it in cutscenes). */

static int buttons;

static int mouse_buttons(void)
{
    union REGS r;
    int b;

    if (!g_mouse)
        return 0;
    r.x.ax = 3;
    int86(0x33, &r, &r);
    b = r.x.bx & 3;
    /* The driver's press counts (INT 33h function 5) belong to the next
       screen's mouse_poll: clear them, or a click that skips a scene
       would come again there. */
    r.x.ax = 5;
    r.x.bx = 0;
    int86(0x33, &r, &r);
    r.x.ax = 5;
    r.x.bx = 1;
    int86(0x33, &r, &r);
    return b;
}

static unsigned long script_wait(void);
static unsigned long wait_t0, wait_left;    /* a script's WAIT running (PIT counts) */

/* A key or a new click: its key code (KEY_LCLICK / KEY_RCLICK), else 0.
   A script's WAIT n lets n tenths of a second go by here, the cutscene
   going on (input_key would wait them out, and at the script's end wait
   for the keyboard: the slides' 30 seconds never ran out). */
static unsigned poll_skip(void)
{
    int b;

    if (g_script) {
        if (wait_left) {
            if (timer_read() - wait_t0 < wait_left)
                return 0;
            wait_left = 0;
        }
        if ((wait_left = script_wait()) != 0) {
            wait_t0 = timer_read();
            return 0;
        }
        return input_ready() ? input_key() : 0;
    }
    if (_bios_keybrd(_KEYBRD_READY))
        return _bios_keybrd(_KEYBRD_READ);
    b = mouse_buttons();
    if (b & ~buttons) {
        buttons = b;
        return b & 1 ? KEY_LCLICK : KEY_RCLICK;
    }
    buttons = b;
    return 0;
}

/* A KEYS script that runs through the intro: "WAIT n" first lets the
   movie play n tenths of a second, then the next key skips it (a script
   with another first key skips it at once). Returns the PIT counts to
   wait, or 0 for none. */
static unsigned long script_wait(void)
{
    char tok[12];
    long pos;
    int c, n = 0;

    if (!g_script)
        return 0;
    pos = ftell(g_script);
    do
        c = fgetc(g_script);
    while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
    while (c != EOF && c > ' ' && n < 11) {
        tok[n++] = (char)c;
        c = fgetc(g_script);
    }
    tok[n] = 0;
    if (stricmp(tok, "WAIT")) {
        fseek(g_script, pos, SEEK_SET);
        return 0;
    }
    n = 0;
    do
        c = fgetc(g_script);
    while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
    while (c >= '0' && c <= '9') {
        n = n * 10 + c - '0';
        c = fgetc(g_script);
    }
    return (unsigned long)n * 119318UL;
}

/* ------------------------------------------------------------------ */
/* Video memory */

static unsigned vseg(void)
{
    return g_video == VIDEO_EGA ? 0xA000 : 0xB800;
}

/* Write mode 0, all bits, no set / reset: plain writes through the map mask. */
static void ega_plain(void)
{
    if (g_video != VIDEO_EGA)
        return;
    outpw(0x3CE, 0x0001);
    outpw(0x3CE, 0x0003);
    outpw(0x3CE, 0x0005);
    outpw(0x3CE, 0xFF08);
    outpw(0x3C4, 0x0F02);
}

static void clear_screen(void)
{
    if (g_vgx) {
        _fmemset(MK_FP(0xA000, 0), 0, 64000u);
        return;
    }
    ega_plain();
    _fmemset(MK_FP(vseg(), 0), 0, g_video == VIDEO_EGA ? 8000 : 16384);
    hgc_mark(0, 0, 320, 200);
}

/* A full screen from the file straight into video memory: EGA four planes
   of 8000 bytes, CGA the two line banks. */
static int read_full(int fd)
{
    unsigned k, n, got;

    ega_plain();
    n = g_video == VIDEO_EGA ? 4 : 2;
    for (k = 0; k < n; k++) {
        unsigned off = 0;
        if (g_video == VIDEO_EGA)
            outpw(0x3C4, ((1 << k) << 8) | 2);
        else
            off = k * 0x2000;
        if (_dos_read(fd, MK_FP(vseg(), off), 8000, &got) || got != 8000)
            break;
    }
    outpw(0x3C4, 0x0F02);
    hgc_mark(0, 0, 320, 200);
    return k == n;
}

/* EGA fade (VideoState fades the palette): darker colours, then black. */
static void set_pal(const unsigned char *pal, int black)
{
    union REGS r;
    int i;

    for (i = 0; i < 16; i++) {
        r.w.ax = 0x1000;
        r.h.bl = (unsigned char)i;
        r.h.bh = black ? 0 : pal[i];
        int86(0x10, &r, &r);
    }
}

/* A DOS block at the top of memory (last fit), so that nothing the C heap
   or DOS takes meanwhile ends up above it and leaves a hole when it is
   freed (as music.c's track_alloc). 0 if there is no room. */
static unsigned alloc_top(unsigned paras)
{
    union REGS r;
    unsigned strat, seg = 0;

    r.w.ax = 0x5800;
    intdos(&r, &r);
    strat = r.w.ax;
    r.w.ax = 0x5801;
    r.w.bx = 2;
    intdos(&r, &r);
    r.h.ah = 0x48;
    r.w.bx = paras;
    intdos(&r, &r);
    if (!r.w.cflag)
        seg = r.w.ax;
    r.w.ax = 0x5801;
    r.w.bx = strat;
    intdos(&r, &r);
    return seg;
}

/* DOS seek from the start of the file; 0 if done. */
static int seek(int fd, unsigned long pos)
{
    union REGS r;

    r.w.ax = 0x4200;
    r.w.bx = fd;
    r.w.cx = (unsigned)(pos >> 16);
    r.w.dx = (unsigned)pos;
    intdos(&r, &r);
    return r.w.cflag;
}

static int open_pack(const char *base)
{
    char name[13];
    int fd;

    sprintf(name, "%s%c.PAK", base, g_pakv);
    if (_dos_open(name, O_RDONLY, &fd))
        return -1;
    return fd;
}

/* ------------------------------------------------------------------ */
/* The intro */

/* Waits until the PIT count passes t0 + d; 1 if a key or click came. */
static int wait_until(unsigned long t0, unsigned long d, unsigned long skip_at)
{
    for (;;) {
        unsigned long now = timer_read() - t0;
        if (skip_at && now >= skip_at) {
            if (g_script)
                input_key();            /* the script's key that skips */
            return 1;
        }
        if (!skip_at && !g_script && poll_skip())
            return 1;
        if (now >= d)
            return 0;
    }
}

static void play_intro(int timing)
{
    unsigned char hdr[16];
    FrameHdr h;
    unsigned char far *buf;
    unsigned seg = 0, got, i, n, bufsize, nplanes, late = 0;
    unsigned short far *rec = NULL;
    unsigned rec_seg = 0;
    unsigned long t0, skip_at, late_max = 0, t_read = 0, t_blit = 0, bytes = 0, now, hold, last = 0;
    unsigned long runs = 0;
    int fd, skipped = 0;

    fd = open_pack("INTRO");
    if (fd < 0) {
        log_printf("INTRO no INTRO%c.PAK", g_pakv);
        return;
    }
    if (_dos_read(fd, hdr, 16, &got) || got != 16 || memcmp(hdr, "XIN1", 4)
        || hdr[4] != (g_vgx ? 2 : g_video == VIDEO_EGA ? 0 : 1)) {
        log_printf("INTRO pack not for this video");
        _dos_close(fd);
        return;
    }
    n = *(unsigned *)(hdr + 6);
    hold = *(unsigned *)(hdr + 8) * JIFFY;
    bufsize = *(unsigned *)(hdr + 10);
    nplanes = g_video == VIDEO_EGA ? 4 : 1;
    seg = alloc_top((bufsize + 15) >> 4);
    if (!seg) {
        log_printf("INTRO no memory for %u bytes", bufsize);
        _dos_close(fd);
        return;
    }
    buf = MK_FP(seg, 0);
    if (timing && (rec_seg = alloc_top((n * 8 + 15) >> 4)) != 0)
        rec = MK_FP(rec_seg, 0);
    mouse_hide();
    buttons = mouse_buttons();
    if (g_vgx)
        vgx_slide(NULL, 0);             /* black until the first frame's colours */
    else
        uk_cga_palette(hdr[5]);
    clear_screen();
    _dos_read(fd, &h, 8, &got);
    skip_at = script_wait();
    if (g_script && !skip_at)
        skip_at = 1;                    /* a key at once */
    t0 = timer_read();
    for (i = 0; i < n; i++) {
        unsigned long r0 = timer_read(), r1, b1;
        FrameHdr next;
        unsigned k = 0;

        if (!(h.flags & FLAG_FULL)) {
            if (_dos_read(fd, buf, h.size + 8, &got) || got < h.size)
                break;
            _fmemcpy(&next, buf + h.size, 8);
            if (got < h.size + 8)
                memset(&next, 0, 8);
        }
        r1 = timer_read();
        if (wait_until(t0, h.due * JIFFY, skip_at)) {
            skipped = 1;
            break;
        }
        now = timer_read();
        if (now - t0 > h.due * JIFFY + 2 * 1193UL) {
            late++;
            if (now - t0 - h.due * JIFFY > late_max)
                late_max = now - t0 - h.due * JIFFY;
        }
        if (h.flags & FLAG_FULL) {
            if (!read_full(fd) || _dos_read(fd, &next, 8, &got))
                break;
            if (got < 8)
                memset(&next, 0, 8);
        } else if (g_vgx)
            k = cut_fli(buf);           /* VGA: the FLI's own chunks (INTROV.PAK) */
        else {
            ega_plain();
            k = cut_runs(buf, nplanes, vseg());
            hgc_runs(buf);              /* Hercules: what the runs wrote */
        }
        hgc_flush(0);                   /* Hercules: a full picture (read_full) */
        b1 = timer_read();
        if (h.flags & FLAG_MUSIC)
            mus_play(MUS_INTRO1 + h.music, 0);
        if (h.cga_pal != 0xFF)
            uk_cga_palette(h.cga_pal);
        t_read += r1 - r0;
        t_blit += b1 - now;
        bytes += h.size;
        runs += k;
        if (rec) {
            /* read, blit (1/10 ms), size, runs */
            rec[i * 4] = (unsigned)((r1 - r0 + (h.flags & FLAG_FULL ? b1 - now : 0)) / 119);
            rec[i * 4 + 1] = (unsigned)((h.flags & FLAG_FULL ? 0 : b1 - now) / 119);
            rec[i * 4 + 2] = h.size | (h.flags & FLAG_FULL ? 0x8000u : 0);
            rec[i * 4 + 3] = k;
        }
        last = h.due * JIFFY;
        h = next;
    }
    _dos_close(fd);
    _dos_freemem(seg);
    log_printf("INTRO %u of %u frames shown, %u late (max %lu ms), read %lu ms, "
               "video %lu ms, %lu KB, %lu runs", i, n, late, timer_ms(late_max),
               timer_ms(t_read), timer_ms(t_blit), bytes / 1024, runs);
    if (rec) {
        FILE *f = fopen("INTRO.TXT", "w");
        unsigned k;
        if (f) {
            fprintf(f, "frame read_0.1ms video_0.1ms size runs\n");
            for (k = 0; k < i; k++)
                fprintf(f, "%u %u %u %u %u\n", k, rec[k * 4], rec[k * 4 + 1], rec[k * 4 + 2],
                        rec[k * 4 + 3]);
            fclose(f);
        }
        _dos_freemem(rec_seg);
    }
    if (!skipped && i == n) {
        /* The last picture stays while the music plays on, then fades. */
        skipped = wait_until(t0, last + hold, skip_at);
        log_printf("INTRO %s", skipped ? "skipped in the hold" : "played to the end");
        if (!skipped && g_video == VIDEO_EGA) {
            unsigned long t1 = timer_read();
            set_pal(g_night_pal, 0);
            wait_until(t1, 450UL * 1193, 0);
            set_pal(g_night_pal, 1);
            wait_until(t1, 900UL * 1193, 0);
        }
    } else
        log_printf("INTRO skipped at frame %u", i);
    mus_stop();
    if (g_vgx)
        vgx_slide(NULL, 0);             /* black; the menu's picture sets its palette */
    clear_screen();
    gfx_night(0);
    uk_cga_palette(0);
}

/* The read cache's "caching" flag (src/xmem.h), NULL = no cache: the
   intro (2 MB, read once) goes past it, or it would only push the packs
   out of a 1 MB cache and make the movie's reads slower. */
static unsigned char __far *cache_flag(void)
{
    union REGS r;
    struct SREGS s;

    segread(&s);
    r.x.ax = 0xD700;
    r.x.si = XMEM_MAGIC;
    r.x.di = 0;
    intdosx(&r, &r, &s);
    return r.x.ax == XMEM_MAGIC ? &((XmemHdr __far *)MK_FP(s.es, r.x.di))->on : NULL;
}

void cut_intro(int argc, char **argv)
{
    int i, want = 1, keys = 0, force = 0, timing = 0, slides = 0, end_test = -1;
    unsigned char __far *on;
    unsigned char was = 0;

    /* main.c calls this for campaigns only (no BATTLE, BENCH, ...). */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!stricmp(a, "NOINTRO"))
            want = 0;
        else if (!stricmp(a, "KEYS"))
            keys = 1;
        else if (!stricmp(a, "INTRO"))
            force = 1;
        else if (!stricmp(a, "INTROTIME"))
            force = timing = 1;         /* frame timings in INTRO.TXT */
        else if (!stricmp(a, "SLIDES"))
            slides = 1;                 /* scripts: the end slides only */
        else if (!stricmp(a, "ENDTEST") && i + 1 < argc)
            end_test = atoi(argv[++i]); /* tests: the slides at once, 1 won, 0 lost */
    }
    play_end = !keys || force || slides || end_test >= 0;
    if (want && (!keys || force)) {
        on = cache_flag();
        if (on) {
            was = *on;
            *on = 0;
        }
        play_intro(timing);
        if (on)
            *on = was;
    }
    if (end_test >= 0) {
        uk_open();
        cut_end(end_test);
        uk_close();
    }
}

/* ------------------------------------------------------------------ */
/* The end slides */

int cut_will_end(void)
{
    int fd;

    if (!play_end)
        return 0;
    fd = open_pack("SLIDES");
    if (fd < 0)
        return 0;
    _dos_close(fd);
    return 1;
}

int cut_end(int won)
{
    unsigned char hdr[6];
    SlideRec rec;
    char far *text;                     /* 2 KB of stack: the captions go elsewhere */
    unsigned text_seg;
    int fd, i, first, last, stop = 0;
    unsigned got;

    if (!play_end)
        return 0;
    fd = open_pack("SLIDES");
    if (fd < 0)
        return 0;
    text_seg = alloc_top((TEXT_MAX + (g_vgx ? 768 : 0)) >> 4);  /* VGA: and the DAC */
    if (!text_seg || _dos_read(fd, hdr, 6, &got) || got != 6 || memcmp(hdr, "XEN1", 4)) {
        if (text_seg)
            _dos_freemem(text_seg);
        _dos_close(fd);
        return 0;
    }
    text = MK_FP(text_seg, 0);
    first = won ? 0 : hdr[5];
    last = won ? hdr[5] : hdr[4];
    log_printf("END %s slides", won ? "victory" : "defeat");
    snd_free();
    mus_play(won ? MUS_WIN : MUS_LOSE, 1);
    mouse_hide();
    buttons = mouse_buttons();
    for (i = first; i < last && !stop; i++) {
        unsigned long t0;
        char *p, *e;
        int y;
        if (seek(fd, 6 + (long)sizeof rec * i) || _dos_read(fd, &rec, sizeof rec, &got)
            || got != sizeof rec)
            break;
        seek(fd, rec.text);
        _dos_read(fd, text, TEXT_MAX - 1, &got);
        text[got] = 0;
        seek(fd, rec.pic);
        if (g_vgx) {
            /* SLIDESV.PAK: the DAC, then the picture a byte a pixel
               (tools/intro.py --vga), shown black until it is all there. */
            vgx_slide(NULL, 0);
            if (_dos_read(fd, text + TEXT_MAX, 768, &got) || got != 768
                || _dos_read(fd, MK_FP(0xA000, 0), 64000u, &got) || got != 64000u)
                break;
            vgx_slide(text + TEXT_MAX, rec.colour);
            rec.colour = 14;
        } else {
            uk_cga_palette(rec.cga_pal);
            if (!read_full(fd))
                break;
        }
        /* The caption, wrapped by the converter, 8 pixels a line. */
        y = rec.y;
        for (p = text; *p; p = e + 1) {
            int last_line;
            e = strchr(p, '\n');
            if (!e)
                e = p + strlen(p);
            last_line = !*e;
            *e = 0;
            uk_text(rec.x, y, UK_SMALL, rec.colour, p);
            if (last_line)
                break;
            y += 8;
        }
        log_printf("END slide %d", i - first + 1);
        hgc_flush(0);
        t0 = timer_read();
        for (;;) {
            unsigned k = poll_skip();
            if (k) {
                stop = (k & 0xFF) == 27;        /* Esc: the slideshow ends */
                break;
            }
            if (timer_read() - t0 >= 30UL * 1193182UL)
                break;
        }
    }
    _dos_close(fd);
    _dos_freemem(text_seg);
    mus_stop();
    clear_screen();
    if (g_vgx) {                /* black; the next screen's picture sets its palette */
        vgx_slide(NULL, 0);
        _fmemset(MK_FP(0xA000, 0), 0, 64000u);
    }
    uk_cga_palette(0);
    uk_invalidate();                    /* the next menu draws its screen */
    snd_set_geo();
    return 1;
}
