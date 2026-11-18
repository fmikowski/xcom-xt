; SPDX-License-Identifier: GPL-3.0-or-later
; The voxel line in assembly: TileEngine::calculateLine with voxel checks
; (src/vox.c vox_line). Large model, Watcom __cdecl.
;
; The run walks the line in OpenXcom's frame (the major axis, two minor
; ones, Bresenham drifts) through the real voxel axes and checks every
; voxel it enters against the LOFT shapes of the tile's four parts and the
; unit there. Where a tile may hold a unit or a grav lift, the run first
; returns R_CALL so that C (vox.c tile_info) fills in what counts there,
; then goes on with that voxel's check (resume = 1). Voxels in layers of a
; tile that hold nothing are passed in one go: the steps up to the next
; tile edge or filled layer follow from the drifts by a multiply and a
; divide.
;
; int __cdecl vox_run(struct VoxRun far *r, int resume)
;   0-3 the part hit, 4 a unit, R_OUT (left the map), R_CALL, -1 the end.
; The block lives in DGROUP (= SS): BP addresses it. The line's origin must
; be on the map (vox.c checks it).

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

R_OUT   equ 5
R_CALL  equ 6

; struct VoxRun (vox.c)
V_MAPSEG  equ 0
V_UNITSEG equ 2
V_VOXSEG  equ 4
V_MCDSEG  equ 6
V_MCDOFF  equ 8                 ; offset of g_pak.mcd
V_BASE    equ 10                ; offsets in the voxel blob: its start,
V_COLS    equ 12                ;   the columns' offsets, ...
V_TMASK   equ 14
V_TVAL    equ 16
V_GEN     equ 18
V_ROWS    equ 20
V_MX      equ 22
V_MY      equ 24
V_MZ      equ 26
V_MXY     equ 28
V_EXCL    equ 30                ; excluded unit + 1, 0 none, 255 no units
V_PHASE   equ 31
V_TX      equ 32
V_TY      equ 34
V_TZ      equ 36
V_TI      equ 38
V_OX      equ 40
V_OY      equ 41
V_OZ      equ 42
V_TFL     equ 43                ; TF_*
V_COL     equ 44                ; 4 column offsets (0 none)
V_MOVE    equ 52                ; 3 move routines: major, minor 1, minor 2
V_AXIS    equ 58                ; their axis codes: 2 * axis + (step < 0)
V_EXT     equ 61                ; the line goes on past the target
V_N       equ 62                ; main steps left
V_D0      equ 64                ; deltas of the three roles
V_D1      equ 66
V_D2      equ 68
V_R1      equ 70
V_R2      equ 72
V_K       equ 74                ; main steps done
V_SP      equ 76
V_UBOT    equ 78                ; the unit in the tile: its voxels above
V_UTOP    equ 80                ;   UBOT up to UTOP,
V_UTPL    equ 82                ;   its shape
V_LMASK   equ 84                ; layers of the tile that hold voxels
V_RV      equ 86                ; the parts' rows at the voxel (rows)
V_RVA     equ 94                ;   ORed
V_URV     equ 96                ; the unit's row
V_KEY     equ 98                ; layer * 16 + y of V_RV, 255 none
V_UKEY    equ 99                ; y of V_URV, 255 none
V_O       equ 100               ; the line: origin, target (3 words each)
V_T       equ 106
V_F0      equ 112               ; the origin by role
V_HIT     equ 118               ; the voxel where the run stopped

TF_CALL   equ 1                 ; C has to fill in the unit and lift
TF_PARTS  equ 2
TF_UNIT   equ 4
TF_LIFT   equ 8                 ; the floor is a grav lift's that stops lines

MCD_GRAVLIFT equ 20h

segment VOXASM_TEXT public class=CODE

; bit y (row mask) and bit 15 - x (row bits) by voxel offset
ybit    dw 1, 2, 4, 8, 10h, 20h, 40h, 80h, 100h, 200h, 400h, 800h, 1000h, 2000h, 4000h, 8000h
xbit    dw 8000h, 4000h, 2000h, 1000h, 800h, 400h, 200h, 100h, 80h, 40h, 20h, 10h, 8, 4, 2, 1
moves   dw mv_xp, mv_xm, mv_yp, mv_ym, mv_zp, mv_zm
rooms   dw rm_xp, rm_xm, rm_yp, rm_ym, rm_zp, rm_zm

; The tile at V_TI: its parts' columns and whether C must add to them.
enter_tile:
        push si
        push di
        mov di, [bp+V_TI]
        shl di, 1
        shl di, 1
        mov es, [bp+V_MAPSEG]
        xor dx, dx              ; DL: flags
        mov word [bp+V_KEY], 0FFFFh     ; no rows cached
        mov [bp+V_LMASK], dx
        mov ax, [es:di]         ; no parts at all (open air): no columns
        or ax, [es:di+2]
        jnz .parts
        mov [bp+V_COL], ax
        mov [bp+V_COL+2], ax
        mov [bp+V_COL+4], ax
        mov [bp+V_COL+6], ax
        mov si, [bp+V_TI]
        jmp short .units
.parts: xor si, si              ; SI: part * 2
.part:  mov word [bp+V_COL+si], 0
        mov bx, si
        shr bx, 1
        mov al, [es:di+bx]
        or al, al
        jz .next
        xor ah, ah
        mov cl, 4
        shl ax, cl
        add ax, [bp+V_MCDOFF]
        mov bx, ax
        push es
        mov es, [bp+V_MCDSEG]
        mov al, [es:bx+6]       ; the record's column (Mcd.vcol)
        pop es
        or al, al
        jz .next
        xor ah, ah
        shl ax, 1
        shl ax, 1
        add ax, [bp+V_COLS]
        mov bx, ax
        mov ax, [bx+2]          ; its layers
        or [bp+V_LMASK], ax
        mov bx, [bx]            ; its runs
        add bx, [bp+V_BASE]
        mov [bp+V_COL+si], bx
        or dl, TF_PARTS
.next:  inc si
        inc si
        cmp si, 8
        jb .part
        mov si, [bp+V_TI]
        ; a grav lift floor: C decides
        mov al, [es:di]
        or al, al
        jz .units
        mov bl, 16
        mul bl
        mov bx, ax
        add bx, [bp+V_MCDOFF]
        mov es, [bp+V_MCDSEG]
        test byte [es:bx+8], MCD_GRAVLIFT
        jnz .call
.units: mov al, [bp+V_EXCL]
        cmp al, 255
        je .done
        mov es, [bp+V_UNITSEG]
        mov ah, [es:si]
        or ah, ah
        jz .below
        cmp ah, al
        jne .call
        jmp short .done
.below: cmp word [bp+V_TZ], 0
        je .done
        sub si, [bp+V_MXY]
        mov ah, [es:si]
        or ah, ah
        jz .done
        cmp ah, al
        je .done
.call:  or dl, TF_CALL
.done:  mov [bp+V_TFL], dl
        pop di
        pop si
        ret

; Moves one voxel along an axis; CF set when it left the map.
mv_xp:  inc byte [bp+V_OX]
        cmp byte [bp+V_OX], 16
        jb mv_in
        mov byte [bp+V_OX], 0
        inc word [bp+V_TX]
        inc word [bp+V_TI]
        mov ax, [bp+V_TX]
        cmp ax, [bp+V_MX]
        jae mv_out
        jmp short mv_new
mv_xm:  dec byte [bp+V_OX]
        jns mv_in
        mov byte [bp+V_OX], 15
        dec word [bp+V_TI]
        dec word [bp+V_TX]
        js mv_out
        jmp short mv_new
mv_yp:  inc byte [bp+V_OY]
        cmp byte [bp+V_OY], 16
        jb mv_in
        mov byte [bp+V_OY], 0
        mov ax, [bp+V_MX]
        add [bp+V_TI], ax
        inc word [bp+V_TY]
        mov ax, [bp+V_TY]
        cmp ax, [bp+V_MY]
        jae mv_out
        jmp short mv_new
mv_ym:  dec byte [bp+V_OY]
        jns mv_in
        mov byte [bp+V_OY], 15
        mov ax, [bp+V_MX]
        sub [bp+V_TI], ax
        dec word [bp+V_TY]
        js mv_out
        jmp short mv_new
mv_zp:  inc byte [bp+V_OZ]
        cmp byte [bp+V_OZ], 24
        jb mv_in
        mov byte [bp+V_OZ], 0
        mov ax, [bp+V_MXY]
        add [bp+V_TI], ax
        inc word [bp+V_TZ]
        mov ax, [bp+V_TZ]
        cmp ax, [bp+V_MZ]
        jae mv_out
        jmp short mv_new
mv_zm:  dec byte [bp+V_OZ]
        jns mv_in
        mov byte [bp+V_OZ], 23
        mov ax, [bp+V_MXY]
        sub [bp+V_TI], ax
        dec word [bp+V_TZ]
        js mv_out
mv_new: call enter_tile
mv_in:  clc
        ret
mv_out: stc
        ret

; AX = V_LMASK >> (layer of V_OZ).
lmask_here:
        mov ax, [bp+V_LMASK]
        mov cl, [bp+V_OZ]
        shr cl, 1
        shr ax, cl
        ret

; Moves along an axis that stay in the tile and in its empty layers (AX);
; the voxel's own layer is empty.
rm_xp:  mov al, 15
        sub al, [bp+V_OX]
        jmp short rm_b
rm_xm:  mov al, [bp+V_OX]
        jmp short rm_b
rm_yp:  mov al, 15
        sub al, [bp+V_OY]
        jmp short rm_b
rm_ym:  mov al, [bp+V_OY]
rm_b:   xor ah, ah
        ret
rm_zp:  call lmask_here         ; up: the rest of this layer, then empty ones
        mov dx, ax
        mov al, [bp+V_OZ]
        mov bl, al
        and al, 1
        xor al, 1               ; the voxel above in the same layer
        xor ah, ah
        shr bl, 1
.up:    inc bl
        cmp bl, 12
        jae .done
        shr dx, 1
        test dx, 1
        jnz .done
        add ax, 2
        jmp short .up
.done:  ret
rm_zm:  call lmask_here         ; down: the same below
        mov dx, [bp+V_LMASK]
        mov al, [bp+V_OZ]
        mov bl, al
        and al, 1               ; the voxel below in the same layer
        xor ah, ah
        shr bl, 1
.dn:    or bl, bl
        jz .done
        dec bl
        mov cl, bl
        mov si, dx
        shr si, cl
        test si, 1
        jnz .done
        add ax, 2
        jmp short .dn
.done:  ret

; AX = the room of role SI (keeps DI, SI).
room:   push si
        mov bl, [bp+V_AXIS+si]
        xor bh, bh
        shl bx, 1
        call [cs:rooms+bx]
        pop si
        ret

; DI = the main steps that keep minor role %3 (delta %1, drift %2) in its
; room: (room * d0 + r) / d.
%macro ROOM 3
        cmp word [bp+%1], 0
        je %%end
        mov si, %3
        call room
        mul word [bp+V_D0]
        add ax, [bp+%2]
        adc dx, 0
        div word [bp+%1]
        cmp ax, di
        jae %%end
        mov di, ax
%%end:
%endmacro

; Minor role %3 (drift %1, delta %2) after DI main steps:
; t = s d - r; c = ceil(t / d0) steps, the drift c d0 - t (or -t).
%macro MINOR 3
        mov ax, di
        mul word [bp+%2]
        sub ax, [bp+%1]
        jbe %%no
        mov cx, ax
        add ax, [bp+V_D0]
        dec ax
        xor dx, dx
        div word [bp+V_D0]
        push ax
        mul word [bp+V_D0]
        sub ax, cx
        mov [bp+%1], ax
        pop ax
        mov si, %3
        call shift
        jmp short %%end
%%no:   neg ax
        mov [bp+%1], ax
%%end:
%endmacro

; Passes the voxels of empty layers: the main steps (and the minor ones
; they take) that stay in the tile without reaching a filled layer.
skip:   xor si, si
        call room
        mov di, ax              ; DI: steps that may be passed
        cmp di, [bp+V_N]
        jbe .m1
        mov di, [bp+V_N]
.m1:    or di, di
        jz .none
        ROOM V_D1, V_R1, 1
        ROOM V_D2, V_R2, 2
        or di, di
        jz .none
        sub [bp+V_N], di        ; s main steps
        add [bp+V_K], di
        mov ax, di
        xor si, si
        call shift
        MINOR V_R1, V_D1, 1
        MINOR V_R2, V_D2, 2
.none:  ret

; AL moves along the axis of role SI (in the tile: no tile change).
shift:  push si
        mov bl, [bp+V_AXIS+si]
        mov cl, bl
        shr bl, 1
        xor bh, bh
        mov si, bx              ; SI: axis, CL bit 0: backwards
        test cl, 1
        jz .f
        neg al
.f:     add [bp+V_OX+si], al
        pop si
        ret

; Checks the voxel: returns (ZF set) when it is empty, else leaves the run
; with what is there, or R_CALL. The parts' rows at the voxel's layer and
; y stay cached until the tile, layer or row changes.
check:
        mov dl, [bp+V_TFL]
        test dl, TF_CALL
        jnz .call
        test dl, TF_PARTS | TF_UNIT | TF_LIFT
        jz .empty
        test dl, TF_LIFT
        jz .t
        cmp byte [bp+V_OZ], 2
        jb .lift
.t:     mov bl, [bp+V_OX]
        xor bh, bh
        shl bx, 1
        mov cx, [cs:xbit+bx]    ; CX: the voxel's bit in a row
        test dl, TF_PARTS
        jz .u
        mov al, [bp+V_OZ]       ; key: layer * 16 + y
        and al, 0FEh
        shl al, 1
        shl al, 1
        shl al, 1
        or al, [bp+V_OY]
        cmp al, [bp+V_KEY]
        je .have
        mov [bp+V_KEY], al
        call rows
.have:  test [bp+V_RVA], cx
        jz .u
        xor si, si
.part:  test [bp+V_RV+si], cx
        jnz .hit
        inc si
        inc si
        jmp short .part
.u:     test dl, TF_UNIT
        jz .empty
        mov al, [bp+V_TZ]       ; the voxel's height against the unit's
        mov ah, 24
        mul ah
        add al, [bp+V_OZ]
        adc ah, 0
        cmp ax, [bp+V_UBOT]
        jle .empty
        cmp ax, [bp+V_UTOP]
        jg .empty
        mov al, [bp+V_OY]
        cmp al, [bp+V_UKEY]
        je .uh
        mov [bp+V_UKEY], al
        mov al, [bp+V_UTPL]
        call trow
        mov [bp+V_URV], ax
.uh:    test [bp+V_URV], cx
        jz .empty
        mov al, 4
        jmp leave
.empty: xor ax, ax              ; ZF set
        ret
.hit:   mov ax, si
        shr ax, 1
        jmp leave
.lift:  xor al, al
        jmp leave
.call:  mov al, R_CALL
        jmp leave

; The four parts' rows at the voxel's layer and y, and all of them ORed
; (keeps CX, DX).
rows:   mov bl, [bp+V_OZ]
        shr bl, 1
        xor bh, bh              ; BX: layer
        xor si, si
        mov [bp+V_RVA], si
.p:     xor ax, ax
        mov di, [bp+V_COL+si]
        or di, di
        jz .s
.r:     cmp bl, [di]            ; the run that holds the layer
        jbe .f
        inc di
        inc di
        jmp short .r
.f:     mov al, [di+1]          ; template
        call trow
.s:     mov [bp+V_RV+si], ax
        or [bp+V_RVA], ax
        inc si
        inc si
        cmp si, 8
        jb .p
        ret

; AX = row y (V_OY) of template AL (keeps BX, CX, DX, SI).
trow:   or al, al
        jz .no
        xor ah, ah
        mov di, ax
        shl di, 1
        add di, [bp+V_TMASK]
        push bx
        mov bl, [bp+V_OY]
        xor bh, bh
        shl bx, 1
        mov bx, [cs:ybit+bx]
        test [di], bx
        pop bx
        jz .no
        mov di, ax
        add di, [bp+V_TVAL]
        mov al, [di]
        test al, 80h
        jz .row
        and al, 7Fh             ; a general template: its row's index
        shl ax, 1
        shl ax, 1
        shl ax, 1
        shl ax, 1
        add al, [bp+V_OY]
        add ax, [bp+V_GEN]
        mov di, ax
        mov al, [di]
        xor ah, ah
.row:   mov di, ax
        shl di, 1
        add di, [bp+V_ROWS]
        mov ax, [di]
        ret
.no:    xor ax, ax
        ret

; The line in OpenXcom's frame: the far end (V_EXT), the roles' axes and
; deltas (x and y swapped if y is longer, then x and z if z is longer
; than that), the drifts, the origin's tile.
setup:  cmp byte [bp+V_EXT], 0
        je .frame
        xor di, di              ; DI: the longest delta
        xor si, si
.m:     mov ax, [bp+V_T+si]
        sub ax, [bp+V_O+si]
        cwd
        xor ax, dx
        sub ax, dx
        cmp ax, di
        jbe .m2
        mov di, ax
.m2:    inc si
        inc si
        cmp si, 6
        jb .m
        or di, di
        jz .frame
        mov ax, 1699            ; n = ceil(1700 / m): on out of any map
        add ax, di
        xor dx, dx
        div di
        mov cx, ax
        xor si, si
.e:     mov ax, [bp+V_T+si]
        sub ax, [bp+V_O+si]
        imul cx
        add ax, [bp+V_O+si]
        mov [bp+V_T+si], ax
        inc si
        inc si
        cmp si, 6
        jb .e
.frame: xor si, si              ; |delta| and direction by real axis
.d:     mov ax, [bp+V_T+si]
        sub ax, [bp+V_O+si]
        mov di, si
        shr di, 1
        mov cx, si              ; code 2 * axis
        or ax, ax
        jns .p
        neg ax
        inc cl
.p:     mov [bp+V_D0+si], ax
        mov [bp+V_AXIS+di], cl
        inc si
        inc si
        cmp si, 6
        jb .d
        mov ax, [bp+V_D1]
        cmp ax, [bp+V_D0]
        jbe .s2
        xchg ax, [bp+V_D0]
        mov [bp+V_D1], ax
        mov ax, [bp+V_AXIS]
        xchg al, ah
        mov [bp+V_AXIS], ax
.s2:    mov ax, [bp+V_D2]
        cmp ax, [bp+V_D0]
        jbe .s3
        xchg ax, [bp+V_D0]
        mov [bp+V_D2], ax
        mov al, [bp+V_AXIS]
        xchg al, [bp+V_AXIS+2]
        mov [bp+V_AXIS], al
.s3:    xor si, si              ; the origin by role, the moves
.f:     mov bl, [bp+V_AXIS+si]
        xor bh, bh
        mov di, bx
        shl bx, 1
        mov ax, [cs:moves+bx]
        and di, 0FEh
        mov cx, [bp+V_O+di]
        mov di, si
        shl di, 1
        mov [bp+V_MOVE+di], ax
        mov [bp+V_F0+di], cx
        inc si
        cmp si, 3
        jb .f
        mov ax, [bp+V_D0]
        mov [bp+V_N], ax
        shr ax, 1
        mov [bp+V_R1], ax
        mov [bp+V_R2], ax
        mov word [bp+V_K], 0
        mov ax, [bp+V_O]        ; the origin's tile
        mov bl, al
        and bl, 15
        mov [bp+V_OX], bl
        mov cl, 4
        shr ax, cl
        mov [bp+V_TX], ax
        mov ax, [bp+V_O+2]
        mov bl, al
        and bl, 15
        mov [bp+V_OY], bl
        shr ax, cl
        mov [bp+V_TY], ax
        mov ax, [bp+V_O+4]
        xor dx, dx
        mov bx, 24
        div bx
        mov [bp+V_TZ], ax
        mov [bp+V_OZ], dl
        mul word [bp+V_MY]
        add ax, [bp+V_TY]
        mul word [bp+V_MX]
        add ax, [bp+V_TX]
        mov [bp+V_TI], ax
        jmp enter_tile

; Leaves the run with AL (from anywhere inside it).
leave:  mov sp, [bp+V_SP]
        cbw
        jmp done

; The main voxel: empty layers of a plain tile are passed first.
main:   mov byte [bp+V_PHASE], 0
        test byte [bp+V_TFL], TF_CALL | TF_UNIT | TF_LIFT
        jnz chk0
        call lmask_here
        test al, 1
        jnz chk0
        call skip
chk0:   call check
after0: cmp word [bp+V_N], 0
        je run_end
        dec word [bp+V_N]
        mov ax, [bp+V_R2]
        sub ax, [bp+V_D2]
        mov [bp+V_R2], ax
        mov ax, [bp+V_R1]
        sub ax, [bp+V_D1]
        mov [bp+V_R1], ax
        jns after1
        add ax, [bp+V_D0]
        mov [bp+V_R1], ax
        call [bp+V_MOVE+2]
        jc run_out
        mov byte [bp+V_PHASE], 1
chk1:   call check
after1: cmp word [bp+V_R2], 0
        jge after2
        mov ax, [bp+V_D0]
        add [bp+V_R2], ax
        call [bp+V_MOVE+4]
        jc run_out
        mov byte [bp+V_PHASE], 2
chk2:   call check
after2: inc word [bp+V_K]
        call [bp+V_MOVE]
        jnc main
run_out:
        mov al, R_OUT
        jmp leave
run_end:
        mov ax, -1
        jmp short done

        global _vox_run
_vox_run:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        mov ax, [bp+6]          ; offset of the block (its segment is SS)
        mov cx, [bp+10]         ; resume
        mov bp, ax
        mov [bp+V_SP], sp
        mov ds, [bp+V_VOXSEG]
        jcxz .start
        mov al, [bp+V_PHASE]    ; the voxel C has just filled in
        cmp al, 1
        je .p1
        cmp al, 2
        je .p2
        jmp chk0
.p1:    jmp chk1
.p2:    jmp chk2
.start: call setup
        jmp main
done:   xchg ax, cx             ; V_HIT: tile * size + offset per axis
        xor bh, bh
        mov ax, [bp+V_TX]
        mov dl, 4
        xchg cx, dx
        shl ax, cl
        xchg cx, dx
        mov bl, [bp+V_OX]
        add ax, bx
        mov [bp+V_HIT], ax
        mov ax, [bp+V_TY]
        xchg cx, dx
        shl ax, cl
        xchg cx, dx
        mov bl, [bp+V_OY]
        add ax, bx
        mov [bp+V_HIT+2], ax
        mov ax, [bp+V_TZ]
        mov bx, 24
        imul bx
        mov bl, [bp+V_OZ]
        xor bh, bh
        add ax, bx
        mov [bp+V_HIT+4], ax
        xchg ax, cx
        pop ds
        pop di
        pop si
        pop bp
        retf
