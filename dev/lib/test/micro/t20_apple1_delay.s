; ============================================================================
; t20_apple1_delay.s — micro-test: apple1/delay.asm cycle budget
; ============================================================================
; GUARDS: delay_ms_a's calibration, in CPU cycles, measured by the machine
;   itself. The A1-IO card's 65C22 Timer 1 counts the cycles across the call;
;   the same call made to a bare RTS is subtracted, so only the routine's own
;   body is left -- whatever constant the timer start and the reads add
;   cancels. delay.asm's header states the budget, and this is that budget:
;     body(A) = TAX 2 + A * 1021 - 1 (last DEX/BNE falls through)
;               + RTS 6 - the bare RTS 6          = 1021 * A + 1 cycles
;     A = 1  ->  1022 = $03FE   (0.9993 ms at 1.022727 MHz)
;     A = 10 -> 10211 = $27E3
;     A = 50 -> 51051 = $C76B
;   A drift here is what the page-cross .asserts in delay.asm exist to stop
;   (+1 cycle per inner iteration = +20 %): this test would see +203 a ms.
;   `--no-dram-refresh`: the refresh stall steals CPU cycles while the timer
;   keeps counting, and it is the routine, not the stall, under test here.
;
;   Mailbox: $0F01.. = the three bodies, little-endian.
;
; POM1-LIB-MICRO-TEST
; INCLUDES: apple1/delay.asm
; CFG: micro.cfg
; PRESET: 1
; ENABLE: a1io
; ARGS: --no-dram-refresh
; LOAD: 0300
; RUN: 0300
; STEPS: 120000
; EXPECT: 0F00 A5 FE 03 E3 27 6B C7
; ============================================================================

MB = $0F00
T1CL = $2004                    ; [B] R6522/W65C22 datasheet: T1 counter low
T1CH = $2005                    ;     (read) / high (write: load + start)
T1LL = $2006                    ;     T1 latch low

.segment "ZEROPAGE"
base:     .res 2                ; counter after the bare-RTS call
arg:      .res 1

.segment "ENTRY"
main:
        SEI
        CLD
        LDX     #$FF
        TXS
        LDA     #1
        LDX     #1
        JSR     body
        LDA     #10
        LDX     #3
        JSR     body
        LDA     #50
        LDX     #5
        JSR     body
        LDA     #$A5
        STA     MB
spin:   JMP     spin

; A = ms, X = mailbox offset: store body(A) at MB+X (lo) / MB+X+1 (hi).
body:   STA     arg
        LDA     #<null
        LDY     #>null
        JSR     timed
        STA     base
        STY     base+1
        LDA     #<delay_ms_a
        LDY     #>delay_ms_a
        JSR     timed           ; A/Y = counter after the delay
        STA     tmp_lo
        SEC                     ; elapsed difference = base - counter
        LDA     base
        SBC     tmp_lo
        STA     MB,X
        LDA     base+1
        STY     tmp_lo
        SBC     tmp_lo
        STA     MB+1,X
        RTS

; Start T1 at $FFFF, call (A/Y) with A = arg, read the counter into A (lo) /
; Y (hi). Identical instructions whatever the target. Keeps X.
timed:  STA     target
        STY     target+1
        STX     save_x
        LDA     #$FF
        STA     T1LL
        STA     T1CH            ; counter = $FFFF, running
        LDA     arg
        JSR     go
        LDA     T1CL
        LDY     T1CH
        LDX     save_x
        RTS
go:     JMP     (target)

null:   RTS

.segment "BSS"
target:   .res 2
save_x:   .res 1
tmp_lo:   .res 1

.segment "CODE"
.include "delay.asm"
