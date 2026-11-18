/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The DEBUG option's battle keys Z and Y (inv.h): an overlay section of
   its own next to the battle core and bplayer (-zc), so that the hand
   menu's section (bact.c) keeps room; loaded only when a key is used. */

#include <stdio.h>
#include "battle.h"
#include "battle_in.h"
#include "effects.h"
#include "inv.h"
#include "input.h"
#include "map.h"
#include "pak.h"
#include "unit.h"

/* DEBUG option: battle key Z (inv.h) */

/* A test of stray writes (tests/README.md, WIN.TXT): the floor pictures are
   as loaded. Layout decides where such a write lands, so this catches only
   those that land here; they hang the blitter or show the wrong picture. */
static void check_fob(void)
{
    unsigned i, s = 0;

    if (g_fob) {
        for (i = 0; i < g_fob_size; i++)
            s += g_fob[i];
        if (s != g_fob_sum)
            log_printf("floor pictures changed: sum %u, loaded %u", s, g_fob_sum);
    }
}

int inv_debug_zap(int *x, int *y, int *z)
{
    const unsigned char *t = TILE(*x, *y, *z);
    int u = UNIT_AT(*x, *y, *z) - 1, p, i, j, k;
    int sp = g_pak.objective ? g_pak.objective : g_pak.script == SCRIPT_MARS ? SPECIAL_END
             : g_base_layout ? SPECIAL_XBASE : 0;

    check_fob();
    if (u >= 0 && g_units[u].faction == FACTION_ALIEN && g_units[u].alive
        && g_units[u].stun < g_units[u].hp) {
        log_printf("T%d: DEBUG Z kills %s at %d,%d,%d", bt_turn, g_units[u].name, *x, *y, *z);
        bt_burn(u, 255);
        return 1;
    }
    if (t[PART_OBJECT] && (g_pak.mcd[t[PART_OBJECT]].flags & MCD_EXPL)) {
        /* An explosive object breaks as a hit would break it (tests of
           terrain explosions, bturn.c): it blows up. */
        log_printf("T%d: DEBUG Z breaks the object at %d,%d,%d", bt_turn, *x, *y, *z);
        bt_break((unsigned char *)t, PART_OBJECT, *z);
        terrain_changed(*x, *y, *z);
        bt_boom_run(-1);
        return 1;
    }
    for (p = 0; p < 4 && (g_pak.objective || g_base_layout); p++)
        if (t[p] && g_pak.mcd[t[p]].special == sp) {
            log_printf("T%d: DEBUG Z blasts the objective at %d,%d,%d", bt_turn, *x, *y, *z);
            bt_explode(*x, *y, *z, 200, -1);
            return 1;
        }
    if (!sp) {
        /* No objective or exit area: a blast at the cursor (tests of
           falling units, bt_fall). */
        log_printf("T%d: DEBUG Z blasts %d,%d,%d", bt_turn, *x, *y, *z);
        bt_explode(*x, *y, *z, 200, -1);
        return 1;
    }
    for (k = 0; k < (int)g_mz; k++)
        for (j = 0; j < (int)g_my; j++)
            for (i = 0; i < (int)g_mx; i++) {
                t = TILE(i, j, k);
                for (p = 0; p < 4; p++)
                    if (t[p] && g_pak.mcd[t[p]].special == sp) {
                        log_printf("T%d: %s at %d,%d,%d", bt_turn,
                                   g_pak.objective || g_base_layout ? "objective" : "exit area",
                                   i, j, k);
                        *x = i;
                        *y = j;
                        *z = k;
                        return 0;
                    }
            }
    return 0;
}

/* DEBUG option: battle key Y, the alien at the cursor falls unconscious
   when its side's turn starts (stun 255; captures in tests); a soldier
   too, but it wakes up two rounds later (stun health + 1). */
void inv_debug_stun(int x, int y, int z)
{
    int u = UNIT_AT(x, y, z) - 1;

    if (u >= 0 && g_units[u].alive) {
        g_units[u].stun = (unsigned char)(g_units[u].faction == FACTION_ALIEN ? 255
                                          : g_units[u].hp + 1);
        log_printf("T%d: DEBUG Y stuns %s at %d,%d,%d", bt_turn, g_units[u].name, x, y, z);
    } else if (g_shot_log) {
        /* SHOTLOG, no unit: the tile's walls go, not its doors (tests of 2x2
           units at a door with no wall beside it; DOS's maps have none until
           a wall is shot away). */
        unsigned char *t = TILE(x, y, z);

        for (u = PART_WESTWALL; u <= PART_NORTHWALL; u++)
            if (!(g_pak.mcd[t[u]].flags & (MCD_DOOR | MCD_UFO_DOOR)))
                t[u] = 0;
        terrain_changed(x, y, z);
        log_printf("T%d: DEBUG Y clears the walls at %d,%d,%d", bt_turn, x, y, z);
    }
}
