/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The fog-of-war fan of src\losasm.asm (reveal_fan_asm, opt item reveal-fan)
   against the code it replaced: the old reveal_ray_asm and its C loop (los.c
   of bfde1a2), both in this one DOS program, on random maps. Built and run by
   tools\fancheck.ps1:

       FANTEST seed maps

   Every map gets a size, a number of levels, walls, objects, floors and a
   fog of its own; 10 units a map, at random tiles (the map's edges often)
   and facings, reveal the fog once with each code. The seen tables, the
   boxes of new tiles and the results must be the same. BENCH.TXT gets a line
   every 25 maps and at the end "fantest: n fans on m maps, d differ". */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_RANGE  20
#define SEEN_BIT    0x10
#define LF_WEST     0x01
#define LF_NORTH    0x02
#define LF_OBJECT   0x04
#define LF_FLOOR    0x08
#define MAX_TILES   14400u      /* 60 x 60 x 4 */

static const signed char g_dir_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const signed char g_dir_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
static unsigned g_mx, g_my, g_mz, mxy;
static unsigned char far *lf, far *g_seen, far *seen_old, far *seen_new, far *fog;
#define TIDX(x, y, z) ((((unsigned)(z) * g_my + (unsigned)(y)) * g_mx) + (unsigned)(x))

/* --- the old code (los.c of bfde1a2; losasm.asm of bfde1a2 with its box
   functions renamed) --- */
struct RevealRay {
    unsigned lfseg, seenseg, pi;
    int fx, fy, fz, sx, sy, sz, n;
    unsigned mx, my, mz, mxy;
    unsigned px, py, pz;
};

int __cdecl reveal_ray_asm(const struct RevealRay far *p);
void __cdecl old_reset_box(void);
void __cdecl old_get_box(unsigned char far *box);

static int reveal_ray(int x0, int y0, int z0, int x1, int y1, int z1)
{
    struct RevealRay p;
    int dx = x1 - x0, dy = y1 - y0, dz = z1 - z0, n;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, az = dz < 0 ? -dz : dz;

    n = ax > ay ? ax : ay;
    if (az > n)
        n = az;
    if (!n)
        return 0;
    p.lfseg = FP_SEG(lf);
    p.seenseg = FP_SEG(g_seen);
    p.pi = TIDX(x0, y0, z0);
    p.fx = x0 * 256 + 128;
    p.fy = y0 * 256 + 128;
    p.fz = z0 * 256 + 128;
    p.sx = dx * 256 / n;
    p.sy = dy * 256 / n;
    p.sz = dz * 256 / n;
    p.n = n;
    p.mx = g_mx;
    p.my = g_my;
    p.mz = g_mz;
    p.mxy = mxy;
    p.px = x0;
    p.py = y0;
    p.pz = z0;
    return reveal_ray_asm(&p);
}

static int old_reveal(int ux, int uy, int uz, int udir)
{
    int tz, k, d, vx, vy, sx, sy, revealed = 0;

    if (!(g_seen[TIDX(ux, uy, uz)] & SEEN_BIT)) {
        g_seen[TIDX(ux, uy, uz)] |= SEEN_BIT;
        revealed = 1;
    }
    d = (udir + 7) & 7;
    vx = g_dir_dx[d] * VIEW_RANGE;
    vy = g_dir_dy[d] * VIEW_RANGE;
    d = (udir + 2) & 6;
    sx = g_dir_dx[d];
    sy = g_dir_dy[d];
    for (k = 0; k <= 2 * VIEW_RANGE; k++) {
        for (tz = uz - 1; tz <= uz + 1; tz++)
            if ((unsigned)tz < g_mz)
                revealed |= reveal_ray(ux, uy, uz, ux + vx, uy + vy, tz);
        vx += sx;
        vy += sy;
        if (vx == vy || vx == -vy) {
            d = sx;
            sx = -sy;
            sy = d;
        }
    }
    return revealed;
}

/* --- the new code (los.c's los_reveal, src\losasm.asm as it is) --- */
struct RevealFan {
    unsigned lfseg, seenseg, pi, mxy;
    unsigned char mx, my, x, y, z, up;
    signed char vx, vy, sx, sy;
};

int __cdecl reveal_fan_asm(const struct RevealFan far *p);
void __cdecl reveal_reset_box(void);
void __cdecl reveal_get_box(unsigned char far *box);

static int new_reveal(int ux, int uy, int uz, int udir)
{
    struct RevealFan p;
    int d, revealed = 0;

    p.pi = TIDX(ux, uy, uz);
    if (!(g_seen[p.pi] & SEEN_BIT)) {
        g_seen[p.pi] |= SEEN_BIT;
        revealed = 1;
    }
    p.lfseg = FP_SEG(lf);
    p.seenseg = FP_SEG(g_seen);
    p.mxy = mxy;
    p.mx = (unsigned char)g_mx;
    p.my = (unsigned char)g_my;
    p.x = (unsigned char)ux;
    p.y = (unsigned char)uy;
    p.z = (unsigned char)uz;
    p.up = uz + 1u < g_mz;
    d = (udir + 7) & 7;
    p.vx = (signed char)(g_dir_dx[d] * VIEW_RANGE);
    p.vy = (signed char)(g_dir_dy[d] * VIEW_RANGE);
    d = (udir + 2) & 6;
    p.sx = g_dir_dx[d];
    p.sy = g_dir_dy[d];
    return revealed | reveal_fan_asm(&p);
}

/* --- the test --- */
static unsigned long rs;

static unsigned rnd(unsigned n)     /* 0 .. n - 1 */
{
    rs = rs * 1103515245ul + 12345ul;
    return (unsigned)((rs >> 16) & 0x7FFF) % n;
}

static unsigned char far *table(void)
{
    unsigned seg;

    if (_dos_allocmem((MAX_TILES + 15) / 16, &seg)) {
        puts("fantest: no memory");
        exit(1);
    }
    return MK_FP(seg, 0);
}

static void line(const char *s, unsigned long fans, unsigned maps, unsigned long bad)
{
    FILE *f = fopen("BENCH.TXT", "a");

    if (f) {
        fprintf(f, s, fans, maps, bad);
        fclose(f);
    }
    printf(s, fans, maps, bad);
}

int main(int argc, char **argv)
{
    static const unsigned char size[8] = { 10, 13, 20, 30, 40, 47, 50, 60 };
    static const unsigned char dens[6] = { 0, 2, 5, 10, 20, 40 };       /* percent */
    static const unsigned char fdens[5] = { 0, 10, 50, 90, 100 };
    static const unsigned char fogs[4] = { 0, 0, 30, 100 };
    unsigned maps = argc > 2 ? atoi(argv[2]) : 100, m, i, n, k;
    unsigned long fans = 0, bad = 0;

    rs = argc > 1 ? atol(argv[1]) : 1;
    lf = table();
    seen_old = table();
    seen_new = table();
    fog = table();
    remove("BENCH.TXT");
    for (m = 1; m <= maps; m++) {
        unsigned d = dens[rnd(6)], fd = fdens[rnd(5)], sd = fogs[rnd(4)];

        g_mx = size[rnd(8)];
        g_my = size[rnd(8)];
        g_mz = 1 + rnd(4);
        mxy = g_mx * g_my;
        n = mxy * g_mz;
        for (i = 0; i < n; i++) {
            unsigned char f = 0;

            if (rnd(100) < d)
                f |= LF_WEST;
            if (rnd(100) < d)
                f |= LF_NORTH;
            if (rnd(200) < d)
                f |= LF_OBJECT;
            if (rnd(100) < fd || (i < mxy && rnd(10)))
                f |= LF_FLOOR;
            if (!rnd(10))
                f |= (unsigned char)(rnd(16) << 4);     /* smoke, fire: not the fan's business */
            lf[i] = f;
            /* the seen table is the walk flags: other bits, seen or not */
            fog[i] = (unsigned char)((rnd(256) & ~SEEN_BIT) | (rnd(100) < sd ? SEEN_BIT : 0));
        }
        for (k = 0; k < 10; k++, fans++) {
            int x = rnd(g_mx), y = rnd(g_my), z = rnd(g_mz), dir = rnd(8), r_old, r_new;
            unsigned char b_old[6], b_new[6];

            if (rnd(10) < 3)
                x = rnd(2) ? rnd(2) : g_mx - 1 - rnd(2);
            if (rnd(10) < 3)
                y = rnd(2) ? rnd(2) : g_my - 1 - rnd(2);
            _fmemcpy(seen_old, fog, n);
            _fmemcpy(seen_new, fog, n);
            if (k % 3 == 0) {           /* the box gathers several fans, as in a walk */
                old_reset_box();
                reveal_reset_box();
            }
            g_seen = seen_old;
            r_old = old_reveal(x, y, z, dir);
            g_seen = seen_new;
            r_new = new_reveal(x, y, z, dir);
            old_get_box(b_old);
            reveal_get_box(b_new);
            if (r_old != r_new || memcmp(b_old, b_new, 6) || _fmemcmp(seen_old, seen_new, n)) {
                FILE *f = fopen("BENCH.TXT", "a");

                bad++;
                if (f) {
                    if (bad <= 20)
                        fprintf(f, "DIFFERENT: map %u (%ux%ux%u) unit %d,%d,%d facing %d: result %d / %d\n",
                                m, g_mx, g_my, g_mz, x, y, z, dir, r_old, r_new);
                    fclose(f);
                }
            }
        }
        if (m % 25 == 0 && m < maps)
            line("at %lu fans, %u maps: %lu differ\n", fans, m, bad);
    }
    line("fantest: %lu fans on %u maps, %lu differ\n", fans, maps, bad);
    return bad != 0;
}
