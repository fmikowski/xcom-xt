/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The pictures of a battle that does not fit otherwise (docs/design.md,
   "Tight battles"): the walls' and objects' pictures stay out of conventional
   memory and come in when drawn (gfx_sprite). From EMS (the picture store,
   picplan.c) a picture is drawn in place: its pointer's segment names its
   logical page, mapped at physical page 2 of the frame. From XMS or from the
   mission pack on disk it is copied into a buffer that is emptied when full.
   Resident: anything may draw. */
#include "pak.h"

int g_sc_fd = -1;
const unsigned long *g_sc_off;  /* the pack's sprite offsets, the pictures' start */
unsigned long g_sc_pos;
unsigned char *g_sc_buf;
unsigned g_sc_size, g_sc_head, g_sc_common, g_sc_n;
unsigned g_sc_tag = 0xFFFF;     /* the picture store: pointers from this segment up */
unsigned g_sc_cur;              /* EMS: the segment whose page is mapped */
unsigned g_sc_top;              /* EMS: physical page 2's segment (0: XMS) */
unsigned g_sc_xh;               /* the store's EMS or XMS handle, 0 none */
unsigned g_alias_sets;          /* bit per unit set: another set's pictures (picplan.c) */
unsigned char g_no_rows[2];     /* a picture of no rows: gfx_sprite draws nothing */

/* sprite_fault: src/sndasm.asm (next to the XMS entry point). */
