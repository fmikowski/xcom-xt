; SPDX-License-Identifier: GPL-3.0-or-later
; UMBFILL.SYS: a device driver that does nothing and stays resident with the
; given size, to use up conventional or upper memory like a driver the test
; machine does not have (a real XT loads a 3,920-byte program into the UMB
; that the 86Box machines v20e / v20x lack).
;
;   DEVICEHIGH=UMBFILL.SYS [bytes]    decimal, default 3920
;
; Build: nasm -f bin -o UMBFILL.SYS tools\umbfill.asm

        bits 16
        cpu 8086
        org 0

header: dd -1
        dw 8000h                        ; character device
        dw strategy
        dw interrupt
        db 'UMBFILL$'

reqhdr  dd 0

strategy:
        mov [cs:reqhdr], bx
        mov [cs:reqhdr + 2], es
        retf

interrupt:
        push ax
        push bx
        push cx
        push dx
        push si
        push ds
        push es
        les bx, [cs:reqhdr]
        cmp byte [es:bx + 2], 0         ; INIT
        jne .done
        lds si, [es:bx + 18]            ; the CONFIG.SYS line
.name:  lodsb                           ; skip the driver's own name
        cmp al, ' '
        ja .name
        dec si
.sp:    lodsb
        cmp al, ' '
        je .sp
        dec si
        xor dx, dx
.dec:   lodsb
        sub al, '0'
        cmp al, 9
        ja .go
        xor ah, ah
        mov cx, ax
        mov ax, dx
        mov dx, 10
        mul dx
        add ax, cx
        mov dx, ax
        jmp .dec
.go:    or dx, dx
        jnz .size
        mov dx, 3920
.size:  les bx, [cs:reqhdr]
        mov [es:bx + 14], dx            ; break address: this many bytes stay
        mov [es:bx + 16], cs
.done:  mov word [es:bx + 3], 0100h
        pop es
        pop ds
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        retf
