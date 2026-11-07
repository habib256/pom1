; ============================================================================
; t25_hgr_hud8.s — micro-test: gen2/hgr_hud8.asm (on gen2/hgr_text8.asm)
; ============================================================================
; GUARDS the differential HUD fields. Local font: glyph i (char $20 + i),
; row r = ((7i + 13r) & $7F) | 1, so every cell is identifiable; white
; attributes, HGR order. Returns = number of cells drawn.
;
;   field A (col 30, scanline 0, 5 cells, page 1):
;     12345 -> 5; 12345 again -> 0; 12346 -> 1 (col 34 = $2022 row 0 is
;     '6' = $1B); 65535 -> 5; then 0 -> 5: the step 0 - 65535 is +1
;     modulo 65536 and must NOT take the "+1 on the cached digits" path
;     (it drew "65536" before the fix); field shows "    0": col 30 = ' '
;     ($01), col 34 = '0' ($71).
;   field B (col 0, scanline 184, 2 cells, page 2 via ht_page = $60):
;     7 -> 2; 9 -> 1 ($43D1 = '9' row 0 = $2F); 123 -> $FF (too wide,
;     nothing drawn: $43D1 still $2F) and the text cursor is left alone
;     (ht_col = 3, ht_sl = 100 as set by the driver).
;   hgr_hud8_invalidate page 1 of A, then 0 again -> 5.
;   hgr_putu8 4096 at the cursor (col 3, scanline 100, page 1): '4' row 0
;     at $322B = $0D, cursor ends at column 7.
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 60000
; EXPECT: 0F00 A5
; EXPECT: 0F01 05 00 01 1B 05 05 01 71
; EXPECT: 0F09 02 01 2F FF 2F 03 64
; EXPECT: 0F10 05 0D 07
; ============================================================================

MB = $0F00

.code
.include "hgr_scanline.inc"
.include "hgr_clear.asm"
.include "hgr_text8.asm"
.include "hgr_hud8.asm"

fld_a:  HUD8_FIELD 30, 0, 5
fld_b:  HUD8_FIELD 0, 184, 2

; 26 glyphs, chars $20..$39 (space, punctuation, digits)
font:
.repeat 26, I
  .repeat 8, R
        .byte ((I * 7 + R * 13) & $7F) | 1
  .endrepeat
.endrepeat

.macro PUTU fld, val, slot
        LDA #<fld
        STA hh_fld
        LDA #>fld
        STA hh_fld+1
        LDA #<(val)
        STA hh_val
        LDA #>(val)
        STA hh_val+1
        JSR hgr_hud8_putu
        STA MB+slot
.endmacro

.segment "ENTRY"
start:
        LDA #0
        LDX #$20
        JSR hgr_fill_page
        LDA #0
        LDX #$40
        JSR hgr_fill_page
        LDA #<font
        STA ht_font_lo
        LDA #>font
        STA ht_font_hi
        LDA #0
        STA ht_rev
        STA ht_cbit
        STA ht_left
        STA ht_page
        LDA #40
        STA ht_wrap
        LDA #$7F
        STA ht_cm_ev
        STA ht_cm_od
        LDA #3                  ; the caller's cursor, kept by the HUD
        STA ht_col
        LDA #100
        STA ht_sl

        PUTU fld_a, 12345, 1
        PUTU fld_a, 12345, 2
        PUTU fld_a, 12346, 3
        LDA $2022               ; col 34, row 0: '6'
        STA MB+4
        PUTU fld_a, 65535, 5
        PUTU fld_a, 0, 6
        LDA $201E               ; col 30: ' '
        STA MB+7
        LDA $2022               ; col 34: '0'
        STA MB+8

        LDA #$60
        STA ht_page
        PUTU fld_b, 7, 9
        PUTU fld_b, 9, $0A
        LDA $43D1               ; page 2, row 184, col 1: '9'
        STA MB+$0B
        PUTU fld_b, 123, $0C
        LDA $43D1
        STA MB+$0D
        LDA ht_col
        STA MB+$0E
        LDA ht_sl
        STA MB+$0F

        LDA #0
        STA ht_page
        LDA #<fld_a
        STA hh_fld
        LDA #>fld_a
        STA hh_fld+1
        LDA #1
        JSR hgr_hud8_invalidate
        PUTU fld_a, 0, $10

        LDA #<4096
        STA hh_val
        LDA #>4096
        STA hh_val+1
        JSR hgr_putu8
        LDA $322B               ; row 100, col 3: '4'
        STA MB+$11
        LDA ht_col
        STA MB+$12

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin
