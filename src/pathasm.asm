; SPDX-License-Identifier: GPL-3.0-or-later
; Path search (Dijkstra with cost buckets) in assembly; mirrors search_c and
; step_cost_idx in path.c. Large model, Watcom __cdecl.
;
; DS = walk tables, ES = search state, both at offset 0 of their segment
; with a fixed stride of TILES (= MAP_MAX_TILES in map.h).

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

TILES   equ 10000

WH      equ 0
WF      equ TILES
WC      equ 2 * TILES
WD      equ 3 * TILES           ; west wall TU low nibble, north wall high

COST    equ 0                   ; bytes, 0FFh = not reached
FROM    equ TILES               ; direction | level << 4
NEXT    equ 2 * TILES           ; words
BUCKET  equ 4 * TILES           ; words

WF_WEST  equ 01h
WF_NORTH equ 02h
WF_SOLID equ 04h
WF_LIFT  equ 08h
WF_AIR   equ 20h                ; no floor: flyers pass up or down through it
WF_OCC   equ 40h
WF_BIG   equ 80h

NO_COST  equ 0FFFFh
MAX_STEP equ 8

; parameter block (struct PathParams)
P_WALK  equ 0
P_SRCH  equ 2
P_N     equ 4
P_MX    equ 6
P_MY    equ 8
P_MZ    equ 10
P_MXY   equ 12
P_START equ 14
P_MAX   equ 16
P_OCC   equ 18                  ; byte: wf bits that bar a destination
P_FLY   equ 19                  ; byte: 1 = the unit flies (hovertanks)
P_GOAL  equ 20                  ; goal tile index (A*), 0FFFFh: none
P_GX    equ 22
P_GY    equ 24
P_KMAX  equ 26                  ; A*: highest bucket key (g + h - h0)
P_H0    equ 28                  ; A*: h of the start
P_HS    equ 30                  ; byte: h = (2 max + min) << hs
P_MIS   equ 31                  ; byte: a missile's search (with a goal): 1 of a flyer,
                                ; 2 of a walker (Pathfinding::validateUpDown: no flight up or down)
P_BEST  equ 32                  ; out (A*): the expanded tile with the lowest h

FR_START equ 80h                ; from[]: the start tile (0FFh without a goal)
FR_Q     equ 40h                ; from[]: in a bucket list (each tile is in one
                                ; at most: one next[] each)

segment PATHASM_TEXT public class=CODE

gmode   db 0                    ; 1: A* toward the goal, 2: a missile's greedy search
hsh     db 0
goal    dw 0
gx      dw 0
gy      dw 0
kmax    dw 0
h0      dw 0
ph      db 0                    ; standing height of the tile being expanded
gcur    dw 0                    ; its cost (word [ph] = ph | gcur << 8: the
                                ; direction loop's CX)
besth   dw 0
bestidx dw 0
poff    dw 0
pseg    dw 0
mx      dw 0
my      dw 0
mz      dw 0
mxy     dw 0
maxtu   dw 0
ccur    dw 0
px      dw 0
py      dw 0
pz      dw 0
pf0     db 0
occmask db WF_OCC
fly     db 0
flyv    db 0                    ; goes up and down through the air too
lhead   dw 0                    ; the direction loop's head for this tile: .inb
                                ; away from the map's edge, else .ehead
nz      dw 0
bestdh  dw 0
; The direction loop counts BP = 2 x direction - 16 (-16 .. -2, then 0 and 2
; for the lift up and down), so the word tables below are read at
; table + DIRS + BP and the loop's tail is two INCs and a sign test.
DIRS    equ 16
offs    dw 0, 0, 0, 0, 0, 0, 0, 0
dxy     db 0, -1,  1, -1,  1, 0,  1, 1,  0, 1,  -1, 1,  -1, 0,  -1, -1     ; dx, dy
; straight steps (even directions): the one wall and door to pass, on the
; tile at BX + woff (the north wall / high door nibble for N and S, the
; west wall / low nibble for E and W); woff[S] = mx, set at the start. The
; wall's bit is the low byte of the word after it (the odd directions' slot).
woff    dw 0, WF_NORTH, 1, WF_WEST, 0, WF_NORTH, 0, WF_WEST

; void __cdecl path_search_asm(const struct PathParams far *p)
        global _path_search_asm
_path_search_asm:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        lds si, [bp+6]
        mov [cs:poff], si
        mov [cs:pseg], ds
        mov ax, [si+P_MAX]
        mov [cs:kmax], ax
        mov byte [cs:gmode], 0
        mov ax, [si+P_GOAL]
        mov [cs:goal], ax
        cmp ax, NO_COST
        je .nogoal
        mov byte [cs:gmode], 1
        mov ax, [si+P_GX]
        mov [cs:gx], ax
        mov ax, [si+P_GY]
        mov [cs:gy], ax
        mov ax, [si+P_KMAX]
        mov [cs:kmax], ax
        mov ax, [si+P_H0]
        mov [cs:h0], ax
        mov al, [si+P_HS]
        mov [cs:hsh], al
        cmp byte [si+P_MIS], 0
        je .astar
        ; a missile: buckets by the step + h in TU (h0 0), see gkey
        mov byte [cs:gmode], 2
        mov byte [cs:hsh], 1
        mov word [cs:h0], 0
.astar:
        mov word [cs:besth], NO_COST
        mov ax, [si+P_START]
        mov [cs:bestidx], ax
.nogoal:
        mov ax, [si+P_MX]
        mov [cs:mx], ax
        mov ax, [si+P_MY]
        mov [cs:my], ax
        mov ax, [si+P_MZ]
        mov [cs:mz], ax
        mov ax, [si+P_MXY]
        mov [cs:mxy], ax
        mov ax, [si+P_MAX]
        mov [cs:maxtu], ax
        mov al, [si+P_OCC]
        mov [cs:occmask], al
        mov al, [si+P_FLY]
        mov [cs:fly], al
        cmp byte [si+P_MIS], 2
        jne .flyv
        xor al, al
.flyv:  mov [cs:flyv], al
        mov bx, [si+P_START]
        mov cx, [si+P_N]
        mov es, [si+P_SRCH]
        mov ds, [si+P_WALK]

        ; direction offsets: dx + dy * mx
        xor di, di
.offs:
        mov al, [cs:dxy+1+di]
        cbw
        imul word [cs:mx]
        mov dx, ax
        mov al, [cs:dxy+di]
        cbw
        add ax, dx
        mov [cs:offs+di], ax
        inc di
        inc di
        cmp di, DIRS
        jb .offs
        mov ax, [cs:mx]
        mov [cs:woff+8], ax

        ; cost[] = not reached over the map's tiles (no other is read),
        ; bucket[] = empty
        cld
        push bx
        xor di, di
        inc cx
        shr cx, 1
        mov ax, NO_COST
        rep stosw
        mov di, BUCKET
        mov cx, 256
        rep stosw
        pop bx
        mov byte [es:bx+COST], 0
        mov si, bx
        shl si, 1
        mov word [es:si+NEXT], NO_COST
        mov byte [es:bx+FROM], 0FFh
        cmp byte [cs:gmode], 0
        je .start1
        mov byte [es:bx+FROM], FR_START + FR_Q
.start1:
        mov [es:BUCKET], bx
        mov word [cs:ccur], 0

.next_cost:
        mov ax, [cs:ccur]
        cmp ax, [cs:kmax]
        jbe .pop
        jmp .finish
.pop:
        mov di, [cs:ccur]
        shl di, 1
        mov bx, [es:di+BUCKET]
        cmp bx, NO_COST
        jne .have
        inc word [cs:ccur]
        jmp .next_cost
.have:
        mov si, bx
        shl si, 1
        mov ax, [es:si+NEXT]
        mov [es:di+BUCKET], ax
        ; a tile whose cost fell while it waited in a later bucket is
        ; expanded there with its new cost (and its neighbours from it
        ; go into the current bucket at the earliest): the costs come
        ; out the cheapest all the same
        and byte [es:bx+FROM], 0FFh - FR_Q
        mov al, [es:bx+COST]
        xor ah, ah
        mov [cs:gcur], ax

        ; decode x, y, z of BX: the level by subtraction, then one 8-bit
        ; division (y < my <= 255 for every map; a 16-bit DIV is about
        ; 150 cycles on the 8088)
        mov ax, bx
        xor cx, cx
.zloop: cmp ax, [cs:mxy]
        jb .zdone
        sub ax, [cs:mxy]
        inc cx
        jmp short .zloop
.zdone: mov [cs:pz], cx
        div byte [cs:mx]
        mov dl, ah
        xor ah, ah
        mov [cs:py], ax
        xor dh, dh
        mov [cs:px], dx

        cmp byte [cs:gmode], 0
        je .decoded
        cmp bx, [cs:goal]
        jne .notgoal
        mov [cs:bestidx], bx
        jmp .finish
.notgoal:
        mov ax, dx
        mov dx, [cs:py]
        call heur
        cmp ax, [cs:besth]
        jae .decoded
        mov [cs:besth], ax
        mov [cs:bestidx], bx
.decoded:

        ; standing height of this tile
        mov al, [bx+WH]
        xor ah, ah
        or al, al
        jz .noh
        dec ax
        jmp short .hset
.noh:
        mov ax, [cs:pz]
        mov cx, 24
        mul cx
.hset:
        mov [cs:ph], al                 ; (at most 254, or 24 x 3)
        mov al, [bx+WF]
        mov [cs:pf0], al

        ; away from the map's edge all eight neighbours are on the map: no
        ; bounds checks per direction (DI = the loop's head)
        mov di, .ehead
        mov ax, [cs:px]
        dec ax
        mov cx, [cs:mx]
        sub cx, 2
        cmp ax, cx
        jae .edge
        mov ax, [cs:py]
        dec ax
        mov cx, [cs:my]
        sub cx, 2
        cmp ax, cx
        jae .edge
        mov di, .inb
.edge:  mov [cs:lhead], di
        mov cx, [cs:ph]                 ; CL = ph, CH = gcur
        mov bp, -DIRS                   ; BP = 2 x direction - 16
        jmp di

        ; The loop over the eight directions. BX = the tile, BP = the
        ; direction (above), CL = its standing height, CH = its cost, DI =
        ; the loop's head. A direction that is turned down before anything
        ; else is looked at leaves CX and DI alone and ends at .fnext; the
        ; others end at .dnext, which loads them again. (The jumps say
        ; short: NASM would make one that is out of range a pair of jumps
        ; without a word.)

.air:
        ; a flyer goes on at this level through the air (.air2)
        cmp byte [cs:fly], 0
        je short .vert
        jmp .air2

.vert:
        ; one down, then one up: the smaller height change wins (ties down)
        mov word [cs:bestdh], 99
        push si                         ; same-level index
        mov dx, 0FFFFh                  ; DX = chosen index (none)
        mov ax, [cs:pz]
        or ax, ax
        jz short .try_up
        sub si, [cs:mxy]
        dec ax
        call candidate
.try_up:
        pop si
        push si
        mov ax, [cs:pz]
        inc ax
        cmp ax, [cs:mz]
        jae short .vdone
        add si, [cs:mxy]
        call candidate
.vdone:
        pop si
        cmp dx, 0FFFFh
        jne short .vgo
        jmp .vdrop
.vgo:   mov si, dx
        jmp .cost

.ehead:
        ; on the map's edge: is this neighbour on the map?
        mov al, [cs:dxy+DIRS+bp]
        cbw
        add ax, [cs:px]
        cmp ax, [cs:mx]
        jae short .fnext
        mov al, [cs:dxy+DIRS+1+bp]
        cbw
        add ax, [cs:py]
        cmp ax, [cs:my]
        jae short .fnext
.inb:   mov si, bx
        add si, [cs:offs+DIRS+bp]       ; SI = same-level neighbour

        ; same level?
        mov al, [si+WH]
        or al, al
        jz short .air
        dec ax
        sub al, cl
        jae short .p1
        neg al
.p1:    cmp al, MAX_STEP
        ja short .vert
        ; a tile that already costs no more than this one (most of the
        ; neighbours: those expanded before) cannot get cheaper through
        ; it, nor can one that a unit bars: skip the wall and door checks
        ; (.relax would turn them down all the same)
        cmp [es:si+COST], ch
        ja short .go
.fnext: inc bp
        inc bp
        jns short .lifts_j
        jmp di
.lifts_j:
        jmp .lifts

.go:    mov al, [cs:occmask]
        test [si+WF], al
        jnz short .fnext
        test al, WF_BIG
        jnz short .bigb_j
        ; the step costs at least the tile's TU (x1.5 on a diagonal; the
        ; same level: no climb): when that is already over the limit or no
        ; cheaper than the tile's cost, .relax would turn it down whatever
        ; the walls and doors, so they are not looked at (the same answer)
        mov al, [si+WC]
        xor ah, ah
        test bp, 2                      ; odd directions are diagonal
        jz short .estr
        mov dx, ax
        shr dx, 1
        add ax, dx
        add al, ch
        adc ah, 0
        cmp ax, [cs:maxtu]
        ja short .fnext
        cmp al, [es:si+COST]            ; AX <= maxtu <= 254
        jae short .fnext
        call blocked
        jc short .dnext_k
        ; on one level: the enter cost x1.5 plus the walls crossed (stepq,
        ; here faster)
        push ax
        xor dl, dl
        call walls
        pop cx
        add ax, cx
        jmp short .relax2
.bigb_j:
        jmp .bigb
.dnext_k:
        jmp .dnext
.estr:  add al, ch
        adc ah, 0
        cmp ax, [cs:maxtu]
        ja short .fnext
        cmp al, [es:si+COST]            ; AX <= maxtu <= 254
        jae short .fnext
        mov dl, [cs:woff+DIRS+2+bp]     ; straight: its one wall
        mov di, [cs:woff+DIRS+bp]
        test [bx+di+WF], dl
        jnz short .dnext_k
        ; on one level: the enter cost plus the door nibble of that wall
        ; (as walls does)
        mov cx, ax
        mov al, [bx+di+WD]
        test dl, WF_NORTH
        jz short .dlow
        shr al, 1
        shr al, 1
        shr al, 1
        shr al, 1
.dlow:  and al, 0Fh
        xor ah, ah
        add ax, cx

.relax2:
        ; AX = the destination's cost this way, SI = the destination (not
        ; barred: .go looked, or .relax), BP = dir
        cmp ax, [cs:maxtu]
        ja short .dnext
.relax3:
        cmp al, [es:si+COST]            ; AX <= maxtu <= 254
        jae short .dnext
        mov dx, ax
        call gkey                       ; AX = its bucket
        cmp ax, [cs:kmax]
        ja short .dnext
        cmp byte [cs:gmode], 2
        jne short .gkeep
        xor dl, dl                      ; a missile: every tile costs 0, none twice
.gkeep:
        mov dh, [es:si+COST]            ; 0FFh: not reached before
        mov [es:si+COST], dl
        mov cl, [cs:pz]
        add cl, cl
        add cl, cl
        add cl, cl
        add cl, cl
        lea di, [bp+DIRS]
        shr di, 1                       ; the direction, 0 .. 9
        or cx, di
        or cl, FR_Q
        cmp dh, 0FFh
        je short .newq
        test byte [es:si+FROM], FR_Q
        mov [es:si+FROM], cl
        jnz short .dnext                ; waiting in a list: it stays there
        jmp short .queue
.newq:  mov [es:si+FROM], cl
.queue: mov di, ax
        shl di, 1
        mov dx, [es:di+BUCKET]
        mov [es:di+BUCKET], si
        mov di, si
        shl di, 1
        mov [es:di+NEXT], dx
.dnext:
        mov cx, [cs:ph]
        mov di, [cs:lhead]
        inc bp
        inc bp
        jns short .lifts
        jmp di
.lifts:
        ; BP = 0: up, 2: down
        cmp bp, 4
        jb short .lift_try
        jmp .pop
.lift_try:
        test byte [cs:pf0], WF_LIFT
        jnz short .lift_ok
        cmp byte [cs:flyv], 0
        jne short .fly_v
        jmp .pop
.fly_v:
        ; flying up into a tile without a floor, or down out of one (all
        ; four quarters of a 2x2 flyer), for 8 TU as a grav lift
        or bp, bp
        jnz short .fly_down
        mov ax, [cs:pz]
        inc ax
        cmp ax, [cs:mz]
        jae short .fly_no
        mov si, bx
        add si, [cs:mxy]
        call air4
        jmp short .fly_chk
.fly_down:
        mov ax, [cs:pz]
        or ax, ax
        jz short .fly_no
        mov si, bx
        call air4
        jc short .fly_no
        sub si, [cs:mxy]
        clc
.fly_chk:
        jc short .fly_no
        test byte [si+WF], WF_SOLID
        jnz short .fly_no
        mov ax, 8
        jmp .relax
.fly_no:
        jmp .dnext
.lift_ok:
        or bp, bp
        jnz short .lift_down
        mov ax, [cs:pz]
        inc ax
        cmp ax, [cs:mz]
        jae short .dnext
        mov si, bx
        add si, [cs:mxy]
        jmp short .lift_check
.lift_down:
        mov ax, [cs:pz]
        or ax, ax
        jz short .fly_no
        mov si, bx
        sub si, [cs:mxy]
.lift_check:
        test byte [si+WF], WF_LIFT
        jz short .fly_no
        mov ax, 8
        jmp .relax

.bigb:
        ; a 2x2 unit pays the average of its parts: when even the tiles'
        ; TU alone (x1.5 on a diagonal, at worst 2 less for the rounding)
        ; are over the limit or no cheaper, .relax would turn it down
        xor ah, ah
        mov al, [si+WC]
        mov cx, ax
        mov al, [si+WC+1]
        add cx, ax
        mov di, si
        add di, [cs:mx]
        mov al, [di+WC]
        add cx, ax
        mov al, [di+WC+1]
        add cx, ax
        test bp, 2
        jz short .bg1
        mov ax, cx
        shr ax, 1
        add cx, ax
        sub cx, 2
.bg1:   shr cx, 1
        shr cx, 1
        add cx, [cs:gcur]
        cmp cx, [cs:maxtu]
        ja short .dnext_j
        cmp cl, [es:si+COST]            ; CX <= maxtu <= 254
        jae short .dnext_j
        call blocked
        jc short .dnext_j
        mov ax, [cs:pz]
        mov [cs:nz], ax
        jmp short .big
.dnext_j:
        jmp .dnext

.vdrop:
        ; no level up or down: a drop on this level, as a flyer's step
        ; (path.c; OpenXcom bars only a climb of more than 8)
        mov al, [si+WH]
        or al, al
        jz short .dnext_j
        dec ax
        add al, MAX_STEP
        cmp al, [cs:ph]
        jae short .dnext_j
        jmp short .air3

.air2:
        ; (Pathfinding: a tile without a floor costs 4 to fly into)
        test byte [si+WF], WF_SOLID
        jz short .air3
        jmp .vert
.air3:  call blocked
        jc short .dnext_j
        mov ax, [cs:pz]
        mov [cs:nz], ax

.cost:
        ; (as above: no cheaper this way, or barred)
        mov al, [es:si+COST]
        cmp al, [cs:gcur]
        jbe short .dnext_j
        mov al, [cs:occmask]
        test [si+WF], al
        jnz short .dnext_j
        test al, WF_BIG
        jnz short .big
        call stepq                      ; a level up or down, or a flyer
        jmp short .relax
.big:   call bigstep

.relax:
        ; AX = step cost, SI = destination, [nz] = its level, BP = dir
        add ax, [cs:gcur]
        cmp ax, [cs:maxtu]
        ja short .dnext_j
        mov cl, [cs:occmask]
        test [si+WF], cl
        jnz short .dnext_j
        jmp .relax3

.finish:
        mov ax, [cs:bestidx]
        mov ds, [cs:pseg]
        mov si, [cs:poff]
        mov [si+P_BEST], ax
        pop ds
        pop di
        pop si
        pop bp
        retf

; AX = cost g of the tile entered in direction BP (the loop's) from (px, py) ->
; AX = its bucket: g, with a goal g + h - h0; no lower than the current
; bucket; a missile (g the step's TU, h0 0): the step + h, the current bucket
; going down to it (PathfindingNode::connect, tuCost + tuGuess, by a heap
; in OpenXcom), far tiles in the last bucket. Preserves BX, DX, SI.
gkey:
        cmp byte [cs:gmode], 0
        je .cl
        push dx
        push ax
        mov cx, [cs:px]
        mov dx, [cs:py]
        or bp, bp
        jns .h                          ; a lift: the same x, y
        mov al, [cs:dxy+DIRS+bp]
        cbw
        add cx, ax
        mov al, [cs:dxy+DIRS+1+bp]
        cbw
        add dx, ax
.h:     mov ax, cx
        call heur
        pop cx
        add ax, cx
        sub ax, [cs:h0]
        pop dx
.cl:    cmp byte [cs:gmode], 2
        je .mis
        cmp ax, [cs:ccur]
        jge .r
        mov ax, [cs:ccur]
.r:     ret
.mis:   cmp ax, [cs:kmax]
        jbe .m1
        mov ax, [cs:kmax]
.m1:    cmp ax, [cs:ccur]
        jae .r
        mov [cs:ccur], ax
        ret

; AX = x, DX = y -> AX = (2 max(|x - gx|, |y - gy|) + min(...)) << hs:
; octile TU (4 straight, 6 diagonal) when no tile costs less than 4.
; Clobbers CX, DX.
heur:
        sub ax, [cs:gx]
        jns .a
        neg ax
.a:     sub dx, [cs:gy]
        jns .b
        neg dx
.b:     cmp ax, dx
        jae .c
        xchg ax, dx
.c:     shl ax, 1
        add ax, dx
        mov cl, [cs:hsh]
        shl ax, cl
        ret

; Vertical candidate: SI = index on level AX. Updates DX/[bestdh] when it
; is standable, within the step height, closer than the best so far and not
; blocked at either level. Preserves BX.
candidate:
        mov cl, [si+WH]
        or cl, cl
        jz .no
        dec cl
        sub cl, [cs:ph]
        jae .c1
        neg cl
.c1:    xor ch, ch
        cmp cx, MAX_STEP
        ja .no
        cmp cx, [cs:bestdh]
        jae .no
        push ax
        push cx
        call blocked                    ; at our level
        jc .nopop
        push bx
        cmp ax, [cs:pz]                 ; the candidate's level
        jb .down
        add bx, [cs:mxy]
        jmp short .chk
.down:  sub bx, [cs:mxy]
.chk:   call blocked
        pop bx
        jc .nopop
        pop cx
        pop ax
        mov [cs:bestdh], cx
        mov [cs:nz], ax
        mov dx, si
        ret
.nopop:
        pop cx
        pop ax
.no:    ret

; CF = 1 unless tile SI (and for a 2x2 flyer, whose occmask has WF_BIG,
; SI + 1, SI + mx, SI + mx + 1) has no floor. Preserves SI.
air4:
        test byte [si+WF], WF_AIR
        jz .no
        test byte [cs:occmask], WF_BIG
        jz .yes
        push si
        test byte [si+WF+1], WF_AIR
        jz .nop
        add si, [cs:mx]
        test byte [si+WF], WF_AIR
        jz .nop
        test byte [si+WF+1], WF_AIR
        jz .nop
        pop si
.yes:   clc
        ret
.nop:   pop si
.no:    stc
        ret

; CF = 1 if walls (or, for diagonals, corners) block the step from tile BX
; in direction BP (the loop's). Preserves BX, SI, AX, DX, DI.
blocked:
        push ax
        push si
        mov ax, [cs:dxy+DIRS+bp]        ; AL = dx, AH = dy
        or al, al
        jz .only_y
        or ah, ah
        jz .only_x
        ; diagonal: WX(i) || WY(i) || WY(ix) || WX(iy) || solid(ix|iy)
        mov si, bx
        call wallx
        jc .yes
        call wally
        jc .yes
        mov si, bx
        test al, al
        js .ixm
        inc si
        jmp short .ixd
.ixm:   dec si
.ixd:   test byte [si+WF], WF_SOLID
        jnz .yes
        call wally
        jc .yes
        mov si, bx
        test ah, ah
        js .iym
        add si, [cs:mx]
        jmp short .iyd
.iym:   sub si, [cs:mx]
.iyd:   test byte [si+WF], WF_SOLID
        jnz .yes
        call wallx
        jc .yes
        jmp short .no
.only_x:
        mov si, bx
        call wallx
        jc .yes
        jmp short .no
.only_y:
        mov si, bx
        call wally
        jc .yes
.no:    clc
        pop si
        pop ax
        ret
.yes:   stc
        pop si
        pop ax
        ret

; CF = 1 if a west wall blocks the x step (AL = dx) out of tile SI.
wallx:
        test al, al
        js .m
        test byte [si+WF+1], WF_WEST
        jmp short .r
.m:     test byte [si+WF], WF_WEST
.r:     jz .clear
        stc
        ret
.clear: clc
        ret

; CF = 1 if a north wall blocks the y step (AH = dy) out of tile SI.
wally:
        test ah, ah
        js .m
        push si
        add si, [cs:mx]
        test byte [si+WF], WF_NORTH
        pop si
        jmp short .r
.m:     test byte [si+WF], WF_NORTH
.r:     jz .clear
        stc
        ret
.clear: clc
        ret

; AX = the cost of one part's step from tile BX to tile SI in direction BP
; (path.c step_part, Pathfinding::getTUCost): the enter cost, +1 to climb a
; level, x1.5 for diagonals, plus the walls crossed, read one level up on a
; climb, the destination's not on the way down. Preserves BX, SI. Clobbers
; CX, DX, DI.
stepq:
        push bx
        mov al, [si+WC]
        xor ah, ah
        xor dl, dl
        mov cx, [cs:nz]
        cmp cx, [cs:pz]
        je .f
        ja .up
        inc dx
        jmp short .f
.up:    inc ax
        add bx, [cs:mxy]
.f:     test bp, 2
        jz .s
        mov cx, ax
        shr cx, 1
        add ax, cx
.s:     push ax
        call walls
        pop cx
        add ax, cx
        pop bx
        ret

; AX = a 2x2 unit's step: the average of its four parts (getTUCost's
; totalCost / 4). Preserves BX, SI. Clobbers CX, DX, DI.
bigstep:
        push bx
        push si
        call stepq
        push ax
        inc bx
        inc si
        call stepq
        pop cx
        add ax, cx
        push ax
        add bx, [cs:mx]
        add si, [cs:mx]
        call stepq
        pop cx
        add ax, cx
        push ax
        dec bx
        dec si
        call stepq
        pop cx
        add ax, cx
        shr ax, 1
        shr ax, 1
        pop si
        pop bx
        ret

; AX = TU of the walls crossed from tile BX to tile SI in direction BP, DL != 0:
; not the destination's (Pathfinding::getTUCost 392-435): the north wall of BX
; going north, else of SI, the west wall of SI going east, else of BX. Two on
; a diagonal: the first and half the other. Clobbers CX, DX, DI.
walls:
        mov cx, [cs:dxy+DIRS+bp]        ; CL = dx, CH = dy
        xor ax, ax
        test cl, cl
        jz .n
        mov di, bx
        js .w
        mov di, si
        test dl, dl
        jnz .n
.w:     mov al, [di+WD]
        and al, 0Fh
.n:     test ch, ch
        jz .c
        mov di, bx
        js .nn
        mov di, si
        test dl, dl
        jnz .c
.nn:    mov ah, [di+WD]
        mov cl, 4
        shr ah, cl
.c:     cmp bp, 2 * 3 - DIRS            ; south-east: the west wall first
        jne .c2
        xchg al, ah
.c2:    test ah, ah
        jz .c3
        shr al, 1
        add al, ah
.c3:    xor ah, ah
        ret
