/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Base Information (BaseInfoState): its own module, in the research
   screens' overlay section (labscr), so that the basescape's facility
   screens (facilscr) stay small enough to leave the overlay manager its
   1 KB next to the geoscape and the quantity lists (docs/design.md,
   "Sections left over"). */

#include <stdio.h>
#include <string.h>
#include "alien.h"
#include "base.h"
#include "craft.h"
#include "facil.h"
#include "geo.h"
#include "inv.h"
#include "gfx.h"
#include "hud.h"
#include "uik.h"
#include "input.h"
#include "unit.h"

#define defs g_fdefs


/* Calls fn for the top-left cell of every finished facility. */
#define EACH_BUILT(x, y, d) \
    for (y = 0; y < BASE_W; y++) \
        for (x = 0; x < BASE_W; x++) \
            if (g_facil.cell[y][x] && !FAC_PART(g_facil.cell[y][x]) && !g_facil.days[y][x] \
                && ((d) = &defs[g_facil.cell[y][x]]) != NULL)

static int p_used[7], p_cap[9];      /* facil_panel */

/* What is used and what the base holds, the 7 CAP_*, then cap[7] the
   radar range, cap[8] the upkeep (out of the hourly section, whose room
   next to the base screens is short). */
static void facil_panel(int *used, int *cap)
{
    static const unsigned char caps[7] = {
        CAP_QUARTERS, CAP_LABS, CAP_WORKSHOPS, CAP_STORES, CAP_HANGARS, CAP_ALIENS, CAP_PSI
    };
    int i;

    used[0] = facil_personnel();
    used[1] = base_sci_allocated();        /* Base::getUsedLaboratories */
    used[2] = base_workshops_used();
    used[3] = (facil_stores_used() + 9) / 10;
    used[4] = craft_hangars_used();
    used[5] = live_aliens();
    used[6] = 0;                /* psi_trainees, without a call into psi.c's section */
    for (i = 0; i < g_roster_n; i++)
        used[6] += g_roster[i].alive && (g_roster[i].psi_flags & PSF_TRAIN);
    for (i = 0; i < 7; i++)
        cap[i] = facil_capacity(caps[i]);
    cap[7] = facil_radar_range();
    cap[8] = facil_upkeep();
}

/* The eight small bases: a base's finished facilities green, those under
   construction red, the selected base framed in white (MiniBaseView), at
   x0, y0 (the basescape's at the right, Base Information's at the top). */
static void draw_mini(int x0, int y0)
{
    int i, x, y;

    for (i = 0; i < MAX_BASES; i++) {
        int bx = x0 + i * 16;
        uk_fill(bx, y0, 16, 16, i == g_bcur ? 15 : 0);
        uk_fill(bx + 1, y0 + 1, 14, 14, g_video == VIDEO_CGA ? 0 : 6);
        if (i >= g_nbases)
            continue;
        {
            const Facilities *f = i == g_bcur ? &g_facil : &g_bb->rec[i].facil;
            for (y = 0; y < BASE_W; y++)
                for (x = 0; x < BASE_W; x++) {
                    unsigned char c = f->cell[y][x];
                    if (FAC_TYPE(c) != FAC_NONE && !FAC_PART(c)) {
                        int s = g_fdefs[FAC_TYPE(c)].size * 2;
                        uk_fill(bx + 2 + x * 2, y0 + 2 + y * 2, s, s, f->days[y][x] ? 12 : 10);
                    }
                }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Base Information (BaseInfoState): staff, space and defences with their
   bars, the small bases, a click on the name to rename the base, and OK,
   Transfers, Stores and Monthly Costs (MonthlyCostsState). Returns 0
   for OK, 1 Stores, 2 Transfers, 3 Monthly Costs: the geoscape runs those and comes back. */

/* Monthly Costs is costs_screen (soldier.c): returned as 3, the geoscape runs
   it. */

static void info_bar(int y, const char *name, int val, int max, int scale8, int colour)
{
    char t[16];
    int w = (int)((long)max * scale8 / 8), v = (int)((long)val * scale8 / 8);

    uk_text(8, y, UK_SMALL, 14, name);
    sprintf(t, "%d:%d", val, max);
    uk_text(126, y, UK_SMALL, 15, t);
    if (w > 150)
        w = 150;
    if (v > 150)
        v = 150;
    uk_fill(166, y + 2, w, 5, 8);
    uk_fill(166, y + 2, v, 5, colour);
}

static void info_draw(void)
{
    int def = 0, sr = 0, lr = 0, x, y;
    const FacilDef *d;

    facil_panel(p_used, p_cap);
    uk_cga_palette(2);
    uk_screen(UI_BG_BASEINFO, T_PURPLE);
    draw_mini(182, 8);
    uk_text(8, 8, uk_width(UK_BIG, g_bhdr[g_bcur].name) > 160 ? UK_SMALL : UK_BIG, 13, g_bhdr[g_bcur].name);
    uk_text(8, 30, UK_SMALL, 13, "PERSONNEL AVAILABLE:PERSONNEL TOTAL>");
    info_bar(41, "Soldiers", roster_ready(), roster_alive(), 8, 6);
    info_bar(51, "Engineers", g_base.engineers - base_eng_allocated(), g_base.engineers, 8, 6);
    info_bar(61, "Scientists", g_base.scientists - base_sci_allocated(), g_base.scientists, 8, 6);
    uk_text(8, 72, UK_SMALL, 13, "SPACE USED:SPACE AVAILABLE>");
    info_bar(83, "Living Quarters", p_used[0], p_cap[0], 4, 10);
    info_bar(93, "Stores", p_used[3], p_cap[3], 4, 10);
    info_bar(103, "Laboratories", p_used[1], p_cap[1], 4, 10);
    info_bar(113, "Work Shops", p_used[2], p_cap[2], 4, 10);
    info_bar(123, "Alien Containment", p_used[5], p_cap[5], 4, 10);
    info_bar(133, "Hangars", p_used[4], p_cap[4], 144, 10);
    EACH_BUILT(x, y, d) {
        def += d->defence;
        sr += d->radar && d->radar < 30;
        lr += d->radar >= 30;
    }
    info_bar(147, "Defence Strength", def, def, 1, 12);
    info_bar(157, "Short Range Detection", sr, sr, 200, 9);
    info_bar(167, "Long Range Detection", lr, lr, 200, 9);
    uk_button(10, 180, 30, 14, "OK", 0);
    uk_button(46, 180, 80, 14, "TRANSFERS", 0);
    uk_button(132, 180, 80, 14, "STORES", 0);
    uk_button(218, 180, 92, 14, "Monthly Costs", 0);
}

int base_info_screen(void)
{
    int ret = -1;

    uk_open();
    while (ret < 0) {
        unsigned key;
        int ch;

        info_draw();
        uk_save(0, 200);
        key = input_key();
        ch = key & 0xFF;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (key == KEY_RCLICK || ch == 27 || ch == 13)
            ret = 0;
        else if (key == KEY_LCLICK) {
            int mx = g_mouse_x, my = g_mouse_y;
            if (my >= 180 && my < 194) {
                if (mx >= 10 && mx < 40)
                    ret = 0;
                else if (mx >= 46 && mx < 126)
                    ret = 2;
                else if (mx >= 132 && mx < 212)
                    ret = 1;
                else if (mx >= 218 && mx < 310)
                    ret = 3;
            } else if (my >= 8 && my < 24 && mx < 135) {
                /* The base's name is an edit box (BaseInfoState::edtBase). */
                char nm[16];
                strcpy(nm, g_bhdr[g_bcur].name);
                if (uk_edit("Name of the base:", nm, 15) && nm[0])
                    strcpy(g_bhdr[g_bcur].name, nm);
            } else if (my >= 8 && my < 24 && mx >= 182 && (mx - 182) / 16 < g_nbases)
                base_select((mx - 182) / 16);
        } else if (ch == 'c')
            ret = 3;
        else if (ch == 't')
            ret = 2;
        else if (ch == 's')
            ret = 1;
        else if (ch >= '1' && ch <= '0' + g_nbases)
            base_select(ch - '1');
    }
    uk_close();
    return ret;
}
