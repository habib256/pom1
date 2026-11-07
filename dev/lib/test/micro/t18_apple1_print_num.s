; ============================================================================
; t18_apple1_print_num.s — micro-test: apple1/print_num.asm, all 256 inputs
; ============================================================================
; GUARDS: print_byte_dec's whole contract, exhaustively:
;     - exactly three characters per call, "000".."255", high bit set, in
;       hundreds / tens / units order (checked against an independent decimal
;       counter the driver steps alongside the input byte),
;     - Y preserved (documented; callers keep a column or loop index there),
;     - nothing relies on ECHO keeping the carry: the stub below CLEARS it and
;       scrambles N/V/Z on every call. Wozmon's ECHO happens to keep C, but a
;       project may point ECHO at its own routine.
;   ECHO is the CALLER's symbol (print_num.asm does not include apple1.inc),
;   so the driver supplies a capturing stub instead of the ROM routine -- no
;   display busy-wait, and the output is assertable.
;
;   Mailbox: $0F01 = mismatching inputs (0), $0F02 = first bad input ($FF =
;   none), $0F03 = calls that changed Y (0), $0F04/05 = inputs checked (256).
;
; POM1-LIB-MICRO-TEST
; INCLUDES: apple1/print_num.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 80000
; EXPECT: 0F00 A5 00 FF 00 00 01
; ============================================================================

MB = $0F00

.segment "ZEROPAGE"
val:      .res 1                ; input byte under test
d_h:      .res 1                ; reference decimal counter (hundreds/tens/units)
d_t:      .res 1
d_u:      .res 1
out_n:    .res 1                ; characters captured by the ECHO stub
out:      .res 4                ; ... and the characters themselves
bad:      .res 1
first:    .res 1
ybad:     .res 1
cnt:      .res 2

.segment "ENTRY"
main:
        SEI
        CLD
        LDX     #$FF
        TXS
        LDA     #0
        STA     val
        STA     d_h
        STA     d_t
        STA     d_u
        STA     bad
        STA     ybad
        STA     cnt
        STA     cnt+1
        LDA     #$FF
        STA     first

@next:  LDA     #0
        STA     out_n
        LDY     #$5A            ; Y sentinel
        LDA     val
        JSR     print_byte_dec
        CPY     #$5A
        BEQ     @y_ok
        INC     ybad
@y_ok:
        ; three characters, each = reference digit | $B0
        LDA     out_n
        CMP     #3
        BNE     @fail
        LDA     d_h
        ORA     #$B0
        CMP     out
        BNE     @fail
        LDA     d_t
        ORA     #$B0
        CMP     out+1
        BNE     @fail
        LDA     d_u
        ORA     #$B0
        CMP     out+2
        BEQ     @pass
@fail:  INC     bad
        LDA     first
        CMP     #$FF
        BNE     @pass
        LDA     val
        STA     first
@pass:
        INC     cnt
        BNE     @c1
        INC     cnt+1
@c1:
        ; step the reference counter: units, tens, hundreds
        INC     d_u
        LDA     d_u
        CMP     #10
        BNE     @c2
        LDA     #0
        STA     d_u
        INC     d_t
        LDA     d_t
        CMP     #10
        BNE     @c2
        LDA     #0
        STA     d_t
        INC     d_h
@c2:    INC     val
        BNE     @next           ; 256 inputs, then val wraps to 0

        LDA     bad
        STA     MB+1
        LDA     first
        STA     MB+2
        LDA     ybad
        STA     MB+3
        LDA     cnt
        STA     MB+4
        LDA     cnt+1
        STA     MB+5
        LDA     #$A5
        STA     MB
spin:   JMP     spin

; ECHO stub: append A to out[] (at most 4 kept), keep A/X/Y, clobber C, N, V, Z.
ECHO:
        STX     echo_x
        LDX     out_n
        CPX     #4
        BCS     @full
        STA     out,X
@full:  INC     out_n
        LDX     echo_x
        BIT     echo_nv         ; N = V = 1
        CLC                     ; a caller relying on C across ECHO breaks here
        RTS
echo_nv: .byte  $C0

.segment "BSS"
echo_x:   .res 1

.segment "CODE"
.include "print_num.asm"
