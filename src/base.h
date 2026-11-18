/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BASE_H
#define BASE_H

#include <stddef.h>
#include <stdio.h>
#include "items.h"
#include "craft.h"
#include "facil.h"
#include "unit.h"

/* The X-COM base: money, stores, staff, research and manufacturing. */

/* Purchases and transfers on their way to a base (Transfer): what = item
   + 1, or TR_*; hours until they arrive. Soldiers in transit are in the
   destination's roster (Soldier.transit), craft in its hangars
   (CS_ORDERED). */
#define TR_SCIENTIST    250
#define TR_ENGINEER     251
#define TR_SOLDIER      252     /* recruits */
#define MAX_TRANSFERS   24
typedef struct {
    unsigned char what, hours;
    unsigned n;
} Transfer;

/* g_base holds the current base (stores, staff, research and production
   projects, deliveries) followed by what belongs to X-COM as a whole (from
   funds on). base_select swaps the first part with the base's record, so
   code working on "the base" uses g_base, g_facil and g_roster as before. */
typedef struct {
    unsigned stores[N_ITEMS];
    unsigned char scientists, engineers;    /* all at the base, at work or not */
    Transfer arriving[MAX_TRANSFERS];
    /* --- X-COM as a whole (never swapped) --- */
    long funds;                         /* $K */
    unsigned char funds10;              /* funds below $1K, in $10 */
    unsigned char races_met;            /* bit per race 0..4 */
    unsigned char cydonia;              /* 1 = final mission available */
    unsigned char done[N_TOPICS];
} Base;

#define BASE_LOCAL  offsetof(Base, funds)

extern Base g_base;

/* ------------------------------------------------------------------ */
/* Several bases (up to 8, as in DOS). The current base lives in g_base,
   g_facil and g_roster; the others in records of the bases block, which
   also holds what the geoscape needs of every base at all times: name,
   position, radar summary, the craft and the way points.
   The block is far memory taken from DOS while the geoscape runs; around
   a battle it goes to a file (base_park) and comes back (base_unpark). */
#define MAX_BASES   8

typedef struct {
    char name[16];
    int x, y;                           /* world map pixels */
    unsigned char radar[3];             /* finished small, large radars,
                                           hyper-wave decoders */
    unsigned char detect;               /* % per 10 minutes that a retaliation
                                           UFO in sight finds the base */
} BaseHdr;

/* Way points (DOS Waypoint): a destination on the map that is no UFO,
   site or base, "Way Point-<id>"; several craft can share one, and it is
   gone when no craft flies to it any more. id 0: a free slot. */
#define MAX_WAYPOINTS   MAX_CRAFT       /* (each needs a craft flying to it) */
typedef struct {
    int x, y;                           /* map pixels */
    int id;
} WayPt;

/* Research projects and production runs of a base, several at once with
   staff allocated to each (DOS; OpenXcom ResearchProject, Production).
   They stay in the base's record (far memory, the geoscape only), never
   in g_base. Each project has its own cost (the rules' x 50-150 %) and
   progress, so several bases can research the same topic; the first to
   finish it ends the others (GeoscapeState::time1Day). */
#define MAX_RESEARCH    N_TOPICS        /* each topic once a base, as DOS: no limit */
#define MAX_PRODUCTION  N_RECIPES       /* each recipe once a base, as DOS */
#define PROD_INFINITE   0xFFFFu         /* ProdRun.total: no end */
#define PROD_SELL       0x80            /* ProdRun.recipe bit 7: each unit made
                                           is sold (OpenXcom's SELL toggle) */
#define PROD_RCP(p)     ((p)->recipe & 0x7F)
typedef struct {
    unsigned char topic;
    unsigned char sci;                  /* scientists allocated */
    unsigned spent;                     /* scientist-days so far */
    unsigned cost;                      /* this project's cost */
} ResProj;
typedef struct {
    unsigned char recipe;
    unsigned char eng;                  /* engineers allocated */
    unsigned total;                     /* units to produce, PROD_INFINITE */
    unsigned made;                      /* units produced */
    unsigned hours;                     /* engineer-hours into the next unit
                                           (paid for when it was started) */
} ProdRun;

typedef struct {
    unsigned char base[BASE_LOCAL];     /* g_base's per-base part */
    Facilities facil;
    unsigned char roster_n;             /* its soldiers in the pool (also the
                                           current base's: g_roster_n) */
    unsigned char n_res, n_prod;
    ResProj res[MAX_RESEARCH];
    ProdRun prod[MAX_PRODUCTION];
} BaseRec;

/* The DOS completion pop-ups waiting for the geoscape (GeoscapeState's
   popup list): research, production and construction at a base. Not
   saved: the geoscape shows them right after the step. */
enum { POP_RESEARCH, POP_PROD_DONE, POP_PROD_MONEY, POP_PROD_MATERIALS, POP_BUILT,
       POP_ARRIVAL, POP_USED };         /* an arrival in the list shown (gpop_arrivals) */
#define MAX_POPUPS      16
typedef struct {
    unsigned char kind, base, id;       /* POP_*, base, topic / recipe / facility / what arrived */
    unsigned n;                         /* POP_ARRIVAL: how many */
} PopEvent;

/* The Graphs screen's history (GraphsState; OpenXcom's Region / Country
   activity and funding vectors, SavedGame funds, incomes, expenditures,
   maintenance, research scores): [0] is this month, [1] the month before,
   up to 11 months back. The countries' activity of this month is in
   alien.c (al_cstate). Saved with the campaign (XS21). */
#define GR_MONTHS       12
#define GR_REGIONS      16
#define GR_COUNTRIES    20
typedef struct {
    int ralien[GR_MONTHS][GR_REGIONS], rxcom[GR_MONTHS][GR_REGIONS];
    int calien[GR_MONTHS][GR_COUNTRIES], cxcom[GR_MONTHS][GR_COUNTRIES];
    int cfund[GR_MONTHS][GR_COUNTRIES];     /* $K a month */
    long income[GR_MONTHS], expend[GR_MONTHS], maint[GR_MONTHS], balance[GR_MONTHS];
    int score[GR_MONTHS];
    long funds;                         /* the funds when last counted (geo.c) */
    unsigned long rtog, ctog;           /* toggle buttons pressed: bit per row, */
    unsigned char ftog, pad;            /* the last bit TOTAL; finance 5 bits */
} Graphs;

typedef struct {
    BaseHdr hdr[MAX_BASES];
    WayPt wp[MAX_WAYPOINTS];
    int wp_next;                        /* the next way point's id */
    Craft craft[MAX_CRAFT];             /* every base's (Craft.base) */
    BaseRec rec[MAX_BASES];
    unsigned char n_pop;                /* after rec: not in saved games */
    PopEvent pop[MAX_POPUPS];
    unsigned char popped[(N_TOPICS + 7) / 8];   /* listed in "We can now research"
                                           during this round of pop-ups */
    Graphs gr;                          /* saved by geo_save_body */
    /* Every base's soldiers, base 0's first, each base's in its order
       (BaseRec.roster_n of them); g_roster points at the current base's.
       Last: BASES.TMP and the saves take only the soldiers there are. */
    Soldier pool[MAX_SOLDIERS];
} BaseBlock;

extern BaseBlock *g_bb;                 /* NULL: no campaign (quick battles) */
extern int g_nbases, g_bcur;            /* bases, the current one */
#define g_bhdr      (g_bb->hdr)
#define g_wp        (g_bb->wp)

/* Allocates the block (1 = ok); frees it. */
int base_block_alloc(void);
void base_block_free(void);
/* Around a battle: the block to BASES.TMP and freed, then back (kept in
   memory when the file cannot be written). 0 = ok. */
int base_park(void);
int base_unpark(void);

/* Makes base b current (stores the current one in its record first). */
void base_select(int b);
/* Copies the current base into its record (before saving). */
void base_sync(void);
/* A base's record seen as a Base (its per-base fields only). */
Base *base_rec(int b);
/* A base's stores (current or not). */
unsigned *base_stores(int b);
/* Soldier i of base b and how many soldiers it has. */
Soldier *base_soldier(int b, int i);
int base_roster_n(int b);
/* The pool (g_roster moves with it): a cleared record at the end of base
   b's soldiers (NULL: the base or the pool is full); soldier i of base b
   leaves it. */
Soldier *base_soldier_new(int b);
void base_soldier_remove(int b, int i);
/* Soldiers base b can still take: MAX_ROSTER and the pool, less the
   recruits on their way there (and to every base, for the pool). Living
   quarters are facil_personnel's. */
int base_soldier_room(int b);
/* The battle's squad, in units_spawn's order (the transport's fit crew, at
   most g_squad_max; base defence: the fit ones): indices into g_roster
   (geoscape); returns how many (at most MAX_SQUAD). */
int base_squad_pick(unsigned char *idx);

/* A new base at map pixel (x, y): only an access lift (placed later), no
   staff or stores; becomes current. Its index, or -1 if there are 8. */
int base_add(int x, int y, const char *name);
/* The base is gone (destroyed by the aliens): its craft, soldiers and
   stores are lost; the bases after it move down. */
void base_remove(int b);
/* Promotions over all bases' soldiers (after a mission). */
void base_promote_all(void);
/* Region cost of a new base there ($K, regions.rul). */
int base_site_cost(int x, int y);
/* Saved games (geo_save_body): the bases block; 1 = ok. */
int base_save_body(FILE *f);
/* old: an XS24 save, whose lists have the sizes of before the limits (geo.c). */
int base_load_body(FILE *f, int old);
/* Reads the lists after the base headers, both formats (startup.c, the
   section the XS24 conversion shares); 1 = ok. */
int base_load_lists(FILE *f, int n, int old);

/* Screen of xfer.c: the current base's soldiers (recovery, transfers). The
   Intercept button's craft list is in geowin.c. */
void soldiers_screen(void);
int soldiers_list(void);                /* the Soldiers button (soldier.c); 1 = Psi Training chosen */
/* xfer.c, for the basescape: a list of the bases (skip: one left out, -1
   none; the chosen base or -1) and the transfer screen, on the
   basescape's list lines (g_scr_lines). */
int base_list(const char *title, int skip);
void transfer_screen(void);
extern char (*g_scr_lines)[41];

void base_new(void);

/* One hour of manufacturing and transfers; one day of construction and
   research (at midnight). Return a message to show, or NULL. */
const char *base_hour(void);
const char *base_day(void);

/* The month's costs in $K: salaries (also of staff on their way), craft
   rent and facility upkeep (OpenXcom Base::getMonthlyMaintenace). */
long base_monthly_costs(void);

/* Money in $10 units: base_pay10 takes the amount if the funds cover it
   (1) or nothing (0); base_add10 adds (or takes, < 0) without a check. */
int base_pay10(unsigned long amount);
void base_add10(long amount);

/* A new soldier's name from NAMES.PAK (name: 14 chars; kept if no pack). */
void base_name_soldier(char *name);

/* Staff, soldiers or items on their way to the current base (to base b);
   0 if the list is full. */
int base_transfer(int what, unsigned n, int hours);
int base_transfer_to(int b, int what, unsigned n, int hours);
unsigned base_arriving(int what);       /* how many of what are on the way */

/* Every base's hour (base_hour where something is due) and day (base_day),
   each message shown as it comes; the month (psi training) with the costs
   of all bases. */
const char *base_hour_all(void);
const char *base_day_all(void);
long base_month_all(void);

/* Research and production at the current base (base.c; the screens are
   in labscr.c). Staff at work and the space they take: allocated
   scientists (one lab place each), engineers plus each run's workshop
   space (Base::getUsedLaboratories, getUsedWorkshops). */
#define BASE_REC        (&g_bb->rec[g_bcur])
int base_sci_allocated(void);
int base_eng_allocated(void);
int base_workshops_used(void);
/* Starts topic t (cost x 50-150 %, the sample used up); index of the
   project or -1 if the list is full. */
int base_research_start(int t);
/* Stops project i: its scientists are free, the sample comes back and
   the progress is lost (Base::removeResearch). */
void base_research_cancel(int i);
/* A new production run of recipe r, 1 unit, no engineers, the first unit
   paid for; index or -1 (list full, no money or materials). */
int base_prod_start(int r);
void base_prod_stop(int i);
/* Takes the money and materials for one unit of recipe r (1), or 0. */
int base_pay_item(int r);
/* Queues a completion pop-up at the current base (dropped when full). */
void base_popup(int kind, int id);
void base_arrival(int what, unsigned n);    /* a transfer arrived (Transfer.what), for ItemsArrivingState */
/* A topic the current base can start (NewResearchListState): needs met,
   not done, not under way at this base. */
int base_topic_available(int t);
/* Recipe r can be made (researched; the hovertanks and their fusion
   bombs also need New Fighter Craft, manufacture.rul). */
int base_recipe_known(int r);
/* Ship runs (units still to build) that keep hangars free. */
int base_ships_in_production(void);

/* Recovery after a won mission: UFO components (crashed: most power
   sources lost), alien gear, the autopsy. race: 0 sectoid .. 4 ethereal,
   -1 unknown. Returns a summary line; g_loot_score: the recovery points of
   the components, Elerium and alloys. */
/* The debriefing's lines (DebriefingState's stats, in DOS's order): the
   quantity and score of each; UFO recovery from DS_POWER on (the tile
   special types 2-11 in order, then the Elerium). */
enum { DS_KILLED, DS_CORPSES, DS_LIVE, DS_ARTIFACTS, DS_BASE_CONTROL, DS_CIV_ALIENS,
       DS_CIV_XCOM, DS_CIV_SAVED, DS_SOLDIERS, DS_TANKS, DS_CRAFT,
       DS_POWER, DS_NAV, DS_CONSTR, DS_FOOD, DS_REPRO, DS_ENTERT, DS_SURGERY, DS_EXAM,
       DS_ALLOYS, DS_HABITAT, DS_ELERIUM, DS_N };
typedef struct {
    int qty[DS_N], score[DS_N];
} DebStats;

/* ufo_type 0: no UFO (alloys / 150). Fills the artifacts and the UFO
   recovery of ds. */
const char *base_loot(int ufo_type, int crashed, int aliens_killed, int race, DebStats *ds);
extern int g_loot_score;
/* The battle map's tile parts by MCD special type 2-11 after the battle
   (main.c, for base_loot). */
extern unsigned g_tile_loot[10];

/* Full-screen base menu (research, manufacture, buy, hire, equip, stores,
   transfers, base selection; basescr.c). Returns BS_* for the geoscape. */
enum { BS_DONE, BS_NEW_BASE, BS_FACIL, BS_PSI, BS_TRANSFER, BS_SELECT, BS_RESEARCH,
       BS_MANUF, BS_EQUIP, BS_BUY, BS_SELL, BS_STORES, BS_XFERS, BS_INFO, BS_DISMANTLE, BS_SOLDIERS,
       BS_ALIENS };
extern int g_base_cell;         /* BS_DISMANTLE: the facility clicked (y * 8 + x) */
int base_screen(void);
/* BS_EQUIP: the geoscape runs the craft screens (craft_screens, src/hangar.c). */
/* labscr.c (an overlay section of its own, run from the geoscape): the
   DOS research and manufacture screens of the current base. */
void research_screen(void);
void manufacture_screen(void);
/* trade.c (an overlay section of its own, run from the geoscape): the
   quantity screens of the current base: Purchase/Recruit, Sell, Stores and
   Transfers (what is on its way here). */
void purchase_screen(void);
void sell_screen(void);
void stores_screen(void);
void transfers_screen(void);
/* contain.c (an overlay section of its own, run from the geoscape): Manage
   Alien Containment of the current base; contain_check opens it after a
   battle when the base holds more aliens than its containment. */
void contain_screen(void);
void contain_check(void);



#endif
