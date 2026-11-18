; SPDX-License-Identifier: GPL-3.0-or-later
; XMSQ.COM: prints the XMS driver's free memory (function 08h: largest block and
; total, in KB) or "no XMS driver". 86Box's DOS MEM does not show XMS on an 8088/V20
; (it does not look for it), so the v20x profile is checked with this.
; Build: nasm -f bin -o XMSQ.COM tools\xmsq.asm

        bits 16
        cpu 8086
        org 100h

        mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne .none
        mov ax, 4310h
        int 2Fh
        mov [entry], bx
        mov [entry + 2], es
        mov ah, 0                       ; version
        call far [entry]
        push bx                         ; driver revision
        push ax                         ; XMS version (BCD)
        mov dx, m_ver
        mov ah, 9
        int 21h
        pop ax
        call hexw
        mov dx, m_rev
        mov ah, 9
        int 21h
        pop ax
        call hexw
        mov dx, m_big
        mov ah, 9
        int 21h
        mov ah, 8
        xor bl, bl
        call far [entry]
        push dx
        push ax
        pop ax                          ; largest free block, KB
        call decw
        mov dx, m_tot
        mov ah, 9
        int 21h
        pop ax                          ; total free, KB
        call decw
        mov dx, m_kb
        mov ah, 9
        int 21h
        mov ax, 4C00h
        int 21h
.none:  mov dx, m_none
        mov ah, 9
        int 21h
        mov ax, 4C01h
        int 21h

decw:   mov bx, 10
        xor cx, cx
.d:     xor dx, dx
        div bx
        push dx
        inc cx
        or ax, ax
        jnz .d
.p:     pop dx
        add dl, '0'
        mov ah, 2
        int 21h
        loop .p
        ret

hexw:   mov cx, 4
.h:     rol ax, 1
        rol ax, 1
        rol ax, 1
        rol ax, 1
        push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .o
        add al, 7
.o:     mov dl, al
        mov ah, 2
        int 21h
        pop ax
        loop .h
        ret

entry   dd 0
m_ver   db 'XMS version $'
m_rev   db ', driver $'
m_big   db 13, 10, 'largest free block $'
m_tot   db ' KB, total free $'
m_kb    db ' KB', 13, 10, '$'
m_none  db 'no XMS driver', 13, 10, '$'
