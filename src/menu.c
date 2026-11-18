/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The DOS front end: start menu, difficulty, save slots, geoscape options. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "bsave.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "inv.h"
#include "map.h"
#include "menu.h"
#include "pak.h"
#include "pedia.h"
#include "saves.h"
#include "uik.h"

int g_difficulty = -1;
const char *const g_difficulty_names[N_DIFFICULTY] = {
    "Beginner", "Experienced", "Veteran", "Genius", "Superhuman"
};

static const char *const months[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* A slot file: SaveHeader (menu.h), then the geoscape's own save data
   (geo_save_body), in a battle save then the battle (src/bsave.h); a
   quick battle's save: the header and the battle. */

/* List lines, allocated only while the front end is on screen. */
static char (*lines)[41];

char (*menu_lines(void))[41]
{
    if (!lines)
        lines = malloc(24 * 41);
    return lines;
}

void menu_free_lines(void)
{
    free(lines);
    lines = NULL;
}

/* ------------------------------------------------------------------ */
/* Save slots: the lists are DOS's (src/saves.c) */

/* One LOG line with the campaign's state after a save and a load, so a
   test can compare the two. The counts that call other sections come first:
   a string of this section's code passed in could move with it meanwhile
   (the LOG once showed garbage for "load"). */
static void log_state(int saved)
{
    int i, craft = 0, topics = 0, train = 0, res = 0, prod = 0, live, stores;

    for (i = 0; g_bb && i < g_nbases; i++) {
        res += g_bb->rec[i].n_res;
        prod += g_bb->rec[i].n_prod;
    }
    for (i = 0; i < MAX_CRAFT; i++)
        craft += g_craft[i].state != CS_EMPTY;
    for (i = 0; i < N_TOPICS; i++)
        topics += g_base.done[i];
    for (i = 0; i < g_roster_n; i++)
        train += (g_roster[i].psi_flags & PSF_TRAIN) != 0;
    live = live_aliens();
    stores = facil_stores_used();
    log_printf("%s state: funds %ld soldiers %d craft %d topics %d research %d production %d "
               "live %d stores %d psi %d", saved ? "save" : "load", g_base.funds, g_roster_n,
               craft, topics, res, prod, live, stores, train);
}

/* SaveGameState: the slot and the name typed in its line (saves.c), the
   save; back to the geoscape with no message, as DOS (an error says so). */
int menu_save(void)
{
    SaveHeader h;
    char path[13];
    int sel, ok;
    FILE *f;

    memset(&h, 0, sizeof h);
    sel = sv_list(1, h.name);
    if (sel < 0)
        return 0;
    memcpy(h.magic, "XSG1", 4);
    h.battle = SH_GEO;
    h.day = (unsigned char)day;
    h.month = (unsigned char)month;
    h.year = (unsigned short)year;
    h.hour = (unsigned char)hour;
    h.minute = (unsigned char)minute;
    h.difficulty = (signed char)g_difficulty;
    h.pedia_ufos = (unsigned short)g_pedia_ufos;
    sprintf(path, "XCOMXT%d.SAV", sel);
    busy_show();                /* the busy pointer while writing (input.h) */
    f = fopen(path, "wb");
    ok = f && fwrite(&h, sizeof h, 1, f) == 1 && geo_save_body(f);
    if (f && fclose(f))
        ok = 0;
    log_printf("save slot %d \"%s\" %d %s %d: %s", sel, h.name, day, months[month], year,
               ok ? "ok" : "failed");
    if (ok)
        log_state(1);
    else
        uk_notice("Could not save the game!");
    return ok;
}

int menu_load_slot(int sel)
{
    SaveHeader h;
    char path[13], magic[4];
    int ok, quick;
    FILE *f;

    g_bs_resume = 0;
    sprintf(path, "XCOMXT%d.SAV", sel);
    busy_show();
    f = fopen(path, "rb");
    ok = f && fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "XSG1", 4);
    if (ok && h.battle && h.sizes != g_bs_sizes) {
        /* A battle saved by another build: refused before the campaign
           in memory is replaced. */
        log_printf("load slot %d: a battle saved by another version", sel);
        ok = 0;
    }
    /* A quick battle's save has no campaign (OpenXcom's New Battle saves
       its own made-up one): the battle follows the header. */
    quick = ok && h.battle == SH_QUICK;
    /* A campaign loaded in a quick battle (main.c, g_bctx.kind BK_QUICK
       until a campaign battle is done) needs the world map first; on the
       geoscape it is in memory here. */
    if (ok && !quick)
        ok = (g_bctx.kind != BK_QUICK || !geo_init()) && geo_load_body(f);
    /* A battle save: the battle comes next (src/bsave.h). */
    if (ok && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "XBT1", 4)) {
        ok = fread(&g_bctx, sizeof g_bctx, 1, f) == 1;
        g_bs_off = ftell(f);
        strcpy(g_bs_path, path);
        g_bs_resume = ok;
    } else if (quick)
        ok = 0;
    if (f)
        fclose(f);
    if (ok) {
        /* A quick battle keeps its difficulty (-1: the packs' crews). */
        g_difficulty = quick || (h.difficulty >= 0 && h.difficulty < N_DIFFICULTY)
                     ? h.difficulty : 1;
        if (!quick)
            g_pedia_ufos = h.pedia_ufos;
        g_geo_loaded = 1;
    }
    if (quick)
        log_printf("load slot %d: %s, quick battle", sel, ok ? "ok" : "failed");
    else {
        log_printf("load slot %d: %s, now %d %s %d, %s%s", sel, ok ? "ok" : "failed", day,
                   months[month % 12], year,
                   g_difficulty_names[g_difficulty < 0 ? 1 : g_difficulty],
                   g_bs_resume ? ", in battle" : "");
        if (ok)
            log_state(0);
    }
    return ok;
}

int menu_quick_left(int res)
{
    g_bs_exit = BX_NONE;
    g_geo_loaded = 0;
    if (res != BT_LOAD || !menu_load_slot(g_bs_slot))
        return 0;
    if (g_bs_resume && g_bctx.kind == BK_QUICK)
        return 1;
    g_geo_loaded = 2;           /* menu_start: the campaign goes on, no menu */
    return 0;
}

/* LoadGameState: a slot with a game (saves.c), then the load. */
int menu_load(void)
{
    int sel, ok;

    sel = sv_list(0, NULL);
    if (sel < 0)
        return 0;
    ok = menu_load_slot(sel);
    if (!ok)
        uk_notice("Could not load the game!");
    return ok;
}

/* ------------------------------------------------------------------ */
/* Start and options menus */

static int menu_start_screen(void);

int menu_start(void)
{
    int play;

    if (g_geo_loaded == 2) {
        g_geo_loaded = 1;       /* a campaign loaded in a quick battle (menu_quick_left) */
        return 1;
    }
    play = menu_start_screen();

    menu_free_lines();
    return play;
}

static const UkMenu start_menu = {
    UI_BG_MENU, T_GREEN, UKS_BUTTONS | UKS_CLEAR, 32, 20, 256, 160, 64, 90, 192, 3, 28, 45,
    "X-COM", NULL, NULL, NULL, 0
};
static const UkMenu diff_menu = {
    UI_BG_MENU, T_GREEN, UKS_BUTTONS | UKS_CLEAR, 64, 10, 192, 180, 80, 50, 160, 5, 24, 20,
    "Difficulty", NULL, NULL, NULL, 0
};
/* The geoscape's options window as DOS 1.4 has it (PauseState from the
   geoscape: 216 x 160 at 20, 20 on BACK01; DOS has no Game Options button
   there): GAME OPTIONS, Load Game, Save Game, Abandon Game, Cancel. */
static const UkMenu options_def = {
    UI_BG_MENU, T_GREEN, UKS_BUTTONS | UKS_POPUP, 20, 20, 216, 160, 38, 52, 180, 4, 22, 30,
    "GAME OPTIONS", NULL, NULL, NULL, 0
};

static int menu_start_screen(void)
{
    if (!menu_lines())
        return 0;
    for (;;) {
        int sel, i;

        strcpy(lines[0], "New Game");
        strcpy(lines[1], "Load Saved Game");
        strcpy(lines[2], "Quit");
        uk_open();
        sel = uk_menu(&start_menu, "UFO Defense", lines, 3, 0, NULL, "nlq");
        uk_close();
        if (sel < 0 || sel == 2)
            return 0;
        if (sel == 1) {
            if (menu_load())
                return 1;
            continue;
        }
        for (i = 0; i < N_DIFFICULTY; i++)
            strcpy(lines[i], g_difficulty_names[i]);
        uk_open();
        sel = uk_menu(&diff_menu, "Select Difficulty Level", lines, N_DIFFICULTY, 0, NULL,
                      "12345");
        uk_close();
        if (sel < 0)
            continue;
        g_difficulty = sel;
        g_pedia_ufos = 0;
        g_geo_loaded = 0;
        log_printf("new game, difficulty %s", g_difficulty_names[sel]);
        return 1;
    }
}

int menu_options(void)
{
    int res = OPT_CANCEL;

    if (!menu_lines())
        return OPT_CANCEL;
    uk_open();
    for (;;) {
        int sel;

        strcpy(lines[0], "Load Game");
        strcpy(lines[1], "Save Game");
        strcpy(lines[2], "Abandon Game");
        strcpy(lines[3], "Cancel");
        sel = uk_menu(&options_def, NULL, lines, 4, 0, NULL, "lsac");
        if (sel == 0) {
            if (menu_load())
                res = OPT_LOADED;
            break;
        }
        if (sel == 1) {
            menu_save();
            break;
        }
        if (sel == 2) {
            /* AbandonGameState: Yes goes to the start menu (Quit there). */
            if (uk_confirm("Abandon Game?")) {
                res = OPT_ABANDON;
                break;
            }
            continue;
        }
        break;
    }
    uk_close();
    return res;
}

/* The difficulty hooks of the battle setup (alien_quantity, alien_adjust)
   are in unit.c: resident, as they run once per alien. */
