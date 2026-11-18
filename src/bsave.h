/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BSAVE_H
#define BSAVE_H

/* Saving and loading in the middle of a battle, as DOS does (src/bsave.c,
   an overlay section of its own next to the battle core; docs/design.md,
   "Saving in battle").

   A battle save is a campaign save (menu.c: the slot header and
   geo_save_body as at the battle's start, kept in CAMPAIGN.TMP while the
   battle runs, since the world and the bases block are not in memory
   then) followed by a battle section: "XBT1", the BsCtx below (what the
   geoscape needs to go back into the battle and to go on after it), then
   the battle itself (packs, map, units, effects, AI, fog). */

#include "unit.h"

/* Which battle of the geoscape (geo.c): the call that runs it again;
   BK_QUICK a quick battle (main.c, OpenXcom's New Battle), saved without
   a campaign. */
enum { BK_NONE, BK_UFO, BK_SITE, BK_BASE, BK_CYD, BK_QUICK };

typedef struct {
    unsigned char kind;             /* BK_*: BK_NONE = no saving (no CAMPAIGN.TMP) */
    unsigned char stage;            /* Cydonia: 1 or 2 */
    signed char k;                  /* the transport (g_craft index), -1 none */
    signed char cur;                /* the current base before the battle */
    signed char b;                  /* base defence: the base */
    unsigned char craft;            /* the transport's map (pak.h CM_*; older saves 0,
                                       the Skyranger's) */
    int mi;                         /* base defence: the alien mission */
    int kills1, lost1;              /* Cydonia: the first stage's result */
    unsigned char day, month;       /* not used (the slot header takes geo.c's date) */
    unsigned short year;
    unsigned short sizes;           /* the build's structure sizes (bsave.c) */
} BsCtx;

extern BsCtx g_bctx;

/* What the battle section's layout depends on (bsave.c): a battle save of
   another build whose structures differ is refused (menu.c, from the slot
   header). */
extern unsigned short g_bs_sizes;
#define BS_TMP  "XCOMXT.TMP"   /* a battle save while it is written */

/* The campaign at the battle's start (geo.c writes it, bsave.c copies it
   into a battle save). */
#define BS_CAMPAIGN "CAMPAIGN.TMP"

/* menu.c found a battle section after the campaign it loaded: the next
   battle the geoscape runs (geo.c, from g_bctx) is that one, restored
   from g_bs_path at g_bs_off instead of set up. Cleared when the battle
   is under way. */
extern int g_bs_resume;
extern char g_bs_path[13];
extern long g_bs_off;

/* A battle left for Load Game (the slot in g_bs_slot) or Abandon Game:
   the geoscape loads that slot or goes back to the start menu. */
enum { BX_NONE, BX_LOAD, BX_ABANDON };
extern int g_bs_exit, g_bs_slot;

/* battle_run's results for those (battle.h: 1 won, -1 lost, -2 aborted). */
#define BT_LOAD     (-5)
#define BT_ABANDON  (-6)
#define BT_NOROOM   (-7)        /* main.c: the battle did not fit, not fought */

/* The options window of the battlescape (PauseState: Load Game, Save
   Game, Abandon Game, Options, Cancel) and what it opens; only in the
   player's turn, as in DOS. May set bt_result to BT_LOAD / BT_ABANDON.
   The caller redraws the battlescape. */
void bs_options(void);

/* play_mission instead of the setup: the saved battle's packs (by name),
   map, units and state. fog: the walk flags (fog of war). NULL = ok. */
const char *bs_restore(unsigned char *fog);

#endif
