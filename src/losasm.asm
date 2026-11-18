; SPDX-License-Identifier: GPL-3.0-or-later
; Fog of war in assembly: the fan of sight rays that uncovers the map in
; front of a unit (los_reveal in los.c), and the box around the tiles
; uncovered. Large model, Watcom __cdecl.
;
; A ray runs from the unit's tile towards a tile of the square ring RANGE
; tiles away, on the unit's level, the one above or the one below. Along its
; major axis it moves one tile a step, always half way through the tile;
; along the minor axis step / 256 of a tile (dx * 256 / n in C, rounded
; towards 0). The three rays to one ring tile change level at step
; RANGE / 2 + 1 (z moves 256 / RANGE = 12 a step from 128: 248 after 10
; steps, 260 after 11, 368 at the end; going down 8, -4, -112), so their
; first RANGE / 2 steps are one walk, and the rest is walked three times
; from where that one ended.
;
; The walk's constants (the steps in the tables, the wall bits, the map's
; edge) are patched into its instructions per octant and ray (as fxasm.asm
; patches its plane instructions).

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

LF_WEST   equ 01h
LF_NORTH  equ 02h
LF_OBJECT equ 04h
LF_FLOOR  equ 08h
SEEN_BIT  equ 10h               ; fog of war bit in the seen table (path.h)

RANGE     equ 20                ; VIEW_RANGE (los.h)

; Parameter block (struct RevealFan in los.c).
F_LFSEG   equ 0
F_SEENSEG equ 2
F_PI      equ 4                 ; pi, mxy, mx, my, x, y, z, up, vx, vy, sx, sy:
F_END     equ 18                ; the variables from pi on

segment LOSASM_TEXT public class=CODE

; (from the parameter block, in its order)
pi      dw 0                    ; the unit's tile in the tables
mxy     dw 0                    ; tiles a level
mxb     db 0                    ; the map's size
myb     db 0
ux      db 0                    ; the unit's tile
uy      db 0
uz      db 0
up      db 0                    ; 1: there is a level above
vx      db 0                    ; the ring tile aimed at, from the unit
vy      db 0
sx      db 0                    ; from one ring tile to the next
sy      db 0
left    db 0                    ; ring tiles to go
revealed db 0
key     db 0                    ; the rays' octant: 1 y is the major axis,
                                ; 2 the major axis runs towards -, 4 the minor
majstep db 0                    ; the major coordinate's step, 1 or -1
c0      db 0, 0                 ; the unit's major and minor coordinate
ends    db 0, 0                 ; the major coordinate after RANGE / 2 steps
                                ; and at the ray's end (the map's edge at most)
zcur    db 0                    ; the level the ray is on

; The minor axis' step for a ring tile 0 .. RANGE - 1 tiles off the major
; axis (RANGE off, the diagonal: see ray).
steps:
%assign i 0
%rep RANGE
        db i * 256 / RANGE
%assign i i + 1
%endrep

; Bounding box of tiles newly marked as seen since reveal_reset_box().
box_x0  db 255
box_x1  db 0
box_y0  db 255
box_y1  db 0
box_z0  db 255
box_z1  db 0

; Grows the box by the tile CL = x, CH = y, BL = z.
%macro GROW 0
        cmp cl, [cs:box_x0]
        jae %%a
        mov [cs:box_x0], cl
%%a:    cmp cl, [cs:box_x1]
        jbe %%b
        mov [cs:box_x1], cl
%%b:    cmp ch, [cs:box_y0]
        jae %%c
        mov [cs:box_y0], ch
%%c:    cmp ch, [cs:box_y1]
        jbe %%d
        mov [cs:box_y1], ch
%%d:    cmp bl, [cs:box_z0]
        jae %%e
        mov [cs:box_z0], bl
%%e:    cmp bl, [cs:box_z1]
        jbe %%f
        mov [cs:box_z1], bl
%%f:
%endmacro

; void __cdecl reveal_reset_box(void)
        global _reveal_reset_box
_reveal_reset_box:
        mov byte [cs:box_x0], 255
        mov byte [cs:box_y0], 255
        mov byte [cs:box_z0], 255
        mov byte [cs:box_x1], 0
        mov byte [cs:box_y1], 0
        mov byte [cs:box_z1], 0
        retf

; void __cdecl reveal_get_box(unsigned char far *box)   x0 x1 y0 y1 z0 z1
        global _reveal_get_box
_reveal_get_box:
        push bp
        mov bp, sp
        push di
        les di, [bp+6]
        mov al, [cs:box_x0]
        mov [es:di], al
        mov al, [cs:box_x1]
        mov [es:di+1], al
        mov al, [cs:box_y0]
        mov [es:di+2], al
        mov al, [cs:box_y1]
        mov [es:di+3], al
        mov al, [cs:box_z0]
        mov [es:di+4], al
        mov al, [cs:box_z1]
        mov [es:di+5], al
        pop di
        pop bp
        retf

; A tile becomes seen: ES:DI, its major and minor coordinate in BL and BH;
; with the carry set it is the tile under the ray's level. Keeps the
; registers.
seen_new:
        push bx
        push cx
        mov cx, bx
        mov bl, [cs:zcur]
        sbb bl, 0                       ; BL = z
        test byte [cs:key], 1
        jz .xy
        xchg cl, ch
.xy:    or byte [es:di], SEEN_BIT       ; CL = x, CH = y
        mov byte [cs:revealed], 1
        GROW
        pop cx
        pop bx
        ret

; Walks a ray on one level until its major coordinate is the patched end
; (p_end): returns with the zero flag set when it got there, clear when the
; ray stopped before.
;   DS:SI   the ray's tile in the sight flags, ES:SI in the seen table
;   BL, BH  its major and minor coordinate, AH the major one's step
;   DH      the minor axis' part of a tile, 0-255 from the tile's edge
;           behind the ray (running towards -: one less, so that a carry is
;           a new tile either way), DL its fraction (the diagonal only)
;   BP      what a step adds to DX
;   CX      from a tile to the one below it, 0 on the ground level
; A step along the major axis alone crosses one wall: towards - that of the
; ray's own tile (it stops the ray there), towards + that of the new tile
; (seen, and the ray ends in it). A step that changes the minor axis' tile
; too is a diagonal one: see .diag.
walk:
.step:  add bl, ah
        add dx, bp
        jc .diag
..@p_neg:
        test byte [si], 0               ; towards -: the major axis' wall bit
        jnz .stop
..@p_add:
        add si, strict word 0           ; one tile on along the major axis
        mov al, [si]                    ; AL = the new tile's flags
..@p_pos:
        test al, 0                      ; towards +: the major axis' wall bit
        jz .mark
.wall:  or al, LF_OBJECT                ; seen; the ray ends as in an object
.mark:  test byte [es:si], SEEN_BIT
        jnz .under
        mov di, si
        call seen_new
.under: test al, LF_FLOOR               ; no floor: the tile below is seen too
        jnz .next
        jcxz .next
        mov di, si
        sub di, cx
        test byte [es:di], SEEN_BIT
        jnz .next
        stc
        call seen_new
.next:  test al, LF_OBJECT              ; (smoke does not hide the map: calculateFOV)
        jnz .stop
..@p_end:
        cmp bl, 0
        jne .step
        ret
.stop:  test sp, sp                     ; zero flag clear
        ret

        ; A diagonal step. The straight line takes the route whose tile
        ; edge it crosses first: along the major axis, then the minor one
        ; (route a), or the other way round (route b); through the corner
        ; it is stopped only if both routes are. A route is closed by the
        ; wall of its first step, an object in the tile between or the wall
        ; of its second step (walls belong to the tile on the + side). The
        ; major axis is crossed half way through the step, the minor one
        ; p / step before its end, where p is how far the ray now is beyond
        ; that edge: route a if 2 p < step, b if 2 p > step. p is DH,
        ; towards - DH + 1: p_h = step / 2 (towards -: step / 2 - 1), p_h2
        ; the same where the halved number is even (else 255, never met on
        ; route a): route b alone if DH > p_h, the corner if DH = p_h2.
.diag:
..@p_mstep:
        add bh, 0                       ; the minor coordinate's step
..@p_lim:
        cmp bh, 0                       ; the map's size along the minor axis
        jae .stop                       ; (also below 0)
..@p_h: cmp dh, 0
        ja .b
..@p_a1:
        test byte [word si+0], 0
        jnz .corner
..@p_a2:
        test byte [word si+0], LF_OBJECT
        jnz .corner
..@p_a3:
        test byte [word si+0], 0
        jz .open
.corner:
..@p_h2:
        cmp dh, 0
        jne .closed
.b:
..@p_b1:
        test byte [word si+0], 0
        jnz .closed
..@p_b2:
        test byte [word si+0], LF_OBJECT
        jnz .closed
..@p_b3:
        test byte [word si+0], 0
        jz .open
.closed:
        mov al, LF_OBJECT               ; the new tile is seen, the ray ends
        jmp short .to
.open:  xor al, al
.to:
..@p_addd:
        add si, strict word 0           ; one tile on along both axes
        or al, [si]
        jmp .mark

; Sets the walk up for the rays of the octant BL (key). DS = CS.
octant:
        mov [key], bl
        mov ax, [ux]                    ; AL = x, AH = y
        mov dx, [mxb]                   ; DL = mx, DH = my
        test bl, 1
        jz .xy
        xchg al, ah
        xchg dl, dh
.xy:    mov [c0], ax                    ; AL = major, AH = minor coordinate
        mov [..@p_lim+2], dh
        mov ah, 1
        test bl, 2
        jz .plus
        mov ah, -1
        mov dl, al                      ; steps to the map's edge: towards -
        jmp short .edge
.plus:  sub dl, al                      ; towards +
        dec dl
.edge:  mov [majstep], ah
        mov dh, dl
        cmp dl, RANGE / 2
        jbe .half
        mov dl, RANGE / 2
.half:  cmp dh, RANGE
        jbe .whole
        mov dh, RANGE
.whole: test bl, 2
        jz .ends
        neg dl
        neg dh
.ends:  add dl, al
        add dh, al
        mov [ends], dx
        ; AX, DX = one tile on along the major and the minor axis in the
        ; tables, CL, CH = the bit of the wall crossed; SI, DI = from a tile
        ; to the one that holds that wall: the tile itself towards -
        mov ax, 1
        mov dl, [mxb]
        xor dh, dh
        mov cx, (LF_NORTH << 8) | LF_WEST
        test bl, 1
        jz .dxy
        xchg ax, dx
        xchg cl, ch
.dxy:   xor si, si
        xor di, di
        xor bh, bh
        test bl, 2
        jz .mplus
        neg ax
        mov [..@p_neg+2], cl
        mov [..@p_pos+1], bh
        jmp short .minor
.mplus: mov si, ax
        mov [..@p_neg+2], bh
        mov [..@p_pos+1], cl
.minor: mov bh, 1
        test bl, 4
        jz .nplus
        neg dx
        mov bh, -1
        jmp short .patch
.nplus: mov di, dx
.patch: mov [..@p_mstep+2], bh
        mov [..@p_add+2], ax
        mov [..@p_a1+2], si             ; route a: the major axis' wall,
        mov [..@p_a1+4], cl
        mov [..@p_a2+2], ax             ; the tile between,
        mov [..@p_b1+2], di             ; (route b: the minor axis' wall,
        mov [..@p_b1+4], ch
        mov [..@p_b2+2], dx             ; the tile between,
        add di, ax
        mov [..@p_a3+2], di             ; the minor axis' wall from there
        mov [..@p_a3+4], ch
        add si, dx
        mov [..@p_b3+2], si             ; the major axis' wall from there)
        mov [..@p_b3+4], cl
        add ax, dx
        mov [..@p_addd+2], ax
        ret

; The rays to one ring tile (vx, vy). DS = sight flags, ES = seen table.
ray:
        push ds
        push cs
        pop ds
        mov ax, [vx]                    ; AL = vx, AH = vy
        xor bx, bx
        cmp al, RANGE
        je .xmajor
        cmp al, -RANGE
        je .xmajor
        xchg al, ah                     ; y is the major axis
        inc bx
.xmajor:
        or al, al
        jns .mplus
        or bl, 2
.mplus: or ah, ah
        jns .nplus
        or bl, 4
        neg ah
.nplus: cmp bl, [key]                   ; AH = ring tile's distance from the major axis
        je .same
        push ax
        call octant
        pop ax
.same:  mov al, ah
        cmp al, RANGE
        jb .frac
        ; The diagonal: every step a diagonal one through the corner
        ; (a carry every time, DH stays 255 = p_h = p_h2).
        mov bp, 0FFFFh
        mov dx, bp
        mov ax, bp
        jmp short .rule
.frac:  mov bx, steps
        xlatb
        mov ah, al
        xor al, al
        mov bp, ax                      ; a step adds AH to DH
        mov al, ah
        mov dx, 8000h                   ; from the middle of the tile
        test byte [key], 4
        jz .halve
        mov dh, 7Fh
        sub al, 2
.halve: shr al, 1
        mov ah, al
        jnc .rule
        mov ah, 0FFh
.rule:  mov [..@p_h+2], al
        mov [..@p_h2+2], ah
        mov bx, [c0]
        mov ax, [ends]
        cmp al, bl
        je .none                        ; the map ends at the unit's tile
        mov [..@p_end+2], al
        mov si, [pi]
        mov al, [uz]
        mov [zcur], al
        xor cx, cx
        or al, al
        jz .ground
        mov cx, [mxy]
.ground:
        mov ah, [majstep]
        pop ds
        call walk                       ; the steps all three rays share
        jnz .ret
        mov al, [cs:ends+1]
        cmp al, bl
        je .ret                         ; the map's edge
        mov [cs:..@p_end+2], al
        push si
        push dx
        push bx
        push cx
        call walk                       ; on along the unit's level
        cmp byte [cs:up], 0
        je .noup
        pop cx
        pop bx
        pop dx
        pop si
        push si
        push dx
        push bx
        push cx
        mov di, [cs:mxy]
        mov cx, di
        inc byte [cs:zcur]
        call level                      ; on up to the next level
        dec byte [cs:zcur]
.noup:  pop cx
        pop bx
        pop dx
        pop si
        jcxz .ret                       ; no level below
        mov di, cx
        neg di
        dec byte [cs:zcur]
        jnz level                       ; on down to the level below
        xor cx, cx                      ; (the ground level)
        jmp short level
.none:  pop ds
.ret:   ret

; The rest of a ray that changes level with its next step: DI = from a
; tile to the one above or below it in the tables; zcur and CX are the new
; level's, the other registers as for walk. The step: going up, a floor on
; the tile it leads to is seen from below and ends the ray, whatever the
; walls; going down the ray does not leave a tile with a floor; else the
; step is as on one level, with the new tile on the other one (the walls
; of a diagonal step are those of the level the ray leaves).
level:
        add [cs:..@p_add+2], di
        add [cs:..@p_addd+2], di
        push di
        or di, di
        js .down
        mov di, dx
        add di, bp                      ; carry: a diagonal step
        mov di, [cs:..@p_add+2]
        jnc .to
        mov di, [cs:..@p_addd+2]
.to:    add di, si                      ; the tile the step leads to (or, where
        test byte [di], LF_FLOOR        ; it leaves the map, some byte: it then
        jz .step                        ; stops at the edge either way)
        add bl, ah
        add dx, bp
        jnc .floor
        add bh, [cs:..@p_mstep+2]
        cmp bh, [cs:..@p_lim+2]
        jae .end                        ; outside the map
.floor: mov si, di
        mov al, [si]
        call walk.wall
        jmp short .end
.down:  test byte [si], LF_FLOOR
        jnz .end
.step:  mov al, bl
        add al, ah
        xchg al, [cs:..@p_end+2]        ; one step
        push ax
        call walk
        pop ax
        mov [cs:..@p_end+2], al
        jnz .end                        ; the ray stopped
        cmp bl, al
        je .end                         ; the map's edge
        pop di
        sub [cs:..@p_add+2], di
        sub [cs:..@p_addd+2], di
        jmp walk
.end:   pop di
        sub [cs:..@p_add+2], di
        sub [cs:..@p_addd+2], di
        ret

; int __cdecl reveal_fan_asm(const struct RevealFan far *p)
; Marks the tiles a unit sees from its tile as seen: rays to the
; 2 * RANGE + 1 ring tiles from (vx, vy) on, (sx, sy) to the next and turning
; at the ring's corners. Returns 1 if any tile became seen.
        global _reveal_fan_asm
_reveal_fan_asm:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        lds si, [bp+6]
        push cs
        pop es
        add si, F_PI
        mov di, pi
        mov cx, (F_END - F_PI) / 2
        cld
        rep movsw                       ; pi .. sy as in the block
        mov word [cs:left], 2 * RANGE + 1       ; (and revealed = 0)
        mov byte [cs:key], 0FFh         ; no octant set up
        mov es, [si+F_SEENSEG-F_END]
        mov ds, [si+F_LFSEG-F_END]
.ring:  call ray
        mov ax, [cs:vx]                 ; AL = vx, AH = vy
        mov cx, [cs:sx]                 ; CL = sx, CH = sy
        add al, cl
        add ah, ch
        mov [cs:vx], ax
        cmp al, ah
        je .turn
        neg ah
        cmp al, ah
        jne .on
.turn:  neg ch                          ; a corner: (sx, sy) = (-sy, sx)
        xchg cl, ch
        mov [cs:sx], cx
.on:    dec byte [cs:left]
        jnz .ring
        mov al, [cs:revealed]
        xor ah, ah
        pop ds
        pop di
        pop si
        pop bp
        retf
