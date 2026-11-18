/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The DOS save and load screens (saves.h).

   DOS 1.4 (OpenXcom's ListLoadOriginalState draws it so): a 320 x 200
   window on BACK01, "Select Save Position" or "Select Game to Load", the
   columns Name, Time and Date, ten slots one under the other, each with
   its number on a small button (1-10) and its name followed by dots;
   CANCEL at the bottom. Saving: a click on a slot (or its digit) puts a
   text cursor in that slot's name line, with its old name (OpenXcom's
   ListSaveState: a used slot keeps its name, an empty one starts empty);
   Enter saves, Esc or the right button leaves the line. Loading: a click
   on a slot that holds a game loads it. DOS has no way to delete a save;
   OpenXcom's right click on a slot (DeleteGameState: "Is it OK to delete
   the saved game?", Yes / No) is kept, as it costs nothing in battle.
   OpenXcom's [NEW SAVED GAME] line, sorting and details line belong to
   its unlimited, named save files; DOS (and this engine) has ten slots.

   The slots are the files XCOMXT0.SAV .. XCOMXT9.SAV: the rows show
   slots 1-9, then slot 0 as "10", so the digit keys keep their slots.

   An overlay section: nothing static (the data group is resident); the
   headers live on the heap while the screen is open. Compiled with -zc,
   tables as string literals (docs/design.md, Research and manufacturing). */

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "input.h"
#include "menu.h"
#include "saves.h"
#include "uik.h"

#define ROWS        N_SLOTS
#define ROW_Y(r)    (34 + (r) * 14)
#define ROW_SLOT(r) (((r) + 1) % N_SLOTS)
#define NAME_X      36
#define NAME_END    192             /* the dots stop before the time */
#define TIME_X      195
#define DATE_X      225
#define BTN_X       120             /* CANCEL, 80 x 16 */
#define BTN_Y       172

#define MONTHS      "JanFebMarAprMayJunJulAugSepOctNovDec"

typedef struct {
    SaveHeader h[N_SLOTS];
    char ok[N_SLOTS];               /* the slot holds a save */
} Slots;

static void read_slots(Slots *s)
{
    char path[13];
    FILE *f;
    int i;

    for (i = 0; i < N_SLOTS; i++) {
        sprintf(path, "XCOMXT%d.SAV", i);
        f = fopen(path, "rb");
        s->ok[i] = 0;
        if (!f)
            continue;
        s->ok[i] = fread(&s->h[i], sizeof s->h[i], 1, f) == 1
                   && !memcmp(s->h[i].magic, "XSG1", 4);
        fclose(f);
        s->h[i].name[NAME_LEN] = 0;
    }
}

/* Row r: its number, the name and dots (or the line being typed: edit),
   the time and the date ("1st  Jan  1999", as GameTime writes them). */
static void draw_row(const Slots *s, int r, const char *edit)
{
    const SaveHeader *h = &s->h[ROW_SLOT(r)];
    int y = ROW_Y(r), x, dw, n;
    char t[80];

    uk_restore(8, y - 2, 304, 13);
    sprintf(t, "%d", r + 1);
    uk_button(10, y - 2, 24, 12, t, edit != NULL);
    if (edit) {
        uk_text(uk_text(NAME_X, y, UK_SMALL, g_uk->hilite, edit), y, UK_SMALL, g_uk->text, "_");
        return;
    }
    x = s->ok[ROW_SLOT(r)] ? uk_text(NAME_X, y, UK_SMALL, g_uk->list, h->name) : NAME_X;
    dw = uk_width(UK_SMALL, ".");
    n = dw > 0 ? (NAME_END - x) / dw : 0;
    if (n > 0) {
        if (n > (int)sizeof t - 1)
            n = sizeof t - 1;
        memset(t, '.', n);
        t[n] = 0;
        uk_text(x, y, UK_SMALL, g_uk->list, t);
    }
    if (!s->ok[ROW_SLOT(r)])
        return;
    sprintf(t, "%d:%02d", h->hour % 24, h->minute % 60);
    uk_text(TIME_X, y, UK_SMALL, g_uk->list, t);
    n = h->day % 10 < 4 && h->day / 10 != 1 ? h->day % 10 : 0;
    sprintf(t, "%d%.2s  %.3s  %d", h->day, "thstndrd" + 2 * n, MONTHS + 3 * (h->month % 12),
            h->year);
    uk_text(DATE_X, y, UK_SMALL, g_uk->list, t);
}

static void draw_rows(const Slots *s)
{
    int r;

    for (r = 0; r < ROWS; r++)
        draw_row(s, r, NULL);
}

static void draw_screen(int saving)
{
    uk_invalidate();                /* uk_menu draws its screen again after this one */
    uk_screen(UI_BG_MENU, T_GREEN);
    uk_window(0, 0, 320, 200);
    uk_center(0, 320, 8, UK_BIG, g_uk->title,
              saving ? "Select Save Position" : "Select Game to Load");
    uk_text(NAME_X, 24, UK_SMALL, g_uk->text, "Name");
    uk_text(TIME_X, 24, UK_SMALL, g_uk->text, "Time");
    uk_text(DATE_X, 24, UK_SMALL, g_uk->text, "Date");
    uk_button(BTN_X, BTN_Y, 80, 16, "CANCEL", 0);
    if (g_video == VIDEO_CGA) {
        /* Four colours: the picture under the rows at half strength, or
           the text would not stand out (as uk_menu's lists). */
        int y, b;
        for (y = 30; y < ROW_Y(ROWS); y++) {
            unsigned char far *d = (unsigned char far *)MK_FP(0xB800, (y & 1) * 0x2000
                                                              + (y >> 1) * 80 + NAME_X / 4);
            unsigned char k = (unsigned char)(y & 1 ? 0x33 : 0xCC);
            for (b = 0; b < (312 - NAME_X) / 4; b++)
                d[b] &= k;
        }
    }
    uk_save(0, 200);
}

/* The row under a click, or -1. */
static int row_at(int x, int y)
{
    int r = (y - 32) / 14;

    if (x < 8 || x >= 312 || y < 32 || r >= ROWS)
        return -1;
    return r;
}

int sv_list(int saving, char *name)
{
    Slots *s;
    char path[13];
    int edit = -1, sel = -1, len = 0, r;

    s = malloc(sizeof *s);
    if (!s)
        return -1;
    read_slots(s);
    uk_open();
    draw_screen(saving);
    draw_rows(s);
    for (;;) {
        unsigned key = input_key();
        int ch = key & 0xFF;

        r = -1;
        if (key == KEY_LCLICK) {
            if (g_mouse_x >= BTN_X && g_mouse_x < BTN_X + 80 && g_mouse_y >= BTN_Y
                && g_mouse_y < BTN_Y + 16)
                break;                          /* CANCEL */
            r = row_at(g_mouse_x, g_mouse_y);
            if (r < 0)
                continue;
        } else if (key == KEY_RCLICK) {
            r = row_at(g_mouse_x, g_mouse_y);
            if (edit >= 0) {
                /* Leaves the line (ListSaveState), the slot as it was. */
                draw_row(s, edit, NULL);
                edit = -1;
                continue;
            }
            if (r < 0 || !s->ok[ROW_SLOT(r)])
                break;
            /* DeleteGameState (OpenXcom): Yes deletes the slot's file. */
            if (uk_confirm("Is it OK to delete the saved game?")) {
                int ok;
                sprintf(path, "XCOMXT%d.SAV", ROW_SLOT(r));
                ok = !remove(path);
                log_printf("delete slot %d: %s", ROW_SLOT(r), ok ? "ok" : "failed");
                if (ok)
                    s->ok[ROW_SLOT(r)] = 0;
                else
                    uk_notice("Could not delete the saved game!");
            }
            draw_rows(s);
            continue;
        } else if (edit >= 0) {
            /* Typing in the slot's line. */
            if (ch == 27) {
                draw_row(s, edit, NULL);
                edit = -1;
                continue;
            }
            if (ch == 13) {
                sel = ROW_SLOT(edit);
                break;
            }
            if (ch == 8 && len > 0)
                name[--len] = 0;
            else if (ch >= ' ' && ch < 127 && len < NAME_LEN) {
                name[len++] = (char)ch;
                name[len] = 0;
            } else
                continue;
            draw_row(s, edit, name);
            continue;
        } else if (ch == 27)
            break;
        else if (ch >= '0' && ch <= '9')
            r = ch == '0' ? ROWS - 1 : ch - '1';
        if (r < 0)
            continue;
        if (!saving) {
            if (s->ok[ROW_SLOT(r)]) {
                sel = ROW_SLOT(r);
                break;
            }
            continue;
        }
        /* The line of slot r takes the name. */
        if (edit >= 0)
            draw_row(s, edit, NULL);
        edit = r;
        strcpy(name, s->ok[ROW_SLOT(r)] ? s->h[ROW_SLOT(r)].name : "");
        len = strlen(name);
        draw_row(s, edit, name);
    }
    uk_close();
    free(s);
    return sel;
}
