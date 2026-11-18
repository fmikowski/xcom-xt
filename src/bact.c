/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Battle action windows (an overlay section of their own next to the
   battle core and bplayer, -zc: the strings stay here): the hand action
   popup (ActionMenuState) and the grenade timer (PrimeGrenadeState). */

#include <stdio.h>
#include <string.h>
#include "ai.h"
#include "battle.h"
#include "battle_in.h"
#include "effects.h"
#include "gfx.h"
#include "inv.h"
#include "input.h"
#include "items.h"
#include "map.h"
#include "pak.h"
#include "panel.h"
#include "psi.h"
#include "render.h"
#include "rules.h"
#include "sound.h"
#include "uik.h"
#include "unit.h"

#define PY          144

/* The map under a window shown again: the windows draw to the screen
   (uk_*), the back buffer still holds the view, cursor and all; VGA
   draws it again. */
static void view_back(int c0, int r0, int cols, int rows)
{
    if (g_vga)
        render_full();
    else
        gfx_present_cells(c0, r0, cols, rows);
}

/* ------------------------------------------------------------------ */
/* Reloading (the clip types are in src/effects.c) */

static int loaded_clip(const Unit *u)
{
    return u->load ? u->load : g_items[u->weapon].clip_item;
}

/* A full clip taken out of the weapon goes into the inventory's first
   free slot, else to the floor. */
static void return_clip(Unit *u, int clip)
{
    if (u->clips && clip == g_items[u->weapon].clip_item)
        u->clips++;             /* aliens: belt clips */
    else if (carry_place((int)(u - g_units), clip, 0) < 0)
        floor_add(u->x, u->y, u->z, clip, 1);
}

/* Reload (R, the hand menu; 15 TU as BattleUnit::checkAmmo): a clip the
   weapon takes from anywhere in the inventory, the type picked when there
   are several (either hand's gun, the left one mirrored: its clip type
   stays in its n); a part-used clip (Unload) gives its rounds. */
void fx_reload(int s)
{
    Unit *u = &g_units[s];
    int clip[N_AMMO + 1], cnt[N_AMMO + 1], n = 0, k, i, std, c, pick;
    char line[48];

    if (u->weapon == NO_ITEM || !g_items[u->weapon].clip_size) {
        msg(COL_INFO, "Nothing to reload");
        return;
    }
    std = g_items[u->weapon].clip_item;
    if (u->clips) {
        clip[n] = std;
        cnt[n++] = u->clips;
    }
    for (k = 0; k < g_max_carry; k++) {
        const Carry *e = &g_carry[k];
        if (e->unit != s || (e->item != std && !ammo_find(u->weapon, e->item)))
            continue;
        for (i = 0; i < n && clip[i] != e->item; i++)
            ;
        if (i == n) {
            if (n > N_AMMO)
                continue;
            clip[n] = e->item;
            cnt[n++] = 0;
        }
        cnt[i]++;
    }
    if (!n) {
        msg(COL_BAD, "No spare clips");
        return;
    }
    if (u->tu < 15) {
        msg(COL_BAD, "Not enough time units");
        return;
    }
    pick = 0;
    if (n > 1) {
        strcpy(line, "Load");
        for (k = 0; k < n; k++) {
            const AmmoDef *a = ammo_find(u->weapon, clip[k]);
            sprintf(line + strlen(line), " %d:%s x%d", k + 1, a ? a->tag : "", cnt[k]);
        }
        msg(COL_INFO, line);
        pick = (input_key() & 0xFF) - '1';
        if (pick < 0 || pick >= n) {
            msg(COL_INFO, "");
            return;
        }
    }
    c = clip[pick];
    if (c == loaded_clip(u) && u->ammo == g_items[u->weapon].clip_size) {
        msg(COL_INFO, "Weapon is fully loaded");
        return;
    }
    i = 0;
    if (u->clips && c == std)
        u->clips--;
    else {
        k = carry_find(s, c);
        g_carry[k].unit = CARRY_FREE;
        i = g_carry[k].n;                   /* rounds of a part-used clip */
    }
    if (u->ammo == g_items[u->weapon].clip_size)
        return_clip(u, loaded_clip(u));     /* unloads a full clip */
    u->tu -= 15;
    u->ammo = (unsigned char)(i ? i : g_items[u->weapon].clip_size);
    u->load = (unsigned char)(c == std ? 0 : c);
    sprintf(line, "Reloaded: %s", g_items[c].name);
    msg(COL_INFO, line);
}

/* ------------------------------------------------------------------ */
/* Grenades */

/* Prime the belt grenade (ActionMenuState BA_PRIME): belt to hand 4 TU,
   priming 50 %, its timer from the Set Timer window (PrimeGrenadeState,
   0-23 ends of a turn; proximity grenades are armed without one). It is
   then thrown for 25 % (G); primed grenades in hand do not go off. */
static void prime_grenade(int s)
{
    Unit *u = &g_units[s];
    int cost = (UNIT_SOLDIER(u) ? 0 : 4) + u->tu_max / 2, t = 0;

    if (u->grenade == NO_ITEM || !u->n_grenades) {
        msg(COL_BAD, "No grenades");
        return;
    }
    if (u->fuse || u->grenade == IT_FLARE) {
        msg(COL_INFO, u->fuse ? "Grenade is already activated" : "Flares need no priming");
        return;
    }
    if (u->tu < cost) {
        msg(COL_BAD, "Not enough time units");
        return;
    }
    if (u->grenade != IT_PROXIMITY) {
        t = bu_timer();
        view_back(8, 37, 25, 101);      /* the window's cells */
        if (t < 0)
            return;
    }
    u->tu -= (unsigned char)cost;
    u->fuse = (unsigned char)(t + 1);
    log_printf("T%d: %s primes %s, timer %d", bt_turn, u->name, g_items[u->grenade].name, t);
    msg(COL_INFO, "Grenade is activated");
}

/* Throwing and priming use the grenade in the left hand; a soldier with
   none there takes the one carried that is quickest to reach into it
   first (the inventory's TU, inventories.rul; given back when the action
   does not happen). */
void grenade_act(int s, int prime)
{
    Unit *u = &g_units[s];
    int k = -1, cost = 0, slot = 0, n = 0;

    if (UNIT_SOLDIER(u) && (k = inv_next_grenade(s, &cost)) >= 0) {
        if (u->tu < cost) {
            msg(COL_BAD, "Not enough time units");
            return;
        }
        slot = g_carry[k].slot;
        n = g_carry[k].n;
        u->grenade = g_carry[k].item;
        u->n_grenades = 1;
        u->fuse = (unsigned char)n;
        u->tu -= (unsigned char)cost;
        g_carry[k].unit = CARRY_FREE;
    }
    if (prime)
        prime_grenade(s);
    else
        throw_grenade(s, g_cur_x, g_cur_y, g_cur_z);
    if (k >= 0 && u->n_grenades && u->fuse == n) {
        g_carry[k].unit = (unsigned char)s;
        g_carry[k].slot = (unsigned char)slot;
        u->grenade = NO_ITEM;
        u->n_grenades = 0;
        u->fuse = 0;
        u->tu += (unsigned char)cost;
    }
}

/* Turning (UnitTurnBState): 1 TU an eighth of a turn, shown an eighth a
   frame at the X-COM Movement Speed (turn_unit); a unit already
   facing the tile opens the door in front of it instead (UnitTurnBState::
   init: TileEngine::unitOpensDoor with rClick, bdoor.c `door_try`). */
void bt_turn_to(int u, int x, int y)
{
    Unit *un = &g_units[u];
    int d = dir_towards(un->x, un->y, x, y), diff = (d - un->dir + 8) % 8;
    int cost = diff > 4 ? 8 - diff : diff;

    if (!cost) {
        if ((d = door_try(un, un->dir, 1)) < 0)
            return;
        if ((d & 7) == 4)
            msg(COL_BAD, "Not enough time units");
        if (d & 8)
            log_printf("T%d: %s opened the door at %d,%d,%d (%d TU), TU %d", bt_turn, un->name,
                       g_door_x, g_door_y, g_door_z, g_door_tu, un->tu);
    } else {
        if (un->tu < cost) {
            msg(COL_BAD, "Not enough time units");
            return;
        }
        if (!reserve_ok(un, cost))
            return;
        un->tu -= cost;
        /* paced, an eighth a frame (battle.c turn_unit) */
        bt_face(u, d);
        un->turret = un->dir;
    }
    update_vision(u, 1);
    flush_fog();
}

/* The actions of the item in a hand (ActionMenuState): a weapon (launch,
   auto, snap, aimed shot, reload; the stun rod's stun), a grenade (throw,
   prime), the medi-kit, motion scanner, Psi-Amp (mind control and panic
   unit for a soldier with psi skill) and mind probe. The left hand's
   weapon and the right hand's grenade are shown mirrored (inv_mirror).
   An empty left hand offers the grenade carried that is quickest to take
   into it. Returns the key of the chosen action (BK_* for the actions
   without one), 0 if cancelled, with the map under the menu shown again;
   the caller makes the hand the active one. */
unsigned panel_actions(int sel, int right)
{
    Unit *u = &g_units[sel];
    char lines[7][48];
    unsigned char keys[7];
    int n = 0, i, k, y0, hn, it = inv_hand(u, !right, &hn), kind = it == NO_ITEM ? -1 : ITEM_KIND(it);
    int m = right ? kind == IK_GRENADE : kind == IK_WEAPON;

    if (m && !inv_mirror(sel))
        return 0;
    if (right && (kind == IK_GRENADE || it == IT_BODY))
        right = 0;                      /* (mirrored, or nothing to do) */
    else if (!right && kind == IK_WEAPON)
        right = 1;
    if (right && u->weapon != NO_ITEM) {
        if (u->weapon == IT_STUNROD) {
            sprintf(lines[n], "Stun   TU %d", unit_shot_tu(u, SHOT_SNAP));
            keys[n++] = 'f';
        } else if (BT_WAYPOINTS(u)) {
            sprintf(lines[n], "Launch missile   TU %d", unit_shot_tu(u, SHOT_AIMED));
            keys[n++] = 'w';
        } else
            for (k = 0; k < 3; k++) {           /* auto, snap, aimed as in DOS */
                int m = "\2\0\1"[k], tu = unit_shot_tu(u, m);
                if (!tu)
                    continue;
                sprintf(lines[n], "%s   Acc %d%%   TU %d", m == SHOT_AUTO ? "Auto shot"
                        : m == SHOT_SNAP ? "Snap shot" : "Aimed shot", unit_shot_acc(u, m), tu);
                keys[n++] = "fau"[m];
            }
        if (g_items[u->weapon].clip_size) {
            strcpy(lines[n], "Reload   TU 15");
            keys[n++] = 'r';
        }
    } else if (!right && (kind == IK_GRENADE || it == NO_ITEM || !UNIT_SOLDIER(u))) {
        /* The grenade, else the one carried a soldier would take into the
           empty left hand (bplayer.c, the move's TU added). */
        int g = u->grenade, pull = 0, fuse = u->fuse;
        if (g == NO_ITEM && UNIT_SOLDIER(u) && (k = inv_next_grenade(sel, &pull)) >= 0) {
            g = g_carry[k].item;
            fuse = g_carry[k].n;
        }
        if (g != NO_ITEM && (u->n_grenades || pull || g != u->grenade)) {
            sprintf(lines[n], "Throw %s   TU %d", g_items[g].name,
                    pull + (fuse ? u->tu_max / 4 : rl_throw_tu(u, g)));
            keys[n++] = 'g';
            if (!fuse && g != IT_FLARE) {
                sprintf(lines[n], "Prime grenade   TU %d",
                        pull + (UNIT_SOLDIER(u) ? 0 : 4) + u->tu_max / 2);
                keys[n++] = 't';
            }
        }
    } else if (it == IT_MEDIKIT) {
        strcpy(lines[n], "Use medi-kit   TU 10");
        keys[n++] = 'm';
    } else if (it == IT_SCANNER) {
        sprintf(lines[n], "Use motion scanner   TU %d", u->tu_max / 4);
        keys[n++] = 'n';
    } else if (it == IT_PSIAMP && u->psi_skill) {
        sprintf(lines[n], "Mind control   TU %d", PSI_AMP_TU);
        keys[n++] = BK_CONTROL;
        sprintf(lines[n], "Panic unit   TU %d", PSI_AMP_TU);
        keys[n++] = BK_PANIC;
    } else if (it == IT_MINDPROBE) {
        sprintf(lines[n], "Use mind probe   TU %d", u->tu_max * PROBE_TU_PCT / 100);
        keys[n++] = 'o';
    }
    if (m)
        inv_mirror(sel);
    if (!n)
        return 0;
    uk_theme(T_GREY);
    y0 = PY - 4 - n * 16;
    for (i = 0; i < n; i++)
        uk_button(48, y0 + i * 16, 224, 15, lines[i], 0);
    for (k = -1; k < 0; ) {
        unsigned key = input_key();
        if (key == KEY_LCLICK)
            k = g_mouse_x >= 48 && g_mouse_x < 272 && g_mouse_y >= y0 && g_mouse_y < y0 + n * 16
                ? keys[(g_mouse_y - y0) / 16] : 0;
        else if (key == KEY_RCLICK || (key & 0xFF) == 27)
            k = 0;
        else if ((key & 0xFF) >= '1' && (key & 0xFF) < '1' + n)
            k = keys[(key & 0xFF) - '1'];
        else
            for (i = 0; i < n; i++)
                if (((key & 0xFF) | 0x20) == keys[i])
                    k = keys[i];
    }
    view_back(6, y0, 28, n * 16);       /* the buttons' cells */
    return k;
}

/* The grenade timer (PrimeGrenadeState): 24 buttons, 0-23 turns, in a
   192 x 93 window under a "Set Timer" frame. Digits pick 0-9, the arrows
   and Enter any of them, a click one; Esc or the right button cancel.
   Returns the timer or -1. The caller shows the map again (cells 8-32,
   rows 37-137). */
int bu_timer(void)
{
    char t[4];
    int i, cur = 0;

    uk_theme(T_GREY);
    uk_fill(65, 45, 192, 93, g_uk->btn[3]);
    uk_window(65, 37, 192, 27);
    uk_center(65, 192, 46, UK_BIG, g_uk->title, "Set Timer");
    for (;;) {
        unsigned key;
        int ch;
        for (i = 0; i < 24; i++) {
            sprintf(t, "%d", i);
            uk_button(66 + (i % 8) * 24, 64 + (i / 8) * 25, 22, 22, t, i == cur);
        }
        key = input_key();
        ch = key & 0xFF;
        if (key == KEY_LCLICK) {
            int x = g_mouse_x - 66, y = g_mouse_y - 64;
            if (x >= 0 && y >= 0 && x < 8 * 24 && y < 3 * 25 && x % 24 < 22 && y % 25 < 22)
                return y / 25 * 8 + x / 24;
            continue;
        }
        if (key == KEY_RCLICK || ch == 27)
            return -1;
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch == '\r')
            return cur;
        switch (key >> 8) {
        case 0x4B: cur = (cur + 23) % 24; break;        /* left */
        case 0x4D: cur = (cur + 1) % 24; break;         /* right */
        case 0x48: cur = (cur + 16) % 24; break;        /* up */
        case 0x50: cur = (cur + 8) % 24; break;         /* down */
        }
    }
}

/* Waypoint markers: the numbers on their tiles (wp_shown of them; hover
   in bplayer.c draws them again when the cursor has moved). */
static int wp_shown;

void bu_wp_marks(void)
{
    char t[4];
    int i;

    for (i = 0; i < wp_shown; i++) {
        int x = TILE_SX(bt_wp[i][0], bt_wp[i][1]) + 13, y = TILE_SY(bt_wp[i][0], bt_wp[i][1],
                                                                        bt_wp[i][2] & 15) + 17;
        if (x >= 0 && x < 312 && y >= 0 && y < PY - 8) {
            sprintf(t, "%d", i + 1);
            uk_text(x, y, UK_SMALL, 14, t);
        }
    }
}

/* Missile waypoints (BattlescapeGame::primaryAction, BA_LAUNCH): up to 9
   tiles, each a click on the map or Enter on the cursor tile (the cursor
   follows the pointer, the view scrolls at the edges, the cursor keys and
   + / - or the panel's level buttons move it); the Launch button at the top
   right, or L, fires; the right button, Esc or Backspace take the last one
   back, with none left they cancel. Returns the number set in bt_wp (bt_nwp
   is left for the caller), 0 if cancelled. */
int bu_waypoints(const Unit *u)
{
    int n = 0, x, y, z;
    char t[48];

    /* ActionMenuState: checked before the targeting; -1, the message stays. */
    if (u->tu < unit_shot_tu(u, SHOT_AIMED) || !u->ammo) {
        msg(COL_BAD, u->ammo ? "Not enough time units" : inv_clips(u) ? "Out of ammo: R to reload" : "Out of ammo");
        return -1;
    }
    for (;;) {
        unsigned key;
        int scan, ch, al;
        wp_shown = n;
        bu_wp_marks();
        if (n)
            uk_button(272, 0, 48, 16, "Launch", 0);
        sprintf(t, "Waypoints %d of %d: Enter adds, L launches", n, BT_MAX_WP);
        panel_message(COL_INFO, t);
        key = bp_map_key();
        scan = key >> 8;
        ch = key & 0xFF;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        x = g_cur_x;
        y = g_cur_y;
        z = g_cur_z;
        if (key == KEY_LCLICK) {
            unsigned k;
            if (n && g_mouse_x >= 272 && g_mouse_y < 16)
                return n;
            /* The panel's level buttons and the alien markers work as in
               targeting (BattlescapeState's buttons stay on). */
            k = panel_click(&al);
            if (al >= 0) {
                g_cur_z = -1;
                bp_set_cursor(g_units[al].x, g_units[al].y, g_units[al].z);
                continue;
            }
            if (k == '+' || k == '-')
                ch = k;
            else if (k || g_mouse_y >= PY || !bp_pick_tile(g_mouse_x, g_mouse_y, g_view_z, &x, &y))
                continue;
            else {
                /* (the view stays where it is) */
                g_cur_x = x;
                g_cur_y = y;
                g_cur_z = g_view_z;
                ch = '\r';
            }
        }
        if (key == KEY_RCLICK || scan == 0x01 || ch == '\b') {
            if (!n--)
                return 0;
            wp_shown = 0;
            render_full();      /* the numbers and Launch off (drawn anew: the cursor too) */
            continue;
        }
        if (ch == 'l' && n)
            return n;
        if (ch == '\r' && n < BT_MAX_WP) {
            bt_wp[n][0] = (unsigned char)g_cur_x;
            bt_wp[n][1] = (unsigned char)g_cur_y;
            bt_wp[n][2] = (unsigned char)g_cur_z;
            if (n && bt_wp[n - 1][0] == g_cur_x && bt_wp[n - 1][1] == g_cur_y
                && (bt_wp[n - 1][2] & 15) == g_cur_z)
                bt_wp[n][2] |= BT_WP_FLOOR;
            log_printf("T%d: waypoint %d at %d,%d,%d", bt_turn, n + 1, g_cur_x, g_cur_y, g_cur_z);
            n++;
            continue;
        }
        switch (scan) {
        case 0x48: x--; y--; break;     /* up */
        case 0x50: x++; y++; break;     /* down */
        case 0x4B: x--; y++; break;     /* left */
        case 0x4D: x++; y--; break;     /* right */
        case 0x47: x--; break;          /* home */
        case 0x49: y--; break;          /* page up */
        case 0x4F: y++; break;          /* end */
        case 0x51: x++; break;          /* page down */
        }
        if (ch == '+' || ch == '=')
            z++;
        if (ch == '-')
            z--;
        if ((x != g_cur_x || y != g_cur_y || z != g_cur_z) && IN_MAP(x, y, z))
            bp_set_cursor(x, y, z);
    }
}
