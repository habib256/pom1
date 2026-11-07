; ============================================================================
; t22_hgr_plot16_line16.s — micro-test: gen2/hgr_plot16.asm + hgr_line16.asm
; ============================================================================
; GUARDS the clipped full-width pixel and line ported from pom2games
; (page 1 cleared first; HGR_FULL_WIDTH_TABLES = 1):
;
;   hgr_plot16 OR at (279,191): $3FF7 = $40 (column 39, bit 6) -- the
;     last pixel of the 280-wide screen, only reachable with 16-bit X;
;   clipped, nothing written: x = 280 (would set $01 in the hole $3FF8),
;     x = $FFFF, y = 192 ($3FF8 and $2000-side probes stay $00);
;   XOR at (0,0) over $81: $80 (pixel off, palette bit kept).
;   hgr_line16 (-3,10) -> (10,10): x = -3..-1 dropped, pixels 0..10 lit:
;     $2880 = $7F, $2881 = $0F;
;   hgr_line16 (275,180) -> (285,190): the diagonal leaves the screen at
;     x = 280: $3377 = $04 (275,180), $23F7 = $40 (279,184), and the hole
;     the next pixel would hit, $27F8 (280,185), stays $00.
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 40000
; EXPECT: 0F00 A5
; EXPECT: 0F01 40 00 80
; EXPECT: 0F04 7F 0F
; EXPECT: 0F06 04 40 00
; ============================================================================

MB = $0F00

HGR_FULL_WIDTH_TABLES = 1

.code
.include "hgr_scanline.inc"
.include "hgr_plot_tables.inc"
.include "hgr_plot16.asm"
.include "hgr_line16.asm"

.segment "ENTRY"
start:
        JSR clear_hgr
        LDA #0
        STA hp_mode
        LDA #<279
        LDX #>279
        LDY #191
        JSR plot
        LDA #<280               ; clipped: x = 280
        LDX #>280
        LDY #191
        JSR plot
        LDA #$FF                ; clipped: x = -1
        LDX #$FF
        LDY #191
        JSR plot
        LDA #0                  ; clipped: y = 192
        LDX #0
        LDY #192
        JSR plot
        LDA $3FF7
        STA MB+1
        LDA $3FF8
        STA MB+2
        LDA #$81
        STA $2000
        LDA #1
        STA hp_mode
        LDA #0
        LDX #0
        LDY #0
        JSR plot
        LDA $2000
        STA MB+3

        LDA #0
        STA hp_mode
        LDA #<-3
        STA h16_x0
        LDA #>-3
        STA h16_x0+1
        LDA #10
        STA h16_y0
        STA h16_y1
        LDA #10
        STA h16_x1
        LDA #0
        STA h16_x1+1
        JSR hgr_line16
        LDA $2880
        STA MB+4
        LDA $2881
        STA MB+5

        LDA #<275
        STA h16_x0
        LDA #>275
        STA h16_x0+1
        LDA #180
        STA h16_y0
        LDA #<285
        STA h16_x1
        LDA #>285
        STA h16_x1+1
        LDA #190
        STA h16_y1
        JSR hgr_line16
        LDA $3377
        STA MB+6
        LDA $23F7
        STA MB+7
        LDA $27F8
        STA MB+8

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

plot:   STA hp_x                ; A/X = x, Y = y
        STX hp_x+1
        STY hp_y
        JMP hgr_plot16

; On-demand libraries (dev/lib/README.md, "Assembled on demand"): after the callers.
.code
.include "hgr_clear.asm"
