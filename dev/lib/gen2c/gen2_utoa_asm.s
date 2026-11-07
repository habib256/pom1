; gen2_utoa_asm.s — gen2c archive member: the legacy _gen2_utoa entry.
;
; _gen2_utoa (value in gen2_u_lo/hi, buffer at gen2_u_ptr) is now an ALIAS of
; the one shared converter, gfx_u16_dec (dev/lib/gfx/gfx_num_dec.s), so a
; binary carries a single 16-bit binary-to-decimal routine. The gen2_u_* slots
; alias that routine's cc65 scratch (tmp1 / tmp2 / ptr1): no zero page of their
; own (the former gen2_blit.s spent 4 bytes on them). Because they ARE cc65
; scratch, only asm may fill them, immediately before the call; C callers use
; gfx_utoa(buf, value) instead (gen2_text_num.c does). Kept for any caller of
; the private gen2_internal.h interface. Requires gfx-gen2.lib on the link line.

        .export   _gen2_utoa
        .exportzp _gen2_u_lo, _gen2_u_hi, _gen2_u_ptr
        .import   gfx_u16_dec
        .importzp ptr1, tmp1, tmp2

_gen2_u_lo  = tmp1           ; value low byte  (consumed)
_gen2_u_hi  = tmp2           ; value high byte (consumed)
_gen2_u_ptr = ptr1           ; output buffer pointer
_gen2_utoa  = gfx_u16_dec    ; Y = digit count on return
