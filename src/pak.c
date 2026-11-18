/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "batui.h"
#include "battle.h"
#include "gfx.h"
#include "hwp.h"
#include "input.h"
#include "inv.h"
#include "map.h"
#include "pak.h"
#include "render.h"
#include "unit.h"
#include "vox.h"

Pack g_pak;

#define BANK_MAX 65000UL

unsigned char *g_banks[MAX_BANKS];
int g_n_banks;
unsigned g_walk2 = 0xFFFF;

/* Sprite banks come straight from DOS, exactly sized: they are the
   biggest allocations and the C heap wastes memory around them. */
static unsigned char *bank_alloc(unsigned long size)
{
    unsigned seg;

    if (size > BANK_MAX || _dos_allocmem((unsigned)((size + 15) >> 4), &seg))
        return NULL;
    return (unsigned char *)MK_FP(seg, 0);
}

#pragma pack(1)
typedef struct {
    char magic[4];
    unsigned short version, n_mcd, n_sprites, n_blocks, map_x, map_y, map_z;
    unsigned char n_fill, n_landing, n_big, video;
    unsigned short ufo_block, craft_block, n_unit_sets, cursor_base;
    unsigned char n_alien_types, n_spawn;
    char title[24];
    unsigned char script, road[3], n_civilians, n_big_blocks, reserved[2];
} PakHeader;
#pragma pack()

static void *xalloc(unsigned long size)
{
    if (size == 0)
        size = 1;
    if (size > BANK_MAX)
        return NULL;
    return malloc((size_t)size);
}

static int read_into(FILE *f, void *dst, unsigned long size)
{
    busy_tick();                /* the busy pointer (input.h) */
    return fread(dst, 1, (size_t)size, f) == (size_t)size;
}

static void xfree(void *p)
{
    if (p)
        free(p);
}

/* Map blocks stay in the pack file; map_generate reads them one at a time,
   so they never take memory alongside the sprites. */
char block_path[80];
static FILE *block_file;
static unsigned char *block_buf;
static unsigned block_buf_size;

/* The voxel shapes' place in the pack file (load_he_blocks), 0 none. */
static long vox_pos;
static unsigned short vox_size;

/* The mission's sprites are read after the map is generated (see
   pak_load_sprites); until then only their offsets are kept: those of the
   common sprites and their end (sprite_off[0 .. spr_common]), then those
   of the landing craft's sprites and their end (numbered from spr_common
   on, the pack's from spr_disk on). */
static unsigned long *sprite_off, sprite_pos;
unsigned mission_sprites;
static unsigned spr_common, spr_disk;

unsigned char g_pak_craft;
unsigned char *g_suit_extra;

#define SPR_OFF(i)  ((i) < spr_common ? (i) : (i) + 1)

unsigned long spr_size(unsigned i)
{
    i = SPR_OFF(i);
    return sprite_off[i + 1] - sprite_off[i];
}

void pak_free_deploy(void)
{
    xfree(g_pak.deploy);
    g_pak.deploy = NULL;
    g_pak.n_deploy = 0;
}

const unsigned char *pak_block_tiles(unsigned i)
{
    const Block *b = &g_pak.block[i];
    unsigned size = b->sx * b->sy * b->sz * 4;

    if (!block_file && !(block_file = fopen(block_path, "rb")))
        return NULL;
    if (size > block_buf_size) {
        xfree(block_buf);
        block_buf = xalloc(size);
        block_buf_size = block_buf ? size : 0;
        if (!block_buf)
            return NULL;
    }
    if (fseek(block_file, (long)b->file_off, SEEK_SET) || !read_into(block_file, block_buf, size))
        return NULL;
    return block_buf;
}

void pak_free_blocks(void)
{
    if (block_file)
        fclose(block_file);
    block_file = NULL;
    xfree(block_buf);
    block_buf = NULL;
    block_buf_size = 0;
    xfree(g_pak.block_nodes);
    g_pak.block_nodes = NULL;
    xfree(g_pak.routes);
    g_pak.routes = NULL;
}

void pak_free(void)
{
    unsigned i;

    for (i = 0; i < (unsigned)g_n_banks; i++)
        _dos_freemem(FP_SEG(g_banks[i]));
    g_n_banks = 0;
    if (g_fob)
        _dos_freemem(FP_SEG(g_fob));
    g_fob = NULL;
    if (g_suit_extra)
        _dos_freemem(FP_SEG(g_suit_extra));
    g_suit_extra = NULL;
    pak_free_blocks();
    xfree(sprite_off);
    sprite_off = NULL;
    xfree(g_pak.block);
    xfree(g_pak.sprite);
    xfree(g_pak.mcd);
    xfree(g_pak.fill);
    xfree(g_pak.landing);
    xfree(g_pak.big);
    xfree(g_pak.node);
    xfree(g_pak.alien_type);
    xfree(g_pak.spawn);
    xfree(g_pak.block_nodes);
    xfree(g_pak.routes);
    xfree(g_vox);
    g_vox = NULL;
    vox_pos = 0;
    xfree(g_pak.lit);
    xfree(g_pak.he);
    pak_free_deploy();
    map_free_nodes();
    memset(&g_pak, 0, sizeof g_pak);
    if (g_pic_note)
        sc_stop();
}

/* n_disk: the pack's sprites; g_pak.n_sprites: the mission's (the common
   ones and the landing craft's). */
static const char *load_sprite_table(FILE *f, unsigned n_disk)
{
    unsigned n = g_pak.n_sprites, n_craft = n - spr_common;
    long table = ftell(f);
    unsigned long end;

    sprite_off = xalloc((unsigned long)(n + 2) * 4);
    /* One spare entry after the last sprite: a picture from elsewhere
       (a floor item's, src/floor.c) is drawn through it. */
    g_pak.sprite = xalloc((unsigned long)(n + 1) * sizeof(unsigned char *));
    if (!sprite_off || !g_pak.sprite)
        return "out of memory (sprite tables)";
    _fmemset(g_pak.sprite, 0, (n + 1) * sizeof(unsigned char *));
    if (!read_into(f, sprite_off, (unsigned long)(spr_common + 1) * 4)
        || fseek(f, table + (long)spr_disk * 4, SEEK_SET)
        || !read_into(f, sprite_off + spr_common + 1, (unsigned long)(n_craft + 1) * 4)
        || fseek(f, table + (long)n_disk * 4, SEEK_SET) || !read_into(f, &end, 4))
        return "pack truncated (sprite offsets)";
    mission_sprites = n;
    sprite_pos = ftell(f);
    if (fseek(f, (long)end, SEEK_CUR))
        return "pack truncated (sprites)";
    return NULL;
}

/* Marks the mission sprites to load: unit and cursor frames always;
   terrain only for the records on the map, their destroyed versions and
   their doors' other halves. Returns the bytes they take. */
unsigned long mark_needed(unsigned char *need)
{
    unsigned n = mission_sprites, r, i, t;
    unsigned char used[256];
    const unsigned char *p;
    unsigned long bytes = 0;
    int more;

    memset(need, 1, n);
    memset(used, 0, sizeof used);
    for (r = 1; r < g_pak.n_mcd; r++) {
        const Mcd *m = &g_pak.mcd[r];
        need[m->sprite] = 0;
        if (MCD_PART(m) == PART_FLOOR && OR_SPRITE(m) != OR_NONE)
            need[OR_SPRITE(m)] = 0;
    }
    p = g_map;                      /* (at most MAP_MAX_TILES tiles) */
    for (t = g_mx * g_my * g_mz; t; t--, p += 4) {
        used[p[0]] = 1;
        used[p[1]] = 1;
        used[p[2]] = 1;
        used[p[3]] = 1;
    }
    used[g_pak.scorched] = 1;       /* where a ground floor is destroyed */
    do {
        more = 0;
        for (r = 1; r < g_pak.n_mcd; r++)
            if (used[r]) {
                const Mcd *m = &g_pak.mcd[r];
                if (m->die_mcd && !used[m->die_mcd])
                    used[m->die_mcd] = more = 1;
                if (m->alt_mcd && !used[m->alt_mcd])
                    used[m->alt_mcd] = more = 1;
            }
    } while (more);
    for (r = 1; r < g_pak.n_mcd; r++)
        if (used[r]) {
            const Mcd *m = &g_pak.mcd[r];
            need[m->sprite] = 1;
            if (MCD_PART(m) == PART_FLOOR && OR_SPRITE(m) != OR_NONE)
                need[OR_SPRITE(m)] = 1;
        }
    for (i = 0; i < n; i++)
        if (need[i])
            bytes += spr_size(i);
    return bytes;
}

unsigned long pak_sprite_bytes(void)
{
    unsigned char *need;
    unsigned long bytes;

    if (!sprite_off || !(need = xalloc(mission_sprites)))
        return 0;
    bytes = mark_needed(need);
    free(need);
    return bytes;
}

/* TileEngine::calculateTerrainLighting in a dark battle: the map columns
   where a light source of the terrain (a floor or an object) gives 6 or
   more, its light less the distance rounded, on every level (addLight):
   g_pak.lit, a bit per column. From the parts on the map: at the setup
   (pak_load_sprites) and, as DOS after each explosion, when a light
   source broke (battle.c bt_break sets relight, effects.c fx_lit calls
   this: a lamp that is destroyed no longer lights). */
void pak_light_map(void)
{
    unsigned x, y, z, k, n = 0, size = (g_mx * g_my + 7) / 8;
    int dx, dy, l, r2;
    const unsigned char *t;

    g_pak.relight = 0;
    if (!g_night || !g_pak.n_lights
        || (!g_pak.lit && (g_pak.lit = xalloc(size)) == NULL))
        return;
    memset(g_pak.lit, 0, size);
    for (z = 0; z < g_mz; z++)
        for (y = 0; y < g_my; y++)
            for (x = 0; x < g_mx; x++)
                for (t = TILE(x, y, z), k = 0; k < g_pak.n_lights; k++)
                    if (t[PART_FLOOR] == g_pak.lights[k][0] || t[PART_OBJECT] == g_pak.lights[k][0]) {
                        l = g_pak.lights[k][1] - 6;
                        r2 = ((2 * l + 1) * (2 * l + 1) - 1) / 4;   /* rounded distance <= l */
                        for (dy = -l; dy <= l; dy++)
                            for (dx = -l; dx <= l; dx++)
                                if (dx * dx + dy * dy <= r2 && (unsigned)(x + dx) < g_mx
                                    && (unsigned)(y + dy) < g_my) {
                                    unsigned i = (y + dy) * g_mx + x + dx;
                                    g_pak.lit[i >> 3] |= (unsigned char)(1 << (i & 7));
                                }
                        n++;
                    }
    log_printf("terrain light: %u sources", n);
}

const char *pak_load_sprites(void)
{
    unsigned n = mission_sprites, first, last, i;
    unsigned char *need;

    if (!sprite_off)
        return NULL;
    if (!block_file && !(block_file = fopen(block_path, "rb")))
        return "cannot open pack";
    need = xalloc(n);
    if (!need)
        return "out of memory (sprite list)";
    if (sc_decide(need)) {
        free(need);
        return "cannot open pack";
    }

    /* Banks of up to 64K, split at sprite boundaries. */
    for (first = 0; first < n; first = last) {
        unsigned long size = 0, pos = 0xFFFFFFFFUL;
        unsigned char *bank;

        for (last = first; last < n; last++) {
            unsigned long s = need[last] == 1 ? spr_size(last) : 0;
            if (size + s > BANK_MAX)
                break;
            size += s;
        }
        if (last == first || g_n_banks >= MAX_BANKS) {
            free(need);
            return "sprite data too large";
        }
        if (!size)
            continue;
        bank = bank_alloc(size);
        if (!bank) {
            free(need);
            return "out of memory (sprites)";
        }
        g_banks[g_n_banks++] = bank;
        for (size = 0, i = first; i < last; i++) {
            unsigned long s = spr_size(i), at = sprite_off[SPR_OFF(i)];
            if (need[i] != 1)
                continue;
            if (pos != at && fseek(block_file, (long)(sprite_pos + at), SEEK_SET)) {
                free(need);
                return "pack truncated (sprites)";
            }
            if (!read_into(block_file, bank + (unsigned)size, s)) {
                free(need);
                return "pack truncated (sprites)";
            }
            pos = at + s;
            g_pak.sprite[i] = bank + (unsigned)size;
            size += s;
        }
    }
    if (!sc_keep(sprite_off, sprite_pos, spr_common, n)) {
        xfree(sprite_off);
        sprite_off = NULL;
    }
    free(need);
    pak_light_map();
    return NULL;
}

/* The end of the loaders' data: the pack's last sections run to the end
   of the file, or to its trailer ("XTR1": colour use and minimap for
   src/batui.c, see tools/convert.py), which is not loaded here; tr_end is
   where the trailer's sections end (the returned end without a trailer).
   Leaves the file position anywhere. */
static long tr_end;

static long data_end(FILE *f)
{
    unsigned short len;
    char m[4];
    long end;

    fseek(f, 0, SEEK_END);
    end = ftell(f);
    if (end >= 6 && !fseek(f, end - 6, SEEK_SET) && read_into(f, &len, 2)
        && read_into(f, m, 4) && !memcmp(m, "XTR1", 4) && (long)len + 6 <= end) {
        tr_end = end - 6;
        return end - 6 - len;
    }
    return tr_end = end;
}

const char *pak_vox(void)
{
    FILE *f;

    g_vox_stamp++;
    if (vox_pos && (f = fopen(block_path, "rb")) != NULL) {
        if (!fseek(f, vox_pos, SEEK_SET) && (g_vox = xalloc(vox_size)) != NULL
            && !read_into(f, g_vox, vox_size)) {
            xfree(g_vox);
            g_vox = NULL;
        }
        fclose(f);
    }
    return g_vox ? NULL : "no voxel shapes (XLF1): convert the pack again";
}

/* The trailer (tools/convert.py): "XHB1", each MCD record's HE blockage
   (its index in g_pak.he into the high bits of its tile_type, MCD_HE),
   "XFM1" their Flammable (MCD_FLAM), "XLT1", "XEX1", the place of "XLF1".
   Leaves the file position anywhere. */
static void load_he_blocks(FILE *f)
{
    unsigned short n;
    unsigned char b, *he = g_pak.he, nh = 1, k;
    char m[4];
    long pos;
    unsigned i;

    g_pak.n_lights = 0;
    memset(he, 0, MAX_HE_VALUES);   /* he[0] = 0: no blockage */
    memset(he + MAX_HE_VALUES, 255, g_pak.n_mcd);       /* never burns */
    for (pos = data_end(f); pos < tr_end;) {
        if (fseek(f, pos, SEEK_SET) || !read_into(f, m, 4))
            return;
        if (!memcmp(m, "XCU1", 4)) {
            pos += 20;
            continue;
        }
        if (!read_into(f, &n, 2))
            return;
        if (!memcmp(m, "XMM1", 4))
            pos += 6 + (long)n * (g_pak.video ? 6 : 10);
        else if (!memcmp(m, "XHB1", 4) || !memcmp(m, "XFM1", 4)) {
            /* By the pack's record table: the common records, then the
               landing craft's (Pack.craft_rec0). */
            unsigned c0 = g_pak.craft_rec0, cd = g_pak.craft_rec_disk, r;
            for (i = 0; i < n && read_into(f, &b, 1); i++) {
                r = i < c0 ? i : i - cd + c0;
                if (!(i < c0 || (i >= cd && r < g_pak.n_mcd)))
                    continue;
                if (m[1] == 'F') {
                    he[MAX_HE_VALUES + r] = b;
                    continue;
                }
                /* the HE blockage's index: a new value, the last one when
                   they are all taken (tools/convert.py checks they fit) */
                for (k = 0; k < nh && he[k] != b; k++)
                    ;
                if (k == MAX_HE_VALUES)
                    k--;
                else if (k == nh)
                    he[nh++] = b;
                g_pak.mcd[r].tile_type |= (unsigned char)(k << 2);
            }
            pos += 6 + (long)n;
        } else if (!memcmp(m, "XLT1", 4)) {
            /* the scorched earth record, the light sources */
            if (!read_into(f, &g_pak.scorched, 1) || g_pak.scorched >= g_pak.n_mcd)
                g_pak.scorched = 0;
            for (i = 0; i < n && g_pak.n_lights < MAX_LIGHTS
                 && read_into(f, g_pak.lights[g_pak.n_lights], 2); i++)
                if ((k = g_pak.lights[g_pak.n_lights++][0]) < g_pak.n_mcd)
                    g_pak.mcd[k].tile_type |= MCD_LIGHT;    /* (battle.c bt_break) */
            pos += 7 + 2 * (long)n;
        } else if (!memcmp(m, "XEX1", 4)) {
            /* the records that explode (MCD_EXPL), their strength */
            read_into(f, g_pak.expl, n < MAX_EXPL ? 2 * n : 2 * MAX_EXPL);
            pos += 6 + 2 * (long)n;
        } else {
            /* "XLF1": the voxel shapes (src/vox.c), read by pak_vox. */
            if (!memcmp(m, "XLF1", 4) && n == g_pak.n_disk_mcd && read_into(f, &vox_size, 2))
                vox_pos = pos + 8;
            return;
        }
    }
}

/* Route nodes of every block (map scripts), kept as read: per block a
   count byte and the nodes. */
static const char *load_block_nodes(FILE *f)
{
    long start = ftell(f), size;

    size = data_end(f) - start;
    if (size < 0)
        size = 0;
    fseek(f, start, SEEK_SET);
    g_pak.block_nodes = xalloc((unsigned long)size);
    if (!g_pak.block_nodes || !read_into(f, g_pak.block_nodes, (unsigned long)size))
        return "pack truncated (block routes)";
    return NULL;
}

/* "XRT1" / "XRT2": the full route nodes of every block (see Pack.routes).
   Returns 1 if the pack has them; else the file position is left as it was. */
static int load_routes(FILE *f)
{
    long start = ftell(f), size;
    char m[4];
    unsigned char n;

    if (!read_into(f, m, 4) || memcmp(m, "XRT", 3) || (m[3] != '1' && m[3] != '2')
        || !read_into(f, &n, 1) || n > 16
        || !read_into(f, g_pak.outside, n)) {
        fseek(f, start, SEEK_SET);
        return 0;
    }
    start = ftell(f);
    size = data_end(f) - start;
    if (size < 0)
        size = 0;
    fseek(f, start, SEEK_SET);
    g_pak.routes = xalloc((unsigned long)size + 2);
    if (!g_pak.routes || !read_into(f, g_pak.routes + 1, (unsigned long)size)) {
        xfree(g_pak.routes);
        g_pak.routes = NULL;
    } else
        *g_pak.routes = m[3] == '2' ? 11 : 10;
    return 1;
}

const char *pak_load(const char *path)
{
    PakHeader h;
    FILE *f;
    const char *err = NULL;
    unsigned i;
    unsigned char o[BO_N];

    /* The battlescape's Game Options (battle.h) as last set. */
    f = fopen(BOPT_FILE, "rb");
    if (f) {
        i = fread(o, 1, BO_N, f) == BO_N ? 0 : BO_N + 1;
        while (i < BO_N && (unsigned)(o[i] - 1) < (unsigned)"\5\2\6\6\6"[i])
            i++;
        if (i == BO_N)
            memcpy(g_bopt, o, BO_N);
        fclose(f);
    }
    pak_free();
    f = fopen(path, "rb");
    if (!f)
        return "cannot open pack";
    if (!read_into(f, &h, sizeof h) || memcmp(h.magic, "XPK2", 4) || h.version != 2
        || h.n_unit_sets > 8) {
        fclose(f);
        return "not a version 2 pack";
    }
    g_pak.n_mcd = h.n_mcd;
    g_pak.n_sprites = h.n_sprites;
    g_pak.n_blocks = h.n_blocks;
    g_pak.map_x = h.map_x;
    g_pak.map_y = h.map_y;
    g_pak.map_z = h.map_z;
    g_pak.n_fill = h.n_fill;
    g_pak.n_landing = h.n_landing;
    g_pak.n_big = h.n_big;
    g_pak.video = h.video;
    g_pak.occ = h.reserved[1];
    g_pak.ufo_block = h.ufo_block;
    g_pak.craft_block = h.craft_block;
    g_pak.n_unit_sets = h.n_unit_sets;
    g_pak.cursor_base = h.cursor_base;
    g_cur_spr[0] = h.cursor_base + CF_BACK;     /* the red box (render.h) */
    g_cur_spr[1] = h.cursor_base + CF_FRONT;
    g_cur_spr[2] = 0;
    g_pak.n_alien_types = h.n_alien_types;
    g_pak.n_spawn = h.n_spawn;
    memcpy(g_pak.title, h.title, sizeof g_pak.title);
    g_pak.title[sizeof g_pak.title - 1] = 0;
    g_pak.script = h.script;
    memcpy(g_pak.road, h.road, sizeof g_pak.road);
    g_pak.n_civilians = h.n_civilians;
    g_pak.n_big_blocks = h.n_big_blocks;
    g_pak.zombie_set = 0xFF;
    strncpy(block_path, path, sizeof block_path - 1);

    /* The transports' maps: the landing craft's records, sprites, block
       and seats (tools/convert.py craft_section). */
    g_pak.n_disk_mcd = g_pak.craft_rec0 = g_pak.craft_rec_disk = h.n_mcd;
    spr_common = spr_disk = h.n_sprites;
    g_pak.craft = CM_SKYRANGER;
    if (h.reserved[0]) {
        char m[4];
        unsigned char nc, c, e[10];
        unsigned short k[2];
        if (!read_into(f, m, 4) || memcmp(m, "XCR1", 4) || !read_into(f, &nc, 1)
            || !read_into(f, k, 4)) {
            fclose(f);
            return "pack truncated (crafts)";
        }
        for (c = 0; c < nc; c++) {
            if (!read_into(f, e, 10)) {
                fclose(f);
                return "pack truncated (crafts)";
            }
            /* u16 first record, u8 count, u16 first sprite, u16 count,
               u16 block, u8 seats */
            if (c == g_pak_craft || !c) {
                g_pak.craft = c;
                g_pak.craft_rec0 = k[0];
                g_pak.craft_rec_disk = e[0] | e[1] << 8;
                g_pak.n_mcd = k[0] + e[2];
                spr_common = k[1];
                spr_disk = e[3] | e[4] << 8;
                g_pak.n_sprites = spr_common + (e[5] | e[6] << 8);
                g_pak.craft_block = e[7] | e[8] << 8;
                pak_free_deploy();
                if (e[9] && (g_pak.deploy = xalloc(e[9] * 4u)) != NULL
                    && read_into(f, g_pak.deploy, e[9] * 4u)) {
                    g_pak.n_deploy = e[9];
                    continue;
                }
            }
            fseek(f, e[9] * 4L, SEEK_CUR);
        }
    }

    g_pak.mcd = xalloc((unsigned long)g_pak.n_mcd * sizeof(Mcd));
    g_pak.fill = xalloc(h.n_fill);
    g_pak.landing = xalloc(h.n_landing);
    g_pak.big = xalloc(h.n_big);
    g_pak.block = xalloc((unsigned long)h.n_blocks * sizeof(Block));
    g_pak.he = xalloc(MAX_HE_VALUES + g_pak.n_mcd);
    if (!g_pak.mcd || !g_pak.fill || !g_pak.landing || !g_pak.big || !g_pak.block || !g_pak.he) {
        err = "out of memory (tables)";
        goto out;
    }
    memset(g_pak.block, 0, h.n_blocks * sizeof(Block));
    {
        /* The common records, then the landing craft's. */
        long table = ftell(f);
        if (!read_into(f, g_pak.mcd, (unsigned long)g_pak.craft_rec0 * sizeof(Mcd))
            || fseek(f, table + (long)g_pak.craft_rec_disk * sizeof(Mcd), SEEK_SET)
            || !read_into(f, g_pak.mcd + g_pak.craft_rec0,
                          (unsigned long)(g_pak.n_mcd - g_pak.craft_rec0) * sizeof(Mcd))
            || fseek(f, table + (long)h.n_mcd * sizeof(Mcd), SEEK_SET)) {
            err = "pack truncated (tables)";
            goto out;
        }
    }
    if (!read_into(f, g_pak.fill, h.n_fill)
        || !read_into(f, g_pak.landing, h.n_landing)
        || !read_into(f, g_pak.big, h.n_big)
        || !read_into(f, g_pak.unit_base, h.n_unit_sets * 2u)) {
        err = "pack truncated (tables)";
        goto out;
    }
    err = load_sprite_table(f, h.n_sprites);
    if (err)
        goto out;

    for (i = 0; i < h.n_blocks; i++) {
        Block *b = &g_pak.block[i];
        unsigned long size;

        if (!read_into(f, b, 4)) {
            err = "pack truncated (blocks)";
            goto out;
        }
        b->file_off = ftell(f);
        size = (unsigned long)b->sx * b->sy * b->sz * 4;
        if (fseek(f, (long)size, SEEK_CUR)) {
            err = "pack truncated (blocks)";
            goto out;
        }
    }

    {
        unsigned short n;
        if (!read_into(f, &n, 2)) {
            err = "pack truncated (routes)";
            goto out;
        }
        g_pak.n_nodes = n;
        g_pak.node = xalloc((unsigned long)n * sizeof(RouteNode));
        if (!g_pak.node || !read_into(f, g_pak.node, (unsigned long)n * sizeof(RouteNode))) {
            err = "pack truncated (routes)";
            goto out;
        }
    }
    g_pak.alien_type = xalloc((unsigned long)h.n_alien_types * sizeof(AlienType));
    g_pak.spawn = xalloc((unsigned long)h.n_spawn * sizeof(Spawn));
    if (!g_pak.alien_type || !g_pak.spawn
        || !read_into(f, g_pak.alien_type, (unsigned long)h.n_alien_types * sizeof(AlienType))
        || !read_into(f, g_pak.spawn, (unsigned long)h.n_spawn * sizeof(Spawn)))
        err = "pack truncated (aliens)";
    if (!err) {
        /* Optional: crew sizes by difficulty (packs from newer converters). */
        char m[4];
        int dq = 0;
        g_pak.n_qty = 0;
        if (h.n_spawn <= 16 && read_into(f, m, 4) && !memcmp(m, "XDQ1", 4)
            && read_into(f, g_pak.spawn_qty, h.n_spawn * 3u)) {
            g_pak.n_qty = h.n_spawn;
            dq = 1;
        }
        /* "XIS1": alien equipment by item level (newer packs). */
        g_pak.n_iset = 0;
        if (dq && !read_into(f, m, 4))
            dq = 0;
        if (dq && !memcmp(m, "XIS1", 4)) {
            unsigned char n = 0;
            if (read_into(f, &n, 1) && n <= 16 && read_into(f, g_pak.iset, n * 3u)
                && read_into(f, g_pak.ilevel, 20))
                g_pak.n_iset = n;
            if (!read_into(f, m, 4))
                dq = 0;
        }
        /* "XFL2": byte 13 of the MCD records is the big wall kind and
           the fuel (older packs: flammability and fuel, or the light
           block value); record 0 (nothing) has 0 there. */
        if (!dq || memcmp(m, "XFL2", 4))
            for (i = 0; i < g_pak.n_mcd; i++)
                g_pak.mcd[i].burn = 0;
        g_pak.mcd[0].burn = 0;
    }
    if (!err && h.script >= SCRIPT_ALIEN_BASE) {
        /* Map scripts: tunnel records, objective tiles, block routes. */
        char m[4];
        if (!read_into(f, m, 4) || memcmp(m, "XSC1", 4) || !read_into(f, g_pak.tunnel, 4)
            || !read_into(f, &g_pak.objective, 1))
            err = "pack truncated (script)";
        else if (!load_routes(f))
            err = load_block_nodes(f);
    } else if (!err)
        load_routes(f);
    if (!err)
        load_he_blocks(f);
out:
    fclose(f);
    return err;
}

#pragma pack(1)
typedef struct {
    char magic[4];
    unsigned short version, n_sets, n_sprites;
    unsigned char video;
    unsigned char reserved[21];
} RaceHeader;
#pragma pack()

#define LOAD_ARMOUR 0
#define LOAD_RACE   1
#define LOAD_TERROR 2
#define LOAD_SUIT   3       /* a soldier armour: frames after the 48 later */
#define SUIT_FRAMES (UNIT_FRAMES + 8)   /* standing, walking, kneeling */

static const char *load_units(const char *path, int as_race);

const char *race_load(const char *path)
{
    return load_units(path, LOAD_RACE);
}

const char *terror_load(const char *path)
{
    return load_units(path, LOAD_TERROR);
}

const char *armour_load(const char *path, int armour)
{
    unsigned char set = (unsigned char)g_pak.n_unit_sets;
    const char *err = load_units(path, LOAD_SUIT);
    if (!err)
        g_armour_set[armour] = set;
    return err;
}

const char *unit_set_load(const char *path, unsigned char *set)
{
    unsigned char s = (unsigned char)g_pak.n_unit_sets;
    const char *err = load_units(path, LOAD_ARMOUR);
    if (!err)
        *set = s;
    return err;
}

/* Battle rules of a rank from a pack without the "XST1" section. */
static void xstat_default(UnitXStat *x, const AlienType *t)
{
    memset(x, 0, sizeof *x);
    memset(x->mod, 100, sizeof x->mod);
    x->strength = t->melee ? t->melee : 50;
    x->stamina = 90;
    x->bravery = 80;
    x->throwing = 60;
    x->under = t->armor[1];
    x->en_rec = 30;
    x->height = 20;
    x->kneel = 14;
    x->width = (t->flags & AT_BIG) ? 0 : 7;
    x->melee = 80;
}

static const char *load_units(const char *path, int as_race)
{
    RaceHeader h;
    FILE *f;
    unsigned long *off = NULL, size;
    unsigned char **table, *bank;
    unsigned i, base, n;
    const char *err = NULL;
    AlienType types[8];
    unsigned char filled = 0;           /* ranks this pack supplies */

    f = fopen(path, "rb");
    if (!f)
        return "cannot open race pack";
    if (!read_into(f, &h, sizeof h) || memcmp(h.magic, "XRC1", 4)) {
        fclose(f);
        return "not a race pack";
    }
    if (h.video != g_pak.video) {
        fclose(f);
        return "race pack for the other video mode";
    }
    if (g_pak.n_unit_sets + h.n_sets > MAX_UNIT_SETS) {
        fclose(f);
        return "too many unit sets";
    }
    {
        if (!read_into(f, types, sizeof types)) {
            err = "race pack truncated";
            goto out;
        }
        for (i = 0; i < 8; i++)
            types[i].sprite_set += (unsigned char)g_pak.n_unit_sets;
        if (as_race == LOAD_RACE) {
            memcpy(g_pak.race, types, sizeof types);
            filled = 0xFF;
        } else if (as_race == LOAD_TERROR) {
            for (i = 0; i < 8; i++)
                if (types[i].valid && !g_pak.race[i].valid) {
                    g_pak.race[i] = types[i];
                    filled |= (unsigned char)(1 << i);
                }
            if (h.reserved[0])
                g_pak.zombie_set = (unsigned char)(g_pak.n_unit_sets + h.reserved[0] - 1);
        }
    }
    off = xalloc((unsigned long)(h.n_sprites + 1) * 4);
    table = xalloc((unsigned long)(g_pak.n_sprites + h.n_sprites + 1) * sizeof(unsigned char *));
    if (!off || !table) {
        xfree(table);
        err = "out of memory (race)";
        goto out;
    }
    if (!read_into(f, off, (unsigned long)(h.n_sprites + 1) * 4)) {
        free(table);
        err = "race pack truncated";
        goto out;
    }
    /* A soldier armour's frames after the 48 (the Flying Suit's hovering
       legs) wait for floorld.c fly_load: until there is room for them,
       they are the standing frames (48 + direction: direction x 5). */
    n = as_race == LOAD_SUIT && h.n_sprites > SUIT_FRAMES ? SUIT_FRAMES : h.n_sprites;
    size = off[n];
    bank = g_n_banks < MAX_BANKS ? bank_alloc(size) : NULL;
    if (!bank || !read_into(f, bank, size)) {
        if (bank)
            _dos_freemem(FP_SEG(bank));
        free(table);
        err = "cannot load race sprites";
        goto out;
    }
    g_banks[g_n_banks++] = bank;
    base = g_pak.n_sprites;
    for (i = 0; i < base; i++)
        table[i] = g_pak.sprite[i];
    for (i = 0; i < h.n_sprites; i++)
        table[base + i] = i < n ? bank + (unsigned)off[i] : table[base + (i - n) * 5];
    free(g_pak.sprite);
    g_pak.sprite = table;
    g_pak.n_sprites = base + h.n_sprites;
    for (i = 0; i < h.n_sets; i++)
        g_pak.unit_base[g_pak.n_unit_sets + i] = base + i * UNIT_FRAMES;
    g_pak.n_unit_sets += h.n_sets;
    if (filled) {
        /* Battle rules by rank: the "XST1" section after the sprites. */
        UnitXStat xs[8];
        char magic[4];
        int have = read_into(f, magic, 4) && !memcmp(magic, "XST1", 4)
                   && read_into(f, xs, sizeof xs);
        for (i = 0; i < 8; i++)
            if (filled & (1 << i)) {
                if (have)
                    g_pak.race_x[i] = xs[i];
                else
                    xstat_default(&g_pak.race_x[i], &types[i]);
            }
    }
out:
    xfree(off);
    fclose(f);
    return err;
}
