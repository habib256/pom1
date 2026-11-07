; ============================================================================
; hgr_xor.asm -- self-erasing XOR rectangles and pre-shifted glyphs (GEN2 HGR)
; ============================================================================
; Drawing the same object twice gives the original bytes back exactly
; (holes, colour phase bit and all): the cheapest way to move a cursor, a
; paddle or a selection box over a background you do not want to redraw.
;
;   hgr_xor_rect    hx_x / hx_y / hx_w / hx_h in pixels: w, h > 0,
;                   x + w <= 256, y + h <= 192. EORs every pixel of the
;                   rectangle (bits 0-6; bit 7, the palette bit, untouched).
;                   Destroys hx_h.
;   hgr_xor_sprite  hx_x / hx_y and hx_data -> 8 rows of TWO bytes (bit 7
;                   clear, bit 0 = leftmost pixel), ALREADY shifted for
;                   x mod 7 by the caller (a 7-entry table of pointers is
;                   the usual way). Both destination bytes must lie in the
;                   40 visible columns; y + 8 <= 192.
;   hx_page         $00 = page 1 ($2000), $60 = page 2 ($4000): EORed into
;                   the hgr_hi high byte. INIT IT. With hgr_flip.asm driving
;                   the page, leave it at $00.
;
; Measured (scratch 6502 core, JSR/RTS included): hgr_xor_sprite 748
; cycles; hgr_xor_rect 10 x 10 at x = 3: 896, 70 x 20: 5 876.
; Code 162 B (14 B of masks included). No clipping, no video sync, not
; reentrant.
; Clobbers A, X, Y and the hx_* scratch.
;
; Needs hgr_lo / hgr_hi (hgr_scanline.inc) and two 256-entry x tables:
; x / 7 and x mod 7. Defaults: hgr_div7 / hgr_mod7 (hgr_div7_ram.asm);
; define HGR_XOR_DIV7 / HGR_XOR_MOD7 before the include to alias others.
; Zero page: 15 B (hx_*); each hx_* is skipped if the program defined it
; before the include (alias it onto scratch of your own).
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_xor.asm, used by
; ARKABREAKOUT). The framebuffer layout is the same; page 2 is $4000 on both.
; ============================================================================
.ifndef _HGR_XOR_LOADED_
_HGR_XOR_LOADED_ = 1

.ifndef HGR_XOR_DIV7
HGR_XOR_DIV7 = hgr_div7
.endif
.ifndef HGR_XOR_MOD7
HGR_XOR_MOD7 = hgr_mod7
.endif

.pushseg
.zeropage
.ifndef hx_ptr
hx_ptr: .res 2
.endif
.ifndef hx_data
hx_data: .res 2
.endif
.ifndef hx_x
hx_x: .res 1
.endif
.ifndef hx_y
hx_y: .res 1
.endif
.ifndef hx_w
hx_w: .res 1
.endif
.ifndef hx_h
hx_h: .res 1
.endif
.ifndef hx_page
hx_page: .res 1
.endif
.ifndef hx_first
hx_first: .res 1
.endif
.ifndef hx_last
hx_last: .res 1
.endif
.ifndef hx_lm
hx_lm: .res 1
.endif
.ifndef hx_rm
hx_rm: .res 1
.endif
.ifndef hx_row
hx_row: .res 1
.endif
.ifndef hx_index
hx_index: .res 1
.endif
.popseg

hgr_xor_rect:
        ldx hx_x
        lda HGR_XOR_DIV7,x
        sta hx_first
        lda HGR_XOR_MOD7,x
        tax
        lda hx_left_masks,x
        sta hx_lm
        lda hx_x
        clc
        adc hx_w
        tax
        dex                             ; x + w - 1: the last pixel
        lda HGR_XOR_DIV7,x
        sta hx_last
        lda HGR_XOR_MOD7,x
        tax
        lda hx_right_masks,x
        sta hx_rm
        lda hx_y
        sta hx_row
@line:  ldx hx_row
        lda hgr_lo,x
        sta hx_ptr
        lda hgr_hi,x
        eor hx_page
        sta hx_ptr+1
        ldy hx_first
        lda hx_lm                       ; first byte: left mask
@byte:  cpy hx_last
        beq @last
        eor (hx_ptr),y
        sta (hx_ptr),y
        iny
        lda #$7f                        ; middle bytes: all seven pixels
        bne @byte
@last:  and hx_rm
        eor (hx_ptr),y
        sta (hx_ptr),y
        inc hx_row
        dec hx_h
        bne @line
        rts

hgr_xor_sprite:
        ldx hx_x
        lda HGR_XOR_DIV7,x
        sta hx_first
        lda hx_y
        sta hx_row
        lda #0
        sta hx_index
@line:  ldx hx_row
        lda hgr_lo,x
        sta hx_ptr
        lda hgr_hi,x
        eor hx_page
        sta hx_ptr+1
        ldy hx_index
        lda (hx_data),y
        ldy hx_first
        eor (hx_ptr),y
        sta (hx_ptr),y
        inc hx_index
        ldy hx_index
        lda (hx_data),y
        ldy hx_first
        iny
        eor (hx_ptr),y
        sta (hx_ptr),y
        inc hx_index
        inc hx_row
        lda hx_index
        cmp #16
        bne @line
        rts

hx_left_masks: .byte $7f,$7e,$7c,$78,$70,$60,$40
hx_right_masks: .byte $01,$03,$07,$0f,$1f,$3f,$7f

.endif  ; _HGR_XOR_LOADED_
