; ============================================================================
; t18_hgr_clear_tables.s — micro-test: gen2/hgr_clear.asm + the RAM-built tables
; ============================================================================
; GUARDS the 2026-11-07 backports from the pom2games Apple II edition:
;
;   - clear_hgr / hgr_fill_page (HGR_CLEAR_LOOP: eight patched STA abs,Y a
;     turn, no zero page): page 2 filled with $AA first, then page 1
;     cleared. $2000 / $3FFF = $00, $4000 / $5FFF = $AA, and the bytes
;     either side of the 16 KB ($1FFF, $6000-side guard at $1FFE) keep $5A.
;     Exit registers as before: A = $00, X = $40 (page 1 + $20), Y = $00.
;   - hgr_scanline_init (hgr_scanline_ram.asm): hgr_lo/hi[191] = $D0 $3F
;     ($3FD0), [100] = $28 $32 ($3228) — addr = $2000 + (y%8)*$400 +
;     ((y/8)%8)*$80 + (y/64)*$28.
;   - hgr_plot_tables_init (HGR_PLOT_TABLES_RAM + HGR_FULL_WIDTH_TABLES):
;     hgr_col[279] = $27, hgr_mask[279] = $40, hgr_col[255] = $24,
;     hgr_mask[13] = $40.
;   - hgr_div7_init (hgr_div7_ram.asm): div7/mod7[255] = $24 $03,
;     [48] = $06 $06.
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 60000
; EXPECT: 0F00 A5
; EXPECT: 0F01 00 00 AA AA 5A 5A 00 40 00
; EXPECT: 0F0A D0 3F 28 32
; EXPECT: 0F0E 27 40 24 40
; EXPECT: 0F12 24 03 06 06
; ============================================================================

MB = $0F00

HGR_PLOT_TABLES_RAM   = 1
HGR_FULL_WIDTH_TABLES = 1

.segment "ENTRY"
start:
        LDA #$5A
        STA $1FFF
        STA $1FFE
        LDA #$AA
        LDX #$40
        JSR hgr_fill_page       ; page 2 := $AA
        JSR clear_hgr           ; page 1 := $00
        STA MB+7                ; exit registers
        STX MB+8
        STY MB+9
        LDA $2000
        STA MB+1
        LDA $3FFF
        STA MB+2
        LDA $4000
        STA MB+3
        LDA $5FFF
        STA MB+4
        LDA $1FFF
        STA MB+5
        LDA $1FFE
        STA MB+6

        JSR hgr_scanline_init
        LDA hgr_lo+191
        STA MB+$0A
        LDA hgr_hi+191
        STA MB+$0B
        LDA hgr_lo+100
        STA MB+$0C
        LDA hgr_hi+100
        STA MB+$0D

        JSR hgr_plot_tables_init
        LDA hgr_col+279
        STA MB+$0E
        LDA hgr_mask+279
        STA MB+$0F
        LDA hgr_col+255
        STA MB+$10
        LDA hgr_mask+13
        STA MB+$11

        JSR hgr_div7_init
        LDA hgr_div7+255
        STA MB+$12
        LDA hgr_mod7+255
        STA MB+$13
        LDA hgr_div7+48
        STA MB+$14
        LDA hgr_mod7+48
        STA MB+$15

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

.code
.include "hgr_clear.asm"
.include "hgr_scanline_ram.asm"
.include "hgr_plot_tables.inc"
.include "hgr_div7_ram.asm"
