/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The base screens that move quantities (the DOS layouts, src/uik.c):
   Purchase/Recruit, Sell, the stores and Transfers (what is on its way to
   the base). The geoscape runs them (BS_BUY ...) for the basescape
   (basescr.c); the lists, with their arrows and scroll bars, are qlist.c's.
   An overlay section of its own, compiled with -zc: its tables and strings
   stay in the section's code, so tab stops are string literals. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "gfx.h"
#include "hwp.h"
#include "hud.h"
#include "input.h"
#include "itemord.h"
#include "inv.h"
#include "map.h"
#include "psi.h"
#include "qlist.h"
#include "uik.h"
#include "unit.h"

#define TABS(s)     ((const unsigned char *)(s))

static int item_at(int k)
{
    return (unsigned char)ITEM_ORDER[k];
}

/* How many of what are on their way to the current base (base_arriving,
   here so that list lines need no call into the hourly section). */
static unsigned arriving(int what)
{
    unsigned n = 0;
    int i;

    for (i = 0; i < MAX_TRANSFERS; i++)
        if (g_base.arriving[i].what == what)
            n += g_base.arriving[i].n;
    return n;
}

/* ------------------------------------------------------------------ */
/* Screens (the DOS layouts, src/uik.c) */

/* The screens' list lines: on the heap while one is open (the basescape's
   are freed before the geoscape runs these). */
#define MAX_LINES (MAX_ROSTER + MAX_CRAFT + 2 + N_ITEMS)   /* Sell: soldiers, craft, staff and items */
#define lines g_scr_lines        /* xfer.c */

/* What the quantity screens keep (on the heap while one is open): a row's
   kind and item, the quantity chosen, and what the order has used up. */
typedef struct {
    unsigned char kind, idx;
    unsigned qty;
} PRow;
typedef struct {
    PRow row[MAX_LINES];
    char info[48];
    int n;
    unsigned long tot10;                /* price of the order, $10 */
    long funds10;
    int q_free, q_cap, s_free, h_free;  /* quarters, soldier places, hangars left */
    long st_free;                       /* store space left, tenths */
} Work;
static Work *bw;

/* Something chosen in the rows (Esc then asks). */
static int staged(void)
{
    int i;

    for (i = 0; i < bw->n; i++)
        if (bw->row[i].qty)
            return 1;
    return 0;
}

/* LOG: the item numbers (hex) of the rows of kind k in list order, for the
   tests of the order (tests/ITEMORD.TXT). */
static void log_items(const char *what, int n, int k)
{
    char b[100];
    int i, l = 0;

    for (i = 0; i < n && l < 92; i++)
        if (bw->row[i].kind == k)
            l += sprintf(b + l, " %02X", bw->row[i].idx);
    b[l] = 0;
    log_printf("%s items:%s", what, b);
}

static int open_work(void)
{
    bw = malloc(sizeof *bw);
    lines = malloc(MAX_LINES * sizeof *lines);
    if (bw && lines)
        return 1;
    free(bw);
    free(lines);
    bw = NULL;
    lines = NULL;
    return 0;
}

static void close_work(void)
{
    free(bw);
    free(lines);
    bw = NULL;
    lines = NULL;
}

/* ------------------------------------------------------------------ */
/* Purchase / Recruit (PurchaseState): personnel, craft and items in one
   list. The arrows and keys choose quantities, Tab (OK) pays and orders
   them, Esc leaves without. Everything bought is a delivery on its way to
   the base (Transfers; craft: the hangar list, due in hours). */

enum { PK_SOLDIER, PK_SCI, PK_ENG, PK_CRAFT, PK_ITEM };

/* The craft for sale (crafts.rul costBuy, transferTime; string literals,
   as tables here would be read through an overlay vector, -zc): purchase
   k 0 the Interceptor (96 hours), 1 the Skyranger (72). An order: the
   craft holds its hangar at once and is delivered empty, due in its hours
   (the craft list shows "Due", the Transfers screen lists it). */
#define craft_buy_type(k)   ((const unsigned char *)"\0\4")[k]
#define craft_buy_hours(k)  ((const unsigned char *)"\x60\x48")[k]

static int hangar_order(int k)
{
    Craft *c = craft_new_slot(craft_buy_type(k));

    if (!c)
        return 0;
    c->timer = craft_buy_hours(k);
    c->fuel = 0;                        /* delivered empty: it refuels */
    log_printf("CRAFT %s bought", craft_name(c));
    return 1;
}

/* Price in $10 (staff: scientists $60K, engineers $50K, soldiers $40K). */
static unsigned long buy_price(const PRow *r)
{
    switch (r->kind) {
    case PK_SOLDIER: return 4000UL;
    case PK_SCI: return 6000UL;
    case PK_ENG: return 5000UL;
    case PK_CRAFT: return g_craft_types[craft_buy_type(r->idx)].cost_k * 100UL;
    }
    return g_buy10[r->idx];
}

static const char *buy_name(const PRow *r)
{
    switch (r->kind) {
    case PK_SOLDIER: return "Soldier";
    case PK_SCI: return "Scientist";
    case PK_ENG: return "Engineer";
    case PK_CRAFT: return g_craft_types[craft_buy_type(r->idx)].name;
    }
    return g_items[r->idx].name;
}

/* A price in $10 as text, as DOS spaces its thousands: $600 000. */
static void cost_str(char *buf, unsigned long p10)
{
    geo_money(buf, (long)(p10 / 100), (int)(p10 % 100));
}

static void buy_line(int i)
{
    const PRow *r = &bw->row[i];
    unsigned have = 0, coming = 0;
    char p[12];
    int k;

    cost_str(p, buy_price(r));

    switch (r->kind) {
    case PK_SOLDIER: have = roster_alive(); coming = arriving(TR_SOLDIER); break;
    case PK_SCI: have = g_base.scientists; coming = arriving(TR_SCIENTIST); break;
    case PK_ENG: have = g_base.engineers; coming = arriving(TR_ENGINEER); break;
    case PK_CRAFT:
        for (k = 0; k < MAX_CRAFT; k++)
            if (CRAFT_HERE(&g_craft[k]) && g_craft[k].type == craft_buy_type(r->idx)) {
                if (g_craft[k].state == CS_ORDERED)
                    coming++;
                else
                    have++;
            }
        break;
    default:
        have = g_base.stores[r->idx];
        coming = arriving(r->idx + 1);
    }
    if (coming)
        sprintf(lines[i], "%.19s\t%s\t%u +%u\t%u", buy_name(r), p, have, coming, r->qty);
    else
        sprintf(lines[i], "%.19s\t%s\t%u\t%u", buy_name(r), p, have, r->qty);
}

/* PurchaseState's "Current Funds>" and "Cost of Purchases>", shortened to
   the line's room; money as DOS spaces it. */
static void buy_info(void)
{
    char m[16], n[16];

    geo_money(m, g_base.funds, g_base.funds10);
    cost_str(n, bw->tot10);
    sprintf(bw->info, "Funds> %s   Purchases> %s", m, n);
}

/* One more of a row, or why not. */
static const char *buy_add(PRow *r)
{
    unsigned long p = buy_price(r);

    if (bw->tot10 + p > (unsigned long)bw->funds10)
        return "Not enough money.";
    switch (r->kind) {
    case PK_SOLDIER:
    case PK_SCI:
    case PK_ENG:
        /* Living quarters as DOS (Base::getAvailableQuarters); soldiers
           also MAX_ROSTER a base and MAX_SOLDIERS in all (unit.h). */
        if (bw->q_free <= 0)
            return "No free living quarters.";
        if (r->kind == PK_SOLDIER) {
            if (bw->s_free <= 0)
                return "No more soldiers (roster full).";
            bw->s_free--;
        }
        bw->q_free--;
        break;
    case PK_CRAFT:
        if (bw->h_free <= 0)
            return "No free hangar.";
        bw->h_free--;
        break;
    default:
        /* Bought items arrive after their delivery time (and take their
           stores space at once). */
        if (bw->st_free < g_item_size[r->idx])
            return "No room in the general stores.";
        bw->st_free -= g_item_size[r->idx];
    }
    r->qty++;
    bw->tot10 += p;
    return NULL;
}

static void buy_sub(PRow *r)
{
    switch (r->kind) {
    case PK_SOLDIER: bw->s_free++; /* fall through */
    case PK_SCI:
    case PK_ENG: bw->q_free++; break;
    case PK_CRAFT: bw->h_free++; break;
    default: bw->st_free += g_item_size[r->idx];
    }
    r->qty--;
    bw->tot10 -= buy_price(r);
}

static int buy_step(int i, int d)
{
    PRow *r = &bw->row[i];
    const char *err = NULL;
    int cnt = d < 0 ? -d : d, done = 0;

    if (d > 0)
        while (cnt-- > 0 && !(err = buy_add(r)))
            done++;
    else
        while (cnt-- > 0 && r->qty) {
            buy_sub(r);
            done++;
        }
    if (!done) {
        if (err) {
            if (r->kind <= PK_ENG)
                log_printf("BASE hire %s: %d/%d %s", buy_name(r), bw->q_cap - bw->q_free,
                           bw->q_cap, err);
            uk_notice(err);
            return 3;           /* the notice wiped the rows */
        }
        return 0;
    }
    buy_line(i);
    buy_info();
    return 1;
}

/* Tab: OK, if the deliveries fit the list of what is on its way. */
static int buy_key(int sel, int ch, int scan)
{
    int i, need = 0, free_ = 0;
    (void)sel;
    (void)scan;

    if (ch != 9)
        return 0;
    for (i = 0; i < bw->n; i++)
        need += bw->row[i].qty && bw->row[i].kind != PK_CRAFT;
    for (i = 0; i < MAX_TRANSFERS; i++)
        free_ += !g_base.arriving[i].what;
    if (need > free_) {
        uk_notice("Too many deliveries on their way: order fewer kinds.");
        return 1;               /* (the rows are drawn again) */
    }
    return 2;
}

static void buy_commit(void)
{
    int i, k, ok;

    for (i = 0; i < bw->n; i++) {
        PRow *r = &bw->row[i];
        unsigned long p;
        int hours = 72;

        if (!r->qty)
            continue;
        p = buy_price(r) * r->qty;
        if (!base_pay10(p)) {
            uk_notice("Not enough money.");
            break;
        }
        if (r->kind == PK_CRAFT) {
            for (k = ok = 0; k < (int)r->qty; k++)
                if (hangar_order(r->idx))
                    ok++;
                else
                    base_add10(buy_price(r));
            hours = craft_buy_hours(r->idx);
            r->qty = ok;
            ok = ok > 0;
        } else {
            if (r->kind == PK_ITEM)
                hours = item_transfer_hours(r->idx);
            ok = base_transfer(r->kind == PK_SOLDIER ? TR_SOLDIER : r->kind == PK_SCI
                               ? TR_SCIENTIST : r->kind == PK_ENG ? TR_ENGINEER : r->idx + 1,
                               r->qty, hours);
            if (!ok)
                base_add10(p);
        }
        if (ok)
            log_printf("BASE buy %u %s for $%lu, due in %dh", r->qty, buy_name(r), p * 10, hours);
        if (ok && r->kind <= PK_ENG)
            log_printf("BASE hire %s: %d/%d", buy_name(r), bw->q_cap - bw->q_free, bw->q_cap);
    }
}

static const UkMenu buy_menu = {
    UI_BG_STORES, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    "Purchase/Hire Personnel", "Item\tCost\tStock (+due)\tBuy", TABS("\x5A\x8E\xDE"),
    "Tab=OK|Esc=Cancel", 178, NULL
};

void purchase_screen(void)
{
    int n = 0, i, k, sel = 0;

    if (!open_work())
        return;
    memset(bw, 0, sizeof *bw);
    for (k = 0; k < 3; k++) {
        bw->row[n].kind = (unsigned char)k;
        buy_line(n++);
    }
    for (k = 0; k < 2; k++) {
        bw->row[n].kind = PK_CRAFT;
        bw->row[n].idx = (unsigned char)k;
        buy_line(n++);
    }
    for (k = 0; k < ITEM_ORDER_N && n < MAX_LINES; k++)
        if (g_buy10[i = item_at(k)]) {
            bw->row[n].kind = PK_ITEM;
            bw->row[n].idx = (unsigned char)i;
            buy_line(n++);
        }
    bw->n = n;
    log_items("BUY", n, PK_ITEM);
    bw->funds10 = g_base.funds * 100 + g_base.funds10;
    bw->q_cap = facil_capacity(CAP_QUARTERS);
    bw->q_free = bw->q_cap - facil_personnel();
    bw->s_free = base_soldier_room(g_bcur);
    bw->h_free = craft_hangar_free() ? facil_capacity(CAP_HANGARS) - craft_hangars_used() : 0;
    bw->st_free = facil_capacity(CAP_STORES) * 10L - facil_stores_used();
    buy_info();
    for (;;) {
        sel = ql_menu(&buy_menu, bw->info, lines, n, sel, buy_key, buy_step);
        if (sel >= 0) {
            buy_commit();
            break;
        }
        if (!staged() || uk_confirm("Leave without ordering?"))
            break;
    }
    close_work();
}

static const UkMenu stores_menu = {
    UI_BG_STORES, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    "Stores", "ITEM\tQUANTITY\tSPACE USED", TABS("\xA0\xDC"), "Esc=OK", 178, NULL
};

void stores_screen(void)
{
    int n = 0, i, k;

    if (!open_work())
        return;
    for (k = 0; k < ITEM_ORDER_N && n < MAX_LINES; k++)
        if (g_base.stores[i = item_at(k)])
        {
            /* The space used as DOS prints it: 3, 0.2, 2.5 (sizes are tenths). */
            unsigned long v = (unsigned long)g_base.stores[i] * g_item_size[i];
            sprintf(lines[n], v % 10 ? "%s\t%u\t%lu.%lu" : "%s\t%u\t%lu", g_items[i].name,
                    g_base.stores[i], v / 10, v % 10);
            n++;
        }
    ql_menu(&stores_menu, NULL, lines, n, 0, NULL, NULL);
    close_work();
}

/* ------------------------------------------------------------------ */
/* Sell (SellState): the hangar's craft first, then the stores' items with
   a DOS sale price. Quantities as in Purchase; Tab (OK) sells them. */

enum { SK_CRAFT, SK_ITEM, SK_SOLDIER, SK_SCI, SK_ENG };
/* A craft's sale price, $K (crafts.rul costSell: the ruleset gives none). */
#define CRAFT_SELL_K    0

/* Sale price in $10: the hovertanks' prices do not fit g_sell10. */
static unsigned long sell10(int item)
{
    return item == IT_HOVER_PLASMA ? 98000UL : item == IT_HOVER_LAUNCHER ? 104300UL
         : g_sell10[item];
}

/* base_add10 here: the section's loops run inside the menu and must not
   call into the hourly section more than needed. */
static void add10(long amount)
{
    long t = g_base.funds * 100 + g_base.funds10 + amount;
    long k = t >= 0 ? t / 100 : -((99 - t) / 100);

    g_base.funds = k;
    g_base.funds10 = (unsigned char)(t - k * 100);
}

static void sell(int item, unsigned n)
{
    if (!n || g_base.stores[item] < n)
        return;                 /* sold out */
    g_base.stores[item] -= n;
    add10((long)sell10(item) * n);
    log_printf("SELL %u %s for $%lu, funds $%ldK", n, g_items[item].name,
               sell10(item) * n * 10, g_base.funds);
}

/* SellState: the craft goes (not one that is out), its weapons with their
   rounds, the HWPs aboard with theirs and its equipment back into the
   stores (Craft::unload), its crew unassigned. */
static void sell_craft(int k)
{
    Craft *c = &g_craft[k];
    int i;

    for (i = 0; i < 2; i++)
        if (c->weapon[i] != CW_NONE) {
            const CraftWeapon *w = &g_craft_weapons[c->weapon[i]];
            g_base.stores[w->launcher]++;
            if (w->ammo_item != NO_ITEM)
                g_base.stores[w->ammo_item] += c->ammo[i] / w->per_item;
        }
    for (i = 0; i < MAX_HWP && c->hwp[i]; i++) {
        const ItemDef *t = &g_items[c->hwp[i]];
        g_base.stores[c->hwp[i]]++;
        if (t->clip_item != NO_ITEM)
            g_base.stores[t->clip_item] += t->clip_size;
    }
    for (i = 0; i < N_ITEMS; i++)
        g_base.stores[i] += c->items[i];
    craft_crew_clear(c);
    add10(CRAFT_SELL_K * 100L);
    log_printf("SELL craft %s for $%dK, funds $%ldK", craft_name(c), CRAFT_SELL_K, g_base.funds);
    c->state = CS_EMPTY;
}

/* SellState sacks personnel too: every soldier of the base (not one on a
   craft that is out) is a row of his own, scientists and engineers are
   rows with a quantity (only those without a project: Base::
   getAvailableScientists / Engineers); they sell for nothing. */
static unsigned long sell_price(const PRow *r)
{
    switch (r->kind) {
    case SK_CRAFT: return CRAFT_SELL_K * 100UL;
    case SK_ITEM: return sell10(r->idx);
    }
    return 0;
}

static unsigned sell_have(const PRow *r)
{
    switch (r->kind) {
    case SK_CRAFT:
    case SK_SOLDIER: return 1;
    case SK_SCI: return g_base.scientists - base_sci_allocated();
    case SK_ENG: return g_base.engineers - base_eng_allocated();  /* (not the workshop space) */
    }
    return g_base.stores[r->idx];
}

static void sell_line(int i)
{
    const PRow *r = &bw->row[i];
    char p[12];
    const char *name;

    cost_str(p, sell_price(r));
    switch (r->kind) {
    case SK_CRAFT: name = craft_name(&g_craft[r->idx]); break;
    case SK_SOLDIER: name = g_roster[r->idx].name; break;
    case SK_SCI: name = "Scientist"; break;
    case SK_ENG: name = "Engineer"; break;
    default: name = g_items[r->idx].name;
    }
    sprintf(lines[i], "%.19s\t%s\t%u\t%u", name, p, sell_have(r), r->qty);
}

/* A soldier leaves the service: off his craft, his armour back in the
   stores (Base::removeSoldier). */
static void sack(int k)
{
    Soldier *s = &g_roster[k];

    if (s->armour)
        g_base.stores[g_armours[s->armour].item]++;
    s->armour = 0;
    s->craft = 0;
    s->psi_flags &= (unsigned char)~PSF_TRAIN;
    s->alive = 0;
    log_printf("SELL sacked %s, %d soldiers left", s->name, roster_alive());
}

static void sell_info(void)
{
    char m[16], n[16];

    cost_str(n, bw->tot10);
    geo_money(m, g_base.funds, g_base.funds10);
    sprintf(bw->info, "VALUE OF SALES> %s   FUNDS> %s", n, m);
}

static int sell_step(int i, int d)
{
    PRow *r = &bw->row[i];
    unsigned have = sell_have(r), was = r->qty;

    if (d > 0)
        r->qty = (unsigned)d > have - r->qty ? have : r->qty + d;
    else
        r->qty = (unsigned)-d > r->qty ? 0 : r->qty + d;
    if (r->qty == was)
        return 0;
    if (r->qty > was)
        bw->tot10 += sell_price(r) * (r->qty - was);
    else
        bw->tot10 -= sell_price(r) * (was - r->qty);
    sell_line(i);
    sell_info();
    return 1;
}

/* A sells all of the row; Tab is OK. */
static int sell_key(int sel, int ch, int scan)
{
    (void)scan;
    if (ch == 9)
        return 2;
    if (ch == 'a' && sell_step(sel, QL_ALL))
        return 1;
    return 0;                   /* type-ahead */
}

static const UkMenu sell_menu = {
    UI_BG_STORES, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    "Sell Items/Sack Personnel", "Item\tValue\tQuantity\tSell/Sack", TABS("\x5A\x8E\xDE"),
    "Tab=Sell/Sack|A=All|Esc=Cancel", 178, NULL
};

void sell_screen(void)
{
    int n = 0, i, k, sel;

    if (!open_work())
        return;
    memset(bw, 0, sizeof *bw);
    for (i = 0; i < g_roster_n && n < MAX_LINES - 64; i++) {
        const Soldier *s = &g_roster[i];
        /* Not one on a craft that is out (SellState). */
        if (s->alive && !s->transit
            && !(s->craft && !s->recovery && g_craft[s->craft - 1].state != CS_HANGAR)) {
            bw->row[n].kind = SK_SOLDIER;
            bw->row[n].idx = (unsigned char)i;
            sell_line(n++);
        }
    }
    for (i = 0; i < MAX_CRAFT; i++)
        if (CRAFT_HERE(&g_craft[i]) && g_craft[i].state == CS_HANGAR) {
            bw->row[n].kind = SK_CRAFT;
            bw->row[n].idx = (unsigned char)i;
            sell_line(n++);
        }
    for (i = SK_SCI; i <= SK_ENG; i++) {
        bw->row[n].kind = (unsigned char)i;
        if (sell_have(&bw->row[n]))
            sell_line(n++);
    }
    for (k = 0; k < ITEM_ORDER_N && n < MAX_LINES; k++)
        if (g_base.stores[i = item_at(k)] && g_sell10[i]) {
            bw->row[n].kind = SK_ITEM;
            bw->row[n].idx = (unsigned char)i;
            sell_line(n++);
        }
    bw->n = n;
    log_items("SELL", n, SK_ITEM);
    sell_info();
    for (sel = 0;;) {
        sel = ql_menu(&sell_menu, bw->info, lines, n, sel, sell_key, sell_step);
        if (sel >= 0 || !staged() || uk_confirm("Leave without selling?"))
            break;
    }
    if (sel >= 0)
        for (i = 0; i < n; i++) {
            const PRow *r = &bw->row[i];
            if (!r->qty)
                continue;
            switch (r->kind) {
            case SK_CRAFT: sell_craft(r->idx); break;
            case SK_SOLDIER: sack(r->idx); break;
            case SK_SCI:
                g_base.scientists -= r->qty;
                log_printf("SELL sacked %u Scientist", r->qty);
                break;
            case SK_ENG:
                g_base.engineers -= r->qty;
                log_printf("SELL sacked %u Engineer", r->qty);
                break;
            default: sell(r->idx, r->qty);
            }
        }
    close_work();
}

/* ------------------------------------------------------------------ */
/* Transfers (TransfersState): everything on its way to this base, soonest
   first: deliveries (purchases, recruits, transfers from other bases),
   soldiers and craft in transit, with the hours left. */

static const UkMenu xfers_menu = {
    UI_BG_STORES, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    "Transfers", "ITEM\tQUANTITY\tARRIVAL TIME (hours)", TABS("\x8C\xC0"), "Esc=OK", 178, NULL
};

void transfers_screen(void)
{
    char tmp[41];
    int n = 0, i, k;

    if (!open_work())
        return;
    for (i = 0; i < MAX_TRANSFERS; i++) {
        const Transfer *t = &g_base.arriving[i];
        const char *name;
        if (!t->what)
            continue;
        name = t->what == TR_SCIENTIST ? "Scientists" : t->what == TR_ENGINEER ? "Engineers"
             : t->what == TR_SOLDIER ? "Soldiers (recruits)" : g_items[t->what - 1].name;
        sprintf(lines[n], "%s\t%u\t%d", name, t->n, t->hours);
        bw->row[n++].qty = t->hours;
    }
    for (i = 0; i < g_roster_n && n < MAX_LINES; i++)
        if (g_roster[i].transit) {
            sprintf(lines[n], "Soldier %.14s\t1\t%d", g_roster[i].name, g_roster[i].transit);
            bw->row[n++].qty = g_roster[i].transit;
        }
    for (i = 0; i < MAX_CRAFT && n < MAX_LINES; i++)
        if (CRAFT_HERE(&g_craft[i]) && g_craft[i].state == CS_ORDERED) {
            sprintf(lines[n], "%s\t1\t%d", craft_name(&g_craft[i]), g_craft[i].timer);
            bw->row[n++].qty = g_craft[i].timer;
        }
    /* The soonest first (a small selection sort). */
    for (i = 0; i < n; i++) {
        for (k = i + 1; k < n; k++)
            if (bw->row[k].qty < bw->row[i].qty) {
                unsigned h = bw->row[i].qty;
                bw->row[i].qty = bw->row[k].qty;
                bw->row[k].qty = h;
                strcpy(tmp, lines[i]);
                strcpy(lines[i], lines[k]);
                strcpy(lines[k], tmp);
            }
    }
    for (i = 0; i < n; i++) {
        char *p;
        strcpy(tmp, lines[i]);
        while ((p = strchr(tmp, 9)) != NULL)
            *p = ' ';
        log_printf("TRANSFERS %s", tmp);
    }
    ql_menu(&xfers_menu, NULL, lines, n, 0, NULL, NULL);
    close_work();
}

