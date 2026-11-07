; gen2_lores_tab.s — the LORES text-row table builder (gen2c LORES family).
;
; LORES shares the TEXT page layout: 24 text rows at
;     base + $80*(r & 7) + $28*(r >> 3)        base = $0400 page 1 / $0800 page 2
; each byte holding two vertically stacked blocks. This builds
; gen2_lo_rowlo/gen2_lo_rowhi (owned by gen2_init.c, so gen2_set_draw_page can
; shift them without linking LORES) once, for the current gen2_lo_base
; (BSS 0 = unset -> page 1, $04), then sets gen2_lo_ready.
;
; Internal ABI (not C): clobbers A/X/Y/tmp1 ONLY — the LORES entry points keep
; their decoded arguments in ptr1-ptr3/tmp2-tmp4 across this call.
;
; Port of pom2games dev/lib/hgrc/hgr_lores.s (same author, GPL-3.0; the GEN2
; LORES page is the Apple II one byte for byte). No division, no multiply:
; the table is walked in its own order, the low byte stepping $00/$80 and the
; high byte +1 every two rows, +$28 per third of the screen.

        .export   gen2_lores_build
        .import   _gen2_lo_base, _gen2_lo_ready, _gen2_lo_rowlo, _gen2_lo_rowhi
        .importzp tmp1

        .code
gen2_lores_build:
        lda _gen2_lo_ready
        bne @done
        lda _gen2_lo_base
        bne @base
        lda #$04                ; BSS default: page 1
        sta _gen2_lo_base
@base:  ldx #0                  ; row index, 0..23
        lda #0
        sta tmp1                ; third offset: $00, $28, $50
@third: ldy _gen2_lo_base       ; high byte, base..base+3
@pair:  lda tmp1
        sta _gen2_lo_rowlo,x
        tya
        sta _gen2_lo_rowhi,x
        inx
        lda tmp1
        ora #$80
        sta _gen2_lo_rowlo,x
        tya
        sta _gen2_lo_rowhi,x
        iny
        inx
        txa
        and #7
        bne @pair
        lda tmp1
        clc
        adc #40
        sta tmp1
        cmp #120
        bne @third
        lda #1
        sta _gen2_lo_ready
@done:  rts
