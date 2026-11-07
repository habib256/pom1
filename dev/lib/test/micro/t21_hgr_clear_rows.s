; ============================================================================
; t21_hgr_clear_rows.s — micro-test: gen2/hgr_clear_rows.asm (+ hgr_flip.asm)
; ============================================================================
; GUARDS the HUD-preserving clears ported from pom2games. Both pages start
; at $FF (hgr_fill_page); a $00 means "cleared", a $FF "left alone".
;
;   page 2 (hgr_set_draw_page $60), hgr_clear_viewport160:
;     row 0 col 0 ($4000) and row 159 col 39 ($5DF7) cleared; row 160
;     col 0 ($4250, the HUD) kept; screen hole $4078 kept; page 1 ($2000)
;     kept.
;   page 2, hgr_clear_hud32: $4250 and row 191 col 39 ($5FF7) cleared,
;     hole $5FF8 kept.
;   page 1, hgr_clear_rows A=10 X=13: rows 10 ($2880) and 12 col 39
;     ($30A7) cleared, rows 9 ($2480) and 13 ($3480) kept.
;   page 1, hgr_clear_visible: $2000 and $3FF7 cleared, hole $3FF8 kept,
;     page 2's hole $4078 kept (the patched stores followed the page back).
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 120000
; EXPECT: 0F00 A5
; EXPECT: 0F01 00 00 FF FF FF
; EXPECT: 0F06 00 00 FF
; EXPECT: 0F09 00 00 FF FF
; EXPECT: 0F0D 00 00 FF FF
; ============================================================================

MB = $0F00

.code
.include "hgr_scanline.inc"
.include "hgr_flip.asm"
.include "hgr_clear_rows.asm"

.segment "ENTRY"
start:
        LDA #$FF
        LDX #$20
        JSR hgr_fill_page
        LDA #$FF
        LDX #$40
        JSR hgr_fill_page

        LDA #$60
        JSR hgr_set_draw_page
        JSR hgr_clear_viewport160
        LDA $4000
        STA MB+1
        LDA $5DF7
        STA MB+2
        LDA $4250
        STA MB+3
        LDA $4078
        STA MB+4
        LDA $2000
        STA MB+5

        JSR hgr_clear_hud32
        LDA $4250
        STA MB+6
        LDA $5FF7
        STA MB+7
        LDA $5FF8
        STA MB+8

        LDA #0
        JSR hgr_set_draw_page
        LDA #10
        LDX #13
        JSR hgr_clear_rows
        LDA $2880
        STA MB+9
        LDA $30A7
        STA MB+$0A
        LDA $2480
        STA MB+$0B
        LDA $3480
        STA MB+$0C

        JSR hgr_clear_visible
        LDA $2000
        STA MB+$0D
        LDA $3FF7
        STA MB+$0E
        LDA $3FF8
        STA MB+$0F
        LDA $4078
        STA MB+$10

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

; On-demand libraries (dev/lib/README.md, "Assembled on demand"): after the callers.
.code
.include "hgr_clear.asm"
