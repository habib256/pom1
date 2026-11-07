; gen2_lores_run.s — LORES spans and rectangles, one clipped kernel.
;
;   void gen2_lores_hlin(unsigned char x0, unsigned char x1, unsigned char y,
;                        unsigned char color);           C stack = y, x1, x0
;   void gen2_lores_vlin(unsigned char x, unsigned char y0, unsigned char y1,
;                        unsigned char color);           C stack = y1, y0, x
;   void gen2_lores_fill_rect(unsigned char x, unsigned char y, unsigned char w,
;                             unsigned char h, unsigned char color);
;                                                        C stack = h, w, y, x
; cc65 fastcall, A = colour (low nibble). Same clipping as the C they replace:
; hlin/vlin are inclusive, clipped to the 40x48 grid, an empty or reversed span
; draws nothing; fill_rect trims w/h at the edges (w or h 0 draws nothing).
;
; Each wrapper consumes its C arguments and hands the kernel x / width-1 in
; ptr2, the first block row / one-past-last in tmp3 / tmp4 and the colour in
; tmp2, all non-empty and on-screen. The kernel walks text rows: a row whose
; two blocks are both covered is a straight byte store; only an unmatched top
; or bottom block needs a read-modify-write of its nibble. The C version did a
; read-modify-write per block through a C call per block row.
;
; Port of pom2games dev/lib/hgrc/hgr_lores_draw.s / _hlin.s / _vlin.s /
; _rect.s (same author, GPL-3.0). No new BSS or zero page. Clobbers ptr1-ptr3,
; tmp1-tmp4.

        .export   _gen2_lores_hlin, _gen2_lores_vlin, _gen2_lores_fill_rect
        .import   gen2_lores_build, _gen2_lo_rowlo, _gen2_lo_rowhi
        .import   incsp3, incsp4
        .importzp sp, ptr1, ptr2, ptr3, tmp1, tmp2, tmp3, tmp4

        .code
_gen2_lores_hlin:
        and #$0F
        sta tmp2
        ldy #0
        lda (sp),y              ; y
        cmp #48
        bcs @done
        sta tmp3
        adc #1                  ; the bound check left C clear
        sta tmp4                ; bottom, exclusive
        iny
        lda (sp),y              ; x1, inclusive
        cmp #40
        bcc :+
        lda #39
:       tax
        inx
        stx ptr2+1              ; right, exclusive
        iny
        lda (sp),y              ; x0
        cmp ptr2+1
        bcs @done               ; reversed or off-screen span
        sta ptr2
        lda ptr2+1
        sbc ptr2                ; C clear: right - x0 - 1
        sta ptr2+1              ; width - 1
        jsr incsp3
        jmp draw
@done:  jmp incsp3

_gen2_lores_vlin:
        and #$0F
        sta tmp2
        ldy #0
        lda (sp),y              ; y1, inclusive
        cmp #48
        bcc :+
        lda #47
:       tax
        inx
        stx tmp4                ; bottom, exclusive
        iny
        lda (sp),y              ; y0
        cmp tmp4
        bcs @done               ; reversed or off-screen span
        sta tmp3
        iny
        lda (sp),y              ; x
        cmp #40
        bcs @done
        sta ptr2
        lda #0
        sta ptr2+1              ; width - 1
        jsr incsp3
        jmp draw
@done:  jmp incsp3

_gen2_lores_fill_rect:
        and #$0F
        sta tmp2
        ldy #3
        lda (sp),y              ; x
        cmp #40
        bcs @done
        sta ptr2
        dey
        lda (sp),y              ; y
        cmp #48
        bcs @done
        sta tmp3
        dey
        lda (sp),y              ; w, up to 255
        beq @done
        adc ptr2                ; the y bound check left C clear
        bcs @rclip
        cmp #40
        bcc @rok
@rclip: lda #40
@rok:   clc
        sbc ptr2
        sta ptr2+1              ; width - 1
        dey
        lda (sp),y              ; h, up to 255
        beq @done
        clc
        adc tmp3
        bcs @bclip
        cmp #48
        bcc @bok
@bclip: lda #48
@bok:   sta tmp4                ; bottom, exclusive
        jsr incsp4
        jmp draw
@done:  jmp incsp4

; --- the kernel -------------------------------------------------------------
draw:
        jsr gen2_lores_build    ; clobbers only A/X/Y/tmp1
        lda tmp2
        asl a
        asl a
        asl a
        asl a
        ora tmp2
        sta tmp2                ; colour in both nibbles
@row:   lda tmp3
        lsr a
        tax
        lda _gen2_lo_rowlo,x
        clc
        adc ptr2                ; row low <= $D0, x <= 39: no carry
        sta ptr1
        lda _gen2_lo_rowhi,x
        sta ptr1+1
        ldy ptr2+1
        lda tmp3
        lsr a
        bcs @upper              ; starts on an odd block: lower nibble only
        inc tmp3
        lda tmp3
        cmp tmp4
        beq @lower              ; ends on an even block: upper nibble only
        inc tmp3                ; both blocks of this text row
        lda tmp2
@full:  sta (ptr1),y
        dey
        bpl @full
@next:  lda tmp3
        cmp tmp4
        bcc @row
        rts
@upper: inc tmp3
        lda #$0F                ; keep the upper (even) block
        bne @part
@lower: lda #$F0                ; keep the lower (odd) block
@part:  sta tmp1
        eor #$FF
        and tmp2
        sta ptr3                ; the replacement nibble
@rmw:   lda (ptr1),y
        and tmp1
        ora ptr3
        sta (ptr1),y
        dey
        bpl @rmw
        jmp @next
