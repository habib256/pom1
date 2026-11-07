; gen2_blit7_asm.s — gen2c archive member: _gen2_blit7_run, the BYTE-ALIGNED
; blit of a 7px/byte sprite (gen2_hgr_blit7, and gen2_hgr_sprite's SET/CLEAR).
;
; The source is pre-packed in the framebuffer's own 7px/byte layout (bit 0 =
; leftmost, bit 7 = 0), so each source byte XOR/OR/ANDs STRAIGHT into a byte
; column — no per-pixel mask walk, no cross-byte shift. The pen is therefore
; byte-aligned: the C wrapper passes b_col = x/7. For a solid sprite this is ~7x
; fewer memory ops than _gen2_blit_run (one op per 7px byte vs one per pixel).
; Reuses the gen2_b_* block (gen2_blit_params.s): b_col start byte column, b_w
; bytes drawn per row, b_stride source bytes per row, b_h rows, b_y top
; scanline, b_src source, b_mode 0 SET / 1 CLEAR / 2 XOR. Y indexes src[j] AND
; dest[col+j] together (ptr1 already points at rowbase+col).
;
; b_mode is LOOP-INVARIANT, so it is dispatched ONCE here to one of three
; specialised row loops (macro-generated so they can't drift), instead of a
; per-byte mode test (~9 wasted cycles on every byte). No zero page of its own;
; destroys A, Y, ptr1. Split from the former monolithic gen2_blit.s.

        .export   _gen2_blit7_run
        .import   _gen2_rowlo, _gen2_rowhi
        .importzp _gen2_b_col, _gen2_b_w, _gen2_b_h
        .importzp _gen2_b_stride, _gen2_b_y, _gen2_b_mode, _gen2_b_src
        .importzp ptr1

.macro  BLIT7_LOOP m
        .local  row, col, nyc
row:    ldy     _gen2_b_y               ; ptr1 = rowbase(y) + col
        lda     _gen2_rowlo,y
        clc
        adc     _gen2_b_col
        sta     ptr1
        lda     _gen2_rowhi,y
        adc     #0
        sta     ptr1+1
        ldy     #0                      ; Y = byte index (src[j] AND dest[col+j])
col:    lda     (_gen2_b_src),y
.if m = 1
        eor     #$FF                    ; CLEAR: dest &= ~src
        and     (ptr1),y
.elseif m = 2
        eor     (ptr1),y                ; XOR: dest ^= src
.else
        ora     (ptr1),y                ; SET: dest |= src
.endif
        sta     (ptr1),y
        iny
        cpy     _gen2_b_w
        bne     col
        clc                             ; src += stride ; y += 1 ; rows--
        lda     _gen2_b_src
        adc     _gen2_b_stride
        sta     _gen2_b_src
        bcc     nyc
        inc     _gen2_b_src+1
nyc:    inc     _gen2_b_y
        dec     _gen2_b_h
        bne     row
        rts
.endmacro

        .segment "CODE"

_gen2_blit7_run:
        lda     _gen2_b_mode
        beq     blit7_set               ; 0 = SET
        cmp     #2
        beq     blit7_xor               ; 2 = XOR
        BLIT7_LOOP 1                     ; 1 = CLEAR (fall-through)
blit7_set:
        BLIT7_LOOP 0
blit7_xor:
        BLIT7_LOOP 2
