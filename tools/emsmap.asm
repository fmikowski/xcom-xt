; SPDX-License-Identifier: GPL-3.0-or-later
; EMSMAP.SYS: maps pages 0-3 of a Lo-tech style EMS board into its page frame
; and stays out of memory (zero bytes resident). 86Box has no plain RAM card
; for the upper memory area, only EMS boards; with the pages mapped once at
; boot, the frame is ordinary RAM that USE!UMBS.SYS can hand out. This stands
; in for a Lo-tech 1MB RAM board at D000 (machines v20e / v20x: a
; second 86Box lotechems board at I/O 260h, frame D000).
;
;   DEVICE=EMSMAP.SYS [port]         hex, default 260
;
; Build: nasm -f bin -o EMSMAP.SYS tools\emsmap.asm

        bits 16
        cpu 8086
        org 0

header: dd -1
        dw 8000h                        ; character device
        dw strategy
        dw interrupt
        db 'EMSMAP$$'

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
.hex:   lodsb
        cmp al, '0'
        jb .go
        cmp al, '9'
        jbe .dig
        and al, 0DFh
        cmp al, 'A'
        jb .go
        cmp al, 'F'
        ja .go
        sub al, 7
.dig:   sub al, '0'
        mov cl, 4
        shl dx, cl
        or dl, al
        jmp .hex
.go:    or dx, dx
        jnz .port
        mov dx, 260h
.port:  xor al, al
.page:  out dx, al
        inc dx
        inc al
        cmp al, 4
        jb .page
        push cs
        pop ds
        mov dx, msg
        mov ah, 9
        int 21h
.done:  les bx, [cs:reqhdr]
        mov word [es:bx + 14], 0        ; nothing stays
        mov [es:bx + 16], cs
        mov word [es:bx + 3], 0100h
        pop es
        pop ds
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        retf

msg:    db 'EMSMAP: EMS pages 0-3 mapped into the frame', 13, 10, '$'
