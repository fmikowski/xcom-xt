/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MENU_H
#define MENU_H

#include "unit.h"

/* The DOS front end: start menu, difficulty, save slots and the geoscape
   options menu. */

#define N_DIFFICULTY 5

/* 0 Beginner .. 4 Superhuman; -1 (quick battles) keeps each pack's average
   crew and the aliens' base stats. */
extern int g_difficulty;
extern const char *const g_difficulty_names[N_DIFFICULTY];

/* Start menu: new game (asks the difficulty) or load. Returns 0 to quit to
   DOS, else 1 with g_geo_loaded telling geo_run which one. */
int menu_start(void);

/* Save / load screens (10 named slots XCOMXT0.SAV .. XCOMXT9.SAV, the DOS
   lists of src/saves.c). Return 1 if a game was saved / loaded. */
int menu_save(void);
int menu_load(void);
/* Loads slot n without the list (Load Game in a battle, src/bsave.c);
   1 = ok. A battle save sets g_bs_resume (src/bsave.h); a quick battle's
   (SH_QUICK) leaves the campaign in memory as it is. A campaign save
   loads the world map first if it is not loaded (a quick battle's Load
   Game). */
int menu_load_slot(int n);
/* A quick battle left for Load Game or Abandon Game (main.c, res BT_LOAD
   or BT_ABANDON, src/bsave.h): loads the slot chosen. 1 = a quick
   battle's save, to go on with in the quick battles' loop; 0 = the
   campaign next, the one loaded (g_geo_loaded) or the start menu. */
int menu_quick_left(int res);

/* A slot file is this header followed by the geoscape's own save data
   (geo_save_body), and in a battle save the battle (src/bsave.h); a quick
   battle's save has no campaign: the battle follows the header. */
#define N_SLOTS     10
#define NAME_LEN    20
enum { SH_GEO, SH_BATTLE, SH_QUICK };   /* SaveHeader.battle */
#pragma pack(1)
typedef struct {
    char magic[4];                  /* "XSG1" */
    char name[NAME_LEN + 4];
    unsigned char day, month;       /* month 0-11 */
    unsigned short year;
    signed char difficulty;
    unsigned char battle;           /* SH_*: saved on the geoscape, in a battle, in a quick battle */
    unsigned short pedia_ufos;
    unsigned short sizes;           /* battle saves: the build's structure sizes (bsave.c) */
    unsigned char hour, minute;     /* the game's time (0 in saves before the DOS lists) */
} SaveHeader;
#pragma pack()

/* Geoscape options (Esc). */
#define OPT_CANCEL  0
#define OPT_LOADED  1
#define OPT_ABANDON 2
#define OPT_QUIT    3
#define OPT_PEDIA   4
int menu_options(void);

/* The geoscape options menu as the DOS Options button opens it (defined in
   geo.c, for the geoscape's key Esc and button panel): runs menu_options,
   then redraws the geoscape, or ends geo_run on abandon / quit. */
void options_menu(void);

/* Line buffer for lists (24 lines), allocated on first use; the geoscape
   frees it when it is back on screen. */
char (*menu_lines(void))[41];
void menu_free_lines(void);

/* Difficulty hooks for units_spawn: aliens for spawn entry s of the loaded
   pack, and the stat changes of the DOS game (TU, reactions and firing up
   with the level; firing and armour halved on Beginner). */
int alien_quantity(int s);
void alien_adjust(Unit *u);

#endif
