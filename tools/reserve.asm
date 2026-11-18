; SPDX-License-Identifier: GPL-3.0-or-later
; RESERVE.COM n: stays resident with as much memory as leaves about n KB
; for the next program (what XCOMXT logs as "DOS free at start"), so 86Box
; tests can run with the free memory of a real machine with its drivers.
; nasm -f bin -o RESERVE.COM reserve.asm (tools\emutest.ps1 -FreeKB n)

        cpu 8086
        org 100h

start:
        mov si, 81h
        xor bx, bx
.skip:  lodsb
        cmp al, ' '
        je .skip
.digit: sub al, '0'
        cmp al, 9
        ja .parsed
        mov cl, al
        xor ch, ch
        mov ax, 10
        mul bx
        add ax, cx
        mov bx, ax
        lodsb
        jmp .digit
.parsed:
        ; paragraphs the next program may have: n * 64
        mov cl, 6
        shl bx, cl
        ; the environment goes, its size is what the next program's copy
        ; of it takes (plus the program name and two arena headers)
        mov es, [2Ch]
        mov ax, es
        dec ax
        push ds
        mov ds, ax
        mov dx, [3]             ; environment paragraphs
        pop ds
        add dx, 3
        mov ah, 49h
        int 21h
        ; keep = (top - PSP) - n * 64 - environment
        mov ax, [2]
        mov cx, cs
        sub ax, cx
        sub ax, bx
        jb .small
        sub ax, dx
        jb .small
        cmp ax, (stop - start + 100h + 15) / 16
        jae .keep
.small: mov ax, (stop - start + 100h + 15) / 16
.keep:  mov dx, ax
        mov ax, 3100h
        int 21h
stop:
