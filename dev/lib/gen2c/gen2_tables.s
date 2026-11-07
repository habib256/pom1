; gen2_tables.s — the gen2c lookup tables, their builder, and gen2_hgr_row
; (CORE: every drawing module calls gen2_build_tables()).
;
;   unsigned char gen2_rowlo[192], gen2_rowhi[192]   scanline bases, CURRENT
;                                                     draw page (BSS)
;   unsigned char gen2_col7[280]                      x / 7
;   unsigned char gen2_mask7[280]                     1 << (x % 7)
;   unsigned char gen2_phase7[280]                    x % 7
;   void gen2_build_tables(void)       idempotent, builds all five once
;   unsigned char *gen2_hgr_row(unsigned char y)      (public, gen2.h)
;
; The C builder computed each scanline base through gen2_hgr_row (a soft x40
; multiply per row) and walked x in a 16-bit C loop: 236 939 cycles on first
; use. Here the rows are walked in table order (+4 pages per line, -32 and a
; $80 toggle per 8-line group, +40 per third) and the three x tables are
; filled in one 8-bit pass, x = 256..279 copied from x = 4..27 (256 =
; 36*7 + 4) with 36 added to the column. Same values, same symbols.
;
; gen2_build_tables's internal ABI: clobbers A/X/Y/tmp1 only (the asm
; gen2_set_draw_page keeps its argument in tmp2 across it). The base page is
; gen2_hgr_base (gen2_init.c; BSS 0 = unset -> $20, page 1); gen2_set_draw_page
; moves the high bytes afterwards.
;
; gen2_hgr_row(y) evaluates the interleave directly (no table, no build, ~70
; cycles; the C took ~640) — for EVERY y, 192..255 included, exactly as the C
; formula did:
;     (gen2_hgr_base << 8) + (y&7)*$400 + ((y>>3)&7)*$80 + (y>>6)*40
;
; Ported from pom2games dev/lib/hgrc/hgr_build_rows.s and hgr_columns.s
; (HGRC_X7_FOLDED build; same author, GPL-3.0). Main RAM, no self-modification.

        .export   _gen2_build_tables, _gen2_hgr_row
        .export   _gen2_rowlo, _gen2_rowhi, _gen2_col7, _gen2_mask7, _gen2_phase7
        .import   _gen2_hgr_base
        .importzp tmp1, tmp2

        .bss
_gen2_rowlo:  .res 192
_gen2_rowhi:  .res 192
_gen2_col7:   .res 280
_gen2_mask7:  .res 280
_gen2_phase7: .res 280
ready:        .res 1           ; BSS 0 = not built yet

        .code
_gen2_build_tables:
        lda ready
        beq @build
        rts
@build: inc ready
        ; --- scanline bases ---------------------------------------------------
        lda _gen2_hgr_base
        bne @base
        lda #$20                ; BSS default: page 1
        sta _gen2_hgr_base
@base:  tay                     ; Y = high byte
        ldx #0                  ; X = y
        stx tmp1                ; tmp1 = low byte
@row:   tya
        sta _gen2_rowhi,x
        lda tmp1
        sta _gen2_rowlo,x
        iny                     ; next line of the group: +$400
        iny
        iny
        iny
        inx
        txa
        and #7
        bne @row
        tya                     ; next 8-line group: undo the +$2000,
        sec                     ; toggle $80 in the low byte, carry into
        sbc #32                 ; the high byte every second group
        tay
        lda tmp1
        eor #$80
        sta tmp1
        bmi @grp
        iny
@grp:   cpx #192
        beq @cols
        txa
        and #63
        bne @row
        lda tmp1                ; next third: low byte +40, high = base
        clc
        adc #40
        sta tmp1
        ldy _gen2_hgr_base
        bne @row                ; base is never 0 here
        ; --- x tables, x = 0..255 -------------------------------------------
@cols:  lda #0
        sta tmp1                ; column
        tay                     ; phase
        tax                     ; x
@lo:    lda tmp1
        sta _gen2_col7,x
        lda bits,y
        sta _gen2_mask7,x
        tya
        sta _gen2_phase7,x
        iny
        cpy #7
        bne @same
        ldy #0
        inc tmp1
@same:  inx
        bne @lo
        ; --- x = 256..279: phases/masks of 4..27, columns + 36. The CPY above
        ; left C = 0 (phase 4 when x wrapped); ADC cannot carry, CPX keeps C=0.
@hi:    lda _gen2_col7+4,x
        adc #36
        sta _gen2_col7+256,x
        lda _gen2_mask7+4,x
        sta _gen2_mask7+256,x
        lda _gen2_phase7+4,x
        sta _gen2_phase7+256,x
        inx
        cpx #280-256
        bne @hi
        rts
bits:   .byte $01, $02, $04, $08, $10, $20, $40

; --- unsigned char *gen2_hgr_row(unsigned char y) — fastcall, A = y ----------
_gen2_hgr_row:
        sta tmp1                ; y
        lda _gen2_hgr_base
        bne @b
        lda #$20                ; BSS default: page 1
        sta _gen2_hgr_base
@b:     sta tmp2
        lda tmp1
        and #7
        asl a
        asl a                   ; (y & 7) * 4 pages
        ora tmp2
        sta tmp2
        lda tmp1
        lsr a
        lsr a
        lsr a
        lsr a
        and #3                  ; ((y >> 3) & 7) >> 1 pages
        ora tmp2
        tax                     ; high byte
        lda tmp1
        and #$08
        beq @e
        lda #$80                ; ((y >> 3) & 1) * $80
@e:     sta tmp2
        lda tmp1
        rol a
        rol a
        rol a
        and #3                  ; y >> 6
        tay
        lda third,y             ; (y >> 6) * 40, at most 120: no carry
        ora tmp2
        rts
third:  .byte 0, 40, 80, 120
