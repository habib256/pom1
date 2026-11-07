; ============================================================================
; t33_tms_m2_glyph.s — micro-test: tms9918m2 init/plot/clear + text_bitmap
; ============================================================================
; GUARDS: the Mode-2 driver after its move to the shared port helpers
;   (vdp_ctrl_a / vdp_write_a / vdp_fill / vdp_fill_blank) and the glyph
;   blitter after the build-time font reversal + low-byte-in-A index math
;   (pom2games text_bitmap.asm @ c0c66fd).
;   - init_vdp_g2: colour table $2000-$37FF = $F1, linear name table at
;     $3800 (0..FF x3), SAT $3B00 = D0 then D1 x127, $3B80 and the pattern
;     table wiped to 0.
;   - text_blit_glyph 'A' at (13,17) -> cell $0208: rows 10 28 44 44 7C 44
;     44 00 (Apple-1 'A', bit 7 = leftmost), colour bytes = pen $5 -> $51,
;     neighbours untouched; code $FE (bit 7 ignored -> '~', the high-index
;     path: $7E*8 = $03F0) at (255,191) -> $17F8: 34 58.
;   - plot_set OR then XOR at (3,5): $0005 = 10 then 00, colour $2005 = 51.
;   - clear_bitmap zeroes exactly $0000-$17FF (sentinel $1800 survives).
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918m2.asm tms9918/tms9918_pad.asm test/micro/support/text_bitmap_codetank.asm
; INCLUDES: tms9918/text_bitmap.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 250000
; EXPECT: 0F00 A5 F1 F1 00 FF 00 FF D0 D1 D1 00 00 00
; EXPECT: 0F0D 10 28 44 44 7C 44 44 00 51 51 F1 00
; EXPECT: 0F19 34 58 51
; EXPECT: 0F1C 10 51 00 51
; EXPECT: 0F20 00 00 00 9D
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_g2, clear_bitmap, plot_set, vdp_set_write, vdp_set_read
.import text_blit_glyph, tms9918_pad18
.importzp pix_x, pix_y, pix_addr_lo, pix_addr_hi, pen_color

.segment "ZEROPAGE"
tmp:     .res 1
tmp2:    .res 1
mptr_lo: .res 1
mptr_hi: .res 1
.exportzp tmp, tmp2, mptr_lo, mptr_hi

.segment "BSS"
plot_mode: .res 1
.export plot_mode

MB = $0F00

.macro SET_R addr
        LDA     #<(addr)
        STA     pix_addr_lo
        LDA     #>(addr)
        STA     pix_addr_hi
        JSR     vdp_set_read
        JSR     tms9918_pad18   ; read-prefetch cushion
.endmacro
.macro PEEK addr, slot
        SET_R   addr
        LDA     VDP_DATA
        STA     MB+slot
.endmacro
; Read 8 consecutive bytes from addr into slots slot..slot+7.
.macro PEEK8 addr, slot
        SET_R   addr
        LDX     #0
:       LDA     VDP_DATA
        STA     MB+slot,X
        JSR     tms9918_pad18
        INX
        CPX     #8
        BNE     :-
.endmacro

.segment "CODE"
main:
        APPLE1_PREAMBLE
        JSR     init_vdp_g2     ; display ON afterwards
        PEEK    $2000, 1        ; F1
        PEEK    $37FF, 2        ; F1
        PEEK    $3800, 3        ; 00
        PEEK    $38FF, 4        ; FF
        PEEK    $3900, 5        ; 00
        PEEK    $3AFF, 6        ; FF
        PEEK    $3B00, 7        ; D0
        PEEK    $3B01, 8        ; D1
        PEEK    $3B7F, 9        ; D1
        PEEK    $3B80, 10       ; 00
        PEEK    $0000, 11       ; 00
        PEEK    $17FF, 12       ; 00

        ; sentinel just past the pattern table, for clear_bitmap below
        LDA     #$00
        STA     pix_addr_lo
        LDA     #$18
        STA     pix_addr_hi
        JSR     vdp_set_write
        LDA     #$9D
        STA     VDP_DATA
        JSR     tms9918_pad18

        ; --- glyph 'A' at (13,17) -> cell $0208, pen 5 --------------------
        LDA     #5
        STA     pen_color
        LDA     #13
        STA     pix_x
        LDA     #17
        STA     pix_y
        LDA     #'A'
        JSR     text_blit_glyph
        PEEK8   $0208, 13       ; 10 28 44 44 7C 44 44 00
        PEEK    $2208, 21       ; 51
        PEEK    $220F, 22       ; 51
        PEEK    $2210, 23       ; F1 (next cell's colour untouched)
        PEEK    $0210, 24       ; 00 (next cell's pattern untouched)

        ; --- code $FE ('~' once bit 7 is dropped) at (255,191) -> $17F8 ---
        LDA     #255
        STA     pix_x
        LDA     #191
        STA     pix_y
        LDA     #$FE
        JSR     text_blit_glyph
        PEEK    $17F8, 25       ; 34
        PEEK    $17F9, 26       ; 58
        PEEK    $37F8, 27       ; 51

        ; --- plot_set OR then XOR at (3,5) ---------------------------------
        LDA     #0
        STA     plot_mode
        LDA     #3
        STA     pix_x
        LDA     #5
        STA     pix_y
        JSR     plot_set
        PEEK    $0005, 28       ; 10
        PEEK    $2005, 29       ; 51
        LDA     #1
        STA     plot_mode
        JSR     plot_set
        PEEK    $0005, 30       ; 00
        PEEK    $2005, 31       ; 51 (XOR leaves the colour alone)

        ; --- clear_bitmap: exactly $0000-$17FF ------------------------------
        JSR     clear_bitmap
        PEEK    $0208, 32       ; 00
        PEEK    $17F8, 33       ; 00
        PEEK    $17FF, 34       ; 00
        PEEK    $1800, 35       ; 9D

        LDA     #$A5
        STA     MB
spin:   JMP     spin
