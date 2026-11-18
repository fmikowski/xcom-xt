/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Loads the pictures of the battle floor's items and bodies (inv.h g_fob)
   from FLOORV.PAK (tools/convert.py build_floor), only those the battle
   can show: what its units carry, their bodies, what lies on the ground.
   A section of its own, loaded once after the setup (main.c). */

#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "batui.h"
#include "effects.h"
#include "input.h"
#include "inv.h"
#include "pak.h"
#include "unit.h"
#include "gfx.h"
#include "sound.h"
#include "xmem.h"

#define FOB_MAX     80          /* frames in FLOORV.PAK at most */
#define FOB_KEEP    768         /* paragraphs the battle keeps free after them */
/* With an XMS picture store, whose buffer grows after them (picplan.c
   sc_start: what is left, keeping PIC_RESERVE), only a little more than its
   SC_MIN. */
#define KEEP        (g_sc_xh && !g_sc_top ? 608u : FOB_KEEP)

static void fob_item(unsigned char *need, const unsigned char *it, int item)
{
    if (item < N_ITEMS && it[item] != 0xFF)
        need[it[item]] = 1;
}

static void fob_body(unsigned char *need, const unsigned char *bd, int b, int big)
{
    int q;

    if (b >= 0 && bd[b] != 0xFF)
        for (q = 0; q < (big ? 4 : 1); q++)
            need[bd[b] + q] = 1;
}

static int get(FILE *f, void *p, unsigned n)
{
    return fread(p, 1, n, f) == n;
}

/* The Flying Suit's hovering legs (drawRoutine0 legsFloat: A3V.PAK frames
   48-55, standing by direction; pak.c leaves them out of the armour's
   sprites), after the floor pictures and only if FOB_KEEP paragraphs stay
   free as for them; else a hovering soldier is drawn standing as before.
   The pack: a 32-byte header (u16 sprites at 8), 8 x 24 bytes of ranks,
   u32 offsets[sprites + 1], the sprites. */
static void fly_load(void)
{
    unsigned long o[9];
    unsigned short ns;
    unsigned seg, size, i;
    unsigned char **s, *b;
    FILE *f;

    if (!g_armour_set[ARM_FLYING])
        return;                             /* nobody wears it */
    s = &g_pak.sprite[g_pak.unit_base[g_armour_set[ARM_FLYING]] + UNIT_FRAMES + 8];
    if (s[0] != s[-(UNIT_FRAMES + 8)] || (f = fopen(g_pack_names[2 + ARM_FLYING], "rb")) == NULL)
        return;                             /* (loaded: not the standing frames) */
    if (fseek(f, 8, SEEK_SET) || !get(f, &ns, 2) || ns < UNIT_FRAMES + 16
        || fseek(f, 32 + 192 + (UNIT_FRAMES + 8) * 4L, SEEK_SET) || !get(f, o, sizeof o))
        size = 0;
    else
        size = (unsigned)(o[8] - o[0]);
    _dos_allocmem(0xFFFF, &seg);            /* fails: seg = the largest block */
    if (!size || seg < (size + 15) / 16 + KEEP || _dos_allocmem((size + 15) / 16, &seg))
        log_printf("flying suit: the hovering legs (%u bytes) do not fit", size);
    else if (fseek(f, 32 + 192 + (ns + 1) * 4L + o[0], SEEK_SET)
             || !get(f, b = (unsigned char *)MK_FP(seg, 0), size)) {
        _dos_freemem(seg);
        log_printf("flying suit: pack truncated");
    } else {
        for (i = 0; i < 8; i++)
            s[i] = b + (unsigned)(o[i] - o[0]);
        g_suit_extra = b;
        log_printf("flying suit: the hovering legs, %u bytes", size);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* The walk phases the packs leave out (tools/walkpak.py): with upper or
   extended memory only, so a plain 640 KB XT stays as it was. Each pack that
   holds unit sets has <name>W.PAK beside it: "XWK1", u8 video, u8 n_sets,
   u16 0, u32 offsets[n_sets * 16 + 1] from the data, the data; frame set *
   16 + direction * 2 + k is walk slot 1 (k 0) or 3 (k 1). The sets the
   battle's units wear are loaded into one DOS block, X-COM's first, then by
   the number of units, while WALK_KEEP paragraphs stay free after it (the
   canvas, 10-12 KB and 12 KB kept, the blast picture and the music come
   later); g_walk2 tells battle.c `walk` which sets do not have them.
   `SET XWALK=OFF` leaves them out; XWALK=1 or 3 (tests) shows walk slot 1
   or 3 in place of the standing frames, so a screenshot shows them. */

#define WALK_KEEP   2048        /* 32 KB */
#define WALK_SETS   5           /* sets in one W pack at most */

static int boards(void)
{
    union REGS r;
    struct SREGS sr;

    if (umb_game)
        return 1;               /* a UMB holds the map buffer or code */
    segread(&sr);
    r.x.ax = 0xD700;            /* the read cache (xmem.h): EMS or XMS */
    r.x.si = XMEM_MAGIC;
    r.x.di = 0;
    intdosx(&r, &r, &sr);
    return r.x.ax == XMEM_MAGIC;
}

static unsigned pack_word(const char *name, long at)
{
    unsigned short w = 0;
    FILE *f = fopen(name, "rb");

    if (f) {
        if (fseek(f, at, SEEK_SET) || !get(f, &w, 2))
            w = 0;
        fclose(f);
    }
    return w;
}

/* Opens the W pack of pack slot p (batui.h g_pack_names) and reads its
   offsets; returns its set count, 0 when there is none. */
static unsigned walk_open(int p, FILE **fp, unsigned long *off)
{
    char name[16], *d;
    const char *n = g_pack_names[p];
    unsigned char hd[8];
    FILE *f;

    *fp = NULL;
    if (*n == '+')
        n++;                    /* a set in the picture store: its pack */
    if (!*n || *n == '*' || strlen(n) > 12 || (d = strchr(strcpy(name, n), '.')) == NULL)
        return 0;               /* none, or another set's pictures */
    strcpy(d, "W.PAK");
    if ((f = fopen(name, "rb")) == NULL)
        return 0;
    if (!get(f, hd, 8) || memcmp(hd, "XWK1", 4) || hd[4] != g_pak.video || !hd[5]
        || hd[5] > WALK_SETS || !get(f, off, (hd[5] * 16 + 1) * 4)) {
        fclose(f);
        return 0;
    }
    *fp = f;
    return hd[5];
}

static void walk_load(void)
{
    unsigned long off[WALK_SETS * 16 + 1], size[MAX_UNIT_SETS], total = 0;
    unsigned char pk[MAX_UNIT_SETS], loc[MAX_UNIT_SETS], first[3 + N_ARMOURS];
    unsigned weight[MAX_UNIT_SETS], i, k, n, seg, set, pos, chosen = 0, nsets = 0, cand;
    unsigned long room;
    unsigned char *b, *pic[16];
    const char *e = getenv("XWALK");
    FILE *f;
    int p;

    g_walk2 = 0xFFFF;
    if (g_pak.video > 1 || g_herc || g_n_banks >= MAX_BANKS || !boards())
        return;
    if (e && !stricmp(e, "OFF")) {
        log_printf("walk frames: XWALK=OFF");
        return;
    }
    room = sc_room();           /* (picplan.c's section: only when the frames may load) */
    /* Which unit sets the battle's walkers wear, X-COM's above all. */
    memset(weight, 0, sizeof weight);
    for (i = 0; i < (unsigned)g_nunits; i++) {
        const Unit *u = &g_units[i];
        if (!u->big && u->sprite_set < MAX_UNIT_SETS)
            weight[u->sprite_set] += u->faction == FACTION_XCOM ? 64 : 1;
    }
    /* Each pack slot's first set: the mission pack's from 0, the race
       pack's after them, the terror pack's after those, the armours' by
       g_armour_set. */
    first[0] = 0;
    first[1] = (unsigned char)pack_word(g_pack_names[0], 26);  /* mission: n_unit_sets */
    first[2] = (unsigned char)(first[1] + pack_word(g_pack_names[1], 6));  /* race: n_sets */
    for (p = 3; p < 3 + N_ARMOURS; p++)
        first[p] = p - 2 < N_ARMOURS ? g_armour_set[p - 2] : 0xFF;
    memset(size, 0, sizeof size);
    for (p = 0; p < 2 + N_ARMOURS; p++) {
        if (p >= 3 && (p - 2 >= N_ARMOURS || !g_armour_set[p - 2]))
            continue;
        n = walk_open(p, &f, off);
        if (!f)
            continue;
        if (p >= 3)
            n = 1;              /* an armour pack: its one set */
        for (k = 0; k < n; k++) {
            set = first[p] + k;
            if (set < g_pak.n_unit_sets && weight[set] && !((g_alias_sets >> set) & 1)
                && !size[set]) {
                size[set] = off[k * 16 + 16] - off[k * 16];
                pk[set] = (unsigned char)p;
                loc[set] = (unsigned char)k;
            }
        }
        fclose(f);
    }
    /* The heaviest first, while the block stays under 64 KB and leaves
       WALK_KEEP free. */
    for (cand = i = 0; i < MAX_UNIT_SETS; i++)
        cand += size[i] != 0;
    _dos_allocmem(0xFFFF, &seg);            /* fails: seg = the largest block */
    for (;;) {
        for (k = MAX_UNIT_SETS, i = 0; i < MAX_UNIT_SETS; i++)
            if (size[i] && !((chosen >> i) & 1) && (k == MAX_UNIT_SETS || weight[i] > weight[k]))
                k = i;
        if (k == MAX_UNIT_SETS)
            break;
        /* (a picture store: its room, a paragraph a picture) */
        if (room ? total + size[k] + 16 * 16 <= room
                 : total + size[k] <= 65000UL && (total + size[k] + 15) / 16 + WALK_KEEP <= seg) {
            total += size[k];
            chosen |= 1u << k;
            nsets++;
        } else
            size[k] = 0;
    }
    if (!chosen) {
        log_printf("walk frames: none (%u sets, %u paragraphs free)", cand, seg);
        return;
    }
    if (room)
        b = NULL;               /* (into the store, set by set) */
    else if (_dos_allocmem((unsigned)((total + 15) / 16), &seg)) {
        log_printf("walk frames: no block of %lu bytes", total);
        return;
    }
    else
        b = (unsigned char *)MK_FP(seg, 0);
    for (pos = 0, set = 0; set < MAX_UNIT_SETS; set++) {
        unsigned char **s;
        if (!((chosen >> set) & 1))
            continue;
        n = walk_open(pk[set], &f, off);
        k = loc[set];
        if (!f)
            continue;
        if (fseek(f, 8 + (n * 16 + 1) * 4L + off[k * 16], SEEK_SET)
            || (b ? !get(f, b + pos, (unsigned)size[set]) : sc_walk(f, off + k * 16, 16, pic))) {
            fclose(f);
            log_printf("walk frames: %s truncated", g_pack_names[pk[set]]);
            continue;
        }
        fclose(f);
        s = &g_pak.sprite[g_pak.unit_base[set]];
        for (i = 0; i < 16; i++)
            s[(i >> 1) * 5 + 1 + 2 * (i & 1)] = b ? b + pos + (unsigned)(off[k * 16 + i] - off[k * 16])
                                                  : pic[i];
        if (e && (*e == '1' || *e == '3'))
            for (i = 0; i < 8; i++)
                s[i * 5] = s[i * 5 + *e - '0'];
        pos += (unsigned)size[set];
        g_walk2 &= ~(1u << set);
    }
    if (b)
        g_banks[g_n_banks++] = b;           /* pak_free frees it */
    log_printf("walk frames: %u sets, %lu bytes%s (sets %04X)", nsets, total,
               b ? "" : " in the picture store", ~g_walk2);
}

void floor_load(void)
{
    unsigned char hd[8], it[N_ITEMS], bd[N_BODIES], wt[FOB_MAX], need[FOB_MAX], at[3];
    unsigned short off[FOB_MAX + 1];
    unsigned n = 0, k = 0, i, j, seg, size, pos;
    long data;
    unsigned char *b;
    FILE *f;

    if (g_fob)
        _dos_freemem(FP_SEG(g_fob));
    g_fob = NULL;
    f = fopen("FLOORE.PAK\0FLOORC.PAK\0FLOORV.PAK" + 11 * g_pak.video, "rb");   /* (pak video byte) */
    if (!f || !get(f, hd, 8) || memcmp(hd, "XFO1", 4) || hd[4] != g_pak.video
        || (n = hd[5]) > FOB_MAX || hd[6] != N_ITEMS || hd[7] != N_BODIES
        || !get(f, it, N_ITEMS) || !get(f, bd, N_BODIES) || !get(f, wt, n)
        || !get(f, off, (n + 1) * 2)) {
        log_printf("floor pictures: no FLOOR pack for this build");
        goto out;
    }
    data = ftell(f);
    /* What the units carry and their bodies (a chryssalid out of a
       zombie too), what lies on the ground (a second stage, a saved
       battle). */
    memset(need, 0, n);
    for (i = 0; i < (unsigned)g_nunits; i++) {
        const Unit *u = &g_units[i];
        if (u->weapon != NO_ITEM) {
            fob_item(need, it, u->weapon);
            if (u->load)                    /* 0: the weapon's own clip */
                fob_item(need, it, u->load);
            if (u->clips)
                fob_item(need, it, g_items[u->weapon].clip_item);
        }
        if (u->n_grenades)
            fob_item(need, it, u->grenade);
        fob_body(need, bd, unit_body(u), u->big);
    }
    if (g_pak.zombie_set != 0xFF)
        for (i = 0; i < 8; i++)
            if (g_pak.race[i].valid && (g_pak.race[i].flags & AT_ZOMBIFY))
                fob_body(need, bd, BODY_LIVE - 1 + g_pak.race_x[i].live, 0);
    for (i = 0; i < g_max_floor; i++)
        fob_item(need, it, g_floor[i].item);
    for (i = 0; i < g_max_carry; i++)            /* inventories (inv.h) */
        if (g_carry[i].unit != CARRY_FREE)
            fob_item(need, it, g_carry[i].item);
    for (i = 0; i < FX_DEVS_TOP; i++)       /* fx_dev_pos: NO_ITEM past n_dev */
        fob_item(need, it, fx_dev_pos(i, at));
    /* Slots in frame order (a 2x2 body's quarters stay in a row). */
    size = FOB_N + 1;
    for (i = 0; i < n; i++)
        if (need[i]) {
            need[i] = (unsigned char)(++k);
            size += 3 + off[i + 1] - off[i];
        }
    _dos_allocmem(0xFFFF, &seg);            /* fails: seg = the largest block */
    if (seg < (size + 15) / 16 + KEEP || _dos_allocmem((size + 15) / 16, &seg)) {
        log_printf("floor pictures: %u bytes do not fit", size);
        goto out;
    }
    b = (unsigned char *)MK_FP(seg, 0);
    for (i = 0; i < N_ITEMS; i++)
        b[i] = it[i] != 0xFF && need[it[i]] ? need[it[i]] - 1 : 0xFF;
    for (i = 0; i < N_BODIES; i++)
        b[N_ITEMS + i] = bd[i] != 0xFF && need[bd[i]] ? need[bd[i]] - 1 : 0xFF;
    b[FOB_N] = (unsigned char)k;
    pos = FOB_N + 1 + 3 * k;
    for (i = j = 0; i < n; i++)
        if (need[i]) {
            unsigned s = off[i + 1] - off[i];
            b[FOB_N + 1 + j] = wt[i];
            ((unsigned short *)(b + FOB_N + 1 + k))[j++] = pos;
            if (fseek(f, data + off[i], SEEK_SET) || !get(f, b + pos, s)) {
                _dos_freemem(seg);
                log_printf("floor pictures: pack truncated");
                goto out;
            }
            pos += s;
        }
    g_fob = b;
    /* (bdebug.c: nothing writes to them from here on) */
    g_fob_size = size;
    g_fob_sum = 0;
    for (i = 0; i < size; i++)
        g_fob_sum += b[i];
    log_printf("floor pictures: %u of %u, %u bytes", k, n, size);
out:
    if (f)
        fclose(f);
    fly_load();
    if (g_sc_xh && !g_sc_top)
        sc_start();                         /* the XMS store's buffer: what is left */
    walk_load();                            /* then only with 32 KB free (the canvas) */
}
