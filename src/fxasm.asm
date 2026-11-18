; SPDX-License-Identifier: GPL-3.0-or-later
; Dithered overlays for the battlescape effects (src/effects.c): smoke,
; fire and flares are drawn into the back buffer one tile at a time from a
; shape (32x40 bit mask, 4 bytes a row, bit 7 = leftmost pixel) and a
; dither pattern repeating every 4 rows. Large model, Watcom __cdecl.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

PLANE   equ 40 * 144                    ; EGA plane (BUF_W * BUF_ROWS)
EGA_W   equ 40
CGA_W   equ 80

segment FXASM_TEXT public class=CODE

pat     db 0, 0, 0, 0                   ; pattern by row & 3
bmask   db 0, 0, 0, 0                   ; byte enables (0 / FFh): x clipping
col     db 0                            ; EGA colour bits / CGA fill
row     dw 0
cga     db 00h, 03h, 0Ch, 0Fh, 30h, 33h, 3Ch, 3Fh
        db 0C0h, 0C3h, 0CCh, 0CFh, 0F0h, 0F3h, 0FCh, 0FFh

; Arguments (both routines):
;   [bp+6]  buffer segment
;   [bp+8]  buffer offset of the tile box's top-left byte (row 0)
;   [bp+10] shape (far pointer: offset, segment)
;   [bp+14] first row, [bp+16] end row (clipped by the caller)
;   [bp+18] pattern rows 0/1, [bp+20] pattern rows 2/3 (absolute row & 3)
;   [bp+22] EGA colour / CGA colour * 55h
;   [bp+24] enabled 8-pixel columns (bits 0-3)

%macro SETUP 1
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp+18]
        mov [cs:pat], ax
        mov ax, [bp+20]
        mov [cs:pat+2], ax
        mov al, [bp+22]
        mov [cs:col], al
        mov al, [bp+24]
        mov bx, bmask
%assign i 0
%rep 4
        xor ah, ah
        shr al, 1
        sbb ah, 0
        mov [cs:bx+i], ah
%assign i i+1
%endrep
        mov es, [bp+6]
        mov ax, [bp+14]
        mov [cs:row], ax
        mov dx, %1
        mul dx
        add ax, [bp+8]
        mov di, ax                      ; DI = first row
        mov si, [bp+14]
        shl si, 1
        shl si, 1
        add si, [bp+10]                 ; SI = shape row
        mov cx, [bp+16]
        sub cx, [bp+14]                 ; CX = rows
        mov ds, [bp+12]
%endmacro

%macro FINISH 0
        pop ds
        pop di
        pop si
        pop bp
        retf
%endmacro

; One EGA byte (column %1) with mask AL. Each plane's instruction is
; patched per call (ega_patch): OR AL where the colour has the plane's bit,
; else AND AH (= NOT AL).
%macro EGABYTE 1
        lodsb
        and al, dl
        and al, [cs:bmask+%1]
        jz %%skip
        mov ah, al
        not ah
..@op%{1}_0:
        or [es:word di+%1+0*PLANE], al
..@op%{1}_1:
        or [es:word di+%1+1*PLANE], al
..@op%{1}_2:
        or [es:word di+%1+2*PLANE], al
..@op%{1}_3:
        or [es:word di+%1+3*PLANE], al
%%skip:
%endmacro

; Patches the 16 plane instructions for the colour in [cs:col]: opcode and
; ModRM 08h 85h = OR [DI+disp16], AL; 20h A5h = AND [DI+disp16], AH (after
; the ES prefix).
ega_patch:
        mov bl, [cs:col]
        mov ax, 0A520h
        shr bl, 1
        jnc .c0
        mov ax, 08508h
.c0:
        mov [cs:..@op0_0+1], ax
        mov [cs:..@op1_0+1], ax
        mov [cs:..@op2_0+1], ax
        mov [cs:..@op3_0+1], ax
        mov ax, 0A520h
        shr bl, 1
        jnc .c1
        mov ax, 08508h
.c1:
        mov [cs:..@op0_1+1], ax
        mov [cs:..@op1_1+1], ax
        mov [cs:..@op2_1+1], ax
        mov [cs:..@op3_1+1], ax
        mov ax, 0A520h
        shr bl, 1
        jnc .c2
        mov ax, 08508h
.c2:
        mov [cs:..@op0_2+1], ax
        mov [cs:..@op1_2+1], ax
        mov [cs:..@op2_2+1], ax
        mov [cs:..@op3_2+1], ax
        mov ax, 0A520h
        shr bl, 1
        jnc .c3
        mov ax, 08508h
.c3:
        mov [cs:..@op0_3+1], ax
        mov [cs:..@op1_3+1], ax
        mov [cs:..@op2_3+1], ax
        mov [cs:..@op3_3+1], ax
        ret

; void __cdecl fxtile_ega(...)
        global _fxtile_ega
_fxtile_ega:
        SETUP EGA_W
        call ega_patch
        test cx, cx
        jnz .row
        jmp .done
.row:
        mov bx, [cs:row]
        and bx, 3
        mov dl, [cs:pat+bx]
        EGABYTE 0
        EGABYTE 1
        EGABYTE 2
        EGABYTE 3
        add di, EGA_W
        inc word [cs:row]
        dec cx
        jz .done
        jmp .row
.done:
        FINISH

; Two CGA bytes (shape byte %1 = CGA bytes 2*%1, 2*%1+1).
%macro CGABYTE 1
        lodsb
        and al, dl
        and al, [cs:bmask+%1]
        jz %%skip
        mov ah, al
        mov bl, al
%rep 4
        shr bl, 1
%endrep
        xor bh, bh
        mov al, [cs:cga+bx]             ; left 4 pixels
        mov bl, ah
        and bl, 0Fh
        mov ah, [cs:cga+bx]             ; right 4 pixels
        mov bx, ax
        not bx
        and [es:di+2*%1], bx
        and al, [cs:col]
        and ah, [cs:col]
        or [es:di+2*%1], ax
%%skip:
%endmacro

; void __cdecl fxtile_cga(...)
        global _fxtile_cga
_fxtile_cga:
        SETUP CGA_W
        test cx, cx
        jnz .row
        jmp .done
.row:
        mov bx, [cs:row]
        and bx, 3
        mov dl, [cs:pat+bx]
        CGABYTE 0
        CGABYTE 1
        CGABYTE 2
        CGABYTE 3
        add di, CGA_W
        inc word [cs:row]
        dec cx
        jz .done
        jmp .row
.done:
        FINISH
