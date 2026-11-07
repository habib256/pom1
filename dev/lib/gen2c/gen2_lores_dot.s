; gen2_lores_dot.s — one LORES block: set and read.
;
;   void          gen2_lores_setblock(unsigned char x, unsigned char y,
;                                     unsigned char color);
;     cc65 fastcall: A = colour, C stack = y, x. Off-screen (x >= 40 or
;     y >= 48) is dropped. The other block of the byte is preserved.
;   unsigned char gen2_lores_getblock(unsigned char x, unsigned char y);
;     A = y, C stack = x. Returns the colour 0..15 in A (X = 0), 0 off-screen.
;
; Block (x, y) is nibble (y & 1) of text row y >> 1 at column x: the LOW
; nibble is the upper (even) block, the HIGH nibble the lower (odd) one.
; Port of pom2games dev/lib/hgrc/hgr_lores_setblock.s + hgr_lores_getblock.s
; (same author, GPL-3.0). Clobbers ptr1, ptr2, tmp1-tmp3.

        .export   _gen2_lores_setblock, _gen2_lores_getblock
        .import   gen2_lores_build, _gen2_lo_rowlo, _gen2_lo_rowhi
        .import   incsp1, incsp2
        .importzp sp, ptr1, ptr2, tmp1, tmp2, tmp3

        .code
_gen2_lores_setblock:
        and #$0F
        sta tmp2                ; colour
        ldy #0
        lda (sp),y
        cmp #48
        bcs @done
        sta tmp3                ; y survives the lazy table builder
        iny
        lda (sp),y
        cmp #40
        bcs @done
        sta ptr2                ; x
        jsr gen2_lores_build    ; clobbers only A/X/Y/tmp1
        lda tmp3
        lsr a
        tax                     ; text row; C = odd block-row
        lda _gen2_lo_rowlo,x
        sta ptr1
        lda _gen2_lo_rowhi,x
        sta ptr1+1
        ldy ptr2
        lda #$F0                ; even: keep the lower block
        bcc @even
        lda tmp2
        asl a
        asl a
        asl a
        asl a
        sta tmp2
        lda #$0F                ; odd: keep the upper block
@even:  sta tmp1
        lda (ptr1),y
        and tmp1
        ora tmp2
        sta (ptr1),y
@done:  jmp incsp2

_gen2_lores_getblock:
        cmp #48
        bcs @out
        sta tmp2                ; y survives the lazy table builder
        ldy #0
        lda (sp),y
        cmp #40
        bcs @out
        sta ptr2                ; x
        jsr gen2_lores_build
        lda tmp2
        lsr a
        tax                     ; text row; C = odd block-row
        lda _gen2_lo_rowlo,x
        sta ptr1
        lda _gen2_lo_rowhi,x
        sta ptr1+1
        ldy ptr2
        lda (ptr1),y            ; preserves C
        bcc @low
        lsr a
        lsr a
        lsr a
        lsr a
@low:   and #$0F
        ldx #0
        jmp incsp1
@out:   lda #0
        tax
        jmp incsp1
