; ============================================================================
; t07_rand_mod.s — micro-test: games/rogue dungeon.asm rand_mod + prng16
; ============================================================================
; GUARDS: the dungeon generator's random primitive, end to end: rand_mod
;   stores `max` in the caller-owned `tmp` (documented clobber), pulls a fresh
;   prng16 sample and reduces by repeated subtraction. The 16-value golden
;   sequence (computed from the $B400 right-shift Galois model, seed $1234)
;   pins BOTH the LFSR trajectory and the modulo reduction; the final
;   prng_lo/prng_hi state pins that rand_mod advanced the PRNG exactly once
;   per call. If prng16 ever started using `tmp` (the tmp-clobber class —
;   rand_mod holds `max` in tmp ACROSS its JSR prng16), the reduction loop
;   would compare against garbage and the sequence changes.
;   Also pinned ($0F13): rand_mod returns with Z/N set from A. The old
;   CMP-based loop left Z = 0 whatever A was, so the rogues' `JSR rand_mod /
;   BEQ` (monster drop, ai_death's coin flip) never saw a 0; MB+19 counts the
;   calls whose Z disagreed with A (0).
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 4000
; EXPECT: 0F00 A5 06 01 00 05 05 00 00 00 05 00 08 07 03 04 00 00 6E 39 00
; ============================================================================

.include "apple1.inc"
.include "zp.inc"               ; canonical tmp + prng_lo/prng_hi slots


MB = $0F00

.segment "ENTRY"
main:
        APPLE1_PREAMBLE

        LDA     #$34            ; seed = $1234 (must be nonzero)
        STA     prng_lo
        LDA     #$12
        STA     prng_hi

        LDA     #0
        STA     MB+19
        LDX     #0
@loop:  TXA
        PHA
        LDA     #10             ; rand_mod(10) -> [0..9]
        JSR     rand_mod
        PHP                     ; Z must say whether A = 0
        TAY
        BEQ     @a0
        PLP
        BNE     @zok            ; A <> 0 and Z = 0
        BEQ     @zbad
@a0:    PLP
        BEQ     @zok            ; A = 0 and Z = 1
@zbad:  INC     MB+19
@zok:
        PLA
        TAX
        TYA
        STA     MB+1,X          ; MB+1..MB+16: golden sequence
        INX
        CPX     #16
        BNE     @loop

        LDA     prng_lo
        STA     MB+17           ; $6E — full 16-bit trajectory pin
        LDA     prng_hi
        STA     MB+18           ; $39

        LDA     #$A5
        STA     MB
spin:   JMP     spin

; .include-style libs (module-local symbols, like every real consumer), after
; the callers: they emit on demand (dev/lib/README.md, "Assembled on demand").
.include "dungeon.asm"          ; rand_mod (needs prng16 + tmp)
.include "prng16.asm"           ; zp.inc pre-declared prng_lo/hi -> no dup
