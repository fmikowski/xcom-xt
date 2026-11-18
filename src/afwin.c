/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The air combat windows (see afwin.h), laid out as OpenXcom's
   DogfightState: the window (INTERWIN.DAT, 160 x 96), the radar (77 x 74
   at 3, 3) with the UFO coming down towards the craft, weapon icons and
   ranges, stance buttons, the craft's damage, distance and status, the
   minimise corner. Up to four at once (calculateWindowPosition). The UI
   pack keeps the pictures where a lone window has them (80, 52); other
   windows draw them shifted. Compiled with -zc: its tables stay in its
   overlay section. */

#include <stdio.h>
#include "afwin.h"
#include "craft.h"
#include "gfx.h"
#include "input.h"
#include "uik.h"

#define WX0         80          /* a lone window: the pictures' place */
#define WY0         52
#define RADAR_H     74
#define UFO_DX      (3 + 77 / 2)
#define BASE_DY     (3 + RADAR_H)   /* distance 0 */

/* Buttons (window coordinates): standoff, cautious, standard, aggressive,
   disengage, UFO, weapon 1, weapon 2 (15 x 17 at 4 / 64, 52), minimise
   (12 x 12 in the corner). */
static const unsigned char buttons[9][4] = {
    { 83, 4, 36, 15 }, { 120, 4, 36, 15 }, { 83, 20, 36, 15 }, { 120, 20, 36, 15 },
    { 120, 36, 36, 15 }, { 83, 36, 36, 17 }, { 4, 52, 15, 17 }, { 64, 52, 15, 17 },
    { 0, 0, 12, 12 }
};
#define BUTTON_KEYS "12345u67m"

/* A picture of the pack at its place in window w. */
static void wpic(const AwWin *w, unsigned id)
{
    uk_pic(id, 0x4000 + w->x - WX0, w->y - WY0);
}

static int dist_y(const AwWin *w, int dist)
{
    int y = w->y + BASE_DY - dist / 8;
    return y < w->y + 3 + w->ufo_r ? w->y + 3 + w->ufo_r : y;
}

void aw_place(AwWin *w, int k, int n)
{
    /* 1: the middle; 2: top and bottom; 3: top, bottom left and right;
       4: the corners. */
    static const unsigned char pos[4][4][2] = {
        { { 80, 52 } }, { { 80, 0 }, { 80, 104 } },
        { { 80, 0 }, { 0, 104 }, { 160, 104 } },
        { { 0, 0 }, { 160, 0 }, { 0, 104 }, { 160, 104 } }
    };

    if (n < 1)
        n = 1;
    if (n > 4)
        n = 4;
    w->x = pos[n - 1][k & 3][0];
    w->y = pos[n - 1][k & 3][1];
}

void aw_begin(void)
{
    uk_open();
    uk_theme(T_GREEN);
    mouse_hide();
}

void aw_weapon(AwWin *w, int slot, int range, int off)
{
    int x;

    /* The weapon's range: a dotted line across its meter (_range1/2 at 19
       and 43, 21 wide), as DogfightState draws it; grey when off. */
    if (range > 0 && range / 8 < RADAR_H)
        for (x = 0; x <= 20; x += 2)
            uk_fill(w->x + 19 + slot * 24 + x, w->y + BASE_DY - range / 8, 1, 1, off ? 8 : 14);
}

void aw_open(AwWin *w, int craft, const unsigned char weapon[2], const int range[2],
             const unsigned char off[2], int ufo_size)
{
    int s;

    uk_pic(UI_DF_WINDOW, w->x, w->y);
    for (s = 0; s < 2; s++) {
        if (weapon[s] == CW_NONE)
            continue;
        wpic(w, UI_DF_WEAPON + s * 6 + weapon[s]);
        aw_weapon(w, s, range[s], off[s]);
    }
    /* The minimise corner. */
    uk_rect(w->x + 1, w->y + 1, 10, 10, 8);
    uk_fill(w->x + 3, w->y + 8, 6, 1, 7);
    w->ufo_r = ufo_size < 0 ? 1 : ufo_size > 4 ? 5 : 1 + ufo_size;    /* 1 .. 5 */
    w->stance = -1;
    w->ufo_y = -1;
    w->ufo_shown = 0;
    aw_damage(w, craft, 0);
}

void aw_ready(void)
{
    uk_save(0, 200);                    /* the clean windows */
}

void aw_close(int wait)
{
    unsigned key;

    if (wait)
        do
            key = input_key();
        while (key == KEY_LCLICK && g_mouse_y >= 200);
    uk_close();
}

unsigned aw_click(const AwWin *w)
{
    int i, x = g_mouse_x - w->x, y = g_mouse_y - w->y;

    for (i = 8; i >= 0; i--)
        if (x >= buttons[i][0] && x < buttons[i][0] + buttons[i][2] && y >= buttons[i][1]
            && y < buttons[i][1] + buttons[i][3])
            return (unsigned char)BUTTON_KEYS[i];
    return 0;
}

void aw_stance(AwWin *w, int stance)
{
    const unsigned char *b;

    if (stance == w->stance)
        return;
    if (w->stance >= 0) {
        b = buttons[w->stance];
        uk_restore(w->x + b[0], w->y + b[1], b[2], b[3]);
    }
    wpic(w, UI_DF_PRESSED + stance);
    w->stance = stance;
}

static void text(const AwWin *w, int x, int y, int width, int colour, const char *s)
{
    uk_restore(w->x + x, w->y + y, width, 9);
    uk_text(w->x + x, w->y + y, UK_SMALL, colour | UK_FLAT, s);
}

void aw_status(AwWin *w, int colour, const char *s)
{
    text(w, 4, 85, 152, colour, s);
}

void aw_distance(AwWin *w, int dist)
{
    char s[8];

    sprintf(s, "%d", dist);
    text(w, 116, 72, 40, 10, s);
}

void aw_ammo(AwWin *w, int slot, int rounds, int off)
{
    char s[8];

    sprintf(s, "%d", rounds);
    text(w, 4 + slot * 60, 70, 16, off ? 8 : 10, s);
}

void aw_damage(AwWin *w, int craft, int percent)
{
    int q = percent <= 0 ? 0 : percent >= 100 ? 4 : percent / 25 + (percent % 25 > 0);

    if (q > 4)
        q = 4;
    uk_restore(w->x + 93, w->y + 40, 22, 25);
    wpic(w, UI_DF_CRAFT + craft * 5 + q);
}

/* The UFO: a round blob (DogfightState's _ufoBlobs), light in the middle. */
static void blob(const AwWin *w, int y, int flash)
{
    int dy, r = w->ufo_r, ux = w->x + UFO_DX;

    for (dy = -r; dy <= r; dy++) {
        int h = 0, ady = dy < 0 ? -dy : dy;
        while ((h + 1) * (h + 1) + ady * ady <= r * r + r)
            h++;
        uk_fill(ux - h, y + dy, 2 * h + 1, 1, flash ? 15 : ady * 2 < r ? 7 : 8);
        if (!flash && ady * 2 < r && h > 1)
            uk_fill(ux - h / 2, y + dy, h + 1, 1, 15);
    }
}

void aw_ufo(AwWin *w, int dist, int flash, int gone)
{
    int y = dist_y(w, dist), r = w->ufo_r;

    if (w->ufo_shown && (y != w->ufo_y || gone))
        uk_restore(w->x + UFO_DX - r, w->ufo_y - r, 2 * r + 1, 2 * r + 1);
    w->ufo_shown = !gone;
    if (gone)
        return;
    blob(w, y, flash);
    w->ufo_y = y;
}

void aw_shot(AwWin *w, int slot, int pos, int erase)
{
    int x = w->x + UFO_DX + (slot ? 3 : -3), y = dist_y(w, pos);

    if (y > w->y + BASE_DY - 3)
        y = w->y + BASE_DY - 3;
    if (erase)
        uk_restore(x, y, 1, 3);
    else
        uk_fill(x, y, 1, 3, 14);
}

void aw_preview(AwWin *w, int ufo_sprite)
{
    unsigned key;

    uk_hold(1);
    uk_pic(UI_UFO_TOP, w->x, w->y);
    uk_pic(UI_UFO_MID + ufo_sprite, w->x, w->y + 15);
    uk_pic(UI_UFO_BOTTOM, w->x, w->y + 67);
    uk_hold(0);
    if (g_video == VIDEO_CGA)
        uk_cga_palette(2);              /* the UFOpaedia's colours */
    key = input_key();
    (void)key;
    uk_cga_palette(0);
    uk_restore(w->x, w->y, 160, 96);
    w->stance = -1;
    w->ufo_shown = 0;
}
