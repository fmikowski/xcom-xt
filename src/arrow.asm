; SPDX-License-Identifier: GPL-3.0-or-later
; The selected unit's arrow (src/render.h; OpenXcom Map::init, drawTerrain)
; into the back buffer. Resident: gfx.c gfx_present_cells calls it before
; every present of the view. Watcom register call: arguments in AX, DX,
; BX, CX; every register kept.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

BUF_W   equ 40                  ; gfx.h: EGA bytes a row (a plane)
PLANE   equ BUF_W * 144         ; PLANE_BYTES
BUFC_W  equ 80                  ; CGA bytes a row

extern _g_view_z:wrt DGROUP, _g_cam_x:wrt DGROUP, _g_cam_y:wrt DGROUP
extern _g_video:wrt DGROUP, _g_bufseg:wrt DGROUP, _g_uk_cmap:wrt DGROUP

segment _BSS public align=2 class=BSS
group DGROUP _BSS

; int g_arrow[4]: on, x, y (its top left pixel without the camera), level
        global _g_arrow
_g_arrow resw 4

segment ARROW_TEXT public class=CODE

; 9 x 9, yellow with a black outline (Map::init): per row the pixels drawn
; and the yellow ones, the left pixel in bit 15.
a_all   dw 3E00h, 3E00h, 3E00h, 0FF80h, 0FF80h, 7F00h, 3E00h, 1C00h, 0800h
a_fill  dw 0000h, 1C00h, 1C00h, 1C00h, 7F00h, 3E00h, 1C00h, 0800h, 0000h
; CGA: a nibble of pixels to their byte in colour 3 (brown; white on
; Hercules), the outline 0 (black).
twice   db 00h, 03h, 0Ch, 0Fh, 30h, 33h, 3Ch, 3Fh, 0C0h, 0C3h, 0CCh, 0CFh, 0F0h, 0F3h, 0FCh, 0FFh
a_n     dw 0                    ; rows left
a_sh    db 0                    ; the left pixel's place in its byte
a_col   db 0                    ; EGA colour

; void render_arrow(int c0, int r0, int cols, int rows)
; Draws the arrow, if on and its level is shown, over the window cells
; c0 .. c0 + cols - 1, rows r0 .. r0 + rows - 1.
        global render_arrow_
render_arrow_:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push es
        cmp word [ss:_g_arrow], 0
        je .out
        mov si, [ss:_g_arrow+6]
        cmp si, [ss:_g_view_z]
        jg .out
        add bx, ax                      ; c1
        add cx, dx                      ; r1
        mov si, cx
        mov di, [ss:_g_arrow+2]
        add di, [ss:_g_cam_x]           ; x
        ; the arrow's pixels lo .. hi - 1 lie in the cells
        mov cl, 3
        shl ax, cl
        sub ax, di                      ; lo
        shl bx, cl
        sub bx, di                      ; hi
        cmp ax, 9
        jge .out
        test bx, bx
        jle .out
        mov bp, 0FF80h                  ; the columns drawn
        test ax, ax
        jle .left
        mov cl, al
        mov ax, 0FFFFh
        shr ax, cl
        and bp, ax
.left:  cmp bx, 9
        jge .right
        mov cl, bl
        mov ax, 0FFFFh
        shr ax, cl
        not ax
        and bp, ax
.right: ; its rows r0 - y .. r1 - y - 1 within 0 .. 8
        mov ax, [ss:_g_arrow+4]
        add ax, [ss:_g_cam_y]           ; y
        sub dx, ax
        sub si, ax
        cmp si, 9
        jle .end
        mov si, 9
.end:   test dx, dx
        jge .start
        xor dx, dx
.start: sub si, dx
        jg .go
.out:   jmp .done
.go:    mov [cs:a_n], si
        add ax, dx                      ; the first row drawn
        mov si, dx
        shl si, 1                       ; its table entry
        cmp word [ss:_g_video], 0
        jne .cga

        ; EGA: yellow, at night the register showing it (batui.c bu_night)
        mov cx, BUF_W
        mul cx
        mov dx, di
        mov cl, 3
        sar dx, cl
        add ax, dx
        xchg ax, di                     ; di: the row's first byte
        and al, 7
        mov [cs:a_sh], al
        les bx, [ss:_g_uk_cmap]
        mov ax, es
        or ax, bx
        mov al, 14
        jz .col
        mov al, [es:bx+14]
.col:   mov [cs:a_col], al
        mov es, [ss:_g_bufseg]
.erow:  call row
        not ax                          ; the bits kept
        mov bl, [cs:a_col]
        mov bh, 4
        push di
.plane: mov cx, dx
        shr bl, 1
        jc .on
        xor cx, cx                      ; black in this plane
.on:    cmp ah, 0FFh
        je .b1
        and [es:di], ah
        or [es:di], ch
.b1:    cmp al, 0FFh
        je .b2
        and [es:di+1], al
        or [es:di+1], cl
.b2:    add di, PLANE
        dec bh
        jnz .plane
        pop di
        add di, BUF_W
        inc si
        inc si
        dec word [cs:a_n]
        jnz .erow
        jmp short .done

.cga:   mov cx, BUFC_W
        mul cx
        mov dx, di
        sar dx, 1
        sar dx, 1
        add ax, dx
        xchg ax, di
        and al, 3
        mov [cs:a_sh], al
        mov es, [ss:_g_bufseg]
        mov bx, twice
.crow:  call row
        push di
        mov cx, 0304h                   ; 3 bytes, a nibble each
.byte:  rol ax, cl
        rol dx, cl
        push ax
        and al, 0Fh
        cs xlatb
        test al, al
        jz .skip
        not al
        and [es:di], al
        mov al, dl
        and al, 0Fh
        cs xlatb
        or [es:di], al
.skip:  pop ax
        inc di
        dec ch
        jnz .byte
        pop di
        add di, BUFC_W
        inc si
        inc si
        dec word [cs:a_n]
        jnz .crow

.done:  pop es
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        retf

; Row si / 2 of the arrow in the columns drawn (bp), moved right by a_sh:
; ax its pixels, dx the yellow ones.
row:    mov cl, [cs:a_sh]
        mov ax, [cs:a_all+si]
        and ax, bp
        shr ax, cl
        mov dx, [cs:a_fill+si]
        and dx, bp
        shr dx, cl
        ret
