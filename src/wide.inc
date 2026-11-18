; SPDX-License-Identifier: GPL-3.0-or-later
; Byte copies and fills that the 286 build (build.ps1 -Cpu 286, NASM
; -dCPU286) does in words on its 16-bit bus: to a 16-bit VGA / EGA card
; or memory a word costs one bus cycle where two bytes cost two, if the
; word is aligned (an odd DI goes one byte first). The 8086 and 186
; builds keep their plain rep movsb / rep stosb (8-bit bus: no gain,
; and their objects stay byte-identical). Flags: changed on the 286.
; Not for latch copies (EGA write mode 1): those must stay bytes.

; CX bytes DS:SI to ES:DI (forward). CX 0 after.
%macro WMOVSB 0
%ifdef CPU286
        jcxz %%x
        test di, 1
        jz %%w
        movsb
        dec cx
%%w:    shr cx, 1
        rep movsw
        adc cx, cx
        rep movsb
%%x:
%else
        rep movsb
%endif
%endmacro

; CX bytes of AL to ES:DI (forward). CX 0 after; the 286 build sets AH = AL.
%macro WSTOSB 0
%ifdef CPU286
        mov ah, al
        jcxz %%x
        test di, 1
        jz %%w
        stosb
        dec cx
%%w:    shr cx, 1
        rep stosw
        adc cx, cx
        rep stosb
%%x:
%else
        rep stosb
%endif
%endmacro
