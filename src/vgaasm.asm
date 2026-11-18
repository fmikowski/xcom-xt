; SPDX-License-Identifier: GPL-3.0-or-later
; The VGA prototype's battle view (mode 13h, 320x200, one byte a pixel):
; the 8-bit back buffer (320 x VIEW_H bytes, one segment), the sprite
; blitters for tools/vgasprite.py's formats, clearing, the present to
; A000, the scroll, the cursor's kept box and the palette. Lives in the
; vgainit overlay section with src/vgainit.c, which copies the code
; between blob_start and blob_end into a block of its own once
; (vga_blob) and points the resident CGA routines at it: their first
; bytes become a far jump to the entry of the same name here, so the
; renderer's calls (gfx.c, rendasm.asm, rcursor.c) arrive here with
; their own arguments and nothing resident changes (docs/design.md, "VGA
; prototype"). The game runs as on CGA (g_video = VIDEO_CGA), and CGA
; buffer offsets are a quarter of the VGA ones: a CGA byte is 4 pixels,
; a row 80 bytes, and every sprite's x is a multiple of 16. The copied
; code has no relocations: data at fixed offsets of its own block (CS),
; jumps relative, the op table by block offsets. Large model, Watcom
; __cdecl.
;
; Block layout (paragraph aligned, offset 0):
;   0000  PAL[768]   the DAC's palette (vgainit.c reads a VGA pack's "XPL1")
;   0300  variables
;   0320  SAVE[1280] the cursor's 32x40 box (src/rcursor.c)
;   0820  the interface's: VMAP[16] (EGA colours 0-15 to the palette's),
;         far pointers to the C side's (vgainit.c), variables, the clean
;         screen's pictures, the pointer's background and picture, a row
;   0B20  the night lights' far pointers and sizes (vgainit.c; "Night
;         lights" below: their working data is in PAL during a night
;         battle, their shade table in the buffer's segment at B400)
;   0B68  V_SHADE: the night's shade, 0 by day; 0B6C P_VOFF: &g_view_off
;   0B70  the entries, 4 bytes apart (vgainit.c), then the code:
;   0 blitc_masked      (const void far *spr, unsigned bufseg, unsigned dest)
;   1 blitc_masked_clip (spr, bufseg, dest, unsigned top, unsigned bottom, unsigned wmask)
;   2 blitc_or          (spr, bufseg, dest)
;   3 blitc_or_clip     (spr, bufseg, dest, top, bottom, wmask)
;   4 bufc_clear_rect   (unsigned bufseg, unsigned col, unsigned row, unsigned cols, unsigned rows)
;   5 bufc_present_rect (bufseg, col, row, cols, rows): buffer to A000
;   6 buf_shift         (unsigned bufseg, unsigned bytes, unsigned planes, int delta)
;   7 buf_box           (unsigned bufseg, unsigned off, unsigned stride, unsigned bytes,
;                        unsigned rows, void far *save, int out): out 1 buffer to SAVE
;   8 vga_pal           (): PAL to the DAC
; The interface: entries 9-22 (see "The interface" below).
;  23 fxtile_cga        (bufseg, off, shape, r0, r1, q01, q23, colour * 55h, cols):
;                       smoke, fire and flare overlays (src/fxasm.asm): DOS's
;                       smoke and fire pictures (fxpic, SMOKEV.PAK at P_SMOKE),
;                       else each CGA colour in 256 colours (fx_col)
;  24 gfx_night         (AX night): the view's shade and lights at the present
;                       ("Night lights" below)
;  25 gfx_dot           (AX x, DX y, BX colour): a tracer dot, 2 x 2 XORed in
;  26 pics_next         (): the next part of the view to redraw as the smoke and
;                       fire pictures' step moves on (bplayer.c's idle loop)
; dest, off: CGA buffer offsets of the sprite box's top left (y * 80 + x
; / 4, 16-bit wrap for boxes off the top or left); col, cols, bytes: CGA
; bytes; delta: CGA bytes. top / bottom: the visible rows of the 40-row
; box (bottom exclusive); wmask: its visible 8-pixel columns (gfx.c
; clip_box, bit 0 = pixels 0-7; contiguous).

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

ROW     equ 320
VIEW_H  equ 144
BYTES   equ ROW * VIEW_H        ; 46080

PAL     equ 0000h
V_LX    equ 0302h               ; clip: first visible column of the box
V_RX    equ 0304h               ; ... first one past it
V_TOP   equ 0306h
V_BOT   equ 0308h
V_ROW   equ 030Ah               ; the row being drawn (of the box)
V_NEXT  equ 030Ch               ; the next span's (or row's) data
SAVE    equ 0320h
VMAP    equ 0820h               ; EGA colour -> palette index
P_MHR   equ 0830h               ; far pointers (vgainit.c): mouse_hide_rect
P_FD    equ 0834h               ; &g_uk_fd
P_UK    equ 0838h               ; &g_uk
P_THEME equ 083Ch               ; the EGA themes
P_PM    equ 0840h               ; &g_ms_pm
P_PF    equ 0844h               ; &g_ms_pf
P_NAME  equ 0848h               ; g_uk_name
P_ARROW equ 084Ch               ; g_arrow
P_VIEWZ equ 0850h               ; &g_view_z
P_CAMX  equ 0854h               ; &g_cam_x
P_CAMY  equ 0858h               ; &g_cam_y
P_BUFSEG equ 085Ch              ; &g_bufseg
V_SY0   equ 0870h               ; the clean rows [V_SY0, V_SY1)
V_SY1   equ 0872h
V_NCL   equ 0874h               ; pictures in CLEAN
V_OPEN  equ 0876h               ; 1: the pack was opened here
V_MX    equ 0878h               ; the pointer's box
V_MY    equ 087Ah
V_MW    equ 087Ch
V_MH    equ 087Eh
V_QX    equ 0880h               ; the rectangle drawn (restore, picture)
V_QY    equ 0882h
V_QW    equ 0884h
V_QH    equ 0886h
V_T0    equ 0888h
V_T1    equ 088Ah
LAST    equ 0890h               ; the last plain picture drawn (an entry)
CLEAN   equ 08A0h               ; 4 entries: the clean screen's pictures
MSAVE   equ 08E0h               ; the pointer's background, 9 x 14
CURSOR  equ 0960h               ; the pointer, 9 x 13 (OpenXcom's Cursor)
P_SMOKE equ 09D8h               ; far: SMOKEV.PAK's pictures (vgainit.c), 0: none
ROWBUF  equ 09E0h               ; a row of a picture
SHADE   equ 0B400h              ; (in the buffer's segment) palette index -> its shade
V_SHADE equ 0B68h               ; the shade (0-15), 0: none
P_VOFF  equ 0B6Ch               ; far pointer: &g_view_off (vgainit.c)
CODE_OFF equ 0B70h
; An entry: E_POS the data's place in the pack (32 bits), x, y, w, h, flags.
E_POS   equ 0
E_X     equ 4
E_Y     equ 6
E_W     equ 8
E_H     equ 10
E_FL    equ 12

; Offset of a label in the copied code.
%define L(x) ((x) - blob_start + CODE_OFF)
; The night lights' data ("Night lights"): offsets in the block.
%define LD(x) (x)

segment VGAASM_TEXT public class=CODE

; unsigned __cdecl vga_blob(unsigned seg): seg 0 returns the code's size;
; else copies it to seg:CODE_OFF.
        global _vga_blob
_vga_blob:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp+6]
        or ax, ax
        jz .size
        mov es, ax
        call .here
.here:  pop si
        sub si, .here - blob_start
        mov ax, cs
        mov ds, ax
        mov di, CODE_OFF
        mov cx, blob_end - blob_start
        cld
        rep movsb
.size:  mov ax, blob_end - blob_start
        pop ds
        pop di
        pop si
        pop bp
        retf

; void far * __cdecl vga_near(int k): the k-th of the resident routines
; vgainit.c points at the interface code that C cannot name (near ones).
extern uk_pic_cga_, uk_cga_c_, uk_glyph_cga_, hud_text_cga_, ms_draw_cga_, ms_erase_cga_
        global _vga_near
_vga_near:
        push bp
        mov bp, sp
        push bx
        mov bx, [bp+6]
        shl bx, 1
        shl bx, 1
        mov ax, [cs:bx+near_tab]
        mov dx, [cs:bx+near_tab+2]
        pop bx
        pop bp
        retf
near_tab:
        dw uk_pic_cga_, seg uk_pic_cga_
        dw uk_cga_c_, seg uk_cga_c_
        dw uk_glyph_cga_, seg uk_glyph_cga_
        dw hud_text_cga_, seg hud_text_cga_
        dw ms_draw_cga_, seg ms_draw_cga_
        dw ms_erase_cga_, seg ms_erase_cga_

; ------------------------------------------------------------------
blob_start:
%assign k 0
%rep 9
        mov bl, k * 2
        jmp short begin
%assign k k + 1
%endrep
        jmp near ui_pic         ; 9
        nop
        jmp near ui_cgac        ; 10
        nop
        jmp near ui_glyph       ; 11
        nop
        jmp near ui_clean       ; 12
        nop
        jmp near ui_save        ; 13
        nop
        jmp near ui_restore     ; 14
        nop
        jmp near ui_fill        ; 15
        nop
        jmp near ui_theme       ; 16
        nop
        jmp near ui_copy        ; 17
        nop
        jmp near ui_hudtext     ; 18
        nop
        jmp near ui_hudclear    ; 19
        nop
        jmp near ui_msdraw      ; 20
        nop
        jmp near ui_mserase     ; 21
        nop
        jmp near ui_arrow       ; 22
        nop
        jmp near op_fx          ; 23
        nop
        jmp near ui_night       ; 24
        nop
        jmp near ui_dot         ; 25
        nop
        jmp near pics_next      ; 26
        nop
begin:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        cld
        xor bh, bh
        jmp [cs:bx+L(optab)]
optab   dw L(op_masked), L(op_mclip), L(op_or), L(op_oclip)
        dw L(op_clear), L(op_present), L(op_shift), L(op_box), L(op_pal)
done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; Sprite header: DS:SI = the sprite, ES = the buffer. AL = first row, AH
; = row count on exit, SI past them; BX = the VGA dest of the first row;
; CH = 0. ZF set: no rows.
%macro HEADER 0
        lds si, [bp+6]
        mov es, [bp+10]
        lodsw
        mov bx, ax
        xor ah, ah
        mov cx, ROW
        mul cx                  ; first row * 320
        mov cx, [bp+12]
        shl cx, 1
        shl cx, 1               ; the CGA dest * 4
        add ax, cx
        xchg ax, bx
        xor ch, ch
        or ah, ah
%endmacro

; Copies CX (1..32) bytes DS:SI to ES:DI, words on even columns. CX = 0 after.
%macro SPAN 0
        test di, 1
        jz %%even
        movsb
        dec cx
%%even:
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
%endmacro

; Clips the span AX = x, CX = length (bytes), DS:SI = its data to the
; columns [V_LX, V_RX) and copies what is left to row BX. Uses AX, CX, DI.
%macro CLIPSPAN 1
        mov di, ax              ; x
        add ax, cx              ; end
        cmp ax, [cs:V_RX]
        jbe %%e
        mov ax, [cs:V_RX]
%%e:    mov cx, [cs:V_LX]
        cmp cx, di
        jae %%s
        mov cx, di              ; start = max(x, lx)
%%s:    sub ax, cx              ; end - start
        jle %%none
        sub cx, di              ; start - x
        add si, cx              ; its first pixel
        add cx, di
        mov di, bx
        add di, cx              ; the row's start column
        mov cx, ax
%if %1
        SPAN
%else
        shr cx, 1               ; OR rows: even columns and lengths
        rep movsw
%endif
%%none:
%endmacro

; Row of the box [V_ROW] outside [V_TOP, V_BOT): jumps to %1. Uses AX.
%macro HIDDEN 1
        mov ax, [cs:V_ROW]
        cmp ax, [cs:V_TOP]
        jb %1
        cmp ax, [cs:V_BOT]
        jae %1
%endmacro

; ---- 0: masked (transparent spans), unclipped
op_masked:
        HEADER
        jz .x
        mov dl, ah              ; rows
.row:
        lodsb
        mov dh, al              ; spans
        or al, al
        jz .next
.span:
        lodsw                   ; al = x | pad << 7, ah = len
        mov cl, ah
        shl al, 1
        adc si, 0               ; the filler byte
        shr al, 1
        xor ah, ah
        mov di, bx
        add di, ax
        SPAN
        dec dh
        jnz .span
.next:
        add bx, ROW
        dec dl
        jnz .row
.x:     jmp done

; The clip rectangle of the box from top, bottom, wmask (into the variables).
clip_vars:
        mov ax, [bp+14]
        mov [cs:V_TOP], ax
        mov ax, [bp+16]
        mov [cs:V_BOT], ax
        mov bx, [bp+18]
        and bx, 15
        mov al, [cs:bx+L(lx_tab)]
        xor ah, ah
        mov [cs:V_LX], ax
        mov al, [cs:bx+L(rx_tab)]
        mov [cs:V_RX], ax
        ret
lx_tab:
%assign m 0
%rep 16
%assign b 0
%assign v 32
%rep 4
%if (m >> b) & 1
%if v == 32
%assign v b * 8
%endif
%endif
%assign b b + 1
%endrep
        db v
%assign m m + 1
%endrep
rx_tab:
%assign m 0
%rep 16
%assign b 0
%assign v 0
%rep 4
%if (m >> b) & 1
%assign v (b + 1) * 8
%endif
%assign b b + 1
%endrep
        db v
%assign m m + 1
%endrep

; ---- 1: masked, clipped to rows [top, bottom) and columns [lx, rx)
op_mclip:
        call clip_vars
        HEADER
        jz .x
        mov [cs:V_ROW], al
        mov byte [cs:V_ROW+1], 0
        mov dl, ah              ; rows
.row:
        lodsb
        mov dh, al              ; spans
        or al, al
        jz .next
        HIDDEN .skip
.span:
        lodsw                   ; al = x | pad << 7, ah = len
        mov cl, ah
        shl al, 1
        adc si, 0
        shr al, 1
        xor ah, ah              ; ax = x, cx = len, si = its data
        mov di, si
        add di, cx
        mov [cs:V_NEXT], di     ; the next span
        CLIPSPAN 1
        mov si, [cs:V_NEXT]
        dec dh
        jnz .span
        jmp short .next
.skip:
        lodsw                   ; a hidden row: past its spans
        mov cl, ah
        shl al, 1
        adc si, 0
        add si, cx
        dec dh
        jnz .skip
.next:
        inc word [cs:V_ROW]
        add bx, ROW
        dec dl
        jnz .row
.x:     jmp done

; ---- 2: OR format (ground floors on a cleared area), unclipped
op_or:
        HEADER
        jz .x
        mov dl, ah
.row:
        lodsw                   ; al = x, ah = words
        mov cl, ah
        xor ah, ah
        mov di, bx
        add di, ax
        rep movsw
        add bx, ROW
        dec dl
        jnz .row
.x:     jmp done

; ---- 3: OR format, clipped
op_oclip:
        call clip_vars
        HEADER
        jz .x
        mov [cs:V_ROW], al
        mov byte [cs:V_ROW+1], 0
        mov dl, ah
.row:
        HIDDEN .skip
        lodsw                   ; al = x, ah = words
        mov cl, ah
        shl cx, 1               ; bytes
        xor ah, ah
        mov di, si
        add di, cx
        mov [cs:V_NEXT], di
        CLIPSPAN 0
        mov si, [cs:V_NEXT]
        jmp short .next
.skip:
        lodsw
        mov cl, ah
        shl cx, 1
        add si, cx
.next:
        inc word [cs:V_ROW]
        add bx, ROW
        dec dl
        jnz .row
.x:     jmp done

; ES = DS = [bp+6]; DI = row [bp+10] * 320 + CGA byte [bp+8] * 4; BX =
; words a row ([bp+12] CGA bytes), DX = the bytes to the next row, AX =
; rows ([bp+14]).
cells:
        mov ax, [bp+10]
        mov cx, ROW
        mul cx
        mov di, [bp+8]
        shl di, 1
        shl di, 1
        add di, ax
        mov bx, [bp+12]
        shl bx, 1
        mov dx, ROW
        sub dx, bx
        sub dx, bx
        mov ax, [bp+6]
        mov es, ax
        mov ds, ax
        mov ax, [bp+14]
        ret

; ---- 4: clear cells of the buffer
op_clear:
        mov ax, [bp+10]         ; (the rows redrawn: fxpic)
        mov [cs:V_CY0], ax
        add ax, [bp+14]
        mov [cs:V_CY1], ax
        call cells
        mov si, ax
        or si, si
        jz .x
        xor ax, ax
.row:
        mov cx, bx
        rep stosw
        add di, dx
        dec si
        jnz .row
.x:     jmp done

; ---- 5: present cells (the buffer's rows are the screen's)
op_present:
        cmp word [cs:V_SHADE], 0
        je .day
        jmp lit_present         ; at night (below)
.day:   call cells
        mov si, di
        mov cx, 0A000h
        mov es, cx
        or ax, ax
        jz .x
.row:
        mov cx, bx
        rep movsw
        add si, dx
        add di, dx
        dec ax
        jnz .row
.x:     jmp done

; ---- 6: the view moves by delta (CGA bytes; the buffer is exactly the
; view's width, so what wraps round a row edge lands in the exposed
; strip, which is redrawn: blit.asm buf_shift)
op_shift:
        mov dx, [bp+12]
        shl dx, 1
        shl dx, 1               ; VGA bytes
        cmp word [cs:V_SHADE], 0
        je .buf
        call vshift             ; at night the screen too ("Night lights")
.buf:   mov ax, [bp+6]
        mov ds, ax
        mov es, ax
        call vmove
        jmp done

; Moves the view's bytes at DS = ES by DX. Keeps DX.
vmove:
        mov cx, dx
        or cx, cx
        js .neg
        jz .x
        neg cx
        add cx, BYTES
        shr cx, 1               ; words to move
        mov di, BYTES - 2       ; the last word
        mov si, di
        sub si, dx
        std
        rep movsw
        cld
        ret
.neg:
        add cx, BYTES
        shr cx, 1
        xor di, di
        mov si, dx
        neg si
        rep movsw
.x:     ret

; ---- 7: the cursor's box between the buffer and SAVE
op_box:
        mov si, [bp+8]
        shl si, 1
        shl si, 1               ; VGA offset
        mov bx, [bp+12]         ; CGA bytes a row
        shl bx, 1               ; VGA words
        mov dx, ROW
        sub dx, bx
        sub dx, bx
        mov ax, [bp+14]         ; rows
        or ax, ax
        jz .x
        cmp word [bp+20], 0
        je .in
        mov ds, [bp+6]
        push cs
        pop es
        mov di, SAVE
.o:     mov cx, bx
        rep movsw
        add si, dx
        dec ax
        jnz .o
        jmp short .x
.in:    mov di, si
        mov es, [bp+6]
        push cs
        pop ds
        mov si, SAVE
.i:     mov cx, bx
        rep movsw
        add di, dx
        dec ax
        jnz .i
.x:     jmp done

; ---- 8: the palette to the DAC
op_pal:
        mov ax, cs
        mov ds, ax
        mov si, PAL
        mov dx, 3C8h
        xor al, al
        out dx, al
        inc dx
        mov cx, 768
.c:     lodsb
        out dx, al
        loop .c
        jmp done

; ------------------------------------------------------------------
; The interface (src/uik.c, hud.c, mouse.c, arrow.asm) in 256 colours.
; vgainit.c turns the first bytes of these resident routines into `call
; far entry; ret[f] [n]`, so they arrive here with the C caller's
; arguments (Watcom register calls: AX, DX, BX, CX, a far pointer in a
; pair, the rest on the stack; everything but AX, DS and ES kept) and the
; patched routine's own return pops the stack arguments:
;   9  uk_pic_cga   (near: DX:AX head, BX x, CX y; to_save): a picture
;                   from the pack's current place (UIV.PAK: a byte a pixel,
;                   masked ones clear where 0)
;   10 uk_cga_c     (near: AX colour): VMAP
;   11 uk_glyph_cga (near: AX x, DX y, CX:BX glyph; h, bpr, colour)
;   12 uk_pic_clean (AX id): into the clean screen's list
;   13 uk_save      (AX y0, DX y1)
;   14 uk_restore   (AX x, DX y, BX w, CX h)
;   15 uk_fill      (AX x, DX y, BX w, CX h; colour)
;   16 uk_theme     (AX theme): the EGA themes
;   17 uk_copy      (AX sx, DX sy, BX dx, CX dy; w, h)
;   18 hud_text_cga (near: AX col, DX row, BX colour; s, len): BIOS font
;   19 hud_clear_rows (AX row, DX count)
;   20 ms_draw_cga  (near: AX x, DX y): the pointer, its background kept
;   21 ms_erase_cga (near: AX x, DX y)
;   22 render_arrow (AX c0, DX r0, BX cols, CX rows): into the back buffer
; There is no copy of the clean screen (320 x 56 bytes for the panel do
; not fit beside the back buffer): uk_save takes the last plain picture
; drawn as the clean screen, uk_pic_clean adds pictures to it, and
; uk_restore reads their part of the rectangle from the pack again (the
; read cache keeps it in XMS or EMS), black where none covers it.

V_SKIP  equ 088Ch               ; part: file bytes between two rows' parts
V_ROWS  equ 088Eh               ; part: rows left
TMPE    equ 0860h               ; an entry being made

%macro UIIN 0
        push bp
        mov bp, sp
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        cld
%endmacro

; First stack argument: [bp+NEAR_A] for a near routine, [bp+FAR_A] far.
NEAR_A  equ 8
FAR_A   equ 10

ui_done:
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop bp
        retf

; mouse_hide_rect(AX x0, DX y0, BX x1, CX y1). Keeps BX, CX, DX, SI, DI.
hide:
        push ds
        push es
        call far [cs:P_MHR]
        pop es
        pop ds
        ret

; The pack's handle in BX (opened here if closed: V_OPEN). CF: none.
open_pack:
        mov word [cs:V_OPEN], 0
        les di, [cs:P_FD]
        mov bx, [es:di]
        or bx, bx
        jns .ok
        push ds
        lds dx, [cs:P_NAME]
        mov ax, 3D00h
        int 21h
        pop ds
        jc .no
        les di, [cs:P_FD]
        mov [es:di], ax
        mov bx, ax
        mov word [cs:V_OPEN], 1
.ok:    clc
.no:    ret

close_pack:
        cmp word [cs:V_OPEN], 0
        je .x
        les di, [cs:P_FD]
        mov bx, [es:di]
        mov word [es:di], -1
        mov ah, 3Eh
        int 21h
        mov word [cs:V_OPEN], 0
.x:     ret

; DI = AX * 320 + DX (screen offset of x DX, row AX). Uses AX, DX.
scr_off:
        push dx
        mov di, ROW
        mul di
        pop dx
        add ax, dx
        mov di, ax
        ret

; Draws the part of entry CS:SI inside [V_QX, +V_QW) x [V_QY, +V_QH),
; reading it from the pack (handle BX, kept). Uses AX, CX, DX, SI, DI,
; DS, ES.
part:
        ; columns: T0 = max(qx, x), T1 = min(qx + qw, x + w) - T0
        mov ax, [cs:V_QX]
        mov dx, [cs:si+E_X]
        cmp ax, dx
        jge .c0
        mov ax, dx
.c0:    mov [cs:V_T0], ax
        mov ax, [cs:V_QX]
        add ax, [cs:V_QW]
        add dx, [cs:si+E_W]
        cmp ax, dx
        jle .c1
        mov ax, dx
.c1:    sub ax, [cs:V_T0]
        jg .cols
        ret
.cols:  mov [cs:V_T1], ax       ; bytes a row
        mov dx, [cs:si+E_W]
        sub dx, ax
        mov [cs:V_SKIP], dx
        ; rows: CX = max(qy, y) .. min(qy + qh, y + h)
        mov cx, [cs:V_QY]
        mov dx, [cs:si+E_Y]
        cmp cx, dx
        jge .r0
        mov cx, dx
.r0:    mov ax, [cs:V_QY]
        add ax, [cs:V_QH]
        add dx, [cs:si+E_H]
        cmp ax, dx
        jle .r1
        mov ax, dx
.r1:    sub ax, cx
        jg .rows
        ret
.rows:  mov [cs:V_ROWS], ax
        push cx                 ; first row
        ; seek: pos + (row - y) * w + (T0 - x)
        mov ax, cx
        sub ax, [cs:si+E_Y]
        mul word [cs:si+E_W]
        add ax, [cs:V_T0]
        adc dx, 0
        sub ax, [cs:si+E_X]
        sbb dx, 0
        add ax, [cs:si+E_POS]
        adc dx, [cs:si+E_POS+2]
        mov cx, dx
        mov dx, ax
        mov ax, 4200h
        int 21h
        pop ax                  ; first row
        mov dx, [cs:V_T0]
        call scr_off            ; DI: its place on the screen
        mov ax, 0A000h
        mov es, ax
        test byte [cs:si+E_FL], 1
        jnz .masked
        ; plain: whole rows of the screen in one read
        mov ds, ax
        mov cx, [cs:V_T1]
        cmp cx, ROW
        jne .prow
        mov ax, [cs:V_ROWS]
        mul cx
        mov cx, ax
        mov dx, di
        mov ah, 3Fh
        int 21h
        ret
.prow:  mov cx, [cs:V_T1]
        mov dx, di
        mov ah, 3Fh
        int 21h
        add di, ROW
        dec word [cs:V_ROWS]
        jz .x
        call skip
        jmp short .prow
.masked:
        push cs
        pop ds
.mrow:  mov cx, [cs:V_T1]
        mov dx, ROWBUF
        mov ah, 3Fh
        int 21h
        mov cx, [cs:V_T1]
        mov si, ROWBUF
        push di
.m:     lodsb
        or al, al
        jz .m0
        mov [es:di], al
.m0:    inc di
        loop .m
        pop di
        add di, ROW
        dec word [cs:V_ROWS]
        jz .x
        call skip
        jmp short .mrow
.x:     ret

; Past V_SKIP bytes of the pack (handle BX). Keeps DS, ES, DI.
skip:
        mov dx, [cs:V_SKIP]
        or dx, dx
        jz .x
        xor cx, cx
        mov ax, 4201h
        int 21h
.x:     ret

; Is [V_QX, +V_QW) x [V_QY, +V_QH) inside entry CS:SI? ZF set if so.
covers:
        mov ax, [cs:V_QX]
        sub ax, [cs:si+E_X]
        jl .no
        add ax, [cs:V_QW]
        cmp ax, [cs:si+E_W]
        jg .no
        mov ax, [cs:V_QY]
        sub ax, [cs:si+E_Y]
        jl .no
        add ax, [cs:V_QH]
        cmp ax, [cs:si+E_H]
        jg .no
        xor ax, ax
        ret
.no:    or sp, sp               ; (ZF clear)
        ret

; Fills [V_QX, +V_QW) x [V_QY, +V_QH) with AL (clipped to the screen).
qfill:
        mov bl, al
        mov dx, [cs:V_QX]
        mov cx, [cs:V_QW]
        or dx, dx
        jns .x0
        add cx, dx
        xor dx, dx
.x0:    mov ax, dx
        add ax, cx
        sub ax, ROW
        jle .x1
        sub cx, ax
.x1:    or cx, cx
        jle .x
        mov ax, [cs:V_QY]
        mov si, [cs:V_QH]
        or ax, ax
        jns .y0
        add si, ax
        xor ax, ax
.y0:    push ax
        add ax, si
        sub ax, 200
        jle .y1
        sub si, ax
.y1:    pop ax
        or si, si
        jle .x
        call scr_off
        mov ax, 0A000h
        mov es, ax
        mov al, bl
        mov dx, ROW
        sub dx, cx
.row:   push cx
        rep stosb
        pop cx
        add di, dx
        dec si
        jnz .row
.x:     ret

; ---- 9: uk_pic_cga (near): DX:AX the head, BX x, CX y
ui_pic:
        UIIN
        mov ds, dx
        mov si, ax
        mov [cs:V_QX], bx
        mov [cs:V_QY], cx
        mov [cs:TMPE+E_X], bx
        mov [cs:TMPE+E_Y], cx
        mov ax, [si+4]
        mov [cs:V_QW], ax
        mov [cs:TMPE+E_W], ax
        mov ax, [si+6]
        mov [cs:V_QH], ax
        mov [cs:TMPE+E_H], ax
        mov al, [si+8]
        xor ah, ah
        mov [cs:TMPE+E_FL], ax
        les di, [cs:P_FD]
        mov bx, [es:di]
        mov ax, 4201h           ; where the data starts
        xor cx, cx
        xor dx, dx
        int 21h
        mov [cs:TMPE+E_POS], ax
        mov [cs:TMPE+E_POS+2], dx
        test byte [cs:TMPE+E_FL], 1
        jnz .draw
        push cs                 ; a plain picture: the clean screen's for uk_save
        pop es
        push cs
        pop ds
        mov si, TMPE
        mov di, LAST
        mov cx, 8
        rep movsw
.draw:  mov si, TMPE
        call part
        jmp ui_done

; ---- 10: uk_cga_c (near): AX colour 0-15
ui_cgac:
        UIIN
        mov bx, ax
        and bx, 15
        mov al, [cs:bx+VMAP]
        xor ah, ah
        jmp ui_done

; ---- 11: uk_glyph_cga (near): AX x, DX y, CX:BX glyph; h, bpr, colour
ui_glyph:
        UIIN
        mov si, bx
        mov ds, cx
        mov cx, ROW
        sub cx, ax              ; pixels to the screen's right edge
        mov [cs:V_T0], cx
        xchg ax, dx
        call scr_off
        mov ax, 0A000h
        mov es, ax
        mov cx, [bp+NEAR_A]     ; rows
        or cx, cx
        jle .x
        mov bl, [bp+NEAR_A+4]   ; colour
.row:   mov dh, [si]
        xor dl, dl
        cmp word [bp+NEAR_A+2], 2
        jne .one
        mov dl, [si+1]
.one:   add si, [bp+NEAR_A+2]
        push di
        push cx
        mov cx, [cs:V_T0]
.px:    or dx, dx
        jz .end
        jcxz .end
        shl dx, 1
        jnc .n
        mov [es:di], bl
.n:     inc di
        dec cx
        jmp short .px
.end:   pop cx
        pop di
        add di, ROW
        loop .row
.x:     jmp ui_done

; ---- 12: uk_pic_clean: AX id; returns 1, 0 if missing
ui_clean:
        UIIN
        mov si, ax
        call open_pack
        jc .no
        mov dx, si
        shl dx, 1
        shl dx, 1
        add dx, 8               ; the entry's place in the table
        xor cx, cx
        mov ax, 4200h
        int 21h
        push cs
        pop ds
        mov dx, TMPE
        mov cx, 4
        mov ah, 3Fh
        int 21h
        mov dx, [cs:TMPE]
        mov cx, [cs:TMPE+2]
        mov ax, dx
        or ax, cx
        jz .none
        add dx, 10
        adc cx, 0
        mov [cs:TMPE+E_POS], dx
        mov [cs:TMPE+E_POS+2], cx
        sub dx, 10
        sbb cx, 0
        mov ax, 4200h
        int 21h
        mov dx, TMPE + 4        ; x, y, w, h, flags (the head)
        mov cx, 10
        mov ah, 3Fh
        int 21h
        and word [cs:TMPE+E_FL], 0FFh
        mov di, CLEAN
        test byte [cs:TMPE+E_FL], 1
        jz .first               ; plain: the new clean screen
        mov ax, [cs:V_NCL]
        cmp ax, 4
        jae .full
        mov cl, 4
        shl ax, cl
        add di, ax
        inc word [cs:V_NCL]
        jmp short .put
.first: mov word [cs:V_NCL], 1
.put:   push cs
        pop es
        mov si, TMPE
        mov cx, 8
        rep movsw
.full:  call close_pack
        mov ax, 1
        jmp ui_done
.none:  call close_pack
.no:    xor ax, ax
        jmp ui_done

; ---- 13: uk_save: AX y0, DX y1
ui_save:
        UIIN
        mov [cs:V_SY0], ax
        mov [cs:V_SY1], dx
        mov word [cs:V_NCL], 0
        cmp word [cs:LAST+E_H], 0
        je .x
        push cs
        pop es
        push cs
        pop ds
        mov si, LAST
        mov di, CLEAN
        mov cx, 8
        rep movsw
        mov word [cs:V_NCL], 1
.x:     jmp ui_done

; ---- 14: uk_restore: AX x, DX y, BX w, CX h
ui_restore:
        UIIN
        mov si, [cs:V_SY0]
        cmp dx, si
        jge .a
        sub si, dx
        sub cx, si
        mov dx, [cs:V_SY0]
.a:     mov si, dx
        add si, cx
        cmp si, [cs:V_SY1]
        jle .b
        mov cx, [cs:V_SY1]
        sub cx, dx
.b:     or bx, bx
        jle .x
        or cx, cx
        jle .x
        mov [cs:V_QX], ax
        mov [cs:V_QY], dx
        mov [cs:V_QW], bx
        mov [cs:V_QH], cx
        add bx, ax
        add cx, dx
        call hide
        mov si, CLEAN
        cmp word [cs:V_NCL], 0
        je .black
        call covers
        je .pics
.black: xor al, al
        call qfill
.pics:  call open_pack
        jc .x
        mov si, CLEAN
        mov cx, [cs:V_NCL]
        jcxz .done
.e:     push cx
        push si
        call part
        pop si
        pop cx
        add si, 16
        loop .e
.done:  call close_pack
.x:     jmp ui_done

; ---- 15: uk_fill: AX x, DX y, BX w, CX h; colour
ui_fill:
        UIIN
        or bx, bx
        jle .x
        or cx, cx
        jle .x
        mov [cs:V_QX], ax
        mov [cs:V_QY], dx
        mov [cs:V_QW], bx
        mov [cs:V_QH], cx
        add bx, ax
        add cx, dx
        call hide
        mov bx, [bp+FAR_A]
        and bx, 15
        mov al, [cs:bx+VMAP]
        call qfill
.x:     jmp ui_done

; ---- 16: uk_theme: AX theme
ui_theme:
        UIIN
        mov cx, 15              ; sizeof (UkTheme)
        mul cx
        add ax, [cs:P_THEME]
        mov dx, [cs:P_THEME+2]
        les di, [cs:P_UK]
        mov [es:di], ax
        mov [es:di+2], dx
        jmp ui_done

; ---- 17: uk_copy: AX sx, DX sy, BX dx, CX dy; w, h
ui_copy:
        UIIN
        push ax
        push dx
        push bx
        push cx
        mov ax, bx
        mov dx, cx
        add bx, [bp+FAR_A]
        add cx, [bp+FAR_A+2]
        call hide
        pop ax                  ; dy
        pop dx                  ; dx
        call scr_off
        pop ax                  ; sy
        pop dx                  ; sx
        push di
        call scr_off
        mov si, di
        pop di
        mov ax, 0A000h
        mov ds, ax
        mov es, ax
        mov bx, [bp+FAR_A]      ; w
        mov dx, [bp+FAR_A+2]    ; h
        or bx, bx
        jle .x
        or dx, dx
        jle .x
        mov ax, ROW
        cmp di, si
        jbe .down
        ; upwards from the last row (the rectangles may overlap)
        push ax
        push dx
        dec dx
        mul dx
        pop dx
        add si, ax
        add di, ax
        pop ax
        neg ax
.down:  sub ax, bx
.row:   mov cx, bx
        rep movsb
        add si, ax
        add di, ax
        dec dx
        jnz .row
.x:     jmp ui_done

; ---- 18: hud_text_cga (near): AX col, DX row, BX colour; s, len
ui_hudtext:
        UIIN
        and bx, 15
        mov bl, [cs:bx+VMAP]
        mov cl, 3
        shl ax, cl
        shl dx, cl
        xchg ax, dx
        call scr_off
        mov ax, 0A000h
        mov es, ax
        lds si, [bp+NEAR_A]
        mov cx, [bp+NEAR_A+4]
        or cx, cx
        jle .x
.ch:    push cx
        push si
        push ds
        lodsb
        and ax, 7Fh
        mov cl, 3
        shl ax, cl
        add ax, 0FA6Eh          ; the BIOS's 8 x 8 font
        mov si, ax
        mov ax, 0F000h
        mov ds, ax
        mov dx, 8
.r:     lodsb
        mov ah, al
        mov cx, 8
.p:     xor al, al
        shl ah, 1
        jnc .b
        mov al, bl
.b:     stosb
        loop .p
        add di, ROW - 8
        dec dx
        jnz .r
        sub di, ROW * 8 - 8     ; the next cell
        pop ds
        pop si
        inc si
        pop cx
        loop .ch
.x:     jmp ui_done

; ---- 19: hud_clear_rows: AX row, DX count
ui_hudclear:
        UIIN
        mov cl, 3
        shl ax, cl
        add dx, dx
        add dx, dx
        add dx, dx              ; pixel rows
        mov word [cs:V_QX], 0
        mov word [cs:V_QW], ROW
        mov [cs:V_QY], ax
        mov [cs:V_QH], dx
        mov cx, dx
        add cx, ax
        mov dx, ax
        xor ax, ax
        mov bx, ROW
        call hide
        xor al, al
        call qfill
        jmp ui_done

; The pointer's box at AX x, DX y: V_MX, V_MY, V_MW (<= 9), V_MH (<= 14);
; DI its screen offset, ES = A000.
mbox:
        mov [cs:V_MX], ax
        mov [cs:V_MY], dx
        mov cx, ROW
        sub cx, ax
        cmp cx, 9
        jbe .w
        mov cx, 9
.w:     mov [cs:V_MW], cx
        mov cx, 200
        sub cx, dx
        cmp cx, 14
        jbe .h
        mov cx, 14
.h:     mov [cs:V_MH], cx
        xchg ax, dx
        call scr_off
        mov ax, 0A000h
        mov es, ax
        ret

; ---- 20: ms_draw_cga (near): AX x, DX y
ui_msdraw:
        UIIN
        call mbox
        mov si, di              ; the background to MSAVE (rows of 9)
        push es
        pop ds
        push cs
        pop es
        mov di, MSAVE
        mov dx, [cs:V_MH]
.s:     mov cx, [cs:V_MW]
        push si
        push di
        rep movsb
        pop di
        pop si
        add si, ROW
        add di, 9
        dec dx
        jnz .s
        mov ax, [cs:V_MX]
        mov dx, [cs:V_MY]
        call mbox
        lds bx, [cs:P_PM]
        mov si, [bx]            ; g_ms_pm: the mask (near, DGROUP)
        cmp byte [si], 80h
        jne .busy
        ; the arrow: OpenXcom's Cursor, 9 x 13 (0 clear)
        push cs
        pop ds
        mov si, CURSOR
        mov dx, [cs:V_MH]
        cmp dx, 13
        jbe .ar
        mov dx, 13
.ar:    push si
        push di
        mov cx, [cs:V_MW]
.ap:    lodsb
        or al, al
        jz .a0
        mov [es:di], al
.a0:    inc di
        loop .ap
        pop di
        pop si
        add si, 9
        add di, ROW
        dec dx
        jnz .ar
        jmp short .x
.busy:  ; the hourglass: mask black, fill white (8 x 14 bits)
        push ds
        lds bx, [cs:P_PF]
        mov bx, [bx]            ; g_ms_pf
        pop ds
        mov dx, [cs:V_MH]
.br:    mov ah, [si]
        mov al, [bx]
        inc si
        inc bx
        push di
        mov cx, [cs:V_MW]
        cmp cx, 8
        jbe .bp
        mov cx, 8
.bp:    shl ah, 1
        jnc .b0
        push bx
        mov bl, [cs:VMAP]
        test al, 80h
        jz .bk
        mov bl, [cs:VMAP+15]
.bk:    mov [es:di], bl
        pop bx
.b0:    shl al, 1
        inc di
        loop .bp
        pop di
        add di, ROW
        dec dx
        jnz .br
.x:     jmp ui_done

; ---- 21: ms_erase_cga (near): AX x, DX y (as drawn)
ui_mserase:
        UIIN
        mov ax, [cs:V_MX]
        mov dx, [cs:V_MY]
        call mbox
        push cs
        pop ds
        mov si, MSAVE
        mov dx, [cs:V_MH]
.r:     mov cx, [cs:V_MW]
        push si
        push di
        rep movsb
        pop di
        pop si
        add si, 9
        add di, ROW
        dec dx
        jnz .r
        jmp ui_done

; ---- 22: render_arrow: AX c0, DX r0, BX cols, CX rows (every register
; kept). The selected unit's arrow, 9 x 9 (OpenXcom Map::init: yellow 16,
; black 15), into the back buffer over the cells shown.
ui_arrow:
        push ax
        UIIN
        les di, [cs:P_ARROW]
        cmp word [es:di], 0
        je .x
        lds si, [cs:P_VIEWZ]
        mov si, [si]
        cmp [es:di+6], si
        jg .x
        ; the rectangle: x [c0 * 8, (c0 + cols) * 8), y [r0, r0 + rows)
        push cx
        mov cl, 3
        shl ax, cl
        shl bx, cl
        pop cx
        mov [cs:V_QX], ax
        mov [cs:V_QW], bx
        mov [cs:V_QY], dx
        mov [cs:V_QH], cx
        ; the arrow's top left: g_arrow[1] + cam_x, g_arrow[2] + cam_y
        lds si, [cs:P_CAMX]
        mov ax, [si]
        add ax, [es:di+2]
        mov [cs:V_T0], ax
        lds si, [cs:P_CAMY]
        mov ax, [si]
        add ax, [es:di+4]
        mov [cs:V_T1], ax
        lds si, [cs:P_BUFSEG]
        mov es, [si]
        push cs
        pop ds
        mov si, L(arrow_px)     ; 81 pixels: 0 clear, else the colour
        xor dx, dx              ; row
.row:   mov ax, [cs:V_T1]
        add ax, dx
        cmp ax, [cs:V_QY]
        jl .skiprow
        mov cx, [cs:V_QY]
        add cx, [cs:V_QH]
        cmp ax, cx
        jge .skiprow
        cmp ax, VIEW_H
        jae .skiprow
        push dx
        mov cx, ROW
        mul cx
        pop dx
        mov di, ax              ; the row in the buffer
        xor bx, bx              ; column
.col:   mov al, [si+bx]
        or al, al
        jz .nc
        mov cx, [cs:V_T0]
        add cx, bx
        cmp cx, [cs:V_QX]
        jl .nc
        push ax
        mov ax, [cs:V_QX]
        add ax, [cs:V_QW]
        cmp cx, ax
        pop ax
        jge .nc
        cmp cx, ROW
        jae .nc
        push di
        add di, cx
        mov [es:di], al
        pop di
.nc:    inc bx
        cmp bx, 9
        jb .col
.skiprow:
        add si, 9
        inc dx
        cmp dx, 9
        jb .row
.x:     pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop bp
        pop ax
        retf

; ---- 24: gfx_night (AX night): the shade of the view's present, as DOS
; shades a tile (OpenXcom ShaderDraw's StandardShade: index + shade, at
; most the last of its 16-colour ramp; 0 stays black). NIGHT_SHADE: no
; light but the sky's (Tile::getShade: 15 - light; the night sky lights
; 15 - global shade).
; ---- 25: gfx_dot (AX x, DX y, BX EGA colour): 2 x 2 pixels XORed on the
; screen (drawn again: gone), not while Hidden Movement covers the view
ui_dot:
        UIIN
        and ax, 0FFFEh
        cmp ax, 318
        ja .x
        cmp dx, VIEW_H - 2
        ja .x
        les di, [cs:P_VOFF]
        cmp word [es:di], 0
        jne .x
        and bx, 15
        mov bl, [cs:bx+VMAP]
        push bx
        push ax
        mov bx, ax
        add bx, 2
        mov cx, dx
        add cx, 2
        call hide               ; (AX lost)
        pop ax
        pop bx
        xchg ax, dx
        call scr_off
        mov ax, 0A000h
        mov es, ax
        xor [es:di], bl
        xor [es:di+1], bl
        xor [es:di+ROW], bl
        xor [es:di+ROW+1], bl
.x:     jmp ui_done

NIGHT_SHADE equ 10
ui_night:
        UIIN
        xor dx, dx
        or ax, ax
        jz .set
        mov dx, NIGHT_SHADE
        push cs                 ; the lights' working data in PAL (the DAC
        pop es                  ; has the battle's palette)
        xor di, di
        xor ax, ax
        mov cx, LD_AUX
        rep stosb
        dec ax
        mov cx, 360
        rep stosb               ; no level shown yet
        mov [cs:LD(LD_TL)], ax  ; no table
        mov word [cs:LD(LD_TSUM)], 0A5A5h
        mov word [cs:LD(LD_FORCE)], 1
.set:   mov [cs:V_SHADE], dx
        jmp ui_done

; ------------------------------------------------------------------
; Night lights (OpenXcom TileEngine::calculateTerrainLighting,
; calculateUnitLighting, addLight; Tile::getShade; ShaderDraw's
; StandardShade): at night the view goes to the screen cell by cell (16 x
; 8 pixels, 20 x 18 of them), each through the shade of its tile: 15 less
; the tile's light, the most of the night sky's (15 - NIGHT_SHADE) and of
; every light's power less its distance rounded (the X-COM units' own
; light, burning units, fires and electro-flares 15; the terrain's light
; sources their MCD value). Light reaches every level of a column, as
; DOS's; a cell's tile is the one at the view's level under the cell's
; centre (a level below is drawn 24 pixels lower, so its tiles' cells are
; up to 1.5 tiles off, as nlit.c's on EGA). The back buffer keeps the day
; picture. At a present: the sources that can reach the view and a
; signature of them, the camera and the level; when it changed, every
; cell's level again, and the cells whose level changed are shown too.
; One 256-byte table (SHADE, in the buffer's segment: no CS prefix) per
; level shown. On a scroll by whole cells the screen moves too (vshift),
; with the levels shown, and only the cells that changed are shown. The
; far pointers are at 0B20 (vgainit.c); the working data lives in PAL
; (unused once the battle's palette is in the DAC), cleared by ui_night.

SUN     equ 15 - NIGHT_SHADE
NSRC    equ 48                  ; sources kept (those that can reach the view)
NTSRC   equ 24                  ; the terrain's
LD_SG   equ 0                   ; signature being made
LD_SIG  equ 2                   ; the shown one's
LD_TSUM equ 4                   ; the terrain light map's sum (pak.c Pack.lit)
LD_NT   equ 6
LD_NS   equ 8
LD_RELIT equ 10
LD_X0   equ 12                  ; the present's rectangle, pixels
LD_X1   equ 14
LD_Y0   equ 16
LD_Y1   equ 18
LD_LEV  equ 20
LD_TL   equ 22                  ; SHADE's level
LD_MASK equ 24                  ; levels to show
LD_A0   equ 26                  ; cell (0, 0)'s centre: x - y and x + y, in 16ths
LD_B0   equ 28
LD_BUF  equ 30
LD_DG   equ 32                  ; DGROUP
LD_FORCE equ 34
LD_HOOK equ 36
LD_T0   equ 38
LD_T1   equ 40
LD_T2   equ 42
LD_FP   equ 44                  ; a far pointer (the present hook's call)
LD_NREL equ 48                  ; counts (BENCH): relights, presents
LD_NPRE equ 50
LD_C0   equ 52                  ; bytes: first column, past the last,
LD_C1   equ 53                  ; first row, past the last
LD_R0   equ 54
LD_R1   equ 55
LD_SHIFTED equ 56               ; the screen moved: the rectangle's cells
                                ; are shown only if they changed
LD_PART equ 58                  ; only the cells of unknown level (vshift)
LD_CA   equ 60                  ; the camera of the levels (A0, B0)
LD_CB   equ 62
LD_TSRC equ 64                  ; NTSRC x (x, y, power)
LD_SRC  equ LD_TSRC + NTSRC * 3 ; NSRC x (x, y, power)
LD_LTAB equ LD_SRC + NSRC * 3   ; the terrain's light parts (8 x part, power)
LD_RM   equ LD_LTAB + 16        ; each row's levels to show (bits)
LD_AUX  equ LD_RM + 36          ; 332 (gfx.h VGA_AUX): 360 cells, each its
                                ; level << 4 | the level shown (14: show
                                ; the rectangle's part, 15: show it whole)
%if LD_AUX + 360 > 768
%error the lights' data does not fit in PAL
%endif
; from vgainit.c (far pointers, then words):
LD_PUNITS equ 0B20h             ; g_units
LD_PNUN equ 0B24h               ; &g_nunits
LD_PFIRE equ 0B28h              ; &fire (a near pointer)
LD_PNFIRE equ 0B2Ch             ; &n_fire
LD_PDEV equ 0B30h               ; &dev
LD_PNDEV equ 0B34h              ; &n_dev
LD_PLIT equ 0B38h               ; &g_pak.lit
LD_PLIGHTS equ 0B3Ch            ; g_pak.lights
LD_PNL  equ 0B40h               ; &g_pak.n_lights
LD_PMAP equ 0B44h               ; &g_map
LD_PMX  equ 0B48h               ; &g_mx, &g_my, &g_mz
LD_PMY  equ 0B4Ch
LD_PMZ  equ 0B50h
LD_PHOOK equ 0B54h              ; &g_present_hook
LD_USIZE equ 0B58h              ; sizeof (Unit), offsets of x, faction, alive, burning
LD_UX   equ 0B5Ah
LD_UFAC equ 0B5Ch
LD_UALIVE equ 0B5Eh
LD_UBURN equ 0B60h
LD_DSIZE equ 0B62h              ; sizeof (Device)
LD_BUFSEG equ 0B64h             ; g_bufseg

; round(sqrt(dx * dx + dy * dy)), dx and dy 0-9 (OpenXcom addLight)
rt_tab:
%assign a 0
%rep 10
%assign b 0
%rep 10
%assign d2 a * a + b * b
%assign r 0
%rep 16
%if r * r + r < d2
%assign r r + 1
%endif
%endrep
        db r
%assign b b + 1
%endrep
%assign a a + 1
%endrep

; SHADE for level DL (StandardShade): per 16-colour block the entries
; moved DL down, the last DL at the block's end; 0 stays 0. Uses AX, BX,
; CX, DX, SI, DI, ES.
mktab:
        mov [cs:LD(LD_TL)], dl
        mov byte [cs:LD(LD_TL)+1], 0
        mov es, [cs:LD(LD_BUFSEG)]
        mov di, SHADE
        mov bl, dl
        xor bh, bh
        mov si, bx
        shl si, 1
        add si, bx
        add si, L(.inc)         ; past DL of the 16 steps
        xor dh, dh              ; the block's first index
.blk:   mov al, dh
        add al, dl
        jmp si
.inc:
%rep 16
        stosb
        inc al
%endrep
        mov al, dh
        or al, 0Fh
        mov cl, dl
        xor ch, ch
        rep stosb
        add dh, 16
        jnz .blk
        mov byte [es:SHADE], 0
        ret

; ---- 5 at night: op_present's arguments (begin's frame), DS = DGROUP
lit_present:
        mov [cs:LD(LD_DG)], ds
        mov ax, [bp+6]
        mov [cs:LD(LD_BUF)], ax
        mov ax, [bp+8]
        shl ax, 1
        shl ax, 1
        mov [cs:LD(LD_X0)], ax
        mov cx, [bp+12]
        or cx, cx
        jnz .c1
        jmp .x
.c1:
        shl cx, 1
        shl cx, 1
        add ax, cx
        mov [cs:LD(LD_X1)], ax
        mov ax, [bp+10]
        mov [cs:LD(LD_Y0)], ax
        mov cx, [bp+14]
        or cx, cx
        jnz .c2
        jmp .x
.c2:
        add ax, cx
        mov [cs:LD(LD_Y1)], ax
        inc word [cs:LD(LD_NPRE)]
        call sources            ; AX = the signature
        mov word [cs:LD(LD_RELIT)], 0
        mov word [cs:LD(LD_PART)], 0
        cmp word [cs:LD(LD_FORCE)], 0
        jne .new
        cmp ax, [cs:LD(LD_SIG)]
        jne .new
        mov bx, [cs:LD(LD_A0)]
        cmp bx, [cs:LD(LD_CA)]
        jne .cam
        mov bx, [cs:LD(LD_B0)]
        cmp bx, [cs:LD(LD_CB)]
        je .same
.cam:   cmp word [cs:LD(LD_SHIFTED)], 0
        je .new
        mov word [cs:LD(LD_PART)], 1     ; the levels moved with the cells
.new:   mov [cs:LD(LD_SIG)], ax
        mov ax, [cs:LD(LD_A0)]
        mov [cs:LD(LD_CA)], ax
        mov ax, [cs:LD(LD_B0)]
        mov [cs:LD(LD_CB)], ax
        mov word [cs:LD(LD_FORCE)], 0
        call levels
        mov word [cs:LD(LD_RELIT)], 1
        inc word [cs:LD(LD_NREL)]
        mov ds, [cs:LD(LD_DG)]
        xor ax, ax              ; cells outside the rectangle may change
        xor dx, dx
        mov bx, 320
        mov cx, VIEW_H
        call hide
.same:  push cs
        pop ds
        push cs
        pop es
        call mark
        call draw
        cmp word [cs:LD(LD_HOOK)], 0
        je .x
        mov ds, [cs:LD(LD_DG)]  ; the panel's markers again (gfx.c)
        les bx, [cs:LD(LD_PHOOK)]
        mov ax, [es:bx]
        mov dx, [es:bx+2]
        mov [cs:LD(LD_FP)], ax
        mov [cs:LD(LD_FP)+2], dx
        or ax, dx
        jz .x
        xor ax, ax
        mov dx, VIEW_H
        call far [cs:LD(LD_FP)]
.x:     mov word [cs:LD(LD_SHIFTED)], 0
        jmp done

; A source AL x, AH y, BL power into SRC at ES:DI (ES = CS) if it can
; light a cell of the view (within 9 tiles: x - y and x + y within 18 of
; the cells'); into the signature. Keeps all but AX, BX.
addsrc:
        push dx
        mov dx, ax
        mov ax, [cs:LD(LD_SG)]
        rol ax, 1
        xor ax, dx
        add al, bl
        mov [cs:LD(LD_SG)], ax
        mov ax, dx
        pop dx
        cmp word [cs:LD(LD_NS)], NSRC
        jae .r
        push cx
        push dx
        mov dx, ax
        xor ah, ah
        mov cl, dh
        xor ch, ch
        push ax
        sub ax, cx              ; x - y
        shl ax, 1
        shl ax, 1
        shl ax, 1
        shl ax, 1
        sub ax, [cs:LD(LD_A0)]
        add ax, 304
        cmp ax, 304 + 304 + 304
        pop ax
        ja .no
        add ax, cx              ; x + y
        shl ax, 1
        shl ax, 1
        shl ax, 1
        shl ax, 1
        sub ax, [cs:LD(LD_B0)]
        add ax, 304
        cmp ax, 272 + 304 + 304
        ja .no
        mov ax, dx
        stosw
        mov al, bl
        stosb
        inc word [cs:LD(LD_NS)]
.no:    pop dx
        pop cx
.r:     ret

; The terrain's sources (TSRC) again when its light map changed (pak.c
; pak_light_map: at the setup, and after a source broke): floors and
; objects among g_pak.lights, every level. Uses all but BP; DS = DGROUP
; after.
terrain:
        les bx, [cs:LD(LD_PLIT)]
        lds si, [es:bx]         ; g_pak.lit
        xor dx, dx
        mov ax, ds
        mov [cs:LD(LD_T1)], ax
        or [cs:LD(LD_T1)], si   ; 0: none (by day, or no light sources)
        jz .sum
        les bx, [cs:LD(LD_PMX)]
        mov ax, [es:bx]
        les bx, [cs:LD(LD_PMY)]
        mul word [es:bx]
        add ax, 7
        mov cl, 3
        shr ax, cl
        mov cx, ax
        mov dx, ds
        xor dx, si
        jcxz .sum
.s:     lodsb
        rol dx, 1
        add dl, al
        loop .s
.sum:   cmp dx, [cs:LD(LD_TSUM)]
        je .x
        mov [cs:LD(LD_TSUM)], dx
        mov word [cs:LD(LD_NT)], 0
        cmp word [cs:LD(LD_T1)], 0
        je .x
        les bx, [cs:LD(LD_PNL)]
        mov cl, [es:bx]
        xor ch, ch
        cmp cx, 8
        jbe .n
        mov cx, 8
.n:     or cx, cx
        jnz .c3
        jmp .x
.c3:
        mov [cs:LD(LD_T2)], cx  ; the parts
        lds si, [cs:LD(LD_PLIGHTS)]
        push cs
        pop es
        mov di, LD(LD_LTAB)
        shl cx, 1
        rep movsb
        les bx, [cs:LD(LD_PMZ)]
        mov ax, [es:bx]
        les bx, [cs:LD(LD_PMY)]
        mul word [es:bx]
        mov [cs:LD(LD_T0)], ax  ; the map's rows, all levels
        or ax, ax
        jz .x
        les bx, [cs:LD(LD_PMAP)]
        lds si, [es:bx]
        mov di, LD(LD_TSRC)
        xor dx, dx              ; dl = x, dh = y
.row:   les bx, [cs:LD(LD_PMX)]
        mov cx, [es:bx]
        push cs
        pop es
        xor dl, dl
        jcxz .nr
.t:     mov al, [si]            ; PART_FLOOR
        mov ah, [si+3]          ; PART_OBJECT
        or ax, ax
        jz .tn
        push cx
        mov cx, [cs:LD(LD_T2)]
        mov bx, LD(LD_LTAB)
.k:     cmp al, [cs:bx]
        je .hit
        cmp ah, [cs:bx]
        je .hit
        add bx, 2
        loop .k
        jmp short .kn
.hit:   cmp word [cs:LD(LD_NT)], NTSRC
        jae .kn
        mov ax, dx
        stosw
        mov al, [cs:bx+1]
        stosb
        inc word [cs:LD(LD_NT)]
.kn:    pop cx
.tn:    add si, 4
        inc dl
        loop .t
.nr:    inc dh
        les bx, [cs:LD(LD_PMY)]
        mov ax, [es:bx]
        push cs
        pop es
        cmp dh, al
        jb .ny
        xor dh, dh
.ny:    dec word [cs:LD(LD_T0)]
        jnz .row
.x:     mov ds, [cs:LD(LD_DG)]
        ret

; The sources that can reach the view into SRC, and the signature of
; all sources (AX); the camera and level as A0, B0. Uses all but BP; DS = DGROUP after.
sources:
        les bx, [cs:P_CAMX]
        mov ax, [es:bx]
        neg ax
        sub ax, 8
        mov [cs:LD(LD_A0)], ax  ; A0 = -8 - cam_x
        mov dx, ax
        les bx, [cs:P_VIEWZ]
        mov ax, [es:bx]
        mov cx, ax
        shl ax, 1
        add ax, cx              ; 3z
        mov cl, 4
        shl ax, cl              ; 48z
        sub ax, 56
        les bx, [cs:P_CAMY]
        sub ax, [es:bx]
        sub ax, [es:bx]
        mov [cs:LD(LD_B0)], ax  ; B0 = 48z - 56 - 2 cam_y
        mov word [cs:LD(LD_SG)], 0
        call terrain
        mov word [cs:LD(LD_NS)], 0
        push cs
        pop es
        mov di, LD(LD_SRC)
        mov cx, [cs:LD(LD_NT)]
        jcxz .nt
        mov si, LD(LD_TSRC)
.t:     mov ax, [cs:si]
        mov bl, [cs:si+2]
        call addsrc
        add si, 3
        loop .t
.nt:    lds si, [cs:LD(LD_PNUN)]
        mov cx, [si]
        lds si, [cs:LD(LD_PUNITS)]
        jcxz .nu
.u:     mov bx, [cs:LD(LD_UALIVE)]
        cmp byte [si+bx], 0
        je .un
        mov bx, [cs:LD(LD_UFAC)]
        cmp byte [si+bx], 0     ; FACTION_XCOM
        je .ul
        mov bx, [cs:LD(LD_UBURN)]
        test byte [si+bx], 7Fh  ; burning (not UB_HIT alone)
        jz .un
.ul:    mov bx, [cs:LD(LD_UX)]
        mov ax, [si+bx]
        mov bl, 15
        call addsrc
.un:    add si, [cs:LD(LD_USIZE)]
        loop .u
.nu:    lds si, [cs:LD(LD_PNDEV)]
        mov cx, [si]
        lds si, [cs:LD(LD_PDEV)]
        mov si, [si]
        mov ds, [cs:LD(LD_DG)]
        jcxz .nd
.d:     cmp byte [si+3], 3      ; DEV_FLARE
        jne .dn
        mov ax, [si]
        mov bl, 15
        call addsrc
.dn:    add si, [cs:LD(LD_DSIZE)]
        loop .d
.nd:    lds si, [cs:LD(LD_PNFIRE)]
        mov cx, [si]
        lds si, [cs:LD(LD_PFIRE)]
        mov si, [si]
        mov ds, [cs:LD(LD_DG)]
        jcxz .nf
.f:     lodsw                   ; x, y (Spot: x, y, z, v)
        inc si
        inc si
        mov bl, 15
        call addsrc
        loop .f
.nf:    mov ax, [cs:LD(LD_SG)]
        ret

; Every cell's level into LV: 15 less the most light on its tile.
levels:
        push bp
        push ds
        push cs
        pop ds
        push cs
        pop es
        mov di, LD(LD_AUX)
        mov ax, [LD(LD_B0)]
        mov [LD(LD_T1)], ax     ; B
        mov byte [LD(LD_T2)], 18
.r:     mov bp, [LD(LD_A0)]     ; A
        mov byte [LD(LD_T2)+1], 20
.c:     cmp word [LD(LD_PART)], 0
        je .go
        cmp byte [di], 0F0h
        jae .go
        inc di                  ; (moved with its cell: known)
        jmp .nx
.go:    mov ax, bp
        add ax, [LD(LD_T1)]
        add ax, 16
        mov cl, 5
        sar ax, cl              ; x = (A + B + 16) >> 5
        cmp ax, -16
        jge .x1
        mov ax, -16
.x1:    cmp ax, 80
        jle .x2
        mov ax, 80
.x2:    mov dl, al
        mov ax, [LD(LD_T1)]
        sub ax, bp
        add ax, 16
        sar ax, cl              ; y = (B - A + 16) >> 5
        cmp ax, -16
        jge .y1
        mov ax, -16
.y1:    cmp ax, 80
        jle .y2
        mov ax, 80
.y2:    mov dh, al
        mov ah, SUN
        mov si, LD(LD_SRC)
        mov cx, [LD(LD_NS)]
        jcxz .put
.s:     lodsb
        sub al, dl
        jns .p1
        neg al
.p1:    cmp al, 9
        ja .s2
        mov bl, al
        lodsb
        sub al, dh
        jns .p2
        neg al
.p2:    cmp al, 9
        ja .s1
        mov bh, al
        mov al, bl
        add al, al
        mov bl, al
        add al, al
        add al, al
        add al, bl              ; 10 dx
        add al, bh
        mov bl, al
        xor bh, bh
        lodsb
        sub al, [bx+L(rt_tab)]
        cmp al, ah
        jle .n
        mov ah, al
        jmp short .n
.s2:    inc si
.s1:    inc si
.n:     loop .s
.put:   mov al, 15
        sub al, ah
        mov cl, 4
        shl al, cl
        mov ah, [di]
        and ah, 0Fh
        or al, ah
        stosb                   ; the level, the shown one kept
.nx:    add bp, 16
        dec byte [LD(LD_T2)+1]
        jnz .c
        add word [LD(LD_T1)], 16
        dec byte [LD(LD_T2)]
        jnz .r
        pop ds
        pop bp
        ret

; The cells to show: in AUX the level shown becomes 15 (show the whole
; cell: its level changed) or 14 (show its part in the rectangle), with
; MASK and each row's RM the levels among them; HOOK: a changed cell under
; the panel's markers (gfx.c GFX_HOOK_C). Without a relight only the
; rectangle's cells (C0-R1; draw looks at the same); after vshift the
; rectangle shows only what changed. DS = ES = CS.
mark:
        mov ax, [LD(LD_X0)]
        mov cl, 4
        shr ax, cl
        mov [LD(LD_T0)], al     ; first column
        mov ax, [LD(LD_X1)]
        add ax, 15
        shr ax, cl
        mov [LD(LD_T0)+1], al   ; past the last
        mov ax, [LD(LD_Y0)]
        mov cl, 3
        shr ax, cl
        mov [LD(LD_T1)], al
        mov ax, [LD(LD_Y1)]
        add ax, 7
        shr ax, cl
        mov [LD(LD_T1)+1], al
        mov ax, [LD(LD_T0)]
        mov bx, [LD(LD_T1)]
        cmp word [LD(LD_RELIT)], 0
        je .rng
        mov ax, 20 << 8
        mov bx, 18 << 8
.rng:   mov [LD(LD_C0)], ax
        mov [LD(LD_R0)], bx
        mov word [LD(LD_MASK)], 0
        mov word [LD(LD_HOOK)], 0
        push bp
        mov dh, [LD(LD_R0)]
.r:     mov dl, [LD(LD_C0)]
        call cellat
        xor bp, bp              ; the row's levels
.c:     mov al, [di]
        mov ah, al
        mov cl, 4
        shr ah, cl              ; the level
        and al, 0Fh             ; the one shown
        cmp al, ah
        je .same
        mov al, 0Fh             ; changed: the whole cell
        cmp dl, 18
        jb .m
        mov word [LD(LD_HOOK)], 1
        jmp short .m
.same:  cmp word [LD(LD_SHIFTED)], 0
        jne .s
        cmp dh, [LD(LD_T1)]
        jb .s
        cmp dh, [LD(LD_T1)+1]
        jae .s
        cmp dl, [LD(LD_T0)]
        jb .s
        cmp dl, [LD(LD_T0)+1]
        jae .s
        mov al, 0Eh             ; in the rectangle
.m:     mov cl, ah
        mov bx, 1
        shl bx, cl
        or [LD(LD_MASK)], bx
        or bp, bx
.s:     mov cl, 4
        shl ah, cl
        or al, ah
        stosb
        inc dl
        cmp dl, [LD(LD_C1)]
        jb .c
        mov bl, dh
        xor bh, bh
        shl bx, 1
        mov [bx+LD(LD_RM)], bp
        inc dh
        cmp dh, [LD(LD_R1)]
        jb .r
        pop bp
        ret

; DI = AUX at cell DL column, DH row. Uses AX.
cellat: mov al, dh
        mov ah, 20
        mul ah
        add al, dl
        adc ah, 0
        add ax, LD(LD_AUX)
        mov di, ax
        ret

; Shows the marked cells, level by level; they are shown at their level
; after. DS = ES = CS.
draw:
        mov word [LD(LD_LEV)], 0
.l:     mov cx, [LD(LD_LEV)]
        mov ax, 1
        shl ax, cl
        mov [LD(LD_T0)], ax
        test [LD(LD_MASK)], ax
        jz .nl
        jcxz .scan
        cmp cx, [LD(LD_TL)]
        je .scan
        mov dl, cl
        call mktab
        push cs
        pop es
.scan:  mov dh, [LD(LD_R0)]
.r:     mov bl, dh
        xor bh, bh
        shl bx, 1
        mov ax, [bx+LD(LD_RM)]
        test ax, [LD(LD_T0)]
        jz .nr                  ; none of this level in the row
        mov dl, [LD(LD_C0)]
        call cellat
        mov si, di
.c:     lodsb
        mov ah, al
        and al, 0Fh
        cmp al, 0Eh
        jb .nc
        mov bl, ah
        mov cl, 4
        shr bl, cl
        cmp bl, [LD(LD_LEV)]
        jne .nc
        and ah, 0F0h
        or ah, bl
        mov [si-1], ah          ; shown at its level
        xor ah, ah
        cmp al, 0Fh
        jne .p
        mov ah, 32              ; whole
.p:     push si
        push dx
        call cell
        pop dx
        pop si
.nc:    inc dl
        cmp dl, [LD(LD_C1)]
        jb .c
.nr:    inc dh
        cmp dh, [LD(LD_R1)]
        jb .r
.nl:    inc word [LD(LD_LEV)]
        cmp word [LD(LD_LEV)], 16
        jb .l
        ret

; Cell DL column, DH row, AH 32: the whole cell, else its part in the
; rectangle (8 or 16 pixels wide); through SHADE unless at level 0. DS =
; CS; ES = CS after.
cell:
        mov [LD(LD_T2)], ah
        mov al, dl
        xor ah, ah
        mov cl, 4
        shl ax, cl
        mov bx, ax              ; x0
        add ax, 16
        mov cx, ax              ; x1
        mov al, dh
        xor ah, ah
        shl ax, 1
        shl ax, 1
        shl ax, 1
        mov dx, ax              ; y0
        add ax, 8               ; y1
        test byte [LD(LD_T2)], 32
        jnz .whole
        cmp bx, [LD(LD_X0)]
        jge .a
        mov bx, [LD(LD_X0)]
.a:     cmp cx, [LD(LD_X1)]
        jle .b
        mov cx, [LD(LD_X1)]
.b:     cmp dx, [LD(LD_Y0)]
        jge .c
        mov dx, [LD(LD_Y0)]
.c:     cmp ax, [LD(LD_Y1)]
        jle .whole
        mov ax, [LD(LD_Y1)]
.whole: sub ax, dx              ; rows
        sub cx, bx
        shr cx, 1               ; words: 4 or 8
        push ax
        mov ax, ROW
        mul dx
        add ax, bx
        mov si, ax
        mov di, ax
        pop dx                  ; rows
        push ds
        mov ax, 0A000h
        mov es, ax
        cmp word [LD(LD_LEV)], 0
        mov ds, [cs:LD(LD_BUF)]
        je .copy
        mov bx, SHADE           ; (in DS)
        cmp cx, 4
        je .s4
.s8:
%rep 8
        lodsw
        xlatb
        xchg al, ah
        xlatb
        xchg al, ah
        stosw
%endrep
        add si, ROW - 16
        add di, ROW - 16
        dec dx
        jnz .s8
        jmp short .x
.s4:
%rep 4
        lodsw
        xlatb
        xchg al, ah
        xlatb
        xchg al, ah
        stosw
%endrep
        add si, ROW - 8
        add di, ROW - 8
        dec dx
        jnz .s4
        jmp short .x
.copy:  mov ax, cx
        mov bx, ROW
        sub bx, cx
        sub bx, cx
.crow:  mov cx, ax
        rep movsw
        add si, bx
        add di, bx
        dec dx
        jnz .crow
.x:     pop ds
        push cs
        pop es
        ret

; op_shift at night (DX the move in bytes, DS = DGROUP): when the view
; is shown and moves by whole cells (16 pixels, 8 rows), the screen moves
; too, and so do the cells' levels and the levels shown; the cells that
; came in from outside are of unknown level (15: the next present works
; out only those when no light moved), and those and the panel's markers'
; columns (18-19) are to be shown (15). Keeps DX.
vshift:
        push dx
        les bx, [cs:P_VOFF]
        cmp word [es:bx], 0
        jne .no
        mov ax, dx
        cwd
        mov cx, ROW
        idiv cx                 ; AX rows, DX pixels
        cmp dx, 160
        jle .a
        sub dx, ROW
        inc ax
.a:     cmp dx, -160
        jge .b
        add dx, ROW
        dec ax
.b:     test al, 7
        jnz .no
        test dl, 15
        jnz .no
        mov cl, 3
        sar ax, cl
        mov [cs:LD(LD_T0)], ax  ; cell rows
        mov cl, 4
        sar dx, cl
        mov [cs:LD(LD_T1)], dx  ; cell columns
        xor ax, ax
        xor dx, dx
        mov bx, 320
        mov cx, VIEW_H
        call hide
        pop dx
        push dx
        push ds
        mov ax, 0A000h
        mov ds, ax
        mov es, ax
        call vmove
        push cs
        pop ds
        push cs
        pop es
        mov ax, [LD(LD_T0)]
        mov cx, 20
        imul cx
        add ax, [LD(LD_T1)]
        mov [LD(LD_T2)], ax     ; k: a cell came from k cells before
        or ax, ax
        jg .back
        mov di, LD(LD_AUX)
        xor dx, dx              ; dl column, dh row
.f:     call vcell
        mov [di], al
        inc di
        inc dl
        cmp dl, 20
        jb .f
        xor dl, dl
        inc dh
        cmp dh, 18
        jb .f
        jmp short .done
.back:  mov di, LD(LD_AUX) + 359
        mov dx, 17 << 8 | 19
.bk:    call vcell
        mov [di], al
        dec di
        dec dl
        jns .bk
        mov dl, 19
        dec dh
        jns .bk
.done:
        mov word [LD(LD_SHIFTED)], 1
        pop ds
.no:    pop dx
        ret

; vshift: cell DL, DH (DI) from cell DI - k: its level and the one shown
; (unknown, 15, from outside the view, the shown one also from or to the
; markers' columns). AL the byte. Uses AH, BX.
vcell:  mov al, dh
        sub al, [LD(LD_T0)]
        cmp al, 18
        jae .none
        mov ah, dl
        sub ah, [LD(LD_T1)]
        cmp ah, 20
        jae .none
        mov bx, di
        sub bx, [LD(LD_T2)]
        mov al, [bx]
        cmp ah, 18
        jae .lo
        cmp dl, 18
        jb .r
.lo:    or al, 0Fh
.r:     ret
.none:  mov al, 0FFh
        ret

;  ---- 23: fxtile_cga (cdecl, src/fxasm.asm's arguments): a tile's dithered
; overlay, shape bits AND the row's pattern AND the enabled 8-pixel columns
op_fx:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        cmp word [cs:P_SMOKE+2], 0
        je .dith
        call fxpic              ; DOS's pictures (below)
        jnc .x
.dith:  mov es, [bp+6]
        mov ax, [bp+18]         ; the patterns of rows 0-3 (in V_SKIP, V_ROWS)
        mov [cs:V_SKIP], ax
        mov ax, [bp+20]
        mov [cs:V_ROWS], ax
        mov ax, [bp+14]         ; first row
        mov [cs:V_T0], ax
        mov cx, ROW
        mul cx
        mov di, [bp+8]
        shl di, 1
        shl di, 1
        add di, ax
        mov al, [bp+22]
        and ax, 3
        mov bx, ax
        mov bl, [cs:bx+L(fx_col)]
        mov dl, [cs:bx+VMAP]    ; the colour (after the MUL)
        mov cx, [bp+16]
        sub cx, [bp+14]
        jle .x
        lds si, [bp+10]
        mov ax, [bp+14]
        shl ax, 1
        shl ax, 1
        add si, ax
.row:   mov bx, [cs:V_T0]
        and bx, 3
        mov dh, [cs:bx+V_SKIP]  ; the row's pattern
        push cx
        push di
        mov cx, 4
        mov ah, [bp+24]         ; the columns
.b:     lodsb
        and al, dh
        shr ah, 1
        jc .on
        xor al, al
.on:    or al, al
        jz .nb
        push di
.px:    shl al, 1
        jnc .n
        mov [es:di], dl
.n:     inc di
        or al, al
        jnz .px
        pop di
.nb:    add di, 8
        loop .b
        pop di
        pop cx
        add di, ROW
        inc word [cs:V_T0]
        loop .row
.x:     jmp done

; DOS's smoke and fire pictures (OpenXcom Map::drawTerrain, SMOKE.PCK;
; SMOKEV.PAK, loaded by vgainit.c: P_SMOKE -> far &smoke, far &n_smoke,
; then the pack's u16 offsets[10] and its sprites: fire frames 0-3, smoke
; frames 8-13, a burning unit's 4-7). A smoke tile: frame 8 + its
; thickness (smoke 1-6, 7-12, 13-15) + its step; a fire: frame 0 + the
; step; a burning unit: frame 4 + the step. A tile's step is its own
; start (OpenXcom picks one at random per tile; here mixed from its
; place) + V_STEP, which pics_next moves on every 4 BIOS ticks (220 ms,
; as OpenXcom's _animFrame / 2), handing bplayer.c's idle loop the parts
; of the view to redraw a few tiles at a time. The tile is found in the smoke or fire list
; by its box (op_fx's offset); then the picture is clipped to the
; redrawn rows (op_clear's). Fire's
; inner flames and a flare's glow are not drawn (the picture has its
; flames; the night lights light the flare's tiles); a burning unit's
; flames, not in the fire list, get frame 4-7 (a burning unit) at the rows given
; (effects.c overlay gives VGA the whole box's rows, not the shape's).
; Called from op_fx (its frame); CF set: dither as before.
V_CY0   equ 030Eh               ; op_clear's rows [V_CY0, V_CY1)
V_CY1   equ 0310h
V_FR    equ 0312h               ; the frame
V_FTOP  equ 0314h
V_FBOT  equ 0316h
V_STEP  equ 09DCh               ; the pictures' step (pics_next)
V_OLD   equ 09DDh               ; ... before the pass (tiles not yet redrawn)
V_NXT   equ 09DEh               ; pics_next: the next of fire + smoke
fxpic:
        mov al, [bp+22]         ; the colour * 55h
        mov ah, [bp+18]
        or ah, [bp+19]
        or ah, [bp+20]
        or ah, [bp+21]          ; the pattern's bits
        cmp al, 0FFh
        jne .nf
        cmp word [bp+18], 0FFFFh        ; colour 3: the dot of something on
        jne .none                       ; the floor without its picture (the
        cmp word [bp+20], 0FFFFh        ; full pattern) is dithered, at its
        jne .none                       ; shape's rows 31-32 (effects.c gives
        cmp word [bp+14], 31            ; VGA the whole box's); a flare's
        jge .d0                         ; glow and fire's inner flames are
        mov word [bp+14], 31            ; not drawn
.d0:    cmp word [bp+16], 33
        jle .d1
        mov word [bp+16], 33
.d1:    stc
        ret
.none:  clc
        ret
.nf:    les bx, [cs:P_CAMX]
        mov dx, [es:bx]
        sar dx, 1
        sar dx, 1
        mov [cs:V_T0], dx       ; cam_x / 4
        les bx, [cs:P_CAMY]
        mov dx, [es:bx]
        mov [cs:V_T1], dx
        cmp al, 0AAh
        je .fire
        or al, al
        jz .smoke
        stc                     ; (colour 1: none)
        ret
.smoke: mov al, 1               ; not found: thin or thick by the pattern
        cmp ah, 0FFh
        je .t
        dec ax
.t:     add al, 4
        mov [cs:V_FR], al
        les bx, [cs:P_SMOKE]
        lds si, [es:bx]
        mov si, [si]            ; smoke (DGROUP)
        les bx, [es:bx+4]
        mov cx, [es:bx]         ; n_smoke
        jmp short .find
.fire:  mov byte [cs:V_FR], 0
        lds si, [cs:LD_PFIRE]
        mov si, [si]
        les bx, [cs:LD_PNFIRE]
        mov cx, [es:bx]
.find:  mov [cs:V_SKIP], si     ; the list (for the tile's place)
        jcxz .nos
.s:     mov al, [si+2]
        and ax, 7               ; z
        mov dx, ax
        shl ax, 1
        add ax, dx
        shl ax, 1
        shl ax, 1
        shl ax, 1
        mov dx, ax              ; 24 z
        mov al, [si]
        add al, [si+1]
        xor ah, ah
        shl ax, 1
        shl ax, 1
        shl ax, 1
        sub ax, dx
        add ax, [cs:V_T1]
        mov dx, ax              ; sy
        mov bx, ax
        shl bx, 1
        shl bx, 1
        shl bx, 1
        shl bx, 1
        mov ax, bx
        shl ax, 1
        shl ax, 1
        add bx, ax              ; 80 sy
        mov al, [si]
        sub al, [si+1]
        cbw
        shl ax, 1
        shl ax, 1
        add ax, [cs:V_T0]       ; sx / 4
        add ax, bx
        cmp ax, [bp+8]
        je .hit
        add si, 4
        loop .s
.nos:   cmp byte [cs:V_FR], 0   ; not in the list: a burning unit
        jne .nsm                ; (drawUnit: frame 4 + the step, no
        mov al, [cs:V_STEP]     ; start of its own)
        and al, 3
        add al, 10
        mov [cs:V_FR], al
.nsm:   mov ax, [bp+14]         ; (smoke: never) at the rows given
        mov [cs:V_FTOP], ax
        mov ax, [bp+16]
        mov [cs:V_FBOT], ax
        jmp .draw
.hit:   cmp byte [cs:V_FR], 0
        je .step                ; (fire)
        mov al, [si+3]
        and al, 0Fh             ; the smoke
        mov ah, 4
        cmp al, 6
        jbe .th
        inc ah
        cmp al, 12
        jbe .th
        inc ah
.th:    mov [cs:V_FR], ah
.step:  mov al, [si]            ; the tile's own start (DOS: random per
        mov ah, al              ; tile; here mixed from x, y, z) + the
        shl ah, 1               ; step (V_STEP, pics_next)
        shl ah, 1
        add al, ah              ; 5x
        mov ah, [si+1]
        xor al, ah
        shl ah, 1
        xor al, ah
        shl ah, 1
        shl ah, 1
        add al, ah              ; ^ y ^ 2y + 8y
        xor al, [si+2]
        mov ah, al
        shr ah, 1
        shr ah, 1
        shr ah, 1
        xor al, ah
        mov cx, si              ; its place in fire, then smoke
        sub cx, [cs:V_SKIP]     ; (pics_next's order)
        shr cx, 1
        shr cx, 1
        cmp byte [cs:V_FR], 0
        je .f
        les bx, [cs:LD_PNFIRE]
        add cx, [es:bx]
.f:     mov ah, [cs:V_STEP]
        mov bx, [cs:V_NXT]
        or bx, bx
        jz .new
        cmp cx, bx
        jb .new
        mov ah, [cs:V_OLD]      ; not yet redrawn in this pass: its old
.new:   add al, ah              ; step, so that a redraw that cuts through
        and al, 3               ; it leaves no seam
        add [cs:V_FR], al
        mov ax, [cs:V_CY0]      ; the redrawn rows, of the box
        sub ax, dx
        jge .t0
        xor ax, ax
.t0:    mov [cs:V_FTOP], ax
        mov ax, [cs:V_CY1]
        sub ax, dx
        cmp ax, 40
        jle .t1
        mov ax, 40
.t1:    mov [cs:V_FBOT], ax
.draw:  mov ax, [cs:V_FBOT]
        cmp ax, [cs:V_FTOP]
        jle .done
        push word [bp+24]       ; op_mclip (entry 1): the columns,
        push ax                 ; bottom, top, the box, the buffer, the
        push word [cs:V_FTOP]   ; picture
        push word [bp+8]
        push word [bp+6]
        les bx, [cs:P_SMOKE]
        mov ax, es
        push ax
        mov bl, [cs:V_FR]
        xor bh, bh
        shl bx, 1
        add bx, [cs:P_SMOKE]
        mov ax, [es:bx+16]      ; its offset in the pack's bytes
        add ax, 16
        add ax, [cs:P_SMOKE]
        push ax
        push cs
        call blob_start + 4
        add sp, 14
.done:  clc
        ret

; ---- 26: pics_next (): DX:AX far V_RX0: redraw x0, y0, x1, y1 there
; (bplayer.c's idle loop gives them to render_rect at once: scratch
; words), for the smoke and fire pictures' step; 0: nothing. A pass over fire, then smoke, starts when the BIOS clock's
; tick / 4 is not V_STEP (which it then becomes), with the view showing
; (g_view_off 0) and the pictures loaded. A tile in sight (its level
; drawn, its box on the view, discovered in g_seen) adds render_tile's
; rectangle (32 x 64, from 24 rows above the box) to the one handed back
; while that stays within PICS_W x PICS_H: a redraw's walk of the map
; costs about as much as its blits, so neighbours go together, and a key
; waits for one such rectangle at most.
PICS_W  equ 64
PICS_H  equ 72
V_RX0   equ V_T0                ; the rectangle so far (V_RX0 0FFFFh: none):
V_RY0   equ V_T1                ; x0, y0, x1, y1 (render_rect's order)
V_RX1   equ V_SKIP
V_RY1   equ V_ROWS
pics_next:
        push si
        push di
        push ds
        mov word [cs:V_RX0], 0FFFFh
        cmp word [cs:P_SMOKE+2], 0
        je .none
        les bx, [cs:P_VOFF]
        cmp word [es:bx], 0
        jne .none
        cmp word [cs:V_NXT], 0
        jne .next
        xor ax, ax
        mov es, ax
        mov al, [es:046Ch]      ; the BIOS clock
        shr al, 1
        shr al, 1
        cmp al, [cs:V_STEP]
        je .none
        mov ah, [cs:V_STEP]
        mov [cs:V_OLD], ah
        mov [cs:V_STEP], al
.next:  mov di, [cs:V_NXT]
        lds si, [cs:LD_PFIRE]
        mov si, [si]            ; fire (DGROUP)
        les bx, [cs:LD_PNFIRE]
        mov ax, di
        sub ax, [es:bx]
        jb .tile
        les bx, [cs:P_SMOKE]
        lds si, [es:bx]
        mov si, [si]            ; smoke
        les bx, [es:bx+4]
        cmp ax, [es:bx]
        jb .sm
        mov word [cs:V_NXT], 0  ; the pass is over
        jmp .out
.sm:    mov di, ax
.tile:  inc word [cs:V_NXT]
        shl di, 1
        shl di, 1
        add si, di              ; the spot
        mov al, [si+2]
        and ax, 7
        mov dx, ax              ; z
        les bx, [cs:P_VIEWZ]
        cmp ax, [es:bx]
        jle .z
        les bx, [cs:P_SMOKE]
        les bx, [es:bx+12]
        cmp word [es:bx], 0     ; g_all_layers
        je .next
.z:     mov cl, 4
        mov al, [si]
        sub al, [si+1]
        cbw
        shl ax, cl
        les bx, [cs:P_CAMX]
        add ax, [es:bx]         ; sx
        cmp ax, -32
        jle .next
        cmp ax, 320
        jge .next
        mov di, ax
        dec cx
        mov al, [si]
        add al, [si+1]
        xor ah, ah
        shl ax, cl
        mov bx, dx
        shl bx, 1
        add bx, dx
        shl bx, cl              ; 24 z
        sub ax, bx
        les bx, [cs:P_CAMY]
        add ax, [es:bx]         ; sy
        cmp ax, -40
        jle .next
        cmp ax, VIEW_H + 24
        jl .y
        jmp .next
.y:     push ax
        les bx, [cs:P_SMOKE]
        les bx, [es:bx+8]
        mov cx, [es:bx]         ; g_seen (DGROUP), 0: all
        jcxz .seen
        les bx, [cs:LD_PMY]
        mov ax, dx
        mul word [es:bx]
        mov bl, [si+1]
        xor bh, bh
        add ax, bx
        les bx, [cs:LD_PMX]
        mul word [es:bx]
        mov bl, [si]
        xor bh, bh
        add bx, ax
        add bx, cx
        test byte [bx], 10h     ; SEEN_BIT
        jnz .seen
        pop ax
        jmp .next
.seen:  pop dx                  ; sy (DI sx): render_tile's rectangle
        mov ax, di
        lea cx, [di+32]
        mov bx, dx
        sub bx, 24
        add dx, 40
        or ax, ax               ; on the view
        jge .c0
        xor ax, ax
.c0:    cmp cx, 320
        jle .c1
        mov cx, 320
.c1:    or bx, bx
        jge .c2
        xor bx, bx
.c2:    cmp dx, VIEW_H
        jle .c3
        mov dx, VIEW_H
.c3:    cmp word [cs:V_RX0], 0FFFFh
        je .set
        cmp ax, [cs:V_RX0]      ; with the one so far
        jle .u0
        mov ax, [cs:V_RX0]
.u0:    cmp cx, [cs:V_RX1]
        jge .u1
        mov cx, [cs:V_RX1]
.u1:    cmp bx, [cs:V_RY0]
        jle .u2
        mov bx, [cs:V_RY0]
.u2:    cmp dx, [cs:V_RY1]
        jge .u3
        mov dx, [cs:V_RY1]
.u3:    mov di, cx
        sub di, ax
        cmp di, PICS_W
        ja .full
        mov di, dx
        sub di, bx
        cmp di, PICS_H
        ja .full
.set:   mov [cs:V_RX0], ax
        mov [cs:V_RX1], cx
        mov [cs:V_RY0], bx
        mov [cs:V_RY1], dx
        jmp .next
.full:  dec word [cs:V_NXT]     ; that tile starts the next one
.out:   cmp word [cs:V_RX0], 0FFFFh
        je .none
        mov ax, V_RX0
        mov dx, cs
        jmp short .x
.none:  xor ax, ax
        cwd
.x:     pop ds
        pop di
        pop si
        retf

fx_col  db 7, 10, 12, 14        ; CGA colour -> EGA colour: smoke grey, -, fire red, flames yellow

%define K 15
%define Y 16
arrow_px:
        db 0, 0, K, K, K, K, K, 0, 0
        db 0, 0, K, Y, Y, Y, K, 0, 0
        db 0, 0, K, Y, Y, Y, K, 0, 0
        db K, K, K, Y, Y, Y, K, K, K
        db K, Y, Y, Y, Y, Y, Y, Y, K
        db 0, K, Y, Y, Y, Y, Y, K, 0
        db 0, 0, K, Y, Y, Y, K, 0, 0
        db 0, 0, 0, K, Y, K, 0, 0, 0
        db 0, 0, 0, 0, K, 0, 0, 0, 0
%undef K
%undef Y

blob_end:
