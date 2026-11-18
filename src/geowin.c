/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The geoscape's windows (see geowin.h), laid out as OpenXcom's Geoscape
   states with the DOS game's backgrounds (BACK12 for interception and
   craft, BACK15 for UFOs and the target list, BACK01 for sites). Window x
   positions are kept on whole bytes (DOS: MultipleTargetsState at 60,
   ConfirmDestinationState at 6) so the background stays inside the frame.
   Compiled with -zc: strings and tables stay in this overlay section, and
   nothing here is kept in the data group. */

#include <stdio.h>
#include <string.h>
#include "geowin.h"
#include "alien.h"
#include "base.h"
#include "craft.h"
#include "gfx.h"
#include "input.h"
#include "uik.h"
#include "uitext.h"

typedef struct { int x, y, w, h; unsigned key, key2; } Hit;

/* A popup window over the geoscape with the picture bg inside it. Packs
   converted before BACK12 / BACK15 were added end before their entries:
   BACK01 then (a full-screen picture or nothing). */
static void win_open(unsigned bg, int x, int y, int w, int h)
{
    int pw, ph;

    uk_open();
    uk_theme(T_GREEN);
    uk_save(0, 200);
    if (bg != UI_BG_MENU && !(uk_pic_box(bg, &pw, &ph) && pw == 320 && ph == 200))
        bg = UI_BG_MENU;
    if (uk_pic_clean(bg) || uk_pic_clean(UI_BG_MENU))
        uk_restore(x, y, w, h);
    else
        uk_fill(x, y, w, h, 0);
    uk_window(x, y, w, h);
}

static void btn(Hit *b, int x, int y, int w, int h, unsigned key, unsigned key2, const char *s)
{
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->key = key;
    b->key2 = key2;
    uk_button(x, y, w, h, s, 0);
}

/* A pop-up's answer: button dflt by itself under the script token AUTO
   (input.h), else wait_btn. */
static int wait_btn(const Hit *b, int n);
static int popup_btn(const Hit *b, int n, int dflt)
{
    if (g_popup_auto)
        return dflt;
    return wait_btn(b, n);
}

/* Waits for a button (click or key); -1 for Esc or a right click. */
static int wait_btn(const Hit *b, int n)
{
    for (;;) {
        unsigned key = input_key();
        int i, ch;
        if (key == KEY_RCLICK)
            return -1;
        if (key == KEY_LCLICK) {
            for (i = 0; i < n; i++)
                if (g_mouse_x >= b[i].x && g_mouse_x < b[i].x + b[i].w && g_mouse_y >= b[i].y
                    && g_mouse_y < b[i].y + b[i].h) {
                    uk_button(b[i].x, b[i].y, b[i].w, b[i].h, "", 1);
                    return i;
                }
            continue;
        }
        ch = key & 0xFF;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch == 27)
            return -1;
        for (i = 0; i < n; i++)
            if (ch && (ch == (int)b[i].key || ch == (int)b[i].key2))
                return i;
    }
}

const char *gw_name_buf(const GeoTarget *t, char *s)
{
    switch (t->kind) {
    case GT_BASE:
        sprintf(s, "%.15s", g_bhdr[t->idx].name);
        break;
    case GT_CRAFT:
        strcpy(s, craft_name(&g_craft[t->idx]));
        break;
    case GT_UFO:
        sprintf(s, t->sub == 2 ? "CRASH SITE-%d" : t->sub == 1 ? "LANDING SITE-%d" : "UFO-%d", t->id);
        break;
    case GT_SITE:
        sprintf(s, g_sites[t->idx].kind == SITE_TERROR ? "TERROR SITE-%d" : "ALIEN BASE-%d",
                t->idx + 1);
        break;
    default:
        if (t->id)
            sprintf(s, "WAY POINT-%d", t->id);
        else
            strcpy(s, "WAY POINT");
    }
    return s;
}

int gw_craft_knots(int type)
{
    static const int knots[N_CRAFT_TYPES] = { 2100, 4200, 3100, 5400, 760 };
    return knots[type];
}

/* ------------------------------------------------------------------ */
/* MultipleTargetsState: 136 wide, a 116 x 16 button per target, 4 apart. */

int gw_pick(const GeoTarget *t, int n)
{
    Hit b[GW_MAX_TARGETS];
    char name[24];
    int i, h = n * 20 - 4 + 20, y = (200 - h) / 2;

    win_open(UI_BG_UFOINFO, 64, y, 136, h);
    for (i = 0; i < n; i++) {
        btn(&b[i], 74, y + 10 + i * 20, 116, 16, '1' + i, i ? 0 : 13, gw_name_buf(&t[i], name));
        log_printf("WIN target %d: %s", i + 1, name);
    }
    i = wait_btn(b, n);
    uk_close();
    return i;
}

/* ------------------------------------------------------------------ */
/* InterceptState: window 320 x 140 at 0, 30; the list at 8, 78 (8 rows),
   columns craft, status, base, weapons / crew / HWPs. */

#define IL_X    8
#define IL_Y    78
#define IL_W    288
#define IL_ROWS 8

static int flying(const Craft *c)
{
    return c->state == CS_OUT || c->state == CS_RETURN;
}

/* InterceptState::lstCraftsLeftClick: ready, or out and not low on fuel. */
static int can_go(const Craft *c)
{
    if (c->state == CS_HANGAR)
        return craft_ready(c);
    return flying(c) && !craft_low_fuel(c);
}

static int crew(int k)
{
    int b = g_craft[k].base, i, n = 0;

    for (i = 0; i < base_roster_n(b); i++)
        n += base_soldier(b, i)->craft == k + 1;
    return n;
}

static void irow(int k, int y, int sel)
{
    const Craft *c = &g_craft[k];
    const CraftType *t = &g_craft_types[c->type];
    const char *st = flying(c) ? "OUT" : craft_status(c);
    char s[16];
    int w, n = 0, h = 0;

    uk_restore(IL_X, y, IL_W, 8);
    if (sel)
        uk_fill(IL_X, y, IL_W, 8, g_uk->sel);
    uk_text(14, y, UK_SMALL, g_uk->text, craft_name(c));
    uk_text(100, y, UK_SMALL, strcmp(st, "READY") ? g_uk->text : g_uk->title, st);
    sprintf(s, "%.14s", g_bhdr[c->base].name);
    uk_text(170, y, UK_SMALL, g_uk->text, s);
    for (w = 0; w < t->weapons; w++)
        n += c->weapon[w] != CW_NONE;
    while (h < 4 && c->hwp[h])
        h++;                    /* HWPs aboard (Craft.hwp) */
    sprintf(s, "%d/%d/%d", n, t->soldiers ? crew(k) : 0, h);
    uk_text(250, y, UK_SMALL, g_uk->text, s);
}

int gw_intercept(int base, int *craft)
{
    Hit b[2];
    int map[MAX_CRAFT], n = 0, i, sel = 0, top = 0, shown = -1, nb, r = GW_CANCEL;

    for (i = 0; i < MAX_CRAFT; i++)
        if (g_craft[i].state != CS_EMPTY && (base < 0 || g_craft[i].base == base))
            map[n++] = i;
    win_open(UI_BG_INTERCEPT, 0, 30, 320, 140);
    uk_center(0, 320, 46, UK_BIG, g_uk->title, "LAUNCH INTERCEPTION");
    uk_text(14, 70, UK_SMALL, g_uk->text, "CRAFT");
    uk_text(100, 70, UK_SMALL, g_uk->text, "STATUS");
    uk_text(170, 70, UK_SMALL, g_uk->text, "BASE");
    uk_text(238, 62, UK_SMALL, g_uk->text, "WEAPONS/");
    uk_text(238, 70, UK_SMALL, g_uk->text, "CREW/HWPs");
    if (base >= 0) {
        btn(&b[0], 16, 146, 142, 16, 0, 0, "CANCEL");
        btn(&b[1], 162, 146, 142, 16, 'b', 'g', "Go to Base");
        nb = 2;
    } else {
        btn(&b[0], 16, 146, 288, 16, 0, 0, "CANCEL");
        nb = 1;
    }
    uk_save(0, 200);                    /* the clean window, for the rows */
    for (;;) {
        unsigned key;
        int ch, scan;
        if (sel < top)
            top = sel;
        if (sel >= top + IL_ROWS)
            top = sel - IL_ROWS + 1;
        if (shown != top * 100 + sel) {
            for (i = 0; i < IL_ROWS; i++)
                if (top + i < n)
                    irow(map[top + i], IL_Y + i * 8, top + i == sel);
            shown = top * 100 + sel;
        }
        key = input_key();
        if (IS_CLICK(key)) {
            int row = (g_mouse_y - IL_Y) / 8;
            if (g_mouse_y >= IL_Y && row < IL_ROWS && top + row < n && g_mouse_x >= IL_X
                && g_mouse_x < IL_X + IL_W) {
                const Craft *c = &g_craft[map[top + row]];
                sel = top + row;
                if (key == KEY_RCLICK) {
                    if (flying(c)) {
                        r = GW_CENTRE;
                        break;
                    }
                } else if (can_go(c)) {
                    r = GW_OK;
                    break;
                }
                continue;
            }
            if (key == KEY_RCLICK)
                break;
            for (i = 0; i < nb; i++)
                if (g_mouse_x >= b[i].x && g_mouse_x < b[i].x + b[i].w && g_mouse_y >= b[i].y
                    && g_mouse_y < b[i].y + 16)
                    break;
            if (i == 0)
                break;
            if (i == 1) {
                r = GW_GOTO_BASE;
                break;
            }
            continue;
        }
        ch = key & 0xFF;
        scan = key >> 8;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        if (ch == 27)
            break;
        if (nb == 2 && (ch == 'b' || ch == 'g')) {
            r = GW_GOTO_BASE;
            break;
        }
        if (scan == 0x48 && sel > 0)
            sel--;
        else if (scan == 0x50 && sel < n - 1)
            sel++;
        else if (ch == 13 && n && can_go(&g_craft[map[sel]])) {
            r = GW_OK;
            break;
        }
    }
    if (n)
        *craft = map[sel];
    uk_close();
    log_printf("WIN Launch Interception: %d craft, %s", n,
               r == GW_OK ? craft_name(&g_craft[map[sel]]) : r == GW_GOTO_BASE ? "Go to Base"
               : r == GW_CENTRE ? "centre" : "CANCEL");
    return r;
}

/* ------------------------------------------------------------------ */
/* GeoscapeCraftState: window 240 x 184 at 8, 8. */

static const char *const alt_names[5] = { "GROUND", "VERY LOW", "LOW", "HIGH", "VERY HIGH" };

/* "Label>" and the value in the second colour. */
static void pair(int x, int y, const char *label, const char *value)
{
    uk_text(uk_text(x, y, UK_SMALL, g_uk->text, label), y, UK_SMALL, g_uk->title, value);
}

static void pair_n(int x, int y, const char *label, const char *fmt, int v)
{
    char s[12];

    sprintf(s, fmt, v);
    pair(x, y, label, s);
}

int gw_craft(int k, int status, const GeoTarget *dest, int alt, int knots)
{
    const Craft *c = &g_craft[k];
    const CraftType *t = &g_craft_types[c->type];
    Hit b[4];
    char s[48], name[24];
    int i, nb = 0;
    static const unsigned char res[4] = { GW_RETURN, GW_TARGET, GW_PATROL, GW_CANCEL };

    win_open(UI_BG_INTERCEPT, 8, 8, 240, 184);
    uk_text(32, 20, UK_BIG, g_uk->title, craft_name(c));
    switch (status) {
    case GWS_RETURN: strcpy(s, "RETURNING TO BASE"); break;
    case GWS_LOW_FUEL: strcpy(s, "LOW FUEL - RETURNING TO BASE"); break;
    case GWS_PATROL: strcpy(s, "PATROLLING"); break;
    case GWS_LOST:
    case GWS_INTERCEPT: sprintf(s, "INTERCEPTING UFO-%d", dest->id); break;
    default: sprintf(s, "DESTINATION: %s", gw_name_buf(dest, name));
    }
    pair(32, 38, "STATUS>", s);
    log_printf("WIN %s: %s", craft_name(c), s);
    sprintf(s, "%.15s", g_bhdr[c->base].name);
    pair(32, 52, "BASE>", s);
    pair_n(32, 60, "SPEED>", "%d", knots);
    pair_n(32, 68, "MAXIMUM SPEED>", "%d", gw_craft_knots(c->type));
    pair(32, 76, "ALTITUDE>", alt_names[alt > 0 && alt < 5 ? alt : 1]);
    pair_n(32, 84, "FUEL>", "%d%%", (int)((long)c->fuel * 100 / t->fuel));
    pair_n(164, 84, "DAMAGE>", "%d%%", (int)((long)c->damage * 100 / t->hp));
    for (i = 0; i < 2; i++) {
        int w = i < t->weapons ? c->weapon[i] : CW_NONE;
        pair(32, 92 + i * 8, i ? "WEAPON-2>" : "WEAPON-1>",
             w == CW_NONE ? "NONE" : g_craft_weapons[w].name);
        if (w != CW_NONE)
            pair_n(164, 92 + i * 8, "ROUNDS>", "%d", c->ammo[i]);
    }
    if (t->soldiers)
        pair_n(164, 68, "SOLDIERS>", "%d", crew(k));
    if (t->vehicles) {
        for (i = 0; i < 4 && c->hwp[i]; i++)
            ;
        pair_n(164, 76, "HWPs>", "%d", i);
    }
    if (status != GWS_LOW_FUEL) {
        btn(&b[nb++], 22, 124, 212, 12, 'r', 0, "RETURN TO BASE");
        btn(&b[nb++], 22, 140, 212, 12, 't', 's', "SELECT NEW TARGET");
        btn(&b[nb++], 22, 156, 212, 12, 'p', 0, "PATROL");
    }
    btn(&b[nb++], 22, 172, 212, 12, 'c', 13,
        status == GWS_LOST ? "GO TO LAST KNOWN UFO POSITION" : "CANCEL");
    i = status == GWS_LOST ? popup_btn(b, nb, 3) : wait_btn(b, nb);
    uk_close();
    if (i < 0)
        return GW_CANCEL;
    return status == GWS_LOW_FUEL ? GW_CANCEL : res[i];
}

/* ------------------------------------------------------------------ */
/* UfoDetectedState: 224 x 128 at 16, 44 (224 x 180 at 16, 10 with the
   hyper-wave decoder); lists with dotted columns at 77. */

static const char *const size_names[5] = { "VERY SMALL", "SMALL", "MEDIUM", "LARGE",
                                           "VERY LARGE" };
static const char *const headings[8] = { "NORTH", "NORTH EAST", "EAST", "SOUTH EAST",
                                         "SOUTH", "SOUTH WEST", "WEST", "NORTH WEST" };

static void dotted(int y, const char *label, const char *value)
{
    int x = uk_text(28, y, UK_SMALL, g_uk->text, label), dot = uk_width(UK_SMALL, ".");

    for (x += 2; dot > 0 && x + dot <= 28 + 75; x += dot)
        uk_text(x, y, UK_SMALL, g_uk->text, ".");
    uk_text(28 + 77, y, UK_SMALL, g_uk->title, value);
}

/* 8 compass points from a direction on the map (y down). */
static int heading(int dx, int dy)
{
    long ax = dx < 0 ? -(long)dx : dx, ay = dy < 0 ? -(long)dy : dy;

    if (ax * 5 > ay * 12)
        return dx > 0 ? 2 : 6;
    if (ay * 5 > ax * 12)
        return dy < 0 ? 0 : 4;
    return dx > 0 ? (dy < 0 ? 1 : 3) : (dy < 0 ? 7 : 5);
}

int gw_ufo(const GwUfo *u)
{
    Hit b[3];
    char name[24], s[12];
    int y = u->decoded ? 10 : 44, ly = u->decoded ? 60 : 80, by = u->decoded ? 136 : 118, i;
    static const unsigned char res[3] = { GW_INTERCEPT, GW_CENTRE, GW_CANCEL };

    win_open(UI_BG_UFOINFO, 16, y, 224, u->decoded ? 180 : 128);
    uk_text(28, u->decoded ? 20 : 53, UK_BIG, g_uk->title, gw_name_buf(&u->t, name));
    log_printf("WIN %s%s: %s, %s, %d knots", name, u->detected ? " detected" : "",
               size_names[u->size], alt_names[u->alt], u->knots);
    if (u->detected)
        uk_text(28, u->decoded ? 36 : 69, UK_SMALL, g_uk->text, "Detected");
    if (u->decoded)
        uk_center(21, 214, 44, UK_SMALL, g_uk->title, "HYPER-WAVE TRANSMISSIONS ARE DECODED");
    dotted(ly, "SIZE", size_names[u->size]);
    dotted(ly + 8, "ALTITUDE", alt_names[u->alt]);
    dotted(ly + 16, "HEADING", u->dx || u->dy ? headings[heading(u->dx, u->dy)] : "NONE");
    sprintf(s, "%d", u->knots);
    dotted(ly + 24, "SPEED", s);
    if (u->decoded) {
        dotted(96, "CRAFT TYPE", u->type);
        dotted(104, "RACE", u->race);
        dotted(112, "MISSION", u->mission);
        dotted(120, "ZONE", u->zone);
    }
    btn(&b[0], 28, by, 200, 12, 'i', 0, "INTERCEPT");
    btn(&b[1], 28, by + 16, 200, 12, 'c', 0, "CENTRE ON UFO-TIME=5 Secs");
    btn(&b[2], 28, by + 32, 200, 12, 13, 0, "CANCEL");
    i = u->detected ? popup_btn(b, 3, 2) : wait_btn(b, 3);
    uk_close();
    if (u->detected)
        log_printf("WIN %s: %s", name, i == 0 ? "Intercept" : i == 1 ? "Centre" : "Cancel");
    return i < 0 ? GW_CANCEL : res[i];
}

/* ------------------------------------------------------------------ */
/* TargetInfoState: 192 x 120 at 32, 40. */

int gw_site(const GeoTarget *t, const char *place, const int *craft, int n)
{
    Hit b[2];
    char name[24];
    int i;

    win_open(UI_BG_MENU, 32, 40, 192, 120);
    uk_center(37, 182, 50, UK_BIG, g_uk->title, gw_name_buf(t, name));
    log_printf("WIN %s: %s, %d craft on the way", name, place, n);
    uk_center(37, 182, 67, UK_SMALL, g_uk->title, place);
    uk_center(37, 182, 78, UK_SMALL, g_uk->text, "TARGETTED BY:");
    for (i = 0; i < n && i < 4; i++)
        uk_center(37, 182, 88 + i * 8, UK_SMALL, g_uk->text, craft_name(&g_craft[craft[i]]));
    btn(&b[0], 48, 124, 160, 12, 'i', 0, "INTERCEPT");
    btn(&b[1], 48, 140, 160, 12, 13, 'o', "OK");
    i = wait_btn(b, 2);
    uk_close();
    return i == 0 ? GW_INTERCEPT : GW_CANCEL;
}

/* ------------------------------------------------------------------ */
/* ConfirmDestinationState: 244 x 72 at 6, 64 (here 240 at 8). */

int gw_confirm(const GeoTarget *t)
{
    Hit b[2];
    char s[40], name[24];
    int i;

    win_open(UI_BG_INTERCEPT, 8, 64, 240, 72);
    sprintf(s, "TARGET: %s", gw_name_buf(t, name));
    uk_center(12, 232, 80, UK_BIG, g_uk->title, s);
    btn(&b[0], 68, 104, 50, 12, 13, 'y', "OK");
    btn(&b[1], 138, 104, 50, 12, 'n', 0, "CANCEL");
    i = wait_btn(b, 2);
    uk_close();
    log_printf("WIN %s: %s", s, i == 0 ? "OK" : "Cancel");
    return i == 0;
}

/* ------------------------------------------------------------------ */
/* SelectDestinationState: DOS puts a 256 x 28 window over the top of the
   globe; here it takes the status lines under the map, so the whole map
   stays clickable. */

void gw_select_bar(int cydonia)
{
    uk_open();
    uk_theme(T_GREEN);
    uk_save(0, 200);
    if (uk_pic_clean(UI_BG_MENU))
        uk_restore(0, 160, 256, 40);
    else
        uk_fill(0, 160, 256, 40, 0);
    uk_window(0, 160, 256, 40);
    uk_text(10, 176, UK_SMALL, g_uk->text, "SELECT DESTINATION");
    uk_button(124, 174, 60, 12, "CANCEL", 0);
    if (cydonia)
        uk_button(194, 174, 60, 12, "CYDONIA", 0);
    uk_close();
    log_printf("WIN Select Destination%s", cydonia ? ", Cydonia" : "");
}

int gw_select_click(void)
{
    if (g_mouse_y < 174 || g_mouse_y >= 186)
        return 0;
    return g_mouse_x >= 124 && g_mouse_x < 184 ? 1 : g_mouse_x >= 194 && g_mouse_x < 254 ? 2 : 0;
}

static void lines(int x, int w, int y, int dy, int font, int c, const char *s);

/* ConfirmCydoniaState: 256 x 160 at 32, 20 on BACK12, Yes / No. */
int gw_cydonia(void)
{
    Hit b[2];
    char s[80];
    int i;

    win_open(UI_BG_INTERCEPT, 32, 20, 256, 160);
    ui_text(UI_MSG, s, sizeof s, MSG_CYDONIA);
    lines(48, 224, 76, 16, UK_BIG, g_uk->title, s);
    btn(&b[0], 70, 142, 80, 20, 13, 'y', "YES");
    btn(&b[1], 170, 142, 80, 20, 'n', 0, "NO");
    i = wait_btn(b, 2);
    uk_close();
    log_printf("WIN Cydonia: %s", i == 0 ? "Yes" : "No");
    return i == 0;
}

/* ------------------------------------------------------------------ */
/* The pop-ups of the time steps. */

/* Lines of s (split at '\n') centred in [x, x + w) from y, dy apart. */
static void lines(int x, int w, int y, int dy, int font, int c, const char *s)
{
    char buf[40];
    int n;

    for (;;) {
        for (n = 0; s[n] && s[n] != '\n' && n < (int)sizeof buf - 1; n++)
            buf[n] = s[n];
        buf[n] = 0;
        uk_center(x, w, y, font, c, buf);
        if (s[n] != '\n')
            break;
        s += n + 1;
        y += dy;
    }
}

/* UfoLostState: 192 x 104 at 32, 48. */
void gw_lost(int id)
{
    Hit b;
    char s[32];

    win_open(UI_BG_UFOINFO, 32, 48, 192, 104);
    sprintf(s, "UFO-%d\nTRACKING LOST", id);
    lines(48, 160, 72, 16, UK_BIG, g_uk->title, s);
    log_printf("WIN UFO-%d: Tracking Lost", id);
    btn(&b, 98, 112, 60, 12, 13, 'o', "OK");
    popup_btn(&b, 1, 0);
    uk_close();
}

/* CraftPatrolState: 224 x 168 at 16, 16. */
int gw_patrol(int craft, const GeoTarget *dest)
{
    Hit b[2];
    char s[64], name[24];
    int i;

    win_open(UI_BG_INTERCEPT, 16, 16, 224, 168);
    sprintf(s, "%s\nhas reached\n%s", craft_name(&g_craft[craft]), gw_name_buf(dest, name));
    lines(16, 224, 56, 16, UK_BIG, g_uk->title, s);
    uk_center(16, 224, 120, UK_BIG, g_uk->title, "Now patrolling");
    btn(&b[0], 58, 144, 140, 12, 'o', 0, "OK");
    btn(&b[1], 58, 160, 140, 12, 13, 'r', "REDIRECT CRAFT");
    i = popup_btn(b, 2, 0);
    uk_close();
    log_printf("WIN %s has reached %s: %s", craft_name(&g_craft[craft]), name,
               i == 1 ? "REDIRECT CRAFT" : "OK");
    return i == 1;
}

/* LowFuelState: 224 x 120 at 16, 40. */
int gw_low_fuel(int craft)
{
    Hit b[2];
    char s[40];
    int i;

    win_open(UI_BG_INTERCEPT, 16, 40, 224, 120);
    uk_center(21, 214, 60, UK_BIG, g_uk->title, craft_name(&g_craft[craft]));
    ui_text(UI_MSG, s, sizeof s, MSG_LOW_FUEL);
    lines(21, 214, 90, 9, UK_SMALL, g_uk->text, s);
    btn(&b[0], 30, 120, 90, 18, 'o', 0, "OK");
    btn(&b[1], 136, 120, 90, 18, 13, '5', "OK - 5 secs");
    i = popup_btn(b, 2, 0);
    uk_close();
    log_printf("WIN %s is low on fuel: %s", craft_name(&g_craft[craft]), i == 1 ? "OK - 5 Secs" : "OK");
    return i == 1;
}

/* CraftErrorState: 256 x 160 at 32, 20; "Not enough <item> to rearm|refuel
   <craft> at <base>" in a 246 x 96 box at 37, 42, wrapped and centred. */
int gw_craft_error(int craft, int item)
{
    Hit b[2];
    char msg[96], ln[5][40], t[48];
    const char *p = msg;
    int i, n = 0;

    win_open(UI_BG_INTERCEPT, 32, 20, 256, 160);
    sprintf(msg, "Not enough %s to %s %s at %.16s", g_items[item].name,
            item == IT_ELERIUM ? "refuel" : "rearm", craft_name(&g_craft[craft]),
            g_bhdr[g_craft[craft].base].name);
    ln[0][0] = 0;
    for (;;) {
        char w[24];
        int k = 0;
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        while (*p && *p != ' ' && k < 23)
            w[k++] = *p++;
        w[k] = 0;
        sprintf(t, ln[n][0] ? "%s %s" : "%s%s", ln[n], w);
        if (ln[n][0] && n < 4 && uk_width(UK_BIG, t) > 240)
            strcpy(ln[++n], w);
        else
            strcpy(ln[n], t);
    }
    for (i = 0; i <= n; i++)
        uk_center(37, 246, 90 - 8 * (n + 1) + 16 * i, UK_BIG, g_uk->title, ln[i]);
    btn(&b[0], 48, 150, 100, 18, 'o', 0, "OK");
    btn(&b[1], 172, 150, 100, 18, 13, '5', "OK - 5 secs");
    i = popup_btn(b, 2, 0);
    uk_close();
    log_printf("WIN %s: %s", msg, i == 1 ? "OK - 5 Secs" : "OK");
    return i == 1;
}

/* MissionDetectedState: 256 x 200 at 0, 0. */
int gw_terror(const char *city)
{
    Hit b[3];
    int i;
    static const unsigned char res[3] = { GW_INTERCEPT, GW_CENTRE, GW_CANCEL };

    win_open(UI_BG_TERROR, 0, 0, 256, 200);    /* BACK03 (BACK01 in older packs) */
    uk_center(5, 246, 56, UK_BIG, g_uk->title, "ALIENS TERRORISE");
    uk_center(5, 246, 80, UK_BIG, g_uk->title, city);
    btn(&b[0], 28, 130, 200, 16, 'i', 0, "INTERCEPT");
    btn(&b[1], 28, 150, 200, 16, 'c', 0, "CENTRE ON SITE-TIME=5 Secs");
    btn(&b[2], 28, 170, 200, 16, 0, 0, "CANCEL");
    i = popup_btn(b, 3, 2);
    uk_close();
    if (i < 0)
        i = 2;
    log_printf("WIN Aliens Terrorize %s: %s", city, i == 0 ? "Intercept" : i == 1 ? "Centre" : "Cancel");
    return res[i];
}

/* AlienBaseState: full screen. */
void gw_base_found(const char *region)
{
    Hit b;
    char s[96], t[48];

    win_open(UI_BG_REPORT, 0, 0, 320, 200);   /* AlienBaseState: BACK13, pink */
    ui_text(UI_MSG, t, sizeof t, MSG_BASE_FOUND);
    sprintf(s, "%s\n%s", t, region);
    lines(0, 320, 64, 16, UK_BIG, g_uk->title, s);
    log_printf("WIN Alien base located in %s", region);
    btn(&b, 136, 180, 48, 12, 13, 'o', "OK");
    popup_btn(&b, 1, 0);
    uk_close();
}

/* ConfirmLandingState: 216 x 160 at 20, 20 (here 224 at 16). */
int gw_landing(int craft, const GeoTarget *dest)
{
    Hit b[2];
    char s[96], name[24], t[24];
    int i;

    win_open(UI_BG_UFOINFO, 16, 20, 224, 160);
    ui_text(UI_MSG, t, sizeof t, MSG_LAND_NEAR);
    sprintf(s, "%s\n%s\n%s", craft_name(&g_craft[craft]), t, gw_name_buf(dest, name));
    lines(16, 224, 44, 16, UK_BIG, g_uk->title, s);
    uk_center(16, 224, 130, UK_BIG, g_uk->text, "Begin Mission?");
    btn(&b[0], 40, 150, 80, 20, 13, 'y', "YES");
    btn(&b[1], 136, 150, 80, 20, 'n', 0, "NO");
    i = popup_btn(b, 2, 0);
    uk_close();
    log_printf("WIN %s ready to land near %s: %s", craft_name(&g_craft[craft]), name,
               i == 0 ? "YES" : "NO");
    return i == 0;
}
