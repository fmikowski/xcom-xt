/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battlescape: the player's turn and commands (overlay section of their
   own, see battle_in.h). */

#include <bios.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "battle.h"
#include "battle_in.h"
#include "batui.h"
#include "bsave.h"
#include "canvas.h"
#include "effects.h"
#include "geo.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "pak.h"
#include "panel.h"
#include "path.h"
#include "psi.h"
#include "render.h"
#include "rules.h"
#include "sound.h"
#include "timer.h"
#include "unit.h"
#include "uik.h"

/* A key or click; a message on the panel goes with it. */
static unsigned read_key(void)
{
    unsigned key = input_key();
    panel_unmessage();
    return key;
}

/* read_key on the battlescape: meanwhile the hovertanks' propulsion runs
   (DOS animates it all the time): the animation frame steps every 2^17
   PIT counts (110 ms) and their quarters 1-3 are drawn again. The view
   scrolls at the screen's edges (Game Options' Scroll Type, batui.c's
   bu_scroll): with the left button down there, or with the pointer there
   for Auto-Scroll; such a click does nothing else. (The test here keeps
   batui out of memory until the pointer is at an edge.) */
static int at_edge(void)
{
    return (unsigned)(g_mouse_x - 2) >= 316 || (unsigned)(g_mouse_y - 2) >= 196;
}

static void hover(void);

static unsigned map_key(void)
{
    const int far *r;
    const Unit *u;
    unsigned char a;
    int sx, sy;
    unsigned key;

again:
    while (!input_ready()) {
        if (g_mouse) {
            if (g_bopt[BO_STYPE] == 2 && at_edge())
                bu_scroll(0);
            else
                hover();
        }
        a = (unsigned char)((unsigned)(timer_read() >> 16) >> 1 & 7);
        if (a != g_anim)
            for (g_anim = a, u = g_units; u < g_units + g_nunits; u++)
                if (u->xtype == XT_HOVER) {     /* (a wreck's tiles too: harmless) */
                    sx = TILE_SX(u->x, u->y);
                    sy = TILE_SY(u->x, u->y, u->z);
                    render_rect(sx - 16, sy - 8, sx + 48, sy + 56);
                }
        if ((sx = g_vga) != 0 && FP_SEG(r = ((const int far *(__cdecl far *)(void))MK_FP(sx, VGA_PICS))()))
            render_rect(r[0], r[1], r[2], r[3]);    /* DOS's smoke and fire pictures move on */
        if (g_cv)
            cv_idle();          /* the canvas draws ahead (canvas.h) */
    }
    g_track = 1;                /* a script's MOVE x y: KEY_MOVE (hover) */
    key = read_key();
    g_track = 0;
    if (key == KEY_LCLICK && at_edge() && bu_scroll(1))
        goto again;
    return key;
}

/* map_key for the waypoint screen (bact.c): the cursor follows the pointer. */
unsigned bp_map_key(void)
{
    unsigned key = map_key();

    if (key == KEY_MOVE) {
        hover();
        return 0;
    }
    return key;
}

static unsigned target;         /* hand menu action waiting for its target */

/* The cursor of the action (OpenXcom BattlescapeGame::setupCursor,
   Map::drawTerrain): the box, the aim crosshairs for a shot, the box with
   the throw, psi (also the mind probe) or waypoint mark; yellow on a unit
   in sight, else red (DOS blinks between the two). */
static void cursor_kind(void)
{
    unsigned b = g_pak.cursor_base, mark = 0;
    unsigned char occ = UNIT_AT(g_cur_x, g_cur_y, g_cur_z);

    if (occ && unit_visible(&g_units[occ - 1]))
        b++;                    /* CF_*_UNIT */
    g_cur_spr[0] = b + CF_BACK;
    g_cur_spr[1] = b + CF_FRONT;
    if (target == 'g')
        mark = CF_THROW;
    else if (target == 'w')
        mark = CF_WAYPOINT;
    else if (target == 'o' || target == BK_PANIC || target == BK_CONTROL)
        mark = CF_PSI;
    else if (target)
        g_cur_spr[0] = g_cur_spr[1] = b + CF_AIM;
    g_cur_spr[2] = mark ? g_pak.cursor_base + mark : 0;
}

static void set_target(unsigned k)
{
    target = k;
    cursor_kind();
    render_cursor_move(g_cur_x, g_cur_y, g_cur_z);
}

static void hud_cursor(void)
{
    char line[48];
    unsigned s = g_cur_spr[1];
    unsigned c = bt_sel >= 0 ? path_cost(g_cur_x, g_cur_y, g_cur_z) : 0xFFFF;
    int items = g_seen && !(g_seen[TIDX(g_cur_x, g_cur_y, g_cur_z)] & SEEN_BIT) ? 0
              : floor_count_at(g_cur_x, g_cur_y, g_cur_z);

    cursor_kind();              /* a unit came or went: the box's colour */
    if (g_cur_spr[1] != s)
        render_cursor_move(g_cur_x, g_cur_y, g_cur_z);
    line[0] = 0;
    if (c != 0xFFFF && c)
        sprintf(line, "%u TU", c);
    if (items)
        sprintf(line + strlen(line), "%sItems %d", line[0] ? "  " : "", items);
    panel_cursor(line);
}

/* The map and the panel again after a full-screen page (inventory, mind
   probe); the panel first, so no old marker comes back with the map. */
static void hud_help(void)
{
    panel_close();
    panel_open();
    render_full();
    hud_status();
}

/* The cursor to a tile; follow: the view goes along (to another level:
   centred on it), else it stays (the mouse's tiles are on the screen). */
static void cursor_to(int x, int y, int z, int follow)
{
    int ox = g_cur_x, oy = g_cur_y, oz = g_cur_z;

    g_cur_x = x;
    g_cur_y = y;
    g_cur_z = z;
    cursor_kind();
    if (z == oz) {
        render_cursor_move(ox, oy, oz);
        if (follow)
            render_follow(x, y, z);
    } else {
        if (follow) {
            g_view_z = z;
            render_center(x, y, z);
        }
        render_full();
    }
    hud_cursor();
}

void bp_set_cursor(int x, int y, int z)
{
    cursor_to(x, y, z, 1);
}

/* DOS always marks the tile under the mouse with the cursor: it follows
   the pointer over the map (each time the pointer has moved: the keys
   still move it too) at the view's level. */
static void hover(void)
{
    static int hx = -1, hy;
    int x, y;

    if (g_mouse_x == hx && g_mouse_y == hy)
        return;
    hx = g_mouse_x;
    hy = g_mouse_y;
    if (hy < HUD_ROW * 8 && bp_pick_tile(hx, hy, g_view_z, &x, &y)
        && (x != g_cur_x || y != g_cur_y || g_view_z != g_cur_z)) {
        cursor_to(x, y, g_view_z, 0);
        if (target == 'w')
            bu_wp_marks();      /* (the cursor's box may have covered a number) */
    }
}

/* The selected unit's arrow (OpenXcom Map::drawTerrain): above unit u (-1:
   none) while the turn waits; not while it walks (the cursor type is
   CT_NONE then). At the tile's standing height, 4 lower on a 2x2 unit,
   24 - (height + float height) below the sprite box's top, 2 higher
   kneeling, centred (render.h g_arrow); DOS's bobbing is left out
   (docs/compromises.md). Its old and new places are shown again. */
static void arrow_rect(const int *a)
{
    int x = a[1] + g_cam_x, y = a[2] + g_cam_y;

    if (a[0])
        render_rect(x, y, x + 9, y + 9);
}

static void arrow_to(int u)
{
    int a[4], o[4], h;
    const Unit *un = &g_units[u];

    memset(a, 0, sizeof a);
    if (u >= 0 && un->alive) {
        const UnitXStat *xs = unit_xstat(un);
        if (!tile_standable(un->x, un->y, un->z, &h))
            h = un->z * 24;             /* standing height: level and terrain */
        a[0] = 1;
        a[1] = ((un->x - un->y) << 4) + 16 - 4;
        a[2] = ((un->x + un->y) << 3) - h + (un->big ? 4 : 0) + 24 - 9 - xs->floating
               - (un->kneeling ? xs->kneel + 2 : xs->height);
        a[3] = un->z;
    }
    if (!memcmp(a, g_arrow, sizeof a))
        return;
    log_printf("ARROW u%d %d,%d,%d", a[0] ? u : -1, a[1], a[2], a[3]);
    memcpy(o, g_arrow, sizeof o);
    memcpy(g_arrow, a, sizeof a);
    arrow_rect(o);
    arrow_rect(a);
}

static void refresh_paths(void)
{
    if (bt_sel >= 0 && g_units[bt_sel].alive)
        path_search(bt_sel, g_units[bt_sel].tu);
}

static void select_unit(int u)
{
    const Unit *un = &g_units[u];

    bt_sel = u;
    target = 0;                 /* (cancelCurrentAction) */
    cursor_kind();
    bt_hit_reset();
    refresh_paths();
    bp_set_cursor(un->x, un->y, un->z);
    hud_status();
}

/* Kneel 4 TU, stand up 8 (soldiers only, not while flying over a tile
   without a floor: BattlescapeGame::kneel, isFloating); enemies may react. */
static void toggle_kneel(int u)
{
    Unit *un = &g_units[u];
    int cost = un->kneeling ? 8 : 4, h;

    if (un->psi_state & PSI_CTRL) {
        msg(COL_BAD, "Aliens cannot kneel");
        return;
    }
    if (UNIT_HWP(un) || !tile_standable(un->x, un->y, un->z, &h))
        return;                 /* as the panel's button in DOS: nothing */
    if (un->tu < cost) {
        msg(COL_BAD, "Not enough time units");
        return;
    }
    if (!reserve_ok(un, cost))
        return;
    un->tu -= cost;
    un->kneeling = !un->kneeling;
    redraw_unit(u);
    reaction_fire(u);
}

/* The medi-kit, motion scanner, Psi-Amp and mind probe work from a hand,
   as DOS. Their keys take one carried elsewhere into a free hand first
   (the left before the right) for the inventory's TU (inventories.rul),
   given back when the action then does nothing (hand_back). */
static int pull_k = -1, pull_slot, pull_tu, pull_cost;

static int in_hand(int s, int item, const char *none)
{
    Unit *u = &g_units[s];
    int n, hand;

    pull_k = -1;
    if (inv_hand(u, 0, &n) == item || inv_hand(u, 1, &n) == item)
        return 1;
    if (carry_find(s, item) < 0) {
        msg(COL_BAD, none);
        return 0;
    }
    if ((pull_k = inv_reach(s, item, &hand, &pull_cost)) < 0) {
        msg(COL_BAD, "Both hands are full");
        return 0;
    }
    if (u->tu < pull_cost) {
        pull_k = -1;
        msg(COL_BAD, "Not enough time units");
        return 0;
    }
    pull_slot = g_carry[pull_k].slot;
    g_carry[pull_k].slot = (unsigned char)SLOT(hand, 0, 0);
    u->tu -= (unsigned char)pull_cost;
    pull_tu = u->tu;
    inv_medikit(s);
    return 1;
}

static void hand_back(int s)
{
    Unit *u = &g_units[s];

    if (pull_k >= 0 && u->tu == pull_tu && g_carry[pull_k].unit == s) {
        g_carry[pull_k].slot = (unsigned char)pull_slot;
        u->tu += (unsigned char)pull_cost;
        inv_medikit(s);
    }
    pull_k = -1;
}

/* The active hand acts (BattleUnit::getActiveHand, chosen on the panel):
   shots and reloads with the left hand's gun when it is the active hand
   or the right holds none, throws and priming with the right hand's
   grenade when it is the active hand or the left holds none; the hands
   are mirrored for it (inv_mirror; 1 if so, hand_unmirror after). */
static int hand_mirror(int s, int grenade)
{
    const Unit *u = &g_units[s];
    int n, l = inv_hand(u, 1, &n), r = inv_hand(u, 0, &n), left = (u->special & SP_LEFT) != 0;
    int kind = grenade ? IK_GRENADE : IK_WEAPON, lk = l == NO_ITEM ? -1 : ITEM_KIND(l);
    int rk = r == NO_ITEM ? -1 : ITEM_KIND(r);

    if (!UNIT_SOLDIER(u) || (grenade ? rk != kind || (left && lk == kind) : lk != kind
                                                  || (!left && rk == kind)))
        return 0;
    return inv_mirror(s);
}

static void hand_unmirror(int s, int m)
{
    if (m && g_units[s].alive)
        inv_mirror(s);
}

/* Psi-Amp in a hand against the alien at the cursor: P attacks its
   morale (it may panic in its turn), C takes control of it for the rest
   of this turn; mode: 0 panic, 1 control (the hand menu's Panic Unit and
   Mind Control), -1 asks. */
static void psi_amp(int s, int mode)
{
    Unit *u = &g_units[s];
    unsigned char occ = UNIT_AT(g_cur_x, g_cur_y, g_cur_z);
    Unit *v = occ ? &g_units[occ - 1] : NULL;
    char text[48];
    int key, control, chance;

    if (!u->psi_skill) {
        msg(COL_BAD, "No psi skill: train in a Psi Lab");
        return;
    }
    if (!v || !v->alive || v->faction != FACTION_ALIEN || !v->seen) {
        msg(COL_BAD, "Put the cursor on an alien in sight");
        return;
    }
    if (u->tu < PSI_AMP_TU) {
        msg(COL_BAD, "Not enough time units");
        return;
    }
    if (mode < 0) {
        sprintf(text, "Psi: P panic %d%%  C control %d%%  Esc", psi_chance(u, v, 0),
                psi_chance(u, v, 1));
        msg(COL_INFO, text);
        key = (read_key() & 0xFF) | 0x20;
        if (key != 'p' && key != 'c') {
            msg(COL_INFO, "");
            return;
        }
        mode = key == 'c';
    }
    control = mode;
    chance = psi_chance(u, v, control);
    u->tu -= PSI_AMP_TU;
    u->dir = (unsigned char)dir_towards(u->x, u->y, v->x, v->y);
    redraw_unit(s);
    snd_hit();
    u->psi_exp++;               /* psi skill experience: 1 a try, 2 more on success */
    if (!psi_roll(u, v, control)) {
        log_printf("T%d: psi %s by %s (%d/%d) on %s (%d%%) failed", bt_turn,
                   control ? "control" : "panic", u->name, u->psi, u->psi_skill, v->name, chance);
        msg(COL_INFO, "The psionic attack failed");
        return;
    }
    log_printf("T%d: psi %s by %s (%d/%d) on %s (%d%%) succeeded", bt_turn,
               control ? "control" : "panic", u->name, u->psi, u->psi_skill, v->name, chance);
    u->psi_exp += 2;
    if (!control) {
        /* A morale attack (PsiAttackBState): morale - (110 - bravery); the
           alien may then panic at the start of its turn. */
        morale_change(v, -(110 - v->bravery));
        sprintf(text, "%s: morale attack, morale %d", unit_title(v), v->morale);
        msg(COL_GOOD, text);
        return;
    }
    v->faction = FACTION_XCOM;
    v->psi_state |= PSI_CTRL;
    v->tu = v->tu_max;
    v->panicked = 0;
    sprintf(text, "%s is under X-COM control!", unit_title(v));
    msg(COL_GOOD, text);
    update_vision(occ - 1, 1);
    redraw_unit(occ - 1);
    flush_fog();
    check_result();
}

/* End of the X-COM turn (or of the battle): controlled aliens go back to
   their side; those still held when the battle is won are captured. */
void psi_release_all(void)
{
    int i;

    for (i = 0; i < g_nunits; i++)
        if (g_units[i].psi_state & PSI_CTRL) {
            psi_release(i);
            log_printf("T%d: %s is free of X-COM control", bt_turn, g_units[i].name);
            if (bt_result == 1) {
                unit_kill(i);
                g_units[i].stunned = 1;
            }
        }
}

/* Map tile under screen pixel (px, py) on level z: tile (x, y) has its
   floor diamond centred 16 pixels right and 32 down from its sprite box. */
int bp_pick_tile(int px, int py, int z, int *tx, int *ty)
{
    int rx = px - g_cam_x - 16, ry = py - g_cam_y - 32 + z * 24;
    int a = rx + 2 * ry + 16, b = 2 * ry - rx + 16;

    /* Floor division by 32 (the values can be negative): Watcom shifts
       a signed int arithmetically (sar), the floor for both signs. */
    *tx = a >> 5;
    *ty = b >> 5;
    return IN_MAP(*tx, *ty, z);
}

/* The front-most X-COM soldier whose body is drawn under (px, py). */
/* Mouse in the battlescape, before the key switch of player_turn: a left
   click on a tile walks there (BattlescapeGame::primaryAction; the cursor
   follows the mouse, so it is on that tile); on a soldier it selects them;
   a right click turns towards the tile (or opens the door in front); the
   panel buttons send their keys. Returns the key to act on (0 = none). */

/* The panel's unit up / down buttons (BattlescapeState::btnUnitUpClick,
   Pathfinding::validateUpDown, BattlescapeGame::moveUpDown): a flyer goes
   up into a tile without a floor or down out of its own, anyone up or
   down a grav lift (8 TU); otherwise nothing happens. The move is the
   Enter key's walk to the tile above or below (the view follows). */
static unsigned unit_updown(int dir)
{
    const Unit *un = &g_units[bt_sel];
    int nz;

    if (bt_sel < 0)
        return 0;
    if (path_cost(un->x, un->y, un->z))
        refresh_paths();        /* the search is the selected unit's */
    if (step_cost(un->x, un->y, un->z, dir, &nz) < 0)
        return 0;
    bp_set_cursor(un->x, un->y, nz);
    return '\r';
}

static unsigned battle_mouse(unsigned key)
{
    int x, y, u;

    if (key == KEY_MOVE) {
        hover();
        return 0;
    }
    if (!IS_CLICK(key)) {
        /* While an action waits for its target the cursor keys (and V)
           still move the cursor: Enter aims at it, Esc cancels. */
        if (target && ((key & 0xFF) == '\r' || (key & 0xFF) == 27)) {
            unsigned k = (key & 0xFF) == 27 ? 0 : target;
            set_target(0);
            msg(COL_INFO, "");
            return k;
        }
        if (target && (key & 0xFF) && !strchr("vV+-=", key & 0xFF))
            set_target(0);
        return key;
    }
    if (key == KEY_LCLICK) {
        int alien;
        unsigned k = panel_click(&alien);
        if (alien >= 0) {
            /* BattlescapeState::btnVisibleUnitClick: the view centred on
               it, also when it is on the screen (a level change for
               bp_set_cursor centres). */
            const Unit *a = &g_units[alien];
            g_cur_z = -1;
            bp_set_cursor(a->x, a->y, a->z);
            log_printf("T%d: view on %s at %d,%d,%d: %d,%d", bt_turn, a->name, a->x, a->y,
                       a->z, g_cam_x, g_cam_y);
            return 0;
        }
        if (k == PK_LHAND || k == PK_RHAND) {
            if (bt_sel < 0)
                return 0;
            /* the clicked hand becomes the active one (setActiveHand) */
            g_units[bt_sel].special = (unsigned char)((g_units[bt_sel].special & ~SP_LEFT)
                                                      | (k == PK_LHAND ? SP_LEFT : 0));
            k = panel_actions(bt_sel, k == PK_RHAND);
            /* Actions aimed at a tile wait for a click on it (DOS
               targeting; the right button cancels), with their cursor
               (the map under the menu is shown again: only its box). */
            set_target(k && strchr("fauog[]", k) ? k : 0);
            hud_status();
            if (target) {
                msg(COL_INFO, "Select a target");
                return 0;
            }
            return k;
        }
        if (k >= PK_RESERVE && k <= PK_RESERVE + 3) {
            /* The reserve buttons set a mode; X cycles through them. */
            bt_reserve = k - PK_RESERVE;
            panel_reserve(bt_reserve);
            return 0;
        }
        if (k == PK_UNIT_UP || k == PK_UNIT_DOWN)
            return unit_updown(k == PK_UNIT_UP ? DIR_UP : DIR_DOWN);
        if (k == PK_MAP)
            return 'q';
        if (k == PK_LAYERS)
            return 'l';
        if (k == PK_OPTIONS)
            return KEY_F1 << 8;
        if (k == PK_ABORT)
            return KEY_F2 << 8;
        if (k)
            return k == 27 ? 0x011B : k;
    }
    if (g_mouse_y >= HUD_ROW * 8)
        return 0;
    if (!bp_pick_tile(g_mouse_x, g_mouse_y, g_view_z, &x, &y))
        return 0;
    if (key == KEY_LCLICK && !target) {
        /* A soldier on the box's tile, not under the pointer's tip
           (BattlescapeGame::primaryAction, SavedBattleGame::selectUnit at
           the selector's position). */
        u = UNIT_AT(x, y, g_view_z) - 1;
        if (u >= 0 && g_units[u].faction == FACTION_XCOM) {
            if (u != bt_sel)    /* the selected one: nothing, as DOS */
                select_unit(u);
            return 0;
        }
    }
    if (target) {
        unsigned k = key == KEY_LCLICK ? target : 0;
        target = 0;
        msg(COL_INFO, "");
        cursor_to(x, y, g_view_z, 0);
        return k;
    }
    /* The click's own tile (the cursor may not have followed yet). */
    if (x != g_cur_x || y != g_cur_y || g_view_z != g_cur_z)
        cursor_to(x, y, g_view_z, 0);
    return key == KEY_RCLICK ? ' ' : '\r';
}

static int again;               /* player_turn: back from the options window */
int bt_vlast = -1;              /* V: the alien it showed last (battle_in.h) */

static void turn(void)
{
    char text[40];

    if (again) {
        again = 0;
        hud_help();
    } else {
        if (bt_turn == 1)
            strcpy(text, g_pak.title);
        else
            sprintf(text, "Turn %d - X-COM", bt_turn);
        msg(COL_TITLE, text);
    }
    if (bt_sel < 0 || !g_units[bt_sel].alive)
        bt_sel = next_soldier(-1);
    if (bt_sel >= 0)
        select_unit(bt_sel);
    hud_status();

    while (!bt_result) {
        unsigned key;
        int scan, ch, nx, ny;

        arrow_to(bt_sel);       /* selected, moved, knelt, back from a walk */
        key = battle_mouse(map_key());
        scan = key >> 8;
        ch = key & 0xFF;
        nx = g_cur_x;
        ny = g_cur_y;

        switch (scan) {
        case KEY_UP:    nx--; ny--; break;
        case KEY_DOWN:  nx++; ny++; break;
        case KEY_LEFT:  nx--; ny++; break;
        case KEY_RIGHT: nx++; ny--; break;
        case KEY_HOME:  nx--; break;
        case KEY_PGUP:  ny--; break;
        case KEY_END:   ny++; break;
        case KEY_PGDN:  nx++; break;
        }
        if ((nx != g_cur_x || ny != g_cur_y) && IN_MAP(nx, ny, g_cur_z)) {
            bp_set_cursor(nx, ny, g_cur_z);
            continue;
        }
        if (scan == KEY_ESC || scan == KEY_F1) {
            /* Esc (keyBattleOptions), F1 or the panel's button: the
               options window (PauseState: save, load, abandon, sound) is
               bsave.c's: battle_run opens it and calls this turn again. */
            bt_request = BR_OPTIONS;
            again = 1;
            return;
        }
        if (scan == KEY_F2) {
            /* The panel's abort button (DOS's key A is our aimed shot):
               the DOS abort window (over the map, TAC00.SCR inside). */
            int yes = bu_abort();
            hud_help();
            if (yes) {
                bt_result = -2;
                return;
            }
            continue;
        }
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        switch (ch) {
        case '+':
        case '=':
            if (g_cur_z < (int)g_mz - 1)
                bp_set_cursor(g_cur_x, g_cur_y, g_cur_z + 1);
            break;
        case '-':
            if (g_cur_z > 0)
                bp_set_cursor(g_cur_x, g_cur_y, g_cur_z - 1);
            break;
        case '\t':
            if (next_soldier(bt_sel) >= 0)
                select_unit(next_soldier(bt_sel));
            break;
        case '\r':
            if (bt_sel >= 0) {
                unsigned long t0 = timer_read();
                int w = bt_sel, bx, by, bz, tz = g_cur_z;
                msg(COL_INFO, "");
                /* A tile with no floor to stand on means the floor below
                   it, but for flyers (Pathfinding::calculate, canFallDown):
                   from a craft's cabin a click beside the craft on the
                   cabin's level walks down the ramp to the ground. */
                if (!unit_flies(&g_units[w]))
                    while (tz && !tile_standable(g_cur_x, g_cur_y, tz, &bx))
                        tz--;
                /* Beyond the TU (the shown costs): the path all the way,
                   walked until the TU run out. */
                if (path_cost(g_cur_x, g_cur_y, tz) == 0xFFFF)
                    path_search_to(w, g_cur_x, g_cur_y, tz, 255, &bx, &by, &bz);
                arrow_to(-1);
                walk(w, g_cur_x, g_cur_y, tz);
                /* Down (or up) a level or more: the view goes with the unit
                   (UnitWalkBState::think, setViewLevel; here at the walk's
                   end, docs/compromises.md). */
                if (g_units[w].z != g_view_z) {
                    g_cur_z = g_view_z = g_units[w].z;
                    render_full();
                }
                log_printf("T%d: %s walked to %d,%d,%d (target %d,%d,%d) TU %d, %lu ms",
                           bt_turn, g_units[w].name, g_units[w].x, g_units[w].y,
                           g_units[w].z, g_cur_x, g_cur_y, tz, g_units[w].tu,
                           timer_ms(timer_read() - t0));
                refresh_paths();
                hud_status();
                hud_cursor();
            }
            break;
        case 'w':
            /* Launch missile: waypoints, then the launch (bact.c), with
               the waypoint cursor. */
            if (bt_sel >= 0) {
                int s = bt_sel, m = hand_mirror(s, 0);
                Unit *un = &g_units[s];
                if (BT_WAYPOINTS(un)) {
                    set_target('w');
                    bt_nwp = bu_waypoints(un);
                    target = 0;
                    cursor_kind();
                    render_full();
                    if (bt_nwp >= 0)
                        msg(COL_INFO, "");
                    else
                        bt_nwp = 0;
                    if (bt_nwp) {
                        /* (a first one on its own tile: it faces the second) */
                        int k = bt_nwp > 1 && bt_wp[0][0] == un->x && bt_wp[0][1] == un->y
                                && bt_wp[0][2] == un->z;
                        shoot(s, bt_wp[k][0], bt_wp[k][1], bt_wp[k][2] & 15, SHOT_AIMED);
                    }
                    bt_nwp = 0;
                    refresh_paths();
                    hud_status();
                }
                hand_unmirror(s, m);
            }
            break;
        case 'f':
        case 'a':
        case 'u':
            if (bt_sel >= 0) {
                int s = bt_sel, m = hand_mirror(s, 0), launch = 0;
                /* A missile launcher's aimed shot: one waypoint, the
                   cursor (DOS has only the launch). */
                if (ch == 'a' && BT_WAYPOINTS(&g_units[s])) {
                    bt_wp[0][0] = (unsigned char)g_cur_x;
                    bt_wp[0][1] = (unsigned char)g_cur_y;
                    bt_wp[0][2] = (unsigned char)g_cur_z;
                    bt_nwp = launch = 1;
                }
                shoot(s, g_cur_x, g_cur_y, g_cur_z,
                      ch == 'a' ? SHOT_AIMED : ch == 'u' ? SHOT_AUTO : SHOT_SNAP);
                hand_unmirror(s, m);
                /* After a shot the cursor stays in targeting mode, the same
                   shot again on the next click (BattlescapeGame::popState;
                   not after a missile's launch); the right button ends it. */
                if (!launch && bt_sel == s && g_units[s].alive)
                    set_target(ch);
                bt_nwp = 0;
                refresh_paths();
                hud_status();
            }
            break;
        case 'r':
            if (bt_sel >= 0) {
                int m = hand_mirror(bt_sel, 0);
                fx_reload(bt_sel);
                hand_unmirror(bt_sel, m);
                refresh_paths();
                hud_status();
            }
            break;
        case 'h':
            /* The active hand (the panel's hands choose it too). */
            if (bt_sel >= 0) {
                g_units[bt_sel].special ^= SP_LEFT;
                msg(COL_INFO, g_units[bt_sel].special & SP_LEFT ? "Left hand" : "Right hand");
            }
            break;
        case 'g':
            if (bt_sel >= 0) {
                int s = bt_sel, m = hand_mirror(s, 1);
                grenade_act(s, 0);
                hand_unmirror(s, m);
                refresh_paths();
                hud_status();
            }
            break;
        case ' ':
            if (bt_sel >= 0) {
                bt_turn_to(bt_sel, g_cur_x, g_cur_y);
                refresh_paths();
                hud_status();
            }
            break;
        case 'c':
            if (bt_sel >= 0)
                select_unit(bt_sel);
            break;
        case 'm':
            if (bt_sel >= 0 && in_hand(bt_sel, IT_MEDIKIT, "No medi-kit")) {
                bt_medikit(bt_sel, g_cur_x, g_cur_y, g_cur_z);  /* bact.c */
                hand_back(bt_sel);
                hud_status();
            }
            break;
        case 'i':
            if (bt_sel >= 0 && !UNIT_HWP(&g_units[bt_sel])) {   /* HWPs have none */
                /* battle_run opens it (invscr.c next to the core alone). */
                bt_request = BR_INVENTORY;
                again = 1;
                return;
            }
            break;
        case 'n':
            if (bt_sel >= 0 && in_hand(bt_sel, IT_SCANNER, "No motion scanner")) {
                bu_motion_scan(bt_sel);
                hand_back(bt_sel);
                hud_status();
            }
            break;
        case 'p':
        case BK_PANIC:
        case BK_CONTROL:
            if (bt_sel >= 0 && in_hand(bt_sel, IT_PSIAMP, "No Psi-Amp")) {
                psi_amp(bt_sel, ch == 'p' ? -1 : ch == BK_CONTROL);
                hand_back(bt_sel);
                hud_status();
            }
            break;
        case 't':
            if (bt_sel >= 0) {
                int s = bt_sel, m = hand_mirror(s, 1);
                grenade_act(s, 1);
                hand_unmirror(s, m);
                hud_status();
            }
            break;
        case 'o':
            if (bt_sel >= 0 && in_hand(bt_sel, IT_MINDPROBE, "No mind probe")) {
                if (bu_mind_probe(bt_sel))
                    hud_help();
                hand_back(bt_sel);
                refresh_paths();
                hud_status();
                hud_cursor();
            }
            break;
        case 'k':
            if (bt_sel >= 0) {
                toggle_kneel(bt_sel);
                refresh_paths();
                hud_status();
            }
            break;
        case 'v': {
            /* Cursor to the next visible alien. */
            int i;
            for (i = 1; i <= g_nunits; i++) {
                int a = (bt_vlast + i) % g_nunits;
                const Unit *al = &g_units[a];
                if (al->alive && al->faction == FACTION_ALIEN && al->seen) {
                    bt_vlast = a;
                    bp_set_cursor(al->x, al->y, al->z);
                    log_printf("T%d: in sight: %s at %d,%d,%d", bt_turn, al->name, al->x,
                               al->y, al->z);   /* for test scripts */
                    break;
                }
            }
            if (i > g_nunits)
                msg(COL_INFO, "No aliens in sight");
            break;
        }
        case 'x': {
            /* TU reserve: none, snap, aimed, auto. */
            static const char *const names[4] = { "none", "snap shot", "aimed shot", "auto shot" };
            char text[32];
            bt_reserve = (bt_reserve + 1) & 3;
            panel_reserve(bt_reserve);
            sprintf(text, "TU reserved: %s", names[bt_reserve]);
            msg(COL_INFO, text);
            break;
        }
        case 'q': {
            /* The minimap; a tile picked there gets the cursor. */
            int mx = g_cur_x, my = g_cur_y, mz = g_cur_z;
            int picked = bu_minimap(&mx, &my, &mz);
            g_view_z = g_cur_z;
            if (picked)
                g_cur_x = mx, g_cur_y = my, g_cur_z = g_view_z = mz;
            render_center(g_cur_x, g_cur_y, g_cur_z);
            hud_help();
            hud_cursor();
            break;
        }
        case 'l':
            /* Layers: the levels above the view level too, or not. */
            g_all_layers = !g_all_layers;
            render_full();
            msg(COL_INFO, g_all_layers ? "All levels shown" : "Levels up to the view level");
            break;
        case 'z':
            /* DEBUG: kill the alien at the cursor, blast the objective
               there, else the cursor to the objective (bdebug.c). */
            if (g_debug) {
                int x = g_cur_x, y = g_cur_y, z = g_cur_z;
                if (!inv_debug_zap(&x, &y, &z))
                    bp_set_cursor(x, y, z);
                hud_status();
            }
            break;
        case 'y':
            if (g_debug)
                inv_debug_stun(g_cur_x, g_cur_y, g_cur_z);  /* DEBUG: bdebug.c */
            break;
        case 's':
            g_sound_on = !g_sound_on;
            msg(COL_INFO, g_sound_on ? "Sound on" : "Sound off");
            break;
        case 'e':
            return;
        }
        if (bt_sel >= 0 && !g_units[bt_sel].alive) {
            bt_sel = next_soldier(bt_sel);
            if (bt_sel >= 0)
                select_unit(bt_sel);
        }
    }
}

/* The turn, then no arrow (the aliens' turn, the inventory, the options). */
void player_turn(void)
{
    turn();
    arrow_to(-1);
}

/* Then the view and the panel. */
void battle_view(void)
{
    /* A saved battle keeps its selection; the view is centred on the
       selected unit (BattlescapeState::init, as at the start), and its
       first player_turn shows no turn message (g_bs_resume: battle_run
       clears it after this): DOS shows none after a load. */
    target = 0;
    again = g_bs_resume;
    if (bt_sel < 0 || !g_units[bt_sel].alive)
        bt_sel = next_soldier(-1);
    if (bt_sel >= 0) {
        g_cur_x = g_units[bt_sel].x;
        g_cur_y = g_units[bt_sel].y;
        g_cur_z = g_units[bt_sel].z;
    }
    g_cursor_on = 1;
    g_view_z = g_cur_z;
    render_center(g_cur_x, g_cur_y, g_cur_z);
    busy_show();                /* the whole view takes most of a second */
    render_full();
    panel_reserve(bt_reserve);
    panel_open();
}

/* The closing message (objective_done: the objective tiles are gone). */
void battle_over(int objective_done)
{
    if (bt_result == 1 && objective_done)
        msg(COL_GOOD, g_pak.script == SCRIPT_BOSS ? "The alien brain is destroyed!"
                                                  : "Alien base control destroyed!");
    else if (bt_result == 1 && g_civ_total)
        msg(COL_GOOD, "Aliens eliminated. Terror site secured!");
    else if (bt_result == 1)
        msg(COL_GOOD, g_pak.script == SCRIPT_CRASH ? "UFO crew eliminated. Mission successful!"
                                                   : "All aliens eliminated. Mission successful!");
    else if (bt_result == -1)
        msg(COL_BAD, "Squad lost. Mission failed.");
    else
        msg(COL_BAD, "Mission aborted.");
}
