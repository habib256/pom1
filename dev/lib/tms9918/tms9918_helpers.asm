; ============================================================================
; tms9918_helpers.asm -- silicon-strict mutualised VDP-store helpers
; ============================================================================
; Standalone tiny module (no zero-page imports, no other lib dependencies)
; that a TMS9918 project links to dodge the inline `STA port / JSR pad18`
; pairs the silicon-strict patcher would otherwise insert.
;
; Exports:
;   vdp_write_a      -- A -> VDP_DATA, then pad. `JSR vdp_write_a` (3 bytes)
;                       replaces `STA VDP_DATA / JSR tms9918_pad18` (6 bytes)
;                       at the same 22 cycles store-to-store when chained.
;   vdp_ctrl_a       -- A -> VDP_CTRL, same pad (the control-port twin).
;   vdp_set_write_xy -- caller does `LDX #lo / LDY #hi / JSR vdp_set_write_xy`,
;                       saving 4 bytes per site vs. inline 2-byte address
;                       write with padding.
;
; vdp_write_a / vdp_ctrl_a are assembled from tms9918_xfer.asm (one source;
; the library's own modules include the same fragment privately). Body:
; STA port / NOP x3 / RTS = 16 cycles, + the caller's 6-cycle JSR = 22 cycles
; between two chained calls -- the pad18 contract, above the ~16c active-
; display floor measured on real silicon ([A] Replica-1, see tms9918.inc).
; A/X/Y and flags preserved.
;
; History: vdp_write_a used to carry two NOPs (20 cycles chained: above the
; silicon floor, below the repo's 22-cycle contract). The third NOP is a port
; of pom2games dev/lib/apple2/ezcolor.asm's ez_data_w / ez_ctrl_w shape.
;
; CAVEAT (mixed styles): when a helper returns, only 16 cycles have passed
; since its store began. An INLINE `STA VDP_DATA` right after the call needs
; >= 6 more cycles of other work first; tools/lint_tms_lib.py models the
; helpers and flags the short gap.
;
; Option: assemble with `-D TMS_PAIR_SEI=1` to mask IRQs around the two
; control bytes of vdp_set_write_xy (rule (b) of the IRQ caveat in
; tms9918.inc): +3 bytes, +9 cycles (PHP 3 + SEI 2 + PLP 4).
; ============================================================================

.include "tms9918.inc"

.export vdp_write_a, vdp_ctrl_a, vdp_set_write_xy

.segment "CODE"

; ----------------------------------------------------------------------------
; vdp_set_write_xy: prep VRAM auto-incrementing write at address (X=lo, Y=hi).
;   Sets the write flag (bit 6) on the high byte automatically.
;   Caller pattern:
;       LDX #lo                 (2c, 2 bytes)
;       LDY #hi                 (2c, 2 bytes)
;       JSR vdp_set_write_xy    (6c, 3 bytes)  ← 7 bytes, vs 11 inline.
;   The two STA VDP_CTRL writes are 22c apart (helper + TYA/ORA bridge), and
;   the helper's tail pad leaves >= 22c before the caller's next VDP store.
;   Clobbers: A. X, Y consumed.
; ----------------------------------------------------------------------------
vdp_set_write_xy:
        TMS_PAIR_BEGIN
        TXA
        JSR     vdp_ctrl_a      ; address low
        TYA
        ORA     #$40            ; bit 6 = write
        JSR     vdp_ctrl_a      ; address high + write flag
        TMS_PAIR_END
        RTS

.include "tms9918_xfer.asm"
