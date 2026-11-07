; ============================================================================
; t50_ondemand_kbd_wait_late.s — micro-test: on-demand kbd.asm, two includes
; ============================================================================
; GUARDS the "Assembled on demand" convention (dev/lib/README.md) on its one
; non-trivial path in kbd.asm: wait_key emitted by a SECOND include, after
; the first one emitted poll_key + the shared tail kbd_take. wait_key cannot
; fall into a tail that lies behind it, so it must end on JMP kbd_take.
;   Include #1 (after the poll_key calls): poll_key + kbd_take.
;   Include #2 (after the wait_key calls): wait_key alone, JMP kbd_take.
;   multiply.asm is included with no caller: umul8 / umul4 must NOT be
;   emitted (checked at assembly time).
;   Same five cases and expected values as t27_apple1_kbd.s (KBD / KBDCR are
;   two RAM bytes): only the assembly of the library differs.
;
; POM1-LIB-MICRO-TEST
; INCLUDES: apple1/kbd.asm m6502/multiply.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 4000
; EXPECT: 0F00 A5 00 43 11 22 41 00 11 22 00 03 11 22 0D 41 11 22 7F 00 11 22
; ============================================================================

MB = $0F00

.segment "BSS"
KBD:      .res 1                ; stand-ins for $D010 / $D011
KBDCR:    .res 1
mbx:      .res 1                ; next mailbox offset

.segment "ENTRY"
main:
        SEI
        CLD
        LDX     #$FF
        TXS
        LDA     #1
        STA     mbx

        ; 1: poll_key, no key; enter with C=1 V=1
        LDA     #$00
        STA     KBDCR
        LDA     #$55
        STA     KBD
        JSR     set_cv
        JSR     poll_key
        JSR     record

        ; 2: poll_key, key 'A' ($C1); enter with C=0 V=0
        LDA     #$80
        STA     KBDCR
        LDA     #$C1
        STA     KBD
        JSR     clr_cv
        JSR     poll_key
        JSR     record

        ; 3: poll_key, key $80 (a NUL with the strobe bit): A=0, Z=1, C kept
        LDA     #$80
        STA     KBD
        JSR     set_c_only
        JSR     poll_key
        JSR     record


; Include #1: only poll_key has been referenced so far.
.segment "CODE"
.include "kbd.asm"
.include "multiply.asm"         ; no caller anywhere: emits nothing
.ifdef wait_key
.error "t50: wait_key emitted before any reference"
.endif
.segment "ENTRY"

        ; 4: wait_key, key ready ($8D = CR), KBDCR has other bits set too
        LDA     #$A7
        STA     KBDCR
        LDA     #$8D
        STA     KBD
        JSR     set_cv
        JSR     wait_key
        JSR     record

        ; 5: wait_key, key $FF -> $7F (N must come back clear)
        LDA     #$FF
        STA     KBD
        JSR     clr_cv
        JSR     wait_key
        JSR     record

        LDA     #$A5
        STA     MB
spin:   JMP     spin

; Set X=$11 Y=$22, and C/V as asked. A is the routine's to set.
set_cv: BIT     vflag           ; V = 1
        SEC
        BCS     xy
clr_cv: CLV
        CLC
        BCC     xy
set_c_only:
        CLV
        SEC
xy:     LDX     #$11
        LDY     #$22
        RTS
vflag:  .byte   $40

; Store A, P & $C3, X, Y at the next four mailbox bytes. Keeps nothing.
record: PHP
        STX     rec_x
        STY     rec_y
        LDX     mbx
        STA     MB,X
        PLA
        AND     #$C3
        STA     MB+1,X
        LDA     rec_x
        STA     MB+2,X
        LDA     rec_y
        STA     MB+3,X
        INX
        INX
        INX
        INX
        STX     mbx
        RTS

.segment "BSS"
rec_x:    .res 1
rec_y:    .res 1

; Include #2: wait_key is referenced now; kbd_take already exists.
.segment "CODE"
.include "kbd.asm"
.assert wait_key > kbd_take, error, "t50: wait_key must come from the second include"
.ifdef umul8
.error "t50: umul8 emitted with no caller"
.endif
.ifdef umul4
.error "t50: umul4 emitted with no caller"
.endif
