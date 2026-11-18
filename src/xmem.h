/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Extended memory (EMS or XMS) and the read cache under the game's file
   reads (src/xmem.c, src/xmemres.asm; docs/design.md, "Extended memory and
   the read cache"). Nothing of it is resident: the cache lives in a block
   of its own that exists only when a driver gave memory. */
#ifndef XMEM_H
#define XMEM_H

#pragma pack(push, 1)
typedef struct {
    unsigned h1, h2;            /* the name's hashes */
    unsigned long size;
    unsigned use;               /* 0 = free, else the open's number */
} XmemFile;

/* The block's header: the layout of src/xmemres.asm's h_ fields. */
typedef struct {
    char sig[4];                /* "XMC1" */
    unsigned char mode, on;     /* XM_EMS / XM_XMS; caching */
    void __far *old21, *old22, *old08, *xms;
    unsigned handle, frame, meta, bounce, nslot, hand, mapped;
    unsigned long hits, miss, reads, dbytes;
    unsigned seq, e21, e22, len, fail;
    unsigned char xmove[16];
    unsigned char hmap[32];     /* bit 7: DOS's file pointer is behind hpos */
    XmemFile files[32];
    unsigned long hpos[32];     /* the cached handles' file pointers */
} XmemHdr;

/* EMS: the conventional stub in front of the code in the frame. */
typedef struct {
    void __far *old21, *old22, *old08, *main;
    unsigned frame, handle, e21, e22, len;
} XmemStub;

/* HMA (src/xmemhma.asm): the conventional stub in front of the code at
   FFFF:DA00; own = 1 when the HMA came from the XMS driver (released at
   exit), 0 when it is the part DOS=HIGH left free (borrowed). */
typedef struct {
    void __far *old21, *old22, *old08, *main, *xms;
    unsigned handle, key, own, e21, e22, len;
} XmemHStub;
#pragma pack(pop)

#define XM_EMS 1
#define XM_XMS 2

/* startup.c, around the UMB plan: xmem_need = the paragraphs the cache's
   code and table want in upper memory (XMS only; 0: none wanted), then
   xmem_start with the segment the plan gave them (0: none, it finds a
   block itself). Options NOXMEM (no cache) and NOEMS (XMS even when EMS
   is there). */
unsigned xmem_need(int argc, char **argv);
void xmem_start(int argc, char **argv, unsigned umb);

/* The cache's header from anywhere (INT 21h AH = D7h with SI = 'XM', DI =
   0): AX = 'XM', the header at ES:DI (DI = DA00h in the HMA). */
#define XMEM_MAGIC 0x4D58u      /* 'XM' */

#endif
