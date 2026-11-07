; ============================================================================
; hgr_plot16.asm -- full-width (280 px) OR/XOR pixel, clipped (GEN2 HGR)
; ============================================================================
; hgr_plot16   hp_x: 16-bit unsigned X (0..279 drawn), hp_y: Y (0..191
;              drawn). Anything off-screen (x >= 280, a "negative" x such as
;              $FFxx, y >= 192) is dropped BEFORE any table access.
;              hp_mode = 0: OR; nonzero: XOR (bit 7, the palette bit, is
;              never changed by XOR). OR keeps bit 7 too, unless
;              HP_COLOR_TABLE is defined: hp_color then indexes that
;              caller-owned palette table ($00 / $80 entries), whose value
;              replaces bit 7 of the written byte.
;              82 cycles a pixel with JSR/RTS (89 past x = 255; hgr_line16
;              on top of it: ~210 a pixel), 25 when clipped.
;
; This is an ADDITION, not a replacement: plot_pixel (hgr_plot.asm) stays
; the fast unclipped 8-bit-X OR plot (~45 cycles). Use hgr_plot16 where the
; whole 280-pixel width or clipping matters, and for hgr_line16.
;
; Options (define before the include):
;   HP_COLOR_TABLE = <table>  palette per hp_color (see above).
;   HP_OR_PLOT = <routine>    called in OR mode with A = the composed byte,
;                             Y = the byte column; may change A before the
;                             store; must preserve Y and hp_* (X is free).
;   HP_PAGE = <byte>          a selector of your own ($00 / $60) EORed into
;                             the hgr_hi high byte (the ht_page convention).
;                             Without it the routine follows hgr_hi (hence
;                             hgr_flip.asm).
; hp_* zero page (7 B, +1 hp_color with HP_COLOR_TABLE): each one is
; skipped if defined before the include (alias your own scratch).
; Needs hgr_lo / hgr_hi and the full-width hgr_col / hgr_mask
; (hgr_plot_tables.inc with HGR_FULL_WIDTH_TABLES = 1; the build fails
; otherwise). Clobbers A, X, Y, hp_ptr, hp_mask. Non-reentrant. Code 69 B.
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_plot.asm, LOGO's
; pixel there); renamed hgr_plot16.asm in POM1 because hgr_plot.asm is
; plot_pixel here. HP_PAGE is POM1's.
; ============================================================================
.ifndef _HGR_PLOT16_LOADED_
_HGR_PLOT16_LOADED_ = 1
; Columns 256..279 read hgr_col/mask+256: with hgr_plot_tables.inc (either
; include order) the build fails unless they are full width.
.ifdef HGR_PLOT_COLUMNS
.assert HGR_PLOT_COLUMNS = 280, error, "hgr_plot16 needs HGR_FULL_WIDTH_TABLES = 1 (columns 256..279)"
.endif

.pushseg
.zeropage
.ifndef hp_x
hp_x: .res 2
.endif
.ifndef hp_y
hp_y: .res 1
.endif
.ifndef hp_ptr
hp_ptr: .res 2
.endif
.ifndef hp_mask
hp_mask: .res 1
.endif
.ifndef hp_mode
hp_mode: .res 1
.endif
.ifdef HP_COLOR_TABLE
.ifndef hp_color
hp_color: .res 1
.endif
.endif
.popseg

hgr_plot16:
        lda hp_y
        cmp #192
        bcs @off
        lda hp_x+1
        beq @low
        cmp #1
        bne @off
        ldx hp_x
        cpx #24
        bcs @off
        lda hgr_mask+256,x
        sta hp_mask
        ldy hgr_col+256,x
        jmp @row
@low:   ldx hp_x
        lda hgr_mask,x
        sta hp_mask
        ldy hgr_col,x
@row:   ldx hp_y
        lda hgr_lo,x
        sta hp_ptr
        lda hgr_hi,x
.ifdef HP_PAGE
        eor HP_PAGE
.endif
        sta hp_ptr+1
        lda (hp_ptr),y
        ldx hp_mode
        bne @xor
        ora hp_mask
.ifdef HP_COLOR_TABLE
        and #$7f
        ldx hp_color
        ora HP_COLOR_TABLE,x
.endif
.ifdef HP_OR_PLOT
        jsr HP_OR_PLOT
.endif
        sta (hp_ptr),y
        rts
@xor:   eor hp_mask
        sta (hp_ptr),y
@off:   rts

.endif  ; _HGR_PLOT16_LOADED_
