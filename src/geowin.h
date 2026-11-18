/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GEOWIN_H
#define GEOWIN_H

/* The geoscape's windows of the DOS game, laid out as OpenXcom's Geoscape
   states: the targets under a click (MultipleTargetsState), Launch
   Interception (InterceptState), a craft in flight (GeoscapeCraftState),
   a UFO (UfoDetectedState), a terror site or alien base
   (TargetInfoState), Select Destination and Confirm Destination. Drawing
   and the player's choice only: geo.c acts on the result. Compiled with
   -zc in an overlay section of its own next to the geoscape's. */

/* A target on the map (Globe::getTargets). */
enum { GT_BASE, GT_CRAFT, GT_UFO, GT_SITE, GT_POINT };
typedef struct {
    unsigned char kind;         /* GT_* */
    unsigned char idx;          /* base, craft (g_craft), UFO slot, site */
    unsigned char sub;          /* UFO: 0 flying, 1 landed, 2 crashed */
    unsigned char pad;
    int id;                     /* UFO: its number */
} GeoTarget;
#define GW_MAX_TARGETS  8

/* Results. */
enum { GW_CANCEL, GW_OK, GW_GOTO_BASE, GW_CENTRE, GW_RETURN, GW_TARGET, GW_PATROL,
       GW_INTERCEPT };

/* The target's name as the DOS game shows it ("UFO-3", "Crash Site-3",
   "Terror Site-1", "Way Point", a base or craft name) in s (24 bytes). */
const char *gw_name_buf(const GeoTarget *t, char *s);

/* Several targets under the click: a button for each; the chosen one or
   -1. One target is not asked about. */
int gw_pick(const GeoTarget *t, int n);

/* Launch Interception: the craft of base (-1: every base), status, base,
   weapons / crew. A craft that can go (ready, or in flight and not low on
   fuel) is picked by a click or Enter: GW_OK, *craft. A right click on a
   craft in flight: GW_CENTRE, *craft. GW_GOTO_BASE (base >= 0), or
   GW_CANCEL. */
int gw_intercept(int base, int *craft);

/* A craft in flight (GeoscapeCraftState). status: GWS_*; dest: the
   destination (GWS_DEST, GWS_INTERCEPT); alt: the altitude 0-4 (0 shown
   as very low), knots: its speed now. GW_RETURN, GW_TARGET, GW_PATROL or
   GW_CANCEL. GWS_LOST: its UFO (dest->id) went off radar, Cancel reads
   "Go to Last Known UFO Position" (a pop-up). */
enum { GWS_RETURN, GWS_LOW_FUEL, GWS_PATROL, GWS_INTERCEPT, GWS_DEST, GWS_LOST };
int gw_craft(int craft, int status, const GeoTarget *dest, int alt, int knots);

/* A UFO (UfoDetectedState, as opened by a click: "Detected" left out). */
typedef struct {
    GeoTarget t;
    int size;                   /* 0 very small .. 4 very large */
    int alt;                    /* 0 ground .. 4 very high */
    int dx, dy;                 /* heading (screen directions), 0, 0: none */
    int knots;
    int decoded;                /* hyper-wave decoder: the lines below */
    int detected;               /* opened by the detection: "Detected" */
    const char *type, *race, *mission, *zone;
} GwUfo;
/* GW_INTERCEPT, GW_CENTRE or GW_CANCEL. */
int gw_ufo(const GwUfo *u);

/* A terror site or alien base (TargetInfoState): name, place, the craft
   on their way there. GW_INTERCEPT or GW_CANCEL. */
int gw_site(const GeoTarget *t, const char *place, const int *craft, int n);

/* Confirm Destination: "Target: <name>", OK = 1. */
int gw_confirm(const GeoTarget *t);

/* Select Destination: the window under the map with its Cancel button and,
   if cydonia, the Cydonia button (SelectDestinationState: the spacecraft
   once the final research is done); gw_select_click says which button a
   click hit: 1 Cancel, 2 Cydonia, 0 none. */
void gw_select_bar(int cydonia);
int gw_select_click(void);
/* ConfirmCydoniaState: "Are you sure ...", Yes = 1. */
int gw_cydonia(void);

/* Speeds in knots (crafts.rul maxSpeed) by CT_*. */
int gw_craft_knots(int type);

/* The DOS pop-ups of the time steps (GeoscapeState::time5Seconds,
   time10Minutes, time30Minutes). UfoLostState: "UFO-n Tracking Lost",
   OK. */
void gw_lost(int id);
/* CraftPatrolState: the craft has reached dest, now patrolling; OK (0)
   or Redirect Craft (1). */
int gw_patrol(int craft, const GeoTarget *dest);
/* LowFuelState: OK (0) or OK - 5 Secs (1). */
int gw_low_fuel(int craft);
/* CraftErrorState: not enough ammunition or Elerium-115 (stores item) to
   rearm or refuel craft; OK (0) or OK - 5 Secs (1). */
int gw_craft_error(int craft, int item);
/* MissionDetectedState for a terror site: GW_INTERCEPT, GW_CENTRE or
   GW_CANCEL. */
int gw_terror(const char *city);
/* AlienBaseState: agents have located an alien base in region. */
void gw_base_found(const char *region);
/* ConfirmLandingState: the craft is ready to land near dest; Yes = 1. */
int gw_landing(int craft, const GeoTarget *dest);

#endif
