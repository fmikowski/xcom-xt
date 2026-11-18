/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lists with scroll bars and quantity arrows (qlist.h): the base screens'
   long lists and the screens that move quantities. A geoscape overlay
   section of its own, loaded next to the geoscape and the screen that
   calls it (-zc: no named tables here, see docs/design.md). */

#include <dos.h>
#include <string.h>
#include "gfx.h"
#include "hud.h"
#include "input.h"
#include "qlist.h"

#define BAR_W       10
#define BAR_RES     16          /* the text is this much narrower beside a bar */
#define GROUP_W     37          /* three buttons and two gaps */
#define PANEL_W     48
/* The arrow columns, moved left by g_ql_shift (qlist.h) from the base's
   place: the decrease buttons' panel (whole bytes), the increase buttons'. */
#define DEC_X       (QL_DEC_X + g_ql_shift)
#define INC_X       (QL_INC_X + g_ql_shift)
#define PANEL1_X    (184 + g_ql_shift)
#define PANEL2_X    (256 + g_ql_shift)

int g_ql_shift;

/* What a click or a held button does (act). */
#define A_STEP      1
#define A_UP        2
#define A_DOWN      3
#define A_PGUP      4
#define A_PGDN      5

/* A triangle three pixels wide in a button 8 high at y. */
static void tri(int x, int y, int dir)
{
    int i;

    for (i = 0; i < 3; i++)
        uk_fill(dir > 0 ? x + i : x + 2 - i, y + 1 + i, 1, 6 - 2 * i, g_uk->btext);
}

/* A button with k triangles (4k + 3 wide). */
static void abutton(int x, int y, int k, int dir)
{
    int w = 4 * k + 3, j;

    uk_fill(x, y, w, 8, g_uk->btn[0]);
    uk_fill(x + 1, y + 1, w - 2, 6, g_uk->btn[2]);
    for (j = 0; j < k; j++)
        tri(x + 2 + 4 * j, y, dir);
}

/* One row's buttons on their two solid panels (whole bytes). */
static void arrows_row(int y, int h)
{
    uk_fill(PANEL1_X, y, PANEL_W, h, 0);
    uk_fill(PANEL2_X, y, PANEL_W, h, 0);
    abutton(DEC_X, y, 3, -1);
    abutton(DEC_X + 17, y, 2, -1);
    abutton(DEC_X + 30, y, 1, -1);
    abutton(INC_X, y, 1, 1);
    abutton(INC_X + 9, y, 2, 1);
    abutton(INC_X + 22, y, 3, 1);
}

/* The step a click at x asks for (+-1, +-10, +-QL_ALL), or 0. */
static int arrow_hit(int x)
{
    if (x >= DEC_X && x < DEC_X + GROUP_W) {
        x -= DEC_X;
        return x < 16 ? -QL_ALL : x < 29 ? -10 : -1;
    }
    if (x >= INC_X && x < INC_X + GROUP_W) {
        x -= INC_X;
        return x < 8 ? 1 : x < 21 ? 10 : QL_ALL;
    }
    return 0;
}

/* The scroll bar: the buttons are part of the clean screen, the thumb is
   drawn over the track. */
static void bar_buttons(const UkMenu *m)
{
    int cx = QL_BAR_X + 4, y0 = m->ly, y1 = m->ly + m->rows * m->rowh - 10, i;

    uk_button(QL_BAR_X, y0, BAR_W, 10, NULL, 0);
    uk_button(QL_BAR_X, y1, BAR_W, 10, NULL, 0);
    for (i = 0; i < 3; i++) {
        uk_fill(cx - i, y0 + 3 + i, 2 * i + 1, 1, g_uk->btext);
        uk_fill(cx - 2 + i, y1 + 3 + i, 5 - 2 * i, 1, g_uk->btext);
    }
}

static void thumb(const UkMenu *m, int n, int top, int *ty, int *th)
{
    int track = m->rows * m->rowh - 20, h = (int)((long)track * m->rows / n), range = n - m->rows;

    if (h < 8)
        h = 8;
    *th = h;
    *ty = m->ly + 10 + (range > 0 ? (int)((long)(track - h) * top / range) : 0);
}

static void bar_thumb(const UkMenu *m, int n, int top)
{
    int ty, th, track = m->rows * m->rowh - 20;

    uk_fill(QL_BAR_X, m->ly + 10, BAR_W, track, g_uk->btn[3]);
    thumb(m, n, top, &ty, &th);
    uk_button(QL_BAR_X, ty, BAR_W, th, NULL, 0);
}

static void draw_row(const UkMenu *m, char lines[][41], int n, int top, int i, int sel, int tw,
                     int arrows)
{
    int y = m->ly + (i - top) * m->rowh;

    if (i < top || i >= top + m->rows)
        return;
    uk_restore(m->lx, y, tw, m->rowh);
    if (i >= n)
        return;
    if (arrows && g_vga)
        arrows_row(y, m->rowh);     /* VGA's restore reads the picture again */
    if (i == sel) {
        if (arrows) {
            /* Not over the buttons' panels. */
            uk_fill(m->lx, y, PANEL1_X - m->lx, m->rowh, g_uk->sel);
            uk_fill(PANEL1_X + PANEL_W, y, PANEL2_X - PANEL1_X - PANEL_W, m->rowh, g_uk->sel);
        } else
            uk_fill(m->lx, y, tw, m->rowh, g_uk->sel);
    }
    {
        const char *s = lines[i];
        int c = *s == 2 ? 15 : *s == 3 ? g_uk->title : g_uk->list;
        if (*s == 2 || *s == 3)
            s++;
        uk_line(m->lx + 2, y, tw - 4, i == sel ? g_uk->hilite : c, s, m->tabs);
    }
}

/* The window, picture, titles and buttons, and what stays the same while
   the list is used (bar buttons, arrows) into the clean screen. */
static void frame(const UkMenu *m, int n, int scroll, int arrows)
{
    int wx = m->ww ? m->wx : 0, ww = m->ww ? m->ww : 320, i;

    uk_theme(m->theme);
    uk_screen(m->bg, m->theme);
    if (m->ww)
        uk_window(m->wx, m->wy, m->ww, m->wh);
    if (m->title)
        uk_center(wx, ww, m->ty, UK_BIG, g_uk->title, m->title);
    if (m->header)
        uk_line(m->lx + 2, m->ly - 10, m->lw - 4, g_uk->text, m->header, m->tabs);
    if (m->buttons)
        uk_buttons(m->buttons, wx + 8, ww - 16, m->by);
    if (m->extra)
        m->extra();
    if (g_video == VIDEO_CGA) {
        /* Four colours: the picture under a list at half strength. */
        int y;
        for (y = m->ly; y < m->ly + m->rows * m->rowh; y++) {
            unsigned char far *d = (unsigned char far *)MK_FP(0xB800, (y & 1) * 0x2000
                                                              + (y >> 1) * 80 + (m->lx >> 2));
            unsigned char k = (unsigned char)(y & 1 ? 0x33 : 0xCC);
            int b;
            for (b = 0; b < m->lw / 4; b++)
                d[b] &= k;
        }
    }
    if (scroll)
        bar_buttons(m);
    if (arrows && n > 0) {
        /* The first row's buttons on two solid panels (whole bytes), copied
           down through the latches: far cheaper than drawing every row's
           fifty rectangles. */
        arrows_row(m->ly, m->rowh);
        for (i = 1; i < n && i < m->rows; i++) {
            uk_copy(PANEL1_X, m->ly, PANEL1_X, m->ly + i * m->rowh, PANEL_W, m->rowh);
            uk_copy(PANEL2_X, m->ly, PANEL2_X, m->ly + i * m->rowh, PANEL_W, m->rowh);
        }
    }
    uk_save(0, 200);
}

/* Type-ahead as uk_menu's: letters jump to the next line that starts with
   what was typed. */
#define TYPED_MAX 12

static int line_starts(const char *s, const char *p, int len)
{
    int i;

    while (*s == ' ' || *s == '*')
        s++;
    for (i = 0; i < len; i++) {
        int c = s[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (!c || c != p[i])
            return 0;
    }
    return 1;
}

static int type_ahead(char lines[][41], int n, int sel, char *typed, int *len, int ch)
{
    int i, k;

    if (*len < TYPED_MAX)
        typed[(*len)++] = (char)ch;
    for (i = 0; i < n; i++) {
        k = (sel + i) % n;
        if (line_starts(lines[k], typed, *len))
            return k;
    }
    *len = 0;
    if (ch < 'a' || ch > 'z')
        return sel;
    typed[(*len)++] = (char)ch;
    for (i = 1; i <= n; i++) {
        k = (sel + i) % n;
        if (line_starts(lines[k], typed, 1))
            return k;
    }
    *len = 0;
    return sel;
}

int ql_menu(const UkMenu *m, const char *info, char lines[][41], int n, int sel,
            UkKey on_key, QlStep on_step)
{
    char typed[TYPED_MAX], shown_info[48];
    int ntyped = 0, top = 0, shown_top = -1, shown_sel = -1, follow = 1, i;
    int rows = m->rows, scroll = n > rows, arrows = on_step != NULL;
    int wx = m->ww ? m->wx : 0, ww = m->ww ? m->ww : 320;
    int tw = m->lw - (scroll || arrows ? BAR_RES : 0);
    int rep = 0, rep_first = 0, rep_row = 0, rep_d = 0;

    uk_open();
    uk_invalidate();
    if (sel >= n)
        sel = n ? n - 1 : 0;
    frame(m, n, scroll, arrows);
    shown_info[0] = 0;
    for (;;) {
        unsigned key;
        int ch, scan, act = 0, act_row = 0, act_d = 0, r;

        if (info && strcmp(info, shown_info)) {
            uk_restore(wx + 6, m->ty + 17, ww - 12, 10);
            if (m->style & UKS_LEFT)
                uk_line(m->lx + 2, m->ty + 17, m->lw - 4, g_uk->title, info, m->tabs);
            else
                uk_center(wx, ww, m->ty + 18, UK_SMALL, g_uk->text, info);
            strncpy(shown_info, info, sizeof shown_info - 1);
            shown_info[sizeof shown_info - 1] = 0;
        }
        if (follow) {
            if (sel < top)
                top = sel;
            if (sel >= top + rows)
                top = sel - rows + 1;
            follow = 0;
        }
        if (top > n - rows)
            top = n - rows;
        if (top < 0)
            top = 0;
        if (top != shown_top) {
            for (i = top; i < top + rows; i++)
                draw_row(m, lines, n, top, i, sel, tw, arrows);
            if (!n)
                uk_text(m->lx + 2, m->ly, UK_SMALL, g_uk->dim, "(nothing)");
            if (scroll)
                bar_thumb(m, n, top);
            shown_top = top;
        } else if (sel != shown_sel) {
            draw_row(m, lines, n, top, shown_sel, sel, tw, arrows);
            draw_row(m, lines, n, top, sel, sel, tw, arrows);
        }
        shown_sel = sel;

        if (rep) {
            /* The button of the last click is still down. */
            if (input_held(rep_first ? 250 : 50)) {
                rep_first = 0;
                act = rep;
                act_row = rep_row;
                act_d = rep_d;
            } else
                rep = 0;
        }
        if (!act) {
            key = input_key();
            if (key == KEY_RCLICK)
                key = 0x011B;
            else if (key == KEY_LCLICK) {
                int x = g_mouse_x, y = g_mouse_y;

                g_uk_row = -1;
                if (y >= m->ly && y < m->ly + rows * m->rowh) {
                    int row = top + (y - m->ly) / m->rowh;
                    if (scroll && x >= QL_BAR_X && x < QL_BAR_X + BAR_W) {
                        int ty, th;
                        thumb(m, n, top, &ty, &th);
                        act = y < m->ly + 10 ? A_UP : y >= m->ly + rows * m->rowh - 10 ? A_DOWN
                            : y < ty ? A_PGUP : y >= ty + th ? A_PGDN : 0;
                        if (act == A_UP || act == A_DOWN) {
                            rep = act;
                            rep_first = 1;
                        }
                        key = 0;
                    } else if (x >= (wx + 3 < m->lx ? wx + 3 : m->lx) && x < m->lx + tw && row < n) {
                        g_uk_row = row;
                        act_d = arrows ? arrow_hit(x) : 0;
                        if (act_d) {
                            sel = act_row = row;
                            act = A_STEP;
                            if (act_d != QL_ALL && act_d != -QL_ALL) {
                                rep = A_STEP;
                                rep_first = 1;
                                rep_row = row;
                                rep_d = act_d;
                            }
                            key = 0;
                        } else {
                            /* A click on the selected row is Enter (not
                               with arrows: they are the way to change). */
                            int enter = !arrows && (on_key == NULL || row == sel);
                            sel = row;
                            if (m->style & UKS_CLICK)
                                key = arrows || (m->style & UKS_TOGGLE) ? 0x3920 : 0x1C0D;
                            else if (!enter)
                                continue;
                            else
                                key = 0x1C0D;
                        }
                    } else
                        continue;
                } else if (m->buttons && (key = uk_buttons_key(m->buttons, wx + 8, ww - 16, m->by)) != 0)
                    ;
                else
                    continue;
            }
        }
        if (!act) {
            if (!key)
                continue;
            ch = key & 0xFF;
            scan = key >> 8;
            if (ch >= 'A' && ch <= 'Z')
                ch += 32;
            if (ch == 27) {
                sel = -1;
                break;
            }
            if (n && ((ch >= 'a' && ch <= 'z') || (ntyped && (ch == ' ' || ch == '-')))) {
                /* The screen's own keys first, else type-ahead. */
                r = on_key ? on_key(sel, ch, scan) : 0;
                if (r == 2)
                    break;
                if (r == 1) {
                    shown_top = -1;
                    ntyped = 0;
                } else {
                    sel = type_ahead(lines, n, sel, typed, &ntyped, ch);
                    follow = 1;
                }
                continue;
            }
            ntyped = 0;
            if (scan == 0x48 && sel > 0) {
                sel--;
                follow = 1;
            } else if (scan == 0x50 && sel < n - 1) {
                sel++;
                follow = 1;
            } else if (scan == 0x49) {
                sel = sel > rows ? sel - rows : 0;
                follow = 1;
            } else if (scan == 0x51 && n) {
                sel = sel + rows < n ? sel + rows : n - 1;
                follow = 1;
            } else if (n && arrows
                       && (act_d = ch == 13 || ch == '+' || ch == '=' || scan == 0x4D ? 1
                                 : ch == '-' || scan == 0x4B ? -1 : ch == '.' || ch == '>' ? 10
                                 : ch == ',' || ch == '<' ? -10 : scan == 0x4F ? QL_ALL
                                 : scan == 0x47 ? -QL_ALL : 0) != 0) {
                act = A_STEP;
                act_row = sel;
                follow = 1;
            } else if (ch == 13 && n)
                break;
            else if (on_key && (n || (m->style & UKS_KEYS0))) {
                r = on_key(sel, ch, scan);
                if (r == 2)
                    break;
                if (r == 1)
                    shown_top = -1;
            }
        }
        if (act == A_STEP) {
            r = on_step(act_row, act_d);
            if (r == 2) {
                sel = act_row;
                break;
            }
            if (r == 4) {
                sel = g_uk_row;
                follow = 1;
            }
            if (r >= 3)
                shown_top = -1;
            else if (r == 1)
                draw_row(m, lines, n, top, act_row, sel, tw, arrows);
        } else if (act == A_UP)
            top--;
        else if (act == A_DOWN)
            top++;
        else if (act == A_PGUP)
            top -= rows;
        else if (act == A_PGDN)
            top += rows;
    }
    uk_invalidate();
    uk_close();
    return sel;
}
