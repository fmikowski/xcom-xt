/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TIMER_H
#define TIMER_H

/* PIT-based timer: 1,193,182 counts per second, wraps after ~1 hour. */

#define TIMER_HOOK_HZ 73        /* 1193182 / 16384 = 72.8 */

void timer_init(void);          /* switches PIT channel 0 to mode 2 */
void timer_shutdown(void);      /* restores mode 3 (and removes the hook) */
unsigned long timer_read(void);
unsigned long timer_ms(unsigned long counts);

/* Waits until ms milliseconds have passed since t0 (a timer_read value):
   the battle's pacing (src/battle.c), a least time that the work in
   between counts towards. With the PACELOG option (g_pace_log) and what
   not 0: "PACE <what> <ms> <took ms>" in the LOG (w a step, t a turn, f a
   round's flight, h its hit). */
void timer_pace(unsigned long t0, unsigned ms, int what);
extern int g_pace_log;

/* Calls fn from the timer interrupt TIMER_HOOK_HZ times a second (own
   stack, interrupts on, never re-entered); NULL removes it. */
void timer_hook(void (*fn)(void));

#endif
