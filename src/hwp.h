/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HWP_H
#define HWP_H

/* Heavy weapons platforms (tanks), as the DOS game: the HWP item is the
   unit and its weapon; 2x2, four seats and a vehicle slot of the craft
   (CraftType.vehicles), no morale, kneeling, inventory, stun or
   experience; hovertanks fly. Battle setup in src/hwp.c, the craft's
   equipment screen and the campaign's bookkeeping in src/hwpscr.c. */

#include "items.h"

#define N_HWP       5               /* types: IT_TANK_CANNON + 0..4 */
#define MAX_HWP     4               /* an Avenger's vehicles */
/* The HWPs of a battle: base defence takes every one at the base
   (Base::setupDefenses), as many as the units (MAX_UNITS) leave room for
   next to 20 soldiers and a battleship's 28 aliens. */
#define MAX_BHWP    8
#define HWP_SEATS   4               /* seats an HWP takes (size 2 x 2) */
#define IS_HWP(it)  ((it) >= IT_TANK_CANNON && (it) <= IT_HOVER_LAUNCHER)

/* The HWPs of the coming battle (the transport's, or the HWP option of
   quick battles): their item, the rounds aboard (a full load) and the
   unit each became (-1: not placed, it stays aboard as it was). */
typedef struct {
    unsigned char item, ammo;
    signed char unit;
    signed char craft;              /* the craft it came on, -1: the stores */
} Hwp;

extern Hwp g_hwp[MAX_BHWP];
extern int g_hwp_n;
/* Cydonia's second stage (an Avenger's HWPs, the first MAX_HWP): health
   and armour (front, right, rear, left, under) the first stage left
   g_hwp[k]; health 0: undamaged. */
extern unsigned char g_hwp_hurt[MAX_HWP][6];

/* Battle setup (the overlay section of pak.c): loads the unit sets of the
   HWPs in g_hwp after the map's sprites, leaving the battle room (an HWP
   that does not fit stays aboard). */
void hwp_load(void);
unsigned long pic_armours(void);
unsigned long pic_want(unsigned long suits);
const char *pic_plan(unsigned free_para);
void hwp_notice(void);         /* (cvinit.c, after cv_battle) */
extern unsigned char g_pic_note, g_pic_early;
/* Places them on the craft's start tiles before the soldiers (units_spawn);
   returns how many soldiers come along: quick battles have 8, fewer with
   HWPs (a Skyranger's 14 seats), the campaign's transport counted them. */
int hwp_place(int soldiers);
/* 1 if a part of the tile is an X-COM start tile (START_POINT). */
int start_tile(int x, int y, int z);
/* Seats of the transport on the map (crafts.rul: Skyranger 14, Lightning
   12, Avenger 26): quick battles' squads. */
int craft_map_seats(void);
/* Quick battles, option HWP: HWPs by type digit, 1 Tank/Cannon ..
   5 Hovertank/Launcher (HWP 14: a cannon tank and a hovertank). */
void hwp_option(const char *digits);

/* The campaign (src/hwpscr.c, its own geoscape section): the transport's
   HWPs into g_hwp before a battle. */
void hwp_board(int craft);
/* Base defence (Base::setupDefenses): the HWPs aboard the base's craft that
   are not out, then those in its stores with a full load of ammunition
   (taken from the stores), as many as g_hwp holds. */
void hwp_board_base(void);
/* After the battle (DebriefingState): the HWPs that came back go to the
   stores with the rounds they have left, then each craft they came on
   takes its HWPs and their full loads from the stores again
   (reequipCraft; a lost craft keeps none). */
void hwp_after_battle(void);
/* CannotReequipState after the debriefing: the HWPs and HWP ammunition
   the stores could not give back to the craft (nothing when all were). */
void hwp_reequip_report(void);
/* The Equip Craft screen of craft k (OpenXcom CraftEquipmentState): HWPs
   and the soldiers' items between the stores and the craft. */
void hwp_equip_screen(int craft);

/* The squad's equipment before a battle (BattlescapeGenerator::deployXCOM):
   the items aboard craft k (k < 0, base defence: the base's stores and
   its craft at home) go into the stores, then to the soldiers who will
   fight (their layouts, else OpenXcom's autoEquip: rifles, pistols, a
   clip, then a second clip and one of each other item, by strength and
   weight; flares only at night) as inv.h entries, the rest to the floor at
   the craft (units_spawn puts it at the first soldier's feet). */
void equip_deploy(int craft, int night);
/* After the battle (DebriefingState::reequipCraft): craft k (k < 0: the
   base's craft at home) takes its equipment back from the stores; what
   they lack leaves its list and goes into the re-equip report. */
void equip_reequip(int craft);

#endif
