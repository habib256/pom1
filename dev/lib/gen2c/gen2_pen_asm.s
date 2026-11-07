; gen2_pen_asm.s — gen2c archive member: the shared pixel pen.
;
; Two leaf helpers shared by the white 16x16 glyph blitter (gen2_text16_asm.s)
; and the 1bpp pixel-walk sprite blitter (gen2_blit_asm.s). Their own member so
; neither user drags the other in. Not C-callable (no leading underscore).
;
;   gen2_plot_one : OR the pen's bit into BOTH scanlines (ptr1, ptr2) at byte
;                   column pen_col, then fall into gen2_advance.
;   gen2_advance  : pen_mask <<= 1, wrapping from bit 6 to bit 0 of the next
;                   byte column (HIRES packs 7 pixels per byte; bit 7 is the
;                   palette bit and is never written by the pen).
;
; Pen state lives in cc65 scratch: pen_col = tmp1, pen_mask = tmp2.
; Clobbers A, Y. Split from the former monolithic gen2_blit.s.

        .export   gen2_plot_one, gen2_advance
        .importzp ptr1, ptr2, tmp1, tmp2

curcol  = tmp1               ; current byte column   (also Y during a store)
curmask = tmp2               ; current bit mask

        .segment "CODE"

gen2_plot_one:
        ldy curcol
        lda curmask
        ora (ptr1),y         ; scanline yy
        sta (ptr1),y
        lda curmask
        ora (ptr2),y         ; scanline yy+1
        sta (ptr2),y
        ; fall through to advance

gen2_advance:
        asl curmask          ; 1,2,4,...,64 -> 2,4,...,128
        lda curmask
        cmp #$80             ; was the column's last (bit 6) pixel just passed?
        bne @done
        lda #$01             ; wrap: back to bit 0 of the next byte column
        sta curmask
        inc curcol
@done:
        rts
