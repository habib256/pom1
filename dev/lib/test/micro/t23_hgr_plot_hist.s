; ============================================================================
; t23_hgr_plot_hist.s — micro-test: gen2/hgr_plot_hist.asm, plain variant
; ============================================================================
; GUARDS the undoable point history ported from pom2games (HGR_PLOT_HIST_ALL,
; PL_HIST = $9800). Both pages cleared; backdrop $2000 = $05 (pixels 0, 2).
;
;   page 0, pl_line (0,0) -> (13,0): $2000 = $7F, $2001 = $7F; only the
;     pixels that were dark are recorded: pl_n[0] = 14 - 2 = 12 ($0C);
;   page 1, pl_plot (7,1): $4401 = $01, pl_n[1] = 1;
;   page 0, pl_erase: the backdrop is back exactly ($2000 = $05,
;     $2001 = $00), pl_n[0] = 0, and page 2 is untouched ($4401 = $01).
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 60000
; EXPECT: 0F00 A5
; EXPECT: 0F01 7F 7F 0C 01 01
; EXPECT: 0F06 05 00 00 01
; ============================================================================

MB = $0F00

PL_HIST = $9800
HGR_PLOT_HIST_ALL = 1

.code
.include "hgr_scanline.inc"
.include "hgr_div7_ram.asm"
.include "hgr_plot_hist.asm"

.segment "ENTRY"
start:
        JSR hgr_div7_init
        LDA #0
        LDX #$20
        JSR hgr_fill_page
        LDA #0
        LDX #$40
        JSR hgr_fill_page
        LDA #$05
        STA $2000
        LDA #0
        STA pl_n
        STA pl_n+1

        LDA #0
        JSR pl_page
        LDA #0
        STA pl_x0
        STA pl_y0
        STA pl_y1
        LDA #13
        STA pl_x1
        JSR pl_line
        LDA $2000
        STA MB+1
        LDA $2001
        STA MB+2
        LDA pl_n
        STA MB+3

        LDA #1
        JSR pl_page
        LDA #7
        STA pl_x
        LDA #1
        STA pl_y
        JSR pl_plot
        LDA $4401
        STA MB+4
        LDA pl_n+1
        STA MB+5

        LDA #0
        JSR pl_page
        JSR pl_erase
        LDA $2000
        STA MB+6
        LDA $2001
        STA MB+7
        LDA pl_n
        STA MB+8
        LDA $4401
        STA MB+9

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

; On-demand libraries (dev/lib/README.md, "Assembled on demand"): after the callers.
.code
.include "hgr_clear.asm"
