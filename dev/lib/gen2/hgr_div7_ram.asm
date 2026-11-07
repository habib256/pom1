; =============================================
; hgr_div7_ram.asm - x / 7 and x mod 7 for x = 0..255, built at start
; =============================================
; hgr_div7[x] = x / 7 (byte column), hgr_mod7[x] = x mod 7 (bit phase).
; The 512 bytes are reserved in segment "BSS" (the program's cfg must
; declare a type = bss segment of that name) instead of stored in the file,
; and hgr_div7_init (27 B, in the current segment) fills them once,
; ~8 000 cycles: the loaded file is 485 B smaller than with the tables
; written out. Clobbers A, X, Y.
;
; Usage: .include "hgr_div7_ram.asm" (any segment), JSR hgr_div7_init before
; the first lookup. hgr_xor.asm and hgr_plot_hist.asm read these names.
; (hgr_col / hgr_mask in hgr_plot_tables.inc are the same x/7 with a bit
; MASK instead of a bit number; a program may alias one to the other.)
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_div7_ram.asm).
; =============================================

.ifndef _HGR_DIV7_RAM_LOADED_
_HGR_DIV7_RAM_LOADED_ = 1

.pushseg
.segment "BSS"
hgr_div7: .res 256
hgr_mod7: .res 256
.popseg

hgr_div7_init:
        LDX     #0
        LDY     #0                      ; x mod 7
        TXA                             ; x / 7
@x:     STA     hgr_div7,X
        PHA
        TYA
        STA     hgr_mod7,X
        PLA
        INY
        CPY     #7
        BCC     @next
        LDY     #0
        ADC     #0                      ; C = 1: next column
@next:  INX
        BNE     @x
        RTS

.endif  ; _HGR_DIV7_RAM_LOADED_
