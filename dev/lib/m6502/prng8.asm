; ============================================================================
; prng8.asm -- 8-bit shift LFSR ($2D tap), shared by maze generators
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"): random is
; emitted only when referenced EARLIER in the program and not yet defined, so
; .include this file after the last call. Included too early, the build stops
; with "Symbol '<routine>' is undefined" -- never a silent drop.
; ============================================================================
; Promoted from inline copies in 3 projects (mutualization Tier 2.2). Reserves
; its own ZP slot pair (prng_lo / prng_hi) via `.ifndef` guard so a ZP-tight
; caller can alias them to existing scratch slots before the .include:
;
;     prng_lo = my_lo
;     prng_hi = my_hi
;     .include "prng8.asm"
;
;   random  -- 8-bit shift LFSR, polynomial $2D.
;              Returns A = new prng_lo. Side-effect: advances prng_lo
;              and shifts prng_hi (ROL prng_hi writes it back).
;              Caller must seed the state to nonzero somewhere — zeroed
;              state stays zero forever.
; ============================================================================

.ifndef prng_lo
.segment "ZEROPAGE"
prng_lo:        .res 1
prng_hi:        .res 1
.endif

.segment "CODE"

.if .referenced(random) .and (.not .defined(random))
random:
        LDA     prng_lo
        ASL     A
        ROL     prng_hi
        BCC     @nf
        EOR     #$2D
@nf:    STA     prng_lo
        RTS
.endif
