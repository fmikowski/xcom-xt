; SPDX-License-Identifier: GPL-3.0-or-later
; The battlescape's floor pass (src/render.c draw_region) in assembly: per
; level, every tile whose sprites can touch the redraw rectangle, column by
; column (x, then y): its floor is drawn (ground floors with the OR blitter
; on the cleared area) and the tiles with walls, objects, units or the
; cursor are collected for the second pass. In the battle core's overlay
; section. Large model, Watcom __cdecl; C functions called with Watcom's
; register convention (they keep CX, SI, DI, BP). The block lies in DGROUP
; (= SS = DS) and is addressed through BP. C leaves DS anything (it reaches
; DGROUP through SS, and sets DS = SS only for __cdecl calls like ours), so
; DS is set again after each C function, for the DS-relative data below.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

SEEN_BIT  equ 10h
MAX_LATE  equ 400               ; render.c
LATE_HID  equ 40h
LATE_ROOF equ 8000h             ; under full floors of the level above
M_SPRITE  equ 0                 ; Mcd
M_PLEVEL  equ 3
M_ORSPR   equ 14
OR_NONE   equ 7FFFh

; The block.
B_MAP   equ 0                   ; far: tile parts
B_SEEN  equ 4                   ; far: SEEN_BIT per tile, segment 0 = no fog
B_MCD   equ 8                   ; far: MCD records
B_UA    equ 12                  ; far: unit per tile, segment 0 = none
B_SPR   equ 16                  ; far: sprite table (far pointers)
B_LATE  equ 20                  ; far: render.c late[]
B_XSH   equ 24                  ; screen x to buffer byte: shift
B_TOP   equ 28
B_EGA   equ 30
B_W     equ 32                  ; buffer row bytes
B_MX    equ 34
B_MY    equ 36
B_BSEG  equ 38
B_CX0   equ 40                  ; clip rectangle (x1 - 32)
B_CY0   equ 42
B_CX1   equ 44
B_CY1   equ 46
B_CAMX  equ 48
B_CAMY  equ 50
B_UA0   equ 52                  ; tile x - y >= B_UA0 + 1 ...
B_UB    equ 54                  ; ... and <= B_UB; x + y >= B_VC + 1, <= B_VD
B_VC    equ 56
B_VD    equ 58
B_Z     equ 60
B_ZBASE equ 62
B_CURX  equ 64                  ; the cursor's tile on this level, x = -1: none
B_CURY  equ 66
B_NLATE equ 68
B_X     equ 70                  ; the column
B_N     equ 72                  ; tiles left in it
B_Y     equ 74                  ; the tile: y, index, box (floor only)
B_IDX   equ 76
B_SX    equ 78
B_SY    equ 80
B_ZY    equ 82                  ; 24 z - cam_y
B_YEND  equ 84                  ; the column's last y + 1
B_OR    equ 86                  ; its OR sprite
B_COV   equ 88                  ; far: the cover map (render.c add[]): 4 columns of
                                ; COV_COL bytes, (x & 3) * COV_COL + y + 3, low
                                ; nibble = the floor's quads drawn over
B_OCC   equ 92                  ; covered floors are left out (pack with cover data)
B_LOOK  equ 94                  ; columns collected ahead of their floors: 3 or 0
B_COVON equ 96                  ; B_OCC, 0 once late[] is full on this level
B_UP1   equ 100                 ; full floors of level z + 1 count (z < top)
B_UP2   equ 102                 ; ... of level z + 2 (z + 2 <= top)
B_C     equ 104                 ; the column being collected
B_S0    equ 106                 ; cover map offsets of columns c, c - 1, c - 2, c - 3
B_S1    equ 108
B_S2    equ 110
B_S3    equ 112
B_SF    equ 114                 ; ... of the floors' column
B_XLO   equ 116                 ; the columns with tiles in the rectangle
B_XHI   equ 118
B_SIZE  equ 120
COV_COL equ 68

extern _g_map:wrt DGROUP, _g_seen:wrt DGROUP, _g_mx:wrt DGROUP, _g_my:wrt DGROUP
extern _g_unit_at:wrt DGROUP, _g_video:wrt DGROUP, _g_bufseg:wrt DGROUP
extern _g_clip_x0:wrt DGROUP, _g_clip_y0:wrt DGROUP, _g_clip_x1:wrt DGROUP, _g_clip_y1:wrt DGROUP
extern _g_cam_x:wrt DGROUP, _g_cam_y:wrt DGROUP
extern _g_cursor_on:wrt DGROUP, _g_cur_x:wrt DGROUP, _g_cur_y:wrt DGROUP, _g_cur_z:wrt DGROUP
extern _g_occ_count:wrt DGROUP
extern gfx_sprite_, gfx_sprite_or_, _blit_or, _blitc_or

segment _BSS public align=2 class=BSS

blk     resb B_SIZE

segment _DATA public align=2 class=DATA
group DGROUP _BSS _DATA

; Tiles (a, b) of the levels above, then their index offsets (rd_setup).
; roofed: the 13 tiles of the level above whose diamonds meet a tile's
; reach (its 32 columns, REACH_UP rows above its box to the box's bottom):
; |a - b| <= 1, |a + b| <= 4.
roof    db 0, 0, 1, 1, -1, -1, 1, 0, 0, 1, -1, 0, 0, -1
        db 2, 1, 1, 2, -1, -2, -2, -1, 2, 2, -2, -2
; floor: the owners of a floor's quads top, bottom, left, right on the
; level above, then the tile covering all of it two levels up.
quad    db 1, 1, 2, 2, 1, 2, 2, 1, 3, 3
roofoff times 18 dw 0
roofmx  dw 0                    ; the map size they are for
rooflev dw 0
segment RENDASM_TEXT public class=CODE

; void __cdecl rd_setup(int top, const void far *mcd, void far *spr, void far *late,
;                       void far *cov, int occ)
; Before a redraw (render_redraw: the clip rectangle is the redrawn one).
        global _rd_setup
_rd_setup:
        push bp
        mov bp, sp
        push si
        push di
        mov si, bp
        mov bp, blk
        mov ax, [si+6]
        mov [bp+B_TOP], ax
        les ax, [si+8]
        mov [bp+B_MCD], ax
        mov [bp+B_MCD+2], es
        les ax, [si+12]
        mov [bp+B_SPR], ax
        mov [bp+B_SPR+2], es
        les ax, [si+16]
        mov [bp+B_LATE], ax
        mov [bp+B_LATE+2], es
        les ax, [si+20]
        mov [bp+B_COV], ax
        mov [bp+B_COV+2], es
        mov ax, [si+24]
        mov [bp+B_OCC], ax
        neg ax
        sbb ax, ax
        and ax, 3
        mov [bp+B_LOOK], ax
        ; far pointers and words from C
        les ax, [_g_map]
        mov [bp+B_MAP], ax
        mov [bp+B_MAP+2], es
        les ax, [_g_seen]
        mov [bp+B_SEEN], ax
        mov [bp+B_SEEN+2], es
        les ax, [_g_unit_at]
        mov [bp+B_UA], ax
        mov [bp+B_UA+2], es
        push ss
        pop es
        mov di, blk + B_MX      ; B_MX .. B_CAMY: the words of cwords[]
        mov si, cwords
        mov cx, (B_CAMY + 2 - B_MX) / 2
.word:
        cs lodsw
        xchg ax, bx
        mov ax, [bx]
        stosw
        loop .word
        ; EGA: 40 bytes a row, 8 pixels a byte; CGA: 80, 4
        mov ax, 1
        mov bx, 40
        mov cx, 3
        cmp word [_g_video], 0
        je .ega
        dec ax
        mov bx, 80
        dec cx
.ega:
        mov [bp+B_EGA], ax
        mov [bp+B_W], bx
        mov [bp+B_XSH], cx
        ; columns: x - y in [A + 1, B], A = floor((x0 - 32 - cam_x) / 16),
        ; B = floor((x1 - 1 - cam_x) / 16)
        mov cl, 4
        mov ax, [bp+B_CX0]
        sub ax, 32
        sub ax, [bp+B_CAMX]
        sar ax, cl
        mov [bp+B_UA0], ax
        mov ax, [bp+B_CX1]
        dec ax
        sub ax, [bp+B_CAMX]
        sar ax, cl
        mov [bp+B_UB], ax
        sub word [bp+B_CX1], 32
        mov ax, [bp+B_MX]
        mul word [bp+B_MY]
        mov bx, ax
        ; the offsets of roof[] and quad[], once per map size
        cmp ax, [rooflev]
        jne .new
        mov ax, [bp+B_MX]
        cmp ax, [roofmx]
        je .known
.new:
        mov [rooflev], bx
        mov ax, [bp+B_MX]
        mov [roofmx], ax
        mov si, roof
        mov di, roofoff
.roof:
        mov al, [si+1]
        cbw
        imul word [bp+B_MX]
        mov cx, ax
        mov al, [si]
        cbw
        add ax, cx
        add ax, bx
        mov [di], ax
        add si, 2
        add di, 2
        cmp si, roofoff
        jb .roof
        add [roofoff + quad + 8 - roof], bx    ; two levels up
.known:
        pop di
        pop si
        pop bp
        retf
cwords  dw _g_mx, _g_my, _g_bufseg, _g_clip_x0, _g_clip_y0, _g_clip_x1, _g_clip_y1, _g_cam_x, _g_cam_y

; int __cdecl rd_level(int z)
; The floor pass of level z: draws the floors, returns the tiles collected
; in late[] (x << 8 | y, LATE_HID if not seen, LATE_ROOF if roofed) in drawing order.
        global _rd_level
_rd_level:
        push bp
        mov bp, sp
        push si
        push di
        mov ax, [bp+6]
        mov bp, blk
        mov [bp+B_Z], ax
        mov word [bp+B_NLATE], 0
        mov word [bp+B_CURX], -1
        cmp word [_g_cursor_on], 0
        je .nocur
        cmp ax, [_g_cur_z]
        jne .nocur
        mov cx, [_g_cur_x]
        mov [bp+B_CURX], cx
        mov cx, [_g_cur_y]
        mov [bp+B_CURY], cx
.nocur:
        mov bx, ax
        mul word [bp+B_MX]
        mul word [bp+B_MY]
        mov [bp+B_ZBASE], ax
        ; rows: x + y in [C + 1, D], C = floor((y0 - 40 + 24 z - cam_y) / 8),
        ; D = floor((y1 + 23 + 24 z - cam_y) / 8)
        mov ax, bx
        shl ax, 1
        add ax, bx
        mov cl, 3
        shl ax, cl              ; 24 z
        sub ax, [bp+B_CAMY]
        mov [bp+B_ZY], ax
        mov bx, ax
        add ax, [bp+B_CY0]
        sub ax, 40
        sar ax, cl
        mov [bp+B_VC], ax
        add bx, [bp+B_CY1]
        add bx, 23
        sar bx, cl
        mov [bp+B_VD], bx
        ; covered floors: walls and objects of the level mark the quads
        ; they draw over in the cover map, columns collected 3 ahead; a
        ; floor looks for full floors of the two levels above (z + 1 within
        ; top, z + 2 too) for the quads still open
        mov ax, [bp+B_OCC]
        mov [bp+B_COVON], ax
        xor cx, cx
        xor dx, dx
        test ax, ax
        jz .up
        mov ax, [bp+B_Z]
        inc ax
        cmp ax, [bp+B_TOP]
        jg .up
        inc cx
        inc ax
        cmp ax, [bp+B_TOP]
        jg .up
        inc dx
.up:
        mov [bp+B_UP1], cx
        mov [bp+B_UP2], dx
        ; the columns with tiles in the rectangle (col_setup's ylo <= yhi):
        ; x >= 0, A + 1, C + 2 - my, (C + A + 2) / 2 rounded up;
        ; x <= mx - 1, D, B + my - 1, (B + D) / 2 rounded down
        mov ax, [bp+B_UA0]
        inc ax
        mov bx, [bp+B_VC]
        add bx, 2
        sub bx, [bp+B_MY]
        cmp bx, ax
        jle .lo1
        mov ax, bx
.lo1:
        mov bx, [bp+B_VC]
        add bx, [bp+B_UA0]
        add bx, 3
        sar bx, 1
        cmp bx, ax
        jle .lo2
        mov ax, bx
.lo2:
        test ax, ax
        jge .lo3
        xor ax, ax
.lo3:
        mov [bp+B_XLO], ax
        mov ax, [bp+B_MX]
        dec ax
        mov bx, [bp+B_VD]
        cmp bx, ax
        jge .hi1
        mov ax, bx
.hi1:
        mov bx, [bp+B_UB]
        add bx, [bp+B_MY]
        dec bx
        cmp bx, ax
        jge .hi2
        mov ax, bx
.hi2:
        mov bx, [bp+B_UB]
        add bx, [bp+B_VD]
        sar bx, 1
        cmp bx, ax
        jge .hi3
        mov ax, bx
.hi3:
        mov [bp+B_XHI], ax
        mov ax, [bp+B_XLO]
.column:
        mov [bp+B_C], ax
        cmp ax, [bp+B_XHI]
        jg .floors
        call col_setup
        jc .floors
        cmp word [bp+B_OCC], 0
        je .collect
        call slots
.collect:
        call col_late
.floors:
        mov ax, [bp+B_C]
        sub ax, [bp+B_LOOK]
        cmp ax, [bp+B_XLO]
        jl .empty
        call col_setup
        jc .empty
        call col_floors
.empty:
        mov ax, [bp+B_C]
        inc ax
        mov cx, [bp+B_XHI]
        add cx, [bp+B_LOOK]
        cmp ax, cx
        jle .column
        push ss
        pop ds
        mov ax, [bp+B_NLATE]
        pop di
        pop si
        pop bp
        retf

; AX = x: the column's tiles that can touch the rectangle, y from B_Y to
; B_YEND - 1, B_N of them, the first's index. CF set: none.
col_setup:
        mov [bp+B_X], ax
        ; ylo = max(0, x - B, C + 1 - x)
        mov bx, ax
        sub bx, [bp+B_UB]
        mov cx, [bp+B_VC]
        inc cx
        sub cx, ax
        cmp cx, bx
        jle .a
        mov bx, cx
.a:
        test bx, bx
        jge .b
        xor bx, bx
.b:
        ; yhi = min(my - 1, x - A - 1, D - x)
        mov dx, ax
        sub dx, [bp+B_UA0]
        dec dx
        mov cx, [bp+B_VD]
        sub cx, ax
        cmp cx, dx
        jge .c
        mov dx, cx
.c:
        mov cx, [bp+B_MY]
        dec cx
        cmp dx, cx
        jle .d
        mov dx, cx
.d:
        sub dx, bx
        jge .some
        stc
        ret
.some:
        inc dx
        mov [bp+B_N], dx
        mov [bp+B_Y], bx
        add dx, bx
        mov [bp+B_YEND], dx
        mov cx, ax              ; x
        mov ax, bx
        mul word [bp+B_MX]
        add ax, cx
        add ax, [bp+B_ZBASE]
        mov [bp+B_IDX], ax
        clc
        ret

; The column walk stopped on the tile with CX tiles left, ES:DI its parts:
; B_Y and B_IDX (also in AX) for it. DS = DGROUP.
sync:
        push ss
        pop ds
        mov ax, [bp+B_YEND]
        sub ax, cx
        mov [bp+B_Y], ax
        mov ax, di
        sub ax, [bp+B_MAP]
        shr ax, 1
        shr ax, 1
        mov [bp+B_IDX], ax
        ret

; sync, and CF set if the tile is not seen (with fog). Keeps ES:DI.
pick:
        call sync
        cmp word [bp+B_SEEN+2], 0
        je .ret                 ; CF clear
        lds bx, [bp+B_SEEN]
        add bx, ax
        test byte [bx], SEEN_BIT
        push ss
        pop ds
        jnz .ret
        stc
.ret:
        ret

; AX = a column: its cover map offset (within B_COV) in AX. Uses CX.
slot_of:
        and ax, 3
        mov cx, ax
        shl ax, 1
        shl ax, 1
        shl ax, 1
        shl ax, 1               ; 16 n
        add ax, cx              ; 17 n
        shl ax, 1
        shl ax, 1               ; 68 n
        add ax, [bp+B_COV]
        ret

; The cover map offsets of columns c .. c - 3 (B_C); clears column c's
; bytes its floors will read (B_Y, B_N: col_setup). A column without tiles
; in the rectangle is never read, so what later columns mark there does
; not matter.
slots:
        mov ax, [bp+B_C]
        call slot_of
        mov [bp+B_S0], ax
        mov di, ax
        mov dx, [bp+B_COV]
        mov si, B_S1
.prev:
        cmp ax, dx
        jne .sub
        add ax, 4 * COV_COL
.sub:
        sub ax, COV_COL
        mov [bp+si], ax
        inc si
        inc si
        cmp si, B_S3 + 2
        jb .prev
        add di, [bp+B_Y]
        add di, 3
        mov es, [bp+B_COV+2]
        xor ax, ax
        mov cx, [bp+B_N]
        cld
        rep stosb
        ret

; The column's floors: tiles with a floor that are seen.
col_floors:
        mov ax, [bp+B_X]
        call slot_of
        mov [bp+B_SF], ax
        call .regs
.tile:
        cmp byte [es:di], 0
        je .next
        test byte [si], SEEN_BIT
        jnz .draw
.next:
        add si, dx
        add di, bx
        loop .tile
        ret
.draw:
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        call sync
        mov al, [es:di]
        call floor
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        jmp short .next
.regs:
        les si, [bp+B_SEEN]
        mov ax, roof + 4        ; 0FFh: seen (no fog)
        ; fall through

; ES:SI = a far table with a byte per tile (segment 0: none), AX = a byte
; of DGROUP read instead. The column walk's registers for the column's
; first tile (B_IDX, B_N): DS:SI = its byte, DX = the bytes' step (0 for the
; stand-in), ES:DI = its parts, BX = their step, CX = tiles left.
col_regs:
        push ss
        pop ds
        xchg ax, si
        xor dx, dx
        mov cx, es
        jcxz .map
        mov si, ax
        add si, [bp+B_IDX]
        mov dx, [bp+B_MX]
        mov ds, cx
.map:
        les di, [bp+B_MAP]
        mov ax, [bp+B_IDX]
        shl ax, 1
        shl ax, 1
        add di, ax
        mov bx, [bp+B_MX]
        shl bx, 1
        shl bx, 1
        mov cx, [bp+B_N]
        ret

; AL = the floor record of the tile: draws it.
floor:
        les si, [bp+B_MCD]
        xor ah, ah
        mov cl, 4
        shl ax, cl
        add si, ax              ; ES:SI = the record
        mov ax, [es:si+M_ORSPR]
        and ax, OR_NONE
        cmp ax, OR_NONE
        je .masked
        mov [bp+B_OR], ax
        ; inside its diamond: left out if all its quads are drawn over
        cmp word [bp+B_COVON], 0
        je .drawn
        push es
        push si
        mov es, [bp+B_COV+2]
        mov di, [bp+B_SF]
        mov bx, [bp+B_Y]
        mov dl, [es:bx+di+3]
        not dl
        and dl, 15              ; quads still open
        jz .covered
        cmp word [bp+B_UP1], 0
        je .open
        mov dh, 1
        mov cx, quad
.quad:
        test dl, dh
        jz .qnext
        call up_floor
        jz .z2
.qnext:
        inc cx
        inc cx
        shl dh, 1
        cmp dh, 16
        jb .quad
.covered:
        pop si
        pop es
        inc word [_g_occ_count]
        ret
.z2:
        cmp word [bp+B_UP2], 0
        je .open
        mov cx, quad + 8
        call up_floor
        jnz .covered
.open:
        pop si
        pop es
.drawn:
        call box
        cmp word [bp+B_Z], 0
        jne .sprite
        mov ax, [bp+B_OR]
        ; The diamond's own rows (sprite header) decide.
        mov bx, ax
        shl bx, 1
        shl bx, 1
        les si, [bp+B_SPR]
        les si, [es:si+bx]      ; ES:SI = the OR sprite
        mov dx, [bp+B_SY]
        mov al, [es:si]
        xor ah, ah
        add dx, ax              ; r0
        mov bx, dx
        mov al, [es:si+1]
        add bx, ax              ; r1
        cmp bx, [bp+B_CY0]
        jle .done
        cmp dx, [bp+B_CY1]
        jge .done
        mov ax, [bp+B_SX]
        cmp ax, [bp+B_CX0]
        jl .clip
        cmp ax, [bp+B_CX1]
        jg .clip
        cmp dx, [bp+B_CY0]
        jl .clip
        cmp bx, [bp+B_CY1]
        jg .clip
        ; dest = sy * w + (sx >> xsh)
        mov ax, [bp+B_SY]
        mul word [bp+B_W]
        mov dx, [bp+B_SX]
        mov cl, [bp+B_XSH]
        sar dx, cl
        add ax, dx
        push ax
        push word [bp+B_BSEG]
        push es
        push si
        cmp word [bp+B_EGA], 0
        je .cga
        call (seg _blit_or):_blit_or
        jmp short .popped
.cga:
        call (seg _blitc_or):_blitc_or
.popped:
        add sp, 8
.done:
        ret
.clip:
        mov ax, [bp+B_OR]
        mov dx, [bp+B_SX]
        mov bx, [bp+B_SY]
        call (seg gfx_sprite_or_):gfx_sprite_or_
        jmp short .ds
.masked:
        call box
.sprite:
        mov ax, [es:si+M_SPRITE]
        mov dx, [bp+B_SX]
        mov bx, [bp+B_SY]
        mov cl, [es:si+M_PLEVEL]
        xor ch, ch
        sub bx, cx
        call (seg gfx_sprite_):gfx_sprite_
.ds:
        push ss
        pop ds
        ret

; The tile's place on the screen (B_X, B_Y): B_SX, B_SY. Uses BX, CX, DX.
box:
        ; sx = (x - y) * 16 + cam_x, sy = (x + y) * 8 - 24 z + cam_y
        mov bx, [bp+B_X]
        mov dx, bx
        mov cx, [bp+B_Y]
        sub bx, cx
        add dx, cx
        mov cl, 4
        shl bx, cl
        add bx, [bp+B_CAMX]
        mov [bp+B_SX], bx
        dec cx
        shl dx, cl
        sub dx, [bp+B_ZY]
        mov [bp+B_SY], dx
        ret

; The column's tiles for the second pass: the cursor's, and seen ones with
; a wall, an object or a unit. With cover data, their walls and objects
; mark the cover map, and the ones under full floors of the level above are
; marked LATE_ROOF. The walk looks at the fog only for tiles with something.
col_late:
        les si, [bp+B_UA]
        mov ax, roof            ; 0: no unit
        call col_regs
        ; AH = tiles left at the cursor's tile; no tile of the walk has that
        ; count if the cursor is not in it (y < 64: a cursor above B_Y gives
        ; AH > B_N, one at B_YEND or below it AH >= 193)
        mov ax, [bp+B_X]
        cmp ax, [bp+B_CURX]
        mov ax, [bp+B_YEND]
        je .cur
        mov ax, [bp+B_CURY]
.cur:
        sub ax, [bp+B_CURY]
        mov ah, al
.tile:
        cmp cl, ah
        je .slow
        mov al, [es:di+1]
        or al, [es:di+2]
        or al, [es:di+3]
        jnz .slow
        cmp byte [si], 0
        jne .slow
.next:
        add si, dx
        add di, bx
        loop .tile
        ret
.slow:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        cmp cl, ah
        jne .some
        call pick               ; the cursor's tile
        mov dx, LATE_HID
        jc .add
        call scatter
        xor dx, dx
        jmp short .add
.some:
        call pick
        jc .back
        mov al, [es:di+1]
        or al, [es:di+2]
        or al, [es:di+3]
        jz .add0
        call scatter
.add0:
        xor dx, dx
        cmp word [bp+B_UP1], 0
        je .add
        call roofed
        jz .add
        mov dh, LATE_ROOF >> 8
.add:
        mov ax, [bp+B_NLATE]
        cmp ax, MAX_LATE
        jb .room
        mov word [bp+B_COVON], 0 ; a tile not drawn: no more marks to trust
        jmp short .back
.room:
        les di, [bp+B_LATE]
        mov bx, ax
        shl bx, 1
        mov ah, [bp+B_X]
        mov al, [bp+B_Y]
        or ax, dx
        mov [es:bx+di], ax
        inc word [bp+B_NLATE]
.back:
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        jmp short .next

; ES:DI = a seen tile's parts: its walls' and object's cover masks
; (or_sprite, tools/occlude.py) mark the quads of its floor and of the
; floors behind it. Uses AX, BX, CX, DX, SI, DI, ES.
scatter:
        cmp word [bp+B_OCC], 0
        je .ret
        mov al, [es:di+1]
        mov ah, [es:di+2]
        mov dl, [es:di+3]
        les si, [bp+B_MCD]
        xor cx, cx
        call .mask
        mov al, ah
        call .mask
        mov al, dl
        call .mask
        jcxz .ret
        les di, [bp+B_COV]
        mov bx, [bp+B_Y]
        add bx, 3
        mov si, bx
        add si, [bp+B_S0]
        mov al, cl              ; (0, 0): bits 0-3
        and al, 15
        or [es:si], al
        test ch, 20h            ; (0, 1) left
        jz .a
        or byte [es:si-1], 4
.a:
        mov si, bx
        add si, [bp+B_S1]
        mov al, cl              ; (1, 1): bits 4-7
        shr al, 1
        shr al, 1
        shr al, 1
        shr al, 1
        or [es:si-1], al
        test ch, 10h            ; (1, 0) right
        jz .b
        or byte [es:si], 8
.b:
        test ch, 80h            ; (1, 2) left
        jz .c
        or byte [es:si-2], 4
.c:
        mov si, bx
        add si, [bp+B_S2]
        mov al, ch              ; (2, 2): bits 8-11
        and al, 15
        or [es:si-2], al
        test ch, 40h            ; (2, 1) right
        jz .ret
        or byte [es:si-1], 8
.ret:
        ret
; AL = a record (0 = none): its mask into CX.
.mask:
        test al, al
        jz .none
        mov bl, al
        xor bh, bh
        shl bx, 1
        shl bx, 1
        shl bx, 1
        shl bx, 1
        or cx, [es:si+bx+M_ORSPR]
.none:
        ret

; BX = tile index: ZF clear if it holds a full floor that is seen (bit 15
; of its or_sprite). Uses AX, BX, CL, DI, ES.
full_floor:
        cmp word [bp+B_SEEN+2], 0
        je .seen
        les di, [bp+B_SEEN]
        test byte [es:bx+di], SEEN_BIT
        jz .no
.seen:
        les di, [bp+B_MAP]
        shl bx, 1
        shl bx, 1
        mov al, [es:bx+di]
        test al, al
        jz .no
        les di, [bp+B_MCD]
        xor ah, ah
        mov cl, 4
        shl ax, cl
        mov bx, ax
        test byte [es:bx+di+M_ORSPR+1], 80h
        ret
.no:
        xor ax, ax
        ret

; SI = tile index: ZF clear if it holds a full floor that is seen. Uses
; AX, BX, CL, DI, ES.
floor_at:
        les di, [bp+B_MAP]
        mov bx, si
        shl bx, 1
        shl bx, 1
        cmp byte [es:bx+di], 0
        je .no
        mov bx, si
        jmp full_floor
.no:
        ret

; CX = an entry of roof[] or quad[]: ZF clear if the tile that far from
; B_X, B_Y, B_IDX holds a full floor that is seen. Keeps CX, DX; uses AX,
; BX, SI, DI, ES.
up_floor:
        mov si, cx
        mov al, [si]
        cbw
        add ax, [bp+B_X]
        cmp ax, [bp+B_MX]
        jae .no
        mov al, [si+1]
        cbw
        add ax, [bp+B_Y]
        cmp ax, [bp+B_MY]
        jae .no
        mov si, [si+roofoff-roof]
        add si, [bp+B_IDX]
        push cx
        call floor_at
        pop cx
        ret
.no:
        xor ax, ax
        ret

; ZF clear if full floors of level z + 1 (drawn: z < top) cover the reach of
; the tile at B_X, B_Y, B_IDX: the 13 tiles of roof[]. Uses AX, BX, CX,
; SI, DI, ES.
roofed:
        mov cx, roof
.tile:
        call up_floor
        jz .no
        inc cx
        inc cx
        cmp cx, quad
        jb .tile
        or cx, cx               ; ZF clear: covered
.no:
        ret
