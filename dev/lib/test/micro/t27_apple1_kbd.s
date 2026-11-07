; ============================================================================
; t27_apple1_kbd.s — micro-test: apple1/kbd.asm register + flag contract
; ============================================================================
; GUARDS: what callers of wait_key / poll_key branch on, for both entries of
;   the shared-tail layout (poll_key branches into wait_key's tail):
;     - A = key & $7F with the strobe set, A = 0 without (poll_key),
;     - Z/N from A on return (callers BEQ on "no key" straight after the JSR),
;     - X, Y, C and V preserved.
;   KBD / KBDCR are the CALLER's symbols (kbd.asm does not include
;   apple1.inc), so the driver points them at two RAM bytes: the key is
;   "typed" by storing it, and nothing has to be injected into the emulated
;   keyboard. (wait_key's blocking loop is the one path this cannot drive.)
;
;   Each case stores A, the flags masked to N V . . . . Z C ($C3), X and Y.
;     1 poll, no key, C=1 V=1   -> A=00 P=43 (V Z C)  X=11 Y=22
;     2 poll, key $C1, C=0 V=0  -> A=41 P=00          X=11 Y=22
;     3 poll, key $80 (NUL)     -> A=00 P=03 (Z C)    X=11 Y=22
;     4 wait, key $8D, C=1 V=1  -> A=0D P=41 (V C)    X=11 Y=22
;     5 wait, key $FF, C=0 V=0  -> A=7F P=00          X=11 Y=22
;
; POM1-LIB-MICRO-TEST
; INCLUDES: apple1/kbd.asm
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

.segment "CODE"
.include "kbd.asm"
