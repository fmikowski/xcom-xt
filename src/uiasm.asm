; SPDX-License-Identifier: GPL-3.0-or-later
; Night colours of the battle panel (src/uik.c, src/batui.c): EGA bytes
; recoloured through a 16-entry colour map, one pixel at a time. Large
; model, Watcom __cdecl.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

segment UIASM_TEXT public class=CODE

v_dst   dw 0
v_cnt   dw 0
v_nb    dw 0
v_rows  dw 0
v_out   dd 0
v_k     db 0

; In: CL CH DL DH = planes 0-3 of 8 pixels (bit 7 the left one), DS:BX =
; the colour map. Out: AH = plane 0, low bytes of BP SI DI = planes 1-3.
remap8:
        mov byte [cs:v_k], 8
.px:
        xor al, al
        shl dh, 1
        rcl al, 1
        shl dl, 1
        rcl al, 1
        shl ch, 1
        rcl al, 1
        shl cl, 1
        rcl al, 1
        xlat
        shr al, 1
        rcl ah, 1
        shr al, 1
        rcl bp, 1
        shr al, 1
        rcl si, 1
        shr al, 1
        rcl di, 1
        dec byte [cs:v_k]
        jnz .px
        ret

; Stores the result of remap8 in v_out.
%macro STORE_OUT 0
        mov [cs:v_out], ah
        mov ax, bp
        mov [cs:v_out+1], al
        mov ax, si
        mov [cs:v_out+2], al
        mov ax, di
        mov [cs:v_out+3], al
%endmacro

; void __cdecl uk_remap4(unsigned char far *planes, const unsigned char far *cmap)
; Four plane bytes of 8 pixels in memory.
        global _uk_remap4
_uk_remap4:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        les di, [bp+6]
        lds bx, [bp+10]
        mov cx, [es:di]
        mov dx, [es:di+2]
        push di
        push bp
        call remap8
        STORE_OUT
        pop bp
        pop di
        mov ax, [cs:v_out]
        mov [es:di], ax
        mov ax, [cs:v_out+2]
        mov [es:di+2], ax
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl uk_remap_vram(unsigned dst, unsigned bytes, unsigned rows,
;                            const unsigned char far *cmap)
; A rectangle of EGA video memory at A000:dst, 40 bytes a row. Leaves the
; map mask at all planes and the read map at plane 0.
        global _uk_remap_vram
_uk_remap_vram:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp+6]
        mov [cs:v_dst], ax
        mov ax, [bp+8]
        mov [cs:v_nb], ax
        mov ax, [bp+10]
        mov [cs:v_rows], ax
        lds bx, [bp+12]
        mov ax, 0A000h
        mov es, ax
        push bp
        cmp word [cs:v_nb], 0
        je .done
        cmp word [cs:v_rows], 0
        je .done
.row:
        mov ax, [cs:v_nb]
        mov [cs:v_cnt], ax
        mov di, [cs:v_dst]
.byte:
        push di
        mov dx, 03CEh
        mov ax, 0004h
        out dx, ax
        mov cl, [es:di]
        mov ax, 0104h
        out dx, ax
        mov ch, [es:di]
        mov ax, 0204h
        out dx, ax
        mov ah, [es:di]
        push ax
        mov ax, 0304h
        out dx, ax
        mov al, [es:di]
        pop dx
        mov dl, dh
        mov dh, al
        call remap8
        STORE_OUT
        pop di
        mov dx, 03C4h
        mov ax, 0102h
        out dx, ax
        mov al, [cs:v_out]
        mov [es:di], al
        mov ax, 0202h
        out dx, ax
        mov al, [cs:v_out+1]
        mov [es:di], al
        mov ax, 0402h
        out dx, ax
        mov al, [cs:v_out+2]
        mov [es:di], al
        mov ax, 0802h
        out dx, ax
        mov al, [cs:v_out+3]
        mov [es:di], al
        mov ax, 0F02h
        out dx, ax
        inc di
        dec word [cs:v_cnt]
        jnz .byte
        add word [cs:v_dst], 40
        dec word [cs:v_rows]
        jnz .row
.done:
        mov dx, 03CEh
        mov ax, 0004h
        out dx, ax
        pop bp
        pop ds
        pop di
        pop si
        pop bp
        retf
