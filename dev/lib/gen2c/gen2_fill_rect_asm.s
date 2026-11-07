; gen2_fill_rect_asm.s — gen2c archive member: _gen2_fill_rect_asm, the
; byte-aligned rectangle fill behind gen2_hgr_fill_rect (gen2_fill_rect.c).
;
; Stores _gen2_f_val into byte columns [col0, col0+cols) on each of `rows`
; scanlines starting at f_y0 — the fast erase behind text/sprites. Scanline
; bases come from the gen2_rowlo/gen2_rowhi tables. The C wrapper guarantees
; rows >= 1, cols >= 1, and that the rectangle is on-screen.
; Y counts down from the last byte as offset and counter and the bytes are
; stored two per loop pass (19 cycles a pair; an odd width starts with one
; single store); ptr1 carries the first column (row low <= $D0 + column <= 39:
; no carry). Ported from pom2games dev/lib/hgrc/hgr_byte_rect_asm.s (same
; author, GPL-3.0). Pinned by micro-test t43_c_rect.
; Owns its 5-byte zero-page argument block. Destroys A, X, Y, ptr1, tmp1-tmp3.
; Split from the former monolithic gen2_blit.s.

        .export   _gen2_fill_rect_asm
        .exportzp _gen2_f_y0, _gen2_f_rows, _gen2_f_col0, _gen2_f_cols, _gen2_f_val
        .import   _gen2_rowlo, _gen2_rowhi
        .importzp ptr1, tmp1, tmp2, tmp3

        .segment "ZEROPAGE"
_gen2_f_y0:    .res 1        ; top scanline of the rectangle
_gen2_f_rows:  .res 1        ; number of scanlines (>= 1)
_gen2_f_col0:  .res 1        ; first byte column
_gen2_f_cols:  .res 1        ; number of byte columns (>= 1)
_gen2_f_val:   .res 1        ; fill byte (0 = erase)

        .segment "CODE"
_gen2_fill_rect_asm:
        lda _gen2_f_y0
        sta tmp1                ; current scanline
        lda _gen2_f_rows
        sta tmp2                ; rows remaining
        lda _gen2_f_cols
        and #1                  ; an odd width starts with one single store
        sta tmp3
@row:   ldy tmp1
        lda _gen2_rowlo,y
        clc
        adc _gen2_f_col0        ; row low <= $D0 + column <= 39: no carry
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ldy _gen2_f_cols
        dey                     ; Y counts down from the last byte
        lda _gen2_f_val
        ldx tmp3
        bne @single
@pair:  sta (ptr1),y            ; 19 cycles a pair; Y is also the counter
        dey
@single:
        sta (ptr1),y
        dey
        bpl @pair
        inc tmp1
        dec tmp2
        bne @row
        rts
