; SPDX-License-Identifier: GPL-3.0-or-later
; VGA outside the battle (docs/design.md, "VGA outside the battle"): the
; DOS palettes per screen, the geoscape's world map and its markers in
; mode 13h. Lives in the vgainit overlay section with src/vgageo.c, which
; copies the code between blob_start and blob_end into a small block of
; its own once (vgx_blob), as vgainit.c does with src/vgaasm.asm's.
; Nothing here is resident and nothing of the battle view changes.
;
; Block layout (paragraph aligned, offset 0, no relocations: data at
; fixed offsets of the block (CS), jumps relative):
;   0000  the entries, 4 bytes apart (far calls from the C side):
;   0 vx_pic   uk_pic_cga's hook (vgageo.c points uk_pic_cga's far call
;              here): a plain picture's palette tag (the head's spare
;              byte, tools/ui.py vga_tag) sets the DOS palette it was
;              drawn in, then on to vgaasm.asm's picture code (THEIRS)
;   1 vx_world (const unsigned char far *map, unsigned vx, unsigned vy,
;              unsigned zoom): the geoscape's 256 x 160 view from the 320 x
;              160 world map (a byte a pixel, WORLDV.PAK), wrapping round,
;              each pixel 1, 2 or 4 times (zoom 0, 1, 2)
;   2 vx_xor   (int x0, int y0, int x1, int y1, int colour): a marker box
;              (geo.c xor_box) in the EGA colour's palette index, the
;              pixels under it kept; the last one drawn, asked again,
;              is put back (erase_marks goes backwards). Colour bits 4-7
;              a DOS marker + 1 (OpenXcom's GlobeMarkers, 3 x 3, VX_MARKS):
;              drawn in the box's middle, a box over 3 outlined
;              (selected) in the EGA colour
;   3 vx_theme uk_theme's hook (AX theme): vgaasm.asm's (THEMT), then the
;              VGA theme of the palette (VX_TT) and its VMAP
;   4 vx_fill  uk_fill's hook (AX x, DX y, BX w, CX h; colour): a colour
;              16-255 is the palette's index (the VGA themes' frames and
;              buttons), else the EGA colour as before (FILLT)
;   5 vx_battle () (hwp.c hwp_notice, just before battle_run): the
;              screen black, the battlescape's palette set, so the
;              battle's first frames are not drawn in the briefing's, and
;              PAL is written before gfx_night (a night battle keeps the
;              lights' data in PAL: no palette changes in a battle)
;   6 vx_slide (const unsigned char far *dac, int caption) (cut.c, the
;              end slides): the DAC from dac's 768 bytes (NULL: black),
;              the caption's palette index on EGA colour 14 (its shadow,
;              index + 3, on 6); the next picture's palette is read again
;   (room for 8 entries) then the variables (VX_*) and the code.
;
; Palettes: PALV.PAK (tools/ui.py vga_palettes): "XVP1", 5, 8; per DOS
; palette (geoscape, basescape, graphs, UFOpaedia, battlescape) 768 bytes
; of DAC values and 16 bytes VMAP; then BACKPALS.DAT's 8 window ramps of
; 48 bytes. A tag's bits 0-2 are the palette + 1, bits 3-6 the ramp + 1
; (DAC 224-239), bit 7 the style (1: DOS's menus). The palette goes to
; vgaasm.asm's PAL (offset 0 of its block: what the DAC holds) and its
; VMAP (0820h), then to the DAC. Then (tools/ui.py vga_themes) for
; palettes 1-5 and styles 0 and 1: 5 themes of 31 bytes, a UkTheme (15)
; in DOS's colours and its VMAP (16); in a battle the battlescape
; palette keeps vgaasm.asm's themes (VX_INBAT).
; Large model, Watcom __cdecl; the 8086 instruction set.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

ROW     equ 320
VIEW_W  equ 256                 ; the geoscape's map view (geo.c)
WMAP_W  equ 320
WMAP_H  equ 160
BYTES   equ 320 * 144           ; the battle view's back buffer (gfx.h VGA_BYTES)
STACK0  equ BYTES - 2048        ; the markers' stack in it (vx_xor)
VMAP    equ 0820h               ; in vgaasm.asm's block (its layout there):
P_FD    equ 0834h               ;   &g_uk_fd
P_UK    equ 0838h               ;   &g_uk
P_BUFSEG equ 085Ch              ;   &g_bufseg
V_NCL   equ 0874h               ;   pictures in CLEAN
LAST    equ 0890h               ;   the last plain picture drawn (an entry)
CLEAN   equ 08A0h               ;   4 entries: the clean screen's pictures
E_POS   equ 0                   ;   an entry: the data's place in the pack,
E_X     equ 4                   ;   x, y, w, h, flags (16 bytes)
E_Y     equ 6
E_W     equ 8
E_H     equ 10
E_FL    equ 12
PALREC  equ 784                 ; a palette's record in PALV.PAK
RAMPS   equ 8 + 5 * PALREC      ; the window ramps
THEMES  equ RAMPS + 8 * 48      ; the VGA themes
TREC    equ 15 + 16             ; one: UkTheme, VMAP
TSET    equ 5 * TREC            ; a palette's and style's five

; Variables (offsets in the block).
VX_THEIRS equ 20h               ; far: vgaasm.asm's entry 9 (ui_pic)
VX_VSEG   equ 24h               ; vgaasm.asm's block
VX_CURP   equ 26h               ; the palette set (1-5) and style (80h), 0 none yet
VX_CURB   equ 27h               ; the window ramp set (1-8), 0 the palette's own
VX_ZOOM   equ 28h
VX_NEED   equ 2Ah               ; source pixels a row
VX_N1     equ 2Ch               ; ... of them before the date line
VX_NAME   equ 2Eh               ; "PALV.PAK", 0
VX_INBAT  equ 37h               ; 1 from vx_battle until another palette: the
                                ; battlescape palette keeps vgaasm.asm's themes
VX_SP     equ 38h               ; the markers' stack (vx_xor)
VX_W      equ 3Ah
VX_H      equ 3Ch
VX_THEMT  equ 3Eh               ; far: vgaasm.asm's entry 16 (ui_theme)
VX_FILLT  equ 42h               ; far: vgaasm.asm's entry 15 (ui_fill)
VX_THEME  equ 46h               ; the theme asked for last
VX_HAVE   equ 47h               ; VX_TT holds the palette's themes
VX_TT     equ 48h               ; the palette's and style's 5 VGA themes

%define L(x) ((x) - blob_start)

segment VGXASM_TEXT public class=CODE

; unsigned __cdecl vgx_blob(unsigned seg): seg 0 returns the block's size;
; else copies the code to seg:0.
        global _vgx_blob
_vgx_blob:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp+6]
        or ax, ax
        jz .size
        mov es, ax
        call .here
.here:  pop si
        sub si, .here - blob_start
        mov ax, cs
        mov ds, ax
        xor di, di
        mov cx, blob_end - blob_start
        cld
        rep movsb
.size:  mov ax, blob_end - blob_start
        pop ds
        pop di
        pop si
        pop bp
        retf

; ------------------------------------------------------------------
blob_start:
        jmp near vx_pic         ; 0
        nop
        jmp near vx_world       ; 1
        nop
        jmp near vx_xor         ; 2
        nop
        jmp near vx_theme       ; 3
        nop
        jmp near vx_fill        ; 4
        nop
        jmp near vx_battle      ; 5
        nop
        jmp near vx_slide       ; 6
        nop
        times VX_THEIRS - ($ - blob_start) db 0
        dd 0                    ; VX_THEIRS
        dw 0                    ; VX_VSEG (10h)
        db 0, 0                 ; VX_CURP, VX_CURB
        dw 0, 0, 0              ; VX_ZOOM, VX_NEED, VX_N1
        times VX_NAME - ($ - blob_start) db 0
        db "PALV.PAK", 0        ; VX_NAME
        db 0                    ; VX_INBAT
        times VX_SP - ($ - blob_start) db 0
        dw STACK0, 0, 0         ; VX_SP, VX_W, VX_H
        dd 0, 0                 ; VX_THEMT, VX_FILLT
        db 0, 0                 ; VX_THEME, VX_HAVE
        times TSET db 0         ; VX_TT

; ---- 0: uk_pic_cga's hook: DX:AX the head (its byte 9 the tag), BX x,
; CX y, the stack as vgaasm.asm's entry 9 wants it.
vx_pic:
        push ds
        push si
        push ax
        mov ds, dx
        mov si, ax
        push bp
        mov bp, sp
        cmp word [bp+14], 0     ; to_save (uik.c pic: the clean screen's picture)
        pop bp
        mov al, [si+9]
        je .set
        cmp byte [cs:VX_CURP], 5
        je .on                  ; a window in the battle keeps the battle's colours
.set:   or al, al
        jz .on
        call set_pal
.on:    push bp
        mov bp, sp
        cmp word [bp+14], 0
        pop bp
        jne clean
        pop ax
        pop si
        pop ds
        jmp far [cs:VX_THEIRS]

; A picture for the clean screen (uk_menu's background inside its window,
; drawn by uk_restore): vgaasm.asm's clean screen gets it as uk_pic_clean
; does, the plain one first, masked ones after it (up to 4), and as the
; last plain picture; nothing is drawn. DS:SI the head, BX x, CX y;
; returns to uk_pic_cga's own return.
clean:
        push bx
        push cx
        push dx
        push di
        push es
        mov es, [cs:VX_VSEG]
        xor di, di
        test byte [si+8], 1
        jnz .masked
        mov word [es:V_NCL], 1
        jmp short .put
.masked:
        mov di, [es:V_NCL]
        cmp di, 4
        jae .x
        inc word [es:V_NCL]
        mov dx, cx
        mov cl, 4
        shl di, cl
        mov cx, dx
.put:   add di, CLEAN
        mov [es:di+E_X], bx
        mov [es:di+E_Y], cx
        mov ax, [si+4]
        mov [es:di+E_W], ax
        mov ax, [si+6]
        mov [es:di+E_H], ax
        mov al, [si+8]
        xor ah, ah
        mov [es:di+E_FL], ax
        push es
        push di
        les di, [es:P_FD]       ; the pack's place: the picture's data
        mov bx, [es:di]
        mov ax, 4201h
        xor cx, cx
        xor dx, dx
        int 21h
        pop di
        pop es
        mov [es:di+E_POS], ax
        mov [es:di+E_POS+2], dx
        test byte [si+8], 1
        jnz .x
        push ds
        push si
        push es
        pop ds
        mov si, di
        mov di, LAST
        mov cx, 8
        cld
        rep movsw
        pop si
        pop ds
.x:     pop es
        pop di
        pop dx
        pop cx
        pop bx
        pop ax
        pop si
        pop ds
        retf

; The DOS palette of tag AL (bits 0-2 palette + 1, 3-6 ramp + 1, 7 the
; style), if it is not the DAC's already, and its VGA themes. Keeps every
; register.
set_pal:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        cld
        mov bl, al              ; the tag
        and al, 87h             ; the palette and style
        test al, 7
        jz .ramp
        mov bh, [cs:VX_CURP]
        cmp al, bh
        je .ramp
        mov [cs:VX_CURP], al
        xor bh, al
        test bh, 7
        jz .th                  ; the same palette, the other style
        mov byte [cs:VX_CURB], 0
        and al, 7
        dec al
        mov ah, 0
        mov cx, PALREC
        mul cx
        add ax, 8
        mov dx, ax              ; the palette: 768 bytes to PAL
        mov es, [cs:VX_VSEG]
        xor di, di
        mov cx, 768
        call read_at
        jc .x
        add dx, 768             ; its VMAP
        mov di, VMAP
        mov cx, 16
        call read_at
        push bx
        xor bx, bx              ; the whole DAC
        mov cx, 768
        call to_dac
        pop bx
.th:    call themes
.ramp:  mov al, bl
        mov cl, 3
        shr al, cl
        and al, 15
        jz .x
        cmp al, [cs:VX_CURB]
        je .x
        mov [cs:VX_CURB], al
        dec al
        mov ah, 48
        mul ah
        add ax, RAMPS
        mov dx, ax
        mov es, [cs:VX_VSEG]
        mov di, 224 * 3
        mov cx, 48
        call read_at
        jc .x
        mov bx, 224
        mov cx, 48
        call to_dac
.x:     pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; The VGA themes of VX_CURP into VX_TT and the one asked for (VX_THEME)
; in use; in a battle (VX_INBAT) the battlescape palette's are
; vgaasm.asm's. Keeps BX.
themes:
        mov byte [cs:VX_HAVE], 0
        mov al, [cs:VX_CURP]
        mov ah, al
        and al, 7
        cmp al, 5
        jb .on
        ja .theirs
        cmp byte [cs:VX_INBAT], 0       ; the battlescape palette: DOS's
        jne .theirs                     ; colours outside the battle only
        jmp short .pal
.on:    mov byte [cs:VX_INBAT], 0
.pal:
        dec al
        shl al, 1
        rol ah, 1
        and ah, 1
        add al, ah              ; (palette - 1) * 2 + style
        mov ah, TSET
        mul ah
        add ax, THEMES
        mov dx, ax
        push cs
        pop es
        mov di, VX_TT
        mov cx, TSET
        call read_at
        jc .theirs
        mov byte [cs:VX_HAVE], 1
        jmp short apply
.theirs:
        mov al, [cs:VX_THEME]
        xor ah, ah
        call far [cs:VX_THEMT]
        ret

; The theme VX_THEME of VX_TT in use: g_uk points at it, its VMAP to
; vgaasm.asm's. Keeps BX.
apply:
        cmp byte [cs:VX_HAVE], 0
        je .x
        push ds
        mov al, [cs:VX_THEME]
        mov ah, TREC
        mul ah
        add ax, VX_TT
        mov si, ax
        mov es, [cs:VX_VSEG]
        les di, [es:P_UK]
        mov [es:di], si
        mov [es:di+2], cs
        mov es, [cs:VX_VSEG]
        mov di, VMAP
        add si, 15
        push cs
        pop ds
        mov cx, 8
        cld
        rep movsw
        pop ds
.x:     ret

; ---- 6: vx_slide (dac far, caption)
vx_slide:
        push bp
        mov bp, sp
        push si
        push ds
        push es
        mov word [cs:VX_CURP], 0        ; (and VX_CURB)
        mov es, [cs:VX_VSEG]
        mov ax, [bp+10]
        mov [es:VMAP+14], al
        add al, 3
        mov [es:VMAP+6], al
        lds si, [bp+6]
        mov dx, 3C8h
        xor al, al
        out dx, al
        inc dx
        mov cx, 768
        mov ax, ds
        or ax, si
        jz .black
        cld
.c:     lodsb
        out dx, al
        loop .c
        jmp short .x
.black: out dx, al
        loop .black
.x:     pop es
        pop ds
        pop si
        pop bp
        retf

; ---- 5: vx_battle ()
vx_battle:
        push di
        push es
        mov ax, 0A000h
        mov es, ax
        xor di, di
        xor ax, ax
        mov cx, ROW * 200 / 2
        cld
        rep stosw
        mov byte [cs:VX_INBAT], 1
        mov byte [cs:VX_CURP], 0        ; (read again: vgaasm.asm's themes)
        mov al, 5               ; the battlescape's
        call set_pal
        pop es
        pop di
        retf

; ---- 3: uk_theme's hook: AX the theme
vx_theme:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        push ax
        call far [cs:VX_THEMT]
        pop ax
        mov [cs:VX_THEME], al
        call apply
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        retf

; ---- 4: uk_fill's hook: AX x, DX y, BX w, CX h; the colour on the stack
; above two returns (uk_fill's patched call, its caller's). A palette
; index (16 and up) goes through VMAP's colour 1 for the time (the
; pointer's colours are 0 and 15).
vx_fill:
        push bp
        mov bp, sp
        cmp word [bp+10], 16
        jae .index
        pop bp
        jmp far [cs:VX_FILLT]
.index: push si
        push es
        mov es, [cs:VX_VSEG]
        push word [es:VMAP]
        mov si, [bp+10]
        xchg ax, si
        mov [es:VMAP+1], al
        xchg ax, si
        mov si, 1
        push si                 ; the colour: 1
        sub sp, 4               ; (where the caller's return would be)
        call far [cs:VX_FILLT]
        add sp, 6
        mov es, [cs:VX_VSEG]
        pop word [es:VMAP]
        pop es
        pop si
        pop bp
        retf

; Reads CX bytes at PALV.PAK's offset DX to ES:DI. CF on failure. Keeps
; BX, CX, DX, DI, ES.
read_at:
        push bx
        push cx
        push dx
        push cx
        push dx
        push cs
        pop ds
        mov dx, VX_NAME
        mov ax, 3D00h           ; open, read only
        int 21h
        pop dx
        pop cx
        jc .x
        mov bx, ax
        push cx
        xor cx, cx
        mov ax, 4200h
        int 21h
        pop cx
        jc .close
        push es
        pop ds
        mov dx, di
        mov ah, 3Fh
        int 21h
.close: pushf
        mov ah, 3Eh
        int 21h
        popf
.x:     pop dx
        pop cx
        pop bx
        ret

; CX bytes of PAL from colour BX on to the DAC.
to_dac:
        mov ds, [cs:VX_VSEG]
        mov si, bx
        shl si, 1
        add si, bx
        mov al, bl
        mov dx, 3C8h
        out dx, al
        inc dx
.c:     lodsb
        out dx, al
        loop .c
        ret

; ---- 1: vx_world (map far, vx, vy, zoom)
vx_world:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        cld
        mov ax, 0A000h
        mov es, ax
        xor di, di
        mov cx, [bp+14]         ; zoom
        mov [cs:VX_ZOOM], cx
        mov ax, VIEW_W
        shr ax, cl
        mov [cs:VX_NEED], ax
        mov bx, WMAP_W
        sub bx, [bp+10]         ; vx: pixels to the date line
        cmp bx, ax
        jbe .n1
        mov bx, ax
.n1:    mov [cs:VX_N1], bx
        mov word [cs:VX_SP], STACK0     ; (geo.c forgets its markers)
        mov ds, [bp+8]          ; the map
        xor dx, dx              ; the screen row
.row:   mov ax, dx
        mov cl, [cs:VX_ZOOM]
        shr ax, cl
        add ax, [bp+12]         ; vy
        mov bx, WMAP_W
        push dx
        mul bx
        pop dx
        mov si, [bp+6]
        add si, ax
        push si
        add si, [bp+10]
        mov cx, [cs:VX_N1]
        call wseg
        pop si
        mov cx, [cs:VX_NEED]
        sub cx, [cs:VX_N1]
        call wseg
        add di, ROW - VIEW_W
        inc dx
        cmp dx, WMAP_H
        jb .row
        pop ds
        pop di
        pop si
        pop bp
        retf

; CX source pixels DS:SI to ES:DI, each 1, 2 or 4 times (VX_ZOOM).
wseg:
        jcxz .x
        mov al, [cs:VX_ZOOM]
        or al, al
        jnz .z
        shr cx, 1
        rep movsw
        jnc .x
        movsb
.x:     ret
.z:     cmp al, 1
        jne .z2
.z1:    lodsb
        mov ah, al
        stosw
        loop .z1
        ret
.z2:    lodsb
        mov ah, al
        stosw
        stosw
        loop .z2
        ret

; ---- 2: vx_xor (x0, y0, x1, y1, colour): geo.c's markers. DOS draws
; them in solid colours, and geo.c erases them in the opposite order
; (erase_marks pops its list), so instead of XORing: a box that is not
; the last one drawn is kept (its pixels and itself on a stack at the end
; of the battle view's back buffer, unused on the geoscape) and filled
; with the colour; the last one drawn, asked again, is put back from the
; stack. A box drawn twice in a row disappears, as with XOR. vx_world
; empties the stack (geo.c forgets its markers when it draws the map).
vx_xor:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        cld
        cmp word [bp+14], 16    ; a DOS marker: 3 x 3 at least (geo.c's 2 x 2)
        jb .box
        mov ax, [bp+10]
        sub ax, [bp+6]
        cmp ax, 2
        jne .h
        cmp word [bp+10], VIEW_W
        jae .h
        inc word [bp+10]
.h:     mov ax, [bp+12]
        sub ax, [bp+8]
        cmp ax, 2
        jne .box
        cmp word [bp+12], WMAP_H
        jae .box
        inc word [bp+12]
.box:   mov es, [cs:VX_VSEG]
        les bx, [es:P_BUFSEG]
        mov ax, [es:bx]
        mov ds, ax              ; the back buffer
        mov es, ax
        mov cx, [bp+10]
        sub cx, [bp+6]          ; w
        jle .xx
        mov ax, [bp+12]
        sub ax, [bp+8]          ; h
        jle .xx
        mov [cs:VX_W], cx
        mov [cs:VX_H], ax
        mul cx
        mov dx, ax              ; the box's bytes
        mov si, [cs:VX_SP]
        cmp si, STACK0 + 10
        jb .draw
        mov ax, [bp+6]
        cmp ax, [si-10]
        jne .draw
        mov ax, [bp+8]
        cmp ax, [si-8]
        jne .draw
        mov ax, [bp+10]
        cmp ax, [si-6]
        jne .draw
        mov ax, [bp+12]
        cmp ax, [si-4]
        jne .draw
        mov ax, [bp+14]
        cmp ax, [si-2]
        jne .draw
        sub si, 10              ; the last one drawn: back from the stack
        sub si, dx
        mov [cs:VX_SP], si
        mov ax, 0A000h
        mov es, ax
        call box_di
.put:   mov cx, [cs:VX_W]
        rep movsb
        add di, ROW
        sub di, [cs:VX_W]
        dec word [cs:VX_H]
        jnz .put
.xx:    jmp near .x
.draw:  mov di, si              ; keep the pixels under it
        add si, dx
        jc .xx
        cmp si, BYTES - 10
        ja .x                   ; (no room: not drawn)
        push di
        mov ax, 0A000h
        mov ds, ax
        call box_di
        mov si, di
        pop di
        mov bx, [cs:VX_H]
.get:   mov cx, [cs:VX_W]
        rep movsb
        add si, ROW
        sub si, [cs:VX_W]
        dec bx
        jnz .get
        mov si, bp              ; the box itself
        add si, 6
        push ss
        pop ds
        mov cx, 5
        rep movsw
        mov [cs:VX_SP], di
        mov ds, [cs:VX_VSEG]    ; the colour
        mov bx, [bp+14]
        and bx, 15
        mov al, [bx+VMAP]
        mov bx, 0A000h
        mov es, bx
        call box_di
        cmp word [bp+14], 16
        jae .pic
        mov bx, [cs:VX_H]
.fill:  mov cx, [cs:VX_W]
        rep stosb
        add di, ROW
        sub di, [cs:VX_W]
        dec bx
        jnz .fill
        jmp short .x
.pic:   mov cx, [cs:VX_W]       ; a DOS marker
        cmp cx, 3
        jb .x
        mov dx, [cs:VX_H]
        cmp dx, 3
        jb .x
        cmp cx, 3
        jne .sel
        cmp dx, 3
        je .mid
.sel:   call outline            ; (selected)
.mid:   sub cx, 3
        shr cx, 1
        add di, cx
        sub dx, 3
        shr dx, 1
        mov ax, ROW
        mul dx
        add di, ax
        mov ax, [bp+14]
        mov cl, 4
        shr ax, cl
        dec ax
        mov cl, 9
        mul cl
        add ax, L(marks)
        mov si, ax
        push cs
        pop ds
        mov dx, 3
.pr:    mov cx, 3
.pp:    lodsb
        or al, al
        jz .sk
        mov [es:di], al
.sk:    inc di
        loop .pp
        add di, ROW - 3
        dec dx
        jnz .pr
.x:     pop ds
        pop di
        pop si
        pop bp
        retf

; The box's edge (VX_W x VX_H at ES:DI) in AL. Keeps CX, DX, DI.
outline:
        push cx
        push dx
        push di
        mov dx, [cs:VX_H]       ; rows left
.r:     push di
        mov cx, [cs:VX_W]
        cmp dx, [cs:VX_H]
        je .full
        cmp dx, 1
        je .full
        mov [es:di], al
        add di, cx
        mov [es:di-1], al
        jmp short .n
.full:  rep stosb
.n:     pop di
        add di, ROW
        dec dx
        jnz .r
        pop di
        pop dx
        pop cx
        ret

; OpenXcom's GlobeMarkers (Resources/UI/globe_ufo.png, 3 x 3, colours of
; the geoscape palette, 0 clear): base, craft, UFO flying, landed,
; crashed, terror site, waypoint, alien base, (8).
marks:  db 9, 9, 9, 9, 0, 9, 9, 9, 9
        db 0, 11, 0, 11, 0, 11, 0, 11, 0
        db 0, 13, 0, 13, 13, 13, 0, 13, 0
        db 7, 0, 7, 0, 7, 0, 7, 0, 7
        db 5, 0, 5, 0, 5, 0, 5, 0, 5
        db 0, 1, 0, 1, 1, 1, 0, 1, 0
        db 3, 0, 3, 0, 3, 0, 3, 0, 3
        db 1, 1, 1, 1, 0, 1, 1, 1, 1
        db 14, 14, 14, 14, 11, 14, 14, 14, 14

; DI = the screen offset of the box's corner ([bp+6], [bp+8]). Keeps AX.
box_di:
        push ax
        mov ax, [bp+8]
        mov di, ROW
        mul di
        add ax, [bp+6]
        mov di, ax
        pop ax
        ret
blob_end:
