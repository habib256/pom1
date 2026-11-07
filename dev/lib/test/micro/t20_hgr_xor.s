; ============================================================================
; t20_hgr_xor.s — micro-test: gen2/hgr_xor.asm (+ hgr_div7_ram.asm)
; ============================================================================
; GUARDS the self-erasing XOR kernels ported from pom2games:
;
;   - hgr_xor_rect x=3 y=5 w=10 h=2, page 1, over bytes of $80 (palette bit
;     set, no pixel): pixels 3..6 of byte 0 and 0..5 of byte 1 flip ->
;     $F8 $BF on rows 5 ($3400) and 6 ($3800); byte 2 stays $80 (the right
;     mask stops at pixel 12); bit 7 is never touched.
;   - the same rectangle again gives the backdrop back: $80 $80.
;   - hgr_xor_sprite at x=50 (column 7, data pre-shifted by the caller),
;     y=60, hx_page = $60 (page 2), over bytes of $55: row 60 ($5387/$5388)
;     $55^$01 / $55^$40 = $54 $15, row 61 $55^$7F = $2A $2A; page 1's
;     matching bytes ($3387) stay $55; drawn twice: $55 $55 again.
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 20000
; EXPECT: 0F00 A5
; EXPECT: 0F01 F8 BF F8 BF 80 80 80
; EXPECT: 0F08 54 15 2A 2A 55 55 55
; ============================================================================

MB = $0F00

; Zero-page modules first: the driver then addresses hx_* in zero page.
.code
.include "hgr_scanline.inc"
.include "hgr_div7_ram.asm"
.include "hgr_xor.asm"

.segment "ENTRY"
start:
        JSR hgr_div7_init
        LDA #$80                ; backdrop page 1 rows 5/6: palette bit only
        LDX #3
@b1:    STA $3400,X
        STA $3800,X
        DEX
        BPL @b1
        LDA #$55                ; backdrop rows 60/61 cols 7/8, both pages
        STA $5387
        STA $5388
        STA $5787
        STA $5788
        STA $3387

        LDA #0
        STA hx_page
        JSR rect
        LDA $3400
        STA MB+1
        LDA $3401
        STA MB+2
        LDA $3800
        STA MB+3
        LDA $3801
        STA MB+4
        LDA $3402
        STA MB+5
        JSR rect                ; again: restored
        LDA $3400
        STA MB+6
        LDA $3801
        STA MB+7

        LDA #$60
        STA hx_page
        JSR sprite
        LDA $5387
        STA MB+8
        LDA $5388
        STA MB+9
        LDA $5787
        STA MB+$0A
        LDA $5788
        STA MB+$0B
        LDA $3387
        STA MB+$0C
        JSR sprite              ; again: restored
        LDA $5387
        STA MB+$0D
        LDA $5788
        STA MB+$0E

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

rect:   LDA #3
        STA hx_x
        LDA #5
        STA hx_y
        LDA #10
        STA hx_w
        LDA #2
        STA hx_h
        JMP hgr_xor_rect

sprite: LDA #50
        STA hx_x
        LDA #60
        STA hx_y
        LDA #<spr
        STA hx_data
        LDA #>spr
        STA hx_data+1
        JMP hgr_xor_sprite

spr:    .byte $01,$40, $7F,$7F, $00,$00, $00,$00
        .byte $00,$00, $00,$00, $00,$00, $00,$00

