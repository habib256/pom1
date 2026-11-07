; =============================================
; hgr_scanline_ram.asm - hgr_lo / hgr_hi built at start (no data in the file)
; =============================================
; The same tables as hgr_scanline.inc, for a program short of file space (or
; tape time): the 384 bytes are reserved in segment "BSS" (the program's cfg
; must declare a type = bss segment of that name), and hgr_scanline_init
; (56 B with its 3-byte table, in the current segment) fills them once,
; ~15 300 cycles:
;   addr[y] = $2000 + (y%8)*$0400 + ((y/8)%8)*$80 + (y/64)*$28
; Loaded file: -328 B (384 - 56).
;
; Usage:
;   .include "hgr_scanline_ram.asm"   ; instead of hgr_scanline.inc
;   ...     JSR hgr_scanline_init     ; before any drawing
; Clobbers A, X, Y. The tables are writable RAM, so hgr_flip.asm works on
; them too.
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_scanline_ram.asm),
; which takes the segment name from a .define; POM1 fixes it to "BSS".
; =============================================

.ifndef _HGR_SCANLINE_RAM_LOADED_
_HGR_SCANLINE_RAM_LOADED_ = 1

.ifdef _HGR_SCANLINE_INC_LOADED_
.error "hgr_scanline_ram.asm: hgr_scanline.inc already defines hgr_lo/hgr_hi"
.endif

.pushseg
.segment "BSS"
hgr_lo: .res 192
hgr_hi: .res 192
.popseg

hgr_scanline_init:
        LDX     #0
@y:     TXA                             ; hi = $20 + (y%8)*4 + (y/8%8)/2
        AND     #7
        ASL     A
        ASL     A
        ORA     #$20
        STA     hgr_hi,X
        TXA
        LSR     A
        LSR     A
        LSR     A
        TAY                             ; Y = y/8
        AND     #7
        LSR     A                       ; C = (y/8%8) & 1: $80 in lo
        ORA     hgr_hi,X
        STA     hgr_hi,X
        LDA     #0
        ROR     A
        STA     hgr_lo,X
        TYA                             ; + (y/64) * $28
        LSR     A
        LSR     A
        LSR     A
        TAY
        LDA     hgr_lo,X
        CLC
        ADC     hgr_sl_third,Y
        STA     hgr_lo,X
        INX
        CPX     #192
        BNE     @y
        RTS
hgr_sl_third: .byte $00, $28, $50

.endif  ; _HGR_SCANLINE_RAM_LOADED_
