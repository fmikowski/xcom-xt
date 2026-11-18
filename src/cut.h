/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUT_H
#define CUT_H

/* Cutscenes as in DOS (OpenXcom CutsceneState): the intro movie at the
   start (VideoState: UFOINTRO/UFOINT.FLI with the GMINTRO tracks) and the
   win and lose slides at the end of the game (SlideshowState: PICTn.LBM
   with their captions, GMWIN / GMLOSE). Converted by tools/intro.py into
   INTROE.PAK / INTROC.PAK and SLIDESE.PAK / SLIDESC.PAK; src/cut.c, an overlay
   section of its own. */

/* Plays the intro unless the command line says otherwise (a quick battle,
   BENCH, a KEYS script without INTRO, NOINTRO); any key or click skips it.
   Also notes whether the end slides play (not under a KEYS script without
   INTRO). */
void cut_intro(int argc, char **argv);

/* 1 if the end slides will play (the pack is there, not a test run). */
int cut_will_end(void);

/* The victory (won) or defeat slides: Esc skips them, another key or a
   click shows the next, each stays 30 s. Returns 0 if they did not play.
   The geoscape's music plays again afterwards. */
int cut_end(int won);

#endif
