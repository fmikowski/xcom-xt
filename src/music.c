/* SPDX-License-Identifier: GPL-3.0-or-later */
/* AdLib music: a port of the X-COM AdLib driver as reverse engineered for
   OpenXcom (src/Engine/Adlib/adlplayer.cpp, "X-COM Adlib Player by
   Volutar", GPL-3.0-or-later), cut down to the 9 voices of one OPL2. */

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "music.h"
#include "sound.h"
#include "timer.h"

int g_opl;
unsigned long g_mus_ticks, g_mus_counts;
unsigned g_mus_max;

#ifndef VOICES
#define VOICES  9               /* one OPL2 */
#endif
#define OPL     0x388

/* Note frequencies (F-number) and octaves: the first 12 notes use their own
   row, every later octave repeats the second. */
static const unsigned freq_lo[12] = {
    0x0B5, 0x0C0, 0x0CC, 0x0D8, 0x0E5, 0x0F2, 0x101, 0x110, 0x120, 0x131, 0x143, 0x157 };
static const unsigned freq_hi[12] = {
    0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287, 0x2AE };
static const signed char detune[12] = { 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5 };
static const unsigned char instr_order[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 9 };
static const unsigned char op1_of[12] = { 0, 1, 2, 8, 9, 10, 16, 17, 18, 24, 25, 26 };

/* Sizes are powers of two: the 8088 has no fast multiply for indexing. */
typedef struct {
    unsigned char note, instr, sample, hifreq, volume, pad;
    int duration;
} Voice;

typedef struct {
    unsigned char sample, prev_cmd, volume;
    int pitch;
    unsigned due;               /* sequencer tick of the next event */
    unsigned cur, start, ret;   /* offsets into the track, 0 = none */
    unsigned char pad[3];
} Instr;

/* Instrument ("sample") record in the track: 24 bytes, 11 used. */
typedef struct {
    unsigned char r20_1, r20_2, r40_1, r40_2, r60_1, r60_2, r80_1, r80_2,
                  rE0_1, rE0_2, rC0;
} Patch;

static Voice voices[VOICES];
static Instr ins[16];
static unsigned char chorus[16];
static unsigned subtrack[128];
static unsigned char *trk;
static unsigned trk_seg;        /* trk's own DOS block */
static unsigned char trk_heap;  /* trk from a hole in the C heap */
static unsigned trk_len, samples_at;
static int tempo, tempo_run;
static unsigned now_tick, next_due;     /* sequencer ticks (wrap around) */
static unsigned char act[16], n_act;    /* instruments in use, driver order */
static volatile unsigned char playing, looping, muted;
static unsigned char shadow[256];
/* FM sound effect: the voice it borrows (0xFF none); main code sets
   opl_lock while it writes the OPL itself. */
static volatile unsigned char fx_voice = 0xFF, fx_req, opl_lock;

#ifndef TEMPO_INC
#define TEMPO_INC 73            /* OpenXcom: 70 at its 70 Hz tick */
#endif
#define VOLUME 127

static struct { unsigned long off; unsigned len; } index_[N_MUS];

/* ------------------------------------------------------------------ */
/* OPL2 */

/* Status reads after an address write (3.3 us) and a data write (23 us):
   6 and 35 by the book, fewer when one read takes longer (calibrated). */
static unsigned reads_addr = 6, reads_data = 35;

static void opl_wait(unsigned n)
{
    while (n--)
        inp(OPL);
}

static void opl_out(unsigned char reg, unsigned char val)
{
    outp(OPL, reg);
    opl_wait(reads_addr);
    outp(OPL + 1, val);
    opl_wait(reads_data);
    shadow[reg] = val;
}

/* Times 512 status reads; keeps 50 % margin over the required delays. */
static void opl_calibrate(void)
{
    unsigned long t0 = timer_read(), ns;

    opl_wait(512);
    ns = (timer_read() - t0) * 838UL / 512;     /* per read */
    if (!ns)
        ns = 1;
    reads_addr = (unsigned)(4950UL / ns + 1);
    reads_data = (unsigned)(34500UL / ns + 1);
    if (reads_addr > 6)
        reads_addr = 6;
    if (reads_data > 35)
        reads_data = 35;
}

/* Skips writes that would not change the register. */
static void opl(unsigned char reg, unsigned char val)
{
    if (shadow[reg] != val)
        opl_out(reg, val);
}

static int opl_detect(void)
{
    unsigned s1, s2;
    unsigned long t0;

    opl_out(4, 0x60);
    opl_out(4, 0x80);
    s1 = inp(OPL);
    opl_out(2, 0xFF);
    opl_out(4, 0x21);
    t0 = timer_read();
    while (timer_read() - t0 < 300)     /* timer 1 overflows after 80 us */
        ;
    s2 = inp(OPL);
    opl_out(4, 0x60);
    opl_out(4, 0x80);
    return (s1 & 0xE0) == 0 && (s2 & 0xE0) == 0xC0;
}

static void adlib_init(void)
{
    unsigned r;

    for (r = 1; r < 0xF5; r++)
        opl_out((unsigned char)r, 0);
    opl_out(0x04, 0x60);
    opl_out(0x04, 0x80);
    opl_out(0x01, 0x20);        /* waveform select */
    opl_out(0xA8, 0x01);
    opl_out(0x08, 0x40);        /* note select */
    opl_out(0xBD, 0xC0);        /* vibrato / AM depth */
}

static void set_amplitude(int v, int value)
{
    opl(0x43 + op1_of[v], ~(value >> 1) & 0x3F);
}

static void clear_voices(void)
{
    int v;

    for (v = 0; v < VOICES; v++) {
        voices[v].sample = 0xFF;
        voices[v].note = 0;
    }
}

static void reset_voices(void)
{
    int v;

    clear_voices();
    for (v = 0; v < VOICES; v++) {
        if (v == fx_voice)
            continue;
        opl(0xB0 + v, 0);
        set_amplitude(v, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Driver */

/* F-number with the instrument's pitch bend, octave in *oct (no division:
   that costs 160 cycles on the 8088). */
static unsigned pitched_freq(int note, int instr, int *oct)
{
    int pitch = ins[instr].pitch, n = note, o = 0, prev;
    unsigned f;

    while (n >= 12) {
        n -= 12;
        o++;
    }
    *oct = o ? o - 1 : 0;
    f = o ? freq_hi[n] : freq_lo[n];
    if (pitch > 0)
        f += detune[n] * pitch;
    else if (pitch < 0) {
        prev = n ? n - 1 : (note ? 11 : 0);
        f += detune[prev] * pitch;
    }
    return f;
}

static void set_instr_pitch(int instr, int pitch)
{
    int v;

    ins[instr].pitch = pitch;
    for (v = 0; v < VOICES; v++) {
        int note = voices[v].note;
        if (note && voices[v].instr == instr) {
            int oct;
            unsigned f = pitched_freq(note, instr, &oct);
            unsigned char hf = (unsigned char)(((f >> 8) & 3) | (oct << 2));
            opl(0xA0 + v, f & 0xFF);
            voices[v].hifreq = hf;
            opl(0xB0 + v, hf | 0x20);
        }
    }
}

/* Free voice, else the one playing longest. */
static int unused_voice(int sample, int *same)
{
    int v, best = 0, maxdur = 0;

    for (v = 0; v < VOICES; v++)
        voices[v].duration++;
    for (v = 0; v < VOICES; v++) {
        if (v == fx_voice)
            continue;
        if (voices[v].duration > maxdur) {
            maxdur = voices[v].duration;
            best = v;
        }
        if (voices[v].note == 0) {
            best = v;
            break;
        }
    }
    if (voices[best].sample == sample)
        *same = 1;
    else
        voices[best].sample = (unsigned char)sample;
    voices[best].duration = 0;
    return best;
}

static void play_note(int note, int volume, int instr)
{
    int sample = ins[instr].sample, v, op, same = 0, oct;
    const Patch *p = (const Patch *)(trk + samples_at + sample * 24);
    unsigned f;
    unsigned char hf;

    note--;
    if (note < 0 || note >= 108)
        return;
    if (volume == 0) {
        for (v = 0; v < VOICES; v++)
            if (voices[v].note == note && voices[v].instr == instr && v != fx_voice) {
                voices[v].note = 0;
                opl(0xB0 + v, voices[v].hifreq);        /* key off */
            }
        return;
    }
    if (volume > 127)
        volume = 127;
    v = unused_voice(sample, &same);
    voices[v].volume = (unsigned char)volume;
    voices[v].note = (unsigned char)note;
    voices[v].instr = (unsigned char)instr;
    op = op1_of[v];
    if (!same) {
        opl(0x20 + op, p->r20_1);
        opl(0x23 + op, p->r20_2);
        opl(0x40 + op, (~p->r40_1 & 0x3F) | (p->r40_1 & 0xC0));
    }
    opl(0xB0 + v, voices[v].hifreq);
    opl(0x43 + op, ~((VOLUME * volume) >> 8) & 0x3F);
    if (!same) {
        opl(0x60 + op, p->r60_1);
        opl(0x63 + op, p->r60_2);
        opl(0x80 + op, p->r80_1);
        opl(0x83 + op, p->r80_2);
        opl(0xE0 + op, p->rE0_1);
        opl(0xE3 + op, p->rE0_2);
        opl(0xC0 + v, p->rC0 ^ 1);
    }
    f = pitched_freq(note, instr, &oct);
    hf = (unsigned char)(((f >> 8) & 3) | (oct << 2));
    opl(0xA0 + v, f & 0xFF);
    voices[v].hifreq = hf;
    opl(0xB0 + v, hf | 0x20);   /* key on */
}

static int free_voice(void)
{
    int v;

    for (v = 0; v < VOICES; v++)
        if (!voices[v].note && v != fx_voice)
            return 1;
    return 0;
}

static unsigned numseq(unsigned *p)
{
    unsigned v = 0;
    unsigned char c;

    do {
        c = trk[(*p)++];
        v = (v << 7) + (c & 0x7F);
    } while (c & 0x80);
    return v;
}

static int decode(int i, int *another)
{
    Instr *in = &ins[i];
    unsigned p = in->cur;
    unsigned char op, a1, a2;
    int delay, c;

    do {
        op = trk[p++];
        if (op == 0xFE) {               /* call subtrack */
            a1 = trk[p++];
            in->ret = p;
            p = subtrack[a1 & 127];
        } else if (op == 0xFD) {        /* return */
            if (in->ret) {
                p = in->ret;
                in->ret = 0;
            }
        } else if (op == 0xFF) {        /* end of track */
            playing = 0;
            delay = 0;
            break;
        } else if (op >= 0x80) {
            in->prev_cmd = op;
            op = trk[p++];
        }
        if (op < 0x80) {                /* running status */
            a1 = op;
            c = chorus[i];
            switch (in->prev_cmd & 0xF0) {
            case 0x80:
                p++;
                play_note(a1, 0, i);
                if (c)
                    play_note(a1, 0, c);
                break;
            case 0x90:
                a2 = trk[p++];
                if (a2 == 0) {
                    play_note(a1, 0, i);
                    if (c)
                        play_note(a1, 0, c);
                } else {
                    int vol = (a2 * in->volume) >> 7;
                    if (c && free_voice()) {
                        ins[c].sample = in->sample;
                        ins[c].pitch = in->pitch - 1;
                        play_note(a1, vol, c);
                    }
                    play_note(a1, vol, i);
                }
                break;
            case 0xB0:
                a2 = trk[p++];
                if (a1 == 0 && a2)
                    tempo = a2 * 4 / 5;
                else if (a1 == 7)
                    in->volume = a2;
                else if (a1 == 0x7E)
                    chorus[i] = (a2 - 1) & 15;
                else if (a1 == 0x7F)
                    chorus[i] = 0;
                break;
            case 0xC0:
                if (a1 == 0x7E)
                    *another = 1;
                else
                    in->sample = a1;
                break;
            case 0xE0:
                set_instr_pitch(i, a1 - 16);
                if (c)
                    set_instr_pitch(c, a1 - 17);
                break;
            }
        }
        delay = numseq(&p);
    } while (delay == 0);
    in->cur = p;
    return delay;
}

/* Subtrack and instrument tables of a loaded track. */
static void parse_track(void)
{
    unsigned p = 0, i, n, len;
    int titled = trk[0] <= 56;

    for (i = 0; i < 16; i++)
        ins[i].start = 0;
    if (titled)
        p += trk[0] + 1;
    tempo = trk[p++];
    samples_at = p + 1;
    p += trk[p] * 24 + 1;
    n = trk[p++];
    for (i = 0; i < n && i < 128; i++) {
        len = *(unsigned *)(trk + p);
        subtrack[i] = p + 4;
        p += len;
    }
    n = trk[p++];
    for (i = 0; i < n; i++) {
        unsigned j = i;
        len = *(unsigned *)(trk + p);
        if (titled) {
            j = trk[p + 4] > 15 ? 15 : trk[p + 4];
            ins[j].start = p + 5;
        } else if (j < 16) {
            ins[j].start = p + 4;
        }
        p += len;
        if (p >= trk_len)
            break;
    }
}

/* Restarts every instrument at the current tick. */
static void rewind_track(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        ins[i].pitch = 0;
        ins[i].ret = 0;
        chorus[i] = 0;
        ins[i].cur = ins[i].start;
        if (ins[i].start)
            ins[i].due = now_tick + numseq(&ins[i].cur);
    }
}

/* One sequencer tick. The driver counts every instrument's delay down each
   tick; here each keeps the tick it is due at, and ticks without an event
   only compare two numbers (the 8088 cannot afford a 16-voice scan at
   50 Hz). */
static void seq_tick(void)
{
    int another, k;
    unsigned d, best;

    if ((int)(next_due - now_tick) > 0) {
        now_tick++;
        return;
    }
    do {
        another = 0;
        for (k = 0; k < n_act; k++) {
            int i = act[k];
            if (ins[i].cur && ins[i].due == now_tick) {
                ins[i].due = now_tick + decode(i, &another);
                if (!playing)
                    break;
            }
        }
        if (!another && playing)
            break;
        rewind_track();
        clear_voices();
    } while (another);
    if (!playing) {
        /* Rewound at the end of the track: the driver starts the delays
           counting on the next tick. */
        for (k = 0; k < n_act; k++)
            ins[act[k]].due++;
        reset_voices();
        playing = looping;
    }
    best = 0x7FFF;
    for (k = 0; k < n_act; k++)
        if (ins[act[k]].cur) {
            d = ins[act[k]].due - now_tick;
            if (d < best)
                best = d;
        }
    next_due = now_tick + best;
    now_tick++;
}

static unsigned pit_count(void)
{
    unsigned lo, hi;

    outp(0x43, 0x00);
    lo = inp(0x40);
    hi = inp(0x40);
    return (hi << 8) | lo;
}

/* ------------------------------------------------------------------ */
/* FM sound effects (AdLib without digital audio): one voice borrowed from
   the music while an effect sounds (a free one if the track leaves one,
   else the note playing longest is cut), given back after its release. A
   recipe is a patch plus a pitch sweep, stepped by the timer hook. */

typedef struct {
    unsigned char m20, c20, m40, c40, m60, c60, m80, c80, mE0, cE0, c0;
} FxPatch;

static const FxPatch fx_patch[8] = {
    { 0x0C, 0x00, 0x00, 0x00, 0xF8, 0xF5, 0x0F, 0x36, 0, 0, 0x0E },  /* 0 noise burst */
    { 0x21, 0x21, 0x18, 0x00, 0xF0, 0xF0, 0x07, 0x07, 0, 0, 0x08 },  /* 1 beam */
    { 0x22, 0x21, 0x10, 0x00, 0xF2, 0xF3, 0x05, 0x05, 1, 0, 0x0C },  /* 2 plasma */
    { 0x0F, 0x00, 0x00, 0x00, 0xF4, 0xF2, 0x04, 0x13, 0, 0, 0x0E },  /* 3 boom */
    { 0x61, 0x61, 0x14, 0x00, 0x95, 0x75, 0x15, 0x15, 2, 0, 0x0A },  /* 4 voice */
    { 0x01, 0x01, 0x08, 0x00, 0xF8, 0xF8, 0xF8, 0x68, 0, 0, 0x04 },  /* 5 thud */
    { 0x0F, 0x0F, 0x00, 0x04, 0x6F, 0x8F, 0x07, 0x08, 0, 0, 0x0E },  /* 6 hiss */
    { 0x27, 0x01, 0x20, 0x00, 0xF3, 0xF2, 0x14, 0x13, 0, 0, 0x00 },  /* 7 bell */
};

/* Per effect (sound.h order): patch, start pitch / 16, pitch step per tick,
   ticks until key off, tick the step changes, second step, random pitch
   mask, priority << 4 | carrier attenuation / 4. Pitch: 512 an octave,
   0 = 24.3 Hz. */
#define N(x) ((unsigned char)(x))
static const unsigned char fx_recipe[N_SFX][8] = {
    { 0, 120, N(-40),  6,  3, N(-20), 0x7F, 0x20 },   /* rifle */
    { 0, 100, N(-40),  9,  4, N(-16), 0x7F, 0x20 },   /* cannon */
    { 1, 200, N(-60), 10, 10,   0, 0x00, 0x22 },   /* laser */
    { 2, 150, N(-30), 12,  4,  20, 0x1F, 0x20 },   /* plasma */
    { 0,  90, N(-30),  4,  4,   0, 0x7F, 0x22 },   /* bullet hit */
    { 2, 170, N(-50),  6,  6,   0, 0x3F, 0x22 },   /* beam hit */
    { 3,  70,  N(-6), 30,  6,  N(-2), 0xFF, 0x20 },   /* explosion */
    { 4, 175,  20, 22,  6, N(-18), 0x0F, 0x20 },   /* sectoid */
    { 6, 210,  N(-4), 24, 24,   0, 0xFF, 0x22 },   /* snakeman */
    { 4,  95,   8, 26,  6, N(-10), 0x07, 0x20 },   /* muton */
    { 4, 160,   4, 32, 16,  N(-8), 0x00, 0x20 },   /* ethereal */
    { 4, 125,  12, 22,  5, N(-12), 0x07, 0x20 },   /* man */
    { 4, 150,  12, 22,  5, N(-12), 0x07, 0x20 },   /* woman */
    { 5,  90,  N(-6),  8,  2,  10, 0x00, 0x12 },   /* door */
    { 5,  50,  N(-8),  2,  2,   0, 0x00, 0x06 },   /* step */
    { 6, 180,  10,  6,  6,   0, 0x3F, 0x14 },   /* throw */
    { 7, 170,   0,  8,  4,  16, 0x00, 0x22 },   /* geoscape alert */
    { 0, 110, N(-30),  5,  2, N(-10), 0x7F, 0x22 },   /* craft cannon */
    { 6, 120,  12, 18,  6,  N(-4), 0x7F, 0x22 },   /* craft missile */
    { 1, 230, N(-40), 12, 12,   0, 0x0F, 0x22 },   /* UFO fire */
    { 0,  85, N(-20),  6,  6,   0, 0xFF, 0x20 },   /* UFO hit */
    { 3,  60,  N(-3), 40, 10,  N(-1), 0xFF, 0x20 },   /* UFO crash */
    { 3,  75,  N(-5), 30,  8,  N(-2), 0xFF, 0x20 },   /* craft explodes */
};

static const unsigned char *fx_e;
static int fx_p;
static unsigned char fx_age, fx_tail, fx_prio;
static unsigned fx_seed = 1;

/* Gives the voice back to the music (its next note sets the patch). */
static void fx_release(void)
{
    unsigned char v = fx_voice;

    if (v == 0xFF)
        return;
    opl(0xB0 + v, shadow[0xB0 + v] & 0x1F);
    opl(0x43 + op1_of[v], 0x3F);
    voices[v].sample = 0xFF;
    voices[v].note = 0;
    fx_voice = 0xFF;
}

/* A free voice, else the music note playing longest. */
static unsigned char fx_take(void)
{
    int v, best = VOICES - 1, maxdur = -1;

    if (!playing)
        return VOICES - 1;
    for (v = VOICES - 1; v >= 0; v--) {
        if (!voices[v].note)
            return (unsigned char)v;
        if (voices[v].duration > maxdur) {
            maxdur = voices[v].duration;
            best = v;
        }
    }
    opl(0xB0 + best, voices[best].hifreq);      /* key off */
    voices[best].note = 0;
    voices[best].sample = 0xFF;
    return (unsigned char)best;
}

static void fx_tick(void)
{
    unsigned char r = fx_req, v, op;
    const FxPatch *pt;
    int p;

    if (r) {
        const unsigned char *e = fx_recipe[r - 1];
        fx_req = 0;
        if (fx_voice == 0xFF || fx_tail || (e[7] >> 4) >= fx_prio) {
            v = fx_voice;
            if (v == 0xFF)
                v = fx_take();
            fx_voice = v;
            op = op1_of[v];
            pt = &fx_patch[e[0]];
            opl(0xB0 + v, 0);
            opl(0x20 + op, pt->m20);
            opl(0x23 + op, pt->c20);
            opl(0x40 + op, pt->m40);
            opl(0x43 + op, pt->c40 + ((e[7] & 15) << 2));
            opl(0x60 + op, pt->m60);
            opl(0x63 + op, pt->c60);
            opl(0x80 + op, pt->m80);
            opl(0x83 + op, pt->c80);
            opl(0xE0 + op, pt->mE0);
            opl(0xE3 + op, pt->cE0);
            opl(0xC0 + v, pt->c0);
            fx_e = e;
            fx_p = e[1] << 4;
            fx_age = 0;
            fx_tail = 0;
            fx_prio = e[7] >> 4;
        }
    }
    v = fx_voice;
    if (v == 0xFF)
        return;
    if (fx_tail) {
        if (--fx_tail == 0)
            fx_release();
        return;
    }
    if (fx_age >= fx_e[3]) {
        opl(0xB0 + v, shadow[0xB0 + v] & 0x1F);  /* key off: release */
        fx_tail = 24;
        return;
    }
    fx_p += (signed char)(fx_age < fx_e[4] ? fx_e[2] : fx_e[5]);
    if (fx_p < 0)
        fx_p = 0;
    else if (fx_p > 4095)
        fx_p = 4095;
    p = fx_p;
    if (fx_e[6]) {
        fx_seed = fx_seed * 25173u + 13849u;
        p += (int)((fx_seed >> 6) & fx_e[6]) - (fx_e[6] >> 1);
        if (p < 0)
            p = 0;
        else if (p > 4095)
            p = 4095;
    }
    /* F-number 512 + (p & 511), block p >> 9, key on */
    opl(0xA0 + v, (unsigned char)p);
    opl(0xB0 + v, (unsigned char)(0x22 | ((p >> 9) << 2) | ((p >> 8) & 1)));
    fx_age++;
}

void mus_fx(int sfx)
{
    if (g_opl && sfx >= 0 && sfx < N_SFX)
        fx_req = (unsigned char)(sfx + 1);
}

/* Timer hook: one driver tick at TIMER_HOOK_HZ. */
static void mus_tick(void)
{
    unsigned c0, c1;

    if (opl_lock)
        return;
    if (!g_sound_on) {
        if (!muted) {
            fx_req = 0;
            fx_release();
            reset_voices();
            muted = 1;
        }
        return;
    }
    muted = 0;
    if (fx_req || fx_voice != 0xFF)
        fx_tick();
    if (!playing)
        return;
    c0 = pit_count();
    tempo_run -= tempo;
    if (tempo_run <= 0) {
        tempo_run += TEMPO_INC;
        seq_tick();
    }
    c1 = pit_count();

    c0 = c0 >= c1 ? c0 - c1 : c0 + 16384u - c1;
    g_mus_ticks++;
    g_mus_counts += c0;
    if (c0 > g_mus_max)
        g_mus_max = c0;
}

/* ------------------------------------------------------------------ */

int mus_init(int music)
{
    FILE *f;
    unsigned char hdr[6 + 6 * N_MUS];
    int i, n;

    g_opl = opl_detect();
    if (!g_opl)
        return -1;
    opl_calibrate();
    {
        unsigned long t0 = timer_read();
        int k;
        for (k = 0; k < 64; k++)
            opl_out(0x08, 0x40);
        t0 = (timer_read() - t0) * 838UL / 64 / 1000;
        log_printf("music: OPL2, %u + %u status reads per write, %lu us per write", reads_addr, reads_data, t0);
    }
    adlib_init();
    f = music ? fopen("MUSIC.PAK", "rb") : NULL;
    if (f) {
        n = fread(hdr, 1, sizeof hdr, f) >= 6 ? *(unsigned *)(hdr + 4) : 0;
        if (hdr[0] == 'X' && hdr[1] == 'M' && hdr[2] == 'U' && hdr[3] == '1')
            for (i = 0; i < n && i < N_MUS; i++) {
                index_[i].off = *(unsigned long *)(hdr + 6 + 6 * i);
                index_[i].len = *(unsigned *)(hdr + 10 + 6 * i);
            }
        fclose(f);
    }
    timer_hook(mus_tick);
    return 0;
}

void mus_shutdown(void)
{
    if (!g_opl)
        return;
    mus_stop();
    timer_hook(0);
    adlib_init();
}

unsigned mus_size(int track)
{
    return g_opl && track >= 0 && track < N_MUS ? index_[track].len : 0;
}

void mus_stop(void)
{
    playing = 0;
    if (!trk)
        return;
    opl_lock = 1;
    reset_voices();
    opl_lock = 0;
    if (trk_seg)
        _dos_freemem(trk_seg);
    if (trk_heap) {
        free(trk);
        _fheapshrink();         /* free heap segments back to DOS */
    }
    trk_seg = 0;
    trk_heap = 0;
    trk = NULL;
}

/* Largest free DOS block, paragraphs. */
static unsigned dos_largest(void)
{
    union REGS r;

    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;
    intdos(&r, &r);
    return r.x.bx;
}

/* Else the track's memory comes straight from DOS, at the top of memory
   (last fit): grown into the C heap, a freed track stayed in the heap, a
   hole the next screen's DOS blocks (sprite banks) could not use. */
static unsigned track_alloc(unsigned len)
{
    union REGS r;
    unsigned strat, seg = 0;

    r.x.ax = 0x5800;
    intdos(&r, &r);
    strat = r.x.ax;
    r.x.ax = 0x5801;
    r.x.bx = 2;                 /* last fit */
    intdos(&r, &r);
    r.h.ah = 0x48;
    r.x.bx = (len + 15) >> 4;
    intdos(&r, &r);
    if (!r.x.cflag)
        seg = r.x.ax;
    r.x.ax = 0x5801;
    r.x.bx = strat;
    intdos(&r, &r);
    return seg;
}

int mus_play(int track, int loop)
{
    return mus_play_at(track, loop, NULL);
}

int mus_play_at(int track, int loop, unsigned char *buf)
{
    FILE *f;
    unsigned len = mus_size(track);
    int k;

    mus_stop();
    if (!len)
        return -1;
    if (!buf) {
        /* A hole in the C heap costs nothing; if the heap had to grow
           into the largest DOS block, a block at the top of memory is
           better (nothing gets stuck between the next screen's blocks). */
        unsigned before = dos_largest();
        buf = malloc(len);
        if (buf && dos_largest() < before) {
            free(buf);
            _fheapshrink();
            buf = NULL;
        }
        trk_heap = buf != NULL;
    }
    if (!buf) {
        trk_seg = track_alloc(len);
        if (!trk_seg)
            return -1;
        buf = MK_FP(trk_seg, 0);
    }
    trk = buf;
    f = fopen("MUSIC.PAK", "rb");
    if (!f || fseek(f, index_[track].off, SEEK_SET)
        || (FP_SEG(trk) != 0xFFFF && fread(trk, 1, len, f) != len)) {    /* (HMA: xmem.c read it) */
        if (f)
            fclose(f);
        if (trk_seg)
            _dos_freemem(trk_seg);
        if (trk_heap) {
            free(trk);
            _fheapshrink();
        }
        trk_seg = 0;
        trk_heap = 0;
        trk = NULL;
        return -1;
    }
    fclose(f);
    trk_len = len;
    memset(ins, 0, sizeof ins);
    parse_track();
    now_tick = next_due = 0;
    n_act = 0;
    for (k = 0; k < 16; k++)
        if (ins[instr_order[k]].start)
            act[n_act++] = instr_order[k];
    rewind_track();
    opl_lock = 1;
    reset_voices();
    opl_lock = 0;
    tempo = tempo * 2 / 5;
    tempo_run = tempo;
    looping = (unsigned char)loop;
    muted = 0;
    playing = 1;
    return 0;
}
