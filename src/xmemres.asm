; SPDX-License-Identifier: GPL-3.0-or-later
; The read cache in EMS or XMS memory (docs/design.md, "Extended memory and
; the read cache"). This module sits in the startup overlay section: none of
; it is resident. src/xmem.c copies the block between hbase and hend into a
; small DOS block (a UMB if there is one), only when an EMS or XMS driver
; gave memory, and points INT 21h and the program's terminate address there.
; The block is position independent: every data reference is an offset from
; hbase (V()), so it runs at offset 0 of its own segment.
;
; INT 21h: reads (3Fh) from files opened read-only whose names end in .PAK
; or .EXE (the packs and the overlay manager's sections) are served from
; 4 KB blocks of the file kept in EMS or XMS; a block missing is read from
; the disk first (whole, or up to the end of the file). The DOS file pointer
; is moved as DOS would move it, so the callers see no difference. A clock
; (second chance) picks the block to replace. Opens (3Dh), closes (3Eh) and
; 46h (force duplicate) keep the handle table up to date. AH = D7h with
; SI = 'XM' is ours: AL = 0 returns AX = 'XM', ES = this block; AL = 1
; registers handle BX (opened before the cache: the overlay manager's EXE)
; under the name at DS:DX.
;
; The terminate address (PSP:0Ah) runs `term` on every way out of the
; program (exit, Ctrl-Break, a critical error's Abort): it frees the EMS or
; XMS memory (leaked EMS pages stay lost until a reboot), puts INT 21h back,
; and INT 08h with the PIT's default rate if the timer was left hooked.
;
; EMS: logical page 0 holds the block table (the "meta" below), mapped at
; physical page 0 of the frame; data blocks are pages 1.., four to a page,
; mapped at physical page 1. XMS: the table follows the code in this block;
; data blocks are at slot * 4096 in the extended memory block; a 4 KB
; conventional buffer takes each block read from the disk (and the copies
; to odd lengths or to buffers above A000: EXMS86 !! maps its EMS pages over
; the upper part of the frame while it copies).
;
; src/xmemhma.asm assembles this file again with HMA defined: the XMS cache
; for a 286 without upper memory, in the high memory area (docs/design.md
; "The HMA"). That copy runs at FFFF:HORG, not at offset 0 (offsets above
; FFFF:0 are the only way to reach the HMA), its table and its bounce
; buffer sit at fixed offsets above it, up to the HMA's last byte, and a
; small stub in conventional memory (hsbase below) turns A20 on if it ever
; finds it off before it jumps there.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

BLK     equ 4096
%ifdef HMA
NSLOTS  equ 256                 ; the table's size (1 MB of blocks: xmem.c MAX_KB)
HORG    equ 0DA00h              ; the code's offset in segment FFFF
BO      equ 10000h - BLK - 16   ; the bounce buffer and the move's struct: to the top
M_BASE  equ BO - (16 + 512 + 6 * NSLOTS)        ; the table below them
LOWBUF  equ 1024                ; the low buffer DOS reads into (xmem.c HMA_LOW)
%else
NSLOTS  equ 512                 ; the table's size (2 MB of blocks)
HORG    equ 0                   ; at offset 0 of its own segment
BO      equ 0                   ; the bounce buffer's offset in its segment
M_BASE  equ 0                   ; the table's offset in its segment
%endif

%define V(x) ((x) - hbase + HORG)

; reg = the bounce buffer's offset (0 in the normal build: xor as before)
%macro BOFF 1
%if BO
        mov %1, BO
%else
        xor %1, %1
%endif
%endmacro

NFILES  equ 32
NHAND   equ 32
; meta layout (offsets in the meta segment)
M_SIG   equ M_BASE              ; 'XMM1', then the EMS handle (HMA: the handle, the stub's key)
M_HEADS equ M_BASE + 16         ; 256 buckets: slot + 1 of the chain, 0 = none
M_FILE  equ M_HEADS + 512       ; NSLOTS bytes: file id, 0 = free
M_REF   equ M_FILE + NSLOTS     ; NSLOTS bytes: used since the hand passed
M_BLK   equ M_REF + NSLOTS      ; NSLOTS words: block number in the file
M_NEXT  equ M_BLK + 2 * NSLOTS  ; NSLOTS words: next slot + 1 in the chain
M_SIZE  equ M_NEXT + 2 * NSLOTS - M_BASE ; 3600 bytes (HMA: 2064)

segment XMEMRES_TEXT public class=CODE align=16

hbase:
; ---- header: src/xmem.h XmemHdr has the same layout ----
h_sig     db 'XMC1'             ; 0
h_mode    db 0                  ; 4: 1 EMS, 2 XMS, 0 released
h_on      db 0                  ; 5: 1 = caching
h_old21   dd 0                  ; 6
h_old22   dd 0                  ; 10: the PSP's terminate address
h_old08   dd 0                  ; 14
h_xms     dd 0                  ; 18: XMS driver entry
h_handle  dw 0                  ; 22: EMS handle / XMS handle
h_frame   dw 0                  ; 24: EMS page frame segment (HMA: the low buffer's)
h_meta    dw 0                  ; 26: segment of the table
h_bounce  dw 0                  ; 28: XMS: 4 KB buffer in conventional memory
h_nslot   dw 0                  ; 30: data blocks
h_hand    dw 0                  ; 32: the clock's hand
h_mapped  dw 0FFFFh             ; 34: EMS logical page at physical page 1
h_hits    dd 0                  ; 36: blocks copied from EMS/XMS
h_miss    dd 0                  ; 40: blocks read from the disk
h_reads   dd 0                  ; 44: DOS reads answered
h_dbytes  dd 0                  ; 48: bytes the cache read from the disk
h_seq     dw 0                  ; 52: opens (the file table's age)
h_e21     dw V(int21)           ; 54: entry points
h_e22     dw V(term)            ; 56
h_len     dw hend - hbase       ; 58: bytes to copy
h_fail    dw 0                  ; 60: reads passed to DOS after an error
h_xmove   times 16 db 0         ; 62: XMS move: len dd, src h, src dd, dst h, dst dd
h_hmap    times NHAND db 0      ; 78: handle -> file id (1..NFILES), 0 = not cached;
                                ; bit 7: DOS's file pointer is behind h_hpos
h_files   times NFILES * 10 db 0 ; 110: h1, h2, size lo, size hi, last use (0 = free)
h_hpos    times NHAND dd 0      ; 430: the cached handles' file pointers
; scratch for the call in progress (INT 21h is not entered again from here:
; our own DOS calls go to h_old21)
u_off     dw 0                  ; the caller's buffer (normalised as we go)
u_seg     dw 0
u_cnt     dw 0
u_hnd     dw 0
u_fid     db 0
u_inb     db 0                  ; XMS: the block is in the bounce buffer
u_fent    dw 0                  ; file entry (offset in this block)
u_pos     dd 0                  ; where the read started
u_cur     dd 0                  ; where it is now
u_n       dw 0                  ; bytes to return
u_left    dw 0
u_blk     dw 0
u_boff    dw 0
u_k       dw 0
u_m       dw 0
u_bkt     dw 0
u_blen    dw 0
u_h1      dw 0
u_h2      dw 0
u_tl      dd 0                  ; the name's last four characters
u_sz      dd 0

; ------------------------------------------------------------------
int21:
        cmp ah, 3Fh
        je .read
        cmp ah, 42h
        je .seek
        cmp ah, 3Dh
        je .open
        cmp ah, 3Eh
        je .close
        cmp ah, 45h
        je .dup
        cmp ah, 46h
        je .dup2
        cmp ah, 0D7h
        je .ours
.chain: jmp far [cs:V(h_old21)]

.read:  cmp bx, NHAND
        jae .chain
        cmp byte [cs:bx + V(h_hmap)], 0
        je .chain
        cmp byte [cs:V(h_on)], 0
        je .dup                 ; off: DOS reads, from the right place
        jcxz .chain
        jmp do_read

.seek:  cmp bx, NHAND
        jae .chain
        cmp byte [cs:bx + V(h_hmap)], 0
        je .chain
        jmp do_seek

.close: cmp bx, NHAND
        jae .chain
        mov byte [cs:bx + V(h_hmap)], 0
        jmp .chain

.dup:   call sync               ; the copy shares the file pointer
        jmp .chain

.dup2:  call sync
        cmp cx, NHAND
        jae .chain
        push bx
        mov bx, cx
        mov byte [cs:bx + V(h_hmap)], 0
        pop bx
        jmp .chain

.open:  cmp byte [cs:V(h_on)], 0
        je .chain
        test al, 7
        jnz .chain              ; written to: never cached
        call name_check
        jc .chain
        sti
        pushf
        call far [cs:V(h_old21)]
        jc .ocf
        call register           ; AX = the new handle
        clc
.ocf:   jmp ret_cf

.ours:  cmp si, 'XM'
        jne .chain
        cmp al, 1
        je .reg
        mov ax, 'XM'
        push cs
        pop es
%ifdef HMA
        mov di, V(hbase)        ; the header's offset (the callers set DI = 0)
%endif
        clc
        jmp ret_cf
.reg:   cmp byte [cs:V(h_on)], 0
        je .rdone
        call name_check
        mov ax, bx
        call register
.rdone: mov ax, 1
        clc
        jmp ret_cf

; Back to the caller with AX and the carry flag as they are now.
ret_cf: push bp
        mov bp, sp
        jc .c
        and byte [bp + 6], 0FEh
        pop bp
        iret
.c:     or byte [bp + 6], 1
        pop bp
        iret

; DOS below us.
dos:    pushf
        call far [cs:V(h_old21)]
        ret

; BX = a handle: if it is cached and DOS's file pointer is behind (reads
; answered here do not move it), DOS's pointer goes to h_hpos. Keeps
; every register.
sync:   cmp bx, NHAND
        jae .out
        test byte [cs:bx + V(h_hmap)], 80h
        jz .out
        push ax
        push cx
        push dx
        push si
        mov si, bx
        shl si, 1
        shl si, 1
        mov dx, [cs:si + V(h_hpos)]
        mov cx, [cs:si + V(h_hpos) + 2]
        mov ax, 4200h
        call dos
        and byte [cs:bx + V(h_hmap)], 7Fh
        pop si
        pop dx
        pop cx
        pop ax
.out:   ret

; INT 21h AH = 42h on a cached handle: DOS moves the pointer (from the
; right place for AL = 1), and the new place is kept.
do_seek:
        cmp al, 1
        jne .abs
        call sync
.abs:   pushf
        call far [cs:V(h_old21)]
        jc .r
        push si
        mov si, bx
        shl si, 1
        shl si, 1
        mov [cs:si + V(h_hpos)], ax
        mov [cs:si + V(h_hpos) + 2], dx
        and byte [cs:bx + V(h_hmap)], 7Fh
        pop si
        clc
.r:     jmp ret_cf

; ------------------------------------------------------------------
; DS:DX = a file name: CF clear if it ends in .PAK or .EXE; its hashes in
; u_h1 / u_h2 (upper case). Keeps every register.
name_check:
        push ax
        push bx
        push cx
        push si
        mov si, dx
        xor bx, bx
        xor cx, cx
        mov word [cs:V(u_tl)], 0
        mov word [cs:V(u_tl) + 2], 0
        cld
.c:     lodsb
        or al, al
        jz .end
        cmp al, 'a'
        jb .u
        cmp al, 'z'
        ja .u
        sub al, 20h
.u:     mov ah, [cs:V(u_tl) + 1]        ; the last four characters
        mov [cs:V(u_tl)], ah
        mov ah, [cs:V(u_tl) + 2]
        mov [cs:V(u_tl) + 1], ah
        mov ah, [cs:V(u_tl) + 3]
        mov [cs:V(u_tl) + 2], ah
        mov [cs:V(u_tl) + 3], al
        xor ah, ah
        push ax                         ; h1 = h1 * 33 + c
        mov ax, bx
        shl ax, 1
        shl ax, 1
        shl ax, 1
        shl ax, 1
        shl ax, 1
        add bx, ax
        pop ax
        add bx, ax
        rol cx, 1                       ; h2 = rol(h2, 3) ^ c
        rol cx, 1
        rol cx, 1
        xor cl, al
        jmp .c
.end:   mov [cs:V(u_h1)], bx
        mov [cs:V(u_h2)], cx
        cmp word [cs:V(u_tl)], '.P'
        jne .exe
        cmp word [cs:V(u_tl) + 2], 'AK'
        je .yes
.exe:   cmp word [cs:V(u_tl)], '.E'
        jne .no
        cmp word [cs:V(u_tl) + 2], 'XE'
        jne .no
.yes:   clc
        jmp .out
.no:    stc
.out:   pop si
        pop cx
        pop bx
        pop ax
        ret

; AX = a handle just opened for the name hashed in u_h1 / u_h2: finds or
; makes its file entry and maps the handle to it. Keeps every register.
register:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        push cs
        pop ds
        cmp ax, NHAND
        jae .done
        mov [V(u_hnd)], ax
        mov bx, ax
        mov ax, 4201h                   ; where it is (0 after an open)
        xor cx, cx
        xor dx, dx
        call dos
        jc .done
        mov si, bx
        shl si, 1
        shl si, 1
        mov [si + V(h_hpos)], ax
        mov [si + V(h_hpos) + 2], dx
        push dx
        push ax
        mov ax, 4202h                   ; the size
        xor cx, cx
        xor dx, dx
        call dos
        mov [V(u_sz)], ax
        mov [V(u_sz) + 2], dx
        pop dx
        pop cx
        jc .done
        mov ax, 4200h                   ; back where it was
        call dos
        jc .done
        ; the entry with these hashes, else a free one, else the oldest
        mov si, V(h_files)
        xor di, di                      ; candidate
        mov cx, NFILES
        mov dx, 0FFFFh                  ; its age
.f:     mov ax, [si + 8]
        or ax, ax
        jz .free
        mov bx, [si]
        cmp bx, [V(u_h1)]
        jne .old
        mov bx, [si + 2]
        cmp bx, [V(u_h2)]
        jne .old
        ; known: the same size or it is another file now
        mov bx, [si + 4]
        cmp bx, [V(u_sz)]
        jne .stale
        mov bx, [si + 6]
        cmp bx, [V(u_sz) + 2]
        je .use
.stale: mov di, si
        jmp .take
.old:   cmp ax, dx
        jae .nx
        mov dx, ax
        mov di, si
        jmp .nx
.free:  cmp dx, 0
        je .nx
        xor dx, dx
        mov di, si
.nx:    add si, 10
        loop .f
        or di, di
        jz .done
.take:  ; DI: forget what it held, then it is this file's
        mov si, di
        cmp word [si + 8], 0
        je .fill
        call fid_of                     ; AL = its id
        call purge
.fill:
        mov ax, [V(u_h1)]
        mov [si], ax
        mov ax, [V(u_h2)]
        mov [si + 2], ax
        mov ax, [V(u_sz)]
        mov [si + 4], ax
        mov ax, [V(u_sz) + 2]
        mov [si + 6], ax
.use:   inc word [V(h_seq)]
        jnz .seq
        mov word [V(h_seq)], 1
.seq:   mov ax, [V(h_seq)]
        mov [si + 8], ax
        call fid_of
        mov bx, [V(u_hnd)]
        mov [bx + V(h_hmap)], al
.done:  pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; SI = a file entry -> AL = its id (1..NFILES). DS = CS.
fid_of: push dx
        mov ax, si
        sub ax, V(h_files)
        mov dl, 10
        div dl
        inc al
        pop dx
        ret

; AL = a file id: its handles and blocks forgotten. DS = CS. Keeps registers.
purge:  push ax
        push bx
        push cx
        push di
        push es
        mov bx, V(h_hmap)
        mov cx, NHAND
.h:     cmp [bx], al
        jne .h1
        mov byte [bx], 0
.h1:    inc bx
        loop .h
        mov ah, al
        call meta_check
        jc .out
        xor di, di
.s:     cmp di, [V(h_nslot)]
        jae .out
        cmp [es:M_FILE + di], ah
        jne .s1
        call unlink
        mov byte [es:M_FILE + di], 0
.s1:    inc di
        jmp .s
.out:   pop es
        pop di
        pop cx
        pop bx
        pop ax
        ret

; ES = the table (EMS: physical page 0 checked and mapped again if
; another program moved it). DS = CS. CF on failure.
meta_check:
        mov es, [V(h_meta)]
        cmp byte [V(h_mode)], 1
        jne .ok
        push ax
        mov ax, [V(h_handle)]
        cmp word [es:M_SIG], 'XM'
        jne .map
        cmp word [es:M_SIG + 2], 'M1'
        jne .map
        cmp [es:M_SIG + 4], ax
        je .ok1
.map:   mov word [V(h_mapped)], 0FFFFh  ; page 1 too, then
        push bx
        push dx
        mov dx, ax
        xor bx, bx
        mov ax, 4400h
        int 67h
        pop dx
        pop bx
        or ah, ah
        pop ax
        jnz .err
        cmp word [es:M_SIG], 'XM'
        jne .err
        jmp .ok
.ok1:   pop ax
.ok:    clc
        ret
.err:   stc
        ret

; AX = block, DL = file id -> BX = 2 * bucket. Uses CX.
hash:   mov bl, dl
        xor bh, bh
        mov cx, bx
        shl bx, 1
        shl bx, 1
        add cx, bx
        shl bx, 1
        shl bx, 1
        shl bx, 1
        add bx, cx                      ; id * 37
        add bx, ax
        and bx, 0FFh
        shl bx, 1
        ret

; u_fid, u_blk -> CF clear and DI = slot when present. u_bkt set. ES = table.
lookup: mov ax, [V(u_blk)]
        mov dl, [V(u_fid)]
        call hash
        mov [V(u_bkt)], bx
        mov di, [es:M_HEADS + bx]
.l:     or di, di
        jz .no
        dec di
        cmp [es:M_FILE + di], dl
        jne .nx
        mov si, di
        shl si, 1
        cmp [es:M_BLK + si], ax
        je .yes
.nx:    shl di, 1
        mov di, [es:M_NEXT + di]
        jmp .l
.yes:   clc
        ret
.no:    stc
        ret

; DI = a slot in use: out of its chain. Keeps registers.
unlink: push ax
        push bx
        push cx
        push dx
        push si
        mov dl, [es:M_FILE + di]
        mov si, di
        shl si, 1
        mov ax, [es:M_BLK + si]
        call hash
        lea si, [bx + M_HEADS]
        mov cx, di
        inc cx
.u:     mov ax, [es:si]
        or ax, ax
        jz .done
        cmp ax, cx
        je .found
        dec ax
        shl ax, 1
        mov si, ax
        add si, M_NEXT
        jmp .u
.found: mov bx, di
        shl bx, 1
        mov ax, [es:M_NEXT + bx]
        mov [es:si], ax
.done:  pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; The clock: DI = a free slot (taken out of its chain if it held a block).
victim: mov cx, [V(h_nslot)]
        shl cx, 1
        inc cx
.v:     mov di, [V(h_hand)]
        mov ax, di
        inc ax
        cmp ax, [V(h_nslot)]
        jb .v1
        xor ax, ax
.v1:    mov [V(h_hand)], ax
        cmp byte [es:M_FILE + di], 0
        je .free
        cmp byte [es:M_REF + di], 0
        je .take
        mov byte [es:M_REF + di], 0
        loop .v
.take:  call unlink
        mov byte [es:M_FILE + di], 0
.free:  ret

; DI = slot: chained under u_fid / u_blk (bucket u_bkt).
insert: mov al, [V(u_fid)]
        mov [es:M_FILE + di], al
        mov byte [es:M_REF + di], 1
        mov si, di
        shl si, 1
        mov ax, [V(u_blk)]
        mov [es:M_BLK + si], ax
        mov bx, [V(u_bkt)]
        mov ax, [es:M_HEADS + bx]
        mov [es:M_NEXT + si], ax
        lea ax, [di + 1]
        mov [es:M_HEADS + bx], ax
        ret

; EMS: DI = slot -> its page at physical page 1; AX = that page's segment,
; DX = the block's offset there. CF on failure.
map_slot:
        push bx
        push cx
        mov bx, di
        shr bx, 1
        shr bx, 1
        inc bx                          ; logical page (0 is the table)
        cmp bx, [V(h_mapped)]
        je .m
        push bx
        mov dx, [V(h_handle)]
        mov ax, 4401h
        int 67h
        pop bx
        or ah, ah
        jnz .e
        mov [V(h_mapped)], bx
.m:     mov dx, di
        and dx, 3
        mov cl, 12
        shl dx, cl
        mov ax, [V(h_frame)]
        add ax, 400h
        pop cx
        pop bx
        clc
        ret
.e:     mov word [V(h_mapped)], 0FFFFh
        pop cx
        pop bx
        stc
        ret

; XMS move of CX bytes (even) with the struct's handles and addresses set;
; CF on failure. DS = CS.
; The driver reads the struct from a copy behind the bounce buffer, in
; conventional memory: EXMS86 `!!` maps its own pages over part of the
; EMS frame while it copies, and this block may be a UMB in that frame.
xmove:  mov [V(h_xmove)], cx
        mov word [V(h_xmove) + 2], 0
        push si
        push di
        push bx
        push cx
        push es
        push ds
        mov es, [V(h_bounce)]
        mov si, V(h_xmove)
        mov di, BO + BLK
        mov cx, 8
        cld
        rep movsw
        mov si, BO + BLK
        push es
        pop ds
        mov ah, 0Bh
        call far [cs:V(h_xms)]
        pop ds
        pop es
        pop cx
        pop bx
        pop di
        pop si
        cmp ax, 1
        jne .e
        clc
        ret
.e:     stc
        ret

; DI = slot, AX = offset in its block -> the struct's XMS side at word SI
; of h_xmove (handle, then the 32-bit offset). Keeps DI.
xside:  push dx
        push cx
        mov dx, [V(h_handle)]
        mov [si], dx
        mov dx, di                      ; slot * 4096 + AX
        mov cl, 4
        shr dx, cl
        mov cx, di
        and cx, 15
        push ax
        mov ax, cx
        mov cl, 12
        shl ax, cl
        pop cx
        add ax, cx
        adc dx, 0
        mov [si + 2], ax
        mov [si + 4], dx
        pop cx
        pop dx
        ret

; Reads block u_blk of the file into slot DI (from the clock): EMS straight
; into the frame, XMS through the bounce buffer. CF on failure (the slot
; stays free).
fill:   call victim
        mov ax, [V(u_blk)]
        mov dx, ax
        mov cl, 4
        shr dx, cl
        mov cl, 12
        shl ax, cl
        push ax
        push dx
        mov si, [V(u_fent)]
        mov cx, [si + 4]
        mov bx, [si + 6]
        sub cx, ax
        sbb bx, dx
        jnz .cap
        cmp cx, BLK
        jbe .lok
.cap:   mov cx, BLK
.lok:   mov [V(u_blen)], cx
        pop dx
        pop ax
        mov bx, [V(u_hnd)]
        mov cx, dx
        mov dx, ax
        mov ax, 4200h
        call dos
        jc .err
        cmp byte [V(h_mode)], 1
        jne .xms
        call map_slot
        jc .err
        push ds
        mov ds, ax
        mov cx, [cs:V(u_blen)]
        mov bx, [cs:V(u_hnd)]
        mov ah, 3Fh
        call dos
        pop ds
        jc .err
        cmp ax, [V(u_blen)]
        jne .err
        jmp .ok
.xms:
%ifdef HMA
        ; DOS never reads into the HMA (a disk BIOS may normalise the
        ; address, FFFF:EFF0 to 0EFE:0000, and write into low memory): the
        ; block comes in LOWBUF pieces through the low buffer (h_frame, after
        ; the stub) and is put together in the bounce buffer
        mov word [V(u_m)], 0
.piece: mov cx, [V(u_blen)]
        sub cx, [V(u_m)]
        jz .pdone
        cmp cx, LOWBUF
        jbe .pc
        mov cx, LOWBUF
.pc:    push ds
        mov ds, [cs:V(h_frame)]
        xor dx, dx
        mov bx, [cs:V(u_hnd)]
        mov ah, 3Fh
        call dos
        pop ds
        jc .err
        cmp ax, cx
        jne .err
        push di
        push es
        push ds
        mov es, [V(h_bounce)]
        mov di, BO
        add di, [V(u_m)]
        add [V(u_m)], cx
        mov ds, [V(h_frame)]
        xor si, si
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
        pop ds
        pop es
        pop di
        jmp .piece
.pdone:
%else
        push ds
        mov ds, [cs:V(h_bounce)]
        BOFF dx
        mov cx, [cs:V(u_blen)]
        mov bx, [cs:V(u_hnd)]
        mov ah, 3Fh
        call dos
        pop ds
        jc .err
        cmp ax, [V(u_blen)]
        jne .err
%endif
        mov word [V(h_xmove) + 4], 0    ; source: the bounce buffer
        mov word [V(h_xmove) + 6], BO
        mov ax, [V(h_bounce)]
        mov [V(h_xmove) + 8], ax
        mov si, V(h_xmove) + 10
        xor ax, ax
        call xside
        mov cx, [V(u_blen)]
        inc cx
        and cx, 0FFFEh
        call xmove
        jc .err
        mov byte [V(u_inb)], 1
.ok:    call insert
        add word [V(h_miss)], 1
        adc word [V(h_miss) + 2], 0
        mov ax, [V(u_blen)]
        add [V(h_dbytes)], ax
        adc word [V(h_dbytes) + 2], 0
        clc
        ret
.err:   stc
        ret

; Copies u_k bytes at u_boff of slot DI's block to the caller's buffer.
; CF on failure.
copy_out:
        cmp byte [V(h_mode)], 1
        jne .xms
        call map_slot
        jc .e
        mov si, dx
        add si, [V(u_boff)]
        push ds
        push es
        les di, [V(u_off)]
        mov cx, [V(u_k)]
        mov ds, ax
.mv:    cld
        shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
        pop es
        pop ds
        clc
        ret
.e:     stc
        ret
.xms:   cmp byte [V(u_inb)], 0
        jne .bnc
        ; straight to the caller: an even count below A000
        test byte [V(u_k)], 1
        jnz .via
        cmp word [V(u_seg)], 0A000h
        jae .via
        mov ax, [V(u_boff)]
        mov si, V(h_xmove) + 4
        call xside
        mov word [V(h_xmove) + 10], 0
        mov ax, [V(u_off)]
        mov [V(h_xmove) + 12], ax
        mov ax, [V(u_seg)]
        mov [V(h_xmove) + 14], ax
        mov cx, [V(u_k)]
        jmp xmove
.via:   ; the even part around it into the bounce buffer
        mov ax, [V(u_boff)]
        and ax, 0FFFEh
        push ax
        mov si, V(h_xmove) + 4
        call xside
        mov word [V(h_xmove) + 10], 0
        mov word [V(h_xmove) + 12], BO
        mov ax, [V(h_bounce)]
        mov [V(h_xmove) + 14], ax
        pop ax
        mov cx, [V(u_boff)]
        add cx, [V(u_k)]
        inc cx
        and cx, 0FFFEh
        sub cx, ax
        call xmove
        jc .e
        mov si, [V(u_boff)]
        and si, 1
%if BO
        add si, BO
%endif
        jmp .bcp
.bnc:   mov si, [V(u_boff)]
%if BO
        add si, BO
%endif
.bcp:   push ds
        push es
        les di, [V(u_off)]
        mov cx, [V(u_k)]
        mov ds, [V(h_bounce)]
        jmp .mv

; ------------------------------------------------------------------
; INT 21h AH = 3Fh on a cached handle (BX, CX > 0, DS:DX).
do_read:
        sti
        cld
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        mov [cs:V(u_seg)], ds
        mov [cs:V(u_off)], dx
        mov [cs:V(u_cnt)], cx
        push cs
        pop ds
        mov [V(u_hnd)], bx
        mov al, [bx + V(h_hmap)]
        and al, 7Fh
        mov [V(u_fid)], al
        dec al
        mov ah, 10
        mul ah
        add ax, V(h_files)
        mov [V(u_fent)], ax
        shl bx, 1                       ; where the file pointer is
        shl bx, 1
        mov ax, [bx + V(h_hpos)]
        mov dx, [bx + V(h_hpos) + 2]
        mov [V(u_pos)], ax
        mov [V(u_pos) + 2], dx
        mov [V(u_cur)], ax
        mov [V(u_cur) + 2], dx
        mov si, [V(u_fent)]
        mov word [V(u_n)], 0
        mov cx, [si + 4]
        mov bx, [si + 6]
        sub cx, ax
        sbb bx, dx
        jb .zero                        ; past the end
        jnz .all
        cmp cx, [V(u_cnt)]
        jb .some
.all:   mov cx, [V(u_cnt)]
.some:  mov [V(u_n)], cx
        mov [V(u_left)], cx
        or cx, cx
        jnz .go
        jmp .zero
.go:    call meta_check
        jc .back
        call norm
.blk:   mov ax, [V(u_cur)]
        mov dx, [V(u_cur) + 2]
        mov bx, ax
        mov cl, 12
        shr bx, cl
        mov cl, 4
        shl dx, cl
        or bx, dx
        mov [V(u_blk)], bx
        and ax, BLK - 1
        mov [V(u_boff)], ax
        mov cx, BLK
        sub cx, ax
        cmp cx, [V(u_left)]
        jbe .k
        mov cx, [V(u_left)]
.k:     mov [V(u_k)], cx
        mov byte [V(u_inb)], 0
        call lookup
        jnc .hit
        cmp word [V(u_k)], BLK          ; a whole block missing: with the
        jne .one                        ; next ones missing too, one DOS read
        call bulk
        jc .back
        cmp word [V(u_left)], 0
        jne .blk
        jmp .done
.one:   call fill
        jc .back
        jmp .copy
.hit:   mov byte [es:M_REF + di], 1
        add word [V(h_hits)], 1
        adc word [V(h_hits) + 2], 0
.copy:  call copy_out
        jc .back
        mov es, [V(h_meta)]
        mov ax, [V(u_k)]
        call advance
        jz .done
        jmp .blk
.done:  ; the file pointer after the read (DOS's is behind until sync)
        mov bx, [V(u_hnd)]
        or byte [bx + V(h_hmap)], 80h
        shl bx, 1
        shl bx, 1
        mov ax, [V(u_cur)]
        mov [bx + V(h_hpos)], ax
        mov ax, [V(u_cur) + 2]
        mov [bx + V(h_hpos) + 2], ax
        add word [V(h_reads)], 1
        adc word [V(h_reads) + 2], 0
.zero:  pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        mov ax, [cs:V(u_n)]
        clc
        jmp ret_cf
.back:  ; something failed: DOS reads it all instead, from where it began
        ; (after eight failures the cache stops)
        inc word [V(h_fail)]
        cmp word [V(h_fail)], 8
        jb .bk
        mov byte [V(h_on)], 0
.bk:    mov bx, [V(u_hnd)]
        mov dx, [V(u_pos)]
        mov cx, [V(u_pos) + 2]
        mov ax, 4200h
        call dos
        and byte [bx + V(h_hmap)], 7Fh
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        pushf
        call far [cs:V(h_old21)]
        jc .pe
        push si                         ; where DOS left it
        push ax
        mov si, bx
        shl si, 1
        shl si, 1
        add ax, [cs:V(u_pos)]
        mov [cs:si + V(h_hpos)], ax
        mov ax, [cs:V(u_pos) + 2]
        adc ax, 0
        mov [cs:si + V(h_hpos) + 2], ax
        pop ax
        pop si
        clc
.pe:    jmp ret_cf

; AX bytes done: on in the file and the caller's buffer; ZF when all are.
advance:
        add [V(u_cur)], ax
        adc word [V(u_cur) + 2], 0
        add [V(u_off)], ax
        call norm
        sub [V(u_left)], ax
        ret

; Block u_blk is missing and the request covers it whole: it and the whole
; blocks after it that are missing too come with one DOS read straight into
; the caller's buffer (as without the cache), then go into the cache.
; CF on failure.
bulk:   mov ax, [V(u_left)]
        mov cl, 12
        shr ax, cl                      ; whole blocks left (1..15)
        mov [V(u_m)], ax
        push word [V(u_blk)]
        mov word [V(u_k)], 1
.cnt:   mov ax, [V(u_k)]
        cmp ax, [V(u_m)]
        jae .go
        inc word [V(u_blk)]
        call lookup
        jnc .go
        inc word [V(u_k)]
        jmp .cnt
.go:    pop word [V(u_blk)]
        mov ax, [V(u_k)]
        mov [V(u_m)], ax
        mov cl, 12
        shl ax, cl
        mov [V(u_blen)], ax
        mov bx, [V(u_hnd)]
        mov dx, [V(u_cur)]
        mov cx, [V(u_cur) + 2]
        mov ax, 4200h
        call dos
        jc .err
        mov cx, [V(u_blen)]
        push ds
        lds dx, [V(u_off)]
        mov bx, [cs:V(u_hnd)]
        mov ah, 3Fh
        call dos
        pop ds
        jc .err
        cmp ax, [V(u_blen)]
        jne .err
.each:  call lookup                     ; its bucket (it is missing)
        call victim
        call put_slot
        jc .err
        call insert
        add word [V(h_miss)], 1
        adc word [V(h_miss) + 2], 0
        add word [V(h_dbytes)], BLK
        adc word [V(h_dbytes) + 2], 0
        inc word [V(u_blk)]
        mov ax, BLK
        call advance
        dec word [V(u_m)]
        jnz .each
        clc
        ret
.err:   stc
        ret

; Copies the 4 KB at the caller's buffer into slot DI. CF on failure.
put_slot:
        cmp byte [V(h_mode)], 1
        jne .x
        call map_slot
        jc .e
        push ds
        push es
        push di
        mov es, ax
        mov di, dx
        lds si, [V(u_off)]
        mov cx, BLK / 2
        cld
        rep movsw
        pop di
        pop es
        pop ds
        clc
        ret
.x:     cmp word [V(u_seg)], 0A000h
        jae .via
        mov word [V(h_xmove) + 4], 0    ; from the caller's buffer
        mov ax, [V(u_off)]
        mov [V(h_xmove) + 6], ax
        mov ax, [V(u_seg)]
        mov [V(h_xmove) + 8], ax
.put:   mov si, V(h_xmove) + 10
        xor ax, ax
        call xside
        mov cx, BLK
        jmp xmove
.via:   push ds                         ; above A000: through the buffer
        push es
        push di
        mov es, [cs:V(h_bounce)]
        BOFF di
        lds si, [cs:V(u_off)]
        mov cx, BLK / 2
        cld
        rep movsw
        pop di
        pop es
        pop ds
        mov word [V(h_xmove) + 4], 0
        mov word [V(h_xmove) + 6], BO
        mov ax, [V(h_bounce)]
        mov [V(h_xmove) + 8], ax
        jmp .put
.e:     stc
        ret

; u_seg:u_off with the offset below 16.
norm:   push ax
        push cx
        mov ax, [V(u_off)]
        mov cl, 4
        shr ax, cl
        add [V(u_seg)], ax
        and word [V(u_off)], 15
        pop cx
        pop ax
        ret

; ------------------------------------------------------------------
; The program's terminate address: every way out comes here (DOS has
; freed the program's memory, this block included, but nothing has
; reused it yet).
term:   pushf
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        push cs
        pop ds
        xor ax, ax
        mov es, ax
        cli
        mov ax, [V(h_old21)]
        mov [es:21h * 4], ax
        mov ax, [V(h_old21) + 2]
        mov [es:21h * 4 + 2], ax
        mov ax, [V(h_old22)]
        mov [es:22h * 4], ax
        mov ax, [V(h_old22) + 2]
        mov [es:22h * 4 + 2], ax
        mov ax, [V(h_old08)]
        mov dx, [V(h_old08) + 2]
        cmp ax, [es:08h * 4]
        jne .t8
        cmp dx, [es:08h * 4 + 2]
        je .t8ok
.t8:    mov [es:08h * 4], ax            ; the timer was left hooked
        mov [es:08h * 4 + 2], dx
        mov al, 36h
        out 43h, al
        xor al, al
        out 40h, al
        out 40h, al
.t8ok:  sti
        mov byte [V(h_on)], 0
        mov dx, [V(h_handle)]
        cmp byte [V(h_mode)], 1
        jne .x
        mov ah, 45h
        int 67h
        jmp .rel
.x:     cmp byte [V(h_mode)], 2
        jne .rel
        mov ah, 0Ah
        call far [V(h_xms)]
.rel:   mov byte [V(h_mode)], 0
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        popf
        jmp far [cs:V(h_old22)]

        align 16
hend:

%ifdef HMA
; ------------------------------------------------------------------
; HMA: the block above runs at FFFF:HORG. What stays in conventional
; memory is this stub. INT 21h: the calls the cache wants go there once the
; stub has seen the table's signature and its key at FFFF:M_SIG (A20 on,
; the HMA still ours); if they are not there, the XMS driver turns A20 on
; for this call (local enable) and off again after it, as DOS had it; if
; they are still not there (another program took the HMA), DOS answers.
; The terminate address frees the XMS block from here and, when the HMA is
; ours (XMS function 01h: no DOS=HIGH), turns A20 off and releases it.
%define HS(x) ((x) - hsbase)

        align 16
hsbase:
hs_old21  dd 0                  ; 0
hs_old22  dd 0                  ; 4
hs_old08  dd 0                  ; 8
hs_main   dd 0                  ; 12: the block's int21 in the HMA
hs_xms    dd 0                  ; 16: XMS driver entry
hs_handle dw 0                  ; 20: XMS handle
hs_key    dw 0                  ; 22: also at FFFF:M_SIG + 6
hs_own    dw 0                  ; 24: 1 = the HMA is ours (XMS 01h)
hs_e21    dw HS(hsint21)        ; 26
hs_e22    dw HS(hsterm)         ; 28
hs_len    dw HS(hsend)          ; 30

hsint21:
        cmp ah, 3Fh
        je .go
        cmp ah, 42h
        je .go
        cmp ah, 3Dh
        je .go
        cmp ah, 3Eh
        je .go
        cmp ah, 45h
        je .go
        cmp ah, 46h
        je .go
        cmp ah, 0D7h
        je .go
        jmp far [cs:HS(hs_old21)]
.go:    push ds
        push ax
        call hs_here
        jne .a20
        pop ax
        pop ds
        jmp far [cs:HS(hs_main)]
.a20:   push bx                         ; A20 on for this call
        mov ah, 5
        call far [cs:HS(hs_xms)]
        pop bx
        call hs_here
        pop ax
        pop ds
        jne .dos
        pushf
        call far [cs:HS(hs_main)]
        call hs_a20off                  ; keeps the flags of the call
        retf 2
.dos:   call hs_a20off
        jmp far [cs:HS(hs_old21)]

; ZF when FFFF:M_SIG holds the table's signature and the key. DS = FFFF, AX.
hs_here:
        mov ax, 0FFFFh
        mov ds, ax
        mov ax, [cs:HS(hs_key)]
        cmp [M_SIG + 6], ax
        jne .r
        cmp word [M_SIG], 'XM'
        jne .r
        cmp word [M_SIG + 2], 'M1'
.r:     ret

; XMS local disable (A20 back as it was). Keeps registers and flags.
hs_a20off:
        pushf
        push ax
        push bx
        mov ah, 6
        call far [cs:HS(hs_xms)]
        pop bx
        pop ax
        popf
        ret

hsterm: pushf
        push ax
        push bx
        push dx
        push es
        xor ax, ax
        mov es, ax
        cli
        mov ax, [cs:HS(hs_old21)]
        mov [es:21h * 4], ax
        mov ax, [cs:HS(hs_old21) + 2]
        mov [es:21h * 4 + 2], ax
        mov ax, [cs:HS(hs_old22)]
        mov [es:22h * 4], ax
        mov ax, [cs:HS(hs_old22) + 2]
        mov [es:22h * 4 + 2], ax
        mov ax, [cs:HS(hs_old08)]
        mov dx, [cs:HS(hs_old08) + 2]
        cmp ax, [es:08h * 4]
        jne .t8
        cmp dx, [es:08h * 4 + 2]
        je .t8ok
.t8:    mov [es:08h * 4], ax            ; the timer was left hooked
        mov [es:08h * 4 + 2], dx
        mov al, 36h
        out 43h, al
        xor al, al
        out 40h, al
        out 40h, al
.t8ok:  sti
        mov dx, [cs:HS(hs_handle)]
        mov ah, 0Ah
        call far [cs:HS(hs_xms)]
        cmp word [cs:HS(hs_own)], 0
        je .out
        mov ah, 4                       ; global disable A20
        call far [cs:HS(hs_xms)]
        mov ah, 2                       ; release the HMA
        call far [cs:HS(hs_xms)]
.out:   pop es
        pop dx
        pop bx
        pop ax
        popf
        jmp far [cs:HS(hs_old22)]

        align 16
hsend:

; void far * __cdecl xmem_hstub(void): the HMA stub to copy (HS(hs_len) bytes).
        global _xmem_hstub
_xmem_hstub:
        mov ax, hsbase
        mov dx, cs
        retf

; void far * __cdecl xmem_hblob(void): the HMA block (to FFFF:HORG; h_len bytes).
        global _xmem_hblob
_xmem_hblob:
        mov ax, hbase
        mov dx, cs
        retf

%else
; ------------------------------------------------------------------
; EMS: the block above runs from EMS logical page 0 (at offset 1000h, after
; the table), mapped at physical page 0. What stays in conventional memory
; is this stub: INT 21h checks that page 0 is ours (maps it again if a
; program moved it) before jumping there, and the terminate address frees
; the EMS pages from here (code in the frame cannot run after that).
%define S(x) ((x) - sbase)

        align 16
sbase:
s_old21   dd 0                  ; 0
s_old22   dd 0                  ; 4
s_old08   dd 0                  ; 8
s_main    dd 0                  ; 12: the block's int21 in the frame
s_frame   dw 0                  ; 16
s_handle  dw 0                  ; 18
s_e21     dw S(sint21)          ; 20
s_e22     dw S(sterm)           ; 22
s_len     dw S(send)            ; 24

sint21: cmp ah, 3Fh
        je .go
        cmp ah, 42h
        je .go
        cmp ah, 3Dh
        je .go
        cmp ah, 3Eh
        je .go
        cmp ah, 45h
        je .go
        cmp ah, 46h
        je .go
        cmp ah, 0D7h
        je .go
        jmp far [cs:S(s_old21)]
.go:    push ds
        push ax
        mov ds, [cs:S(s_frame)]
        mov ax, [cs:S(s_handle)]
        cmp word [M_SIG], 'XM'
        jne .map
        cmp word [M_SIG + 2], 'M1'
        jne .map
        cmp [M_SIG + 4], ax
        je .ok
.map:   push bx
        push dx
        mov dx, ax
        xor bx, bx
        mov ax, 4400h
        int 67h
        mov word [1000h + V(h_mapped)], 0FFFFh  ; page 1 is unknown too
        pop dx
        pop bx
.ok:    pop ax
        pop ds
        jmp far [cs:S(s_main)]

sterm:  pushf
        push ax
        push dx
        push es
        xor ax, ax
        mov es, ax
        cli
        mov ax, [cs:S(s_old21)]
        mov [es:21h * 4], ax
        mov ax, [cs:S(s_old21) + 2]
        mov [es:21h * 4 + 2], ax
        mov ax, [cs:S(s_old22)]
        mov [es:22h * 4], ax
        mov ax, [cs:S(s_old22) + 2]
        mov [es:22h * 4 + 2], ax
        mov ax, [cs:S(s_old08)]
        mov dx, [cs:S(s_old08) + 2]
        cmp ax, [es:08h * 4]
        jne .t8
        cmp dx, [es:08h * 4 + 2]
        je .t8ok
.t8:    mov [es:08h * 4], ax            ; the timer was left hooked
        mov [es:08h * 4 + 2], dx
        mov al, 36h
        out 43h, al
        xor al, al
        out 40h, al
        out 40h, al
.t8ok:  sti
        mov dx, [cs:S(s_handle)]
        mov ah, 45h
        int 67h
        pop es
        pop dx
        pop ax
        popf
        jmp far [cs:S(s_old22)]

        align 16
send:

; ------------------------------------------------------------------
; Called from src/xmem.c (same section, while it is loaded).

; void far * __cdecl xmem_stub(void): the EMS stub to copy (S(s_len) bytes).
        global _xmem_stub
_xmem_stub:
        mov ax, sbase
        mov dx, cs
        retf

; void far * __cdecl xmem_blob(void): the block to copy (its length is
; in its header, h_len).
        global _xmem_blob
_xmem_blob:
        mov ax, hbase
        mov dx, cs
        retf
%endif
