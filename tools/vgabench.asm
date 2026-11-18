; SPDX-License-Identifier: GPL-3.0-or-later
; VGABENCH.COM: what a VGA intro movie would cost on this machine (docs/design.md,
; "VGA outside the battle", the intro). Writes BENCH.TXT on the current drive:
;   disk   KB/s reading UIV.PAK (1 MB, in 4 KB and in 16 KB reads, DOS int 21h)
;   xms    KB/s moving 1 MB from a 64 KB XMS block to conventional memory (16 KB moves)
;   vga    KB/s writing mode 13h video memory (rep movsw, 64000 bytes x 16)
;   runs   thousands of FLI LC-like runs a second (skip byte, count byte, copy n=8)
; Times from the BIOS tick (55 ms): every test runs for seconds.
;   nasm -f bin -o build\VGABENCH.COM tools\vgabench.asm
        cpu 8086
        org 100h

BUF     equ 4000h                       ; 16 KB buffer at CS:4000 (the .COM's 64 KB)

start:  mov dx, outname
        xor cx, cx
        mov ah, 3Ch
        int 21h
        jc .quit
        mov [ofd], ax

        mov word [chunk], 4096
        call disk
        mov si, s_disk4
        call report
        mov word [chunk], 16384
        call disk
        mov si, s_disk16
        call report

        call xms
        mov si, s_xms
        call report

        mov ax, 13h
        int 10h
        call vga
        mov [vres], ax
        call runs
        push ax
        mov ax, 3
        int 10h
        mov ax, [vres]
        mov si, s_vga
        call report
        pop ax
        mov si, s_runs
        call report

        mov si, s_done
        call puts
        mov bx, [ofd]
        mov ah, 3Eh
        int 21h
.quit:  mov ax, 4C00h
        int 21h

; ---- timing: AX = ticks since t0 (call tick0 first)
tick0:  xor ax, ax
        mov es, ax
        mov ax, [es:46Ch]
.w:     cmp ax, [es:46Ch]               ; start on a tick's edge
        je .w
        mov ax, [es:46Ch]
        mov [t0], ax
        ret
ticks:  xor ax, ax
        mov es, ax
        mov ax, [es:46Ch]
        sub ax, [t0]
        ret

; ---- disk: AX = KB/s reading 1 MB of UIV.PAK in [chunk] reads
disk:   mov dx, inname
        mov ax, 3D00h
        int 21h
        jc .fail
        mov [fd], ax
        call tick0
        mov word [left], 1024
.r:     mov bx, [fd]
        mov cx, [chunk]
        mov dx, BUF
        mov ah, 3Fh
        int 21h
        jc .done
        cmp ax, cx
        jb .done
        mov ax, [chunk]
        mov cl, 10
        shr ax, cl
        sub [left], ax
        ja .r
.done:  call ticks
        push ax
        mov bx, [fd]
        mov ah, 3Eh
        int 21h
        pop cx
        mov ax, 1024
        sub ax, [left]
        jmp kbs
.fail:  xor ax, ax
        ret

; AX KB in CX ticks -> AX KB/s (KB * 182 / (ticks * 10))
kbs:    jcxz .z
        mov bx, 182
        mul bx
        push dx
        push ax
        mov ax, cx
        mov bx, 10
        mul bx
        mov cx, ax
        pop ax
        pop dx
        div cx
        ret
.z:     xor ax, ax
        ret

; ---- xms: AX = KB/s, 64 x 16 KB moves from an EMB to BUF (0 without XMS)
xms:    mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne .none
        mov ax, 4310h
        int 2Fh
        mov [xdrv], bx
        mov [xdrv+2], es
        mov ah, 09h                     ; a 64 KB block (moved round 16 times)
        mov dx, 64
        call far [xdrv]
        or ax, ax
        jz .none
        mov [emb.sh], dx                ; move: EMB -> BUF
        mov word [emb.dh], 0
        mov [emb.do], word BUF
        mov [emb.do+2], cs
        mov word [emb.len], 16384
        mov word [emb.len+2], 0
        call tick0
        mov word [left], 64
        xor bp, bp
.m:     mov [emb.so], bp
        mov word [emb.so+2], 0
        mov si, emb
        mov ah, 0Bh
        call far [xdrv]
        add bp, 16384                   ; (offsets in the first 64 KB, wrapping: same speed)
        dec word [left]
        jnz .m
        call ticks
        push ax
        mov dx, [emb.sh]
        mov ah, 0Ah
        call far [xdrv]
        pop cx
        mov ax, 1024
        jmp kbs
.none:  xor ax, ax
        ret

; ---- vga: AX = KB/s, 16 x 64000 bytes to A000 (rep movsw from BUF, wrapping)
vga:    call tick0
        mov word [left], 16
        push ds
.f:     mov ax, 0A000h
        mov es, ax
        xor di, di
        mov si, BUF
        mov cx, 32000
        cld
        rep movsw                       ; (reads past BUF: the segment, any bytes)
        dec word [cs:left]
        jnz .f
        pop ds
        call ticks
        mov cx, ax
        mov ax, 1000                    ; 16 x 62.5 KB
        jmp kbs

; ---- runs: AX = thousands of runs a second: 65536 x (skip, count 8, 8 bytes)
; to A000 (mode 13h)
runs:   call tick0
        mov ax, 0A000h
        mov es, ax
        mov word [left], 8
.o:     mov si, BUF
        xor di, di
        mov dx, 8192
.r:     lodsb                           ; skip
        xor ah, ah
        and al, 7
        add di, ax
        lodsb                           ; count
        mov cx, 8
        rep movsb
        and si, 3FFFh
        or si, BUF
        and di, 7FFFh
        dec dx
        jnz .r
        dec word [left]
        jnz .o
        call ticks
        mov cx, ax
        mov ax, 64                      ; 65536 runs = 64 "KB" of runs
        jmp kbs

; ---- output: the line SI (ASCIIZ) then AX in decimal
report: push ax
        call puts
        pop ax
        mov di, num + 6
        mov byte [di], 13
        mov byte [di+1], 10
        mov cx, 10
.d:     xor dx, dx
        div cx
        add dl, '0'
        dec di
        mov [di], dl
        or ax, ax
        jnz .d
        mov dx, di
        mov cx, num + 8
        sub cx, di
        jmp short write
puts:   mov dx, si
        xor cx, cx
.l:     lodsb
        or al, al
        jz .e
        inc cx
        jmp .l
.e:
write:  mov bx, [ofd]
        mov ah, 40h
        int 21h
        ret

outname db "C:\BENCH.TXT", 0
inname  db "C:\UIV.PAK", 0
s_disk4 db "disk 4K KB/s ", 0
s_disk16 db "disk 16K KB/s ", 0
s_xms   db "xms KB/s ", 0
s_vga   db "vga KB/s ", 0
s_runs  db "runs k/s ", 0
s_done  db "done", 13, 10, 0
ofd     dw 0
fd      dw 0
chunk   dw 0
left    dw 0
vres    dw 0
t0      dw 0
xdrv    dd 0
emb:
emb.len dd 0
emb.sh  dw 0
emb.so  dd 0
emb.dh  dw 0
emb.do  dd 0
num     times 8 db 0
