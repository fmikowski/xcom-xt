/* SPDX-License-Identifier: GPL-3.0-or-later */
/* xcom-xt: X-COM battlescape for XT-class PCs with EGA. */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <bios.h>
#include <dos.h>
#include <i86.h>
#include "battle.h"
#include "batui.h"
#include "brief.h"
#include "base.h"
#include "canvas.h"
#include "bsave.h"
#include "craft.h"
#include "cut.h"
#include "effects.h"
#include "geo.h"
#include "alien.h"
#include "gfx.h"
#include "hud.h"
#include "hwp.h"
#include "input.h"
#include "inv.h"
#include "los.h"
#include "map.h"
#include "menu.h"
#include "pak.h"
#include "path.h"
#include "render.h"
#include "sound.h"
#include "timer.h"
#include "uik.h"
#include "unit.h"
#include "vgageo.h"

int g_quick_squad = 8;         /* quick battles' soldiers (SQUAD) */
#define ALIEN_COUNT 3

static unsigned char *seen;

/* Largest free DOS memory block in paragraphs (dos_free_kb: in KB). */
static unsigned dos_free_para(void)
{
    union REGS r;

    r.h.ah = 0x48;
    r.w.bx = 0xFFFF;
    int86(0x21, &r, &r);
    return r.w.bx;
}
#define dos_free_kb() (dos_free_para() / 64)

/* INT 21h back as it was (atexit). */
static void rd_stop(void)
{
    rd_count_stop();
}

/* All free DOS memory in KB (the MCB chain): heap holes count too. */
static unsigned dos_total_kb(void)
{
    union REGS r;
    struct SREGS s;
    unsigned seg, total = 0;

    segread(&s);
    r.h.ah = 0x52;
    int86x(0x21, &r, &r, &s);
    seg = *(unsigned far *)MK_FP(s.es, r.w.bx - 2);
    for (;;) {
        const unsigned char far *mcb = (const unsigned char far *)MK_FP(seg, 0);
        unsigned size = *(const unsigned far *)(mcb + 3);
        if (mcb[0] != 'M' && mcb[0] != 'Z')
            break;
        if (!*(const unsigned far *)(mcb + 1))
            total += size;
        if (mcb[0] == 'Z')
            break;
        seg += size + 1;
    }
    return total / 64;
}

/* The command line (startup.c); returns 1 for a quick battle. */
int main_options(int argc, char **argv, const char **forced, const char **forced_race,
                 int *want_cga, int *snd_flags);
void lim_init(int argc, char **argv);

/* BENCH: src/vbench.c (an overlay section of its own). */
void bench_run(unsigned char *seen);

/* The quick battles' mission packs (TTnV.PAK) and race packs (RxV.PAK) for
   the video mode: startup.c find_packs (the startup section) lists them. */
char pack_names[64][13], race_names[8][13];
int n_packs, n_races;

unsigned long g_seed;
#if defined(RDDIAG) || defined(RDTIME)
static unsigned long g_rd_t0;
#endif
int g_aliens = 99, g_bench;

unsigned g_tile_loot[10];
unsigned char g_xbase_had[5], g_xbase_now[5];

/* Sets up and fights one battle. Returns the battle result (1 won, -1 lost,
   -2 aborted), or -9 on a setup error (message in *err). */
static int play_mission(const char *pack_name, const char *race_name, int *kills, int *lost,
                        const char **err)
{
    int i, res;

    /* The setup's reads count from here (battle.c, "T1: setup"); what the
       geoscape read since the last battle is not the setup's. */
    log_printf("reads before the battle: %u, %lu KB", rd_count() - g_rd_mark,
               (rd_bytes() - g_rd_mark_bytes + 512) / 1024);
#if defined(RDDIAG) || defined(RDTIME)
    log_printf("RD ticks since start-up: %lu", *(unsigned long far *)MK_FP(0x40, 0x6C) - g_rd_t0);
#endif
    g_rd_mark = rd_count();
    g_rd_mark_bytes = rd_bytes();
    busy_show();                /* seconds of loading: the busy pointer (input.h) */
    rng_seed(g_seed);
    snd_free();                 /* geoscape sounds make room for the battle */
    if (g_bs_resume) {
        /* A saved battle (src/bsave.c): its packs, map, units and state
           instead of the setup. */
        *err = bs_restore(seen);
        race_name = g_pack_names[1];
        pack_name = g_pack_names[0];
        if (*err)
            g_bs_resume = 0;
        goto set_up;
    }
    memset(g_pack_names, 0, sizeof g_pack_names);
    strncpy(g_pack_names[0], pack_name, 12);
    strncpy(g_pack_names[1], race_name, 12);
    *err = pak_load(pack_name);
    g_bctx.craft = g_pak.craft;         /* a battle save lands the same craft */
    if (!*err && g_pak.craft != CM_SKYRANGER)
        log_printf("craft map %s, %d seats", g_pak.craft == CM_AVENGER ? "Avenger" : "Lightning",
                   g_pak.n_deploy);
    if (!*err && g_pak.video != g_video + (g_vga != 0))       /* VGA packs: 2 (gfx.h) */
        *err = "wrong video format";
    if (!*err)
        *err = race_load(race_name);
    if (!*err) {
        /* Terror units (TxV.PAK) only when the crew list calls for ranks
           the race pack has none for: terror sites, Cydonia. */
        int s;
        for (s = 0; s < g_pak.n_spawn; s++)
            if (g_pak.spawn[s].rank < 8 && !g_pak.race[g_pak.spawn[s].rank].valid)
                break;
        if (s < g_pak.n_spawn) {
            char name[13];
            strcpy(name, race_name);
            name[0] = 'T';
            if (terror_load(name))
                log_printf("no terror units: %s", name);
            else
                strcpy(g_pack_names[2], name);
        }
    }
    if (!*err)
        log_printf("packs loaded: free %u KB (%u KB in all)", dos_free_kb(), dos_total_kb());
    if (!*err) {
        /* Cities differ a lot in how many building sprites they need: if
           this one would not leave room for the battle, try another layout
           (the path, sight and map tables are taken at start-up). */
        unsigned long room, want, suits = pic_armours();
        int tries;
        for (tries = 0; ; tries++) {
            if (map_generate()) {
                *err = "map does not fit";
                break;
            }
            room = (unsigned long)dos_free_kb() * 1024;
            want = pic_want(suits);     /* (with a picture store: the floors) */
            if (want <= room || tries == 7 || g_pak.script != SCRIPT_URBAN)
                break;
            log_printf("map %d needs %lu KB, %lu KB free: another layout", tries,
                       want / 1024, room / 1024);
        }
    }
    if (!*err && g_brief_crashed && g_pak.script == SCRIPT_CRASH)
        map_crash_power();
    if (!*err && g_base_layout)
        map_base_modules(g_xbase_had);
    /* Which pictures fit, and the armours' and HWPs' (picplan.c, hwp.c). */
    if (!*err)
        *err = pic_plan(dos_free_para());
    if (!*err && g_hwp_n && g_pic_early)
        hwp_load();
    if (!*err)
        *err = pak_load_sprites();
    if (!*err)
        log_printf("map and sprites: free %u KB (%u KB in all)", dos_free_kb(), dos_total_kb());
    if (!*err) {
        pak_free_blocks();
        *err = pak_vox();       /* into the room the blocks left */
    }
    if (!*err && g_hwp_n && !g_pic_early)
        hwp_load();
    if (!*err)
        *err = sc_start();
    if (!*err && (path_init() || los_init()))
        *err = "out of memory";
    if (!*err)
        map_reveal();
    if (!*err && units_spawn(g_roster ? g_squad_max : g_quick_squad, g_aliens))
        *err = "no room for the squad";
    if (!*err) {
        int n = 0;
        for (i = 0; i < g_nunits; i++)
            n += g_units[i].faction == FACTION_XCOM && !UNIT_HWP(&g_units[i]);
        log_printf("squad %d soldiers", n);
    }
set_up:
    if (*err) {
        log_printf("setup of %s failed: %s (free %u KB)", pack_name, *err, dos_free_kb());
        return -9;
    }
    floor_load();               /* pictures of what will lie on the ground */
    if (g_herc)
        hgc_sprites();          /* Hercules: terrain and units recoloured (hgcinit.c) */
    if (!g_bench)
        snd_set_battle(race_name[1]);
    log_printf("seed %lu pack %s (%s) race %s", g_seed, pack_name, g_pak.title, race_name);
    log_printf("craft at %d,%d  ufo at %d,%d  free memory %u KB (%u KB in all, %u paragraphs)",
               g_craft_x, g_craft_y, g_ufo_x, g_ufo_y, dos_free_kb(), dos_total_kb(), dos_free_para());
#if defined(RDDIAG) || defined(RDTIME)
    {
        union REGS r;
        r.h.ah = 0x48;
        r.w.bx = 0xFFFF;
        int86(0x21, &r, &r);
        log_printf("RD free paragraphs %u, ticks since start-up %lu", r.w.bx,
                   *(unsigned long far *)MK_FP(0x40, 0x6C) - g_rd_t0);
#ifdef RDDIAG
        {   /* where each loaded overlay section sits (umb branch) */
            char b[200];
            int i, n = 0;
            b[0] = 0;
            for (i = 1; i < 35 && n < 180; i++)
                if (rd_secseg(i, 3))
                    n += sprintf(b + n, " s%d@%04x", i, rd_secseg(i, 3));
            log_printf("RD sections loaded:%s", b);
        }
#endif
    }
#endif
    if (g_bench) {
        g_seen = NULL;
        g_cursor_on = 1;
        bench_run(seen);
        return 0;
    }
    g_seen = g_see_all ? NULL : seen;
    cv_battle(1);               /* the view's canvas, with EMS / XMS (canvas.h) */
    hwp_notice();
    res = battle_run();
    cv_battle(0);
    if (res == BT_LOAD || res == BT_ABANDON)
        return res;             /* left for Load Game / Abandon Game (bsave.h) */
    map_count_loot(g_tile_loot);
    if (g_base_layout)
        map_base_modules(g_xbase_now);
    /* Unconscious aliens are not kills: the debriefing counts them as live
       captures (inv_recover) when the battle is won, as DOS does. */
    *kills = *lost = 0;
    {
        int stunned = 0;
        for (i = 0; i < g_nunits; i++) {
            if (g_units[i].alive)
                continue;
            if (g_units[i].faction == FACTION_ALIEN) {
                if (g_units[i].stunned)
                    stunned++;
                else
                    (*kills)++;
            } else if (g_units[i].faction == FACTION_XCOM && !g_units[i].stunned
                       && !UNIT_HWP(&g_units[i]))
                (*lost)++;          /* soldiers: HWPs are not casualties */
        }
        log_printf("result %d kills %d lost %d stunned %d", res, *kills, *lost, stunned);
    }
    if (g_civ_total)
        log_printf("civilians %d saved %d killed by aliens %d by X-COM %d", g_civ_total,
                   g_civ_saved, g_civ_killed_alien, g_civ_killed_xcom);
    g_seed = *(unsigned long *)MK_FP(0x40, 0x6C) + g_seed * 31;
    return res;
}

static const char *random_race(void)
{
    return race_names[rnd(n_races)];
}

/* DEBUG with RACE (no MISSION, no BATTLE): every campaign battle uses that
   race (tests: a psionic captive, a race that is easy to beat). */
char campaign_race[13];

/* Geoscape battle callback. */
static int campaign_battle(const char *pack_name, int *kills, int *lost)
{
    const char *err;
    char race[13];
    int res, resumed = g_bs_resume;

    if (resumed) {
        /* A saved battle: no briefing, its packs are in the save. */
        race[0] = 0;
        goto fight;
    }
    if (campaign_race[0] && !(pack_name[0] == 'C' && pack_name[1] == 'Y'))
        strcpy(race, campaign_race);    /* DEBUG RACE: before the mission's */
    else if (g_next_race >= 0 && g_next_race < 6) {
        /* The geoscape knows the race (alien mission, base, Cydonia). */
        FILE *f;
        sprintf(race, "R%c%c.PAK", "SFNMEX"[g_next_race], g_pakv);
        f = fopen(race, "rb");
        if (f)
            fclose(f);
        else
            strcpy(race, random_race());
    } else if (pack_name[0] == 'C' && pack_name[1] == 'Y')
        sprintf(race, "RX%c.PAK", g_pakv);
    else
        strcpy(race, campaign_race[0] ? campaign_race : random_race());
    {
        static const char codes[] = "SFNMEX";
        const char *p = strchr(codes, race[1]);
        g_last_race = p ? (int)(p - codes) : -1;
        if (g_last_race == 5)
            g_last_race = 4;
    }
    {
        /* A floppy install may lack this site's pack: use any mission. */
        FILE *f = fopen(pack_name, "rb");
        if (f)
            fclose(f);
        else
            pack_name = pack_names[rnd(n_packs)];
    }
    /* The DOS briefing (no craft at the base defence). */
    brief_show(pack_name, strncmp(pack_name, "BASE", 4) ? craft_troop_name(g_bcur) : NULL);
fight:
    uk_suspend();               /* the geoscape's fonts leave the heap (CGA) */
    res = play_mission(pack_name, race, kills, lost, &err);
    if (res == -9) {
        log_printf("mission %s race %s: %s (free memory %u KB)", pack_name, race, err,
                   dos_free_kb());
        *kills = *lost = 0;
        snd_set_geo();
        uk_resume();
        if (resumed) {
            /* The saved battle cannot be set up again (a pack missing):
               not a battle fought; back to the start menu. */
            pak_free();
            uk_notice("The saved battle cannot be loaded.");
            g_bs_exit = BX_ABANDON;
            return BT_ABANDON;
        }
        /* Not a battle fought: the craft goes home, as after Abandon. */
        pak_free();
        uk_notice("Not enough memory for this battle.");
        return BT_NOROOM;
    }
    pak_free();
    if (res == BT_LOAD || res == BT_ABANDON) {
        snd_set_geo();
        uk_resume();
        return res;
    }
    snd_result(res);
    /* The panel shows the result; the geoscape updates the roster
       (roster_after_battle) after the key. */
    input_key();
    busy_show();                /* the geoscape and the debriefing load */
    snd_set_geo();
    uk_resume();
    return res;
}

int main(int argc, char **argv)
{
    const char *err = NULL, *forced = NULL, *forced_race = NULL;
    char pack_name[13], race_name[13];
    unsigned tiles;
    int i, want_cga = 0, quick = 0, kills, lost, res = 0, snd_flags = 0;
    unsigned prog0 = *(unsigned far *)MK_FP(_psp - 1, 3), prog1;

    /* The battle's lists from DGROUP's near heap, sized by memory, then
       the rest of the near heap (18 KB) to DOS (startup.c). */
    lim_init(argc, argv);
    prog1 = *(unsigned far *)MK_FP(_psp - 1, 3);
    g_seed = *(unsigned long *)MK_FP(0x40, 0x6C);
    _amblksiz = 1024;   /* grow the heap in small steps: big tables waste less */
    /* The command line (startup.c: an overlay section, it runs once). */
    quick = main_options(argc, argv, &forced, &forced_race, &want_cga, &snd_flags);
    atexit(rd_stop);            /* main_options hooked INT 21h (rd_count_start) */

    switch (gfx_init(want_cga)) {
    case 2: printf("out of memory\n"); return 1;
    case 3: return 1;   /* HERC without the card (hgcinit.c said why) */
    }
    tiles = MAP_MAX_TILES;
    if (!g_unit_at)             /* a big UMB may hold it (startup.c) */
        g_unit_at = alloc_para(tiles);
    /* The path and sight tables (100 KB, kept for every battle) and the map
       buffer go below the packs, before the geoscape breaks the heap up.
       The fog of war is a bit in the path module's walk flags. */
    g_mx = g_my = g_mz = 0;
    if (!g_unit_at || path_init() || !(seen = path_fog_table()) || los_init() || map_alloc()) {
        gfx_shutdown();
        printf("out of memory\n");
        return 1;
    }
    {
        /* What DOS gave the program (PSP to the top of memory), the
           program's own DOS block (code, overlay area, DGROUP) and what
           is left after the start-up tables. */
        log_printf("DOS free at start: %u KB",
                   (*(unsigned far *)MK_FP(_psp, 2) - _psp) / 64);
        log_printf("memory: program %u KB (%u KB of near heap given back), %u KB free",
                   (prog1 + 63) / 64, (prog0 - prog1) / 64, dos_free_kb());
    }
    hud_init();
#if defined(RDDIAG) || defined(RDTIME)
    g_rd_t0 = *(unsigned long far *)MK_FP(0x40, 0x6C);
#endif
    mouse_init();
    timer_init();
    snd_init(snd_flags);
    rng_seed(g_seed);

    if (!quick)
        cut_intro(argc, argv);  /* the intro movie, as DOS (src/cut.c) */
    if (quick) {
        /* Quick battles (OpenXcom's New Battle) can be saved; Load Game in
           one goes on with a quick battle's save here, with any other in
           the campaign, as Abandon Game goes to the start menu. */
        g_bctx.kind = BK_QUICK;
        for (;;) {
            rng_seed(g_seed);
            if (forced)
                strncpy(pack_name, forced, 12);
            else if (g_bench)
                strcpy(pack_name, g_video == VIDEO_EGA ? "FA1E.PAK" : "FA1C.PAK");
            else
                strcpy(pack_name, pack_names[rnd(n_packs)]);
            pack_name[12] = 0;
            if (forced_race)
                strncpy(race_name, forced_race, 12);
            else if (g_bench)
                strcpy(race_name, g_video == VIDEO_EGA ? "RSE.PAK" : "RSC.PAK");
            else
                strcpy(race_name, random_race());
            race_name[12] = 0;
            res = play_mission(pack_name, race_name, &kills, &lost, &err);
            if (res == BT_LOAD || res == BT_ABANDON) {
                pak_free();
                if (menu_quick_left(res))
                    continue;
                quick = 0;          /* the campaign (the start menu if none was loaded) */
                break;
            }
            if (res == -9 || g_bench)
                break;
            snd_result(res);
            hud_field(0, 23, 40, 15, "N: new mission   Esc: quit to DOS");
            {
                int key;
                do
                    key = input_key() & 0xFF;
                while (key != 27 && (key | 0x20) != 'n');
                if (key == 27)
                    break;
            }
            forced = NULL;
            forced_race = NULL;
        }
        if (g_bench)
            _bios_keybrd(_KEYBRD_READ);
    }
    if (!quick) {
        if (geo_init()) {
            err = "world map (WORLDx.PAK) missing";
        } else {
            snd_set_geo();
            /* Start menu; abandoning a game comes back to it. A campaign
               loaded in a quick battle goes on first (menu_start). */
            while (menu_start()) {
                busy_show();    /* the geoscape's code and world: a second or two */
                geo_run(campaign_battle);
                if (!g_geo_abandon)
                    break;
            }
        }
    }
    log_printf("quit to DOS");
    snd_shutdown();
    mouse_shutdown();
    timer_shutdown();
    gfx_shutdown();
    if (err)
        printf("%s\n", err);
    else
        printf("Thanks for playing xcom-xt.\n");
    return 0;
}
