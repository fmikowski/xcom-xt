; SPDX-License-Identifier: GPL-3.0-or-later
; XMS driver calls for sound.c: finding the driver (INT 2Fh 4300h / 4310h)
; and calling its entry point with registers from a small block. Also the
; tight battles' picture faults (sprite_in, sprite_fault), which call it. Large
; model, Watcom __cdecl (SI, DI, BP, DS kept).

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

extern _g_sc_top:wrt DGROUP, _g_sc_xh:wrt DGROUP, _g_sc_cur:wrt DGROUP, _g_sc_n:wrt DGROUP
extern _g_sc_off:wrt DGROUP, _g_sc_common:wrt DGROUP, _g_sc_head:wrt DGROUP, _g_sc_size:wrt DGROUP
extern _g_sc_buf:wrt DGROUP, _g_sc_fd:wrt DGROUP, _g_sc_pos:wrt DGROUP, _g_no_rows:wrt DGROUP
extern _g_sc_tag:wrt DGROUP

segment _DATA public align=2 class=DATA
group DGROUP _DATA

segment SNDASM_TEXT public class=CODE

entry   dw 0, 0

; unsigned long __cdecl xms_find(void): driver entry point, 0 = no driver
        global _xms_find
_xms_find:
        push es
        push bx
        mov ax, 4300h
        int 2Fh
        cmp al, 80h
        jne .none
        mov ax, 4310h
        int 2Fh
        mov [cs:entry], bx
        mov [cs:entry + 2], es
        mov ax, bx
        mov dx, es
        pop bx
        pop es
        retf
.none:  xor ax, ax
        xor dx, dx
        pop bx
        pop es
        retf

;-----------------------------------------------------------------------
; void __cdecl sprite_in(unsigned char __far * __far *e, unsigned i): before
; gfx_sprite draws sprite i (table entry e): a picture of the store whose EMS
; page is not mapped, one of the XMS store (g_sc_tag <= segment != g_sc_cur),
; or a NULL one with the disk cache's buffer there: sprite_fault.
        global _sprite_in
_sprite_in:
        push bp
        mov bp, sp
        les bx, [bp + 6]
        mov ax, [es:bx + 2]
        cmp ax, [_g_sc_tag]
        jb .low
        cmp ax, [_g_sc_cur]
        jne .fault
        pop bp
        retf
.low:   or ax, ax
        jnz .done
        cmp word [_g_sc_buf + 2], 0
        jne .fault
.done:  pop bp
        retf
.fault: pop bp
        jmp _sprite_fault               ; (the same arguments)

;-----------------------------------------------------------------------
; void __cdecl sprite_fault(unsigned char __far * __far *e, unsigned i):
; the picture of table entry e (sprite i) out of conventional memory, for
; gfx_sprite (docs/design.md, "Tight battles"; src/sprcache.c has the
; variables). Here for the XMS entry point. EMS (g_sc_top): the entry's
; segment names its logical page, mapped at physical page 2 (g_sc_cur = that
; segment, 0 if the driver refused). Else a copy into the buffer g_sc_buf,
; emptied when full (each entry: the sprite's number, the length, the
; pointer the table had), from the XMS store (g_sc_xh: the entry is
; F000h + its length in words : its paragraph there) or read from the
; mission pack (g_sc_fd; the entry is NULL). The entry is g_no_rows until the copy is done.
        global _sprite_fault
_sprite_fault:
        push bp
        mov bp, sp
        push si
        push di
        les di, [bp + 6]                ; ES:DI = the entry
        mov cx, [_g_sc_top]
        jcxz .copy
        mov bx, cx
        sub bx, [es:di + 2]             ; the logical page
        mov dx, [_g_sc_xh]
        mov ax, 4402h
        int 67h
        xor dx, dx
        or ah, ah
        jnz .mapped
        mov dx, [es:di + 2]
.mapped:
        mov [_g_sc_cur], dx
        jmp .out
.copy:
        mov si, [bp + 10]               ; i
        push word [es:di + 2]           ; [bp - 6]: the pointer's segment
        push word [es:di]               ; [bp - 8]: its offset
        mov word [es:di], _g_no_rows
        mov [es:di + 2], ds
        cmp word [_g_sc_xh], 0
        je .pack
        mov cx, [bp - 6]                ; XMS: the segment is F000h + the length in words
        sub cx, 0F000h
        jb .out                         ; (not a picture of the store)
        shl cx, 1
        push ax                         ; ([bp - 10], [bp - 12]: unused)
        push ax
        jmp short .sized
.pack:
        cmp si, [_g_sc_n]
        jae .out
        les bx, [_g_sc_off]             ; the pack's offsets: i, or i + 1 after the common ones
        mov ax, si
        cmp si, [_g_sc_common]
        cmc
        adc ax, 0
        shl ax, 1
        shl ax, 1
        add bx, ax
        mov cx, [es:bx + 4]
        sub cx, [es:bx]
        inc cx
        and cl, 0FEh                    ; CX = the length, even
        push word [es:bx + 2]           ; [bp - 10]: its place in the pack
        push word [es:bx]               ; [bp - 12]
.sized:
        mov ax, [_g_sc_head]
        add ax, cx
        add ax, 8
        cmp ax, [_g_sc_size]
        jbe .room
        ; Full: every picture in it back to the table's pointer it had.
        les bx, [bp + 6]
        mov ax, si
        shl ax, 1
        shl ax, 1
        sub bx, ax                      ; ES:BX = the table
        push ds
        mov dx, [_g_sc_head]
        lds si, [_g_sc_buf]
        add dx, si
.flush:
        cmp si, dx
        jae .flushed
        mov di, [si]
        shl di, 1
        shl di, 1
        mov ax, [si + 4]
        mov [es:bx + di], ax
        mov ax, [si + 6]
        mov [es:bx + di + 2], ax
        mov ax, [si + 2]
        add si, 8
        add si, ax
        jmp .flush
.flushed:
        pop ds
        mov word [_g_sc_head], 0
.room:
        les bx, [_g_sc_buf]
        add bx, [_g_sc_head]            ; ES:BX = the new entry
        cmp word [_g_sc_xh], 0
        je .disk
        push es                         ; XMS move (function 0Bh): to ES:BX + 8,
        push bx
        push cx
        push es
        lea ax, [bx + 8]
        push ax
        xor ax, ax
        push ax                         ; handle 0,
        mov ax, [bp - 8]                ; from the store at the paragraph * 16,
        mov dx, ax
        mov cl, 4
        shl ax, cl
        mov cl, 12
        shr dx, cl
        push dx
        push ax
        push word [_g_sc_xh]
        xor ax, ax
        push ax                         ; CX bytes
        mov cx, [bp - 18]
        push cx
        mov si, sp
        push ds
        push ss
        pop ds
        mov ah, 0Bh
        call far [cs:entry]
        pop ds
        add sp, 16
        pop cx
        pop bx
        pop es
        cmp ax, 1
        jne .out
        jmp short .got
.disk:
        push bx
        push cx
        mov dx, [_g_sc_pos]
        mov cx, [_g_sc_pos + 2]
        add dx, [bp - 12]
        adc cx, [bp - 10]
        mov bx, [_g_sc_fd]
        mov ax, 4200h
        int 21h
        pop cx
        pop dx
        jc .out
        push dx
        push ds
        push es
        pop ds
        add dx, 8
        mov ah, 3Fh
        int 21h                         ; (BX = the handle still)
        pop ds
        pop bx
        jc .out
        inc ax                          ; the last byte may lie past the pack's end
        cmp ax, cx
        jb .out
.got:
        mov ax, [bp + 10]
        mov [es:bx], ax
        mov [es:bx + 2], cx
        mov ax, [bp - 8]
        mov [es:bx + 4], ax
        mov ax, [bp - 6]
        mov [es:bx + 6], ax
        add cx, 8
        add [_g_sc_head], cx
        add bx, 8
        mov dx, es
        les di, [bp + 6]
        mov [es:di], bx
        mov [es:di + 2], dx
.out:
        lea sp, [bp - 4]
        pop di
        pop si
        pop bp
        retf

; void __cdecl xms_call(unsigned __far *r): r[0] AX, r[1] BX, r[2] DX,
; r[3] SI, r[4] DS in; AX, BX, DX back in r[0..2]
        global _xms_call
_xms_call:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es
        les di, [bp + 6]
        cmp word [cs:entry + 2], 0
        je .fail
        mov ax, [es:di]
        mov bx, [es:di + 2]
        mov dx, [es:di + 4]
        mov si, [es:di + 6]
        mov ds, [es:di + 8]
        push es
        push di
        call far [cs:entry]
        pop di
        pop es
        mov [es:di], ax
        mov [es:di + 2], bx
        mov [es:di + 4], dx
        jmp .done
.fail:  mov word [es:di], 0
        mov word [es:di + 2], 80h       ; "function not implemented"
.done:  pop es
        pop ds
        pop di
        pop si
        pop bp
        retf
