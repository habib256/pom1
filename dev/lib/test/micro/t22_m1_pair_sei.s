; ============================================================================
; t22_m1_pair_sei.s — micro-test: TMS_PAIR_SEI = 1 (IRQ-masked control pairs)
; ============================================================================
; GUARDS: the opt-in IRQ wrapper of tms9918.inc (TMS_PAIR_BEGIN/END, rule (b)
;   of its IRQ caveat), as built into tms9918m1's vdp_set_write/vdp_set_read.
;   - the caller's I flag comes back unchanged: clear stays clear, set stays
;     set (PHP/SEI ... PLP, never a forced CLI);
;   - the wrapped routines still address VRAM correctly: a write/read
;     round trip at $1234 returns the byte.
;   Not pinned here: that an IRQ is actually held off between the two bytes
;   (would need a live VDP frame IRQ + handler); the option only adds
;   PHP/SEI/PLP around code these assertions already cover.
;
; POM1-LIB-MICRO-TEST
; LIBS: test/micro/support/tms9918m1_pair_sei.asm tms9918/tms9918_pad.asm
; INCLUDES: tms9918/tms9918m1.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 120000
; EXPECT: 0F00 A5 00 04 00 04 6B
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_g1, vdp_set_write, vdp_set_read, tms9918_pad18
.importzp vdp_lo, vdp_hi

.segment "ZEROPAGE"
tmp:    .res 1
.exportzp tmp

MB = $0F00

.segment "CODE"
main:
        APPLE1_PREAMBLE
        SEI
        JSR     init_vdp_g1     ; R1 bit 5 = 0: the VDP raises no IRQ
        LDA     #$34
        STA     vdp_lo
        LDA     #$12
        STA     vdp_hi

        CLI                     ; I = 0 in, I = 0 out
        JSR     vdp_set_write
        PHP
        PLA
        AND     #$04
        STA     MB+1            ; 00
        SEI                     ; I = 1 in, I = 1 out
        JSR     tms9918_pad18
        LDA     #$6B
        STA     VDP_DATA
        JSR     tms9918_pad18
        JSR     vdp_set_write   ; (re-address: the store above advanced it)
        PHP
        PLA
        AND     #$04
        STA     MB+2            ; 04
        CLI
        JSR     vdp_set_read
        PHP
        PLA
        AND     #$04
        STA     MB+3            ; 00
        SEI
        JSR     vdp_set_read
        PHP
        PLA
        AND     #$04
        STA     MB+4            ; 04
        LDA     VDP_DATA
        STA     MB+5            ; 6B

        LDA     #$A5
        STA     MB
spin:   JMP     spin
