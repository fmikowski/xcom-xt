/* SPDX-License-Identifier: GPL-3.0-or-later */
/* UFOpaedia: sections, entries unlocked by research, text pages. Titles,
   sections and unlock rules come from UFOPAED.PAK (tools/pedia.py), so
   nothing stays in memory while the screen is closed. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "hud.h"
#include "uik.h"
#include "input.h"
#include "menu.h"
#include "pedia.h"
#include "geo.h"

#define MAX_LIST    24

unsigned g_pedia_ufos;

#pragma pack(1)
typedef struct {
    char title[24], unlock[24];
    unsigned char section, reserved;
    unsigned short offset, length;
} PackEntry;
#pragma pack()

static FILE *pak;
static PackEntry *pe;
static char (*sections)[24];
static unsigned n_pe, n_sections;
static long text_base;

/* "" always, "#n" UFO type n recovered, "?name" item exists, else the
   research topic of that name. */
static int unlocked(const PackEntry *e)
{
    const char *u = e->unlock;
    int i;

    if (!*u || g_debug)
        return 1;                       /* DEBUG: every page */
    if (*u == '#')
        return (g_pedia_ufos >> atoi(u + 1)) & 1;
    if (*u == '?') {
        for (i = 0; i < N_ITEMS; i++)
            if (!strcmp(g_items[i].name, u + 1))
                return 1;
        return 0;
    }
    for (i = 0; i < N_TOPICS; i++)
        if (!strcmp(g_topics[i].name, u))
            return g_base.done[i];
    return 0;
}

static int open_pack(void)
{
    unsigned short h[4];

    pak = fopen("UFOPAED.PAK", "rb");
    if (!pak)
        return 0;
    if (fread(h, 2, 4, pak) == 4 && !memcmp(h, "XUP2", 4)) {
        n_pe = h[2];
        n_sections = h[3];
        sections = malloc(n_sections * 24);
        pe = malloc(n_pe * sizeof(PackEntry));
        if (sections && pe && fread(sections, 24, n_sections, pak) == n_sections
            && fread(pe, sizeof(PackEntry), n_pe, pak) == n_pe) {
            text_base = ftell(pak);
            return 1;
        }
    }
    free(sections);
    free(pe);
    fclose(pak);
    return 0;
}

static void close_pack(void)
{
    free(sections);
    free(pe);
    fclose(pak);
}

/* ------------------------------------------------------------------ */
/* Pages, laid out as OpenXcom's ArticleState* (src/uipak.h PL_*) */

#pragma pack(1)
typedef struct {
    unsigned char layout, stats_x;      /* stats x / 2 */
    unsigned short pic, x, y, w;        /* picture, text box */
    unsigned short stats_y;             /* 0: stats above the text */
    unsigned short pic2;                /* facility: its building */
    unsigned char size;                 /* facility: 1 or 2 squares */
    unsigned char stats_w;              /* stats box width / 2 */
} PageDef;
#pragma pack()

#define BTN_Y       5
static const char page_buttons[] = "OK";

/* A line of stats: runs of two spaces become columns; in a stats box
   (box set) a "label  value" line, the value at 7/10 of its width as
   OpenXcom's article lists. Returns the next y. */
static int stat_line(int x, int y, int w, int box, const char *s)
{
    unsigned char tabs[3];
    char t[48];
    int n = 0;

    tabs[0] = (unsigned char)(box ? w * 7 / 10 : 112);
    tabs[1] = (unsigned char)(box ? 0 : 200);
    tabs[2] = 0;
    while (*s && n < 46) {
        if (s[0] == ' ' && s[1] == ' ') {
            while (*s == ' ')
                s++;
            t[n++] = '\t';
            continue;
        }
        t[n++] = *s++;
    }
    t[n] = 0;
    uk_line(x, y, w, 11, t, tabs);
    return y + 9;
}

/* One line of the flow (not drawn while lines are to be skipped). */
static void put(int x, int *y, int colour, const char *s, int *skip)
{
    if (*skip > 0) {
        (*skip)--;
        return;
    }
    uk_text(x, *y, UK_SMALL, colour, s);
    *y += 9;
}

/* Prints the description from p on, words wrapped at w pixels; returns
   the next y. */
int pedia_flow(int x, int y, int w, const char *p, int y_end, int colour, int skip, int *more)
{
    char line[64];
    int n = 0, last_space = -1;

    for (;; p++) {
        char c = *p;
        if (c == '\n') {
            /* The pack wraps at 38 characters: join the lines again. */
            if (n && line[n - 1] == '-')
                continue;
            c = ' ';
        }
        if (c == ' ' && !n)
            continue;
        if (c && n < 62) {
            line[n++] = c;
            line[n] = 0;
            if (c == ' ')
                last_space = n - 1;
            if (uk_width(UK_SMALL, line) <= w)
                continue;
        }
        if (!c && !n)
            break;
        if (c && last_space > 0) {
            /* Cut at the last space; the rest starts the next line. */
            int rest = n - last_space - 1;
            line[last_space] = 0;
            put(x, &y, colour, line, &skip);
            memmove(line, line + last_space + 1, rest);
            n = rest;
            line[n] = 0;
            last_space = -1;
        } else {
            put(x, &y, colour, line, &skip);
            n = 0;
            last_space = -1;
        }
        if (!c || y > y_end)
            break;
    }
    if (more)
        *more = *p != 0;
    return y;
}

/* Weapon, grenade and armour stats from the engine's tables, found by the
   entry's title. */
static int show_stats(const PackEntry *e, int x, int y)
{
    static const unsigned char tabs[] = { 96, 160, 0 };
    char line[48];
    int i, a;

    for (i = 0; i < N_ITEMS && strcmp(g_items[i].name, e->title); i++)
        ;
    if (i < N_ITEMS && (g_items[i].kind == IK_WEAPON || g_items[i].kind == IK_HWP)) {
        const ItemDef *it = &g_items[i];
        uk_line(x, y, 300, 14, "Shot\tAccuracy\tTU cost", tabs);
        y += 10;
        for (i = 0; i < 3; i++) {
            int acc = i == 0 ? it->acc_snap : i == 1 ? it->acc_aimed : it->acc_auto;
            if (!acc)
                continue;
            sprintf(line, "%s\t%d%%\t%d%%", i == 0 ? "Snap" : i == 1 ? "Aimed" : "Auto x3", acc,
                    i == 0 ? it->tu_snap : i == 1 ? it->tu_aimed : it->tu_auto);
            uk_line(x, y, 300, 11, line, tabs);
            y += 9;
        }
        sprintf(line, "Damage %d", it->power);
        uk_text(x, y, UK_SMALL, 12, line);
        if (it->clip_size)
            sprintf(line, "Ammunition: %d rounds", it->clip_size);
        else
            strcpy(line, "Ammunition: unlimited");
        uk_text(x + 96, y, UK_SMALL, 12, line);
        return y + 14;
    }
    if (i < N_ITEMS && g_items[i].kind == IK_GRENADE) {
        sprintf(line, "Damage %d   Blast radius %d tiles", g_items[i].power,
                g_items[i].power / 20);
        uk_text(x, y, UK_SMALL, 12, line);
        return y + 14;
    }
    for (i = 1; i < N_ARMOURS && strcmp(g_armours[i].name, e->title); i++)
        ;
    if (i >= N_ARMOURS)
        return y;
    for (a = 0; a < 4; a++) {
        sprintf(line, "%s armour\t%d", a == 0 ? "Front" : a == 1 ? "Side" : a == 2 ? "Rear" : "Under",
                (&g_armours[i].front)[a]);
        uk_line(x, y + a * 9, 200, 11, line, tabs);
    }
    return y + 41;
}

/* The page of entry k: picture, title, stats and a page of its text (a long
   article takes several: the lines before the page are laid out but not
   drawn). Returns 1 if text is left for another page. */
static int draw_page(unsigned k, int page)
{
    const PackEntry *e = &pe[k];
    PageDef d;
    char *buf, *p;
    int y, more = 0;
    unsigned bg;

    if (!uk_read(UI_PEDIA_MAP, k * sizeof d, &d, sizeof d)) {
        memset(&d, 0, sizeof d);
        d.layout = PL_TEXT;
        d.x = 10;
        d.y = 48;
        d.w = 296;
    }
    bg = d.layout == PL_PICTURE ? d.pic : d.layout == PL_ITEM ? UI_BG_PEDIA_ITEM
       : d.layout == PL_UFO ? UI_BG_PEDIA_UFO : d.layout == PL_FACIL ? UI_BG_PEDIA_FACIL
       : UI_BG_PEDIA_TEXT;
    uk_screen(bg, T_GREEN);
    if (d.layout == PL_ITEM)
        uk_pic_in(d.pic, 160, 6);
    if (d.layout == PL_UFO)
        uk_pic(d.pic, 160, 6);          /* ArticleStateUfo: INTERWIN.DAT's UFO */
    if (d.layout == PL_FACIL) {
        /* ArticleStateBaseFacility: the facility in the 64 x 64 box at
           232, 16; one square centred, with its building on the shape. */
        int i, j, n = 0, s = d.size > 1 ? 2 : 1, o = s > 1 ? 0 : 16;
        uk_hold(1);
        for (j = 0; j < s; j++)
            for (i = 0; i < s; i++, n++) {
                uk_pic(d.pic + n, 232 + o + i * 32, 16 + o + j * 32);
                if (s == 1)
                    uk_pic(d.pic2, 232 + o, 16 + o);
            }
        uk_hold(0);
    }
    uk_button(5, BTN_Y, 30, 14, "OK", 0);
    uk_button(40, BTN_Y, 30, 14, "<<", 0);
    uk_button(75, BTN_Y, 30, 14, ">>", 0);
    uk_text(5, 24, UK_BIG, 14, e->title);
    if (d.layout != PL_UFO && d.layout != PL_FACIL)
        uk_text(5, 42, UK_SMALL, 10, sections[e->section % n_sections]);
    y = d.y;
    if (d.layout == PL_ITEM) {
        y = show_stats(e, 8, 66);
        if (y < 82)
            y = 82;
    }
    buf = malloc(e->length + 1);
    if (!buf)
        return 0;
    fseek(pak, text_base + e->offset, SEEK_SET);
    buf[fread(buf, 1, e->length, pak)] = 0;
    p = buf;
    if (strstr(buf, "\n\n")) {
        /* Baked stats lines first: in their own box (craft), else above
           the text. */
        char *nl;
        int sx = d.stats_y ? d.stats_x * 2 : d.x, sy = d.stats_y ? d.stats_y : y;
        int sw = d.stats_y && d.stats_w ? d.stats_w * 2 : d.w;
        for (; (nl = strchr(p, '\n')) != NULL && nl != p; p = nl + 1) {
            *nl = 0;
            sy = stat_line(sx, sy, sw, d.stats_y, p);
        }
        p++;
        if (!d.stats_y)
            y = sy + 5;
    }
    pedia_flow(d.x, y, d.w, p, 186, 15, page * ((186 - y) / 9 + 1), &more);
    if (more)
        uk_text(268, 190, UK_SMALL, 10, "PgDn>");
    free(buf);
    return more;
}

/* Shows entry k; Left/Right step through the section's unlocked entries,
   PgDn / Space / a click on the page and PgUp turn the pages of a long
   article. Returns when Esc is pressed. */
static void show_page(unsigned k)
{
    int redraw = 1, page = 0, more = 0;

    for (;;) {
        const PackEntry *e = &pe[k];
        unsigned key, i;

        if (redraw)
            more = draw_page(k, page);
        redraw = 0;
        key = input_key();
        if (key == KEY_RCLICK)
            return;
        if (key == KEY_LCLICK) {
            if (g_mouse_y < BTN_Y || g_mouse_y >= BTN_Y + 14 || g_mouse_x < 5 || g_mouse_x >= 105)
                key = g_mouse_y > BTN_Y + 14 ? 0x5100 : 0;
            else
                key = g_mouse_x < 38 ? 0x011B : g_mouse_x < 73 ? 0x4B00 : 0x4D00;
        }
        if ((key & 0xFF) == 27 || (key & 0xFF) == 13)
            return;
        if (((key >> 8) == 0x51 || (key & 0xFF) == ' ') && more) {
            page++;
            redraw = 1;
        } else if ((key >> 8) == 0x49 && page) {
            page--;
            redraw = 1;
        } else if ((key >> 8) == 0x4D || (key >> 8) == 0x50) {
            for (i = k + 1; i < n_pe; i++)
                if (pe[i].section == e->section && unlocked(&pe[i])) {
                    k = i;
                    page = 0;
                    redraw = 1;
                    break;
                }
        } else if ((key >> 8) == 0x4B || (key >> 8) == 0x48) {
            for (i = k; i-- > 0;)
                if (pe[i].section == e->section && unlocked(&pe[i])) {
                    k = i;
                    page = 0;
                    redraw = 1;
                    break;
                }
        }
    }
}

static const UkMenu section_menu = {
    UI_BG_MENU, T_GREEN, UKS_LIST | UKS_POPUP, 32, 10, 256, 180, 40, 50, 240, 12, 9, 24,
    NULL, NULL, NULL, "Esc=OK", 166, NULL
};

static void show_section(unsigned s, char (*lines)[41])
{
    unsigned char map[MAX_LIST];
    unsigned i;
    int n = 0, sel = 0;
    UkMenu m = section_menu;

    for (i = 0; i < n_pe && n < MAX_LIST; i++)
        if (pe[i].section == s && unlocked(&pe[i])) {
            strcpy(lines[n], pe[i].title);
            map[n++] = (unsigned char)i;
        }
    m.title = sections[s];
    for (;;) {
        uk_invalidate();
        sel = uk_menu(&m, NULL, lines, n, sel, NULL, NULL);
        if (sel < 0)
            return;
        show_page(map[sel]);
    }
}

static const UkMenu start_def = {
    UI_BG_MENU, T_GREEN, UKS_BUTTONS | UKS_POPUP, 32, 10, 256, 180, 50, 50, 220, 9, 13, 26,
    "UFOpaedia", NULL, NULL, "Esc=OK", 166, NULL
};

void pedia_screen(void)
{
    char (*lines)[41] = menu_lines();
    unsigned s;
    int sel = 0;

    uk_open();
    if (!lines || !open_pack()) {
        uk_notice("UFOPAED.PAK is missing.");
        uk_close();
        return;
    }
    for (;;) {
        for (s = 0; s < n_sections && s < MAX_LIST; s++)
            strcpy(lines[s], sections[s]);
        uk_invalidate();
        sel = uk_menu(&start_def, NULL, lines, s, sel, NULL, NULL);
        if (sel < 0)
            break;
        show_section(sel, lines);
    }
    close_pack();
    uk_close();
}

/* The article of a research topic (ResearchCompleteState's View Reports,
   Ufopaedia::openArticle): the first entry unlocked by it, else the one of
   that title. */
void pedia_article(const char *topic)
{
    unsigned i, k = 0xFFFF;

    uk_open();
    if (open_pack()) {
        for (i = 0; i < n_pe && k == 0xFFFF; i++)
            if (!strcmp(pe[i].unlock, topic))
                k = i;
        for (i = 0; i < n_pe && k == 0xFFFF; i++)
            if (!strcmp(pe[i].title, topic))
                k = i;
        if (k != 0xFFFF) {
            log_printf("PEDIA %s", pe[k].title);
            uk_invalidate();
            show_page(k);
        }
        close_pack();
    }
    uk_close();
}
