; ============================================================================
; prng16.asm -- 16-bit Galois LFSR ($B400 tap), shared by arcade games
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"): prng16 is
; emitted only when referenced EARLIER in the program and not yet defined, so
; .include this file after the last call. Included too early, the build stops
; with "Symbol '<routine>' is undefined" -- never a silent drop. dungeon.asm's
; rand_mod calls prng16: include dungeon.asm first.
; ============================================================================
; Promoted from inline copies in 2 projects (mutualization Tier 2.2). State
; lives in `prng_lo / prng_hi` — same canonical name as `prng8.asm`, so a
; project that uses both LFSRs shares one state pair physically (you'd only
; pick one in practice, but the unified naming lets `dev/lib/apple1/zp.inc`
; declare the slots once and have either lib find them).
;
; Reserves its own ZP slot pair via `.ifndef` guard with the standard
; alias-on-tight-ZP convention:
;
;     prng_lo = my_lo
;     prng_hi = my_hi
;     .include "prng16.asm"
;
; Or, simpler, `.include "zp.inc"` once at the top of your project and the
; slots are pre-declared at $06/$07.
;
;   prng16  -- 16-bit Galois LFSR, polynomial $B400 (taps 16, 14, 13, 11).
;              Returns A = new prng_lo. Mutates prng_lo + prng_hi.
;              Caller must seed the state to nonzero — zeroed state stays
;              zero forever.
;
; A semantically equivalent routine `roll_lfsr` (no return value, uses
; lfsr_lo/lfsr_hi instead of prng_lo/prng_hi) lives in
; `dev/lib/m6502/math.asm`. The two LFSRs are conceptually similar but
; physically distinct state — math.asm's `lfsr_*` lives in BSS (the
; LINEBUF segment for projects that follow its convention), prng16's
; `prng_*` lives in ZEROPAGE for fast access in tight game loops.
; ============================================================================

.ifndef prng_lo
.segment "ZEROPAGE"
prng_lo:        .res 1
prng_hi:        .res 1
.endif

.segment "CODE"

; The high byte stays in A from the shift to the feedback store (pattern of
; the pom2games edition's apple2c/apple2rand.s): 14 bytes instead of 15, and
; 19/20 cycles (no tap / tap) instead of 16/23 -- the same mean, a flatter
; worst case. Same state transition, same return: A = new prng_lo, N/Z from
; it, C = the bit shifted out, X/Y preserved.
.if .referenced(prng16) .and (.not .defined(prng16))
prng16:
        LDA     prng_hi
        LSR     A               ; bit 0 of prng_hi -> C
        ROR     prng_lo         ; C -> bit 7, bit 0 -> C (the shifted-out bit)
        BCC     @store          ; shifted-out bit 0: no feedback
        EOR     #$B4            ; taps 16, 14, 13, 11
@store: STA     prng_hi
        LDA     prng_lo
        RTS
.endif
