; ============================================================================
; t30_math_div10.s — micro-test: m6502/math.asm div_arg_by_10, 516 dividends
; ============================================================================
; GUARDS: div_arg_by_10 (the digit extractor under print_decimal) for every
;   dividend of a 129-step walk over the whole 16-bit range (512 values,
;   wrapping) plus the edges 0, 9, 10 and 65535. Each result is checked by the
;   driver itself, independently of any golden table:
;       remainder < 10   and   quotient * 10 + remainder = dividend
;   (quotient * 10 built as q*8 + q*2, 17-bit, so an overflow cannot hide).
;   t06 pins one vector (12345); a remainder kept one too long or a
;   comparison against the wrong bound slips past a single vector, not this.
;   Also pinned: Y and `tmp` come back untouched (the remainder lives in A
;   since 2026-11; the old loop parked it in `tmp`).
;
;   Mailbox: $0F01 = wrong results (0), $0F02/03 = first bad dividend
;   ($FFFF = none), $0F04 = calls that changed Y or tmp (0), $0F05/06 =
;   dividends checked (516 = $0204).
;
; POM1-LIB-MICRO-TEST
; LIBS: m6502/math.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 150000
; EXPECT: 0F00 A5 00 FF FF 00 04 02
; ============================================================================

.import div_arg_by_10

.segment "ZEROPAGE"
tmp:      .res 1
tmp2:     .res 1
arg_lo:   .res 1
arg_hi:   .res 1
arg2_lo:  .res 1
arg2_hi:  .res 1
th_lo:    .res 1
th_hi:    .res 1
.exportzp tmp, tmp2, arg_lo, arg_hi, arg2_lo, arg2_hi, th_lo, th_hi
n:        .res 2                ; dividend under test
rem:      .res 1
p:        .res 3                ; q * 10, 17 bits
q2:       .res 3                ; q * 2
bad:      .res 1
first:    .res 2
regbad:   .res 1
cnt:      .res 2
walk:     .res 2

.segment "BSS"
prod_lo:   .res 1
prod_hi:   .res 1
sign_flag: .res 1
lfsr_lo:   .res 1
lfsr_hi:   .res 1
.export prod_lo, prod_hi, sign_flag, lfsr_lo, lfsr_hi

MB = $0F00

.segment "CODE"
main:
        SEI
        CLD
        LDX     #$FF
        TXS
        LDA     #0
        STA     bad
        STA     regbad
        STA     cnt
        STA     cnt+1
        STA     n
        STA     n+1
        LDA     #$FF
        STA     first
        STA     first+1

        ; the walk: n = 0, 129, 258, ... (512 values, wrapping past 65535)
        LDA     #<512
        STA     walk
        LDA     #>512
        STA     walk+1
@walk:  JSR     check
        CLC
        LDA     n
        ADC     #129
        STA     n
        BCC     @w1
        INC     n+1
@w1:    LDA     walk
        BNE     @w2
        DEC     walk+1
@w2:    DEC     walk
        LDA     walk
        ORA     walk+1
        BNE     @walk

        ; the edges
        LDX     #0
@edge:  LDA     edges,X
        STA     n
        LDA     edges+1,X
        STA     n+1
        TXA
        PHA
        JSR     check
        PLA
        TAX
        INX
        INX
        CPX     #8
        BNE     @edge

        LDA     bad
        STA     MB+1
        LDA     first
        STA     MB+2
        LDA     first+1
        STA     MB+3
        LDA     regbad
        STA     MB+4
        LDA     cnt
        STA     MB+5
        LDA     cnt+1
        STA     MB+6
        LDA     #$A5
        STA     MB
spin:   JMP     spin

edges:  .word   0, 9, 10, 65535

; Divide n, verify, count.
check:  LDA     n
        STA     arg_lo
        LDA     n+1
        STA     arg_hi
        LDA     #$C3            ; tmp sentinel
        STA     tmp
        LDY     #$5A            ; Y sentinel
        JSR     div_arg_by_10
        STA     rem
        CPY     #$5A
        BNE     @regs
        LDA     tmp
        CMP     #$C3
        BEQ     @regs_ok
@regs:  INC     regbad
@regs_ok:
        INC     cnt
        BNE     @c1
        INC     cnt+1
@c1:    LDA     rem
        CMP     #10
        BCS     @bad
        ; p = q*8 + q*2 (q = arg, 16-bit; p is 17-bit)
        LDA     arg_lo
        STA     q2
        LDA     arg_hi
        STA     q2+1
        LDA     #0
        STA     q2+2
        ASL     q2
        ROL     q2+1
        ROL     q2+2            ; q*2
        LDA     q2
        STA     p
        LDA     q2+1
        STA     p+1
        LDA     q2+2
        STA     p+2
        ASL     p
        ROL     p+1
        ROL     p+2
        ASL     p
        ROL     p+1
        ROL     p+2             ; q*8
        CLC
        LDA     p
        ADC     q2
        STA     p
        LDA     p+1
        ADC     q2+1
        STA     p+1
        LDA     p+2
        ADC     q2+2
        STA     p+2             ; q*10
        CLC
        LDA     p
        ADC     rem
        STA     p
        LDA     p+1
        ADC     #0
        STA     p+1
        LDA     p+2
        ADC     #0
        BNE     @bad            ; > 65535
        LDA     p
        CMP     n
        BNE     @bad
        LDA     p+1
        CMP     n+1
        BNE     @bad
        RTS
@bad:   INC     bad
        LDA     first
        AND     first+1
        CMP     #$FF
        BNE     @ret
        LDA     n
        STA     first
        LDA     n+1
        STA     first+1
@ret:   RTS
