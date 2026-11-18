; SPDX-License-Identifier: GPL-3.0-or-later
; LSTAMP.COM: a time stamp for the game's log (tools\optbench.ps1).
; The game is not changed: this program, run before it, puts "@hhhhhhhh "
; in front of every line the game appends to a file named LOG.TXT, the
; time of the line in counts of the PC's timer chip (1,193,182 a second,
; 32 bits, hexadecimal), taken when the game opens the log for the line.
; So the time between any two LOG lines can be read off the log, to a few
; microseconds, on the unchanged game.
;
; The clock of the stamps stands still while the log is written: the time
; from the log's open to the return of its close (150 to 350 ms a line on
; the emulated XT's disk) is left out of every later stamp, so a stretch
; with many LOG lines takes as long on this clock as it would without LOG.
;
; It stays resident in 36 paragraphs (0.56 KB): its 0.5 KB of code and data
; are moved down over the unused part of its own PSP (from 40h) and the rest
; is given back; the environment is freed. So the game has 0.6 KB less
; memory than without it. (The interrupt vectors 80h-FFh would cost nothing,
; but only 80h-BFh are free: the XT-IDE BIOS keeps its drive table at
; 0000:0300.)
;
; How: it hooks INT 21h and INT 08h.
; - Open / create (3Dh, 3Ch) of a name ending in LOG.TXT: the time is taken,
;   and the first write (40h) to the handle it returns is preceded by a write
;   of the stamp to the same handle (log_printf, src/input.c: fopen "a", one
;   line, fclose).
; - Close (3Eh) of that handle: the time since the open is added to what
;   the stamps leave out.
; - INT 08h (IRQ 0) adds the timer's divisor to a 32-bit count; a stamp is
;   that count plus what the timer chip has counted of the current period
;   (channel 0 latched; a period whose interrupt is still pending counts).
; - The game's music runs channel 0 four times faster through its own INT 08h
;   handler (src/timer.c timer_hook: divisor 16384, set through INT 21h 25h).
;   The hook sees that call, keeps its own handler first in line, jumps on to
;   the game's, and counts 16384 a period from then on; 35h for vector 8
;   answers with the handler behind it. At the game's switch back the part of
;   the fast period that had passed is not known (the chip is reprogrammed
;   before the call): half a period is taken, so a stretch across a switch
;   back is off by at most 6.9 ms; an interrupt pending right at a switch
;   (rare) puts the later stamps off by one period.
; nasm -f bin -o LSTAMP.COM lstamp.asm

        cpu 8086
        org 100h

section .text
start:  jmp install

; The resident part: it runs at CS:0040, where the installer moves it.
section .res vstart=0040h align=1 follows=.text
res0:
old21   dd 0
chain08 dd 0                    ; where INT 08h goes on: the BIOS's handler or the game's
orig08  dd 0                    ; the handler found at the start
count   dd 0                    ; timer counts up to the last IRQ 0
curdiv  dd 65536                ; the divisor now
half    dw 8000h
tval    dd 0                    ; the pending line's time
topen   dd 0                    ; when the log was opened
excl    dd 0                    ; time spent writing the log so far
h       dw 0FFFFh               ; the log's handle
hw      dw 0FFFFh               ; the same until its first write: that one gets the stamp
name    db 'LOG.TXT', 0
buf     db '@00000000 '

int08:  push ax
        mov ax, [cs:curdiv]
        add [cs:count], ax
        mov ax, [cs:curdiv+2]
        adc [cs:count+2], ax
        pop ax
        jmp far [cs:chain08]

; The time now in DX:AX; CL = the PIC's request register. DS = CS,
; interrupts off.
stamp:  push bx
        mov al, 0
        out 43h, al             ; latch channel 0
        in al, 40h
        mov bl, al
        in al, 40h
        mov bh, al
        mov al, 0Ah
        out 20h, al
        in al, 20h
        mov cl, al
        mov ax, [curdiv]
        sub ax, bx              ; counted in this period
        xor dx, dx
        test cl, 1
        jz .add
        cmp ax, [half]          ; wrapped, its interrupt still pending
        jae .add
        add ax, [curdiv]
        adc dx, [curdiv+2]
.add:   add ax, [count]
        adc dx, [count+2]
        pop bx
        ret

int21:  cmp ah, 40h
        je .write
        cmp ah, 3Eh
        je .close
        cmp ah, 3Dh
        je .open
        cmp ah, 3Ch
        je .open
        cmp ax, 2508h
        je .set8
        cmp ax, 3508h
        je .get8
.chain: jmp far [cs:old21]

.get8:  les bx, [cs:chain08]
        iret

; INT 21h 25h for vector 8: DS:DX = the game's handler, or the old one back
.set8:  push ax
        push bx
        push cx
        push dx
        push ds
        mov bx, ds
        push cs
        pop ds
        mov [chain08], dx
        mov [chain08+2], bx
        cmp dx, [orig08]
        jne .fast
        cmp bx, [orig08+2]
        jne .fast
        ; back: the chip already runs the long period again
        add word [count], 2000h
        adc word [count+2], 0
        mov byte [curdiv+1], 0  ; 65536
        mov byte [curdiv+2], 1
        mov byte [half+1], 80h
        jmp short .set
.fast:  call stamp
        mov [count], ax
        mov [count+2], dx
        mov byte [curdiv+1], 40h ; 16384
        mov byte [curdiv+2], 0
        mov byte [half+1], 20h
.set:   pop ds
        pop dx
        pop cx
        pop bx
        pop ax
        iret

.open:  push si
        push di
        push cx
        push es
        push ax
        cld
        mov si, dx
.len:   lodsb
        or al, al
        jnz .len
        sub si, 8               ; the name's last 7 characters and its end
        push cs
        pop es
        mov di, name
        mov cx, 8
        repe cmpsb
        pop ax
        pop es
        pop cx
        pop di
        pop si
        jne .chain
        push ds
        push ax
        push cx
        push dx
        push cs
        pop ds
        call stamp
        mov [topen], ax
        mov [topen+2], dx
        sub ax, [excl]
        sbb dx, [excl+2]
        mov [tval], ax
        mov [tval+2], dx
        pop dx
        pop cx
        pop ax
        pop ds
        pushf
        call far [cs:old21]
        jc .oret
        mov [cs:h], ax
        mov [cs:hw], ax
.oret:  sti
        retf 2

.close: cmp bx, [cs:h]
        jne .chain
        pushf
        call far [cs:old21]
        pushf
        push ax
        push cx
        push dx
        push ds
        push cs
        pop ds
        cli
        call stamp
        sub ax, [topen]
        sbb dx, [topen+2]
        add [excl], ax
        adc [excl+2], dx
        mov word [h], 0FFFFh
        pop ds
        pop dx
        pop cx
        pop ax
        popf
        sti
        retf 2

.write: cmp bx, [cs:hw]
        jne .chain
        push ax
        push cx
        push dx
        push di
        push ds
        push cs
        pop ds
        mov word [hw], 0FFFFh
        mov di, buf + 1
        mov dx, [tval+2]
        call hex4
        mov dx, [tval]
        call hex4
        mov dx, buf
        mov cx, 10
        mov ah, 40h
        pushf
        call far [old21]
        pop ds
        pop di
        pop dx
        pop cx
        pop ax
        jmp .chain

hex4:   mov cx, 4
.l:     push cx
        mov cl, 4
        rol dx, cl
        pop cx
        mov al, dl
        and al, 0Fh
        add al, 90h
        daa
        adc al, 40h
        daa
        mov [di], al
        inc di
        loop .l
        ret
res_end:
res_len equ res_end - res0
RES_AT  equ 40h
RES_PARAS equ (RES_AT + res_len + 15) / 16

section .inst vstart=103h+res_len align=1 follows=.res
install:
        mov es, [2Ch]           ; the environment goes
        mov ah, 49h
        int 21h
        push cs
        pop es
        mov si, 103h            ; the resident part down to CS:0040
        mov di, RES_AT
        mov cx, res_len
        cld
        rep movsb
        mov ax, 3521h
        int 21h
        mov [old21], bx
        mov [old21+2], es
        mov ax, 3508h
        int 21h
        mov [orig08], bx
        mov [orig08+2], es
        mov [chain08], bx
        mov [chain08+2], es
        cli
        mov al, 34h             ; channel 0, mode 2, 65536: as the game sets it
        out 43h, al
        xor al, al
        out 40h, al
        out 40h, al
        mov dx, int08
        mov ax, 2508h
        int 21h
        mov dx, int21
        mov ax, 2521h
        int 21h
        sti
        mov dx, RES_PARAS
        mov ax, 3100h
        int 21h
