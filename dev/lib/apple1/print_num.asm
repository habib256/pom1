; ============================================================================
; print_num.asm -- decimal byte output for Apple-1
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"):
; print_byte_dec is emitted only when referenced EARLIER in the program and
; not yet defined, so .include this file after the last call. Included too
; early, the build stops with "Symbol '<routine>' is undefined" -- never a
; silent drop.
; ============================================================================
;   print_byte_dec -- A = unsigned byte (0..255). Prints exactly three ASCII
;                     decimal digits via Wozmon ECHO ($FFEF). Leading zeros
;                     are emitted ("042", not " 42") — keeps column layout
;                     stable in tabular output. Clobbers A, X. Y preserved.
;
; Modelled on the inline print_3_digits from games_sokoban/Sokoban.asm:592
; (where it ships in 4 KB), promoted to lib/apple1 so any project can drop
; in via `.include "print_num.asm"`.
;
; No ZP usage — intermediate digits live on the 6502 stack via PHA/PLA.
;
; Caller responsibility: ECHO ($FFEF) must be in scope (inline equate or
; `.include "apple1.inc"`). This module does NOT re-include apple1.inc.
; ============================================================================

.segment "CODE"

; ----------------------------------------------------------------------------
; print_byte_dec -- A = byte (0..255), prints "000".."255" via ECHO.
;                   Clobbers A, X. Y preserved.
; ----------------------------------------------------------------------------
.if .referenced(print_byte_dec) .and (.not .defined(print_byte_dec))
print_byte_dec:
        ; Hundreds digit -- count successful subtractions of 100 in X. The
        ; loop overshoots by one (X starts at -1, the last SBC borrows), and
        ; ADC #100 with C clear takes that last subtraction back.
        LDX     #$FF
        SEC
@h:     INX
        SBC     #100
        BCS     @h              ; no borrow -> one more hundred
        ADC     #100            ; C = 0 here: A = remainder 0..99 (C ends 1)
        PHA                     ; save remainder for the tens digit
        TXA
        ORA     #'0' | $80
        JSR     ECHO
        PLA

        ; Tens digit -- same pattern. SEC again rather than trusting ECHO to
        ; keep C: a project may point ECHO at its own routine.
        LDX     #$FF
        SEC
@t:     INX
        SBC     #10
        BCS     @t
        ADC     #10             ; A = units 0..9
        PHA
        TXA
        ORA     #'0' | $80
        JSR     ECHO
        PLA

        ; Units digit -- whatever's left in A.
        ORA     #'0' | $80
        JMP     ECHO            ; tail call
.endif
