/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The command line, read once at the start (main.c): an overlay
   section of its own (build.ps1), so that none of it stays resident. */

#include <dos.h>
#include <i86.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alien.h"
#include "base.h"
#include "battle.h"
#include "brief.h"
#include "effects.h"
#include "fx_in.h"
#include "geo.h"
#include "hwp.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "menu.h"
#include "music.h"
#include "pak.h"
#include "packver.h"
#include "sound.h"
#include "timer.h"
#include "unit.h"
#include "xmem.h"

/* main.c */
extern int g_quick_squad, g_aliens, g_bench;
extern unsigned long g_seed;
extern char campaign_race[13];

/* sndasm.asm: the XMS driver. */
unsigned long __cdecl xms_find(void);
void __cdecl xms_call(unsigned __far *r);

/* The Watcom dynamic overlay manager: one more area for its sections. */
void _ovl_addarea(unsigned seg, unsigned paras);

/* sndasm.asm */
unsigned long __cdecl xms_find(void);
void __cdecl xms_call(unsigned __far *r);

/* The largest free upper memory block, up to cap paragraphs, from DOS 5+
   (DOS=UMB: link the UMBs, allocate high only) or else from the XMS driver
   (function 10h: HIMEM + EMM386 without DOS=UMB, or an XT UMB driver); once
   one came from either, the next ones come from the same. */
static unsigned umb_get(unsigned cap, unsigned *paras)
{
    union REGS r;
    unsigned x[5], strat, link, seg = 0;

    if (!umb_xms) {
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
            if (!r.x.cflag) {           /* DOS has UMBs */
                r.x.ax = 0x5801;
                r.x.bx = 0x40;          /* high memory only, first fit */
                intdos(&r, &r);
                r.h.ah = 0x48;
                r.x.bx = 0xFFFF;
                intdos(&r, &r);
                *paras = r.x.bx < cap ? r.x.bx : cap;
                if (*paras) {
                    r.h.ah = 0x48;
                    r.x.bx = *paras;
                    intdos(&r, &r);
                    if (!r.x.cflag && (seg = r.x.ax) < 0xA000) {
                        _dos_freemem(seg);  /* not high after all */
                        seg = 0;
                    }
                }
                r.x.ax = 0x5801;
                r.x.bx = strat;
                intdos(&r, &r);
                r.x.ax = 0x5803;
                r.x.bx = link;
                intdos(&r, &r);
            }
        }
        if (seg || umb_seg)
            return seg;
    }
    if (xms_entry_ok != 1)
        return 0;
    x[0] = 0x1000;
    x[2] = cap;
    xms_call(x);
    if (x[0] != 1 && (x[1] & 0xFF) == 0xB0 && x[2]) {
        x[0] = 0x1000;              /* a smaller UMB is free */
        xms_call(x);
    }
    if (x[0] != 1)
        return 0;
    umb_xms = 1;
    *paras = x[2];
    return x[1];
}

/* Upper memory blocks (docs/design.md, "Upper memory"), taken before the
   start-up tables. The largest UMB's top holds the map buffer
   (MAP_MAX_TILES * 4 bytes) when the UMB has room for it and the battle's
   music track (44 KB): that leaves 39 KB more conventional memory for
   every battle, so the fallbacks for tight maps (an HWP left aboard, floor
   items as marks, music left out, city layouts rebuilt) are not needed.
   Sound keeps the bottom, the size of the battle's music track (with a
   Sound Blaster also 8 KB for effects loaded per screen); the rest of it,
   and every other UMB DOS or the XMS driver has, become overlay areas of
   the dynamic overlay manager (sections stay loaded: fewer disk reads). A
   UMB with less than 4 KB beyond the track, or NOUMB, is the sound's alone
   (at most 64 KB), as without the plan. xneed: paragraphs the XMS read
   cache wants for its code and table (src/xmem.c), taken from an overlay
   area's share, never from the tiers above it; returns their segment. */
#define UMB_SOUND   0x1000u     /* paragraphs: the most sound addresses */
#define OVL_MIN     0x100u      /* paragraphs: the smallest overlay area added */
#define MAP_PARAS   (unsigned)((MAP_MAX_TILES * 4UL + 15) >> 4)
#define TAB_PARAS   ((MAP_MAX_TILES + 15) >> 4)    /* the unit map, the sight flags */
#define OVL_BIG     0x800u      /* paragraphs: overlay area kept next to them */

static unsigned umb_plan(int no_umb, int snd_flags, unsigned xneed)
{
    unsigned char h[6 + 6 * N_MUS];
    unsigned n, mus = 0, map, tabs, ovl, seg, xseg = 0;
    FILE *f;

    xms_entry_ok = xms_find() ? 1 : 2;
    if (!(no_umb && (snd_flags & SND_QUIET)))
        umb_seg = umb_get(no_umb ? UMB_SOUND : 0xFFFFu, &umb_paras);
    if (umb_seg)
        log_printf("sound: UMB at %04X, %u KB (%s)", umb_seg, umb_paras / 64,
                   umb_xms ? "XMS" : "DOS");
    if (no_umb) {
        log_printf("UMB: NOUMB (sound only)");
        return 0;
    }
    if (!(snd_flags & (SND_QUIET | SND_NO_MUSIC)) && (f = fopen("MUSIC.PAK", "rb")) != NULL) {
        if (fread(h, 1, sizeof h, f) == sizeof h && !memcmp(h, "XMU1", 4)) {
            unsigned a = *(unsigned *)(h + 10 + 6 * MUS_TACTIC), b = *(unsigned *)(h + 10 + 6 * MUS_MARS);
            mus = ((a > b ? a : b) + 15) >> 4;
        }
        fclose(f);
    }
    if (getenv("BLASTER") && !(snd_flags & (SND_QUIET | SND_NO_SB)))
        mus += 8 * 64;          /* sound.c UMB_MIN */
    n = umb_paras;
    if (!umb_seg || n < mus + OVL_MIN)
        return 0;
    map = n - mus >= MAP_PARAS ? MAP_PARAS : 0;
    ovl = n - mus - map;
    /* A big UMB (96 KB+) also takes the unit map and the sight flags
       (19.5 KB more conventional memory: the XT whose UMB this big comes
       with an XMS driver low has 6 KB less of it) and keeps 32 KB or more
       of overlay area. */
    tabs = map && ovl >= 2 * TAB_PARAS + OVL_BIG ? 2 * TAB_PARAS : 0;
    ovl -= tabs;
    if (ovl < OVL_MIN)
        ovl = 0;                /* left to sound */
    umb_paras = n - map - tabs - ovl;
    umb_game = 1;
    seg = umb_seg + umb_paras;
    if (map) {
        g_map = (unsigned char *)MK_FP(seg + ovl + tabs, 0);
        _fmemset(g_map, 0, MAP_MAX_TILES * 4U);
    }
    if (tabs) {
        g_unit_at = (unsigned char *)MK_FP(seg + ovl, 0);
        g_los_flags = (unsigned char *)MK_FP(seg + ovl + TAB_PARAS, 0);
        _fmemset(g_unit_at, 0, 2 * TAB_PARAS * 16U);
    }
    if (xneed && ovl >= xneed + OVL_MIN) {
        xseg = seg;             /* the read cache's block, below the overlay area */
        seg += xneed;
        ovl -= xneed;
    }
    if (ovl)
        _ovl_addarea(seg, ovl);
    log_printf("UMB: %u paragraphs: map buffer at %04X (%u), tables %u, overlay area at %04X (%u), sound %u",
               n, map ? seg + ovl + tabs : 0, map, tabs, seg, ovl, umb_paras);
    while (n_more < UMB_MORE && (seg = umb_get(0xFFFFu, &n)) != 0) {
        umb_more[n_more++] = seg;
        if (xneed && !xseg && n >= xneed) {
            xseg = seg;
            seg += xneed;
            n -= xneed;
        }
        if (n >= OVL_MIN)
            _ovl_addarea(seg, n);
        log_printf("UMB: another at %04X, %u paragraphs: overlay area", seg, n);
    }
    return xseg;
}

/* ------------------------------------------------------------------ */
/* Lists sized by memory (docs/design.md, "Lists sized by memory")

   The battle's lists that DOS does not limit (smoke, fire, devices, floor
   items, the carry pool, terrain explosions) are taken from the near heap
   at the very start, before main() gives the rest of DGROUP's 64 KB back
   to DOS: DGROUP is then as large as the program needs, the lists are
   addressed through DS as before, and nothing breaks the memory up. Their
   sizes go from today's (MAX_*: a 640 KB XT) towards the *_TOP ones, in
   proportion, with the memory a machine has beyond the reference XT of
   the memory floors (86Box xt88adl: MS-DOS 5 and a mouse driver, LIM_REF
   paragraphs free here): half of what it has more, the other half for
   the battle's own fallbacks (HWPs, floor pictures, music), and
   LIM_UMB_GROW bytes of the 39 KB the map buffer in a UMB takes out of
   conventional memory (umb_plan, a UMB of LIM_UMB_MAP paragraphs or more):
   the worst battle measured with one, a terror site with a tank and a
   hovertank, has 14 KB left after the setup, and the floor pictures want
   12 KB. A UMB big enough for the unit map and the sight flags too
   (LIM_UMB_TABS: 19.5 KB more) gives the lists their top sizes. */
#define LIM_REF         23088u      /* paragraphs free here on the reference XT (23083, LOG "LIM:") */
#define LIM_UMB_GROW    1536        /* bytes the lists take with the map buffer in a UMB */
#define LIM_UMB_MAP     0xB00u      /* 44 KB: umb_plan's map buffer and music track */
#define LIM_UMB_TABS    0x1800u     /* 96 KB: also its unit map and sight flags */

static unsigned lim_free, lim_umb;      /* for the LOG line */

/* PUSH SP: the 8088, 8086, V20, V30 and 186 push SP after the decrement, the
   286 and up before it. Nonzero below the 286. */
int cpu_below_286(void);
#pragma aux cpu_below_286 = "push sp" "pop ax" "sub ax, sp" value [ax] modify exact [ax];

/* EMS, or XMS on a 286 (the picture store, docs/design.md "The 286: XMS
   budget"): a tight battle's walls, objects and unit sets go there, which
   gives it far more than the lists' top sizes cost (about 4.4 KB). Not
   with NOXMEM. */
static int lim_ext(int argc, char **argv)
{
    unsigned seg = *(unsigned __far *)MK_FP(0, 0x67 * 4 + 2);
    union REGS r;
    int i;

    for (i = 1; i < argc; i++)
        if (!stricmp(argv[i], "NOXMEM"))
            return 0;
    if (seg && !_fmemcmp(MK_FP(seg, 10), "EMMXXXX0", 8)) {
        r.h.ah = 0x40;
        int86(0x67, &r, &r);
        if (!r.h.ah)
            return 1;
    }
    return !cpu_below_286() && xms_find();
}

/* The largest UMB that DOS 5 (linked UMBs) or the XMS driver would give,
   in paragraphs; nothing is allocated. */
static unsigned umb_size(void)
{
    union REGS r;
    unsigned strat, link, n = 0, xr[5];

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
        r.x.ax = 0x5801;
        r.x.bx = 0x40;          /* upper memory only */
        intdos(&r, &r);
        r.h.ah = 0x48;
        r.x.bx = 0xFFFF;
        intdos(&r, &r);
        if (r.x.cflag)
            n = r.x.bx;
        r.x.ax = 0x5801;
        r.x.bx = strat;
        intdos(&r, &r);
        r.x.ax = 0x5803;
        r.x.bx = link;
        intdos(&r, &r);
    }
    if (!n && xms_find()) {
        xr[0] = 0x1000;         /* request a UMB of FFFFh paragraphs: DX = the largest */
        xr[1] = 0;
        xr[2] = 0xFFFF;
        xr[3] = xr[4] = 0;
        xms_call(xr);
        /* fails with BL B0h, DX = the largest; HIMEM without UMBs (a 286)
           answers BL 80h and leaves DX at FFFFh */
        if (!xr[0] && (xr[1] & 0xFF) == 0xB0)
            n = xr[2];
    }
    return n;
}

/* An entry count between today's and the top one (grow of room bytes). */
static int lim_size(unsigned base, unsigned top, unsigned long grow, unsigned long room)
{
    return (int)(base + (grow ? (unsigned)((top - base) * grow / room) : 0));
}

#define LIM_BYTES(s, f, d, fl, c, b) ((s) * sizeof(Spot) + (f) * sizeof(Spot) + (d) * sizeof(Device) \
                                      + (fl) * sizeof(FloorItem) + (c) * sizeof(Carry) + (b) * 3)

void lim_init(int argc, char **argv)
{
    union REGS r;
    unsigned long room = LIM_BYTES(SMOKE_TOP, FIRE_TOP, DEV_TOP, FLOOR_TOP, CARRY_TOP, BOOM_TOP)
                         - LIM_BYTES(MAX_SMOKE, MAX_FIRE, MAX_DEV, MAX_FLOOR, MAX_CARRY, MAX_BOOM),
                  grow = 0;
    unsigned char __near *p;
    unsigned n;
    int i;

    for (i = 1; i < argc && stricmp(argv[i], "NOUMB"); i++)
        ;
    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;
    intdos(&r, &r);
    lim_free = r.x.bx;
    lim_umb = i < argc ? 0 : umb_size();
    if (lim_free > LIM_REF)
        grow = (lim_free - LIM_REF) * 8UL;  /* half of it, in bytes */
    if (lim_umb >= LIM_UMB_TABS || lim_ext(argc, argv))
        grow = room;
    else if (lim_umb >= LIM_UMB_MAP)
        grow += LIM_UMB_GROW;
    if (grow > room)
        grow = room;
    max_smoke = lim_size(MAX_SMOKE, SMOKE_TOP, grow, room);
    max_fire = lim_size(MAX_FIRE, FIRE_TOP, grow, room);
    max_dev = lim_size(MAX_DEV, DEV_TOP, grow, room);
    g_max_floor = lim_size(MAX_FLOOR, FLOOR_TOP, grow, room);
    g_max_carry = lim_size(MAX_CARRY, CARRY_TOP, grow, room);
    g_max_boom = lim_size(MAX_BOOM, BOOM_TOP, grow, room);
    n = LIM_BYTES(max_smoke, max_fire, max_dev, g_max_floor, g_max_carry, g_max_boom);
    p = _nmalloc(n);
    if (!p) {
        printf("out of memory\n");
        exit(1);
    }
    memset(p, 0, n);
    smoke = (Spot __near *)p;
    fire = smoke + max_smoke;
    dev = (Device __near *)(fire + max_fire);
    g_floor = (FloorItem __near *)(dev + max_dev);
    g_floor_end = g_floor + g_max_floor;
    g_carry = (Carry __near *)g_floor_end;
    g_carry_end = g_carry + g_max_carry;
    g_boom = (unsigned char (__near *)[3])g_carry_end;
    /* The C start-up code lets DGROUP's near heap fill it up to 64 KB:
       the rest goes back to DOS. malloc takes DOS memory first anyway and
       falls back on the near heap only when DOS has none left. */
    _nheapshrink();
}

/* The packs must be from this version of the converter (PACKS.VER, written
   by XCOMCONV.EXE): checked once, here in the startup section, so that it
   costs no resident code or data. */
static void check_packs(void)
{
    char buf[32];
    int n = 0, len = (int)sizeof(PACK_MARKER_TEXT) - 1;
    FILE *f = fopen(PACK_MARKER_FILE, "rb");

    if (f) {
        n = (int)fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
    }
    buf[n > 0 ? n : 0] = 0;
    if (n <= len || memcmp(buf, PACK_MARKER_TEXT, len) || atoi(buf + len) != PACK_FORMAT_VERSION) {
        printf("Packs are from another version: run XCOMCONV.EXE\n");
        exit(1);
    }
    log_printf("packs format %d", PACK_FORMAT_VERSION);
}


/* The canvas's overlay area on a 286 without upper memory (docs/design.md
   "The canvas on the 286"): 8 KB of conventional memory for good, taken
   before anything else, so that canvas.c's section runs beside the battle's
   (cvinit.c g_cv_low). Not with NOXMEM, NOCVAREA or SET XCV=OFF, nor for
   CGA or Hercules (the canvas is EGA's and VGA's). */
#define CV_AREA     512u        /* cvpriv.h UMB_NEED */
extern unsigned g_cv_low;       /* cvinit.c */

static void cv_area(int argc, char **argv, int no)
{
    unsigned seg;
    const char *e = getenv("XCV");
    int i;

    for (i = 1; i < argc && !no; i++)
        no = !stricmp(argv[i], "NOXMEM") || !stricmp(argv[i], "NOCVAREA");
    if (no || umb_game || (e && !stricmp(e, "OFF")) || !xms_find() || _dos_allocmem(CV_AREA, &seg))
        return;
    _ovl_addarea(seg, CV_AREA);
    g_cv_low = CV_AREA;
    log_printf("canvas: overlay area at %04X (%u paragraphs)", seg, CV_AREA);
}

/* ------------------------------------------------------------------ */
/* The video mode and its folder (docs/design.md "Mode folders")

   v: C, E or V. The mode's folder (CGA\, EGA\, VGA\) counts when it holds its UI pack;
   flat: that pack in the game's folder. */
static int mode_has(char v, int flat)
{
    struct find_t ff;
    char name[12];

    strcpy(name, "?GA\\UI?.PAK");
    name[0] = name[6] = v;
    return !_dos_findfirst(name + (flat ? 4 : 0), _A_NORMAL, &ff);
}

#define VGA_XMS_KB  256u        /* free XMS (one block) the VGA version asks for */

extern char pack_names[64][13], race_names[8][13];     /* main.c */
extern int n_packs, n_races;

/* The quick battles' lists: mission packs TTnv.PAK, race packs Rxv.PAK, in the
   mode's folder (rd_dir), else in the game's. DOS ignores anything after * in a
   pattern, so they are filtered by hand. */
static void find_packs(char v)
{
    struct find_t ff;
    char pat[10];
    int k;

    strcpy(pat, "?GA\\*.PAK");
    pat[0] = v;
    n_packs = n_races = 0;
    for (k = rd_dir ? 0 : 4; k <= 4 && !n_packs && !n_races; k += 4) {
        if (_dos_findfirst(pat + k, _A_NORMAL, &ff))
            continue;
        do {
            int len = strchr(ff.name, '.') - ff.name;
            if (len < 3 || ff.name[len - 1] != v)
                continue;
            if (ff.name[0] == 'C' && ff.name[1] == 'Y')
                continue;                       /* Cydonia: only at the end */
            if (len == 4 && n_packs < 64)
                strcpy(pack_names[n_packs++], ff.name);
            else if (len == 3 && ff.name[0] == 'R' && ff.name[1] != 'X' && n_races < 8)
                strcpy(race_names[n_races++], ff.name);
        } while (_dos_findnext(&ff) == 0);
    }
}

/* Stops with the reason (on the screen and in the LOG). */
static void mode_stop(const char *msg)
{
    log_printf("%s", msg);
    printf("%s\n", msg);
    exit(1);
}

/* want: 0 none, 1 CGA, 2 HERC, 3 VGA, 4 EGA. Sets rd_dir; returns gfx_init's want
   (0: EGA if the card is there, else Hercules or CGA as gfx_init finds). */
static int mode_pick(int want, int below, int argc, char **argv, int need, int need_race)
{
    union REGS r;
    unsigned x[5], xk = 0;
    int vga, ega, any, i, forced = want;
    char v, msg[64];
    const char *why;

    r.x.ax = 0x1A00;            /* display combination: AL 1Ah, BL 7 / 8 on a VGA */
    int86(0x10, &r, &r);
    vga = r.h.al == 0x1A && (r.h.bl == 7 || r.h.bl == 8);
    r.h.ah = 0x12;              /* EGA information: BL stays 10h without an EGA BIOS */
    r.h.bl = 0x10;
    int86(0x10, &r, &r);
    ega = r.h.bl != 0x10;
    for (i = 1; i < argc && stricmp(argv[i], "NOXMEM"); i++)
        ;
    if (xms_find()) {
        x[0] = 0x0800;          /* free extended memory: AX the largest block (KB) */
        x[1] = x[2] = x[3] = x[4] = 0;
        xms_call(x);
        xk = x[0];
    }
    any = mode_has('C', 0) || mode_has('E', 0) || mode_has('V', 0);
    why = !vga ? "no VGA card" : below ? "VGA card, below 286"
        : !mode_has('V', 0) ? "VGA card, no VGA folder"
        : xk < VGA_XMS_KB || i < argc ? "VGA card, too little XMS" : "VGA card, 286 or better, VGA folder";
    if (want) {
        v = want == 3 ? 'V' : want == 4 ? 'E' : 'C';
        if (want == 4 && !ega)
            mode_stop("EGA: no EGA or VGA card found.");
        if (any && !mode_has(v, 0) && !mode_has(v, 1)) {
            sprintf(msg, "%s: the %cGA folder is missing (%cGA\\UI%c.PAK).",
                    "CGA\0 HERC\0VGA\0 EGA" + 5 * (want - 1), v, v, v);
            mode_stop(msg);
        }
    } else if (vga && !below && mode_has('V', 0) && xk >= VGA_XMS_KB && i >= argc) {
        v = 'V';
        want = 3;
    } else if (ega && (!any || mode_has('E', 0) || mode_has('E', 1)))
        v = 'E';
    else if (!any || mode_has('C', 0) || mode_has('C', 1)) {
        v = 'C';
        want = ega;             /* an EGA card without the E packs: CGA */
    } else
        mode_stop(ega ? "No packs for this machine's video: the EGA or CGA folder is missing."
                      : "No packs for this machine's video: the CGA folder is missing.");
    rd_dir = mode_has(v, 0) ? v : 0;
    log_printf("mode: %s (%s; %s; %u KB XMS), packs in %s", v == 'V' ? "VGA" : v == 'E' ? "EGA"
               : want == 2 ? "HERC" : want || (*(unsigned char far *)MK_FP(0x40, 0x10) & 0x30) != 0x30
               ? "CGA" : "HERC (mono card)", forced ? "forced" : "auto", why, xk,
               !rd_dir ? "the game folder" : v == 'V' ? "VGA\\" : v == 'E' ? "EGA\\" : "CGA\\");
    if (want == 4)
        want = 0;
    find_packs(v);
    if ((!n_packs && need) || (!n_races && need_race)) {
        sprintf(msg, "Mission or race packs (*%c.PAK) missing.", v);
        mode_stop(msg);
    }
    return want;
}

int main_options(int argc, char **argv, const char **forced_, const char **forced_race_,
                 int *want_cga_, int *snd_flags_)
{
    const char *forced = NULL, *forced_race = NULL;
    int i, want_cga = 0, quick = 0, snd_flags = 0, no_umb = 0, below = cpu_below_286();

    g_exact = !below;           /* DOS's lines cost an XT minutes a turn (design.md) */

    /* XCOMXT [BATTLE] [BENCH] [CGA | EGA | HERC | VGA] [MISSION name] [RACE name] [SEED n]
              [KEYS file] [LOG file] [ALIENS n] [DIFF n] [NOSB] [NOMUSIC] [NOFMSFX] [NOSOUND] [NOUMB] [EXACTAI] [NOEXACTAI]
              [DEBUG] [GEAR] [PSI] [CRAFT S|L|A] [SQUAD n] */
    for (i = 1; i < argc; i++) {
        if (!stricmp(argv[i], "BENCH"))
            g_bench = 1;
        else if (!stricmp(argv[i], "BATTLE"))
            quick = 1;
        else if (!stricmp(argv[i], "SEED") && i + 1 < argc)
            g_seed = strtoul(argv[++i], NULL, 10);
        else if (!stricmp(argv[i], "KEYS") && i + 1 < argc)
            input_script(argv[++i]);
        else if (!stricmp(argv[i], "LOG") && i + 1 < argc)
            log_open(argv[++i]);
        else if (!stricmp(argv[i], "CGA"))
            want_cga = 1;
        else if (!stricmp(argv[i], "HERC"))
            want_cga = 2;               /* Hercules (gfx_init) */
        else if (!stricmp(argv[i], "VGA"))
            want_cga = 3;               /* the VGA prototype's battle view (vgainit.c) */
        else if (!stricmp(argv[i], "EGA"))
            want_cga = 4;               /* EGA forced (mode_pick) */
        else if (!stricmp(argv[i], "MONTH") && i + 1 < argc)
            g_months_played = atoi(argv[++i]);  /* quick battles: the alien item level row */
        else if (!stricmp(argv[i], "ALIENS") && i + 1 < argc)
            g_aliens = atoi(argv[++i]);
        else if (!stricmp(argv[i], "MISSION") && i + 1 < argc)
            forced = argv[++i];
        else if (!stricmp(argv[i], "RACE") && i + 1 < argc)
            forced_race = argv[++i];
        else if (!stricmp(argv[i], "ALIENBASE"))
            g_alien_test |= 1;
        else if (!stricmp(argv[i], "CYDONIA"))
            g_alien_test |= 2;
        else if (!stricmp(argv[i], "NOSB"))
            snd_flags |= SND_NO_SB;
        else if (!stricmp(argv[i], "NOMUSIC"))
            snd_flags |= SND_NO_MUSIC;
        else if (!stricmp(argv[i], "NOFMSFX"))
            snd_flags |= SND_NO_FMSFX;
        else if (!stricmp(argv[i], "NOSOUND"))
            snd_flags |= SND_QUIET;
        else if (!stricmp(argv[i], "NIGHT"))
            g_night = 1;
        else if (!stricmp(argv[i], "DEBUG"))
            g_debug |= 1;
        else if (!stricmp(argv[i], "BOOMAIM"))
            g_debug |= 3;           /* tests: aliens shoot at explosive terrain (ai.c) */
        else if (!stricmp(argv[i], "BOMBAI"))
            g_debug |= 4;           /* tests: blaster aliens launch whenever they can (aiway.c) */
        else if (!stricmp(argv[i], "GEAR"))
            g_quick_gear = 1;
        else if (!stricmp(argv[i], "BOMBGEAR"))
            g_quick_gear = 2;       /* tests: blaster launchers (spawn.c) */
        else if (!stricmp(argv[i], "PSI"))
            g_quick_psi = 1;        /* quick battles: psi skill and Psi-Amps */
        else if (!stricmp(argv[i], "DIFF") && i + 1 < argc)
            g_difficulty = atoi(argv[++i]) % N_DIFFICULTY;
        else if (!stricmp(argv[i], "CRASHED"))
            g_brief_crashed = 1;    /* quick battles: the UFO crashed */
        else if (!stricmp(argv[i], "SEEALL"))
            g_see_all = 1;          /* debugging: no fog, every unit shown */
        else if (!stricmp(argv[i], "SHOTLOG"))
            g_shot_log = 1;         /* debugging: shot paths, map dump */
        else if (!stricmp(argv[i], "HOLDVIEW") && i + 1 < argc)
            g_view_hold = (unsigned char)atoi(argv[++i]);   /* tests: stay on a picture (bview.c) */
        else if (!stricmp(argv[i], "PACELOG"))
            g_pace_log = 1;         /* tests: the Game Options' pacing (battle.h) */
        else if (!stricmp(argv[i], "TERROR"))
            g_terror_now = 1;       /* debugging: a terror site at once */
        else if (!stricmp(argv[i], "ARMOUR") && i + 1 < argc)
            g_quick_armour = (unsigned char)(atoi(argv[++i]) % N_ARMOURS);
        else if (!stricmp(argv[i], "HWP") && i + 1 < argc)
            hwp_option(argv[++i]);
        else if (!stricmp(argv[i], "CRAFT") && i + 1 < argc) {
            /* quick battles: the transport's map, S(kyranger) L(ightning) A(venger) */
            char c = (char)(argv[++i][0] | 32);
            g_pak_craft = (unsigned char)(c == 'a' ? CM_AVENGER : c == 'l' ? CM_LIGHTNING
                                                                         : CM_SKYRANGER);
        } else if (!stricmp(argv[i], "SQUAD") && i + 1 < argc)
            g_quick_squad = atoi(argv[++i]);    /* quick battles: soldiers (8) */
        else if (!stricmp(argv[i], "NOUMB"))
            no_umb = 1;             /* the UMB for sound only (speed comparisons) */
        else if (!stricmp(argv[i], "EXACTAI"))
            g_exact = 1;
        else if (!stricmp(argv[i], "NOEXACTAI"))
            g_exact = 0;
    }
    log_printf("CPU: %s, exact AI and sight %s", below ? "below 286" : "286 or better",
               g_exact ? "on" : "off");
    log_printf("LIM: %u paragraphs free at the start, UMB %u: smoke %d fire %d devices %d "
               "floor %d carry %d explosions %d", lim_free, lim_umb, max_smoke, max_fire,
               max_dev, g_max_floor, g_max_carry, g_max_boom);
    if (g_debug && forced_race && !forced && !quick) {
        strncpy(campaign_race, forced_race, 12);
        strupr(campaign_race);
        forced_race = NULL;
    }
    if (forced || forced_race)
        quick = 1;
    if (g_bench) {
        g_seed = 12345UL;
        g_aliens = 3;
        quick = 1;
    }

    /* The video mode, its folder and the quick battles' pack lists. */
    want_cga = mode_pick(want_cga, below, argc, argv, !forced, !forced_race);
    check_packs();
    i = umb_plan(no_umb, snd_flags, xmem_need(argc, argv));
    cv_area(argc, argv, below || (want_cga && want_cga != 3));
    xmem_start(argc, argv, i);  /* EMS / XMS read cache: src/xmem.c */
    rd_count_start();           /* the read count and the mode folder (rdcount.asm); main
                                   unhooks it at exit */
    *forced_ = forced;
    *forced_race_ = forced_race;
    *want_cga_ = want_cga;
    *snd_flags_ = snd_flags;
    return quick;
}

/* ------------------------------------------------------------------ */
/* Campaign saves: the bases block's lists after the headers (base.c) */

/* The lists as the save has them after the base headers: way points, the
   next way point's id, craft, each base's record. XS24 saves (old) were
   written before the limits branch: 16 way points, 24 craft, 16 research
   projects and 10 production runs a base. They go into the first entries
   of the larger lists (the block is cleared, so the rest is empty) with
   nothing else changed: the records' other fields come first. The code
   lives here, in the startup section, since the geoscape's sections have
   no room left. */
int base_load_lists(FILE *f, int n, int old)
{
    BaseRec *r;
    int ok;

    if (!old)
        return fread(g_wp, sizeof g_wp, 1, f) == 1
            && fread(&g_bb->wp_next, sizeof g_bb->wp_next, 1, f) == 1
            && fread(g_craft, sizeof g_craft, 1, f) == 1
            && fread(g_bb->rec, sizeof g_bb->rec[0], n, f) == (size_t)n;
    ok = fread(g_wp, sizeof g_wp[0], 16, f) == 16
      && fread(&g_bb->wp_next, sizeof g_bb->wp_next, 1, f) == 1
      && fread(g_craft, sizeof g_craft[0], 24, f) == 24;
    for (r = g_bb->rec; ok && r < g_bb->rec + n; r++)
        ok = fread(r, offsetof(BaseRec, res), 1, f) == 1
          && fread(r->res, sizeof r->res[0], 16, f) == 16
          && fread(r->prod, sizeof r->prod[0], 10, f) == 10;
    return ok;
}
