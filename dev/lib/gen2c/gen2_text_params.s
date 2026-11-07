; gen2_text_params.s — gen2c archive member: the text zero-page argument block.
;
; Shared by the 16x16 (gen2_text16_asm.s) and native 8x8 (gen2_text8_asm.s)
; string renderers and written by gen2_text.c (#pragma zpsym in
; gen2_internal.h). Its own member so a program using either renderer links
; one copy of the block and nothing else. 13 bytes of zero page.

        .exportzp _gen2_g_glyph, _gen2_g_col, _gen2_g_mask, _gen2_g_y
        .exportzp _gen2_t_col, _gen2_t_bit, _gen2_t_n, _gen2_t_color
        .exportzp _gen2_t_s, _gen2_t_font

        .segment "ZEROPAGE"
; per-glyph blitter arguments (the string loop fills all but g_y):
_gen2_g_glyph: .res 2        ; pointer to 8 glyph bytes
_gen2_g_col:   .res 1        ; starting byte column
_gen2_g_mask:  .res 1        ; starting bit mask
_gen2_g_y:     .res 1        ; top scanline (written by the C wrapper)
; string renderer arguments:
_gen2_t_col:   .res 1        ; current byte column of the pen
_gen2_t_bit:   .res 1        ; current bit within the column (0..6)
_gen2_t_n:     .res 1        ; glyph cells left to draw (C precomputes the fit)
_gen2_t_color: .res 1        ; 0 = white blitter, else colour blitter
_gen2_t_s:     .res 2        ; string pointer
_gen2_t_font:  .res 2        ; glyph table base (kBBFontAscii)
