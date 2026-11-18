; SPDX-License-Identifier: GPL-3.0-or-later
; Hercules: the CGA picture in the card's second page (B800) shown in the
; first (B000, 720x348 graphics), byte for byte: a CGA byte (4 pixels of 2
; bits) is a Hercules byte of 8 pixels, so each game pixel shows as its
; own 2-pixel pattern (00 black, 01 and 10 half grey, 11 white). Game row
; y is Hercules line y + 74 (640x200, centred: 74 lines down, 5 bytes in).
; Lives in the hgcinit overlay section with src/hgcinit.c, which copies
; the code between blob_start and blob_end into a heap block once
; (hgc_blob); the resident stubs in src/video.asm call it there: they
; never load an overlay. The copied code has no relocations: data at
; fixed offsets of its own block (CS), jumps relative.
;
; Block layout (paragraph aligned, offset 0; src/hgcinit.c fills the data):
;   0000  LO[200]  first CGA byte of each game row marked (FFh = none)
;   00C8  HI[200]  byte after the last
;   0190  PMIN, PMAX: rows marked lie in [PMIN, PMAX); SCANP: the next row
;         the round-robin compare looks at; RSEG: a copy's source segment
;   01A0  ROWOFF[200]  each game row's offset in the CGA picture (from B000)
;   0330  LINE[200]    its Hercules line's offset in page 0, 5 bytes in
;   04C0  the code
; Video: ES = B000 throughout: B000:8000 = B800:0000 (the CGA picture,
; (y & 1) * 2000h + (y >> 1) * 80), B000:0000 page 0 (line L at (L & 3) *
; 2000h + (L >> 2) * 90). Large model, Watcom __cdecl.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

LO      equ 0000h
HI      equ 00C8h
PMIN    equ 0190h
PMAX    equ 0192h
SCANP   equ 0194h
RSEG    equ 0196h
ROWOFF  equ 01A0h
LINE    equ 0330h
CODE_OFF equ 04C0h

segment HGCASM_TEXT public class=CODE

; unsigned __cdecl hgc_blob(unsigned seg): seg 0 returns the code's size;
; else copies it to seg:CODE_OFF.
        global _hgc_blob
_hgc_blob:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov cx, blob_end - blob_start
        mov ax, [bp+6]
        or ax, ax
        jz .size
        mov es, ax
        call .here
.here:  pop si
        sub si, .here - blob_start
        mov ax, cs
        mov ds, ax
        mov di, CODE_OFF
        cld
        rep movsb
.size:  mov ax, blob_end - blob_start
        pop ds
        pop di
        pop si
        pop bp
        retf

; ------------------------------------------------------------------
; The copied code. Entry at its start (block offset CODE_OFF):
; void __far __cdecl hgc_op(int op, int a, int b, int c, int d)
;   op 0 MARK x0 y0 x1 y1, 1 SHOW (MARK, then FLUSH), 2 FLUSH a: the rows
;   marked copied, then a rows compared (src/gfx.h), 3 SHOW what the
;   intro frame at b:a wrote (src/cut.c), 4 the battle view's present: rows
;   b & 0FFh .. + (b >> 8), CGA bytes [c, c + d), copied from the back
;   buffer at segment a (src/gfx.c; faster than reading B800 back), 5
;   colour 2 made 3 in c bytes of a CGA picture at b:a (src/geo.c).
blob_start:
        push bp
        mov bp, sp
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        cld
        mov ax, cs
        mov ds, ax
        mov ax, 0B000h
        mov es, ax
        mov ax, [bp+6]
        cmp ax, 2
        je .flush
        cmp ax, 3
        je .run
        cmp ax, 4
        je .buf
        cmp ax, 5
        je .white
        call mark
        cmp word [bp+6], 1
        jne .out
        call flush
        jmp short .out
.run:   call markruns
        call flush
        jmp short .out
.buf:   call showbuf
        call flush
        jmp short .out
.flush: call flush
        mov cx, [bp+8]
        jcxz .out
        call scan
        jmp short .out
.white: les di, [bp+8]          ; colour 2 -> 3 in c bytes at b:a
        mov cx, [bp+12]
        jcxz .out
.wl:    mov al, [es:di]
        mov ah, al
        and ah, 0AAh
        shr ah, 1
        or al, ah
        stosb
        loop .wl
.out:   pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop bp
        retf

; ax (signed) clamped to 0..si.
clamp:  or ax, ax
        jns .pos
        xor ax, ax
.pos:   cmp ax, si
        jbe .ok
        mov ax, si
.ok:    ret

; Marks rows [y0, y1), CGA bytes [x0 >> 2, (x1 + 3) >> 2): [bp+8..14].
mark:   mov si, 320
        mov ax, [bp+8]
        call clamp
        shr ax, 1
        shr ax, 1
        mov dl, al
        mov ax, [bp+12]
        call clamp
        add ax, 3
        shr ax, 1
        shr ax, 1
        mov dh, al
        cmp dh, dl
        jbe .r
        mov si, 200
        mov ax, [bp+14]
        call clamp
        mov cx, ax
        mov ax, [bp+10]
        call clamp
        cmp ax, cx
        jae .r
        cmp ax, [PMIN]
        jae .a
        mov [PMIN], ax
.a:     cmp cx, [PMAX]
        jbe .b
        mov [PMAX], cx
.b:     mov bx, ax
        sub cx, ax
.l:     cmp dl, [LO+bx]
        jae .c
        mov [LO+bx], dl
.c:     cmp dh, [HI+bx]
        jbe .d
        mov [HI+bx], dh
.d:     inc bx
        loop .l
.r:     ret

; Marks what an intro frame's runs wrote (src/cutasm.asm cut_runs, CGA:
; u16 runs, then runs of u8 skip, u8 n, n bytes): [bp+8] offset, [bp+10]
; segment of the frame.
markruns:
        mov si, [bp+8]
        mov es, [bp+10]
        es lodsw
        mov cx, ax
        xor di, di
        jcxz .end
.r:     push cx
        xor ax, ax
        es lodsb                ; skip
        add di, ax
        es lodsb                ; bytes
        push ax
        mov cx, ax
        mov ax, di
        call markoff
        pop ax
        add si, ax
        add di, ax
        pop cx
        loop .r
.end:   mov ax, 0B000h
        mov es, ax
        ret

; Marks the bytes [ax, ax + cx) of the CGA picture (offsets in either line
; bank). Keeps si, di.
markoff:
        jcxz .r
        xor bx, bx
        test ah, 20h
        jz .b0
        and ax, 1FFFh
        inc bx                  ; odd rows
.b0:    xor dx, dx
        push cx
        mov cx, 80
        div cx                  ; ax = row in the bank, dx = byte
        pop cx
        shl ax, 1
        add bx, ax              ; game row
.seg:   cmp bx, 200
        jae .r
        mov ax, 80
        sub ax, dx
        cmp ax, cx
        jbe .m
        mov ax, cx              ; al = bytes in this row
.m:     mov ah, dl
        add ah, al
        cmp dl, [LO+bx]
        jae .x1
        mov [LO+bx], dl
.x1:    cmp ah, [HI+bx]
        jbe .x2
        mov [HI+bx], ah
.x2:    cmp bx, [PMIN]
        jae .x3
        mov [PMIN], bx
.x3:    inc bx
        cmp bx, [PMAX]
        jbe .x4
        mov [PMAX], bx
.x4:    inc bx                  ; the next row of the bank
        xor ah, ah
        xor dx, dx
        sub cx, ax
        jnz .seg
.r:     ret

; Op 4: the rectangle from the back buffer; marks inside it are dropped.
showbuf:
        mov ax, [bp+10]
        xor bx, bx
        mov bl, al              ; first row
        xor cx, cx
        mov cl, ah              ; rows
        mov dl, [bp+12]
        mov dh, dl
        add dh, [bp+14]
        jcxz .r
.l:     cmp [LO+bx], dl
        jb .keep
        cmp [HI+bx], dh
        ja .keep
        mov byte [LO+bx], 0FFh
        mov byte [HI+bx], 0
.keep:  push bx
        push cx
        push dx
        mov ax, bx
        call conv_buf
        pop dx
        pop cx
        pop bx
        inc bx
        loop .l
.r:     ret

; Converts every row marked.
flush:  mov ax, [PMIN]
.l:     cmp ax, [PMAX]
        jae .end
        mov bx, ax
        mov dl, [LO+bx]
        mov dh, [HI+bx]
        cmp dh, dl
        jbe .n
        mov byte [LO+bx], 0FFh
        mov byte [HI+bx], 0
        push ax
        call conv_row
        pop ax
.n:     inc ax
        jmp short .l
.end:   mov word [PMIN], 200
        mov word [PMAX], 0
        ret

; Compares cx rows from SCANP on with page 0; copies those that differ
; (writes nobody marked).
scan:
.next:  push cx
        mov bx, [SCANP]
        shl bx, 1
        mov si, [ROWOFF+bx]
        mov di, [LINE+bx]
        push ds
        mov cx, es
        mov ds, cx
        mov cx, 40
        repe cmpsw
        pop ds
        je .same
        mov ax, [SCANP]
        mov dx, 5000h           ; bytes 0 .. 80
        call conv_row
.same:  mov ax, [SCANP]
        inc ax
        cmp ax, 200
        jb .w
        xor ax, ax
.w:     mov [SCANP], ax
        pop cx
        loop .next
        ret

; Game row ax, CGA bytes [dl, dh): copied from the CGA picture to its line.
conv_row:
        mov [RSEG], es
        mov bx, ax
        shl bx, 1
        mov si, [ROWOFF+bx]
        jmp short copy_row

; The same from the battle view's back buffer: row ax at [bp+8]:ax * 80
; (src/gfx.c gfx_present_cells, BUFC_W 80).
conv_buf:
        mov cx, [bp+8]
        mov [RSEG], cx
        mov bx, ax
        shl bx, 1
        mov si, ax
        mov cl, 4
        shl si, cl
        mov cx, si
        shl si, 1
        shl si, 1
        add si, cx
copy_row:                       ; bx = 2 * row, si = its start in [RSEG]
        xor cx, cx
        mov cl, dh
        sub cl, dl
        xor dh, dh
        add si, dx
        mov di, [LINE+bx]
        add di, dx
        push ds
        mov ds, [RSEG]
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
        pop ds
        ret
blob_end:
