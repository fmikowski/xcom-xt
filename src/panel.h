/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PANEL_H
#define PANEL_H

/* The battlescape control panel of the DOS game (UFOGRAPH/ICONS.PCK) in
   rows 144-199: buttons, the selected soldier's name, rank, time units,
   energy, health and morale, the items in the hands, messages, and the
   visible-alien markers above it. The picture is streamed from the UI pack
   (EGA keeps a clean copy in spare video memory, CGA 4.5 KB of heap). */

void panel_open(void);
void panel_close(void);

/* Soldier sel (-1 none) and the view level; draws what changed. */
void panel_status(int sel, int level);

/* The visible-alien markers for unit sel (-1: none, the aliens' turn). */
void panel_marks(int sel);

/* EGA: the view moved on the screen by dc cells, dr rows (render_scroll). */
void panel_shift(int dc, int dr);

/* Small text at the right of the name line (cursor: walk cost, items). */
void panel_cursor(const char *s);

/* A message in the box over the lower panel, until panel_unmessage
   (the next key) brings the soldier's figures back. */
void panel_message(int colour, const char *s);
void panel_unmessage(void);

/* The reserve-TU button shown pressed: 0 none, 1 snap, 2 aimed, 3 auto. */
void panel_reserve(int mode);

/* A click on the panel or an alien marker: the key it stands for, a PK_
   code, or 0 if none; alien markers set *alien to the unit. */
#define PK_LHAND    0x101       /* the hands: action popup */
#define PK_RHAND    0x102
#define PK_MAP      0x103       /* minimap */
#define PK_LAYERS   0x104       /* all levels / up to the view level */
#define PK_OPTIONS  0x105
#define PK_ABORT    0x106       /* the abort button (no DOS key here: F2) */
#define PK_UNIT_UP  0x107       /* the unit up / down buttons (no DOS key) */
#define PK_UNIT_DOWN 0x108
#define PK_RESERVE  0x110       /* + 0-3: reserve none / snap / aimed / auto */
unsigned panel_click(int *alien);

/* The actions of a hand (the DOS popup over the map): returns the key of
   the chosen action, 0 if cancelled. The caller redraws the map. */
unsigned panel_actions(int sel, int right);

#endif
