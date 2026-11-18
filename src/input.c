/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <bios.h>
#include <ctype.h>
#include <i86.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "timer.h"
#ifdef RDDIAG
#include <dos.h>
#include "xmem.h"

/* The read cache's header (src/xmemres.asm), NULL = none. */
static XmemHdr __far *xmem_hdr(void)
{
    union REGS r;
    struct SREGS s;

    segread(&s);
    r.x.ax = 0xD700;
    r.x.si = XMEM_MAGIC;
    r.x.di = 0;
    intdosx(&r, &r, &s);
    return r.x.ax == XMEM_MAGIC ? (XmemHdr __far *)MK_FP(s.es, r.x.di) : NULL;
}
#endif

FILE *g_script;          /* the KEYS file (src/cut.c peeks at it) */
unsigned g_rd_mark;
unsigned long g_rd_mark_bytes;
static unsigned repeat_key;
static int repeat_left;
static unsigned pending;        /* mouse click seen by input_ready */
int g_popup_auto;
int g_alert_pause;
int g_track;                    /* MOVE returns KEY_MOVE (input.h) */
static int hold_left;           /* HOLD: repeats left with the button down */
char g_log_path[32];             /* the LOG file, "" when none (input.h) */

static const struct { const char *name; unsigned key; } keys[] = {
    { "U", 0x4800 }, { "D", 0x5000 }, { "L", 0x4B00 }, { "R", 0x4D00 },
    { "HOME", 0x4700 }, { "END", 0x4F00 }, { "PGUP", 0x4900 }, { "PGDN", 0x5100 },
    { "TAB", 0x0F09 }, { "ENTER", 0x1C0D }, { "SPACE", 0x3920 }, { "ESC", 0x011B },
    { "PLUS", 0x0D2B }, { "MINUS", 0x0C2D },
    { "BKSP", 0x0E08 }, { "F1", 0x3B00 }, { "F2", 0x3C00 },
};

int input_script(const char *path)
{
    g_script = fopen(path, "r");
    return g_script != NULL;
}

static int next_token(char *buf, int size)
{
    int c, n = 0;

    do {
        c = fgetc(g_script);
    } while (c != EOF && isspace(c));
    while (c != EOF && !isspace(c)) {
        if (n < size - 1)
            buf[n++] = (char)toupper(c);
        c = fgetc(g_script);
    }
    buf[n] = 0;
    return n;
}

/* Reads the x y numbers after CLICK, RCLICK or MOVE. */
static void script_xy(void)
{
    char tok[16];
    int x, y;

    next_token(tok, sizeof tok);
    x = atoi(tok);
    next_token(tok, sizeof tok);
    y = atoi(tok);
    mouse_set(x < 0 ? 0 : x > 319 ? 319 : x, y < 0 ? 0 : y > 199 ? 199 : y);
}

static unsigned read_key(void)
{
    char tok[16];

    if (repeat_left > 0) {
        repeat_left--;
        return repeat_key;
    }
    while (g_script) {
        char *star;
        unsigned key = 0;
        int i, count = 1;

        if (!next_token(tok, sizeof tok)) {
            fclose(g_script);
            g_script = NULL;
            g_popup_auto = 0;
            log_printf("script finished");
            break;
        }
        star = strchr(tok, '*');
        if (star) {
            *star = 0;
            count = atoi(star + 1);
        }
        for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++)
            if (!strcmp(tok, keys[i].name))
                key = keys[i].key;
        if (!strcmp(tok, "AUTO") || !strcmp(tok, "NOAUTO")) {
            g_popup_auto = tok[0] == 'A';
            g_alert_pause |= g_popup_auto;
            continue;
        }
        if (!strcmp(tok, "OLDCLOCK") || !strcmp(tok, "DOSCLOCK")) {
            g_alert_pause = tok[0] == 'O';
            continue;
        }
        if (!strcmp(tok, "WAIT")) {
            /* WAIT n: n tenths of a second (BIOS ticks), e.g. for screenshots. */
            volatile unsigned long far *ticks = (unsigned long far *)MK_FP(0x40, 0x6C);
            unsigned long t0 = *ticks;
            next_token(tok, sizeof tok);
            while (*ticks - t0 < (unsigned long)atoi(tok) * 182 / 100)
                mouse_poll();
            continue;
        }
        if (!strcmp(tok, "HOLD")) {
            script_xy();
            next_token(tok, sizeof tok);
            hold_left = atoi(tok);
            log_printf("HOLD %d %d %d", g_mouse_x, g_mouse_y, hold_left);
            return KEY_LCLICK;
        }
        if (!strcmp(tok, "CLICK") || !strcmp(tok, "RCLICK") || !strcmp(tok, "MOVE")) {
            script_xy();
            if (tok[0] == 'M' && !g_track)
                continue;
            key = tok[0] == 'C' ? KEY_LCLICK : tok[0] == 'R' ? KEY_RCLICK : KEY_MOVE;
            log_printf("%s %d %d", tok, g_mouse_x, g_mouse_y);
        }
        if (!key && tok[0] && !tok[1])
            key = (unsigned char)tolower(tok[0]);
        if (!key && tok[0] == '\'' && tok[1])     /* 'x = the letter x */
            key = (unsigned char)tolower(tok[1]);
        if (!key || count < 1)
            continue;
        repeat_key = key;
        repeat_left = count - 1;
        return key;
    }
    if (pending) {
        unsigned k = pending;
        pending = 0;
        return k;
    }
    for (;;) {
        unsigned k;
        if (_bios_keybrd(_KEYBRD_READY))
            return _bios_keybrd(_KEYBRD_READ);
        k = mouse_poll();
        if (k)
            return k;
    }
}

unsigned input_key(void)
{
    unsigned key;

    hold_left = 0;              /* any other input ends a scripted hold */
#ifdef GAPDIAG
    gap_mark(1);
    key = read_key();
    gap_mark(3);
#else
    key = read_key();
#endif
    busy_idle();
#ifdef OVLWATCH
    {
        static unsigned redo_seen;      /* a block: busy_idle() came before it */

        if (ovl_redo_count() != redo_seen) {
            redo_seen = ovl_redo_count();
            log_printf("OVL moved section %u with self-relocations (%u in all)",
                       ovl_redo_section(), redo_seen);
        }
    }
#endif
#ifdef RDDIAG
    {
        static unsigned r0, o0, s0[40], k0;
        static unsigned long b0, rt0, st0[40], cm0, ch0, cb0;
        char buf[320];
        int i, n;

        if (rd_count() != r0) {
            XmemHdr __far *xh = xmem_hdr();
            n = sprintf(buf, "RD %04x: %u reads %lu B opens %u (%Fs) %lu ms t%lu", k0, rd_count() - r0,
                        rd_bytes() - b0, rd_opens() - o0, rd_lastname(),
                        (rd_readtime() - rt0) / 1193UL, *(unsigned long far *)MK_FP(0x40, 0x6C));
            if (xh) {
                /* the cache: 4 KB blocks read from the disk / copied, disk bytes */
                n += sprintf(buf + n, " cache %lu/%lu %lu B", xh->miss - cm0, xh->hits - ch0,
                             xh->dbytes - cb0);
                cm0 = xh->miss;
                ch0 = xh->hits;
                cb0 = xh->dbytes;
            }
            for (i = 1; i < 40; i++)
                if (rd_secload(i) != s0[i]) {
                    n += sprintf(buf + n, " s%d*%u/%lums@%04x", i, rd_secload(i) - s0[i],
                                 (rd_sectime(i) - st0[i]) / 1193UL, rd_secseg(i, 3));
                    s0[i] = rd_secload(i);
                    st0[i] = rd_sectime(i);
                }
            log_printf("%s", buf);
            r0 = rd_count();
            b0 = rd_bytes();
            o0 = rd_opens();
            rt0 = rd_readtime();
        }
        k0 = key;
    }
#endif

    /* The caller is about to draw: no pointer until the next poll. */
    mouse_hide();
    return key;
}

/* 1 while the script still has tokens; at its end closes it, so that
   input_ready does not promise a key that only the keyboard can give
   (an air combat polling input_ready would then wait for a key). */
static int script_left(void)
{
    int c;

    if (!g_script)
        return 0;
    do {
        c = fgetc(g_script);
    } while (c != EOF && isspace(c));
    if (c != EOF) {
        ungetc(c, g_script);
        return 1;
    }
    fclose(g_script);
    g_script = NULL;
    g_popup_auto = 0;
    log_printf("script finished");
    return 0;
}

int input_held(unsigned ms)
{
    unsigned long t0;

    if (hold_left > 0) {
        hold_left--;
        return 1;
    }
    if (g_script || !g_mouse)
        return 0;
    t0 = timer_read();
    while (timer_read() - t0 < (unsigned long)ms * 1193)
        if (!mouse_down())
            return 0;
    return mouse_down();
}

int input_ready(void)
{
    if (repeat_left > 0 || pending || script_left())
        return 1;
    pending = mouse_poll();
    if (pending)
        return 1;
    return _bios_keybrd(_KEYBRD_READY) != 0;
}

void log_open(const char *path)
{
    FILE *f;

    strncpy(g_log_path, path, sizeof g_log_path - 1);
    f = fopen(g_log_path, "w");
    if (f)
        fclose(f);
}

void log_printf(const char *fmt, ...)
{
    FILE *f;
    va_list ap;

    if (!g_log_path[0])
        return;
    /* Reopened per line so the file is complete even if the PC is reset. */
    f = fopen(g_log_path, "a");
    if (!f)
        return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
