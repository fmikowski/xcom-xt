; SPDX-License-Identifier: GPL-3.0-or-later
; UMBPROBE.COM: prints a map of the upper memory area C000-EFFF in 4 KB
; blocks, one row per 64 KB segment (C000, D000, E000), 16 characters per row:
;   R  the block's byte at offset 10h can be written and read back (RAM)
;   o  not writable, reads something other than FFh (ROM)
;   .  not writable, reads FFh or 00h (nothing there)
; It writes a byte and puts the old one back (EMS page frames and RAM cards
; show up as R only where memory is mapped). Used on the v20e / v20x 86Box
; machines to see where a RAM card sits (tests/README.md). Then it waits
; for ever (86Box is stopped by the test script).
; Build: nasm -f bin -o UMBPROBE.COM tools\umbprobe.asm

        bits 16
        cpu 8086
        org 100h

start:  mov si, 0C000h                  ; segment of the row
.row:   mov ax, si
        call hex4
        mov al, ':'
        call putc
        mov al, ' '
        call putc
        mov cx, 16
        mov dx, si                      ; segment of the block
.blk:   mov es, dx
        mov bl, [es:10h]
        mov al, bl
        xor al, 0A5h
        mov [es:10h], al
        mov ah, [es:10h]
        mov [es:10h], bl
        cmp ah, al
        jne .no
        mov al, 'R'
        jmp .out
.no:    mov al, '.'
        cmp bl, 0FFh
        je .out
        cmp bl, 0
        je .out
        mov al, 'o'
.out:   call putc
        add dx, 100h                    ; 4 KB
        loop .blk
        mov al, 13
        call putc
        mov al, 10
        call putc
        add si, 1000h
        cmp si, 0F000h
        jb .row
        jmp $                           ; stays on screen for the screenshot

hex4:   mov cx, 4
.d:     rol ax, 1
        rol ax, 1
        rol ax, 1
        rol ax, 1
        push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .p
        add al, 7
.p:     call putc
        pop ax
        loop .d
        ret

putc:   push ax
        push dx
        mov dl, al
        mov ah, 2
        int 21h
        pop dx
        pop ax
        ret
