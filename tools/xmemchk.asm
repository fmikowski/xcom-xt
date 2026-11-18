; SPDX-License-Identifier: GPL-3.0-or-later
; XMEMCHK.COM: appends one line to XMEMCHK.TXT with what the EMS and XMS
; drivers have free and where INT 21h, 22h and 08h point. Run before and
; after the game (tools\emutest.ps1 -Pre / -Post): the two lines must be
; equal, else the game left EMS pages, XMS memory or a vector behind.
;   nasm -f bin -o XMEMCHK.COM tools\xmemchk.asm
        cpu 8086
        org 100h

start:  mov di, line + 8                ; after "XMEMCHK "
        ; EMS: the driver's name at offset 10 of INT 67h's segment
        xor ax, ax
        mov es, ax
        mov es, [es:67h * 4 + 2]
        mov si, emmname
        push di
        mov di, 10
        mov cx, 8
        repe cmpsb
        pop di
        push cs
        pop es
        jne .noems
        mov ah, 42h
        int 67h
        or ah, ah
        jnz .noems
        push dx
        mov si, s_ems
        call puts
        mov ax, bx
        call hex
        mov al, '/'
        stosb
        pop ax
        call hex
        mov si, s_hnd
        call puts
        mov ah, 4Bh
        int 67h
        mov ax, bx
        call hex
        jmp .xms
.noems: mov si, s_noems
        call puts
.xms:   mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne .noxms
        mov ax, 4310h
        int 2Fh
        mov [xent], bx
        mov [xent + 2], es
        push cs
        pop es
        mov ah, 08h
        call far [xent]
        push dx
        push ax
        mov si, s_xms
        call puts
        pop ax
        call hex
        mov al, '/'
        stosb
        pop ax
        call hex
        ; the HMA (docs/design.md "The HMA"): A20's state (07h) and whether
        ; a program may have it (01h, given back at once with 02h); "dos"
        ; with DOS=HIGH: then what DOS left free (INT 2Fh 4A01h)
        mov si, s_a20
        call puts
        mov ah, 07h
        call far [xent]
        call hex
        mov si, s_hma
        call puts
        mov ax, 4A01h
        xor bx, bx
        push di
        mov di, 0FFFFh
        int 2Fh
        pop di
        push cs
        pop es
        or bx, bx
        jz .own
        mov si, s_dos
        call puts
        mov ax, bx
        call hex
        jmp .vec
.own:   mov ah, 01h
        mov dx, 0FFFFh
        call far [xent]
        push ax
        cmp ax, 1
        jne .busy
        mov ah, 02h
        call far [xent]
.busy:  pop ax
        call hex
        jmp .vec
.noxms: push cs
        pop es
        mov si, s_noxms
        call puts
.vec:   mov si, s_v21
        mov bx, 21h * 4
        call vec
        mov si, s_v22
        mov bx, 22h * 4
        call vec
        mov si, s_v08
        mov bx, 08h * 4
        call vec
        mov ax, 0A0Dh
        stosw
        ; append to XMEMCHK.TXT
        mov ax, 3D01h
        mov dx, fname
        int 21h
        jnc .open
        mov ah, 3Ch
        xor cx, cx
        mov dx, fname
        int 21h
        jc .out
.open:  mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        mov ah, 40h
        mov dx, line
        mov cx, di
        sub cx, line
        int 21h
        mov ah, 3Eh
        int 21h
.out:   mov ax, 4C00h
        int 21h

; SI = label, BX = vector address: " label SSSS:OOOO"
vec:    call puts
        push ds
        xor ax, ax
        mov ds, ax
        mov dx, [bx]
        mov ax, [bx + 2]
        pop ds
        call hex
        mov al, ':'
        stosb
        mov ax, dx
        ; fall through
; AX as four hex digits at ES:DI
hex:    push cx
        mov cx, 4
.d:     push cx
        mov cl, 4
        rol ax, cl
        pop cx
        push ax
        and al, 15
        add al, '0'
        cmp al, '9'
        jbe .p
        add al, 7
.p:     stosb
        pop ax
        loop .d
        pop cx
        ret

; SI = zero-terminated string to ES:DI
puts:   lodsb
        or al, al
        jz .e
        stosb
        jmp puts
.e:     ret

emmname db 'EMMXXXX0'
s_ems   db 'ems free ', 0
s_hnd   db ' handles ', 0
s_noems db 'ems none', 0
s_xms   db ' xms free ', 0
s_noxms db ' xms none', 0
s_a20   db ' a20 ', 0
s_hma   db ' hma ', 0
s_dos   db 'dos ', 0
s_v21   db ' int21 ', 0
s_v22   db ' int22 ', 0
s_v08   db ' int08 ', 0
fname   db 'XMEMCHK.TXT', 0
xent    dd 0
line    db 'XMEMCHK '
