/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Base screens next to the basescape, each base's list, transfers
   between bases (TransferItemsState), the soldier list and the craft list
   of the Intercept button: an overlay section of their own, so that the
   hourly section, the basescape (basescr) and one of these fit in the
   overlay area together. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alien.h"
#include "base.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "itemord.h"
#include "inv.h"
#include "map.h"
#include "qlist.h"
#include "uik.h"
#include "unit.h"

int geo_distance(int x0, int y0, int x1, int y1);

/* The screens' list lines (basescr's buffer while the basescape is open). */
char (*g_scr_lines)[41];
#define lines g_scr_lines
static char info[48];

/* "Interceptor-1" (craft_name, for lines). */
static char *craft_label(const Craft *c, char *buf)
{
    sprintf(buf, "%s-%d", g_craft_types[c->type].name, c->num);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Several bases */

/* Another base's data straight from the bases block (no calls into the
   hourly overlay section from list loops). */
static const Base *rec_of(int b)
{
    return b == g_bcur ? &g_base : (const Base *)g_bb->rec[b].base;
}

/* Column offsets as string literals: in this -zc section a named table that
   a menu points at would get an overlay vector (labscr.c). */
#define TABS(s)     ((const unsigned char *)(s))

/* TransferBaseState (transferBaseSelect): a window of 280 x 140 at 20, 30
   with the funds, NAME and AREA (big) and the other bases with their region. */
static void bases_extra(void)
{
    char m[16], t[32];

    geo_money(m, g_base.funds, g_base.funds10);
    sprintf(t, "Current Funds> %s", m);
    uk_text(30, 54, UK_SMALL, 9, t);
    uk_text(28, 64, UK_BIG, 9, "NAME");
    uk_text(160, 64, UK_BIG, 9, "AREA");
}

static const UkMenu bases_menu = {
    UI_BG_STORES, T_BLUE, UKS_LIST | UKS_POPUP, 20, 30, 280, 140, 28, 80, 248, 8, 8, 38,
    NULL, NULL, TABS("\x82"), "Esc=Cancel", 146, bases_extra
};

/* A list of the bases (type-ahead by name); skip: a base not offered
   (-1 none). Returns the chosen base or -1. */
int base_list(const char *title, int skip)
{
    UkMenu m = bases_menu;
    int map[MAX_BASES], n = 0, b, sel, own = !lines;

    if (own && (lines = malloc(MAX_BASES * sizeof *lines)) == NULL)
        return -1;
    m.title = title;
    for (b = 0; b < g_nbases; b++)
        if (b != skip) {
            sprintf(lines[n], "%.16s\t%.16s", g_bhdr[b].name, alien_region_name(g_bhdr[b].x, g_bhdr[b].y));
            map[n++] = b;
        }
    uk_invalidate();
    sel = uk_menu(&m, NULL, lines, n, 0, NULL, NULL);
    uk_invalidate();
    if (own) {
        free(lines);
        lines = NULL;
    }
    return sel < 0 ? -1 : map[sel];
}

/* Map distance between two bases in OpenXcom's units (TransferItemsState::
   getDistance: the chord on a sphere of radius 51.2), x 10. The flat map
   gives the central angle (1.125 degrees a pixel); sin by Bhaskara. */
static int base_chord10(int a, int b)
{
    int dx = abs(g_bhdr[a].x - g_bhdr[b].x), dy = abs(g_bhdr[a].y - g_bhdr[b].y), px;
    long d16, p;

    if (dx > 160)
        dx = 320 - dx;
    px = geo_distance(0, 0, dx, dy);
    if (px > 160)
        px = 160;
    d16 = px * 9L;                      /* half the angle, 1/16 degrees */
    p = d16 * (2880 - d16) / 256;       /* d (180 - d) */
    return (int)(4096L * p / (40500L - p));
}

/* Transfer rows (TransferItemsState). */
enum { XR_SOLDIER, XR_CRAFT, XR_SCI, XR_ENG, XR_ITEM };
typedef struct {
    unsigned char kind, idx;            /* XR_*, soldier / craft / item */
    unsigned have, amount;
} XRow;

#define MAX_XROWS (MAX_ROSTER + MAX_CRAFT + 2 + N_ITEMS)    /* soldiers, craft, staff, items */
static XRow *xrow;
static int xdest, xcost10;              /* cost in $ x 10 per distance unit */
static int x_quarters, x_hangars, x_aliens, x_roster;   /* free at the destination */
static long x_stores;                   /* tenths */

static void xfer_line(int i)
{
    const XRow *r = &xrow[i];
    const char *name;
    char buf[24];

    switch (r->kind) {
    case XR_SOLDIER: name = g_roster[r->idx].name; break;
    case XR_CRAFT: name = craft_label(&g_craft[r->idx], buf); break;
    case XR_SCI: name = "Scientist"; break;
    case XR_ENG: name = "Engineer"; break;
    default: name = g_items[r->idx].name;
    }
    sprintf(lines[i], "%.19s\t%u\t%u", name, r->have - r->amount, r->amount);
}

/* Unit cost in $ (items 1, staff and soldiers 5, craft 25 a distance unit). */
static long xfer_unit(const XRow *r)
{
    int k = r->kind == XR_ITEM ? 1 : r->kind == XR_CRAFT ? 25 : 5;
    return (long)k * xcost10 / 10;
}

static long xfer_total(int n)
{
    long t = 0;
    int i;
    for (i = 0; i < n; i++)
        t += xfer_unit(&xrow[i]) * xrow[i].amount;
    return t;
}

static int xfer_n;

/* One more of row i, if the destination has room. */
static const char *xfer_add(int i)
{
    XRow *r = &xrow[i];

    if (r->amount >= r->have)
        return NULL;
    switch (r->kind) {
    case XR_SOLDIER:
        if (x_roster <= 0)
            return "No room for more soldiers there.";
        /* fall through */
    case XR_SCI:
    case XR_ENG:
        if (x_quarters <= 0)
            return "No free living quarters there.";
        x_quarters--;
        if (r->kind == XR_SOLDIER)
            x_roster--;
        break;
    case XR_CRAFT:
        if (x_hangars <= 0)
            return "No free hangar there.";
        x_hangars--;
        break;
    default:
        if (r->idx >= IT_LIVE && r->idx <= IT_LIVE_LAST) {
            if (x_aliens <= 0)
                return "No alien containment space there.";
            x_aliens--;
        } else {
            int size = g_item_size[r->idx];
            if (x_stores < size)
                return "Not enough store space there.";
            x_stores -= size;
        }
    }
    r->amount++;
    return NULL;
}

static void xfer_sub(int i)
{
    XRow *r = &xrow[i];

    if (!r->amount)
        return;
    r->amount--;
    switch (r->kind) {
    case XR_SOLDIER: x_roster++; /* fall through */
    case XR_SCI:
    case XR_ENG: x_quarters++; break;
    case XR_CRAFT: x_hangars++; break;
    default:
        if (r->idx >= IT_LIVE && r->idx <= IT_LIVE_LAST)
            x_aliens++;
        else
            x_stores += g_item_size[r->idx];
    }
}

static void xfer_info(void)
{
    sprintf(info, "To %.16s   Cost: $%ld   Funds: $%ldK", g_bhdr[xdest].name,
            xfer_total(xfer_n), g_base.funds);
}

/* d rows more (or less, d < 0) of row i; why not if none. */
static int xfer_step(int i, int d)
{
    XRow *r = &xrow[i];
    const char *err = NULL;
    unsigned before = r->amount;
    int cnt = d < 0 ? -d : d;

    if (d > 0)
        while (cnt-- > 0 && r->amount < r->have && !err)
            err = xfer_add(i);
    else
        while (cnt-- > 0 && r->amount)
            xfer_sub(i);
    if (r->amount == before) {
        if (err) {
            uk_notice(err);
            return 3;           /* the notice wiped the rows */
        }
        return 0;
    }
    xfer_line(i);
    xfer_info();
    return 1;
}

/* Something chosen (Esc then asks). */
static int xfer_staged(int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (xrow[i].amount)
            return 1;
    return 0;
}

/* Tab: OK. */
static int xfer_key(int sel, int ch, int scan)
{
    (void)sel;
    (void)scan;
    return ch == 9 ? 2 : 0;
}

static const UkMenu xfer_menu = {
    UI_BG_STORES, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    "Transfer", "Item\tAt this base\tTransfer", TABS("\x78\xDE"),
    "Tab=OK|Esc=Cancel", 178, NULL
};

/* Sends the chosen rows: soldiers into the destination's roster (in
   transit), craft (on their way, CS_ORDERED), staff and items into its
   deliveries; all arrive after 6 + distance / 10 hours. */
static void xfer_execute(int n, int hours)
{
    int i, gone = 0;

    /* Soldiers leave the roster for the destination's in their order (one
       at a time through the pool, which may be full; g_roster follows it)... */
    for (i = 0; i < n; i++)
        if (xrow[i].kind == XR_SOLDIER && xrow[i].amount) {
            Soldier one = g_roster[xrow[i].idx - gone], *s;
            base_soldier_remove(g_bcur, xrow[i].idx - gone++);
            s = base_soldier_new(xdest);        /* (x_roster kept room there) */
            *s = one;
            s->transit = (unsigned char)hours;
            s->psi_flags &= ~PSF_TRAIN;
            s->craft = 0;
            log_printf("XFER soldier %s to %s, %dh", s->name, g_bhdr[xdest].name, hours);
        }
    for (i = n - 1; i >= 0; i--) {      /* ... then the rest */
        XRow *r = &xrow[i];
        if (!r->amount)
            continue;
        switch (r->kind) {
        case XR_SOLDIER:
            break;
        case XR_CRAFT: {
            Craft *c = &g_craft[r->idx];
            char buf[24];
            craft_crew_clear(c);
            c->base = (unsigned char)xdest;
            c->state = CS_ORDERED;
            c->timer = hours;
            log_printf("XFER craft %s to %s, %dh", craft_label(c, buf), g_bhdr[xdest].name, hours);
            break;
        }
        case XR_SCI:
        case XR_ENG:
            if (r->kind == XR_SCI)
                g_base.scientists -= (unsigned char)r->amount;
            else
                g_base.engineers -= (unsigned char)r->amount;
            base_transfer_to(xdest, r->kind == XR_SCI ? TR_SCIENTIST : TR_ENGINEER, r->amount,
                             hours);
            log_printf("XFER %u %s to %s, %dh", r->amount,
                       r->kind == XR_SCI ? "scientists" : "engineers", g_bhdr[xdest].name, hours);
            break;
        default:
            g_base.stores[r->idx] -= r->amount;
            base_transfer_to(xdest, r->idx + 1, r->amount, hours);
            log_printf("XFER %u %s to %s, %dh", r->amount, g_items[r->idx].name,
                       g_bhdr[xdest].name, hours);
        }
    }
}

/* Free transfer slots at the destination (one a row of staff or items). */
static int xfer_slots(void)
{
    const Base *r = rec_of(xdest);
    int i, n = 0;
    for (i = 0; i < MAX_TRANSFERS; i++)
        n += !r->arriving[i].what;
    return n;
}

void transfer_screen(void)
{
    int n = 0, i, k, sel = 0, src = g_bcur, hours, need;
    long cost;

    xdest = base_list("Select Destination Base", g_bcur);
    if (xdest < 0)
        return;
    xrow = malloc(MAX_XROWS * sizeof *xrow);
    lines = malloc(MAX_XROWS * sizeof *lines);
    if (!xrow || !lines) {
        free(xrow);
        free(lines);
        lines = NULL;
        return;
    }
    /* Room at the destination (its capacities, while it is current). */
    base_select(xdest);
    x_quarters = facil_capacity(CAP_QUARTERS) - facil_personnel();
    x_hangars = facil_capacity(CAP_HANGARS) - craft_hangars_used();
    x_aliens = facil_capacity(CAP_ALIENS) - live_aliens();
    x_stores = facil_capacity(CAP_STORES) * 10L - facil_stores_used();
    x_roster = MAX_ROSTER - g_roster_n - (int)base_arriving(TR_SOLDIER);
    base_select(src);
    xcost10 = base_chord10(src, xdest);
    hours = 6 + xcost10 / 100;

    for (i = 0; i < g_roster_n && n < MAX_XROWS; i++)
        if (!g_roster[i].transit) {         /* (not those still on their way here) */
            xrow[n].kind = XR_SOLDIER;
            xrow[n].idx = (unsigned char)i;
            xrow[n++].have = 1;
        }
    for (i = 0; i < MAX_CRAFT && n < MAX_XROWS; i++)
        if (g_craft[i].base == g_bcur && g_craft[i].state == CS_HANGAR) {
            xrow[n].kind = XR_CRAFT;
            xrow[n].idx = (unsigned char)i;
            xrow[n++].have = 1;
        }
    /* Only staff not at work on a project (Base::getAvailableScientists). */
    if ((k = g_base.scientists - base_sci_allocated()) > 0) {
        xrow[n].kind = XR_SCI;
        xrow[n++].have = k;
    }
    if ((k = g_base.engineers - base_eng_allocated()) > 0) {
        xrow[n].kind = XR_ENG;
        xrow[n++].have = k;
    }
    /* The items in DOS's order (TransferItemsState: the ruleset's item list). */
    for (k = 0; k < ITEM_ORDER_N && n < MAX_XROWS; k++)
        if (g_base.stores[i = (unsigned char)ITEM_ORDER[k]]) {
            xrow[n].kind = XR_ITEM;
            xrow[n].idx = (unsigned char)i;
            xrow[n++].have = g_base.stores[i];
        }
    for (i = 0; i < n; i++) {
        xrow[i].amount = 0;
        xfer_line(i);
    }
    {
        char b[100];                    /* LOG: the item rows in order (tests/ITEMORD.TXT) */
        int l = 0;
        for (i = 0; i < n && l < 92; i++)
            if (xrow[i].kind == XR_ITEM)
                l += sprintf(b + l, " %02X", xrow[i].idx);
        b[l] = 0;
        log_printf("XFER items:%s", b);
    }
    xfer_n = n;
    xfer_info();
    for (sel = 0;;) {
        sel = ql_menu(&xfer_menu, info, lines, n, sel, xfer_key, xfer_step);
        if (sel >= 0 || !xfer_staged(n) || uk_confirm("Leave without transferring?"))
            break;
    }
    cost = sel < 0 ? 0 : xfer_total(n);
    for (need = i = 0; sel >= 0 && i < n; i++)
        if (xrow[i].amount)
            need += xrow[i].kind >= XR_SCI ? 1 : 100;   /* 100: soldiers or craft too */
    if (need % 100 > xfer_slots())
        uk_notice("Too many transfers to that base at once.");
    else if (need) {
        char q[48];
        sprintf(q, "Transfer for $%ld (%d hours)?", cost, hours);
        if (!uk_confirm(q))
            ;
        else if (!base_pay10((cost + 5) / 10))
            uk_notice("Not enough money.");
        else
            xfer_execute(n, hours);
    }
    free(xrow);
    xrow = NULL;
    free(lines);
    lines = NULL;
}

/* The DOS Soldiers list of the current base: rank, name, missions, kills
   and whether the soldier is fit (wounded: days in hospital; transfers:
   hours to arrival; psi training), else the craft whose crew it is. Enter
   moves the soldier to the next transport here with a free seat (the DOS
   craft crew screen), or off. */
static const UkMenu soldier_menu = {
    UI_BG_SOLDIERS, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    "Soldiers", "Name\tTU\tHP\tFire\tM/K\tCraft", TABS("\x70\x88\xA0\xBA\xDA"), "Enter=Craft|Esc=OK",
    178, NULL
};

static int soldiers_done;

/* Space closes the list, as it closed the old roster page. */
static int soldiers_key(int sel, int ch, int scan)
{
    (void)sel;
    (void)scan;
    soldiers_done = ch == ' ';
    return soldiers_done ? 2 : 0;
}

/* Soldier.craft cycles through the transports here with a free seat. */
static void crew_next(Soldier *s)
{
    int k, i, n;

    if (s->recovery) {          /* CraftSoldiersState refuses the wounded */
        log_printf("CREW %s wounded%s", s->name, s->craft ? ", unassigned" : "");
        s->craft = 0;
        return;
    }
    for (k = s->craft; k < MAX_CRAFT; k++) {
        const Craft *c = &g_craft[k];
        if (!CRAFT_HERE(c) || !g_craft_types[c->type].soldiers)
            continue;
        for (i = n = 0; i < g_roster_n; i++)
            n += g_roster[i].craft == k + 1;
        if (n < craft_seats(c)) {
            s->craft = (unsigned char)(k + 1);
            log_printf("CREW %s in %s", s->name, craft_name(c));
            return;
        }
    }
    s->craft = 0;
    log_printf("CREW %s unassigned", s->name);
}

void soldiers_screen(void)
{
    char head[48], t[64];
    int i, sel = 0;

    lines = malloc((g_roster_n ? g_roster_n : 1) * sizeof *lines);
    if (!lines)
        return;
    uk_open();
    uk_invalidate();
    for (;;) {
        for (i = 0; i < g_roster_n; i++) {
            const Soldier *s = &g_roster[i];
            char st[16];
            if (s->transit)
                sprintf(st, "Transfer %dh", s->transit);
            else if (s->recovery)
                sprintf(st, "Wounded %dd", s->recovery);
            else if (s->psi_flags & PSF_TRAIN)
                strcpy(st, "Psi training");
            else
                strcpy(st, s->craft ? craft_name(&g_craft[s->craft - 1]) : "None");
            /* A line has 40 characters: a long name and a craft overran it
               (the next row's start showed after the craft). */
            sprintf(t, "%s\t%d\t%d\t%d\t%d/%d\t%.14s", s->name,
                    s->tu, s->hp, s->accuracy, s->missions, s->kills, st);
            strncpy(lines[i], t, 40);
            lines[i][40] = 0;
        }
        sprintf(head, "%.16s: %d soldiers, %d fit", g_bhdr[g_bcur].name, g_roster_n,
                roster_ready());
        soldiers_done = 0;
        sel = ql_menu(&soldier_menu, head, lines, g_roster_n, sel, soldiers_key, NULL);
        if (sel < 0 || soldiers_done)
            break;
        crew_next(&g_roster[sel]);
    }
    uk_invalidate();
    uk_close();
    free(lines);
    lines = NULL;
}


/* Size pad, never called: with the canvas (memspeed) the area is set by
   battle+ai, and geo+basescr+xfer would then fit it exactly; build.ps1's
   near-fit rule would grow the area by 3.7 KB. These bytes keep that set
   from fitting (docs/design.md, the overlay manager and "nearly fits"). */
const char *xfer_pad(void)
{
    return "xfer: size pad.";
}
