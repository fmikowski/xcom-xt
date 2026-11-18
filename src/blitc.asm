; SPDX-License-Identifier: GPL-3.0-or-later
; CGA (mode 4) back buffer and sprite blitters. Large model, Watcom __cdecl.
;
; The back buffer is packed 2 bits per pixel, BUFC_W bytes per row with
; BUFC_LEFT bytes of margin, BUF_TOP rows of margin above the window.
; Sprite formats: tools/cgasprite.py.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

BUFC_W    equ 80
BUFC_LEFT equ 0
BUF_TOP   equ 0
VIEWC_W   equ 80                        ; visible bytes per row

segment BLITC_TEXT public class=CODE

v_rows  db 0
v_wmask db 0fh

present_tab db 0,1,0,1, 1,2,1,2, 0,1,0,1, 1,2,1,2
masked_tab  db 0,1,0,0, 1,2,1,1, 0,1,0,0, 0,1,0,0

%macro CWORD 0
        shr cl, 1
        jc %%present
        shr cl, 1
        inc di
        inc di
        jmp short %%next
%%present:
        shr cl, 1
        jc %%opaque
        lodsw
        mov dx, [es:di]
        and dx, [bx]
        or ax, dx
        stosw
        inc bx
        inc bx
        jmp short %%next
%%opaque:
        movsw
%%next:
%endmacro

%macro CWORDCLIP 1
        shr cl, 1
        jc %%present
        shr cl, 1
        jmp short %%skip_di
%%present:
        test byte [cs:v_wmask], %1
        jz %%hidden
        shr cl, 1
        jc %%opaque
        lodsw
        mov dx, [es:di]
        and dx, [bx]
        or ax, dx
        stosw
        inc bx
        inc bx
        jmp short %%next
%%opaque:
        movsw
        jmp short %%next
%%hidden:
        inc si
        inc si
        shr cl, 1
        jc %%skip_di
        inc bx
        inc bx
%%skip_di:
        inc di
        inc di
%%next:
%endmacro

; void __cdecl blitc_masked(const void far *spr, unsigned bufseg, unsigned dest)
        global _blitc_masked
_blitc_masked:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov es, [bp+10]
        mov di, [bp+12]
        lds si, [bp+6]
        cld
        lodsw                           ; al = first row, ah = row count
        test ah, ah
        jnz .go
        jmp .done
.go:
        mov ch, ah
        mov cl, ah
        mov ah, BUFC_W
        mul ah
        add di, ax
        lodsw                           ; mask bytes
        mov dx, ax
        lodsw                           ; data bytes (unused)
        mov bp, si                      ; ops
        mov bl, cl
        xor bh, bh
        add si, bx
        mov bx, si                      ; masks
        add si, dx                      ; data
.row:
        mov cl, [ds:bp]
        inc bp
        CWORD
        CWORD
        CWORD
        CWORD
        add di, BUFC_W - 8
        dec ch
        jz .done
        jmp .row
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl blitc_masked_clip(const void far *spr, unsigned bufseg,
;     unsigned dest, unsigned clip_top, unsigned clip_bottom, unsigned wmask)
        global _blitc_masked_clip
_blitc_masked_clip:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov es, [bp+10]
        mov al, [bp+18]
        mov [cs:v_wmask], al
        lds si, [bp+6]
        cld
        lodsw                           ; al = first row, ah = row count
        mov bl, al
        mov bh, al
        add bh, ah                      ; bh = end row
        mov cl, [bp+14]
        cmp cl, bl
        jae .t
        mov cl, bl
.t:     mov ch, [bp+16]
        cmp ch, bh
        jbe .b
        mov ch, bh
.b:     cmp cl, ch
        jb .go
        jmp .done
.go:
        sub ch, cl
        mov [cs:v_rows], ch
        mov al, cl
        mov ah, BUFC_W
        mul ah
        add ax, [bp+12]
        mov di, ax                      ; first visible row
        sub cl, bl                      ; rows skipped
        lodsw
        mov dx, ax                      ; mask bytes
        lodsw                           ; data bytes
        mov al, bh
        sub al, bl                      ; stored rows
        xor ah, ah
        mov bp, si                      ; ops
        add ax, si
        mov bx, ax                      ; masks
        add ax, dx
        mov si, ax                      ; data
        xor ch, ch
        jcxz .noskip
.scan:
        ; advance masks/data past one skipped row (two nibbles of ops)
        push cx
        mov al, [ds:bp]
        inc bp
        mov cl, al
        and al, 0Fh
        mov ah, al
        push bx
        mov bx, present_tab
        cs xlatb
        mov dl, al
        mov al, ah
        mov bx, masked_tab
        cs xlatb
        mov dh, al
        mov al, cl
        mov cl, 4
        shr al, cl
        mov ah, al
        mov bx, present_tab
        cs xlatb
        add dl, al
        mov al, ah
        mov bx, masked_tab
        cs xlatb
        add dh, al
        pop bx
        mov al, dl
        xor ah, ah
        shl ax, 1
        add si, ax
        mov al, dh
        xor ah, ah
        shl ax, 1
        add bx, ax
        pop cx
        loop .scan
.noskip:
        mov ch, [cs:v_rows]
.row:
        mov cl, [ds:bp]
        inc bp
        CWORDCLIP 1
        CWORDCLIP 2
        CWORDCLIP 4
        CWORDCLIP 8
        add di, BUFC_W - 8
        dec ch
        jz .done
        jmp .row
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl blitc_or(const void far *spr, unsigned bufseg, unsigned dest)
        global _blitc_or
_blitc_or:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov es, [bp+10]
        mov di, [bp+12]
        lds si, [bp+6]
        cld
        lodsw
        test ah, ah
        jz .done
        mov cl, ah
        xor ch, ch
        mov ah, BUFC_W
        mul ah
        add di, ax
.row:
        lodsw
        or ax, [es:di]
        stosw
        lodsw
        or ax, [es:di]
        stosw
        lodsw
        or ax, [es:di]
        stosw
        lodsw
        or ax, [es:di]
        stosw
        add di, BUFC_W - 8
        loop .row
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl blitc_or_clip(const void far *spr, unsigned bufseg,
;     unsigned dest, unsigned clip_top, unsigned clip_bottom, unsigned wmask)
        global _blitc_or_clip
_blitc_or_clip:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov es, [bp+10]
        mov dh, [bp+18]
        lds si, [bp+6]
        cld
        lodsw
        mov bl, al
        mov bh, al
        add bh, ah
        mov cl, [bp+14]
        cmp cl, bl
        jae .t
        mov cl, bl
.t:     mov ch, [bp+16]
        cmp ch, bh
        jbe .b
        mov ch, bh
.b:     cmp cl, ch
        jb .go
        jmp .done
.go:
        sub ch, cl
        mov al, cl
        sub al, bl
        xor ah, ah
        shl ax, 1
        shl ax, 1
        shl ax, 1
        add si, ax                      ; skip hidden rows (8 bytes each)
        mov al, cl
        mov ah, BUFC_W
        mul ah
        add ax, [bp+12]
        mov di, ax
        mov cl, ch
        xor ch, ch
.row:
        mov dl, dh
        mov bl, 4
.word:
        shr dl, 1
        jnc .hide
        lodsw
        or ax, [es:di]
        stosw
        jmp short .wnext
.hide:
        inc si
        inc si
        inc di
        inc di
.wnext:
        dec bl
        jnz .word
        add di, BUFC_W - 8
        loop .row
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl bufc_clear_rect(unsigned bufseg, unsigned col, unsigned row,
;                              unsigned cols, unsigned rows)   (cols even)
        global _bufc_clear_rect
_bufc_clear_rect:
        push bp
        mov bp, sp
        push di
        mov es, [bp+6]
        mov ax, [bp+10]
        add ax, BUF_TOP
        mov dx, BUFC_W
        mul dx
        add ax, [bp+8]
        add ax, BUFC_LEFT
        mov di, ax
        mov dx, BUFC_W
        sub dx, [bp+12]
        mov bx, [bp+14]
        cld
        xor ax, ax
.row:
        mov cx, [bp+12]
        shr cx, 1
        rep stosw
        add di, dx
        dec bx
        jnz .row
        pop di
        pop bp
        retf

; void __cdecl bufc_present_rect(unsigned bufseg, unsigned col, unsigned row,
;                                unsigned cols, unsigned rows)
; Copies to CGA memory at B800 (even rows at 0, odd rows at 2000h). The
; first row's address is worked out once, the rest step from bank to bank.
; VGA overwrites its first 5 bytes (src\vgainit.c).
%if BUF_TOP != 0 || BUFC_LEFT != 0 || BUFC_W != 80
%error bufc_present_rect: a buffer row is a screen line
%endif
        global _bufc_present_rect
_bufc_present_rect:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp+10]
        mov dx, BUFC_W
        mul dx
        mov di, ax
        shr di, 1                       ; (row/2) * 80, + 40 on an odd row
        test byte [bp+10], 1
        jz .even
        add di, 2000h - 40
.even:
        add ax, [bp+8]
        mov si, ax
        add di, [bp+8]
        mov ax, 0B800h
        mov es, ax
        mov bx, [bp+12]                 ; bytes a row
        mov dx, BUFC_W
        sub dx, bx                      ; row skip in the buffer
        mov ax, [bp+14]                 ; rows left
        mov ds, [bp+6]
        mov bp, 2000h
        sub bp, bx                      ; to the next row: the other bank
        cld
.row:
        mov cx, bx
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
        add si, dx
        add di, bp
        cmp di, 4000h
        jb .next
        sub di, 4000h - 80              ; odd bank to even: a line down
.next:
        dec ax
        jnz .row
        pop ds
        pop di
        pop si
        pop bp
        retf
