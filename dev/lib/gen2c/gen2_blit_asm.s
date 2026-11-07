; gen2_blit_asm.s — gen2c archive member: _gen2_blit_run, the 1bpp MSB-first
; pixel-walk sprite blitter behind gen2_hgr_blit (modes SET / CLEAR / XOR).
;
; HIRES packs 7 pixels/byte (bit 7 = palette), so we cannot shift-and-OR bytes:
; we walk pixel by pixel with the shared pen (gen2_pen_asm.s: pen_col = tmp1,
; pen_mask = tmp2, bits 0..6 only — bit 7 is never written, palette preserved).
; Per source row: load the MSB-first bits into a shift register, draw
; gen2_b_w pixels (the C wrapper right-clips so the pen never leaves col
; 0..39), then step source by gen2_b_stride and y by 1. XOR mode makes a moving
; sprite erasable by re-blitting (draw, then draw again at the old spot).
;
; Arguments: the gen2_b_* block (gen2_blit_params.s). No zero page of its own:
; the source byte index lives in cc65's ptr2 (unused by the pen on this path).
; Destroys A, X, Y, ptr1, ptr2, tmp1..tmp4. Split from the former monolithic
; gen2_blit.s.

        .export   _gen2_blit_run
        .import   _gen2_rowlo, _gen2_rowhi
        .import   gen2_advance
        .importzp _gen2_b_col, _gen2_b_mask, _gen2_b_w, _gen2_b_h
        .importzp _gen2_b_stride, _gen2_b_y, _gen2_b_mode, _gen2_b_src
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

b_srcidx = ptr2              ; source byte index within the current row

        .segment "CODE"

_gen2_blit_run:
@row:
        ldy _gen2_b_y           ; ptr1 = scanline base
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        lda _gen2_b_col         ; reset pen to the sprite's left edge
        sta tmp1                ; curcol
        lda _gen2_b_mask
        sta tmp2                ; curmask
        ldy #0                  ; load first source byte of this row
        lda (_gen2_b_src),y
        sta tmp3                ; srcbits (MSB-first shift register)
        lda #8
        sta tmp4                ; bits left in srcbits
        sty b_srcidx            ; srcidx = 0
        ldx _gen2_b_w           ; pixels this row
@px:
        asl tmp3                ; C = next pixel (bit 7 first)
        bcc @skip
        jsr blit_apply          ; pixel set: SET/CLEAR/XOR + advance pen
        jmp @after
@skip:
        jsr gen2_advance        ; pixel clear: just advance the pen
@after:
        dec tmp4                ; consumed a source bit
        bne @next
        inc b_srcidx            ; byte exhausted -> next source byte
        ldy b_srcidx
        lda (_gen2_b_src),y
        sta tmp3
        lda #8
        sta tmp4
@next:
        dex
        bne @px
        clc                     ; next source row + next scanline
        lda _gen2_b_src
        adc _gen2_b_stride
        sta _gen2_b_src
        bcc @nyc
        inc _gen2_b_src+1
@nyc:
        inc _gen2_b_y
        dec _gen2_b_h
        bne @row
        rts

; apply one pixel at (curcol=tmp1, curmask=tmp2) on scanline ptr1, then advance.
blit_apply:
        ldy tmp1                ; curcol
        lda _gen2_b_mode
        beq @bset
        cmp #2
        beq @bxor
        lda tmp2                ; CLEAR: dest &= ~mask (keeps the palette bit)
        eor #$FF
        and (ptr1),y
        sta (ptr1),y
        jmp gen2_advance
@bset:
        lda tmp2                ; SET: dest |= mask
        ora (ptr1),y
        sta (ptr1),y
        jmp gen2_advance
@bxor:
        lda tmp2                ; XOR: dest ^= mask
        eor (ptr1),y
        sta (ptr1),y
        jmp gen2_advance
