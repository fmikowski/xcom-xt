/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Air combat (DogfightState) in the DOS windows over the globe, up to four
   at once. In the rarely used overlay section: it runs once per
   interception and calls nothing of the hourly geoscape section. */

#include <stdio.h>
#include "afwin.h"
#include "base.h"
#include "craft.h"
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "map.h"
#include "menu.h"
#include "sound.h"
#include "timer.h"
#include "uik.h"

/* ------------------------------------------------------------------ */
/* Air combat: the DOS window (src/afwin.c draws it, in this section too),
   the UFO coming down the radar towards the craft; the distance between
   them in OpenXcom's units. */

#define DIST_START   560        /* standoff distance (OpenXcom units) */
#define DIST_AWAY    640        /* the fight starts here and ends beyond it */
#define DIST_CLOSE   64
#define DIST_OFF     800        /* disengaging */
#define TICKS_FRAME  2

enum { ST_STANDOFF, ST_CAUTIOUS, ST_STANDARD, ST_AGGRESSIVE, ST_DISENGAGE };

static const UfoCombat *combat(const Fight *f)
{
    return &g_ufo_combat[f->ufo_type < N_UFO_COMBAT ? f->ufo_type : 1];
}

static void draw_shots(Fight *f, int erase)
{
    int i;
    for (i = 0; i < DF_SHOTS; i++)
        if (f->shots[i].slot >= 0)
            aw_shot(&f->w, f->shots[i].slot, f->shots[i].pos, erase);
}

/* DogfightState::setStatus: the text stays 50 ticks (OpenXcom updates). */
#define MSG_TICKS   50

static void df_say(Fight *f, int colour, const char *s)
{
    aw_status(&f->w, colour, s);
    f->msgt = MSG_TICKS;
}

static void df_message(Fight *f, int colour, const char *s)
{
    df_say(f, colour, s);
    log_printf("AIR %s", s);
}

static int weapon_range(const Craft *c, int s)
{
    return c->weapon[s] != CW_NONE ? g_craft_weapons[c->weapon[s]].range * 8 : 0;
}

/* One frame (TICKS_FRAME ticks) of DogfightState::update for fight f, 20
   ticks a second here (33 in OpenXcom by default). The distance starts at
   640 and closes 2 a tick (opens 4 a tick) towards the stance's distance;
   weapons switched on fire in range except at standoff, at the stance's
   reload; missiles and cannon rounds fly 8 a tick, beams hit after 3
   ticks; to hit: accuracy x (100 + 300 / (5 - UFO size)) / 200 %, damage
   50-100 %. The UFO fires when in its range: 60 % to hit, 0-power damage,
   reload (reload - 2 x difficulty) plus up to as much again. Past its
   break-off countdown the UFO runs at top speed: it escapes if faster
   than the craft. More than half its damage crashes it, all of it
   destroys it; another fight that brings it down ends this one. The
   status line shows DogfightState's texts. Cautious at half the craft's
   damage only moves the distance to standoff (retreat), the stance and
   the firing stay. */
static void fight_frame(Fight *f)
{
    const UfoCombat *uc = combat(f);
    Craft *c = f->c;
    const CraftType *ct = &g_craft_types[c->type];
    int diff = g_difficulty >= 0 && g_difficulty < N_DIFFICULTY ? g_difficulty : 1;
    int tick, i, s, dmg;
    char name[24];

    sprintf(name, "%s-%d", ct->name, c->num);
    draw_shots(f, 1);
    for (tick = 0; tick < TICKS_FRAME && f->result < 0; tick++) {
        int want = DIST_START, lo = 999, hi = 0, change = 0, flying = 0;

        if (*f->ufo_damage > uc->hp / 2) {
            f->result = DF_ENDED;       /* brought down in another fight */
            break;
        }
        if (f->msgt && !--f->msgt) {
            if (f->breaking)
                df_say(f, 12, "UFO OUTRUNNING INTERCEPTOR!");
            else
                aw_status(&f->w, 0, "");
        }
        /* The UFO's break-off countdown runs once a tick however many
           craft fight it (Ufo::setInterceptionProcessed); then it outruns
           them all. */
        if (f->counts && *f->escape > 0)
            --*f->escape;
        if (!f->breaking && *f->escape == 0 && uc->speed > ct->speed) {
            f->breaking = 1;
            df_message(f, 12, "UFO OUTRUNNING INTERCEPTOR!");
        }
        if (f->ufo_cd > 0)
            f->ufo_cd--;
        for (s = 0; s < 2; s++)
            if (c->weapon[s] != CW_NONE && c->ammo[s] > 0) {
                int r = weapon_range(c, s);
                if (r < lo) lo = r;
                if (r > hi) hi = r;
            }
        switch (f->stance) {
        case ST_CAUTIOUS: want = hi && !f->retreat ? hi : DIST_START; break;
        case ST_STANDARD: want = hi ? lo : DIST_START; break;
        case ST_AGGRESSIVE: want = DIST_CLOSE; break;
        case ST_DISENGAGE: want = DIST_OFF; break;
        }
        if (f->breaking)
            change = 4;
        else if (f->dist < want)
            change = f->dist + 4 > want ? want - f->dist : 4;
        else if (f->dist > want)
            change = -2;
        f->dist += change;

        /* Shots fly 8 units a tick (beams 3 ticks); the hit roll is made
           on arrival. Fired shots keep their place when the craft moves. */
        for (i = 0; i < DF_SHOTS; i++) {
            const CraftWeapon *w;
            if (f->shots[i].slot < 0)
                continue;
            w = &g_craft_weapons[c->weapon[f->shots[i].slot]];
            if (w->ammo_item == NO_ITEM)
                f->shots[i].pos = ++f->shots[i].cov >= 3 ? f->dist : 0;
            else {
                f->shots[i].pos += 8 + (f->breaking ? 0 : change);
                if ((f->shots[i].cov += 8) > w->range * 8) {
                    f->shots[i].slot = -1;      /* past its range: missed */
                    continue;
                }
            }
            if (f->shots[i].pos < f->dist) {
                flying = 1;
                continue;
            }
            f->shots[i].slot = -1;
            if ((int)rnd(100) < (w->accuracy * (100 + 300 / (5 - uc->size)) + 100) / 200) {
                dmg = w->damage / 2 + rnd(w->damage - w->damage / 2 + 1);
                *f->ufo_damage += dmg;
                f->ufo_flash = 2;
                f->dirty = 1;
                snd_hit();
                df_say(f, 10, "UFO HIT!");
                log_printf("AIR UFO hit by %s: %d damage.", w->name, dmg);
                if (*f->ufo_damage >= uc->hp) {
                    f->result = DF_UFO_DESTROYED;
                    df_message(f, 10, "UFO DESTROYED!");
                } else if (*f->ufo_damage > uc->hp / 2) {
                    f->result = DF_UFO_DOWN;
                    df_message(f, 10, "UFO CRASH LANDS!");
                }
                if (f->result >= 0) {
                    snd_death();
                    break;
                }
            }
        }
        if (f->result >= 0)
            break;

        /* Our weapons fire on their own when in range, not at standoff
           and not when switched off. */
        for (s = 0; s < 2; s++) {
            const CraftWeapon *w;
            if (c->weapon[s] == CW_NONE)
                continue;
            w = &g_craft_weapons[c->weapon[s]];
            if (f->cooldown[s] > 0) {
                f->cooldown[s]--;
                continue;
            }
            if (c->ammo[s] <= 0 || f->off[s] || f->stance == ST_STANDOFF
                || f->stance == ST_DISENGAGE || f->dist > w->range * 8)
                continue;
            for (i = 0; i < DF_SHOTS && f->shots[i].slot >= 0; i++)
                ;
            if (i == DF_SHOTS)
                continue;
            f->shots[i].slot = s;
            f->shots[i].pos = 0;
            f->shots[i].cov = 0;
            c->ammo[s]--;
            f->cooldown[s] = w->reload[f->reload];
            f->dirty = 1;
            flying = 1;
            snd_shot(0);
        }

        /* The UFO fires back when we are inside its range. */
        if (uc->power && f->ufo_cd == 0 && f->dist <= uc->range * 8) {
            int fc = uc->reload - 2 * diff;
            if (fc < 1)
                fc = 1;
            f->ufo_cd = fc + rnd(fc + 1);
            snd_shot(1);
            df_say(f, 12, "UFO RETURNS FIRE!");
            if ((int)rnd(100) < 60) {
                dmg = rnd(uc->power + 1);
                if (dmg) {
                    c->damage += dmg;
                    f->craft_flash = 2;
                    f->dirty = 1;
                    df_say(f, 12, ">>> INTERCEPTOR DAMAGED <<<");
                    log_printf("AIR %s hit: %d damage.", name, dmg);
                    if (c->damage >= ct->hp) {
                        f->result = DF_CRAFT_LOST;
                        snd_death();
                        df_message(f, 12, ">>> INTERCEPTOR DESTROYED <<<");
                        break;
                    }
                    /* Cautious: at half damage the target distance is the
                       standoff one (the stance stays). */
                    if (f->stance == ST_CAUTIOUS && c->damage * 2 >= ct->hp)
                        f->retreat = 1;
                }
            }
        }
        if (f->dist > DIST_AWAY && !flying && (f->breaking || f->stance == ST_DISENGAGE)) {
            f->result = f->breaking ? DF_UFO_ESCAPED : DF_DISENGAGED;
        }
    }

    /* Redraw the moving parts. */
    aw_ufo(&f->w, f->dist, f->ufo_flash, f->result == DF_UFO_DESTROYED || f->result == DF_ENDED);
    if (f->ufo_flash)
        f->ufo_flash--;
    if (f->craft_flash)
        f->craft_flash--;
    if (f->result < 0)
        draw_shots(f, 0);
    aw_distance(&f->w, f->dist);
    if (f->dirty) {
        f->dirty = 0;
        /* The craft picture only when its damage changes (read from the
           UI pack). */
        dmg = f->result == DF_CRAFT_LOST ? 100 : c->damage * 100 / ct->hp;
        if (dmg != f->shown_dmg)
            aw_damage(&f->w, c->type, f->shown_dmg = dmg);
        for (s = 0; s < 2; s++)
            if (c->weapon[s] != CW_NONE)
                aw_ammo(&f->w, s, c->ammo[s], f->off[s]);
        aw_stance(&f->w, f->stance);
    }
    if (f->result >= 0)
        log_printf("AIR result %d craft damage %d ufo damage %d", f->result, c->damage,
                   *f->ufo_damage);
}

/* A key (or the click it stands for) for fight f. Returns 1 if it was
   minimised. */
static int fight_key(Fight *f, int ch)
{
    const CraftType *ct = &g_craft_types[f->c->type];
    int s;
    char line[48];

    if (ch == 'u') {
        /* The UFO button: its picture until a key. */
        aw_preview(&f->w, f->ufo_type > 0 && f->ufo_type < N_UFO_COMBAT ? f->ufo_type - 1 : 0);
        f->dirty = 1;
        f->shown_dmg = -1;
    }
    if (ch == 27)
        ch = '5';
    if (ch >= '1' && ch <= '5' && !f->breaking) {
        f->stance = ch - '1';
        f->retreat = 0;
        df_say(f, 10, f->stance == ST_STANDOFF ? "STANDOFF"
                      : f->stance == ST_CAUTIOUS ? "CAUTIOUS ATTACK"
                      : f->stance == ST_STANDARD ? "STANDARD ATTACK"
                      : f->stance == ST_AGGRESSIVE ? "AGGRESSIVE ATTACK" : "DISENGAGING");
        /* The reload is the stance's (standoff keeps the last). */
        if (f->stance >= ST_CAUTIOUS && f->stance <= ST_AGGRESSIVE)
            f->reload = f->stance - ST_CAUTIOUS;
        f->dirty = 1;
    }
    if ((ch == '6' || ch == '7') && f->c->weapon[s = ch - '6'] != CW_NONE) {
        /* DogfightState::weapon1Click: the weapon on or off. */
        f->off[s] ^= 1;
        aw_weapon(&f->w, s, weapon_range(f->c, s), f->off[s]);
        f->dirty = 1;
        sprintf(line, "%s-%d weapon %d %s", ct->name, f->c->num, s + 1, f->off[s] ? "off" : "on");
        log_printf("AIR %s", line);
    }
    if (ch == 'm') {
        /* btnMinimizeClick: at standoff range only. */
        if (f->dist >= DIST_START && !f->breaking) {
            f->result = DF_MINIMISED;
            sprintf(line, "%s-%d: dogfight minimised.", ct->name, f->c->num);
            log_printf("AIR %s", line);
            return 1;
        }
        df_message(f, 12, "Minimise at Standoff Range Only");
    }
    return 0;
}

int craft_dogfights(Fight *f, int n)
{
    int open[MAX_FIGHTS], m = 0, k, i, focus = 0, any = 1, minimised = 0, range[2];

    for (i = 0; i < n; i++) {
        Fight *g = &f[i];
        const UfoCombat *uc = combat(g);
        int diff = g_difficulty >= 0 && g_difficulty < N_DIFFICULTY ? g_difficulty : 1;
        if (g->result < 0 && !g->started) {
            g->started = 1;
            g->counts = 1;
            for (k = 0; k < i; k++)
                if (f[k].ufo_damage == g->ufo_damage && f[k].counts)
                    g->counts = 0;
            g->dist = g->resume ? DIST_START : DIST_AWAY;
            g->stance = ST_STANDOFF;
            g->breaking = 0;
            g->cooldown[0] = g->cooldown[1] = 0;
            g->reload = 1;
            g->ufo_cd = 0;
            g->retreat = 0;
            g->msgt = 0;
            g->craft_flash = g->ufo_flash = 0;
            for (k = 0; k < DF_SHOTS; k++)
                g->shots[k].slot = -1;
            if (*g->escape <= 0) {
                *g->escape = uc->breakoff + rnd(uc->breakoff + 1) - 30 * diff;
                if (*g->escape < 1)
                    *g->escape = 1;
            }
            log_printf("AIR %s-%d vs UFO-%d %s", g_craft_types[g->c->type].name, g->c->num,
                       g->ufo_id, g->ufo_name);
        }
        if (g->result < 0 && m < MAX_FIGHTS)
            open[m++] = i;
    }
    if (!m)
        return 0;
    aw_begin();
    for (k = 0; k < m; k++) {
        Fight *g = &f[open[k]];
        aw_place(&g->w, k, m);
        range[0] = weapon_range(g->c, 0);
        range[1] = weapon_range(g->c, 1);
        aw_open(&g->w, g->c->type, g->c->weapon, range, g->off, combat(g)->size);
    }
    aw_ready();
    for (k = 0; k < m; k++) {
        Fight *g = &f[open[k]];
        g->dirty = 1;
        g->shown_dmg = -1;
        log_printf("AIR Intercepting UFO-%d.", g->ufo_id);
        df_say(g, 10, "STANDOFF");
    }

    while (any && !minimised) {
        unsigned long t0 = timer_read();

        if (input_ready()) {
            unsigned key = input_key();
            int target = focus;
            if (key == KEY_LCLICK) {
                key = 0;
                for (k = 0; k < m; k++)
                    if (g_mouse_x >= f[open[k]].w.x && g_mouse_x < f[open[k]].w.x + 160
                        && g_mouse_y >= f[open[k]].w.y && g_mouse_y < f[open[k]].w.y + 96) {
                        target = focus = k;
                        key = aw_click(&f[open[k]].w);
                    }
            }
            if ((key & 0xFF) == '\t')
                focus = (focus + 1) % m;
            else if (f[open[target]].result < 0)
                minimised = fight_key(&f[open[target]], key & 0xFF);
        }
        any = 0;
        for (k = 0; k < m; k++)
            if (f[open[k]].result < 0) {
                fight_frame(&f[open[k]]);
                any |= f[open[k]].result < 0;
            }
        /* About 20 ticks a second (scripts too, so test runs take real
           time). */
        while (timer_read() - t0 < 1193UL * 100)
            ;
    }
    /* All fights over: the windows wait for a key. A minimised one goes
       at once (the others come back placed anew). */
    aw_close(!minimised);
    hud_clear_rows(0, 25);
    for (i = k = 0; i < n; i++)
        k += f[i].result < 0;
    return k;
}
