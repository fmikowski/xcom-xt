/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HUD_H
#define HUD_H

/* Text on the EGA screen using the BIOS 8x8 font, written straight to the
   bit planes. Rows and columns are 8x8 character cells (40 x 25). */

int hud_init(void);     /* 0 = ok */
void hud_text(int col, int row, int colour, const char *s);
void hud_clear_rows(int row, int count);

/* Pads or cuts the text to width characters. */
void hud_field(int col, int row, int width, int colour, const char *s);

#define HUD_MSG_ROW 22

#endif
