/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSIC_H
#define MUSIC_H

/* AdLib (OPL2) music: the original ADLIB.CAT tracks (MUSIC.PAK), played by
   a port of the X-COM AdLib driver (OpenXcom's adlplayer, by Volutar) from
   the timer interrupt. */

enum { MUS_GEO1, MUS_GEO2, MUS_TACTIC, MUS_INTER, MUS_WIN, MUS_LOSE, MUS_MARS,
       MUS_INTRO1, MUS_INTRO2, MUS_INTRO3, N_MUS };

extern int g_opl;               /* OPL2 found at 388h */

int mus_init(int music);        /* detects the OPL, reads MUSIC.PAK's index
                                   (music 0: the OPL for FM effects only) */
void mus_fx(int sfx);           /* FM sound effect (SFX_*), played by the timer hook */
void mus_shutdown(void);
unsigned mus_size(int track);   /* bytes, 0 = not available */
int mus_play(int track, int loop);  /* loads the track; 0 = playing */
/* The same into buf (len mus_size bytes, e.g. in an upper memory block);
   buf NULL = a hole in the C heap, else a DOS block at the top of memory. The caller keeps buf until mus_stop. */
int mus_play_at(int track, int loop, unsigned char *buf);
void mus_stop(void);            /* silences and frees the track */

/* Load measurement for the LOG option: ticks run, PIT counts spent. */
extern unsigned long g_mus_ticks, g_mus_counts;
extern unsigned g_mus_max;

#endif
