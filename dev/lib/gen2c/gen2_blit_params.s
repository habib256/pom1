; gen2_blit_params.s — gen2c archive member: the sprite-blit zero-page block.
;
; Shared by the pixel-walk blitter (gen2_blit_asm.s), the byte-aligned blit7
; (gen2_blit7_asm.s) and the pre-shift XOR kernels (gen2_preshift_asm.s);
; written by gen2_sprites.c / gen2_preshift.c (#pragma zpsym in
; gen2_internal.h). Its own member: one copy for whichever kernels a program
; links. 9 bytes of zero page.

        .exportzp _gen2_b_col, _gen2_b_mask, _gen2_b_w, _gen2_b_h
        .exportzp _gen2_b_stride, _gen2_b_y, _gen2_b_mode, _gen2_b_src

        .segment "ZEROPAGE"
_gen2_b_col:   .res 1        ; start byte column of the pen
_gen2_b_mask:  .res 1        ; start bit mask (1 << (x % 7)) — pixel walk only
_gen2_b_w:     .res 1        ; pixels (blit_run) / bytes (blit7) drawn per row
_gen2_b_h:     .res 1        ; rows (bottom-clipped by C)
_gen2_b_stride:.res 1        ; source bytes per row
_gen2_b_y:     .res 1        ; current scanline (advanced per row)
_gen2_b_mode:  .res 1        ; 0 SET (OR) / 1 CLEAR (AND ~) / 2 XOR (EOR)
_gen2_b_src:   .res 2        ; source row pointer (advanced per row)
