; SPDX-License-Identifier: GPL-3.0-or-later
; The battle view's canvas (src/canvas.c): copies between the EGA back
; buffer (4 planes of 144 rows x 40 bytes, gfx.h) and a canvas block (4
; planes of 48 rows x a stride of bytes), the EMS page mapping and the
; check mode's compare. In the canvas overlay section. Large model, Watcom
; __cdecl (SI, DI, BP, DS kept).

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

BUF_W     equ 40
PLANE     equ BUF_W * 144               ; back buffer plane
BLK_H     equ 24                        ; canvas block rows (cvpriv.h BLK_H)
VBUF_W    equ 320                       ; VGA's back buffer row (gfx.h VGA_BYTES)

segment CVASM_TEXT public class=CODE

; void __cdecl cv_copy(unsigned bseg, unsigned boff, void far *blk, unsigned kstride,
;                      unsigned bytes, unsigned rows, int out)
; out bit 0 = 1: the back buffer's rect (bytes x rows at boff) into the block
; at blk (row stride kstride, plane stride kstride * BLK_H); 0: back. EGA: 4
; planes of rows of BUF_W; bytes even, at most 254. Bit 1, VGA: one plane of
; rows of VBUF_W, kept by 16-pixel columns (each a "plane" of 16 bytes x
; rows at 16 * plane in the buffer); bytes a multiple of 16, at most 320.
; rows 1-255. A row is one REP MOVSW with both skips in registers (BX
; source, BP destination).
        global _cv_copy
_cv_copy:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        cld
        mov ax, [bp+14]                 ; block row stride
        mov cx, BLK_H
        mul cx                          ; AX = block plane stride
        mov dx, [bp+16]                 ; bytes a row of a plane
        mov si, PLANE                   ; buffer plane stride
        mov bx, BUF_W                   ; buffer row
        mov di, 4                       ; planes
        test byte [bp+20], 2
        jz .e
        mov cl, 4
        shr dx, cl
        mov di, dx                      ; VGA: a plane a 16-pixel column
        mov dx, 16
        mov si, dx
        mov bx, VBUF_W
.e:     mov cx, [bp+14]
        sub cx, dx                      ; block skip
        sub bx, dx                      ; buffer skip
        push di                         ; [bp-10] planes
        shr dx, 1
        push dx                         ; [bp-12] words a row
        test byte [bp+20], 1
        jz .in
        push si                         ; [bp-14] source plane stride
        push ax                         ; [bp-16] destination plane stride
        push cx                         ; [bp-18] destination skip
        les di, [bp+10]
        mov si, [bp+8]
        mov ds, [bp+6]
        jmp .go
.in:    push ax
        push si
        push bx
        mov bx, cx                      ; source skip: the block's
        mov es, [bp+6]
        mov di, [bp+8]
        lds si, [bp+10]
.go:    mov ah, [bp-12]                 ; words a row
        mov dh, [bp+18]                 ; rows
        mov dl, [bp-10]                 ; planes
        xor cx, cx
.pl:    push si
        push di
        push bp
        mov bp, [bp-18]
        mov al, dh
.r:     mov cl, ah
        rep movsw
        add si, bx
        add di, bp
        dec al
        jnz .r
        pop bp
        pop di
        pop si
        add si, [bp-14]
        add di, [bp-16]
        dec dl
        jnz .pl
        add sp, 10
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; unsigned __cdecl cv_cmp(unsigned bseg, unsigned boff, void far *blk, unsigned kstride,
;                         unsigned bytes, unsigned rows, int vga)
; The check mode: 0 if the back buffer's rect equals the block's, else 1
; (vga: VGA's back buffer, one plane: a cell's column).
        global _cv_cmp
_cv_cmp:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        cld
        mov bx, [bp+14]
        mov ax, bx
        mov cx, BLK_H
        mul cx
        push ax                         ; [bp-10]
        mov ax, BUF_W
        mov ch, 4
        cmp word [bp+20], 0
        je .e
        mov ax, VBUF_W
        mov ch, 1
.e:     sub ax, [bp+16]
        push ax                         ; [bp-12] buffer skip
        sub bx, [bp+16]                 ; block skip
        mov dx, [bp+16]
        shr dx, 1
        mov ah, ch
        mov es, [bp+6]
        mov di, [bp+8]
        lds si, [bp+10]
.cp:    push si
        push di
        mov al, [bp+18]
.cr:    mov cx, dx
        repe cmpsw
        jne .diff
        add di, [bp-12]
        add si, bx
        dec al
        jnz .cr
        pop di
        pop si
        add di, PLANE
        add si, [bp-10]
        dec ah
        jnz .cp
        xor ax, ax
        jmp .cdone
.diff:  pop di
        pop si
        mov ax, 1
.cdone: add sp, 4
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; unsigned __cdecl cv_map(unsigned handle, unsigned page)
; EMS: maps the handle's logical page at physical page 3 (INT 67h 44h);
; AH = 0 when it worked.
        global _cv_map
_cv_map:
        push bp
        mov bp, sp
        mov dx, [bp+6]
        mov bx, [bp+8]
        mov ax, 4403h
        int 67h
        mov al, ah
        xor ah, ah
        pop bp
        retf

; unsigned __cdecl cv_seen(unsigned char far *seen, unsigned idx, int step, unsigned n,
;                          unsigned char far *bits)
; The fog's shadow (canvas.c sync_seen): the first k < n whose tile
; idx + k * step has SEEN_BIT (10h) other than bit idx + k * step of bits;
; n if none.
        global _cv_seen
_cv_seen:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        lds si, [bp+6]                  ; seen
        les di, [bp+16]                 ; bits
        mov bx, [bp+10]                 ; idx
        mov dx, [bp+14]                 ; tiles left
        xor ax, ax                      ; k
.sl:    cmp ax, dx
        jae .sdone
        push ax
        mov cl, bl
        and cl, 7
        mov ah, 1
        shl ah, cl                      ; the tile's bit
        push bx
        mov cl, 3
        shr bx, cl
        mov al, [es:di+bx]
        pop bx
        and al, ah                      ; shadow bit (nonzero = seen)
        neg al
        sbb al, al                      ; FFh / 0
        mov ah, [si+bx]
        and ah, 10h
        neg ah
        sbb ah, ah
        cmp al, ah
        pop ax
        jne .sdone
        inc ax
        add bx, [bp+12]
        jmp .sl
.sdone: pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
