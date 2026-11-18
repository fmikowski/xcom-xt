/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FACIL_H
#define FACIL_H

/* Base facilities on the 6x6 grid of the DOS game: building, capacities,
   maintenance, radar and base defences. */

enum {
    FAC_NONE, FAC_LIFT, FAC_QUARTERS, FAC_LAB, FAC_WORKSHOP, FAC_STORES,
    FAC_HANGAR, FAC_SRADAR, FAC_LRADAR, FAC_CONTAIN, FAC_MISSILE,
    FAC_LASER, FAC_PLASMA, FAC_GRAV,
    FAC_PSILAB, FAC_FUSION, FAC_MIND, FAC_HYPER,
    N_FACILITIES
};

/* What facilities hold (facil_capacity). */
enum { CAP_QUARTERS, CAP_LABS, CAP_WORKSHOPS, CAP_STORES, CAP_HANGARS, CAP_ALIENS,
       CAP_PSI, N_CAPS };

#define BASE_W      6
#define FAC_TYPE(c) ((c) & 0x1F)
#define FAC_PART(c) ((c) >> 5)      /* quarter of a 2x2 facility, 0 = top left */

typedef struct {
    unsigned char cell[BASE_W][BASE_W];     /* type | part << 5 */
    unsigned char days[BASE_W][BASE_W];     /* construction days left (top-left cell) */
    unsigned char hours;                    /* unused (construction is daily) */
} Facilities;

extern Facilities g_facil;           /* the current base's (base.h) */

/* The DOS facilities (facilities.rul). */
typedef struct {
    const char *name;
    const char *abbr;           /* label on the grid (3 letters, hangar 6) */
    unsigned cost_k;
    unsigned char days, upkeep_k;
    unsigned char size;         /* 1 or 2 (hangar) */
    unsigned char cap, cap_n;   /* CAP_* and how much, 0xFF = none */
    unsigned char radar;        /* range in map pixels (67.5 nautical miles) */
    unsigned char radar_pct;    /* detection chance per 30 minutes */
    unsigned defence;           /* damage per hit */
    unsigned char hit;          /* percent */
    unsigned char block;        /* XBASE_nn map block (hangar: first of 4) */
    unsigned char need;         /* research topic, REQ_NONE */
    unsigned char colour;
} FacilDef;
extern const FacilDef g_fdefs[N_FACILITIES];


void facil_new(void);               /* the DOS starting base */
/* After any change to the finished facilities: the current base's radar
   and detection summary in its header (used for every base at once). */
void facil_summary(void);

/* A day of construction (at midnight); returns a message when a facility
   is done. */
const char *facil_day(void);

int facil_capacity(int cap);        /* of finished facilities */
int facil_personnel(void);          /* scientists + engineers + soldiers */
int facil_stores_used(void);        /* tenths of a storage unit */
/* Storage space per item, tenths of a unit (items.rul sizes). */
extern const unsigned char g_item_size[N_ITEMS];
int facil_upkeep(void);             /* $K per month */

/* Radar of base b (Base::detect), every 30 minutes: a UFO at squared
   distance d2 (map pixels) is found by a hyper-wave decoder in range (2),
   else with the chances of the radars that reach it added up, times (100 +
   its visibility) % (1); 0 = not found. facil_in_range: 2 inside a
   decoder's range, 1 inside a radar's (a tracked UFO stays tracked). */
int facil_detect(int b, long d2, int visibility);
int facil_in_range(int b, long d2);
int facil_radar_range(void);        /* the current base's longest */

/* Base defences fire at an attacking UFO with hp hull points left (full
   screen); returns the hull points left, 0 = destroyed. */
int facil_defend(const char *ufo_name, int hp, int hp_max);

/* Map blocks of the base defence pack (XBASE_nn), BASE_W x BASE_W, row by
   row; 0xFF = earth. Only finished facilities appear. */
void facil_battle_layout(unsigned char *blocks);
/* After a won base defence (facilscr.c): the facilities whose block lost
   all its base objects are destroyed, then those cut off from the lift
   (Base::destroyFacility, destroyDisconnectedFacilities). */
void facil_wreck(const unsigned char *had, const unsigned char *now);

/* The base layout screen: build and dismantle; place_lift: a new base's
   first facility (the access lift) is placed instead. */
void facil_screen(int place_lift);

/* The facility at cell y * 8 + x is dismantled after a question (a click in
   the base view). */
void facil_dismantle(int cell);
/* The Base Information screen (BaseInfoState); returns what its buttons
   ask for: 0 OK, 1 Stores, 2 Transfers (the geoscape runs them, trade.c). */
int base_info_screen(void);
void costs_screen(void);           /* Monthly Costs (soldier.c): Base Information's button, code 3 */

/* The base seen from above (BASEBITS.PCK), 192 x 192 pixels at x0, y0. */
void facil_draw_view(int x0, int y0);

#endif
