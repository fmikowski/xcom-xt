; SPDX-License-Identifier: GPL-3.0-or-later
; Counts file reads through DOS (INT 21h, AH = 3Fh) and the bytes asked
; for: in a battle nearly all of them are overlay sections the dynamic
; overlay manager loads, which the LOG shows per turn. Large model, Watcom
; __cdecl.
;
; With OVLWATCH (overlay stress builds: build.ps1 -OvlArea) the hook also
; counts the overlay manager re-applying a moved section's relocations
; (redoRelocs, see tools/ovlfix.ps1): that is the only time it seeks to the
; start of a section's relocation records (disk_addr + num_paras * 16 in
; __OVLTAB__); a normal load seeks to the code and reads on. Normal builds
; have none of this.

%ifndef CPU
%define CPU 8086
%endif
        bits 16
        cpu CPU

segment OVLCOUNT_TEXT public class=CODE

%ifdef OVLWATCH
        extern __OVLTAB__
        extern __OVLTABEND__
redo    dw 0
redosec dw 0
%endif
reads   dw 0
bytes   dw 0, 0
old21   dw 0, 0
; The mode folder (docs/design.md "Mode folders"): a read-only open (3Dh) of a name of up
; to 12 characters tries <rd_dir>GA\name first (startup.c sets rd_dir: C, E or V; 0 none),
; then the name as given.
        global _rd_dir
_rd_dir db 0, "GA\"
rdname  times 13 db 0
rdend:

%ifdef RDDIAG
        extern __OVLTAB__
        extern __OVLTABEND__
secload times 64 dw 0
opens   dw 0
lastnm  times 16 db 0
; Timed reads (umb branch): PIT counts (1193182 a second) spent in DOS
; reads, in all and per overlay section (the reads after the seek to its
; code, until the next seek or open), from timer_read.
        extern timer_read_
dgseg   dw 0
cursec  dw 0                    ; 2 * section number of the load going on
t0      dw 0, 0
rdtime  dw 0, 0
sectime times 128 dw 0
%endif

int21:
%ifdef OVLWATCH
        cmp ax, 4200h
        jne .noseek
        call ovl_seek
.noseek:
%endif
%ifdef RDDIAG
        cmp ax, 4200h
        jne .nodseek
        call diag_seek
.nodseek:
        cmp ah, 3Dh
        jne .noopen
        call diag_open
.noopen:
%endif
        cmp ah, 3Fh
        jne .open
        inc word [cs:reads]
        add [cs:bytes], cx
        adc word [cs:bytes + 2], 0
%ifdef RDDIAG
        jmp timed_read
%endif
.chain:
        jmp far [cs:old21]

.open:  cmp ah, 3Dh
        jne .chain
        test al, 3                      ; read only: writes stay in the game's folder
        jnz .chain
        cmp byte [cs:_rd_dir], 0
        je .chain
        push si
        push di
        push es
        push ax
        push cs
        pop es
        mov si, dx
        mov di, rdname
        cld
.cp:    cmp di, rdend
        jae .no                         ; a long name (a path): as given
        lodsb
        stosb
        or al, al
        jnz .cp
        pop ax
        push ax
        push ds
        push dx
        push cs
        pop ds
        mov dx, _rd_dir
        pushf
        call far [cs:old21]
        pop dx
        pop ds
        jc .no
        inc sp                          ; the caller's AX: AX is the handle, CF clear
        inc sp
        pop es
        pop di
        pop si
        sti                             ; (the flags DOS gave back have our IF 0)
        retf 2
.no:    pop ax
        pop es
        pop di
        pop si
        jmp .chain

%ifdef RDDIAG
; DX:AX = timer_read() (keeps every other register).
now:
        push bx
        push cx
        push si
        push di
        push es
        push ds
        mov ds, [cs:dgseg]
        call far timer_read_
        pop ds
        pop es
        pop di
        pop si
        pop cx
        pop bx
        ret

timed_read:
        push dx
        push ax
        call now
        mov [cs:t0], ax
        mov [cs:t0 + 2], dx
        pop ax
        pop dx
        pushf
        call far [cs:old21]
        pushf
        push ax
        push dx
        push bx
        call now
        sub ax, [cs:t0]
        sbb dx, [cs:t0 + 2]
        add [cs:rdtime], ax
        adc [cs:rdtime + 2], dx
        mov bx, [cs:cursec]
        or bx, bx
        jz .nosec
        shl bx, 1
        add [cs:sectime + bx], ax
        adc [cs:sectime + bx + 2], dx
.nosec: pop bx
        pop dx
        pop ax
        popf
        retf 2
%endif

%ifdef OVLWATCH
; CX:DX = the seek's offset. Keeps every register.
ovl_seek:
        push ax
        push bx
        push si
        push di
        push ds
        mov ax, seg __OVLTAB__
        mov ds, ax
        mov si, __OVLTAB__ + 10         ; the first section's entry
        xor di, di
.next:  cmp si, __OVLTABEND__
        jae .done
        inc di
        mov ax, [si + 8]                ; num_paras
        mov bx, ax
        push cx
        mov cl, 4
        shl ax, cl
        mov cl, 12
        shr bx, cl
        pop cx
        add ax, [si + 12]               ; + disk_addr
        adc bx, [si + 14]
        add si, 16
        cmp ax, dx
        jne .next
        cmp bx, cx
        jne .next
        inc word [cs:redo]
        mov [cs:redosec], di
.done:  pop ds
        pop di
        pop si
        pop bx
        pop ax
        ret

; unsigned __cdecl ovl_redo_count(void): relocations redone so far;
; unsigned __cdecl ovl_redo_section(void): the last section (1 = first)
        global _ovl_redo_count
_ovl_redo_count:
        mov ax, [cs:redo]
        retf
        global _ovl_redo_section
_ovl_redo_section:
        mov ax, [cs:redosec]
        retf
%endif

%ifdef RDDIAG
; Seek to CX:DX: a section's code (disk_addr) = that section loads.
diag_seek:
        push ax
        push si
        push di
        push ds
        mov ax, seg __OVLTAB__
        mov ds, ax
        mov si, __OVLTAB__ + 10
        xor di, di
.next:  cmp si, __OVLTABEND__
        jae .done
        add di, 2
        mov ax, [si + 12]
        cmp ax, dx
        jne .skip
        mov ax, [si + 14]
        cmp ax, cx
        jne .skip
        cmp di, 128
        jae .done
        inc word [cs:secload + di]
        mov [cs:cursec], di
        jmp .found
.skip:  add si, 16
        jmp .next
.done:  mov word [cs:cursec], 0
.found: pop ds
        pop di
        pop si
        pop ax
        ret

; Open of DS:DX: counted, the name kept.
diag_open:
        push ax
        push si
        push di
        mov word [cs:cursec], 0
        inc word [cs:opens]
        mov si, dx
        xor di, di
.cp:    mov al, [si]
        mov [cs:lastnm + di], al
        or al, al
        jz .end
        inc si
        inc di
        cmp di, 15
        jb .cp
        mov byte [cs:lastnm + di], 0
.end:   pop di
        pop si
        pop ax
        ret

; unsigned __cdecl rd_secload(int i): loads of section i (1 = first)
        global _rd_secload
_rd_secload:
        push bp
        mov bp, sp
        push bx
        mov bx, [bp + 6]
        shl bx, 1
        mov ax, [cs:secload + bx]
        pop bx
        pop bp
        retf
; unsigned __cdecl rd_opens(void)
        global _rd_opens
_rd_opens:
        mov ax, [cs:opens]
        retf
; unsigned long __cdecl rd_sectime(int i): PIT counts reading section i
        global _rd_sectime
_rd_sectime:
        push bp
        mov bp, sp
        push bx
        mov bx, [bp + 6]
        shl bx, 1
        shl bx, 1
        mov ax, [cs:sectime + bx]
        mov dx, [cs:sectime + bx + 2]
        pop bx
        pop bp
        retf
; unsigned __cdecl rd_secseg(int i, int w): word w of section i's __OVLTAB__
; entry (w = 3: the segment it is loaded at, 0 = not loaded)
        global _rd_secseg
_rd_secseg:
        push bp
        mov bp, sp
        push bx
        push ds
        mov ax, seg __OVLTAB__
        mov ds, ax
        mov bx, [bp + 6]
        dec bx
        mov cl, 4
        shl bx, cl
        add bx, [bp + 8]
        add bx, [bp + 8]
        mov ax, [bx + __OVLTAB__ + 10]
        pop ds
        pop bx
        pop bp
        retf
; unsigned long __cdecl rd_readtime(void): PIT counts in all reads
        global _rd_readtime
_rd_readtime:
        mov ax, [cs:rdtime]
        mov dx, [cs:rdtime + 2]
        retf
; char far * __cdecl rd_lastname(void)
        global _rd_lastname
_rd_lastname:
        mov ax, lastnm
        mov dx, cs
        retf
%endif

; void __cdecl rd_count_start(void): hooks INT 21h (rd_count_stop unhooks)
        global _rd_count_start
_rd_count_start:
        push es
        push bx
%ifdef RDDIAG
        mov [cs:dgseg], ds
%endif
        cmp word [cs:old21 + 2], 0
        jne .done
        mov ax, 3521h
        int 21h
        mov [cs:old21], bx
        mov [cs:old21 + 2], es
        push ds
        push cs
        pop ds
        mov dx, int21
        mov ax, 2521h
        int 21h
        pop ds
.done:  pop bx
        pop es
        retf

; void __cdecl rd_count_stop(void)
        global _rd_count_stop
_rd_count_stop:
        cmp word [cs:old21 + 2], 0
        je .done
        push ds
        lds dx, [cs:old21]
        mov ax, 2521h
        int 21h
        pop ds
        mov word [cs:old21 + 2], 0
.done:  retf

; unsigned __cdecl rd_count(void): reads so far
        global _rd_count
_rd_count:
        mov ax, [cs:reads]
        retf

; unsigned long __cdecl rd_bytes(void): bytes asked for so far
        global _rd_bytes
_rd_bytes:
        mov ax, [cs:bytes]
        mov dx, [cs:bytes + 2]
        retf
