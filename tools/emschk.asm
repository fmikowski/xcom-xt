; SPDX-License-Identifier: GPL-3.0-or-later
; EMSCHK.COM: does mapping EMS pages at physical page 1 really change what the
; frame shows? Writes 'A' to logical page 0 and 'B' to logical page 1 through
; physical page 1, maps page 0 again and reads it back (expect A). Also shows
; the byte at D400:0 after each step. Appends a line to C:\EMSCHK.TXT.
        cpu 8086
        org 100h
        mov ah, 40h
        int 67h
        mov [st], ah
        mov ah, 41h
        int 67h
        mov [frame], bx
        mov ah, 43h
        mov bx, 2
        int 67h
        mov [ast], ah
        mov [hnd], dx
        mov ax, [frame]
        add ax, 400h
        mov es, ax
        ; page 0 -> 'A'
        mov ax, 4401h
        xor bx, bx
        mov dx, [hnd]
        int 67h
        mov [m0], ah
        mov byte [es:0], 'A'
        call peek
        mov [d1], al
        ; page 1 -> 'B'
        mov ax, 4401h
        mov bx, 1
        mov dx, [hnd]
        int 67h
        mov [m1], ah
        mov byte [es:0], 'B'
        call peek
        mov [d2], al
        ; page 0 again
        mov ax, 4401h
        xor bx, bx
        mov dx, [hnd]
        int 67h
        mov al, [es:0]
        mov [back], al
        call peek
        mov [d3], al
        mov ah, 45h
        mov dx, [hnd]
        int 67h
        ; the line
        mov di, line
        mov al, [st]
        call hex2
        mov al, [ast]
        call hex2
        mov al, [m0]
        call hex2
        mov al, [m1]
        call hex2
        mov ax, [frame]
        xchg al, ah
        call hex2
        mov al, [frame]
        call hex2
        mov al, ' '
        mov [di], al
        inc di
        mov al, [back]
        mov [di], al
        inc di
        mov al, [d1]
        call hex2
        mov al, [d2]
        call hex2
        mov al, [d3]
        call hex2
        mov word [di], 0A0Dh
        add di, 2
        ; append to the file
        mov ax, 3D01h
        mov dx, fname
        int 21h
        jnc .op
        mov ah, 3Ch
        xor cx, cx
        mov dx, fname
        int 21h
        jc .x
.op:    mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        mov cx, di
        sub cx, msg
        mov dx, msg
        mov ah, 40h
        int 21h
        mov ah, 3Eh
        int 21h
.x:     mov ax, 4C00h
        int 21h

peek:   push ds
        mov ax, 0D400h
        mov ds, ax
        mov al, [0]
        pop ds
        ret

hex2:   push ax
        mov cl, 4
        shr al, cl
        call nib
        pop ax
        and al, 0Fh
nib:    add al, '0'
        cmp al, '9'
        jbe .s
        add al, 7
.s:     mov [di], al
        inc di
        ret

fname   db 'C:\EMSCHK.TXT', 0
st      db 0
ast     db 0
m0      db 0
m1      db 0
back    db 0
d1      db 0
d2      db 0
d3      db 0
hnd     dw 0
frame   dw 0
msg     db 'EMSCHK st/alloc/map0/map1/frame back(A ok) D400 bytes: '
line    times 40 db 0
