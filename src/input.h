/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INPUT_H
#define INPUT_H

#include <stdio.h>

/* Keyboard and mouse input, optionally fed from a script file for testing,
   and a simple append-only log. */

/* Script tokens are separated by spaces or new lines: U D L R HOME END PGUP
   PGDN TAB ENTER SPACE ESC PLUS MINUS BKSP F1, a single letter, or 'x for the letter x
   (needed for U, D, L, R, which name arrows); "TOKEN*n" repeats.
   CLICK x y / RCLICK x y: left / right mouse click at screen pixel (x, y);
   HOLD x y n: a left click there, the button then held for n repeats;
   MOVE x y puts the pointer there without clicking; WAIT n pauses n tenths
   of a second (the pointer is shown meanwhile).
   When the script runs out, input falls back to the keyboard and mouse. */
int input_script(const char *path);
extern FILE *g_script;          /* the open script, NULL when none or ended */

/* Script token AUTO: the geoscape's pop-ups (src/geowin.c) take their
   passive answer by themselves (Cancel, OK, landing Yes), as the message
   lines did before them; for scripts that test something else. The
   inventory before a campaign battle closes by itself too. Off again
   when the script ends or at the token NOAUTO. */
extern int g_popup_auto;
/* Script tokens AUTO and OLDCLOCK: an alert (a pop-up) stops the clock until
   a speed is picked, as the geoscape did before it followed DOS (GeoscapeState
   breaks the step only; the scripts were written for the old stop); DOSCLOCK
   turns it off (AUTO sets it, so that the old scripts run as written). Off
   when the script ends. */
extern int g_alert_pause;

/* 1 if input_key would return without waiting. */
int input_ready(void);

/* The DOS arrow buttons repeat while the left button is held (OpenXcom's
   250 ms, then every 50 ms): 1 if it is still down after ms milliseconds,
   0 once it is released. Scripts: HOLD x y n clicks at (x, y) and holds the
   button for n repeats. */
int input_held(unsigned ms);

/* Returns scan code << 8 | ASCII, like BIOS INT 16h, or a mouse click:
   KEY_LCLICK / KEY_RCLICK with the position in g_mouse_x, g_mouse_y. */
unsigned input_key(void);

#define KEY_LCLICK  0xF000
#define KEY_RCLICK  0xF100
#define IS_CLICK(k) (((k) & 0xFE00) == 0xF000)
/* While g_track is set (the inventory holding an item), a script's MOVE x y
   returns KEY_MOVE (logged as MOVE x y) instead of only moving the pointer. */
#define KEY_MOVE    0xF200
extern int g_track;

/* Mouse (mouse.c): INT 33h driver, pointer drawn by the engine. Without a
   driver everything works from the keyboard. */
extern int g_mouse;                     /* 1 = driver found */
extern int g_mouse_x, g_mouse_y;        /* pixels, 0-319 / 0-199 */

int mouse_init(void);                   /* after gfx_init; 1 = mouse found */
void mouse_shutdown(void);
void mouse_set(int x, int y);           /* moves the pointer */
unsigned mouse_poll(void);              /* draws the pointer; a new click or 0 */
int mouse_down(void);                   /* the left button is down now */
void mouse_hide(void);

/* Call before writing video memory in the pixel rectangle [x0,x1) x [y0,y1):
   hides the pointer if it lies there (it comes back at the next poll). */
void mouse_hide_rect(int x0, int y0, int x1, int y1);
#ifdef GAPDIAG
void gap_mark(int what);                /* XT_RDDIAG=gap builds (mouse.c) */
#endif

/* Busy pointer (mouse.c): long calculations and loads call busy_tick
   often (at least every few tenths of a second, never in the middle of
   drawing). Half a second after the last wait for input, the pointer
   becomes an hourglass that turns over every 0.27 s (without a mouse it
   stands in the top right corner) and stays until the game waits for
   input again (mouse_poll, input_key: busy_idle). Cheap when not due: a
   look at the BIOS clock. busy_show: the hourglass at once (before work
   known to be long, such as a battle's setup). */
void busy_tick(void);
void busy_show(void);
void busy_idle(void);

extern char g_log_path[];        /* the LOG file, "" when none */
void log_open(const char *path);
void log_printf(const char *fmt, ...);

/* File reads through DOS so far and the bytes asked for (src/rdcount.asm):
   in battles the overlay sections loaded from disk. rd_count_start hooks
   INT 21h, rd_count_stop unhooks it (atexit). */
void __cdecl rd_count_start(void);
void __cdecl rd_count_stop(void);
/* The mode folder's letter (C, E, V; 0: none): read-only opens try <x>GA\name first
   (rdcount.asm; set by startup.c, docs/design.md "Mode folders"). */
extern char __far rd_dir;
unsigned __cdecl rd_count(void);
unsigned long __cdecl rd_bytes(void);
#ifdef OVLWATCH
/* Overlay stress builds (build.ps1 -OvlArea): how often the overlay
   manager re-applied a moved section's relocations, and the last section
   (src/rdcount.asm); input_key logs each new one. */
unsigned __cdecl ovl_redo_count(void);
unsigned __cdecl ovl_redo_section(void);
#endif
#ifdef RDDIAG
unsigned __cdecl rd_secload(int i);
unsigned long __cdecl rd_sectime(int i);    /* PIT counts (umb branch) */
unsigned long __cdecl rd_readtime(void);
unsigned __cdecl rd_secseg(int i, int w);
unsigned __cdecl rd_opens(void);
char __far * __cdecl rd_lastname(void);
#endif
/* Where the battle's count starts (play_mission, before the setup): the
   geoscape's reads before it are not the setup's. */
extern unsigned g_rd_mark;
extern unsigned long g_rd_mark_bytes;

#endif
