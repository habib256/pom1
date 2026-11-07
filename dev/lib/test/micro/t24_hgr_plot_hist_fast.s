; ============================================================================
; t24_hgr_plot_hist_fast.s — micro-test: gen2/hgr_plot_hist.asm, FAST variant
; ============================================================================
; GUARDS HGR_PLOT_HIST_FAST (ported from pom2games): the byte-run history,
; the object stack (pl_erase_to) and pl_sprite. PL_HIST = $9800. Both pages
; cleared; backdrop $2000 = $05 (pixels 0, 2).
;
;   page 0, pl_line (0,0) -> (13,0), x major: $2000 = $7F, $2001 = $7F and
;     ONE entry per byte: pl_n[0] = 2 (the plain variant records 12);
;   mark = 2; pl_sprite W=1 H=2 (rows $03, $01, all shifts the same) at
;     (14,3): $2C02 = $03, $3002 = $01, pl_n[0] = 4;
;   pl_erase_to 2 pops the sprite only: $2C02 = $00, $3002 = $00, the line
;     stays ($2001 = $7F), pl_n[0] = 2;
;   page 1, pl_line (0,50) -> (0,57), y major: $4B00 (row 50) = $01, one
;     entry per pixel: pl_n[1] = 8;
;   page 0, pl_erase: $2000 = $05 (backdrop), $2001 = $00, pl_n[0] = 0;
;     page 2 untouched ($4B00 = $01).
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 60000
; EXPECT: 0F00 A5
; EXPECT: 0F01 7F 7F 02
; EXPECT: 0F04 03 01 04
; EXPECT: 0F07 00 00 7F 02
; EXPECT: 0F0B 01 08
; EXPECT: 0F0D 05 00 00 01
; ============================================================================

MB = $0F00

PL_HIST = $9800
HGR_PLOT_HIST_FAST = 1

.code
.include "hgr_scanline.inc"
.include "hgr_div7_ram.asm"
.include "hgr_clear.asm"
.include "hgr_plot_hist.asm"    ; zero page here, routines after the callers

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

        LDA pl_n                ; the mark: everything drawn so far
        PHA
        LDA #<spr
        STA pl_sp
        LDA #>spr
        STA pl_sp+1
        LDA #14
        STA pl_x
        LDA #3
        STA pl_y
        JSR pl_sprite
        LDA $2C02
        STA MB+4
        LDA $3002
        STA MB+5
        LDA pl_n
        STA MB+6
        PLA
        JSR pl_erase_to
        LDA $2C02
        STA MB+7
        LDA $3002
        STA MB+8
        LDA $2001
        STA MB+9
        LDA pl_n
        STA MB+$0A

        LDA #1
        JSR pl_page
        LDA #0
        STA pl_x0
        STA pl_x1
        LDA #50
        STA pl_y0
        LDA #57
        STA pl_y1
        JSR pl_line
        LDA $4B00
        STA MB+$0B
        LDA pl_n+1
        STA MB+$0C

        LDA #0
        JSR pl_page
        JSR pl_erase
        LDA $2000
        STA MB+$0D
        LDA $2001
        STA MB+$0E
        LDA pl_n
        STA MB+$0F
        LDA $4B00
        STA MB+$10

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

; 1 byte x 2 rows; the 7 shifts all point at the same rows (x = 14 is
; shift 0, the one used).
spr:    .byte 1, 2
        .addr rows, rows, rows, rows, rows, rows, rows
rows:   .byte $03, $01

.code
.include "hgr_plot_hist.asm"
