/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The funding screen (alien.h) and the recruits' names, in the rarely used
   overlay section: they run on request and call nothing of the hourly
   section. The month-end report is mreport.c's. */

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

#define COL_INFO    7
#define COL_ALERT   12
#define COL_GOOD    10
#define COL_DOWN    14

/* A text with dots after it up to width w (TextList::setDot). */
static void dotted(int x, int y, int w, int c, const char *s)
{
    char t[40];
    int n = strlen(s);

    strcpy(t, s);
    while (n < 38 && uk_width(UK_SMALL, t) < w) {
        t[n++] = '.';
        t[n] = 0;
    }
    uk_text(x, y, UK_SMALL, c, t);
}

/* FundingState (fundingWindow): "International Relations", the countries
   with their funding and its change since last month, TOTAL; the list
   dotted as DOS (columns 108, 100, 52 from x 32, headers at y 30, rows
   from y 40). Funding is in $K here, DOS has whole dollars. */
static void fund_money(char *buf, long k, int sign)
{
    char m[16];

    geo_money(m, k < 0 ? -k : k, 0);
    sprintf(buf, "%s%s", k < 0 ? "-" : sign && k > 0 ? "+" : "", m);
}

void alien_funding_screen(void)
{
    char line[24];
    int i, y;
    long total = 0;

    if (!al_world)
        return;
    uk_page(UI_BG_STORES, T_GREEN, "International Relations");
    uk_line(32, 30, 260, g_uk->text, "Country	Funding	Change", (const unsigned char *)"lÐ");
    for (i = 0, y = 40; i < al_n_countries; i++, y += 8) {
        const CountryState *s = &al_cstate[i];
        total += s->funding;
        dotted(32, y, 108, g_uk->list, al_countries[i].name);
        fund_money(line, s->funding, 0);
        dotted(140, y, 100, g_uk->list, line);
        fund_money(line, s->change, 1);
        uk_text(240, y, UK_SMALL, g_uk->list, line);
    }
    dotted(32, y, 108, g_uk->text, "TOTAL");
    fund_money(line, total, 0);
    uk_text(140, y, UK_SMALL, g_uk->text, line);
    uk_page_wait();
    hud_clear_rows(0, 25);
}

/* Recruits' names (base.h), here in the rarely used section: called when
   recruits arrive and at the start of a campaign. */

/* One name from a nation's block of NAMES.PAK (tools/names.py). */
static int pick_name(FILE *f, long blk, unsigned idx, char *out)
{
    unsigned char b[2], len;
    unsigned o;

    if (fseek(f, blk + 8 + 2L * idx, SEEK_SET) || fread(b, 1, 2, f) != 2)
        return 0;
    o = b[0] | b[1] << 8;
    if (fseek(f, blk + o, SEEK_SET) || fread(&len, 1, 1, f) != 1 || len > 23
        || fread(out, 1, len, f) != len)
        return 0;
    out[len] = 0;
    return 1;
}

/* A soldier's name as SoldierNamePool::genName: a nationality, a woman one
   time in four (soldiers.rul femaleFrequency), first and last name. The
   roster keeps 13 letters: longer names are drawn again (10 tries, the
   DOS game's limit for duplicates), then the first name is shortened.
   Without NAMES.PAK the recruit keeps the built-in name. */
void base_name_soldier(char *name)
{
    FILE *f = fopen("NAMES.PAK", "rb");
    unsigned char h[8];
    char first[24], last[24];
    int tries, k, nations = 0;

    if (!f)
        return;
    if (fread(h, 1, 5, f) == 5 && !memcmp(h, "XNM1", 4))
        nations = h[4];
    for (tries = 0; tries < 10 && nations; tries++) {
        unsigned n[4], fem;
        long blk;
        if (fseek(f, 5 + 4L * rnd(nations), SEEK_SET) || fread(h, 1, 4, f) != 4)
            break;
        blk = h[0] | (long)h[1] << 8 | (long)h[2] << 16;
        if (fseek(f, blk, SEEK_SET) || fread(h, 1, 8, f) != 8)
            break;
        for (k = 0; k < 4; k++)
            n[k] = h[2 * k] | h[2 * k + 1] << 8;
        fem = rnd(100) < 25 && n[1];
        k = fem ? 3 : 2;
        if (!n[0] || !pick_name(f, blk, fem ? n[0] + rnd(n[1]) : rnd(n[0]), first))
            break;
        last[0] = 0;
        if (n[k] && !pick_name(f, blk, n[0] + n[1] + (fem ? n[2] : 0) + rnd(n[k]), last))
            break;
        if (strlen(first) + strlen(last) + 1 > 13) {
            if (tries < 9)
                continue;
            first[1] = '.';
            first[2] = 0;
        }
        sprintf(first + strlen(first), last[0] ? " %.12s" : "", last);
        strncpy(name, first, 13);
        name[13] = 0;
        break;
    }
    fclose(f);
}
