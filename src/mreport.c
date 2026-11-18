/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The month-end report (MonthlyReportState, alien.h): its own overlay
   section, loaded next to the geoscape once a month. The countries'
   decisions (Country::newMonth) and the rating are computed here, the
   report is DOS's: heading, month, rating, income, maintenance and balance,
   and the council's words with the countries that are pleased, unhappy or
   have signed a pact. The council's words and the heading are the game's
   texts: read from the interface pack (UI_REPORT, RT_*; made from the
   player's ENGLISH.DAT), not kept here. Compiled with -zc: strings stay in
   the section's code. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alien.h"
#include "alien_in.h"
#include "base.h"
#include "geo.h"
#include "hud.h"
#include "input.h"
#include "map.h"
#include "uik.h"
#include "uitext.h"

static long funding_total(void)
{
    long total = 0;
    int i;

    for (i = 0; al_world && i < al_n_countries; i++)
        total += al_cstate[i].funding;
    return total;
}

/* Country::newMonth (the DOS rule): each country weighs the Council's
   totals and its own activity; pacts stop the funding. */
static long country_month(long xcom_total, long alien_total)
{
    long diff = 0;
    int i;

    for (i = 0; i < al_n_countries; i++) {
        CountryState *s = &al_cstate[i];
        long f = s->funding, cap = al_countries[i].funding_cap, delta, change = 0;
        long good = xcom_total / 10 + s->xcom, bad = alien_total / 20 + s->alien;

        delta = f * (5 + rnd(16)) / 100;
        if (bad <= good + 30) {
            if (good > bad + 30 && (long)rnd((unsigned)(good > 30000 ? 30000 : good) + 1) > bad) {
                change = delta;
                if (f + change > cap)
                    change = cap - f;
            }
        } else if ((long)rnd((unsigned)(bad > 30000 ? 30000 : bad) + 1) > good) {
            change = -delta;
            if (f + change < 0)
                change = -f;
        }
        if (s->new_pact && !s->pact) {
            s->new_pact = 0;
            s->pact = 2;
        }
        if (s->pact)
            change = -f;
        s->funding = f + change;
        s->change = change;
        diff += change;
    }
    return diff;
}

/* Text of width w from (x, y) on, wrapped at spaces; "\n" breaks a line.
   Stops at y_end. */
static void flow(int x, int y, int w, int y_end, int colour, const char *p)
{
    char line[64];
    int n = 0, last = -1;

    for (;; p++) {
        char c = *p;
        if (c == '\n' || !c) {
            if (n)
                uk_text(x, y, UK_SMALL, colour, line);
            y += 8;
            n = 0;
            last = -1;
            if (!c || y > y_end)
                return;
            continue;
        }
        if (n < 62) {
            line[n++] = c;
            line[n] = 0;
            if (c == ' ')
                last = n - 1;
            if (uk_width(UK_SMALL, line) <= w)
                continue;
            /* One word too many: cut at the last space. */
            if (last > 0) {
                int rest = n - last - 1;
                line[last] = 0;
                uk_text(x, y, UK_SMALL, colour, line);
                memmove(line, line + last + 1, rest);
                n = rest;
                line[n] = 0;
            } else {
                uk_text(x, y, UK_SMALL, colour, line);
                n = 0;
            }
            last = -1;
            y += 8;
            if (y > y_end)
                return;
        }
    }
}

/* "A", "A and B", "A, B and C" (countryList) of the countries marked
   with kind in kinds[]; returns how many. */
static int names(char *out, const unsigned char *kinds, int kind)
{
    int i, n = 0, total = 0;

    for (i = 0; i < al_n_countries; i++)
        total += kinds[i] == kind;
    out[0] = 0;
    for (i = 0; i < al_n_countries; i++)
        if (kinds[i] == kind) {
            if (n)
                strcat(out, n + 1 == total ? " and " : ", ");
            strcat(out, al_countries[i].name);
            n++;
        }
    return n;
}

/* Text k (RT_*) of the month report table into dst (max bytes with the NUL). */
#define rtext(dst, max, k)  ui_text(UI_REPORT, dst, max, k)

/* A paragraph after the text so far: text k. */
#define TEXT_MAX    700
static void paragraph(char *out, int k)
{
    unsigned l0 = strlen(out);

    if (l0 + 3 < TEXT_MAX) {
        strcpy(out + l0, "\n\n");
        if (!rtext(out + l0 + 2, TEXT_MAX - l0 - 2, k))
            out[l0] = 0;
    }
}

/* One paragraph about the countries of one kind: the names, then the
   singular or plural words (text one for a single country, else one + 1;
   left out if it would not fit the buffer: DOS cuts its text at the
   window's height too). */
static void country_text(char *out, const unsigned char *kinds, int kind, int one)
{
    char list[260];
    int n = names(list, kinds, kind);
    unsigned l0 = strlen(out), l1 = l0 + strlen(list) + 3;

    if (n && l1 + 2 < TEXT_MAX) {
        sprintf(out + l0, "\n\n%s ", list);
        if (!rtext(out + l1, TEXT_MAX - l1, n == 1 ? one : one + 1))
            out[l0] = 0;
    }
}

int alien_month_report(int months, long funds, long costs, int month, int year)
{
    char line[48], m[16], *text = malloc(TEXT_MAX);
    unsigned char kinds[MAX_COUNTRIES];
    const char *rate;
    long xcom_total, change;
    int i, lose = 0, thr, rating;

    if (!text)
        return 0;
    uk_open();
    /* The Council is more lenient after the first month: +400 research. */
    if (months > 1)
        al_month_research += 400;
    xcom_total = al_month_xcom + al_month_research;
    rating = (int)(xcom_total - al_month_alien);
    change = al_world ? country_month(xcom_total, al_month_alien) : 0;
    /* Thresholds: defeatScore -900 + 100 per difficulty level. */
    thr = -900 + 100 * al_difficulty();
    rate = rating > 500 ? "EXCELLENT!" : rating > 0 ? "GOOD!" : rating > thr ? "OK"
         : rating > thr - 300 ? "POOR!" : "TERRIBLE!";
    rtext(text, TEXT_MAX, rating > 500 ? RT_PLEASED : rating > thr ? RT_SATISFIED : RT_DISSATISFIED);
    if (al_last_rating <= thr && rating <= thr)
        lose = 1;
    else if (funds <= -1000) {
        if (al_warned)
            lose = 1;
        else {
            al_warned = 1;
            paragraph(text, RT_DEBT);
        }
    } else
        al_warned = 0;
    if (lose)
        rtext(text, TEXT_MAX, RT_TERMINATED);
    al_last_rating = rating;

    /* The countries: 1 happy (funding up), 2 unhappy (down), 3 new pact. */
    for (i = 0; i < al_n_countries; i++) {
        const CountryState *s = &al_cstate[i];
        kinds[i] = s->pact == 2 ? 3 : s->change > 0 ? 1 : s->change < 0 ? 2 : 0;
    }
    if (!lose) {
        country_text(text, kinds, 1, RT_UP_ONE);
        country_text(text, kinds, 2, RT_DOWN_ONE);
        country_text(text, kinds, 3, RT_PACT_ONE);
    }
    for (i = 0; i < al_n_countries; i++)
        if (al_cstate[i].pact == 2)
            al_cstate[i].pact = 1;

    uk_invalidate();
    uk_screen(UI_BG_REPORT, T_PURPLE);
    uk_window(0, 0, 320, 200);
    /* The big font here is wider than the VGA game's: the small one when it does not fit. */
    rtext(line, sizeof line, RT_TITLE);
    uk_text(16, 8, uk_width(UK_BIG, line) > 300 ? UK_SMALL : UK_BIG, g_uk->title, line);
    sprintf(line, "Month> %.3s %d", (const char *)"JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * month, year);
    uk_text(16, 24, UK_SMALL, 9, line);
    sprintf(line, "Monthly Rating> %d %s", rating, rate);
    uk_text(146, 24, UK_SMALL, 9, line);
    geo_money(m, funding_total(), 0);
    sprintf(line, "Income> %s (%s", m, change > 0 ? "+" : change < 0 ? "-" : "");
    geo_money(m, change < 0 ? -change : change, 0);
    strcat(line, m);
    strcat(line, ")");
    uk_text(16, 32, UK_SMALL, 9, line);
    geo_money(m, costs, 0);
    sprintf(line, "Maintenance> %s", m);
    uk_text(16, 40, UK_SMALL, 9, line);
    geo_money(m, funds < 0 ? -funds : funds, 0);
    sprintf(line, "Balance> %s%s", funds < 0 ? "-" : "", m);
    uk_text(160, 40, UK_SMALL, 9, line);
    flow(16, 48, 280, 176, 13, text);
    uk_button(135, 180, 50, 12, "OK", 0);
    log_printf("REPORT rating %d xcom %ld research %ld aliens %ld change %ld funds %ld costs %ld",
               rating, al_month_xcom, al_month_research, al_month_alien, change, funds, costs);
    /* Like the OK button of the DOS report: other keys do nothing. */
    for (;;) {
        unsigned k = input_key();
        if ((k & 0xFF) == 13 || (k & 0xFF) == 27 || (k & 0xFF) == ' '
            || (k == KEY_LCLICK && g_mouse_y >= 180 && g_mouse_y < 192))
            break;
    }
    uk_close();
    free(text);
    hud_clear_rows(0, 25);
    for (i = 0; i < al_n_countries; i++)
        al_cstate[i].xcom = al_cstate[i].alien = 0;
    al_month_xcom = al_month_alien = al_month_research = 0;
    return lose;
}
