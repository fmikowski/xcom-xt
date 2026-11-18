/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CVPRIV_H
#define CVPRIV_H

/* What canvas.c (the canvas's work) and cvinit.c (its start and end, an
   overlay section of its own so that canvas.c fits a small UMB overlay
   area) share. */

/* out: bit 0 to the block, bit 1 (CV_8BPP) VGA's back buffer (rows of 320
   bytes, one plane) instead of EGA's 4 planes of 40 */
void __cdecl cv_copy(unsigned bseg, unsigned boff, void far *blk, unsigned kstride,
                     unsigned bytes, unsigned rows, int out);
unsigned __cdecl cv_cmp(unsigned bseg, unsigned boff, void far *blk, unsigned kstride,
                        unsigned bytes, unsigned rows, int vga);
#define CV_8BPP 2
unsigned __cdecl cv_map(unsigned handle, unsigned page);
unsigned __cdecl cv_seen(unsigned char far *seen, unsigned idx, int step, unsigned n,
                         unsigned char far *bits);
unsigned long __cdecl xms_find(void);
void __cdecl xms_call(unsigned far *r);

#define BLK_PX  128             /* pixels across a block */
#define BLK_SH  7               /* (its shift) */
#define BLK_W   16              /* bytes across a block (EGA; cell columns of render_redraw) */
#define BLK_H   24              /* rows (cvasm.asm BLK_H) */
#define BLK_SZ  (BLK_W * BLK_H * 4)
#define PER_PG  10              /* blocks in a 16 KB EMS page */
#define SAVE_SZ (BUF_W * BLK_H * 4)     /* the back buffer rows cv_idle borrows */
#define VBLK_SZ (BLK_PX * BLK_H)        /* VGA: a byte a pixel (3072) */
#define VPER_PG 5
#define VSAVE_SZ (320 * BLK_H)
#define MAX_SLOT 512            /* 768 KB of blocks */
#define MIN_SLOT 40             /* fewer: off (a view touches up to 28 blocks; VGA's worst
                                   jungle battle had 24: 704 taken back, slower than drawing) */
#define OFFX    2048            /* world pixels to block indices: never negative */
#define OFFY    288
#define UMB_NEED 512            /* paragraphs of UMB overlay area: canvas.c + cvasm.asm 461 */
#define LIT_NEED 592            /* ... with nlit.c + litasm.asm too: 125 */
#define KEEP_FREE 768           /* paragraphs of conventional memory left to the battle */
#define FILL_R  3               /* blocks around the view cv_idle draws */

typedef struct {                /* what the renderer draws of a unit */
    unsigned char kind, x, y, z, big, pose, set, xt, tur, misc, body;
    signed char lev;
} USh;
typedef struct { unsigned char x, y, z, f; } TSh;

typedef struct {
    /* XMS move (XMS function 0Bh reads it from conventional memory) */
    unsigned long len;
    unsigned sh;
    unsigned long so;
    unsigned dh;
    unsigned long dof;
    unsigned seg, mode, handle, frame, slots, hbits, hand, used, page, save_pg;
    int check, idle_x, idle_y, idle_t, synced;
    unsigned gen, idle_gen;
    unsigned char far *fob, far *seenp;
    unsigned nfx;
    unsigned long n_read, n_hit, n_part, n_miss, n_store, n_evict, n_fill, n_bad;
    unsigned long t_sync, t_get;
    unsigned short far *hash, far *key;
    unsigned char far *mask, far *ref;      /* 3 bytes a slot: 2 cell rows each */
    USh far *us;
    TSh far *fl, far *dv, far *fx;
    unsigned far *keys;
    unsigned char far *seen, far *bounce;
    unsigned long xbase;            /* XMS: the blocks' place in the handle (a loan: xmemlend.h) */
    unsigned lent, lent_kb;         /* the loan's xl_lend answer and size, 0 none */
    /* the back buffer's format: EGA 0, 40, 3, 1536, 10; VGA CV_8BPP, 320, 0, 3072, 5
       (canvas.c koff: the blocks' layouts) */
    unsigned vga, bw, psh, bsz, per_pg;
} Cv;

#define MODE_EMS 1
#define MODE_XMS 2

extern Cv far *cvd;             /* the canvas's state; NULL while off */
void cv_exit(void);             /* cvinit.c: atexit */

/* Night light (nlit.c, litasm.asm; gfx.h g_lit): cells of the map view, a
   row a dword, bit set = lit. */
#define LIT_COLS    20
#define LIT_BANDS   18
typedef struct {
    int cam_x, cam_y, top;      /* what now[] is for */
    unsigned sum;               /* lit_sum */
    unsigned relight;           /* Pack.relight + 1 ter[] was made for */
    unsigned long n_make, n_change, n_cells;
    unsigned long now[LIT_BANDS], was[LIT_BANDS];
    unsigned long ter[LIT_BANDS];       /* the terrain's lights */
} Lit;
#define LITD ((Lit far *)g_lit)
void __cdecl lit_rect(unsigned bseg, unsigned long far *cells, int c0, int r0, int cols, int rows);
void __cdecl lit_disc(unsigned long far *cells, int cu, int cv, int tile);

#endif
