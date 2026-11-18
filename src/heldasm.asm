; SPDX-License-Identifier: GPL-3.0-or-later
; The inventory's held item (src/invscr.c, Held): its picture drawn at the
; pointer and the screen under it saved and put back. Lives in the
; inventory's overlay section with invscr.c. Large model, Watcom __cdecl.
;
; The Held block (one heap block, invscr.c): u16 voff (video offset of the
; top left byte), nc (byte columns), rows, sh (bit shift within a byte:
; EGA x & 7, CGA (x & 3) * 2), pix, save (offsets of the pixels and the
; save-under in the block's segment), ega (1 EGA, 0 CGA). The pixels: per
; row, per channel (EGA mask, planes 0-3; CGA mask, data) a strip of
; STRIP bytes: 0, the picture's bytes, 0 ... A word read at strip + k is
; (byte k - 1, byte k): shifted right by sh it gives the screen's byte k.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

E_STRIP equ 6                   ; EGA: up to 4 bytes (32 pixels) + 2
C_STRIP equ 10                  ; CGA: up to 8 bytes (32 pixels) + 2

H_VOFF  equ 0
H_NC    equ 2
H_ROWS  equ 4
H_SH    equ 6
H_PIX   equ 8
H_SAVE  equ 10
H_EGA   equ 12

; locals
L_ROWS  equ -2
L_PLANE equ -4
L_VSEG  equ -6
L_BSEG  equ -8

segment HELDASM_TEXT public class=CODE

; The next screen row of offset reg (EGA 40 bytes; CGA: the odd rows are
; 2000h further on).
%macro NEXTROW 1
        cmp byte [bp+L_VSEG+1], 0A0h
        jne %%cga
        add %1, 40
        jmp short %%done
%%cga:
        xor %1, 2000h
        test %1, 2000h
        jnz %%done
        add %1, 80
%%done:
%endmacro

; void __cdecl held_blit(void far *held, unsigned what)
; what 0: save the screen under the picture, 1: draw the picture,
; 2: put the saved screen back. EGA: leaves write mode 0, replace, no
; set/reset, all planes, read plane 0, bit mask FF.
        global _held_blit
_held_blit:
        push bp
        mov bp, sp
        sub sp, 8
        push si
        push di
        push ds
        cld
        lds bx, [bp+6]          ; ds:bx = Held
        mov [bp+L_BSEG], ds
        mov ax, [bx+H_ROWS]
        mov [bp+L_ROWS], ax
        mov word [bp+L_VSEG], 0B800h
        mov word [bp+L_PLANE], 0
        cmp word [bx+H_EGA], 0
        je .go
        mov word [bp+L_VSEG], 0A000h
        mov dx, 03CEh
        mov ax, 0005h           ; write mode 0
        out dx, ax
        mov ax, 0003h           ; replace
        out dx, ax
        mov ax, 0001h           ; no set/reset
        out dx, ax
        mov ax, 0FF08h          ; bit mask
        out dx, ax
.go:
        mov ax, [bp+10]
        cmp ax, 1
        jne .notdraw
        jmp draw
.notdraw:
        ja restore

; ---- save: video -> save (EGA plane by plane through the read map) ----
save:
        mov es, [bp+L_BSEG]
        mov di, [bx+H_SAVE]
        mov dx, [bx+H_NC]
        mov bx, [bx+H_VOFF]     ; (ds:bx no longer needed)
        mov ds, [bp+L_VSEG]
.plane:
        cmp byte [bp+L_VSEG+1], 0A0h
        jne .cga
        push dx
        mov dx, 03CEh
        mov ah, [bp+L_PLANE]
        mov al, 4               ; read map select
        out dx, ax
        pop dx
.cga:
        mov si, bx
        mov ax, [bp+L_ROWS]
.row:
        push si
        mov cx, dx
        rep movsb
        pop si
        NEXTROW si
        dec ax
        jnz .row
        inc word [bp+L_PLANE]
        cmp byte [bp+L_VSEG+1], 0A0h
        jne .done
        cmp word [bp+L_PLANE], 4
        jb .plane
.done:
        jmp finish

; ---- restore: save -> video (EGA plane by plane through the map mask) ----
restore:
        mov si, [bx+H_SAVE]
        mov dx, [bx+H_NC]
        mov bx, [bx+H_VOFF]
        mov es, [bp+L_VSEG]
.plane:
        cmp byte [bp+L_VSEG+1], 0A0h
        jne .cga
        push dx
        mov dx, 03C4h
        mov cl, [bp+L_PLANE]
        mov ah, 1
        shl ah, cl
        mov al, 2               ; map mask
        out dx, ax
        pop dx
.cga:
        mov di, bx
        mov ax, [bp+L_ROWS]
.row:
        push di
        mov cx, dx
        rep movsb
        pop di
        NEXTROW di
        dec ax
        jnz .row
        inc word [bp+L_PLANE]
        cmp byte [bp+L_VSEG+1], 0A0h
        jne .done
        cmp word [bp+L_PLANE], 4
        jb .plane
.done:
        jmp finish

; ---- draw ----
draw:
        mov es, [bp+L_VSEG]
        mov di, [bx+H_VOFF]
        mov si, [bx+H_PIX]
        mov cl, [bx+H_SH]
        mov ch, [bx+H_NC]
        cmp word [bx+H_EGA], 0
        jne .ega
        jmp .cga

        ; EGA: per byte the shifted mask into the bit mask register, a read
        ; for the latches, then each plane's shifted byte through the map
        ; mask (as src/uik.c pic_ega); bytes the picture leaves clear are
        ; skipped.
.ega:
        mov bl, ch              ; columns
.erow:
        push si
        push di
        mov ch, bl
.ecol:
        mov ax, [si]
        xchg al, ah
        shr ax, cl
        test al, al
        jz .enext
        mov ah, al
        mov al, 8
        mov dx, 03CEh
        out dx, ax              ; bit mask
        mov al, [es:di]         ; latches
        mov dx, 03C4h
        mov ax, 0102h
        out dx, ax
        mov ax, [si+E_STRIP]
        xchg al, ah
        shr ax, cl
        mov [es:di], al
        mov ax, 0202h
        out dx, ax
        mov ax, [si+2*E_STRIP]
        xchg al, ah
        shr ax, cl
        mov [es:di], al
        mov ax, 0402h
        out dx, ax
        mov ax, [si+3*E_STRIP]
        xchg al, ah
        shr ax, cl
        mov [es:di], al
        mov ax, 0802h
        out dx, ax
        mov ax, [si+4*E_STRIP]
        xchg al, ah
        shr ax, cl
        mov [es:di], al
.enext:
        inc si
        inc di
        dec ch
        jnz .ecol
        pop di
        pop si
        add si, 5*E_STRIP
        add di, 40
        dec word [bp+L_ROWS]
        jnz .erow
        jmp finish

        ; CGA: (screen & ~mask) | (data & mask), both shifted.
.cga:
        mov dl, ch
.crow:
        push si
        push di
        mov ch, dl
.ccol:
        mov ax, [si]
        xchg al, ah
        shr ax, cl
        mov bl, al              ; mask
        mov ax, [si+C_STRIP]
        xchg al, ah
        shr ax, cl
        and al, bl
        not bl
        and bl, [es:di]
        or al, bl
        mov [es:di], al
        inc si
        inc di
        dec ch
        jnz .ccol
        pop di
        pop si
        add si, 2*C_STRIP
        NEXTROW di
        dec word [bp+L_ROWS]
        jnz .crow

finish:
        cmp byte [bp+L_VSEG+1], 0A0h
        jne .out
        mov dx, 03C4h
        mov ax, 0F02h           ; all planes
        out dx, ax
        mov dx, 03CEh
        mov ax, 0004h           ; read plane 0
        out dx, ax
        mov ax, 0FF08h
        out dx, ax
.out:
        pop ds
        pop di
        pop si
        mov sp, bp
        pop bp
        retf
