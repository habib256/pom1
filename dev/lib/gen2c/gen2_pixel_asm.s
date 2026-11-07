; gen2_pixel_asm.s — gen2c archive member: _gen2_plot_asm / _gen2_unplot_asm,
; the bodies of gen2_hgr_plot / gen2_hgr_unplot (gen2_pixel.c).
;
; The byte column (x/7) and bit mask (1<<x%7) come from the gen2_col7 /
; gen2_mask7 lookup tables instead of per-pixel division; x is split into two
; ranges because a 6502 index is 8-bit: x<256 indexes the LUT directly, x>=256
; (only 256..279 on a 280-wide screen) indexes it at +256 with the low byte.
; Owns its 3-byte zero-page argument block. Destroys A, X, Y, ptr1, tmp1, tmp2.
; Split from the former monolithic gen2_blit.s.

        .export   _gen2_plot_asm, _gen2_unplot_asm
        .exportzp _gen2_p_x, _gen2_p_y
        .import   _gen2_rowlo, _gen2_rowhi, _gen2_col7, _gen2_mask7
        .importzp ptr1, tmp1, tmp2

        .segment "ZEROPAGE"
_gen2_p_x:     .res 2        ; pixel x (0..279)
_gen2_p_y:     .res 1        ; pixel y (0..191)

        .segment "CODE"

; --- pixel address setup: shared by plot/unplot ------------------------------
; Leaves: ptr1 = scanline base, Y = byte column, A = bit mask (1 << x%7).
gen2_pixsetup:
        lda _gen2_p_x+1      ; x high byte
        bne @hi
        ldx _gen2_p_x        ; x low (0..255)
        lda _gen2_col7,x
        sta tmp1
        lda _gen2_mask7,x
        jmp @row
@hi:
        ldx _gen2_p_x        ; x low (0..23 -> x 256..279)
        lda _gen2_col7+256,x
        sta tmp1
        lda _gen2_mask7+256,x
@row:
        sta tmp2             ; mask
        ldy _gen2_p_y
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ldy tmp1             ; Y = byte column
        lda tmp2             ; A = mask
        rts

; --- _gen2_plot_asm : set (OR in) one white pixel ----------------------------
_gen2_plot_asm:
        jsr gen2_pixsetup    ; Y=col, A=mask, ptr1=scanline base
        ora (ptr1),y
        sta (ptr1),y
        rts

; --- _gen2_unplot_asm : clear (AND off) one pixel ----------------------------
_gen2_unplot_asm:
        jsr gen2_pixsetup
        eor #$ff             ; ~mask
        and (ptr1),y
        sta (ptr1),y
        rts
