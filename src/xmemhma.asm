; SPDX-License-Identifier: GPL-3.0-or-later
; The read cache for the high memory area (docs/design.md "The HMA"):
; src/xmemres.asm assembled again with HMA defined, so it runs at FFFF:D800
; with its table and bounce buffer above it, plus the stub that stays in
; conventional memory. In the xmem overlay section with xmemres; src/xmem.c
; copies it out only on a 286 or better with HIMEM and no upper memory.
%define HMA
%include "xmemres.asm"
