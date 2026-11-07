; gen2_carrier_params.s — gen2c archive member: the NTSC artifact-colour
; carrier block, shared by the tinted glyph blitter (gen2_text16_asm.s, for
; gen2_hgr_puts_color) and the colorize pass (gen2_colorize_asm.s). Written by
; the C gen2_set_carrier helpers (gen2_text.c, gen2_colorize.c). Its own
; member so either user links one copy without the other. 3 bytes of zero page.
;
; Carrier values (verified against the GEN2 renderer, see gen2.h): violet
; even=$55 odd=$2A hi=$00 ; green $2A/$55/$00 ; orange = green|hi $80 ;
; blue = violet|hi $80.

        .exportzp _gen2_z_ce, _gen2_z_co, _gen2_z_hi

        .segment "ZEROPAGE"
_gen2_z_ce:    .res 1        ; carrier mask for EVEN byte columns
_gen2_z_co:    .res 1        ; carrier mask for ODD  byte columns
_gen2_z_hi:    .res 1        ; high bit to OR in ($00 green/violet, $80 orange/blue)
