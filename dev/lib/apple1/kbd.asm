; ============================================================================
; kbd.asm -- Apple-1 keyboard primitives factored out of 15+ projects
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"): poll_key and
; wait_key are each emitted only when referenced EARLIER in the program and
; not yet defined, so .include this file after the last call. Included too
; early, the build stops with "Symbol '<routine>' is undefined" -- never a
; silent drop. kbd_take, the shared tail, comes with either. menu.asm calls
; wait_key, so it is included before this file.
; ============================================================================
; Two routines (one shared tail), no ZP usage. X, Y and C are preserved.
;
;   wait_key  -- block until a key is ready, return A = key with bit 7
;                cleared. Standard Apple-1 PIA bit-7 strobe convention.
;
;   poll_key  -- non-blocking read. A = key (bit 7 cleared) if a key was
;                pending, A = 0 otherwise. The 6502 Z flag reflects the
;                same: BEQ branches when no key was available.
;
; Caller responsibility: KBD ($D010) and KBDCR ($D011) must be in scope —
; either via inline equates or `.include "apple1.inc"`. This module does
; NOT re-include apple1.inc to avoid duplicate-symbol errors in projects
; that declare KBD/KBDCR inline.
;
; Drop in via `.include "kbd.asm"` (Makefile already passes -I ../../lib/apple1).
; ============================================================================

.segment "CODE"

; Layout (ported from the pom2games edition, commit c17beec -- "keyboard
; entries"): both entries end on ONE shared tail, `kbd_take`, which is only
; reached once the KBDCR strobe has been seen set. wait_key's loop falls
; straight into it, poll_key branches to it, so the read + bit-7 strip is not
; duplicated. 19 bytes instead of 25; wait_key costs the same cycles as
; before, poll_key one more with a key (taken BMI) and one less without.

; ----------------------------------------------------------------------------
; poll_key -- non-blocking. A = key (bit 7 stripped) if KBDCR strobe set,
;             A = 0 otherwise. Z flag reflects the same.
;             Clobbers A. X, Y and C preserved.
; ----------------------------------------------------------------------------
.if .referenced(poll_key) .and (.not .defined(poll_key))
poll_key:
        LDA     KBDCR
.ifdef kbd_take                 ; tail emitted by an earlier include: it may
        BPL     @none           ; be out of branch range
        JMP     kbd_take
@none:
.else
        BMI     kbd_take        ; bit 7 = 1 -> a key is waiting
.endif
        LDA     #$00            ; no key: A = 0, Z = 1
        RTS
.endif

; ----------------------------------------------------------------------------
; wait_key -- block until a key is ready. A = key with bit 7 cleared.
;             Clobbers A. X, Y and C preserved.
; ----------------------------------------------------------------------------
.if .referenced(wait_key) .and (.not .defined(wait_key))
wait_key:
        LDA     KBDCR
        BPL     wait_key        ; bit 7 = 0 -> no key, keep polling
.ifdef kbd_take
        JMP     kbd_take        ; the tail was emitted by an earlier include
.endif                          ; (otherwise it follows: fall through)
.endif
.if (.referenced(kbd_take) .or .referenced(wait_key)) .and (.not .defined(kbd_take))
kbd_take:
        LDA     KBD             ; [B] MC6821: reading the data port clears CA1's flag
        AND     #$7F            ; strip the PIA bit-7 strobe (sets Z/N from the key)
        RTS
.endif
