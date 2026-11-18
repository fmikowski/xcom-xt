/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Heavy weapons platforms on the geoscape (an overlay section of its own,
   -zc): the Equip Craft screen for HWPs and the HWPs that go into a
   battle. As OpenXcom's CraftEquipmentState: an HWP aboard takes four
   seats and a vehicle slot and its full load of ammunition from the
   stores. After the battle: src/hwpdeb.c. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "craft.h"
#include "hwp.h"
#include "input.h"
#include "inv.h"
#include "qlist.h"
#include "rules.h"
#include "uik.h"
#include "unit.h"

static int aboard(const Craft *c)
{
    int n = 0;
    while (n < MAX_HWP && c->hwp[n])
        n++;
    return n;
}

/* The seats the soldiers hold (the wounded keep theirs: Craft::getNumSoldiers). */
static int seated(const Craft *c)
{
    int i, n = 0, k = (int)(c - g_craft) + 1;

    for (i = 0; i < g_roster_n; i++)
        n += g_roster[i].craft == k;
    return n;
}

/* The rounds one load takes from the stores (lasers and plasma: none). */
static int load_of(int item)
{
    return g_items[item].clip_item == NO_ITEM ? 0 : g_items[item].clip_size;
}

/* HWP k leaves craft c (the others move up). */
static void unload(Craft *c, int k)
{
    for (; k < MAX_HWP - 1; k++)
        c->hwp[k] = c->hwp[k + 1];
    c->hwp[MAX_HWP - 1] = 0;
}

static void board(int item, int craft)
{
    Hwp *h = &g_hwp[g_hwp_n++];

    h->item = (unsigned char)item;
    h->ammo = (unsigned char)g_items[item].clip_size;
    h->unit = -1;
    h->craft = (signed char)craft;
    if (h < g_hwp + MAX_HWP)
        g_hwp_hurt[h - g_hwp][0] = 0;
}

void hwp_board(int k)
{
    const Craft *c;
    int i;

    g_hwp_n = 0;
    if (k < 0)
        return;
    c = &g_craft[k];
    for (i = 0; i < MAX_HWP && c->hwp[i]; i++)
        board(c->hwp[i], k);
}

void hwp_board_base(void)
{
    int i, j, item, clip;

    g_hwp_n = 0;
    for (i = 0; i < MAX_CRAFT; i++) {
        const Craft *c = &g_craft[i];
        if (!CRAFT_HERE(c) || c->state != CS_HANGAR)   /* transports at home too */
            continue;
        for (j = 0; j < MAX_HWP && c->hwp[j] && g_hwp_n < MAX_BHWP; j++)
            board(c->hwp[j], i);
    }
    for (item = IT_TANK_CANNON; item <= IT_HOVER_LAUNCHER; item++) {
        clip = g_items[item].clip_item;
        while (g_hwp_n < MAX_BHWP && g_base.stores[item]
               && (clip == NO_ITEM || g_base.stores[clip] >= (unsigned)load_of(item))) {
            g_base.stores[item]--;
            if (clip != NO_ITEM)
                g_base.stores[clip] -= load_of(item);
            board(item, -1);
        }
    }
    if (g_hwp_n)
        log_printf("HWP %d defend the base", g_hwp_n);
}

/* ------------------------------------------------------------------ */
/* Equip Craft (CraftEquipmentState): the HWPs and their ammunition, then
   every item a soldier can take into battle, in the stores and aboard;
   Right / + / Enter loads one, Left / - unloads one. Opened by the
   geoscape for the hangar (BS_EQUIP), so that this section runs next to
   the geoscape alone. */

/* The battle items in the order of items.rul (OpenXcom lists them so):
   a string literal, as named tables of a -zc section read garbage. */
#define EQ_ORDER "\x00\x09\x01\x0A\x02\x0B\x5A\x5B\x38\x39\x5C\x5D\x3A\x3B\x5E\x5F" \
                 "\x03\x04\x05\x0F\x60\x61\x11\x1A\x16\x46\x17\x62\x08\x0E\x07\x0D" \
                 "\x06\x0C\x3C\x3D\x18\x19\x10\x47"
#define EQ_N    40
/* The HWPs and their ammunition come first, each with its own (the HWPs'
   place in items.rul). tools/itemorder.py --check tests both literals. */
#define HWP_ORDER "\x63\x68\x64\x69\x65\x66\x67\x6A"
#define ROWS    (N_HWP + 3 + EQ_N)
static char (*lines)[41];       /* on the heap while shown, with info and err */
static char *info, *err;
static unsigned char *row_of;   /* the item of each row shown */
static int nrows;
static Craft *cr;
static int seats;               /* the craft's soldiers' seats */

/* An item X-COM can use in battle (RuleItem requirements researched). */
static int usable(int item)
{
    int t;

    switch (item) {
    case IT_PPISTOL: case IT_CLIP_PPISTOL: t = RS_PPISTOL; break;
    case IT_PRIFLE: case IT_CLIP_PRIFLE: t = RS_PRIFLE; break;
    case IT_HPLASMA: case IT_CLIP_HPLASMA: t = RS_HPLASMA; break;
    case IT_AGRENADE: t = RS_AGRENADE; break;
    case IT_LAUNCHER: t = RS_LAUNCHER; break;
    case IT_STUNBOMB: t = RS_STUNBOMB; break;
    case IT_BLASTER: t = RS_BLASTER; break;
    case IT_BLASTERBOMB: t = RS_BLASTERBOMB; break;
    case IT_PSIAMP: t = RS_PSIAMP; break;
    case IT_MINDPROBE: t = RS_MINDPROBE; break;
    case IT_MEDIKIT: t = RS_MEDIKIT; break;
    case IT_SCANNER: t = RS_SCANNER; break;
    default: return 1;
    }
    return g_base.done[t];
}

static int row_item(int r)
{
    return row_of[r];
}

static void make_lines(void)
{
    int r, k, n, item;

    nrows = 0;
    for (r = 0; r < N_HWP + 3 + EQ_N; r++) {
        item = r < N_HWP + 3 ? (unsigned char)HWP_ORDER[r] : (unsigned char)EQ_ORDER[r - N_HWP - 3];
        if (r < N_HWP + 3) {
            n = 0;
            for (k = 0; k < MAX_HWP && cr->hwp[k]; k++)
                if (item < IT_HWP_SHELLS ? cr->hwp[k] == item : g_items[cr->hwp[k]].clip_item == item)
                    n += item < IT_HWP_SHELLS ? 1 : g_items[cr->hwp[k]].clip_size;
        } else {
            n = cr->items[item];
            if (!usable(item))
                continue;
        }
        /* Only what the base has: in the stores or aboard (not the
           hovertanks and rounds it does not own). */
        if (!n && !g_base.stores[item])
            continue;
        row_of[nrows] = (unsigned char)item;
        sprintf(lines[nrows++], g_items[item].kind == IK_CLIP ? "  %s\t%u\t%d" : "%s\t%u\t%d",
                g_items[item].name, g_base.stores[item], n);
    }
    k = seated(cr) + HWP_SEATS * aboard(cr);
    sprintf(info, "Space available %d   Space used %d   HWPs %d of %d", seats - k, k,
            aboard(cr), g_craft_types[cr->type].vehicles);
}

/* d items between the stores and the craft (d > 0: aboard), as many as
   there are or fit. */
static const char *move_item(int item, int d)
{
    unsigned n;

    if (cr->state != CS_HANGAR)
        return "The craft is not in its hangar.";
    if (d > 0) {
        n = g_base.stores[item];
        if (n > (unsigned)d)
            n = d;
        if (n > 255u - cr->items[item])
            n = 255u - cr->items[item];
        g_base.stores[item] -= n;
        cr->items[item] += n;
    } else {
        n = cr->items[item];
        if (n > (unsigned)-d)
            n = -d;
        g_base.stores[item] += n;
        cr->items[item] -= n;
    }
    if (n)
        log_printf("CRAFT %s %s %d aboard", craft_name(cr), g_items[item].name, cr->items[item]);
    return NULL;
}

static const char *load(int item)
{
    int n = aboard(cr), clip = g_items[item].clip_item;

    if (!g_base.stores[item])
        return NULL;
    if (cr->state != CS_HANGAR)
        return "The craft is not in its hangar.";
    if (n >= g_craft_types[cr->type].vehicles)
        return "No room for another HWP.";
    if (seated(cr) + HWP_SEATS * (n + 1) > seats)
        return "Not enough space: take soldiers off first.";
    if (clip != NO_ITEM && g_base.stores[clip] < (unsigned)load_of(item)) {
        sprintf(err, "Not enough %s to arm HWP (%d).", g_items[clip].name, load_of(item));
        return err;
    }
    g_base.stores[item]--;
    if (clip != NO_ITEM)
        g_base.stores[clip] -= load_of(item);
    cr->hwp[n] = (unsigned char)item;
    log_printf("CRAFT %s HWP %s aboard (%d of %d)", craft_name(cr), g_items[item].name, n + 1,
               g_craft_types[cr->type].vehicles);
    return NULL;
}

static void unload_one(int item)
{
    int k = aboard(cr), clip = g_items[item].clip_item;

    while (--k >= 0 && cr->hwp[k] != item)
        ;
    if (k < 0 || cr->state != CS_HANGAR)
        return;
    unload(cr, k);
    g_base.stores[item]++;
    if (clip != NO_ITEM)
        g_base.stores[clip] += load_of(item);
    log_printf("CRAFT %s HWP %s to the stores", craft_name(cr), g_items[item].name);
}

/* The arrows and keys (qlist.c): d items (HWPs) of row sel aboard (d > 0)
   or back in the stores. 1: only that row changed, 3: others too (an HWP's
   rounds), 2: rows came or went (the list is made again). */
static int equip_step(int sel, int d)
{
    const char *e = NULL;
    int item = row_item(sel), cnt = d < 0 ? -d : d, k, rows = nrows;

    if (item >= IT_HWP_SHELLS)
        return 0;               /* the HWPs' rounds only show what they hold */
    if (g_items[item].kind != IK_HWP)
        e = move_item(item, d);
    else
        for (k = 0; k < cnt && !e; k++) {
            int was = aboard(cr);
            if (d > 0)
                e = load(item);
            else
                unload_one(item);
            if (aboard(cr) == was)
                break;
        }
    if (e)
        uk_notice(e);
    make_lines();
    if (nrows != rows)
        return 2;               /* uk_menu again */
    return e || g_items[item].kind == IK_HWP ? 3 : 1;
}

static const UkMenu equip_menu = {
    UI_BG_EQUIP, T_BLUE, UKS_LIST, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    NULL, NULL, NULL, NULL, 178, NULL
};

void hwp_equip_screen(int k)
{
    char title[32];
    UkMenu m = equip_menu;
    int sel = 0;

    cr = &g_craft[k];
    seats = g_craft_types[cr->type].soldiers;
    lines = malloc(ROWS * sizeof *lines + 96 + ROWS);
    if (!lines)
        return;
    info = (char *)(lines + ROWS);
    err = info + 48;
    row_of = (unsigned char *)err + 48;
    sprintf(title, "Equipment for %s", craft_name(cr));
    m.title = title;
    /* Strings, not a constant array: a far pointer to a constant table of
       this section came out with a wrong segment (the overlay manager). */
    m.header = "Item\tStores\tCraft";
    m.tabs = (const unsigned char *)"\x78\xDE";      /* 120, 222 */
    m.buttons = "Esc=OK";
    make_lines();
    {
        char b[100];                    /* LOG: the items shown, in order (tests/ITEMORD.TXT) */
        int i, l = 0;
        for (i = 0; i < nrows && l < 92; i++)
            l += sprintf(b + l, " %02X", row_of[i]);
        b[l] = 0;
        log_printf("EQUIPLIST items:%s", b);
    }
    for (;;) {
        if (sel >= nrows)
            sel = nrows - 1;
        sel = ql_menu(&m, info, lines, nrows, sel, NULL, equip_step);
        if (sel < 0)
            break;
    }
    free(lines);
    lines = NULL;
}

/* ------------------------------------------------------------------ */
/* The squad's equipment around a battle (BattlescapeGenerator::deployXCOM,
   autoEquip, DebriefingState::reequipCraft) */

/* The battle items in the order of their items.rul names (STR_AC_AP_AMMO
   .. STR_STUN_ROD): the order of a craft's contents, so the order in
   which autoEquip offers them. */
#define EQ_ABC  "\x39\x5C\x5D\x10\x38\x3D\x3C\x62\x0F\x0B\x5A\x5B\x02\x05\x08\x0E" \
                "\x11\x5F\x5E\x03\x04\x16\x47\x1A\x00\x09\x06\x0C\x07\x0D\x61\x46" \
                "\x01\x0A\x3A\x18\x3B\x60\x19\x17"

static unsigned char *avail;            /* items not yet given (on the heap) */
static unsigned char squad[MAX_SQUAD];
static int nsquad;

/* Squad soldier i's entries: by his place in the squad, the index of his
   record in the battle's roster (base.c squad_out). */
static int key_of(int i)
{
    return CARRY_ROSTER | i;
}

/* Takes one of item for the battle. */
static void use(int item)
{
    avail[item]--;
    g_base.stores[item]--;
}

/* What soldier i carries (weapons with their loaded clip). */
static int weight_of(int i)
{
    int k, w = 0, key = key_of(i);

    for (k = 0; k < g_max_carry; k++)
        if (g_carry[k].unit == key) {
            w += g_weights[g_carry[k].item];
            if (SLOT_SEC(g_carry[k].slot) == SEC_RHAND && g_carry[k].n
                && g_items[g_carry[k].item].clip_item != NO_ITEM)
                w += g_weights[g_items[g_carry[k].item].clip_item];
        }
    return w;
}

/* The rounds a weapon gets: a clip of its own type (clip; NO_ITEM: the
   standard one) taken if there is one, else none; lasers need none. */
static int load_rounds(int weapon, int clip)
{
    const ItemDef *d = &g_items[weapon];

    if (!d->clip_size || d->kind != IK_WEAPON)
        return 0;
    if (d->clip_item == NO_ITEM)
        return d->clip_size;
    if (clip == NO_ITEM)
        clip = d->clip_item;
    if (!avail[clip])
        return 0;
    use(clip);
    return d->clip_size;
}

/* Room for n more entries in the battle's carry pool (inv.h g_max_carry):
   what does not fit stays in the craft (or the stores). */
static int room(int n)
{
    int k;

    for (k = 0; k < g_max_carry && n; k++)
        n -= g_carry[k].unit == CARRY_FREE;
    return !n;
}

/* The layout's items of soldier i (clips: second pass), as
   BattlescapeGenerator::placeItemByLayout: a gun with a special clip in its
   EquipmentLayoutItem, in whatever slot, takes one (its type in the gun's
   n) or stays at the craft. */
static void by_layout(int i, int clips)
{
    const Soldier *s = &g_roster[squad[i]];
    int e, key = key_of(i);

    for (e = 0; e < LAYOUT_N && s->layout[e][0] != NO_ITEM; e++) {
        int item = s->layout[e][0], slot = s->layout[e][1], n = 0, clip = NO_ITEM;
        if (SLOT_SEC(slot) == SEC_LOAD || (g_items[item].kind == IK_CLIP) != clips
            || !avail[item] || !carry_fits(key, slot, item, -1))
            continue;
        if (g_items[item].kind == IK_WEAPON && e + 1 < LAYOUT_N
            && s->layout[e + 1][0] != NO_ITEM && SLOT_SEC(s->layout[e + 1][1]) == SEC_LOAD) {
            clip = s->layout[e + 1][0];
            if (!avail[clip])
                continue;
        }
        if (!room(1))
            continue;
        use(item);
        if (g_items[item].kind == IK_WEAPON)
            n = inv_wn(item, clip == NO_ITEM ? 0 : clip, load_rounds(item, clip));
        else if (item == IT_MEDIKIT)
            n = 10;
        carry_add(key, slot, item, n);
    }
}

static int carries_type(int key, int item)
{
    int k, n = 0;

    for (k = 0; k < g_max_carry; k++)
        n += g_carry[k].unit == key && g_carry[k].item == item;
    return n;
}

/* autoEquip's _addItem for soldier i: 1 if it took the item. */
static int auto_add(int i, int item, int pass)
{
    const Soldier *s = &g_roster[squad[i]];
    const ItemDef *d = &g_items[item];
    int key = key_of(i), w = weight_of(i) + g_weights[item], rh = carry_at(key, SEC_RHAND), k;

    if (!room(1))
        return 0;                   /* (by_layout) */
    if (d->kind == IK_WEAPON) {
        int clip = d->clip_item;
        /* Only a loaded gun, into an empty right hand, light enough. */
        if (rh >= 0 || (clip != NO_ITEM && !avail[clip]))
            return 0;
        if (clip != NO_ITEM)
            w += g_weights[clip];
        if (s->strength * 66 < w * 100)
            return 0;
        use(item);
        carry_add(key, SLOT(SEC_RHAND, 0, 0), item, load_rounds(item, NO_ITEM));
        return 1;
    }
    if (carries_type(key, item) >= (d->kind == IK_CLIP && pass == 3 ? 2 : 1))
        return 0;                   /* "we already have one, thanks" */
    if (d->kind == IK_CLIP) {
        /* Ammunition only for the gun in hand. */
        int g = rh >= 0 ? g_carry[rh].item : NO_ITEM;
        if (g == NO_ITEM || (g_items[g].clip_item != item && !ammo_find(g, item)))
            return 0;
    }
    if (s->strength < w || (k = carry_free_slot(key, item)) < 0)
        return 0;
    use(item);
    carry_add(key, k, item, item == IT_MEDIKIT ? 10 : 0);
    return 1;
}

static int floor_used(void)
{
    int j, n = 0;

    for (j = 0; j < g_max_floor; j++)
        n += g_floor[j].item != NO_ITEM;
    return n;
}

/* A rifle (two-handed gun or stun rod), a pistol. */
static int rifle(int item)
{
    return g_items[item].kind == IK_WEAPON && item_two_handed(item);
}

void equip_deploy(int k, int night)
{
    int i, j, pass, e, n;

    carry_clear();
    floor_clear();
    avail = malloc(N_ITEMS);
    if (!avail)
        return;
    memset(avail, 0, N_ITEMS);
    /* The craft's items into the stores, from where the battle takes them
       (what stays behind is there afterwards; equip_reequip takes the
       craft's list back out). */
    for (j = 0; j < MAX_CRAFT; j++) {
        Craft *c = &g_craft[j];
        if (k >= 0 ? j != k : !CRAFT_HERE(c) || c->state != CS_HANGAR)
            continue;
        for (i = 0; i < N_ITEMS; i++) {
            g_base.stores[i] += c->items[i];
            avail[i] = c->items[i];
        }
    }
    if (k < 0)
        for (e = 0; e < EQ_N; e++) {
            i = (unsigned char)EQ_ORDER[e];
            avail[i] = g_base.stores[i] > 255 ? 255 : (unsigned char)g_base.stores[i];
        }
    {
        unsigned long b = 0;        /* (equip_reequip: tests) */
        for (i = 0; i < N_ITEMS; i++)
            if (g_items[i].buy_k)
                b += g_base.stores[i];
        log_printf("EQUIP stores: %lu bought items", b);
    }
    /* The soldiers units_spawn will place, in its order. */
    nsquad = base_squad_pick(squad);
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < nsquad; i++)
            by_layout(i, pass);
    /* autoEquip for those without a layout: rifles, pistols, ammunition,
       then the rest (a second clip; flares at night). */
    for (pass = 0; pass < 4; pass++)
        for (e = 0; e < EQ_N; e++) {
            int item = (unsigned char)EQ_ABC[e], kind = g_items[item].kind;
            if (pass == 0 ? !rifle(item) : pass == 1 ? kind != IK_WEAPON || rifle(item)
                : pass == 2 ? kind != IK_CLIP : kind == IK_WEAPON || (item == IT_FLARE && !night))
                continue;
            while (avail[item]) {
                for (i = 0; i < nsquad; i++)
                    if (g_roster[squad[i]].layout[0][0] == NO_ITEM && auto_add(i, item, pass))
                        break;
                if (i == nsquad)
                    break;
            }
        }
    /* The rest lies at the craft (guns loaded, as loadWeapons does), as
       far as the floor list keeps room for the battle's own. */
    for (e = 0; e < EQ_N; e++) {
        int item = (unsigned char)EQ_ORDER[e];
        while (avail[item] && floor_used() < g_max_floor - g_max_floor / 4) {    /* 48 of 64 */
            use(item);
            floor_add(0xFF, 0xFF, 0xFF, item, item == IT_MEDIKIT ? 10
                      : g_items[item].kind == IK_WEAPON ? load_rounds(item, NO_ITEM) : 1);
        }
    }
    n = floor_used();
    for (i = 0; i < nsquad; i++) {
        char line[80];
        int key = key_of(i);
        sprintf(line, "EQUIP %s:", g_roster[squad[i]].name);
        for (j = 0; j < g_max_carry && strlen(line) < 60; j++)
            if (g_carry[j].unit == key)
                sprintf(line + strlen(line), " %s", g_items[g_carry[j].item].name);
        log_printf("%s", line);
    }
    log_printf("EQUIP at the craft: %d items", n);
    if (!room(1))
        log_printf("EQUIP carry pool full: the rest stays in the craft");
    free(avail);
    avail = NULL;
}
