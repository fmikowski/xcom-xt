; SPDX-License-Identifier: GPL-3.0-or-later
; The intro's frame deltas into video memory (src/cut.c, tools/intro.py).
; Lives in the cutscene overlay section with cut.c. Large model, Watcom
; __cdecl.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU
%include "wide.inc"

segment CUTASM_TEXT public class=CODE

c_mask  db 0
c_runs  dw 0

; unsigned __cdecl cut_runs(const unsigned char far *src, unsigned planes,
;                           unsigned vseg)
; src: per plane u16 runs, then runs x (u8 skip, u8 n, n bytes). planes 4
; (EGA: each plane through the sequencer's map mask, 1, 2, 4, 8) or 1
; (CGA). Returns the number of runs. Leaves the map mask at all planes.
        global _cut_runs
_cut_runs:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        lds si, [bp+6]
        mov es, [bp+12]
        mov byte [cs:c_mask], 1
        mov word [cs:c_runs], 0
        cld
.plane:
        cmp word [bp+10], 1
        je .nomask
        mov dx, 03C4h
        mov al, 2
        mov ah, [cs:c_mask]
        out dx, ax
        shl byte [cs:c_mask], 1
.nomask:
        lodsw
        mov bx, ax
        add [cs:c_runs], ax
        xor di, di
        xor ah, ah
        test bx, bx
        jz .next
.run:
        lodsb                   ; skip
        add di, ax
        lodsb                   ; bytes
        mov cx, ax
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
        dec bx
        jnz .run
.next:
        dec word [bp+10]
        jnz .plane
        mov dx, 03C4h
        mov ax, 0F02h
        cmp byte [cs:c_mask], 1
        je .done
        out dx, ax
.done:
        mov ax, [cs:c_runs]
        pop ds
        pop di
        pop si
        pop bp
        retf

; unsigned __cdecl cut_fli(const unsigned char far *frame)
; VGA (mode 13h): one frame of the DOS intro as UFOINT.FLI has it
; (tools/intro.py --vga: u16 chunks, then the chunks): COLOR (11: the DAC,
; 6 bits), LC (12), BLACK (13), BRUN (15) and COPY (16) into A000h.
; Returns the number of runs (packets).
        global _cut_fli
_cut_fli:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov word [cs:c_runs], 0
        lds si, [bp+6]
        mov ax, 0A000h
        mov es, ax
        cld
        lodsw                           ; chunks
        mov cx, ax
.chunk: jcxz .done
        push cx
        mov dx, si                      ; the chunk: u32 size, u16 type
        lodsw
        mov bx, ax
        lodsw
        lodsw
        push dx
        push bx
        cmp ax, 11
        je .color
        cmp ax, 12
        je .lc
        cmp ax, 15
        je .brun
        cmp ax, 13
        je .black
        cmp ax, 16
        je .copy
.next:  pop bx
        pop si
        add si, bx
        pop cx
        dec cx
        jmp short .chunk
.done:  mov ax, [cs:c_runs]
        pop ds
        pop di
        pop si
        pop bp
        retf

.color: lodsw                           ; packets: u8 skip, u8 n (0: 256), n x RGB
        mov bx, ax
        xor di, di
.cp:    or bx, bx
        jz .next
        lodsb
        xor ah, ah
        add di, ax
        lodsb
        mov cx, ax                      ; (AH 0)
        or cx, cx
        jnz .cn
        mov ch, 1
.cn:    mov dx, 3C8h
        mov ax, di
        out dx, al
        inc dx
        add di, cx
        mov ax, cx
        shl ax, 1
        add cx, ax
.cc:    lodsb
        out dx, al
        loop .cc
        dec bx
        jmp short .cp

.lc:    lodsw                           ; lines skipped, lines; per line u8 packets
        mov cx, 320
        mul cx
        mov di, ax
        lodsw
        mov dx, ax
.ll:    or dx, dx
        jz .next
        push di
        lodsb
        mov bl, al
.lp:    or bl, bl
        jz .le
        lodsb                           ; skip
        xor ah, ah
        add di, ax
        lodsb                           ; size: n copied, -n one byte n times
        cbw
        or ax, ax
        jl .lr
        mov cx, ax
        WMOVSB
        jmp short .ln
.lr:    neg ax
        mov cx, ax
        lodsb
        WSTOSB
.ln:    inc word [cs:c_runs]
        dec bl
        jmp short .lp
.le:    pop di
        add di, 320
        dec dx
        jmp short .ll

.brun:  xor di, di                      ; 200 lines; per line u8 (unused), then
        mov dx, 200                     ; n: one byte n times, -n: n copied
.bl:    push di
        lodsb
        mov bx, 320
.bp:    or bx, bx
        jle .be
        lodsb
        cbw
        or ax, ax
        jl .bc
        mov cx, ax
        sub bx, ax
        lodsb
        WSTOSB
        jmp short .bn
.bc:    neg ax
        mov cx, ax
        sub bx, ax
        WMOVSB
.bn:    inc word [cs:c_runs]
        jmp short .bp
.be:    pop di
        add di, 320
        dec dx
        jnz .bl
        jmp .next

.black: xor ax, ax
        xor di, di
        mov cx, 32000
        rep stosw
        jmp .next

.copy:  xor di, di
        mov cx, 32000
        rep movsw
        jmp .next
