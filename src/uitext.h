/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Game texts from the interface pack. The sentences of the original game
   are not kept in the source: the converter (tools/ui.py) writes them to
   UIE.PAK / UIC.PAK from the player's own GEODATA\ENGLISH.DAT, in tables of
   texts: u16 offsets[n + 1] (the last: the end), then the texts, each
   ending in NUL (UI_REPORT: RT_*, UI_MSG: MSG_*, see uipak.h). Static, so
   that every overlay section that reads texts carries its own copy and
   nothing is added to the resident code. */
#ifndef UITEXT_H
#define UITEXT_H

#include "uik.h"

/* Text k of the table into dst, room for max bytes with the NUL; 0 (and an
   empty string) if the pack lacks it or it does not fit. */
static int ui_text(unsigned table, char *dst, unsigned max, int k)
{
    unsigned short off[2];
    unsigned n;

    dst[0] = 0;
    if (!uk_read(table, k * 2, off, 4) || (n = off[1] - off[0]) < 1 || n > max
        || !uk_read(table, off[0], dst, n)) {
        dst[0] = 0;
        return 0;
    }
    return 1;
}

#endif
