; gen2_hgr_x2.s — hand-asm gen2_hgr_inflate_x2 (mono x1 -> x2 single-colour).
; -----------------------------------------------------------------------------
; The C form of this function (gen2_hgr_x2_ref.c, kept as the HOST reference for the
; hgr_inflate_x2_smoke cross-check) is MISCOMPILED by cc65 2.18 -Oirs: the
; optimiser drops the whole pixel-setting body, so every x2 sprite inflates to
; all-zero on the 6502. Rather than fight the optimiser with #pragma optimize
; (correct but slow naive codegen), the target build uses this hand-asm: fast,
; and immune to the miscompile. Pinned on-target by dev/lib/test/micro/
; t13_c_inflate_x2.c and t44_c_inflate_x2_all.c (ctest lib_micro_tests) and
; cross-checked against the host C / the editor's magnifyColor2x by
; hgr_inflate_x2_smoke.
;
; void gen2_hgr_inflate_x2(const unsigned char *mono, unsigned char wbytes,
;                          unsigned char h, unsigned char color,
;                          unsigned char *out);
; cc65 fastcall: A/X = out; C stack = color, h, wbytes, mono lo, mono hi.
;
; BYTE-WISE: one source byte (7 pixels, bit 0 leftmost, bit 7 ignored) is
; exactly two output bytes (14 dots). Its low nibble and its three high pixels
; go through a 16-entry spread table (bit i -> bit 2i), giving the 7 LEFT dots
; of a 16-bit dot word; the right dots are that word << 1, white is both. The
; word's bits 0..6 and 7..13 are the two output bytes, stored on BOTH doubled
; rows. The palette bit (blue/orange) is ORed into an output byte only when
; that byte is non-zero — exactly the C's rule, which ORs it with every lit
; dot and leaves an unlit byte at zero. Colours: BLACK and anything >= 6
; light nothing, WHITE both dots, VIOLET/BLUE the left (even) dot, GREEN/
; ORANGE the right (odd) one, BLUE/ORANGE with bit 7. The old kernel walked
; every source pixel with 21 zero-page bytes of state; this needs none of its
; own (cc65 scratch only).
;
; Output layout as before: row-pairs of 2*wbytes bytes, contiguous
; (2*wbytes x 2*h bytes in all). wbytes or h of 0 writes nothing (the old
; kernel wrote 256 bytes per row for wbytes 0); so does an output that would
; not fit a 16-bit pointer (wbytes*h > 16383).
;
; Ported from pom2games dev/lib/hgrc/hgr_x2_target.s + hgr_inflate_x2_asm.s
; (same author, GPL-3.0). No self-modification.

        .export   _gen2_hgr_inflate_x2
        .importzp sp, ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

        .rodata
spread: .byte 0, 1, 4, 5, 16, 17, 20, 21, 64, 65, 68, 69, 80, 81, 84, 85

        .code
; --- entry: decode the C arguments -----------------------------------------
; Kernel ABI: ptr1 = source, ptr2 = out, tmp1 = wbytes, tmp2 = h, tmp3 = colour;
; the mono slots at (sp)+3/+4 are reused for the 16-bit doubled-row stride.
_gen2_hgr_inflate_x2:
        sta ptr2
        stx ptr2+1
        ldy #0
        lda (sp),y              ; colour
        sta tmp3
        iny
        lda (sp),y              ; h
        beq @done
        sta tmp2
        sta tmp4
        iny
        lda (sp),y              ; wbytes
        beq @done
        sta tmp1
        lda #0                  ; high byte of wbytes * h, 8 add/rotate steps
        ldx #8
@mul:   lsr tmp4
        bcc @rot
        clc
        adc tmp1
@rot:   ror a
        dex
        bne @mul
        cmp #64                 ; 4 * wbytes * h must fit 16 bits
        bcs @done
        ldy #3
        lda (sp),y
        sta ptr1
        iny
        lda (sp),y
        sta ptr1+1
        lda tmp1                ; stride = 2 * wbytes, into the mono slots
        asl a
        ldy #3
        sta (sp),y
        iny
        lda #0
        rol a
        sta (sp),y
        jsr inflate
@done:  clc                     ; drop the 5 argument bytes
        lda sp
        adc #5
        sta sp
        bcc :+
        inc sp+1
:       rts

; --- the kernel ---------------------------------------------------------------
inflate:
        lda tmp3
        cmp #6
        bcc @valid
        lda #0                  ; unknown colour: black, like the C
@valid: cmp #4
        bcc @cready
        clc
        adc #126                ; blue/orange -> left/right code + bit 7
@cready:
        sta tmp3                ; bits 0-1: 0 black 1 white 2 left 3 right
@row:   ldy #3                  ; ptr3 = ptr2 + stride: the second row
        clc
        lda (sp),y
        adc ptr2
        sta ptr3
        iny
        lda (sp),y
        adc ptr2+1
        sta ptr3+1
        lda tmp1
        sta tmp4                ; source bytes left in this row
@byte:  ldy #0
        lda (ptr1),y
        and #15
        tax
        lda spread,x
        sta ptr4                ; left dots of pixels 0-3
        lda (ptr1),y
        lsr a
        lsr a
        lsr a
        lsr a
        and #7                  ; source bit 7 is never a pixel
        tax
        lda spread,x
        sta ptr4+1              ; left dots of pixels 4-6
        lda tmp3
        and #3
        cmp #2
        beq @split              ; left dots as they are
        cmp #3
        beq @right
        cmp #1
        beq @white
        lda #0                  ; black
        sta ptr4
        sta ptr4+1
        jmp @split
@white: lda ptr4                ; both dots: word | word << 1
        asl a
        ora ptr4
        sta ptr4
        lda ptr4+1
        rol a
        ora ptr4+1
        sta ptr4+1
        jmp @split
@right: asl ptr4                ; right dots: word << 1
        rol ptr4+1
@split: lda ptr4                ; byte 0 = dots 0..6
        and #$7F
        tax
        asl ptr4                ; byte 1 = dots 7..13
        lda ptr4+1
        rol a
        sta ptr4+1
        txa
        beq @lo
        bit tmp3
        bpl @lo
        ora #$80                ; palette bit on a lit byte only
@lo:    sta (ptr2),y
        sta (ptr3),y
        iny
        lda ptr4+1
        beq @hi
        bit tmp3
        bpl @hi
        ora #$80
@hi:    sta (ptr2),y
        sta (ptr3),y
        inc ptr1
        bne :+
        inc ptr1+1
:       clc
        lda ptr2
        adc #2
        sta ptr2
        bcc :+
        inc ptr2+1
:       clc
        lda ptr3
        adc #2
        sta ptr3
        bcc :+
        inc ptr3+1
:       dec tmp4
        beq @rowend
        jmp @byte
@rowend:
        lda ptr3                ; the next row-pair starts after the second row
        sta ptr2
        lda ptr3+1
        sta ptr2+1
        dec tmp2
        beq @end
        jmp @row
@end:   rts
