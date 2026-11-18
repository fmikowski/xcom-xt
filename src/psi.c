/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Psi training in the base (geoscape side of psionics, see psi.h). In
   hwpscr's overlay section: alone this small section fitted next to the
   quantity lists with under 1 KB left (build.ps1, $apart). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "facil.h"
#include "geo.h"
#include "hud.h"
#include "input.h"
#include "map.h"
#include "psi.h"
#include "uik.h"
#include "unit.h"

int psi_trainees(void)
{
    int i, n = 0;

    for (i = 0; i < g_roster_n; i++)
        if (g_roster[i].alive && (g_roster[i].psi_flags & PSF_TRAIN))
            n++;
    return n;
}

/* One month in the lab (OpenXcom Soldier::trainPsi with the xcom1 soldier
   rules): a first month gives 16-24 skill and shows the soldier's psi
   strength, then 5-12 a month up to 50 and 1-3 up to 100. */
static void train(Soldier *s)
{
    int gain;

    if (s->psi_skill <= 16)
        gain = 16 + rnd(9);
    else if (s->psi_skill <= 50)
        gain = 5 + rnd(8);
    else
        gain = 1 + rnd(3);
    s->psi_skill = (unsigned char)(s->psi_skill + gain > 100 ? 100 : s->psi_skill + gain);
    s->psi_flags |= PSF_KNOWN;
    log_printf("PSI %s trained: skill %d strength %d", s->name, s->psi_skill, s->psi);
}

const char *psi_month(void)
{
    static char msg[40];
    int i, n = 0, room = facil_capacity(CAP_PSI);

    for (i = 0; i < g_roster_n; i++) {
        Soldier *s = &g_roster[i];
        if (!s->alive || !(s->psi_flags & PSF_TRAIN))
            continue;
        if (n >= room) {                /* labs dismantled: no place */
            s->psi_flags &= ~PSF_TRAIN;
            continue;
        }
        train(s);
        n++;
    }
    if (!n)
        return NULL;
    sprintf(msg, "Psi training: %d soldier%s improved", n, n > 1 ? "s" : "");
    return msg;
}

/* DEBUG option: G grants the psionics research and kit, T trains now. */
static int debug_key(int sel, int ch, int scan)
{
    (void)sel;
    (void)scan;
    if (!g_debug)
        return 0;
    if (ch == 'g') {
        g_base.done[RS_PSILAB] = g_base.done[RS_PSIAMP] = g_base.done[RS_MINDPROBE] = 1;
        g_base.stores[IT_PSIAMP] += 2;
        g_base.stores[IT_MINDPROBE] += 1;
        log_printf("PSI debug: research and kit granted");
    } else if (ch == 't') {
        const char *m = psi_month();
        log_printf("PSI debug: %s", m ? m : "nobody in training");
    } else
        return 0;
    return 1;
}

/* Column offsets as a string literal (see menu.c slot_tabs). */
#define psi_tabs    ((const unsigned char *)"\x8C\xBE\xF0")
static const UkMenu psi_menu = {
    UI_BG_PSI, T_PURPLE, UKS_LIST | UKS_CLICK, 0, 0, 320, 200, 8, 50, 304, 14, 9, 8,
    "Psionic Training", "Name\tStrength\tSkill\tIn training", psi_tabs,
    "Enter=Train / stop|Esc=OK", 178, NULL
};

void psi_screen(void)
{
    char (*lines)[41] = malloc((g_roster_n ? g_roster_n : 1) * sizeof *lines);   /* only while open */
    char help[41];
    int sel = 0, i;

    if (!lines)
        return;
    for (;;) {
        int cap = facil_capacity(CAP_PSI);
        for (i = 0; i < g_roster_n; i++) {
            const Soldier *s = &g_roster[i];
            char str[4];
            if (s->psi_flags & PSF_KNOWN)
                sprintf(str, "%3d", s->psi);
            else
                strcpy(str, "  ?");
            sprintf(lines[i], "%s\t%s\t%d\t%s", s->name, str,
                    s->psi_skill, s->psi_flags & PSF_TRAIN ? "Yes" : "No");
        }
        sprintf(help, "In training: %d   Laboratory space: %d", psi_trainees(), cap);
        sel = uk_menu(&psi_menu, help, lines, g_roster_n, sel, debug_key, NULL);
        if (sel < 0)
            break;
        if (g_roster[sel].psi_flags & PSF_TRAIN)
            g_roster[sel].psi_flags &= ~PSF_TRAIN;
        else if (psi_trainees() < cap)
            g_roster[sel].psi_flags |= PSF_TRAIN;
        else
            uk_notice(cap ? "No free space in the psi labs."
                          : "Build a Psionic Laboratory first.");
    }
    free(lines);
}
