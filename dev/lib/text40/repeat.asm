; ============================================================================
; repeat.asm -- repeat-character output for borders, padding, separators
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"):
; repeat_char_ax is emitted only when referenced EARLIER in the program and
; not yet defined, so .include this file after the last call. Included too
; early, the build stops with "Symbol '<routine>' is undefined" -- never a
; silent drop.
; ============================================================================
;   repeat_char_ax -- print A (low 7 bits) X times via ECHO.
;     Inputs:   A = character (bit 7 ignored — set internally)
;               X = repeat count (0..255; 0 prints nothing)
;     Output:   X = 0
;     Clobbers: A. Y preserved.
;
; Modelled on Little Tower's GRAPH/GRAPHIT subroutine (dead code,
; deleted from LittleTower-1.0.asm in juillet 2026). Lets future
; projects build grids, padding and separators without re-deriving
; the counter loop.
;
; No ZP usage — character lives on the 6502 stack across each ECHO call.
;
; Caller responsibility: ECHO ($FFEF) in scope (from apple1.inc).
; ============================================================================

.segment "CODE"

.if .referenced(repeat_char_ax) .and (.not .defined(repeat_char_ax))
repeat_char_ax:
        CPX     #0
        BEQ     @done                   ; X = 0 → no-op
        ORA     #$80                    ; bit 7 for ECHO
@lp:    PHA
        JSR     ECHO
        PLA
        DEX
        BNE     @lp
@done:  RTS
.endif
