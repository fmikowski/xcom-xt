/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UIPAK_H
#define UIPAK_H

/* Entries of the interface pack UIE.PAK / UIC.PAK (tools/ui.py reads these
   numbers from this file). The pack: "XUI1", u16 count, u16 0, then
   u32 offsets[count] (0 = missing), then the entries.

   Picture entry: u16 x, y, w, h (pixels; x and w multiples of 8 on EGA,
   of 4 on CGA), u8 flags, u8 0, then the pixels:
     EGA plain   plane 0 rows, plane 1 rows, plane 2 rows, plane 3 rows
     EGA masked  per row, per byte column: mask (1 = drawn), plane 0..3
     CGA plain   rows of w / 4 bytes
     CGA masked  per row, per byte: mask (11 = drawn), data
   Font entry: u8 height, u8 bytes per row, u8 widths[96] (characters
   32-127), then per character the body rows and the shadow rows.
   Pedia table: per UFOPAED.PAK entry 16 bytes: u8 layout, u8 stats x / 2,
   u16 picture, u16 text x, y, width, u16 stats y (0: stats above the
   text), u16 second picture, u8 size, u8 stats width / 2.
   UI_BIGOB and UI_ICON have room for N_ITEMS entries each (src/items.h;
   checked by tools/ui.py and src/invscr.c): new items move what follows. */

#define UIF_MASKED      0x01
#define UIF_CGAPAL      0x06    /* CGA palette: (flags >> 1) & 3 = 0lo 0hi 1lo 1hi */
#define UIF_SCREEN      0x08    /* full screen: selects its CGA palette */

#define UI_FONT_SMALL   0
#define UI_FONT_BIG     1
#define UI_BG_MENU      2       /* BACK01, brown: menus, UFOpaedia lists, debriefing */
#define UI_BG_PSI       3       /* BACK01, violet */
#define UI_BG_RESEARCH  4       /* BACK05 */
#define UI_BG_MANUF     5       /* BACK17 */
#define UI_BG_STORES    6       /* BACK13: buy, sell, stores, hire, funding */
#define UI_BG_REPORT    7       /* BACK13, pink: month report */
#define UI_BG_EQUIP     8       /* BACK04 */
#define UI_BG_HANGAR    9       /* BACK14 */
#define UI_BG_SOLDIERS  10      /* BACK02 */
#define UI_BG_PEDIA_ITEM 11     /* BACK08 */
#define UI_BG_PEDIA_TEXT 12     /* BACK10 */
#define UI_BG_PEDIA_UFO 13      /* BACK11 */
#define UI_BG_BRIEFING  14      /* BACK16 */
#define UI_BG_ABORT     15      /* TAC00 */
#define UI_GEOBORD      16      /* geoscape button panel, x 256-319 */
#define UI_ICONS        17      /* battlescape panel, rows 144-199 */
#define UI_TAC01        18      /* inventory buttons */
#define UI_BASE_EARTH   19      /* base view: empty grid square, 32x32 */
#define UI_RANK         20      /* + rank 0-5: insignia 26x23 */
#define UI_DOLL         26      /* + 0-17: inventory paper dolls (see ui.py DOLLS) */
#define UI_BASEBITS     44      /* + frame 0-39: base view sprites, 32x32 masked */
#define UI_BIGOB        84      /* + item (0 .. N_ITEMS - 1): hand picture, masked */
#define UI_ICON         191     /* + item: 16x16 picture for the backpack, masked */
#define UI_UP           298     /* + n - 1: UP0nn.SPK UFOpaedia pictures (1-42) */
#define UI_PEDIA_MAP    344     /* table: UFOpaedia page layouts */
#define UI_RESERVE      345     /* + 0-3: reserve none / snap / aimed / auto pressed */
#define UI_SCANBORD     349     /* minimap frame (SCANBORD.PCK) */
#define UI_DF_WINDOW    350     /* air combat window (INTERWIN.DAT), 160x96 at 80,52 */
#define UI_DF_PRESSED   351     /* + 0-4: stance buttons pressed (standoff .. disengage) */
#define UI_DF_WEAPON    356     /* + slot x 6 + CW_*: craft weapon icons (INTICON.PCK) */
#define UI_DF_CRAFT     368     /* + CT_* x 5 + 0-4: the craft, damage in quarters */
#define UI_UFO_TOP      388     /* UFO pictures (INTERWIN.DAT): top strip 160x15 */
#define UI_UFO_BOTTOM   389     /* bottom strip 160x29 */
#define UI_UFO_MID      390     /* + UFO sprite 0-7: the UFO, 160x52 */
#define UI_BG_PEDIA_FACIL 398   /* BACK09 */
#define UI_BG_BRIEF_TERROR 399  /* BACK16, terror palette */
#define UI_BG_BRIEF_MARS 400    /* BACK16, Mars palette */
#define UI_BG_BRIEF_FINAL 401   /* BACK01, Mars palette */
#define UI_BRIEFING     402     /* table: mission briefings (see BRIEF_*) */
#define UI_BG_INTERCEPT 403     /* BACK12, aqua: interception, craft, destination */
#define UI_BG_UFOINFO   404     /* BACK15, dark blue: UFO, targets under a click */
#define UI_HWP          405     /* + 0-4: HWP pictures (IT_TANK_CANNON ..), as UI_BIGOB */
#define UI_BG_TERROR    410     /* BACK03, pink: the terror site pop-up */
#define UI_GRAPHS       411     /* the Graphs screen's 7 icon buttons (GRAPHS.SPK), 224x24 at 96,0 */
#define UI_BODY         412     /* + body (inv.h BODY_*, 36): its corpse's hand picture, as
                                   UI_BIGOB (none for the 2x2 units' bodies) */
#define UI_BG_FACIL     448     /* BACK05, oxide: Build Facilities (Installation) */
#define UI_BG_PLACE     449     /* BACK01, oxide: the facility being placed */
#define UI_BG_BASEINFO  450     /* BACK07: Base Information */
#define UI_DF_SKYRANGER 451     /* + 0-4: the Skyranger as UI_DF_CRAFT's (the craft screen) */
#define UI_BG_SOLDIER_INFO 456  /* BACK06: the soldier's screen (SoldierInfoState) */
#define UI_SOLDIER_RANK 457     /* + rank 0-5: SMOKE.PCK insignia 26x23 at 4, 4, masked, base colours */
#define UI_FIGURE       463     /* + type x 4 + n % 4: the craft screen's figures (BASEBITS 38 soldier,
                                   40 HWP, 39 equipment, 11x18, masked) as drawn for the n-th of a row
                                   that starts at x 85, y 96 (tools/ui.py): the next four are 40
                                   pixels on (uk_pic_in with that offset; y + 25 for the second row) */
#define UI_REPORT       475     /* table: the month report's texts (see RT_*) */
#define UI_MSG          476     /* table: messages and window texts (see MSG_*) */
#define UI_COUNT        477

/* UFOpaedia page layouts (UI_PEDIA_MAP). */
#define PL_TEXT         0       /* BACK10, text below the title */
#define PL_PICTURE      1       /* full picture, text in its box */
#define PL_ITEM         2       /* BACK08 with the item's picture */
#define PL_UFO          3       /* BACK11 with the UFO (pic = UI_UFO_MID + sprite) */
#define PL_FACIL        4       /* BACK09 with the facility: pic = the BASEBITS
                                   shape, pic2 = its building (size 1), size */

/* Briefing table (UI_BRIEFING): u16 offsets[BRIEF_N + 1] (the last: the
   end), then per mission u16 background, s8 text offset, title NUL, text
   NUL. */
#define BRIEF_CRASH     0
#define BRIEF_LANDED    1
#define BRIEF_TERROR    2
#define BRIEF_ALIEN_BASE 3
#define BRIEF_BASE      4
#define BRIEF_MARS      5
#define BRIEF_FINAL     6
#define BRIEF_N         7

/* Month report texts (UI_REPORT, tools/ui.py report_table): u16 offsets[RT_N + 1]
   (the last: the end), then the texts, each ending in NUL. The ones about
   countries come without the names: "<names> <text>". */
#define RT_TITLE        0
#define RT_PLEASED      1       /* the council: very pleased */
#define RT_SATISFIED    2
#define RT_DISSATISFIED 3
#define RT_TERMINATED   4       /* the project is over */
#define RT_DEBT         5       /* reduce your debts */
#define RT_UP_ONE       6       /* one country: funding up (pleased) */
#define RT_UP_MANY      7
#define RT_DOWN_ONE     8       /* unhappy, funding down */
#define RT_DOWN_MANY    9
#define RT_PACT_ONE     10      /* secret pact */
#define RT_PACT_MANY    11
#define RT_N            12

/* Message texts (UI_MSG, tools/ui.py msg_table; same layout as UI_REPORT).
   Line breaks are NL (LF) characters. */
#define MSG_NO_DISMANTLE 0      /* the facility cannot be dismantled: linked to the lift */
#define MSG_NO_BUILD    1       /* cannot build here: next to a facility */
#define MSG_CYDONIA     2       /* send the craft to Cydonia? (3 lines) */
#define MSG_LOW_FUEL    3       /* is low on fuel, returning to base (2 lines) */
#define MSG_LAND_NEAR   4       /* ready to land near (2 lines, between craft and place) */
#define MSG_BASE_FOUND  5       /* X-Com agents have located an alien base in (2 lines) */
#define MSG_NO_EQUIP    6       /* not enough equipment to fully re-equip squad (2 lines) */
#define MSG_CONTAIN     7       /* containment full: the aliens that do not fit die (ours) */
#define MSG_N           8

#endif
