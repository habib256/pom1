; gen2_page.s — double buffering: the draw page and the display flip.
;
;   void gen2_set_draw_page(unsigned char page);  1 or 2 (anything else -> 1)
;   void gen2_show_page(void);                    display the draw page
;
; gen2_set_draw_page builds the tables if needed, then moves the 192 HIRES
; scanline high bytes to the other page with EOR $60 ($20..$3F <-> $40..$5F,
; the row offset untouched), unrolled eight rows per iteration: ~2 300 cycles
; per flip where the C add loop took ~15 000 — most of a 17 030-cycle frame
; for a program that double-buffers. The LORES row table, once built by the
; LORES family, flips the same way with EOR $0C ($04..$07 <-> $08..$0B);
; until then only gen2_lo_base is set, so a HIRES-only program still links no
; LORES code. Same state, same results as the C it replaces.
;
; gen2_show_page READS $C255 (page 2) or $C254 (page 1): the read is the
; toggle, and an asm read is never elided (the C macros store into
; gen2_ss_sink for cc65's sake).
;
; Ported from pom2games dev/lib/hgrc/hgr_set_draw_page.s + hgr_rows_asm.s
; (same author, GPL-3.0). Clobbers A/X/Y/tmp1/tmp2. No self-modification.

        .export   _gen2_set_draw_page, _gen2_show_page, _gen2_draw_page2
        .import   _gen2_build_tables, _gen2_rowhi, _gen2_hgr_base
        .import   _gen2_lo_base, _gen2_lo_ready, _gen2_lo_rowhi
        .importzp tmp2

        .include "../gen2/gen2.inc"     ; GEN2_PAGE1 / GEN2_PAGE2

        .bss
_gen2_draw_page2: .res 1        ; 0 = drawing on page 1 (the BSS default)

        .code
_gen2_set_draw_page:
        ldx #0
        cmp #2
        bne @norm
        inx
@norm:  stx tmp2                ; gen2_build_tables preserves tmp2
        jsr _gen2_build_tables
        lda tmp2
        sta _gen2_draw_page2
        beq @p1
        ldy #$08                ; LORES base, page 2
        lda #$40                ; HIRES base, page 2
        bne @sel
@p1:    ldy #$04
        lda #$20
@sel:   cmp _gen2_hgr_base
        beq @lores
        sta _gen2_hgr_base      ; the tables are built: flip their high bytes
        ldx #23
@flip:  lda _gen2_rowhi,x
        eor #$60
        sta _gen2_rowhi,x
        .repeat 7, blk
        lda _gen2_rowhi+24*(blk+1),x
        eor #$60
        sta _gen2_rowhi+24*(blk+1),x
        .endrepeat
        dex
        bpl @flip
@lores: lda _gen2_lo_ready
        beq @store              ; not built: the builder reads the base
        cpy _gen2_lo_base
        beq @done
        ldx #23
@lrow:  lda _gen2_lo_rowhi,x
        eor #$0C
        sta _gen2_lo_rowhi,x
        dex
        bpl @lrow
@store: sty _gen2_lo_base
@done:  rts

_gen2_show_page:
        lda _gen2_draw_page2
        beq @p1
        lda GEN2_PAGE2          ; $C255 (the read is the toggle)
        rts
@p1:    lda GEN2_PAGE1          ; $C254
        rts
