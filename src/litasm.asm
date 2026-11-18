; SPDX-License-Identifier: GPL-3.0-or-later
; Night light (src/nlit.c): the map view's present through the cells, and a
; light's disc of cells. In the canvas's overlay section. Large model, Watcom
; __cdecl (SI, DI, BP, DS kept). Cells are 16 x 8 pixels, 20 across and 18
; down; a row of cells is a dword, bit set = lit.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

BUF_W   equ 40
PLANE   equ BUF_W * 144
COLS    equ 20
BANDS   equ 18

segment LITASM_TEXT public class=CODE

; The band's runs, bytes: n lit words, or n | 80h dark words; 0 = the end.
runs    times COLS + 2 db 0
w_c0    dw 0                            ; first byte in a row
w_rows  dw 0                            ; rows of this band
w_row   dw 0                            ; the band's first row in the rectangle
w_skip  dw 0                            ; BUF_W - bytes

; void __cdecl lit_rect(unsigned bseg, unsigned long far *cells, int c0, int r0,
;                       int cols, int rows)
; The rectangle (bytes c0 .. c0 + cols, made even; rows) of the back buffer at
; bseg to EGA page 0, as blit.asm buf_present_rect: lit cells as they are,
; dark cells with plane 3 clear and the other planes through a checkerboard.
        global _lit_rect
_lit_rect:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        cmp word [bp+18], 0
        jg .some
        jmp .none
.some:
        mov ax, [bp+12]                 ; c0
        and ax, ~1
        mov [cs:w_c0], ax
        mov cx, [bp+16]                 ; cols
        add cx, [bp+12]
        inc cx
        and cx, ~1
        sub cx, ax                      ; bytes in a row, even
        mov bx, BUF_W
        sub bx, cx
        mov [cs:w_skip], bx
        mov ax, [bp+14]
        mov [cs:w_row], ax
        mov dx, 3C4h
        mov al, 2
        out dx, al
        mov ax, 0A000h
        mov es, ax
        cld
.band:
        mov bx, [cs:w_row]
        mov ax, bx
        and ax, 7
        neg ax
        add ax, 8                       ; rows to the band's end
        mov dx, [bp+14]
        add dx, [bp+18]
        sub dx, bx                      ; rows left
        cmp ax, dx
        jle .h
        mov ax, dx
.h:
        mov [cs:w_rows], ax
        ; its dword: (row >> 3) * 4
        shr bx, 1
        and bx, ~3
        lds si, [bp+8]
        mov ax, [bx+si]
        mov dx, [bx+si+2]
        mov cx, [cs:w_c0]
        shr cx, 1
        jcxz .noshift
.sh:
        shr dx, 1
        rcr ax, 1
        loop .sh
.noshift:
        mov cx, BUF_W
        sub cx, [cs:w_skip]
        shr cx, 1                       ; the words
        mov bx, runs
        xor si, si                      ; SI words of the run, DI 80h if dark
        mov di, 1                       ; (none yet)
.bit:
        shr dx, 1
        rcr ax, 1
        sbb bp, bp                      ; lit -1, dark 0
        not bp
        and bp, 80h                     ; lit 0, dark 80h
        cmp bp, di
        je .same
        test si, si
        jz .first
        xchg ax, si
        or ax, di
        mov [cs:bx], al
        inc bx
        xchg ax, si
.first:
        mov di, bp
        xor si, si
.same:
        inc si
        loop .bit
        mov ax, si
        or ax, di
        xor ah, ah
        mov [cs:bx], ax                 ; the last run and the end
        mov bp, sp
        add bp, 6                       ; (SI, DI, DS on the stack)
        mov ds, [bp+6]
        mov al, 1
.plane:
        push ax
        mov dx, 3C5h
        out dx, al
        mov bh, al
        mov ax, [cs:w_row]
        mov dx, BUF_W
        mul dx
        add ax, [cs:w_c0]
        mov di, ax
        mov si, ax
        mov al, bh
        cmp al, 2
        jb .off
        add si, PLANE
        cmp al, 4
        jb .off
        add si, PLANE
        cmp al, 8
        jb .off
        add si, PLANE
.off:
        mov dx, 0AAAAh
        test byte [cs:w_row], 1
        jz .pat
        not dx
.pat:
        mov bp, [cs:w_rows]
        cmp al, 8
        je .row3
.row:
        mov bx, runs
.run:
        mov cl, [cs:bx]
        inc bx
        test cl, cl
        js .dark
        jz .rowend
        xor ch, ch
        rep movsw
        jmp short .run
.dark:
        and cx, 7Fh
        shl cx, 1
        shl cx, 1
        neg cx
        add cx, .darkend
        jmp cx
        times COLS db 0ADh, 21h, 0D0h, 0ABh     ; lodsw, and ax, dx, stosw
.darkend:
        jmp short .run
.rowend:
        add si, [cs:w_skip]
        add di, [cs:w_skip]
        not dx
        dec bp
        jz .planeend
        jmp .row
.row3:                                  ; plane 3: lit as it is, dark clear
        mov bx, runs
.run3:
        mov cl, [cs:bx]
        inc bx
        test cl, cl
        js .dark3
        jz .rowend3
        xor ch, ch
        rep movsw
        jmp short .run3
.dark3:
        and cx, 7Fh
        mov ax, cx
        shl ax, 1
        add si, ax
        xor ax, ax
        rep stosw
        jmp short .run3
.rowend3:
        add si, [cs:w_skip]
        add di, [cs:w_skip]
        dec bp
        jnz .row3
.planeend:
        pop ax
        shl al, 1
        cmp al, 16
        jae .planes_done
        jmp .plane
.planes_done:
        mov bp, sp
        add bp, 6
        mov ax, [cs:w_rows]
        add [cs:w_row], ax
        mov ax, [bp+14]
        add ax, [bp+18]
        cmp [cs:w_row], ax
        jae .done
        jmp .band
.done:
        mov dx, 3C5h
        mov al, 0Fh
        out dx, al
.none:
        pop ds
        pop di
        pop si
        pop bp
        retf

; Half widths of the disc in cells, its rows from the middle: (j + .5)^2 +
; (k + .5)^2 <= 180 (x^2 + y^2 <= 90 tiles, effects.c near15; u = x - y and
; v = x + y are the cells). Upwards 3 rows more, a level's height: the wall or
; unit on a lit tile reaches that far above its diamond.
rows_up db 13, 13, 13
rows_dn db 13, 13, 13, 13, 13, 12, 12, 11, 10, 9, 8, 7, 5
ROWS_DN equ $ - rows_dn
ROWS_UP equ $ - rows_up
; one tile's diamond (nlit.c terrain)
tile_rows db 1, 1, 1, 1
; the 20 bits from bit u up
bits_lo dw 0FFFFh, 0FFFEh, 0FFFCh, 0FFF8h, 0FFF0h, 0FFE0h, 0FFC0h, 0FF80h
        dw 0FF00h, 0FE00h, 0FC00h, 0F800h, 0F000h, 0E000h, 0C000h, 08000h
        dw 0, 0, 0, 0, 0
bits_hi dw 0Fh, 0Fh, 0Fh, 0Fh, 0Fh, 0Fh, 0Fh, 0Fh
        dw 0Fh, 0Fh, 0Fh, 0Fh, 0Fh, 0Fh, 0Fh, 0Fh
        dw 0Fh, 0Eh, 0Ch, 8, 0

; void __cdecl lit_disc(unsigned long far *cells, int cu, int cv, int tile)
; A light whose tile's diamond has its middle at the corner of cells cu, cv;
; tile = 1: only that diamond (and above it).
        global _lit_disc
_lit_disc:
        push bp
        mov bp, sp
        push si
        push di
        les di, [bp+6]
        mov si, rows_dn
        mov cx, ROWS_DN
        cmp word [bp+14], 0
        je .dn
        mov si, tile_rows
        mov cx, 1
.dn:
        mov dx, [bp+12]
        mov bx, 1
        call .side
        mov si, rows_up
        mov cx, ROWS_UP
        cmp word [bp+14], 0
        je .up
        mov si, tile_rows
        mov cx, 4
.up:
        mov dx, [bp+12]
        dec dx
        mov bx, -1
        call .side
        pop di
        pop si
        pop bp
        retf
; CS:SI half widths, CX of them, DX the band, BX the step to the next.
.side:
        cmp dx, BANDS
        jae .next
        mov ax, dx
        shl ax, 1
        shl ax, 1
        xchg ax, bx                     ; (BX its dword, AX the step)
        cmp word [es:bx+di], 0FFFFh     ; a row all lit: nothing to add
        jne .some2
        cmp byte [es:bx+di+2], 0Fh
        jne .some2
        mov bx, ax
        jmp short .next
.some2:
        mov bx, ax
        push bx
        push cx
        mov al, [cs:si]
        xor ah, ah
        mov bx, [bp+10]                 ; cu
        mov cx, bx
        add cx, ax                      ; past the last cell
        sub bx, ax                      ; the first
        jns .u0
        xor bx, bx
.u0:
        cmp cx, COLS
        jle .u1
        mov cx, COLS
.u1:
        cmp bx, cx
        jge .skip
        shl bx, 1
        mov ax, [cs:bx+bits_lo]
        mov bx, [cs:bx+bits_hi]         ; BX:AX from the first ...
        push si
        mov si, cx
        shl si, 1
        mov cx, [cs:si+bits_lo]
        not cx
        and ax, cx
        mov cx, [cs:si+bits_hi]
        not cx
        and bx, cx                      ; ... less those past the last
        mov si, dx
        shl si, 1
        shl si, 1
        add si, di
        or [es:si], ax
        or [es:si+2], bx
        pop si
.skip:
        pop cx
        pop bx
.next:
        inc si
        add dx, bx
        loop .side
        ret
