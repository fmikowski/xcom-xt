; SPDX-License-Identifier: GPL-3.0-or-later
; HMAQ.COM: prints what DOS (DOS=HIGH, MS-DOS 5+) has left free in the high memory
; area (INT 2Fh AX=4A01h: BX = bytes, ES:DI = the start) and the A20 line's state
; (XMS function 07h), or "no free HMA". docs/design.md "The HMA".
; Build: nasm -f bin -o HMAQ.COM tools\hmaq.asm

        bits 16
        cpu 8086
        org 100h

        mov dx, m_dos
        mov ah, 9
        int 21h
        mov ax, 3000h
        int 21h
        push ax
        xor ah, ah
        call decw
        mov dl, '.'
        mov ah, 2
        int 21h
        pop ax
        mov al, ah
        xor ah, ah
        call decw
        mov ax, 3306h                   ; true version, DH bit 4 = DOS in HMA
        xor bx, bx
        xor dx, dx
        int 21h
        push dx
        mov dx, m_flags
        mov ah, 9
        int 21h
        pop ax
        mov al, ah
        xor ah, ah
        call hexw
        mov dx, m_hma
        mov ah, 9
        int 21h
        mov ax, 4A01h
        xor bx, bx
        mov di, 0FFFFh
        int 2Fh
        push es
        push di
        mov ax, bx
        call decw
        mov dx, m_at
        mov ah, 9
        int 21h
        pop di
        pop ax
        push di
        call hexw
        mov dl, ':'
        mov ah, 2
        int 21h
        pop ax
        call hexw
        mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne .done
        mov ax, 4310h
        int 2Fh
        mov [entry], bx
        mov [entry + 2], es
        mov dx, m_a20
        mov ah, 9
        int 21h
        mov ah, 7
        call far [entry]
        call hexw
.done:  mov dx, m_crlf
        mov ah, 9
        int 21h
        mov ax, 4C00h
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
m_dos   db 'DOS $'
m_flags db ', 3306h DH $'
m_hma   db 13, 10, 'free HMA $'
m_at    db ' bytes at $'
m_a20   db 13, 10, 'A20 (XMS 07h) $'
m_crlf  db 13, 10, '$'
