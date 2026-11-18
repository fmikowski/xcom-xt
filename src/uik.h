/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UIK_H
#define UIK_H

/* The original game's look: pictures streamed from UIE.PAK / UIC.PAK
   (tools/ui.py), the original small and big fonts, windows, bevelled
   buttons, lists and message boxes. Positions are screen pixels.

   Nothing stays in memory between screens: pictures go from the file
   straight to video memory, the EGA fonts and the clean copy of the
   screen live in unused EGA video memory (CGA: on the heap while a screen
   is open). */

#include "uipak.h"

#define UK_SMALL    0
#define UK_BIG      1

/* Colours are EGA colours 0-15 (mapped to the CGA palette in use). Text
   gets a shadow one shade darker (bright colours) or black; | UK_FLAT for
   none. */
#define UK_FLAT     0x100

/* Colour themes (uk_theme). */
enum { T_GREEN, T_BLUE, T_GREY, T_PURPLE, T_GOLD, N_THEMES };

typedef struct {
    unsigned char frame[3];     /* window ridge: edge, middle, centre */
    unsigned char title, text, hilite, sel, dim;
    unsigned char btn[5];       /* button: light edge, light, fill, dark, dark edge */
    unsigned char btext, list;  /* button text, list rows */
} UkTheme;

extern const UkTheme *g_uk;     /* current theme */
extern const UkTheme g_cga_themes[N_THEMES];   /* (also Hercules) */
extern int g_uk_ok;             /* the pack was found */

/* Opens the pack and loads the fonts (nested: each uk_open needs a
   uk_close). 0 if the pack is missing: text then uses the BIOS font. */
int uk_open(void);
void uk_close(void);
/* Around a battle: the CGA fonts and copies leave the heap, then return. */
void uk_suspend(void);
void uk_resume(void);
/* Keeps the pack file open between pictures (many small pictures). */
void uk_hold(int on);

void uk_theme(int t);
/* CGA palette 0-3 (0lo 0hi 1lo 1hi); no effect on EGA. */
void uk_cga_palette(int p);

/* Draws pack entry id at (x, y), or where the converter put it if x < 0.
   Full-screen pictures select their CGA palette. 0 if missing. */
int uk_pic(unsigned id, int x, int y);
/* Same, offset by the position stored with it (item pictures: centred in
   a 32 x 48 box at x, y). */
int uk_pic_in(unsigned id, int x, int y);
/* Draws entry id at its own position into the clean copy (uk_save). */
int uk_pic_clean(unsigned id);
/* EGA: keeps the screen rectangle at x, y (x a multiple of 8) in spare
   video memory under key (0x8000 and up, beside the pack's entries) and
   check; uk_recall draws it at x, y while it stays there (1), else 0 (CGA
   always). Large plain pictures stay there the same way by themselves. */
void uk_keep(unsigned key, unsigned check, int x, int y, int w, int h);
int uk_recall(unsigned key, unsigned check, int x, int y);
/* Size of a picture; 0 if missing. */
int uk_pic_box(unsigned id, int *w, int *h);

void uk_fill(int x, int y, int w, int h, int c);
/* Night battles (EGA): g_uk_cmap maps each day colour to the palette
   register that shows it at night (src/batui.c); fills, text and pictures
   are drawn through it. NULL by day and on screens in day colours. */
extern const unsigned char *g_uk_cmap;
void uk_rect(int x, int y, int w, int h, int c);        /* outline */
void uk_window(int x, int y, int w, int h);             /* ridge frame */
void uk_button(int x, int y, int w, int h, const char *label, int pressed);

int uk_text(int x, int y, int font, int c, const char *s);     /* returns the end x */
int uk_width(int font, const char *s);
void uk_center(int x, int w, int y, int font, int c, const char *s);
/* Text cut at width w (with tabs: columns at x + tabs[k]). */
void uk_line(int x, int y, int w, int c, const char *s, const unsigned char *tabs);
/* A column of a uk_line text that is "\x01" and a digit k shows the k-th of
   these NUL-separated strings (a row has 40 characters; a long column text
   such as "Heavy Weapons Platform" next to a long name would be cut). */
extern const char *g_uk_ext;

/* Reads n bytes at offset off of pack entry id (tables); 0 if missing. */
int uk_read(unsigned id, unsigned off, void *dst, unsigned n);

/* A page: background, window, big title; then uk_page_wait shows an OK
   button at the bottom and waits for a key or click (returns the key). */
void uk_page(unsigned bg, int theme, const char *title);
unsigned uk_page_wait(void);

/* The clean screen: uk_save keeps rows y0..y1-1 as they are now, uk_restore
   puts a rectangle of them back (EGA: whole bytes). */
void uk_save(int y0, int y1);
/* Copies a rectangle of the screen (x, w multiples of 8). */
void uk_copy(int sx, int sy, int dx, int dy, int w, int h);
void uk_restore(int x, int y, int w, int h);

/* Full screen: background picture (or black if bg is UK_NOBG), theme. */
#define UK_NOBG     0xFFFF
void uk_screen(unsigned bg, int theme);

/* A message box over the current screen; waits for a key or click and
   puts the screen back. */
void uk_notice(const char *s);
/* Question with Yes / No: 1 = yes (Y, Enter or the Yes button). */
int uk_confirm(const char *s);
/* One line of text input in a box (name: NUL-terminated, max chars). */
int uk_edit(const char *prompt, char *name, int max);

/* Key named in a button spec ("Esc", "Enter", "Tab", "+", a letter). */
unsigned uk_key_named(const char *k, int len);

/* Menus: a list of text rows or a column of buttons, with a window, title,
   column header and buttons along the bottom ("Esc=OK|Enter=Buy one"). */
#define UKS_LIST    0
#define UKS_BUTTONS 1
#define UKS_POPUP   2           /* the picture only inside the window */
#define UKS_CLEAR   4           /* ... and black around it */
#define UKS_KEYS0   8           /* on_key gets the keys of an empty list too */
#define UKS_REPEAT  16          /* a row held with the left button acts again
                                   (Enter), as DOS arrow buttons repeat */
#define UKS_CLICK   32          /* uk_menu: a click on a row acts at once (Enter), also
                                   with an on_key handler (else the first click
                                   selects). ql_menu: a click on a row acts at once
                                   (Enter; with arrows, outside them: Space) */
#define UKS_TOGGLE  64          /* ql_menu: a click on a row is Space (on_key) */
#define UKS_LEFT    128         /* ql_menu: the info line at the list's left, 17 under
                                   ty, its columns at the tabs, in the title's colour;
                                   a row starting  is white,  the title's colour */

typedef struct {
    unsigned bg;
    unsigned char theme, style;
    short wx, wy, ww, wh;       /* window frame (ww = 0: none) */
    short lx, ly, lw, rows, rowh;
    short ty;                   /* title y, big font, centred in the window */
    const char *title;
    const char *header;         /* column titles, tab separated, or NULL */
    const unsigned char *tabs;  /* column offsets, 0 ends */
    const char *buttons;        /* bottom buttons, or NULL */
    short by;                   /* their y */
    void (*extra)(void);        /* draws more of the screen, or NULL */
} UkMenu;

/* Returns the chosen row or -1 (Esc). info: a line under the title (may
   change between calls). on_key handles other keys for the selected row:
   returns 0 nothing, 1 rows changed (redrawn), 2 return sel now. The info
   line is drawn again when its text changed (on_key may change it).
   hotkeys: one letter per row. The screen is drawn again only when another
   menu (or uk_invalidate) came in between. */
typedef int (*UkKey)(int sel, int ch, int scan);
int uk_menu(const UkMenu *m, const char *info, char lines[][41], int n, int sel,
            UkKey on_key, const char *hotkeys);
void uk_invalidate(void);

/* Picks a pressed bottom button or list row for a click (used by screens
   with their own loop). Returns the key of the button under the click, or 0. */
unsigned uk_buttons_key(const char *spec, int x, int w, int y);
extern int g_uk_bh;             /* their height (16) */
void uk_buttons(const char *spec, int x, int w, int y);

/* The text row of the last uk_menu screen that was clicked, -1 = none. */
extern int g_uk_row;

#endif
