/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mission briefing (see brief.h), laid out as OpenXcom's BriefingState.
   In the rarely used overlay section; compiled with -zc (its strings stay
   in the section). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include "brief.h"
#include "gfx.h"
#include "input.h"
#include "pedia.h"
#include "uik.h"

int g_brief_crashed, g_brief_site;

static int kind_of(const char *pak)
{
    if (!strncmp(pak, "UR", 2))
        return BRIEF_TERROR;
    if (!strncmp(pak, "UBASE", 5))
        return BRIEF_ALIEN_BASE;
    if (!strncmp(pak, "BASE", 4))
        return BRIEF_BASE;
    if (!strncmp(pak, "CYD2", 4))
        return BRIEF_FINAL;
    if (!strncmp(pak, "CY", 2))
        return BRIEF_MARS;
    return g_brief_crashed ? BRIEF_CRASH : BRIEF_LANDED;
}

static const char *const targets[BRIEF_N] = {
    "CRASH SITE", "LANDING SITE", "TERROR SITE", "ALIEN BASE", "X-COM BASE",
    "CYDONIA", "CYDONIA"
};

void brief_show(const char *pak, const char *craft)
{
    unsigned short off[2];
    int k = kind_of(pak), n, dy;
    char *buf, *title, line[40];

    uk_open();
    if (!uk_read(UI_BRIEFING, k * 2, off, 4) || (n = off[1] - off[0]) < 5
        || (buf = malloc(n)) == NULL) {
        uk_close();
        return;
    }
    if (!uk_read(UI_BRIEFING, off[0], buf, n)) {
        free(buf);
        uk_close();
        return;
    }
    dy = (signed char)buf[2];
    title = buf + 3;
    uk_invalidate();
    uk_screen(*(unsigned short *)buf, T_GREEN);
    if (g_video == VIDEO_CGA) {
        /* Four colours: the picture under the text at half strength (as
           uk_menu's lists), or the text would not stand out. */
        int y, b;
        for (y = 20; y < 160; y++) {
            unsigned char far *d = (unsigned char far *)MK_FP(0xB800,
                (y & 1) * 0x2000 + (y >> 1) * 80);
            unsigned char k = (unsigned char)(y & 1 ? 0x33 : 0xCC);
            for (b = 2; b < 78; b++)
                d[b] &= k;
        }
    }
    uk_window(0, 0, 320, 200);
    uk_text(16, 24, UK_BIG, g_uk->text, title);
    if (g_brief_site && k <= BRIEF_ALIEN_BASE)
        sprintf(line, "%s-%d", targets[k], g_brief_site);
    else
        strcpy(line, targets[k]);
    uk_text(16, 40 + dy, UK_BIG, g_uk->text, line);
    if (craft) {
        sprintf(line, "CRAFT> %s", craft);
        uk_text(16, 56 + dy, UK_BIG, g_uk->text, line);
    }
    pedia_flow(16, 72 + dy, 274, title + strlen(title) + 1, 158, g_uk->text, 0, NULL);
    uk_button(100, 164, 120, 18, "OK", 0);
    log_printf("BRIEF %s: %s", pak, title);
    for (;;) {
        unsigned key = input_key();
        if (key == KEY_LCLICK && (g_mouse_x < 100 || g_mouse_x >= 220 || g_mouse_y < 164
                                  || g_mouse_y >= 182))
            continue;
        break;
    }
    free(buf);
    uk_close();
}
