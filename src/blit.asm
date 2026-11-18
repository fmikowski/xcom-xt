; SPDX-License-Identifier: GPL-3.0-or-later
; Planar EGA back buffer and sprite blitters. Large model, Watcom __cdecl.
;
; The back buffer holds 4 bit planes of BUF_ROWS x BUF_W bytes each. Its
; visible window starts BUF_TOP rows down and BUF_LEFT bytes in; the margins
; let 32x40 sprites hang off any edge without clipping.
;
; Sprites are drawn one bit plane at a time in 16-pixel words; see
; tools/egasprite.py for the masked and OR formats.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

BUF_W     equ 40
BUF_TOP   equ 0
BUF_LEFT  equ 0
BUF_ROWS  equ 144
PLANE     equ BUF_W * BUF_ROWS          ; 10752
VIEW_W    equ 40                        ; visible bytes per row

segment BLIT_TEXT public class=CODE

; Masked blit state shared by the four plane passes (not reentrant).
v_di0   dw 0
v_ops   dw 0
v_mask  dw 0
v_rows  db 0

; One 16-pixel word. CL holds the row's remaining op bits, DS:SI the plane
; data, DS:BX the masks, ES:DI the destination.
%macro WORDCOL 0
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

; Row-run handlers: one loop per pattern of the two 16-pixel words
; (0 absent, 1 masked, 2 opaque). CX = rows in the run, DS:SI plane data,
; DS:BX masks, ES:DI destination.
%macro WMASKED 0
        lodsw
        mov dx, [es:di]
        and dx, [bx]
        or ax, dx
        stosw
        inc bx
        inc bx
%endmacro

%macro WKIND 1
%if %1 == 0
        inc di
        inc di
%elif %1 == 1
        WMASKED
%else
        movsw
%endif
%endmacro

%macro RUNLOOP 2
.h%1%2:
        WKIND %1
        WKIND %2
        add di, BUF_W - 4
        loop .h%1%2
        jmp .next_run
%endmacro

v_runs   dw 0
v_nruns  db 0
v_rleft  db 0
v_plane  db 0

; void __cdecl blit_masked(const void far *spr, unsigned bufseg, unsigned dest)
        global _blit_masked
_blit_masked:
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
        mov cl, ah
        mov ah, BUF_W
        mul ah
        add di, ax
        mov [cs:v_di0], di
        lodsw                           ; mask bytes
        mov dx, ax
        lodsw                           ; plane bytes (unused here)
        lodsb                           ; run count
        mov [cs:v_nruns], al
        xor ch, ch
        add si, cx                      ; skip the per-row ops
        mov [cs:v_runs], si
        xor ah, ah
        shl ax, 1
        add si, ax
        mov [cs:v_mask], si
        add si, dx                      ; plane 0 data
        mov byte [cs:v_plane], 4
.pass:
        mov di, [cs:v_di0]
        mov bx, [cs:v_mask]
        mov bp, [cs:v_runs]
        mov al, [cs:v_nruns]
        mov [cs:v_rleft], al
.run:
        mov cl, [ds:bp]
        xor ch, ch
        mov dl, [ds:bp+1]
        add bp, 2
        xor dh, dh
        shl dx, 1
        push bx
        mov bx, dx
        mov dx, [cs:run_table+bx]
        pop bx
        jmp dx
.next_run:
        dec byte [cs:v_rleft]
        jnz .run
        add word [cs:v_di0], PLANE
        dec byte [cs:v_plane]
        jnz .pass
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

        RUNLOOP 0, 0
        RUNLOOP 0, 1
        RUNLOOP 0, 2
        RUNLOOP 1, 0
        RUNLOOP 1, 1
        RUNLOOP 1, 2
        RUNLOOP 2, 0
        RUNLOOP 2, 1
        RUNLOOP 2, 2

run_table dw _blit_masked.h00, _blit_masked.h01, _blit_masked.h02
          dw _blit_masked.h10, _blit_masked.h11, _blit_masked.h12
          dw _blit_masked.h20, _blit_masked.h21, _blit_masked.h22
; void __cdecl blit_or(const void far *spr, unsigned bufseg, unsigned dest)
; ORs a full-width sprite into the buffer (ground floors on cleared areas).
        global _blit_or
_blit_or:
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
        jz .done
        mov bl, ah
        mov ah, BUF_W
        mul ah
        add di, ax
        mov dx, di
        mov bh, 4
.plane:
        mov di, dx
        mov cl, bl
        xor ch, ch
.row:
        lodsw
        or ax, [es:di]
        stosw
        lodsw
        or ax, [es:di]
        stosw
        add di, BUF_W - 4
        loop .row
        add dx, PLANE
        dec bh
        jnz .plane
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf
; ---------------------------------------------------------------------------
; Clipped variants. clip_top/clip_bottom are rows of the 40-row sprite box
; (bottom exclusive); wmask bit 0/1 = word 0/1 (pixels 0-15/16-31) visible.

v_data  dw 0
v_plen  dw 0
v_wmask db 3

; Per op nibble: present words and masked (present, not opaque) words.
present_tab db 0,1,0,1, 1,2,1,2, 0,1,0,1, 1,2,1,2
masked_tab  db 0,1,0,0, 1,2,1,1, 0,1,0,0, 0,1,0,0

; Like WORDCOL, but words outside the clip only advance the streams.
%macro WORDCLIP 1
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

%macro CLIPPASS 1
        mov di, [cs:v_di0]
%if %1
        add di, PLANE * %1
        mov ax, [cs:v_plen]
        add [cs:v_data], ax
%endif
        mov si, [cs:v_data]
        mov bp, [cs:v_ops]
        mov bx, [cs:v_mask]
        mov ch, [cs:v_rows]
%%row:
        mov cl, [ds:bp]
        inc bp
        WORDCLIP 1
        WORDCLIP 2
        add di, BUF_W - 4
        dec ch
        jz %%end
        jmp %%row
%%end:
%endmacro

; Computes the visible row span of a sprite: on entry AL = first row,
; AH = row count, [bp+14] = clip top, [bp+16] = clip bottom. On exit
; CL = first visible row, CH = visible count, BL = first, BH = first+count;
; jumps to .done when nothing is visible.
%macro ROW_SPAN 0
        mov bl, al
        mov bh, al
        add bh, ah
        mov cl, [bp+14]
        cmp cl, bl
        jae %%t
        mov cl, bl
%%t:
        mov ch, [bp+16]
        cmp ch, bh
        jbe %%b
        mov ch, bh
%%b:
        cmp cl, ch
        jb %%go
        jmp .done
%%go:
        sub ch, cl
%endmacro

; void __cdecl blit_masked_clip(const void far *spr, unsigned bufseg,
;     unsigned dest, unsigned clip_top, unsigned clip_bottom, unsigned wmask)
        global _blit_masked_clip
_blit_masked_clip:
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
        ROW_SPAN
        mov [cs:v_rows], ch
        mov al, cl
        mov ah, BUF_W
        mul ah
        add ax, [bp+12]
        mov [cs:v_di0], ax
        sub cl, bl                      ; rows skipped at the top
        lodsw
        mov dx, ax                      ; mask bytes
        lodsw
        mov [cs:v_plen], ax
        lodsb                           ; run count
        xor ah, ah
        shl ax, 1
        mov di, ax                      ; run bytes
        mov al, bh
        sub al, bl                      ; stored rows
        xor ah, ah
        mov bx, si                      ; ops
        add ax, si
        add ax, di                      ; skip the runs
        mov [cs:v_mask], ax
        add ax, dx
        mov [cs:v_data], ax
        xor dx, dx                      ; dl = present, dh = masked words skipped
        xor ch, ch
        jcxz .noskip
.scan:
        mov al, [bx]
        inc bx
        and al, 0Fh
        push bx
        mov ah, al
        mov bx, present_tab
        cs xlatb
        add dl, al
        mov al, ah
        mov bx, masked_tab
        cs xlatb
        add dh, al
        pop bx
        loop .scan
.noskip:
        mov [cs:v_ops], bx
        mov al, dh
        xor ah, ah
        shl ax, 1
        add [cs:v_mask], ax
        mov al, dl
        xor ah, ah
        shl ax, 1
        add [cs:v_data], ax
        CLIPPASS 0
        CLIPPASS 1
        CLIPPASS 2
        CLIPPASS 3
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl blit_or_clip(const void far *spr, unsigned bufseg,
;     unsigned dest, unsigned clip_top, unsigned clip_bottom, unsigned wmask)
        global _blit_or_clip
_blit_or_clip:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov es, [bp+10]
        mov dh, [bp+18]                 ; word mask
        lds si, [bp+6]
        cld
        lodsw                           ; al = first row, ah = row count
        mov dl, ah
        ROW_SPAN
        mov al, dl
        xor ah, ah
        shl ax, 1
        shl ax, 1
        mov [cs:v_plen], ax             ; bytes per plane
        mov al, cl
        sub al, bl
        xor ah, ah
        shl ax, 1
        shl ax, 1
        add si, ax                      ; skip hidden top rows
        mov al, cl
        mov ah, BUF_W
        mul ah
        add ax, [bp+12]
        mov [cs:v_di0], ax
        mov bp, si
        mov bl, ch                      ; visible rows
        mov dl, 4
.oplane:
        mov si, bp
        mov di, [cs:v_di0]
        mov cl, bl
        xor ch, ch
.orow:
        test dh, 1
        jz .oh0
        lodsw
        or ax, [es:di]
        stosw
        jmp short .ow1
.oh0:
        inc si
        inc si
        inc di
        inc di
.ow1:
        test dh, 2
        jz .oh1
        lodsw
        or ax, [es:di]
        stosw
        jmp short .oe
.oh1:
        inc si
        inc si
        inc di
        inc di
.oe:
        add di, BUF_W - 4
        loop .orow
        add bp, [cs:v_plen]
        add word [cs:v_di0], PLANE
        dec dl
        jnz .oplane
.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl buf_shift(unsigned bufseg, unsigned plane_bytes, unsigned planes,
;                        int delta)
; Moves every plane's bytes by delta (even; + = towards the end) in one
; word copy: the view is exactly as wide as the buffer, so the bytes that
; wrap round a row edge land in the exposed strip, which is redrawn.
; EGA (4 planes) and CGA (1).
        global _buf_shift
_buf_shift:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp+6]
        mov ds, ax
        mov es, ax
        mov dx, [bp+12]                 ; delta
        mov bx, [bp+8]                  ; plane bytes
        xor si, si                      ; plane base
.splane:
        mov cx, dx
        test cx, cx
        js .sneg
        jz .snext
        neg cx
        add cx, bx                      ; bytes to move
        shr cx, 1
        mov di, si
        add di, bx
        sub di, 2                       ; last word
        push si
        mov si, di
        sub si, dx
        std
        rep movsw
        cld
        pop si
        jmp short .snext
.sneg:
        add cx, bx                      ; plane bytes - |delta|
        shr cx, 1
        mov di, si
        push si
        sub si, dx                      ; + |delta|
        cld
        rep movsw
        pop si
.snext:
        add si, bx
        dec word [bp+10]
        jnz .splane
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl vram_shift(unsigned bytes, int delta)
; The same move on EGA page 0's first bytes (the map view), all four
; planes at once through the latches (write mode 1): a quarter of the bus
; traffic of presenting the moved part again.
        global _vram_shift
_vram_shift:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov dx, 3C4h
        mov ax, 0F02h                   ; map mask: all planes
        out dx, ax
        mov dx, 3CEh
        mov ax, 0105h                   ; write mode 1
        out dx, ax
        mov ax, 0A000h
        mov ds, ax
        mov es, ax
        mov dx, [bp+8]                  ; delta
        mov cx, [bp+6]
        mov ax, dx
        test ax, ax
        jns .vpos
        neg ax
.vpos:
        sub cx, ax                      ; bytes to move
        test dx, dx
        js .vneg
        mov di, [bp+6]
        dec di                          ; last byte
        mov si, di
        sub si, dx
        std
        rep movsb
        cld
        jmp short .vdone
.vneg:
        xor di, di
        mov si, dx
        neg si
        cld
        rep movsb
.vdone:
        mov dx, 3CEh
        mov ax, 0005h                   ; write mode 0
        out dx, ax
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl buf_clear(unsigned bufseg)
        global _buf_clear
_buf_clear:
        push bp
        mov bp, sp
        push di
        mov es, [bp+6]
        xor di, di
        xor ax, ax
        mov cx, PLANE * 2               ; 4 planes in words
        cld
        rep stosw
        pop di
        pop bp
        retf

; void __cdecl buf_clear_rect(unsigned bufseg, unsigned col, unsigned row,
;                             unsigned cols, unsigned rows)
; Clears a rectangle of the visible window (col/row relative to it).
        global _buf_clear_rect
_buf_clear_rect:
        push bp
        mov bp, sp
        push si
        push di
        mov es, [bp+6]
        mov ax, [bp+10]
        add ax, BUF_TOP
        mov dx, BUF_W
        mul dx
        add ax, [bp+8]
        add ax, BUF_LEFT
        mov di, ax
        mov dx, BUF_W
        sub dx, [bp+12]                 ; row skip
        mov si, [bp+12]
        shr si, 1                       ; words per row (cols are even)
        mov bh, 4                       ; planes
        cld
.cplane:
        push di
        mov bl, [bp+14]                 ; rows
        xor ax, ax
.crow:
        mov cx, si
        rep stosw
        add di, dx
        dec bl
        jnz .crow
        pop di
        add di, PLANE
        dec bh
        jnz .cplane
        pop di
        pop si
        pop bp
        retf

; void __cdecl vram_box(unsigned bufseg, unsigned off, unsigned bytes,
;                       unsigned rows, int out)
; EGA (src/rcursor.c, the cursor's box): copies a box of the back buffer
; (bytes x rows at off, 4 planes) to video memory at 1F40h of each plane,
; the 192 bytes between pages 0 and 1, or back (out = 0). A row goes as
; words (an odd byte last), the row skip stays in DX, the plane in AH.
        global _vram_box
_vram_box:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov bx, [bp+10]                 ; bytes a row
        mov dx, BUF_W
        sub dx, bx                      ; row skip in the buffer
        xor ax, ax                      ; plane 0
        cld
.vbp:
        push dx
        cmp word [bp+14], 0
        je .vbin
        push ax
        mov cl, ah
        mov ah, 1
        shl ah, cl
        mov al, 2                       ; map mask: this plane
        mov dx, 3C4h
        out dx, ax
        pop ax
        pop dx
        mov ds, [bp+6]
        mov si, [bp+8]
        mov cx, 0A000h
        mov es, cx
        mov di, 1F40h
        mov al, [bp+12]
.vbro:
        mov cx, bx
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
        add si, dx
        dec al
        jnz .vbro
        jmp short .vbnext
.vbin:
        mov al, 4                       ; read map select: this plane
        mov dx, 3CEh
        out dx, ax
        pop dx
        mov cx, 0A000h
        mov ds, cx
        mov si, 1F40h
        mov es, [bp+6]
        mov di, [bp+8]
        mov al, [bp+12]
.vbri:
        mov cx, bx
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
        add di, dx
        dec al
        jnz .vbri
.vbnext:
        add word [bp+8], PLANE          ; the next plane (the argument is ours)
        inc ah
        cmp ah, 4
        jb .vbp
        mov dx, 3C4h
        mov ax, 0F02h
        out dx, ax
        mov dx, 3CEh
        mov ax, 0004h
        out dx, ax
        pop ds
        pop di
        pop si
        pop bp
        retf

; void __cdecl buf_box(unsigned bufseg, unsigned off, unsigned stride,
;     unsigned bytes, unsigned rows, void far *save, int out)
; CGA: the same box copy between the back buffer and linear memory.
        global _buf_box
_buf_box:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov dx, [bp+10]
        sub dx, [bp+12]                 ; row skip
        mov bl, [bp+14]                 ; rows
        cld
        cmp word [bp+20], 0
        je .bin
        mov ds, [bp+6]
        mov si, [bp+8]
        les di, [bp+16]
.bro:
        mov cx, [bp+12]
        rep movsb
        add si, dx
        dec bl
        jnz .bro
        jmp short .bdone
.bin:
        mov es, [bp+6]
        mov di, [bp+8]
        lds si, [bp+16]
.bri:
        mov cx, [bp+12]
        rep movsb
        add di, dx
        dec bl
        jnz .bri
.bdone:
        pop ds
        pop di
        pop si
        pop bp
        retf


; void __cdecl buf_present_rect(unsigned bufseg, unsigned col, unsigned row,
;                               unsigned cols, unsigned rows)
; Copies a rectangle of the visible window to the same place on EGA page 0.
; A row goes as words (an odd byte last), the row skip stays in DX; a
; full-width rectangle is one string move a plane (its rows lie end to end).
%if BUF_W != VIEW_W || BUF_LEFT != 0 || BUF_TOP != 0
%error buf_present_rect: the buffer's offsets are the screen's
%endif
        global _buf_present_rect
_buf_present_rect:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp+10]                 ; row
        mov dx, VIEW_W
        mul dx
        add ax, [bp+8]
        mov di, ax                      ; VRAM offset
        mov si, ax                      ; buffer offset: the same
        mov ax, 0A000h
        mov es, ax
        mov dx, 3C4h
        mov al, 2                       ; sequencer: map mask
        out dx, al
        mov bx, [bp+12]                 ; bytes a row
        mov dx, BUF_W
        sub dx, bx                      ; row skip
        jnz .part
        mov al, [bp+14]                 ; full width: one row of rows x BUF_W
        mov ah, BUF_W
        mul ah
        xchg ax, bx
        mov byte [bp+14], 1             ; (the argument is ours)
.part:
        mov al, 1                       ; plane bit
        mov ds, [bp+6]
        cld
.plane:
        push dx
        mov dx, 3C5h
        out dx, al
        pop dx
        push si
        push di
        mov ah, [bp+14]                 ; rows
.prow:
        mov cx, bx
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
        add si, dx
        add di, dx
        dec ah
        jnz .prow
        pop di
        pop si
        add si, PLANE
        shl al, 1
        cmp al, 16
        jb .plane
        mov dx, 3C5h
        mov al, 0Fh
        out dx, al
        pop ds
        pop di
        pop si
        pop bp
        retf
