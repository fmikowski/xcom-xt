/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Saving and loading in the middle of a battle, as DOS does (bsave.h;
   docs/design.md, "Saving in battle"): the battlescape's options window
   and what it opens, the battle section of a save, and a saved battle
   restored instead of set up.

   An overlay section of its own: battle_run (the core) calls bs_options
   with nothing else on the stack, play_mission bs_restore before the
   battle. Everything goes straight between the battle's structures and
   the file (no buffers but the copy buffer; the slot lists are
   src/saves.c's, a section of their own); the core's functions
   it calls are on the stack anyway. Compiled with -zc (its strings stay in
   the section); tables are string literals (docs/design.md, Research and
   manufacturing: named tables of a -zc section read garbage through far
   pointers). */

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "ai_in.h"
#include "battle.h"
#include "battle_in.h"
#include "base.h"
#include "batui.h"
#include "bsave.h"
#include "craft.h"
#include "fx_in.h"
#include "geo.h"
#include "gfx.h"
#include "hwp.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "menu.h"
#include "pak.h"
#include "panel.h"
#include "path.h"
#include "pedia.h"
#include "render.h"
#include "saves.h"
#include "sound.h"
#include "uik.h"
#include "unit.h"

BsCtx g_bctx;
int g_bs_resume;
char g_bs_path[13];
long g_bs_off;
int g_bs_exit, g_bs_slot;

#define BS_SIZES ((unsigned short)(sizeof(Unit) * 3 + sizeof(AiUnit) * 5 + sizeof(Soldier) * 7 \
                  + sizeof(FloorItem) * 11 + sizeof(Spot) * 13 + sizeof(Device) * 17 \
                  + sizeof(MapNode) * 19 + sizeof(Hwp) * 23 + sizeof(Base) * 29 \
                  + sizeof(BsCtx) * 31 + MAX_UNITS + MAX_FLOOR + MAX_SMOKE + MAX_FIRE \
                  + MAX_DEV + MAX_SQUAD * 3 + BU_PACKS + MAX_BHWP * 37 + 1 + sizeof(Carry) * 41 \
                  + MAX_CARRY * 43 + 2 * 53))     /* 2 * 53: the lists by their counts (lim_init) */
unsigned short g_bs_sizes = BS_SIZES;

/* ------------------------------------------------------------------ */
/* The battle section: one description for writing and reading */

static FILE *bf;
static int bok, writing, short_lists;

static void io(void *p, unsigned n)
{
    if (!bok || !n)
        return;
    busy_tick();                /* the busy pointer (input.h) */
    if (writing ? fwrite(p, 1, n, bf) != n : fread(p, 1, n, bf) != n)
        bok = 0;
}
#define IO(v)   io(&(v), sizeof(v))

/* What the packs and the map do not give: the packs' names, the HWPs, the
   squad's roster (Cydonia's second stage changed it), the battle's
   counters, view and random numbers. */
static void io_head(unsigned long *rng)
{
    io(g_pack_names, sizeof g_pack_names);
    IO(g_hwp_n);
    io(g_hwp, sizeof g_hwp);
    io(g_hwp_hurt, sizeof g_hwp_hurt);
    IO(g_night);
    IO(g_squad_max);
    IO(g_squad_craft);
    IO(g_troop_craft);
    IO(g_carried);
    IO(g_civ_total);
    IO(g_civ_killed_alien);
    IO(g_civ_killed_xcom);
    io(g_xbase_had, sizeof g_xbase_had);
    IO(g_last_race);
    IO(g_walk_reserve);         /* (0 in the player's turn) */
    io(rng, sizeof *rng);
    IO(bt_turn);
    IO(bt_sel);
    IO(bt_reserve);
    IO(bt_obj_left);
    IO(bt_obj_done);
    IO(bt_vlast);
    IO(g_cur_x);
    IO(g_cur_y);
    IO(g_cur_z);
    IO(g_all_layers);
    {
        /* The squad's records (none in a quick battle) and where they
           came from in the base's roster; read into a block of their own. */
        unsigned char has = g_roster != NULL;
        int n = g_roster_n;
        IO(has);
        IO(n);
        if (!writing && bok) {
            if (!has)
                roster_squad_free();
            else if (n < 0 || n > MAX_SQUAD || roster_squad_alloc(n))
                bok = 0;
        }
        if (has)
            io(g_roster, n * sizeof(Soldier));
        io(g_squad_idx, sizeof g_squad_idx);
    }
    /* The base as the battle's setup left it: the motion scanner, the
       Psi-Amps and the special clips the squad took from the stores
       (units_spawn) after the campaign went to CAMPAIGN.TMP. */
    IO(g_base);
}

/* The map as it is now (damage, open doors, burnt parts), not generated
   again: the tiles, the route nodes and their flags, the craft and UFO
   blocks. */
static void io_map(void)
{
    unsigned n;

    IO(g_mx);
    IO(g_my);
    IO(g_mz);
    IO(g_craft_x);
    IO(g_craft_y);
    IO(g_ufo_x);
    IO(g_ufo_y);
    n = g_mx * g_my * g_mz;
    if (!writing && (n > MAP_MAX_TILES || g_mx > MAP_GRID * 10 || g_my > MAP_GRID * 10
                     || map_alloc()))
        bok = 0;
    io(g_map, n * 4);
    if (!writing)
        map_free_nodes();           /* (sets g_n_node to 0) */
    IO(g_n_node);
    if (!writing && bok) {
        g_node = malloc(g_n_node ? g_n_node * sizeof(MapNode) : 1);
        if (!g_node)
            bok = 0;
    }
    io(g_node, g_n_node * sizeof(MapNode));
    io(g_node_seg, sizeof g_node_seg);
    io(g_node_first, sizeof g_node_first);
}

/* Units, the floor, smoke, fire and devices, the AI's knowledge, the fog
   of war and the sight flags (with the effects' bits). */
/* The floor list and the carry pool up to their last entry in use: their
   sizes follow the memory (lim_init, src/startup.c), and a PC with less
   may lack room for a battle saved on another. */
static void lists_used(int *nf, int *nc)
{
    int f = g_max_floor, c = g_max_carry;

    while (f && g_floor[f - 1].item == NO_ITEM)
        f--;
    while (c && g_carry[c - 1].unit == CARRY_FREE)
        c--;
    *nf = f;
    *nc = c;
}

static void io_state(unsigned char *fog)
{
    unsigned n = g_mx * g_my * g_mz;
    int nf, nc;

    IO(g_nunits);
    io(g_units, sizeof g_units);
    io(g_unit_at, n);
    lists_used(&nf, &nc);
    IO(nf);
    IO(nc);
    IO(n_smoke);
    IO(n_fire);
    IO(n_dev);
    if (!writing && bok) {
        floor_clear();
        carry_clear();
        if (nf > g_max_floor || nc > g_max_carry || n_smoke > max_smoke || n_fire > max_fire
            || n_dev > max_dev) {
            log_printf("bload: lists too small here: floor %d/%d carry %d/%d smoke %d/%d fire %d/%d "
                       "devices %d/%d", nf, g_max_floor, nc, g_max_carry, n_smoke, max_smoke,
                       n_fire, max_fire, n_dev, max_dev);
            bok = 0;
            short_lists = 1;
        }
    }
    io(g_floor, nf * sizeof(FloorItem));
    io(g_carry, nc * sizeof(Carry));
    io(smoke, n_smoke * sizeof(Spot));
    io(fire, n_fire * sizeof(Spot));
    io(dev, n_dev * sizeof(Device));
    IO(g_fx_count);
    IO(g_fx_flares);
    io(ai_unit, MAX_UNITS * sizeof(AiUnit));
    io(ai_spotted, MAX_UNITS);
    io(ai_watch, MAX_UNITS);
    IO(ai_walker);
    io(fog, n);
    io(g_los_flags, n);
}

/* ------------------------------------------------------------------ */
/* The state line: equal after a save and the load of it (tests) */

static unsigned ck(unsigned s, const void *p, unsigned n)
{
    const unsigned char *b = p;
    unsigned char a = s, c = s >> 8;    /* the two sums mod 256 */

    while (n--) {
        a += *b++;
        c += a;
    }
    return (unsigned)c << 8 | a;
}

static void state_log(const char *what, unsigned char *fog)
{
    unsigned n = g_mx * g_my * g_mz, fx, ai, fg;
    int nf, nc;

    if (!g_log_path[0])                 /* no LOG: the sums would go nowhere */
        return;

    lists_used(&nf, &nc);
    fx = ck(ck(ck(0, smoke, n_smoke * sizeof(Spot)), fire, n_fire * sizeof(Spot)), dev,
            n_dev * sizeof(Device));
    ai = ck(ck(ck(0, ai_unit, MAX_UNITS * sizeof(AiUnit)), ai_spotted, MAX_UNITS), g_node,
            g_n_node * sizeof(MapNode));
    fg = ck(ck(0, fog, n), g_los_flags, n);
    log_printf("%s state: turn %d sel %d reserve %d units %d %04X map %04X floor %04X "
               "fx %d/%d/%d %04X ai %04X fog %04X hwp %d rng %08lX", what, bt_turn, bt_sel,
               bt_reserve, g_nunits, ck(0, g_units, g_nunits * sizeof(Unit)),
               ck(0, g_map, n * 4), ck(ck(0, g_floor, nf * sizeof(FloorItem)), g_carry, nc * sizeof(Carry)),
               n_smoke, n_fire, n_dev, fx,
               ai, fg, g_hwp_n, g_rng);
}

/* ------------------------------------------------------------------ */
/* Writing a battle save */

#define MONTHS  "JanFebMarAprMayJunJulAugSepOctNovDec"

static int write_save(int slot, const char *name)
{
    SaveHeader h;
    char path[13], *buf;
    unsigned long rng = g_rng;
    unsigned n;
    FILE *src = NULL;
    int quick = g_bctx.kind == BK_QUICK;

    busy_show();                /* seconds of writing */
    sprintf(path, "XCOMXT%d.SAV", slot);
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "XSG1", 4);
    strncpy(h.name, name, NAME_LEN);
    /* The geoscape's date and time stay as at the battle's start; a quick
       battle has OpenXcom's New Battle start, 12:00 on 1 Jan 1999. */
    h.day = (unsigned char)(quick ? 1 : day);
    h.month = (unsigned char)(quick ? 0 : month);
    h.year = (unsigned short)(quick ? 1999 : year);
    h.hour = (unsigned char)(quick ? 12 : hour);
    h.minute = (unsigned char)(quick ? 0 : minute);
    h.difficulty = (signed char)g_difficulty;
    h.battle = quick ? SH_QUICK : SH_BATTLE;
    h.pedia_ufos = (unsigned short)g_pedia_ufos;
    h.sizes = BS_SIZES;             /* menu.c refuses another build's */
    g_bctx.sizes = BS_SIZES;
    /* The campaign as at the battle's start (none in a quick battle), then
       the battle. */
    buf = malloc(512);
    if (!quick)
        src = fopen(BS_CAMPAIGN, "rb");
    bf = buf && (src || quick) ? fopen(BS_TMP, "wb") : NULL;   /* the slot keeps its old save until done */
    bok = bf != NULL;
    writing = 1;
    io(&h, sizeof h);
    while (bok && src && (n = fread(buf, 1, 512, src)) > 0)
        io(buf, n);
    io((void *)"XBT1", 4);
    io(&g_bctx, sizeof g_bctx);
    io_head(&rng);
    io_map();
    io_state(path_fog_table());
    io((void *)"XBTE", 4);
    if (src)
        fclose(src);
    if (bf && fclose(bf))
        bok = 0;
    if (bf && !bok)
        remove(BS_TMP);
    if (bok) {
        remove(path);
        bok = !rename(BS_TMP, path);
    }
    free(buf);
    log_printf("save slot %d \"%s\" %d %.3s %d: %s", slot, h.name, h.day,
               MONTHS + 3 * (h.month % 12), h.year, bok ? "ok" : "failed");
    if (bok)
        state_log("bsave", path_fog_table());
    return bok;
}

/* ------------------------------------------------------------------ */
/* Restoring a saved battle (play_mission) */

static unsigned free_kb(void)
{
    union REGS r;

    r.h.ah = 0x48;
    r.w.bx = 0xFFFF;
    int86(0x21, &r, &r);
    return r.w.bx / 64;
}

const char *bs_restore(unsigned char *fog)
{
    const char *err = NULL;
    unsigned long rng = 0;
    char end[4];

    if (g_bctx.sizes != BS_SIZES)
        return "saved by another version";
    bf = fopen(g_bs_path, "rb");
    if (!bf)
        return "save missing";
    bok = !fseek(bf, g_bs_off, SEEK_SET);
    writing = 0;
    io_head(&rng);
    if (!bok)
        err = "save truncated";
    /* The packs of the battle, in the order they were loaded (the unit
       sets the units refer to come out the same), with the transport's
       map the battle had (its records are on the saved map). */
    g_pak_craft = g_bctx.craft;
    if (!err)
        err = pak_load(g_pack_names[0]);
    pak_free_deploy();
    if (!err && g_pak.video != g_video + (g_vga != 0))   /* (VGA packs: 2) */
        err = "wrong video format";
    if (!err)
        err = race_load(g_pack_names[1]);
    if (!err && g_pack_names[2][0] && terror_load(g_pack_names[2]))
        err = "terror units missing";
    if (!err)
        err = armours_load();
    if (!err) {
        log_printf("bload: packs loaded: free %u KB", free_kb());
        io_map();
        if (!bok)
            err = "save truncated (map)";
    }
    /* Sprites for the map as it is now: what the battle's damage left
       needed no more than the map at its start. */
    if (!err && g_hwp_n)
        hwp_load();             /* the HWP sets of the save (hwp.c) */
    if (!err)
        err = pak_load_sprites();
    if (!err) {
        log_printf("bload: map and sprites: free %u KB", free_kb());
        pak_free_blocks();
        err = pak_vox();
    }
    if (!err) {
        err = sc_start();
        g_path_load = 1;        /* io_state reads wf and the sight flags whole */
        if (path_init() || los_init())
            err = "out of memory";
        g_path_load = 0;
    }
    if (!err) {
        fx_reset();                 /* the effects' own state; their lists come next */
        short_lists = 0;
        io_state(fog);
        io(end, 4);
        if (!bok || memcmp(end, "XBTE", 4))
            err = short_lists ? "its lists need more memory" : "save truncated (units)";
    }
    fclose(bf);
    if (err)
        return err;
    g_rng = rng;
    state_log("bload", fog);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Windows over the battlescape, as OpenXcom's battlescape states:
   216 x 160 at 52, 20 (PauseState, AbandonGameState), the battle's own
   picture (TAC00.SCR) inside */

#define WX  52
#define WY  20
#define WW  216
#define WH  160

static void popup(const char *title, int ty)
{
    uk_open();
    uk_theme(T_GREY);
    mouse_hide();
    uk_save(WY, WY + WH);        /* CGA: the window's rows on the heap */
    uk_pic_clean(UI_BG_ABORT);
    uk_fill(WX, WY, WW, WH, 0);
    uk_restore(WX, WY, WW, WH);
    uk_window(WX, WY, WW, WH);
    if (title)
        uk_center(WX, WW, ty, UK_BIG, g_uk->title, title);
}

/* A key of keys or a click on button i (bx[i], by[i], w x h); Enter =
   def, Esc and the right button = esc. */
static int wait_keys(const char *keys, const char *bx, const char *by, int w, int h, int def,
                     int esc)
{
    int n = strlen(keys);

    for (;;) {
        unsigned key = input_key();
        int ch = key & 0xFF, i;
        if (key == KEY_RCLICK)
            return esc;
        if (key == KEY_LCLICK) {
            for (i = 0; i < n; i++) {
                int x = (unsigned char)bx[i], y = (unsigned char)by[i];
                if (g_mouse_x >= x && g_mouse_x < x + w && g_mouse_y >= y && g_mouse_y < y + h)
                    return keys[i];
            }
            continue;
        }
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch == 13)
            return def;
        if (ch == 27)
            return esc;
        if (ch && strchr(keys, ch))
            return ch;
    }
}

/* PauseState from the battlescape: L, S, A, O, C (Esc) or the buttons. */
static int pause_window(void)
{
    int ch;

    popup("OPTIONS", 32);
    uk_button(70, 52, 180, 18, "Load Game", 0);
    uk_button(70, 74, 180, 18, "Save Game", 0);
    uk_button(70, 96, 180, 18, "Abandon Game", 0);
    uk_button(70, 122, 180, 18, "Game Options", 0);
    uk_button(70, 150, 180, 18, "CANCEL", 0);
    ch = wait_keys("lsaoc", "\x46\x46\x46\x46\x46", "\x34\x4A\x60\x7A\x96", 180, 18, 'c', 'c');
    uk_close();
    return ch;
}

/* Game Options: DOS's Battlescape Options screen (the X-COM technical
   supplement's GAME OPTIONS; OpenXcom's OptionsBattlescapeState shows
   OpenXcom's own set instead): the whole screen, black inside the frame,
   rows of numbered buttons that grow to the right (scroll speed also
   downwards), the chosen one pressed; OK and SAVE GAME. Settings in
   g_bopt (battle.h). Keys: R scroll speed, T scroll type, F fire speed,
   X X-COM and A alien movement speed (each the next value, round), S Save
   Game, Enter / Esc / O / the right button OK. Returns 's' for Save Game. */
static const unsigned char *battle_cmap;

static void opt_box(int r, int i, int *x, int *y, int *w, int *h)
{
    if (r == BO_SCROLL) {
        *x = 9 + 25 * i;
        *y = 41;
        *w = 22;
        *h = 14 + 4 * i;
    } else if (r == BO_STYPE) {
        *x = 148;
        *y = 42 + 15 * i;
        *w = 30;
        *h = 13;
    } else {
        *x = (unsigned char)"\x09\x22\x41\x69\x9B\xD6"[i];
        *w = (unsigned char)"\x16\x1C\x25\x2E\x38\x47"[i];
        *y = 85 + 33 * (r - BO_FIRE);
        *h = 12;
    }
}

static int game_options(void)
{
    int r, i, x, y, w, h, ch;
    unsigned key;
    char d[2];
    FILE *f;

    battle_cmap = g_uk_cmap;
    g_uk_cmap = NULL;
    gfx_night(0);
    uk_open();
    uk_theme(T_GREY);
    mouse_hide();
    uk_fill(0, 0, 320, 200, 0);
    uk_window(0, 0, 320, 200);
    uk_center(0, 320, 10, UK_BIG, g_uk->title, "GAME OPTIONS");
    for (r = 0; r < 7; r++)
        uk_text("\x09\x94\xB7\xB7\x09\x09\x09"[r], "\x20\x20\x2D\x3C\x4B\x6C\x8D"[r], UK_SMALL,
                g_uk->text, r == 0 ? "SCROLL SPEED" : r == 1 ? "SCROLL TYPE"
                : r == 2 ? "TRIGGER SCROLL" : r == 3 ? "AUTO-SCROLL" : r == 4 ? "FIRE SPEED"
                : r == 5 ? "XCOM MOVEMENT SPEED" : "ALIEN MOVEMENT SPEED");
    uk_button(9, 177, 125, 16, "OK", 0);
    uk_button(184, 177, 125, 16, "SAVE GAME", 0);
    d[1] = 0;
    for (;;) {
        for (r = 0; r < BO_N; r++)
            for (i = 0; i < "\5\2\6\6\6"[r]; i++) {
                opt_box(r, i, &x, &y, &w, &h);
                d[0] = (char)('1' + i);
                uk_button(x, y, w, h, d, g_bopt[r] == i + 1);
            }
        key = input_key();
        ch = key & 0xFF;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (key == KEY_LCLICK) {
            ch = g_mouse_y < 177 ? 0 : g_mouse_x < 134 ? 'o' : g_mouse_x >= 184 ? 's' : 0;
            for (r = 0; r < BO_N; r++)
                for (i = 0; i < "\5\2\6\6\6"[r]; i++) {
                    opt_box(r, i, &x, &y, &w, &h);
                    if (g_mouse_x >= x && g_mouse_x < x + w && g_mouse_y >= y
                        && g_mouse_y < y + h)
                        g_bopt[r] = (unsigned char)(i + 1);
                }
        }
        for (r = 0; ch && r < BO_N; r++)
            if (ch == "rtfxa"[r])
                g_bopt[r] = (unsigned char)(g_bopt[r] % "\5\2\6\6\6"[r] + 1);
        if (ch == 's' || ch == 'o' || ch == '\r' || ch == 27 || key == KEY_RCLICK)
            break;
    }
    uk_close();
    log_printf("T%d: game options: scroll %d %s, fire %d, xcom %d, alien %d", bt_turn,
               g_bopt[BO_SCROLL], g_bopt[BO_STYPE] == 1 ? "trigger" : "auto", g_bopt[BO_FIRE],
               g_bopt[BO_XCOM], g_bopt[BO_ALIEN]);
    f = fopen(BOPT_FILE, "wb");
    if (f) {
        fwrite(g_bopt, 1, BO_N, f);
        fclose(f);
    }
    g_uk_cmap = battle_cmap;
    gfx_night(g_night);
    return ch;
}

/* AbandonGameState: Yes (Enter) goes back to the start menu. */
static int abandon_window(void)
{
    int ch;

    popup(NULL, 0);
    uk_center(WX, WW, 70, UK_BIG, g_uk->title, "Abandon Game?");
    uk_button(70, 140, 50, 20, "Yes", 0);
    uk_button(200, 140, 50, 20, "No", 0);
    ch = wait_keys("yn", "\x46\xC8", "\x8C\x8C", 50, 20, 'y', 'n');
    uk_close();
    return ch == 'y';
}

/* After a full-screen list: the battlescape as it was, at night in its
   colours (battle_cmap: above). */
static void battle_back(void)
{
    g_uk_cmap = battle_cmap;
    gfx_night(g_night);
    panel_close();
    panel_open();
    render_full();              /* (after: no old visible-alien markers) */
    hud_status();
}

/* ------------------------------------------------------------------ */
/* The save and load lists (ListSaveState, ListLoadState): DOS's, as on
   the geoscape (src/saves.c), in day colours like the minimap */

static int slot_list(int saving, char *name)
{
    battle_cmap = g_uk_cmap;
    g_uk_cmap = NULL;
    gfx_night(0);
    return sv_list(saving, name);
}

/* ListSaveState: the slot and its name, then the save; back to the
   battle (player_turn draws it), else to the options window. */
static int save_screen(void)
{
    char name[NAME_LEN + 1];
    int sel, ok = 0;

    sel = slot_list(1, name);
    if (sel >= 0) {
        ok = write_save(sel, name);
        if (!ok)
            uk_notice("Could not save the game!");
    }
    if (!ok)
        battle_back();
    else {
        g_uk_cmap = battle_cmap;
        gfx_night(g_night);
    }
    return ok;
}

void bs_options(void)
{
    int ch, sel;

    log_printf("T%d: options window", bt_turn);
    for (;;) {
        ch = pause_window();
        if (ch == 'o') {
            /* OK goes back to the battlescape, as DOS. */
            if (game_options() != 's')
                return;
            ch = 's';
        }
        if (ch == 's') {
            if (g_bctx.kind == BK_NONE) {
                uk_notice("This battle cannot be saved (no disk room).");
                continue;
            }
            if (save_screen())
                return;
            continue;
        }
        if (ch == 'l') {
            /* Any save, also in a quick battle (OpenXcom's New Battle
               loads campaigns too): main.c or the geoscape loads it. */
            sel = slot_list(0, NULL);
            if (sel >= 0) {
                /* The battle ends without counting; the geoscape loads
                   the slot (geo.c, battle_exits). */
                log_printf("T%d: load slot %d", bt_turn, sel);
                g_bs_slot = sel;
                g_bs_exit = BX_LOAD;
                bt_result = BT_LOAD;
                g_uk_cmap = battle_cmap;
                return;
            }
            battle_back();
            continue;
        }
        if (ch == 'a') {
            if (abandon_window()) {
                log_printf("T%d: game abandoned", bt_turn);
                g_bs_exit = BX_ABANDON;
                bt_result = BT_ABANDON;
                return;
            }
            continue;
        }
        return;
    }
}
