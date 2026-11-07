; ============================================================================
; t51_ondemand_kbd_poll_late.s — micro-test: on-demand kbd.asm, two includes
; ============================================================================
; GUARDS the mirror case of t50: poll_key emitted by a SECOND include, after
; the first one emitted wait_key + kbd_take (wait_key falling into its tail).
; poll_key's BMI may not reach a tail that lies behind it, so a late
; poll_key ends on BPL / JMP kbd_take instead.
;   Include #1 (after the wait_key calls): wait_key + kbd_take.
;   Include #2 (after the poll_key calls): poll_key alone, JMP kbd_take.
;   multiply.asm is included after a umul4 reference only: umul4 must be
;   emitted and umul8 must not (checked at assembly time).
;   The t27 cases, wait_key ones first: 4, 5, then 1, 2, 3 (KBD / KBDCR are
;   two RAM bytes).
;
; POM1-LIB-MICRO-TEST
; INCLUDES: apple1/kbd.asm m6502/multiply.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 4000
; EXPECT: 0F00 A5 0D 41 11 22 7F 00 11 22 00 43 11 22 41 00 11 22 00 03 11 22
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


; Include #1: only wait_key has been referenced so far.
.segment "CODE"
.include "kbd.asm"
.ifdef poll_key
.error "t51: poll_key emitted before any reference"
.endif
.segment "ENTRY"

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

; Never called: a reference, so that multiply.asm emits umul4 (and only it).
.segment "ENTRY"
unused_mul:
        JMP     umul4

; Include #2: poll_key is referenced now; kbd_take already exists.
.segment "CODE"
.include "kbd.asm"
.include "multiply.asm"
.assert poll_key > kbd_take, error, "t51: poll_key must come from the second include"
.ifndef umul4
.error "t51: umul4 referenced but not emitted"
.endif
.ifdef umul8
.error "t51: umul8 emitted with no caller"
.endif
