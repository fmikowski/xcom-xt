/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SOUND_H
#define SOUND_H

/* Sound effects: digitised samples from SOUNDS.PAK on a Sound Blaster
   (8-bit DMA, one effect at a time, IRQ driven), otherwise short blocking
   PC speaker effects (PIT channel 2). Music: see music.h. */

extern int g_sound_on;          /* S key in battle: effects and music */

/* Effects, in the order of tools/sound.py EFFECTS. */
enum {
    SFX_RIFLE, SFX_CANNON, SFX_LASER, SFX_PLASMA, SFX_HIT_BULLET, SFX_HIT_BEAM,
    SFX_EXPLOSION, SFX_DIE_SECTOID, SFX_DIE_SNAKEMAN, SFX_DIE_MUTON,
    SFX_DIE_ETHEREAL, SFX_DIE_MALE, SFX_DIE_FEMALE, SFX_DOOR, SFX_STEP,
    SFX_THROW, SFX_GEO_POPUP, SFX_CRAFT_CANNON, SFX_CRAFT_MISSILE,
    SFX_UFO_FIRE, SFX_UFO_HIT, SFX_UFO_CRASH, SFX_CRAFT_EXPLODE,
    N_SFX
};

#define SND_NO_SB       1       /* snd_init flags: PC speaker only */
#define SND_NO_MUSIC    2
#define SND_QUIET       4       /* no sound at all */
#define SND_NO_FMSFX    8       /* no AdLib FM effects (PC speaker) */

void snd_init(int flags);       /* after timer_init; logs what it found */
void snd_shutdown(void);        /* before timer_shutdown */

/* Upper memory blocks: startup.c umb_plan finds them (the sound's UMB, its
   top the game's when umb_game; the game's other UMBs), snd_shutdown frees
   them. */
#define UMB_MORE        3
extern unsigned umb_seg, umb_paras, umb_more[UMB_MORE];
extern int g_hma_track;                /* the battle track in the HMA (xmem.c), -1 none */
extern unsigned g_hma_lo;
extern unsigned char umb_game, umb_xms, n_more, xms_entry_ok;

/* Effect sets: the samples one screen needs, loaded within the free memory
   (call after the screen's own data is loaded). */
void snd_set_geo(void);
void snd_dogfight(int on);      /* 1: a dogfight is on (interception track) */
void snd_set_battle(int race);  /* race pack letter: S F N M E X */
void snd_free(void);            /* frees effects and music */
void snd_result(int res);       /* after a battle (1 won): its tune, once */

void snd_play(int sfx);         /* sample if loaded, else a speaker effect */

/* Game events. */
void snd_fire(int weapon);      /* weapon item (IT_*) */
void snd_shot(int alien);       /* weapon unknown: rifle or plasma */
void snd_hit(void);             /* impact of the last shot / geoscape alert */
void snd_kill(int alien);       /* death scream */
void snd_death(void);
void snd_explode(void);
void snd_door(void);
void snd_step(void);
void snd_off(void);             /* PC speaker off */

/* Counters for the LOG option. */
extern unsigned g_sb_port, g_sb_irq, g_sb_dma, g_sb_dsp;
extern volatile unsigned g_sb_irqs, g_sb_started;

#endif
