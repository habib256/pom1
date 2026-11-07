; gen2_rect_run.s — the rectangle kernels behind gen2_rect.c (RECT family).
;
;   _gen2_pixrect_run    fill/erase the PIXEL rectangle [r_x..r_xr] x r_rows
;                        from r_y0 (r_mode 1 = white, 0 = black)
;   _gen2_fill_rect_run  store f_val into byte columns [f_col0, +f_cols) on
;                        f_rows scanlines from f_y0
;
; Same parameter blocks (the _gen2_r_* / _gen2_f_* zero-page bytes of
; gen2_blit.s), same results as gen2_blit.s's _gen2_pixrect_asm /
; _gen2_fill_rect_asm, which they replace; faster because the full bytes of a
; row are stored TWO per loop pass:
;   - pixrect: the edge bytes keep their read-modify-write, the run between
;     them is pairs (21 cycles a pair: sta/iny/sta/iny/dex/bne) plus one odd
;     byte, counts computed once per call; the old run tested CPY per byte;
;   - byte rect: Y counts down from the last byte as offset and counter, pairs
;     at 19 cycles, an odd width starting with one single store; ptr1 carries
;     the first column (row low <= $D0 + column <= 39: no carry).
; Every keep mask still clears bit 7 (the palette bit) on the edges, exactly
; as before. The edge state lives in cc65's tmp3/tmp4/ptr2-ptr4 and one BSS
; byte: no zero page of its own (the old kernel kept 7 gr_* bytes in ZP).
;
; The C wrapper (gen2_rect.c) clips and guarantees rows >= 1, cols >= 1,
; x <= xr <= 279, an on-screen rectangle and built tables. Destroys A/X/Y,
; ptr1-ptr4, tmp1-tmp4. No self-modification.
;
; Ported from pom2games dev/lib/hgrc/hgr_pixrect_asm.s + hgr_byte_rect_asm.s
; (same author, GPL-3.0).

        .export   _gen2_pixrect_run, _gen2_fill_rect_run
        .import   _gen2_col7, _gen2_mask7, _gen2_rowlo, _gen2_rowhi
        .importzp _gen2_r_x, _gen2_r_xr, _gen2_r_y0, _gen2_r_rows, _gen2_r_mode
        .importzp _gen2_f_y0, _gen2_f_rows, _gen2_f_col0, _gen2_f_cols, _gen2_f_val
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

colL  = tmp3                    ; left byte column
colR  = tmp4                    ; right byte column (== colL for one byte)
keepL = ptr3                    ; left byte: AND mask
setL  = ptr3+1                  ; left byte: OR mask
keepR = ptr4                    ; right byte: AND mask
setR  = ptr4+1                  ; right byte: OR mask

        .bss
setF:   .res 1                  ; full bytes: $7F fill / $00 erase

        .code
_gen2_pixrect_run:
        ; --- left edge: colL = col7[x], leftMask (bits x%7..6) -> tmp1 ------
        lda _gen2_r_x+1
        bne @xhi
        ldx _gen2_r_x
        lda _gen2_col7,x
        sta colL
        lda _gen2_mask7,x
        jmp @xmask
@xhi:   ldx _gen2_r_x
        lda _gen2_col7+256,x
        sta colL
        lda _gen2_mask7+256,x
@xmask: sec                     ; A = 1<<bitL ; leftMask = ~(A-1) & $7F
        sbc #1
        eor #$7F                ; A-1 is 0..63
        sta tmp1
        ; --- right edge: colR = col7[xr], rightMask (bits 0..xr%7) -> tmp2 --
        lda _gen2_r_xr+1
        bne @rhi
        ldx _gen2_r_xr
        lda _gen2_col7,x
        sta colR
        lda _gen2_mask7,x
        jmp @rmask
@rhi:   ldx _gen2_r_xr
        lda _gen2_col7+256,x
        sta colR
        lda _gen2_mask7+256,x
@rmask: asl a                   ; A = 1<<bitR ; rightMask = (A<<1) - 1
        sbc #0                  ; ASL cleared C: subtract one -> 1..127
        sta tmp2
        ; --- keep/set per region ----------------------------------------------
        lda colR
        cmp colL                ; C = 1 (ordered edges), kept to the count
        bne @masks
        lda tmp1                ; one byte: both edges' masks combined
        and tmp2
        sta tmp1
@masks: lda _gen2_r_mode
        bne @set
        lda tmp1                ; erase: keep = ~mask & $7F, set = 0
        eor #$7F
        sta keepL
        lda tmp2
        eor #$7F
        sta keepR
        lda #0
        sta setL
        sta setF
        sta setR
        beq @fill               ; always
@set:   lda #$7F                ; fill: keep = $7F (drops the palette bit)
        sta keepL
        sta keepR
        sta setF
        lda tmp1
        sta setL
        lda tmp2
        sta setR
@fill:  lda _gen2_r_y0
        sta tmp1                ; current scanline
        lda _gen2_r_rows
        sta tmp2                ; rows remaining
        ; full bytes between the edges: pairs in ptr2, the odd one in ptr2+1
        ; (a one-byte rectangle uses neither). C = 1 from the edge compare.
        lda colR
        sbc colL
        beq @row
        sbc #1                  ; C = 1: colR - colL - 1
        lsr a
        sta ptr2
        lda #0
        rol a
        sta ptr2+1
@row:   ldy tmp1
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ldy colL                ; left (or only) byte: (b & keepL) | setL
        lda (ptr1),y
        and keepL
        ora setL
        sta (ptr1),y
        cpy colR
        beq @next               ; one-byte rectangle: row done
        iny
        ldx ptr2
        beq @odd                ; no pair between the edges
        lda setF
@pair:  sta (ptr1),y            ; 21 cycles a pair
        iny
        sta (ptr1),y
        iny
        dex
        bne @pair
@odd:   ldx ptr2+1
        beq @right
        lda setF
        sta (ptr1),y
        iny
@right: lda (ptr1),y            ; right byte: (b & keepR) | setR
        and keepR
        ora setR
        sta (ptr1),y
@next:  inc tmp1
        dec tmp2
        bne @row
        rts

_gen2_fill_rect_run:
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
