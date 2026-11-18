/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AFWIN_H
#define AFWIN_H

/* The air combat windows of the DOS game (DogfightState, INTERWIN.DAT and
   INTICON.PCK) over the geoscape: drawing only, the fights themselves are
   the caller's (craft_dogfights in src/airfight.c). Up to four windows at
   once, placed as DogfightState::calculateWindowPosition does. Distances
   are OpenXcom's units: 8 a pixel of the radar, as the weapon ranges x 8. */

typedef struct {
    int x, y;                   /* the window on the screen */
    int stance, ufo_y, ufo_r, ufo_shown;
} AwWin;

/* Window k (0-based) of n open ones. */
void aw_place(AwWin *w, int k, int n);

/* The windows: aw_begin, then aw_open for each (window, weapon icons and
   range marks: range in OpenXcom units, 0 = no weapon in that slot; off:
   switched off, drawn grey; the craft picture), then aw_ready keeps the
   clean picture. craft: CT_*, weapon: CW_* or CW_NONE, ufo_size: 0 (very
   small) .. 4 (very large). */
void aw_begin(void);
void aw_open(AwWin *w, int craft, const unsigned char weapon[2], const int range[2],
             const unsigned char off[2], int ufo_size);
void aw_ready(void);

/* Waits for a key or a click (wait), then closes. */
void aw_close(int wait);

/* The key a click in window w stands for: '1'-'5' the stances, 'u' the
   UFO button, 'm' minimise (the corner), '6' / '7' weapon 1 / 2 on or off,
   0 none. */
unsigned aw_click(const AwWin *w);

void aw_stance(AwWin *w, int stance);           /* 0 standoff .. 4 disengage, pressed */
void aw_status(AwWin *w, int colour, const char *s);    /* the line at the bottom */
void aw_distance(AwWin *w, int dist);
void aw_ammo(AwWin *w, int slot, int rounds, int off);
void aw_damage(AwWin *w, int craft, int percent);      /* the craft coloured from the top */
/* Weapon slot switched on or off: its range marks and ammunition grey. */
void aw_weapon(AwWin *w, int slot, int range, int off);

/* The UFO at distance dist (flash: white), or gone; erases the last one. */
void aw_ufo(AwWin *w, int dist, int flash, int gone);

/* A shot of weapon slot at distance pos; erase: remove it. */
void aw_shot(AwWin *w, int slot, int pos, int erase);

/* The UFO button: the UFO's picture over the window until a key. The
   caller then redraws its parts (aw_open's picture is restored). */
void aw_preview(AwWin *w, int ufo_sprite);

#endif
