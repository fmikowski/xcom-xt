/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SAVES_H
#define SAVES_H

/* The save and load screens of DOS 1.4 (OpenXcom's ListSaveState and
   ListLoadState, drawn as its ListLoadOriginalState shows the DOS ones):
   ten numbered slots with name, time and date; saving types the name in
   the slot's own line. An overlay section of its own (saves), used by the
   geoscape's options (menu.c) and the battlescape's (bsave.c); its
   strings stay in the section. docs/design.md, "Saving in battle". */

/* The slot chosen, or -1 (Cancel, Esc). saving: the name typed in the
   slot's line is left in name (NAME_LEN + 1 bytes, menu.h); else only a
   slot holding a save can be chosen. A right click on a slot holding a
   save deletes it after a question (OpenXcom's DeleteGameState). */
int sv_list(int saving, char *name);

#endif
