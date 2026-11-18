; SPDX-License-Identifier: GPL-3.0-or-later
; XTUMB.SYS: a minimal XMS driver for an 8088 that only hands out one upper
; memory block (XMS function 10h / 11h), for testing xcom-xt's UMB path in
; 86Box (the Xi8088 machine maps RAM into C000-EFFF; no other XT machine
; has RAM there). It plays the part of an XT UMB board's driver.
;
;   DEVICE=XTUMB.SYS [seg [paras [port [port...]]]]   hex; default D000 1000 (64 KB at D000)
;
; With a port, the block is the page frame of a Lo-tech style EMS board
; (four 16 KB windows, page registers at port .. port + 3): the driver maps
; pages 0-3 into the frame once, so the frame acts as plain upper memory.
; Each further port is another board (its frame the next 64 KB), so two
; boards with frames D000 and E000 make one 128 KB block.
;
; Loaded a second time, the driver chains to the first (or to any XMS
; driver before it): requests it cannot serve go there, so DOS 5 and the
; game see two UMBs (xcom-xt's several-block path).
;
; The block is checked for RAM at start-up (first and last paragraph); with
; no RAM there the driver does not stay. Functions: 00h version (2.00, no
; HMA), 10h request UMB, 11h release UMB; everything else answers "not
; implemented" (BL = 80h). DOS 5's DOS=UMB may also take the block.
; Build: nasm -f bin -o XTUMB.SYS tools\xtumb.asm

        bits 16
        cpu 8086
        org 0

header: dd -1
        dw 8000h                        ; character device
        dw strategy
        dw interrupt
        db 'XTUMB$$$'

reqhdr  dd 0
old2f   dd 0
prevxms dd 0                            ; the XMS driver loaded before, 0 = none
umbseg  dw 0D000h
umbsize dw 1000h
umbused db 0

strategy:
        mov [cs:reqhdr], bx
        mov [cs:reqhdr + 2], es
        retf

interrupt:
        push ax
        push bx
        push es
        les bx, [cs:reqhdr]
        cmp byte [es:bx + 2], 0         ; INIT
        jne .other
        call init
        jmp .out
.other: mov word [es:bx + 3], 0100h     ; done
.out:   pop es
        pop bx
        pop ax
        retf

int2f:
        cmp ax, 4300h
        jne .n1
        mov al, 80h
        iret
.n1:    cmp ax, 4310h
        jne .chain
        push cs
        pop es
        mov bx, xms
        iret
.chain: jmp far [cs:old2f]

xms:    jmp short .go                   ; XMS entry: hookable prologue
        nop
        nop
        nop
.go:    or ah, ah
        jnz .f10
        mov ax, 0200h
        xor bx, bx
        xor dx, dx
        retf
.f10:   cmp ah, 10h
        jne .f11
        cmp byte [cs:umbused], 0
        jne .none
        cmp dx, [cs:umbsize]
        ja .small
        mov byte [cs:umbused], 1
        mov bx, [cs:umbseg]
        mov dx, [cs:umbsize]
        mov ax, 1
        retf
.small: cmp word [cs:prevxms + 2], 0
        je .small0
        call far [cs:prevxms]           ; the driver before may have it
        or ax, ax
        jnz .ret
        cmp dx, [cs:umbsize]            ; else the larger of the two
        jae .ret
.small0: mov dx, [cs:umbsize]
        xor ax, ax
        mov bl, 0B0h
.ret:   retf
.none:  cmp word [cs:prevxms + 2], 0
        jne .prev
        xor ax, ax
        xor dx, dx
        mov bl, 0B1h
        retf
.f11:   cmp ah, 11h
        jne .nimp
        cmp dx, [cs:umbseg]
        jne .bad
        cmp byte [cs:umbused], 0
        je .bad
        mov byte [cs:umbused], 0
        mov ax, 1
        retf
.bad:   cmp word [cs:prevxms + 2], 0
        jne .prev
        xor ax, ax
        mov bl, 0B2h
        retf
.nimp:  cmp word [cs:prevxms + 2], 0
        jne .prev
        xor ax, ax
        mov bl, 80h
        retf
.prev:  jmp far [cs:prevxms]

resident_end:

; ---- start-up (dropped after INIT) ----

; ES:BX = request header.
init:
        push cx
        push dx
        push si
        push di
        push ds
        lds si, [es:bx + 18]            ; the CONFIG.SYS line
.name:  lodsb                           ; skip the driver's own name
        cmp al, ' '
        ja .name
        dec si
        call hex
        jc .args
        mov [cs:umbseg], ax
        call hex
        jc .args
        mov [cs:umbsize], ax
.board: call hex
        jc .args
        mov dx, ax                      ; EMS page registers: pages 0-3
        xor al, al
.page:  out dx, al
        inc dx
        inc al
        cmp al, 4
        jb .page
        jmp .board
.args:  push cs
        pop ds
        ; RAM at the first and the last paragraph?
        mov ax, [umbseg]
        call ramtest
        jc .noram
        mov ax, [umbseg]
        add ax, [umbsize]
        dec ax
        call ramtest
        jc .noram
        mov ax, 4300h                   ; an XMS driver already there?
        int 2Fh
        cmp al, 80h
        jne .first
        mov ax, 4310h
        int 2Fh
        mov [prevxms], bx
        mov [prevxms + 2], es
.first: mov ax, 352Fh
        int 21h
        mov [old2f], bx
        mov [old2f + 2], es
        mov dx, int2f
        mov ax, 252Fh
        int 21h
        mov dx, msg_ok
        mov ah, 9
        int 21h
        les bx, [reqhdr]
        mov word [es:bx + 14], resident_end
        mov [es:bx + 16], cs
        jmp .done
.noram: mov dx, msg_no
        mov ah, 9
        int 21h
        les bx, [reqhdr]
        mov word [es:bx + 14], 0        ; nothing stays
        mov [es:bx + 16], cs
.done:  mov word [es:bx + 3], 0100h
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        ret

; AX = segment: CF set when its first word is not RAM (kept as it was).
ramtest:
        push es
        mov es, ax
        mov ax, [es:0]
        mov word [es:0], 5AA5h
        cmp word [es:0], 5AA5h
        jne .no
        mov word [es:0], 0A55Ah
        cmp word [es:0], 0A55Ah
        jne .no
        mov [es:0], ax
        clc
        pop es
        ret
.no:    stc
        pop es
        ret

; DS:SI: skips blanks, reads a hex number into AX; CF when there is none.
hex:    xor dx, dx
        xor cx, cx
.sp:    lodsb
        cmp al, ' '
        je .sp
        cmp al, 9
        je .sp
.dig:   cmp al, '0'
        jb .end
        cmp al, '9'
        jbe .d09
        and al, 0DFh
        cmp al, 'A'
        jb .end
        cmp al, 'F'
        ja .end
        sub al, 'A' - 10
        jmp .add
.d09:   sub al, '0'
.add:   push cx
        mov cl, 4
        shl dx, cl
        pop cx
        or dl, al
        inc cx
        lodsb
        jmp .dig
.end:   dec si
        mov ax, dx
        cmp cx, 1
        ret                             ; CF when cx = 0 (no digit)

msg_ok: db 'XTUMB: upper memory block for XMS function 10h', 13, 10, '$'
msg_no: db 'XTUMB: no RAM in the upper memory block, not installed', 13, 10, '$'
