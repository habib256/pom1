; ============================================================================
; delay.asm -- approximate millisecond delay for Apple-1 (1.022727 MHz)
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"): delay_ms_a
; is emitted only when referenced EARLIER in the program and not yet defined,
; so .include this file after the last call. Included too early, the build
; stops with "Symbol '<routine>' is undefined" -- never a silent drop.
; ============================================================================
; One routine, no ZP usage (uses X for outer count, Y for inner).
;
;   delay_ms_a -- delay approximately A milliseconds. A = ms count.
;                 Clobbers A, X, Y.
;
; Calibration at POM1_CPU_CLOCK_HZ = 1 022 727 (real Apple-1 clock):
;   - Inner loop (LDY #203 / DEY / BNE @i): 2 + 5*202 + 4 = 1016 cycles
;   - Outer wrap (DEX / BNE @o): 5 cycles taken, 4 cycles falling through
;   - Per ms: ~1021 cycles → 0.998 ms, accuracy ~0.2%.
;
; Edge cases:
;   - A = 0  → wraps to 256 ms (TAX gives X=0, BNE @o falls through to
;              DEX immediately, X becomes $FF and the outer loop runs 256
;              times). If you don't want this, the caller must guard with
;              CMP/BEQ before JSR.
;   - Apple-1 has no interrupts so the loop is timing-stable.
;   - For delays > 255 ms, JSR multiple times (e.g. 1 s = LDA #250 / JSR
;     delay_ms_a / LDA #250 / JSR delay_ms_a / LDA #250 / JSR delay_ms_a /
;     LDA #250 / JSR delay_ms_a).
;
; Drop in via `.include "delay.asm"`.
; ============================================================================

.segment "CODE"

.if .referenced(delay_ms_a) .and (.not .defined(delay_ms_a))
delay_ms_a:
        TAX                     ; X = ms count
@o:     LDY     #203            ; inner reload calibrated for ~1 ms @ 1.022 MHz
@i:     DEY
@bi:    BNE     @i
        DEX
@bo:    BNE     @o
        RTS
        ; Inner BNE staying in the same page is part of the 1 ms calibration:
        ; a page-cross adds 1 cycle per iteration (~+20% drift). A taken
        ; branch pays it when the address AFTER the branch (@bi+2 / @bo+2)
        ; is in another page than its target -- not the branch's own address.
        ; The old asserts tested @i+2 / @o+2 and missed exactly the case of
        ; the BNE operand sitting at $xxFF (fixed 2026-11-07, from the
        ; pom2games edition).
        .assert >(@bi+2) = >@i, error, "delay_ms_a inner branch crosses a page"
        .assert >(@bo+2) = >@o, error, "delay_ms_a outer branch crosses a page"
.endif
