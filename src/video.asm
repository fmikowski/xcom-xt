; SPDX-License-Identifier: GPL-3.0-or-later
; Low-level video helpers. Large memory model, Watcom __cdecl (far calls).

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

segment VIDEO_TEXT public class=CODE

; void __cdecl vid_setmode(unsigned char mode)
        global _vid_setmode
_vid_setmode:
        push bp
        mov bp, sp
        mov al, [bp+6]
        xor ah, ah
        int 10h
        pop bp
        retf

; Hercules (src/gfx.h): the copier's entry in its heap block
; (src/hgcasm.asm), segment 0 = none (EGA, CGA). Watcom register calls:
; arguments in AX, DX, BX, CX; every register kept.
hgc_ptr dw 0, 0

; void hgc_set(unsigned seg, unsigned off)
        global hgc_set_
hgc_set_:
        mov [cs:hgc_ptr], dx
        mov [cs:hgc_ptr+2], ax
        retf

; unsigned char hgc_off(void): Hercules off (video off, the configuration
; switch back to 0, no more conversions): returns 7 for the BIOS mode set
; after it, else 3.
        global hgc_off_
hgc_off_:
        mov ax, 3
        cmp word [cs:hgc_ptr+2], 0
        je .text
        push dx
        mov dx, 3B8h
        xor al, al
        out dx, al
        mov dl, 0BFh
        out dx, al
        pop dx
        mov word [cs:hgc_ptr+2], 0
        mov al, 7
.text:  retf

; void hgc_mark(int x0, int y0, int x1, int y1): copier op 0
        global hgc_mark_
hgc_mark_:
        push si
        xor si, si
go:     cmp word [cs:hgc_ptr+2], 0
        je .none
        push ax
        push cx
        push bx
        push dx
        push ax
        push si
        call far [cs:hgc_ptr]
        add sp, 10
        pop ax
.none:  pop si
        retf

; void hgc_show(int x0, int y0, int x1, int y1): op 1
        global hgc_show_
hgc_show_:
        push si
        mov si, 1
        jmp short go

; void hgc_runs(const void far *frame): op 3, what an intro frame wrote
        global hgc_runs_
hgc_runs_:
        push si
        mov si, 3
        jmp short go

; void hgc_buf(unsigned bufseg, unsigned rows_r0, unsigned b0, unsigned n): op 4
        global hgc_buf_
hgc_buf_:
        push si
        mov si, 4
        jmp short go

; void hgc_white2(void far *p, unsigned n): op 5
        global hgc_white2_
hgc_white2_:
        push si
        mov si, 5
        jmp short go

; void hgc_flush(int scan): op 2 (the copier reads only AX)
        global hgc_flush_
hgc_flush_:
        push si
        mov si, 2
        jmp short go
