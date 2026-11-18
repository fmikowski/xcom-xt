; SPDX-License-Identifier: GPL-3.0-or-later
; aiway.c's own assembly, in its section (path.c's walk tables, as pathasm.asm).

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

WF       equ 10000
WF_WEST  equ 01h
WF_NORTH equ 02h

segment WAYASM_TEXT public class=CODE

; void __cdecl path_doors_asm(unsigned walkseg, unsigned ntiles, const void far *map,
;                             const void far *mcd)
; The passable doors become walls, or back: WF_WEST and WF_NORTH flip for the
; west and north walls whose MCD record has a door flag and is no wall already
; (DOS: isBlocked with a missile target). A missile's search does it twice.
        global _path_doors_asm
_path_doors_asm:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ds, [bp+6]
        xor si, si
        les di, [bp+10]
        mov cl, 4
.loop:  mov dx, [es:di+1]               ; the west wall, the north wall
        test dx, dx
        jz .next
        push es
        mov es, [bp+16]
        mov al, dl
        call .door
        jz .n1
        xor byte [si+WF], WF_WEST
.n1:    mov al, dh
        call .door
        jz .n2
        xor byte [si+WF], WF_NORTH
.n2:    pop es
.next:  add di, 4
        inc si
        dec word [bp+8]
        jnz .loop
        pop ds
        pop di
        pop si
        pop bp
        retf
; AL = an MCD record, ES its segment: ZF clear if a passable door. Uses AX, BX.
.door:  xor ah, ah
        test al, al
        jz .r
        shl ax, cl
        mov bx, ax
        add bx, [bp+14]
        cmp byte [es:bx+5], 255         ; Mcd.tu_walk
        je .no
        test byte [es:bx+8], 18h        ; Mcd.flags: MCD_DOOR, MCD_UFO_DOOR
        ret
.no:    xor ax, ax
.r:     ret
