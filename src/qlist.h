/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef QLIST_H
#define QLIST_H

#include "uik.h"

/* Lists of quantities and long lists (qlist.c, a geoscape overlay section
   next to the base screens): as uk_menu, plus

   - a scroll bar on the right when the list is longer than its window:
     an arrow above and below (held = repeats) and a thumb; a click on the
     bar above or below the thumb scrolls a page. The mouse wheel does not
     exist on the XT's mice. Scrolling does not move the selection;
   - with on_step: three buttons each way on every row, like a tape
     recorder's: one triangle = 1, two = 10, three = all (all available,
     all that fit, or back to none), at fixed pixel columns
     (QL_DEC_X..., the quantity belongs at QL_QTY_X, i.e. tab QL_QTY_X -
     m->lx - 2 of the row's text). The keys: Enter, Right and + = 1 more,
     Left and - = 1 less, . and , = 10, End = all, Home = none.

   on_step(row, delta): delta is +-1, +-10 or +-QL_ALL; returns 0 nothing,
   1 the row's text changed (drawn again), 3 every row's text changed, 2
   leave (the list returns row), 4 the rows changed and the selection
   goes to g_uk_row (a row moved). on_key as in uk_menu (for the other keys;
   its 2 also returns the row: an OK key). Esc and the right button return
   -1. The info line is drawn again when its text changes.

   The window, picture and buttons are drawn here as uk_menu does (once a
   call: call again only when the rows change); uk_invalidate is called on
   return. */
#define QL_ALL      30000

#define QL_DEC_X    190         /* three, two, one triangle pointing left */
#define QL_QTY_X    232         /* the quantity between the buttons */
#define QL_INC_X    258         /* one, two, three triangles pointing right */
#define QL_BAR_X    304         /* scroll bar */
#define QL_QTY_TAB  (QL_QTY_X - 10)     /* tab of the quantity column (lx 8) */

/* The arrow columns move left by this many pixels (a multiple of 8, 0 at
   the start; QL_QTY_TAB moves with them): Manage Alien Containment keeps
   a column to their right. Set before ql_menu, put back after. */
extern int g_ql_shift;

typedef int (*QlStep)(int row, int delta);

int ql_menu(const UkMenu *m, const char *info, char lines[][41], int n, int sel,
            UkKey on_key, QlStep on_step);

#endif
