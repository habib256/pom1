; gen2_text8_asm.s — gen2c archive member: native 8x8 string renderer behind
; gen2_hgr_puts8 / gen2_hgr_putu8 (and the gfx_text GEN2 cell backend).
;
;   _gen2_puts_run8   whole NUL-terminated string, 8px pitch
;   _gen2_blit_glyph8 one glyph at native size (no pixel doubling)
;
; The Beautiful Boot font is 7px wide (bit 0 = leftmost, bit 7 always 0), so
; each glyph is a 7px cell; the pen advances 8px (one blank column = inter-char
; gap). One source bit -> one screen pixel on ONE scanline. Arguments: the
; gen2_g_* / gen2_t_* block (gen2_text_params.s). Its own member so a dense-HUD
; program does not link the 16x16 blitters. No zero page of its own; destroys
; A, X, Y, ptr1, tmp1..tmp4. Split from the former monolithic gen2_blit.s.

        .export   _gen2_puts_run8, _gen2_blit_glyph8
        .import   _gen2_rowlo, _gen2_rowhi
        .importzp _gen2_g_glyph, _gen2_g_col, _gen2_g_mask, _gen2_g_y
        .importzp _gen2_t_col, _gen2_t_bit, _gen2_t_n, _gen2_t_s, _gen2_t_font
        .importzp ptr1, tmp1, tmp2, tmp3, tmp4

curcol  = tmp1               ; current byte column
curmask = tmp2               ; current bit mask
curbits = tmp3               ; remaining source bits of the current row
rowcnt  = tmp4               ; row counter 0..7

        .segment "CODE"

; --- _gen2_blit_glyph8 : draw the 8x8 glyph at NATIVE size (no pixel doubling) -
_gen2_blit_glyph8:
        lda #0
        sta rowcnt
@rowloop:
        ; ptr1 = gen2_row(g_y + row)   (single scanline — no paired ptr2)
        lda rowcnt
        clc
        adc _gen2_g_y
        tay
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ; load this row's 8 source bits
        ldy rowcnt
        lda (_gen2_g_glyph),y
        sta curbits
        ; reset pen to the glyph's left edge
        lda _gen2_g_col
        sta curcol
        lda _gen2_g_mask
        sta curmask
        ldx #8                ; 8 bits (bit 7 is blank -> a no-op plot)
@bitloop:
        lsr curbits           ; bit 0 (leftmost) -> carry
        bcc @skip
        ldy curcol            ; lit: one pixel on this scanline
        lda curmask
        ora (ptr1),y
        sta (ptr1),y
@skip:
        asl curmask           ; advance the pen one pixel
        lda curmask
        cmp #$80
        bne @nextbit
        lda #$01
        sta curmask
        inc curcol
@nextbit:
        dex
        bne @bitloop
        inc rowcnt
        lda rowcnt
        cmp #8
        bne @rowloop
        rts

; --- _gen2_puts_run8 : native 8x8 string render (8px pitch) ------------------
; Same param block as _gen2_puts_run (gen2_t_col/bit/n/s/font); draws each glyph
; with gen2_blit_glyph8 and steps the pen 8px (= 1 byte column + 1 bit).
_gen2_puts_run8:
@loop:
        lda _gen2_t_n
        beq @done
        ldy #0
        lda (_gen2_t_s),y       ; c = *s
        bne @go
@done:
        rts
@go:
        cmp #$20                ; clamp non-printable -> space
        bcc @space
        cmp #$80
        bcc @okc
@space:
        lda #$20
@okc:
        sec                     ; gen2_g_glyph = font + (c-$20)*8
        sbc #$20
        sta tmp1
        lda #0
        sta tmp2
        asl tmp1
        rol tmp2
        asl tmp1
        rol tmp2
        asl tmp1
        rol tmp2
        lda tmp1
        clc
        adc _gen2_t_font
        sta _gen2_g_glyph
        lda tmp2
        adc _gen2_t_font+1
        sta _gen2_g_glyph+1
        lda _gen2_t_col
        sta _gen2_g_col
        ldx _gen2_t_bit         ; gen2_g_mask = 1 << bit
        lda #1
@mk:
        dex
        bmi @mkd
        asl a
        jmp @mk
@mkd:
        sta _gen2_g_mask
        jsr _gen2_blit_glyph8
        ; pen += 8px: bit += 1 (+ carry into col on the 7px wrap)
        lda _gen2_t_bit
        clc
        adc #1
        cmp #7
        bcc @nowrap
        sbc #7                  ; carry set by cmp (>=7): A = bit+1-7
        sta _gen2_t_bit
        lda _gen2_t_col
        clc
        adc #2                  ; col += 1, +1 for the bit wrap
        sta _gen2_t_col
        jmp @adv2
@nowrap:
        sta _gen2_t_bit
        lda _gen2_t_col
        clc
        adc #1
        sta _gen2_t_col
@adv2:
        inc _gen2_t_s           ; ++s
        bne @sok
        inc _gen2_t_s+1
@sok:
        dec _gen2_t_n
        jmp @loop
