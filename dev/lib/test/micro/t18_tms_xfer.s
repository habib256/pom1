; ============================================================================
; t18_tms_xfer.s — micro-test: tms9918_xfer.asm (store helpers + fill/copy)
; ============================================================================
; GUARDS: the shared port helpers every TMS9918 module now goes through.
;   - vdp_write_a / vdp_ctrl_a keep A, X, Y and the N/V/Z/C flags (callers
;     branch on a compare made before the call, as with tms9918_pad18).
;   - vdp_fill: 16-bit count in X:Y. 300 bytes ($01:$2C) land exactly at
;     $1000-$112B (fencepost $112C stays 0, sentinel $0FFF intact); a whole
;     page ($01:$00) is 256 bytes; a zero count writes nothing. A kept,
;     X = $FF, Y = 0 on exit.
;   - vdp_fill_blank: same count logic, unpadded, display blanked.
;   - vdp_copy: X:A = $01:$2C copies 300 table bytes, advances the source
;     pointer by one page, leaves tmp = $2C; a zero count copies nothing.
;   - vdp_copy_short: A = 0 means 256 bytes; X preserved.
;   - vdp_copy_blank: X:A = $01:$03 = 259 bytes, display blanked; X = 0.
; Runs with the display ON (init_vdp_g1) except for the two blank variants,
; so the harness's zero-[TMS9918 DROP] assertion covers the padded paths.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918m1.asm tms9918/tms9918_pad.asm
; INCLUDES: tms9918/tms9918_xfer.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 250000
; EXPECT: 0F00 A5 80 33 C7 80 50 33 C7 00
; EXPECT: 0F09 EE A7 A7 A7 A7 00 A7 FF 00
; EXPECT: 0F12 B1 B1 00 00 C3 C3 00 FF
; EXPECT: 0F1A 03 FC 03 01 00 2C 00
; EXPECT: 0F21 03 FC 00 77 03 FC 11 00 00
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_g1, vdp_set_write, vdp_set_read
.import vdp_display_off, vdp_display_on
.importzp vdp_lo, vdp_hi

.segment "ZEROPAGE"
tmp:    .res 1                  ; m1 + vdp_copy* scratch
src:    .res 2                  ; VDP_XFER_PTR
.exportzp tmp

VDP_XFER_PTR = src
MB = $0F00

; Point the VDP at a VRAM address for writing / reading (tms9918m1 calls).
.macro SET_W addr
        LDA     #<(addr)
        STA     vdp_lo
        LDA     #>(addr)
        STA     vdp_hi
        JSR     vdp_set_write
.endmacro
.macro SET_R addr
        LDA     #<(addr)
        STA     vdp_lo
        LDA     #>(addr)
        STA     vdp_hi
        JSR     vdp_set_read
.endmacro
; One VRAM byte into mailbox slot `slot`.
.macro PEEK addr, slot
        SET_R   addr
        LDA     VDP_DATA
        STA     MB+slot
.endmacro
.macro SRC_TABLE
        LDA     #<table
        STA     src
        LDA     #>table
        STA     src+1
.endmacro

.segment "ENTRY"
main:
        APPLE1_PREAMBLE
        JSR     init_vdp_g1     ; VRAM wiped to 0, display ON

        ; --- 1. register / flag transparency -----------------------------
        SET_W   $0FFF
        LDX     #$33
        LDY     #$C7
        CLV
        CLC
        LDA     #$80            ; N=1 Z=0 C=0 V=0
        JSR     vdp_write_a
        PHP
        STA     MB+1            ; 80
        STX     MB+2            ; 33
        STY     MB+3            ; C7
        PLA
        AND     #$C3            ; N V . . . . Z C
        STA     MB+4            ; 80
        LDA     #$00            ; write address $1000 through vdp_ctrl_a
        JSR     vdp_ctrl_a      ; low byte
        SEC
        LDA     #$50            ; $10 | $40: N=0 Z=0 (C=1 masked out)
        JSR     vdp_ctrl_a      ; high byte + write flag
        PHP
        STA     MB+5            ; 50
        STX     MB+6            ; 33
        STY     MB+7            ; C7
        PLA
        AND     #$C2            ; N V Z
        STA     MB+8            ; 00

        ; --- 2. vdp_fill: sentinel + 300 bytes + fencepost ---------------
        SET_W   $0FFF
        LDA     #$EE
        JSR     vdp_write_a     ; $0FFF = EE, address now $1000
        LDA     #$A7
        LDX     #$01
        LDY     #$2C            ; 300
        JSR     vdp_fill
        STA     MB+15           ; A kept: A7
        STX     MB+16           ; FF
        STY     MB+17           ; 00
        PEEK    $0FFF, 9        ; EE
        PEEK    $1000, 10       ; A7
        PEEK    $10FF, 11       ; A7
        PEEK    $1100, 12       ; A7
        PEEK    $112B, 13       ; A7
        PEEK    $112C, 14       ; 00

        ; --- 3. a whole page, then a zero count --------------------------
        SET_W   $1200
        LDA     #$B1
        LDX     #$01
        LDY     #$00            ; 256
        JSR     vdp_fill
        LDA     #$B2            ; count 0: nothing written at $1300
        LDX     #$00
        LDY     #$00
        JSR     vdp_fill
        PEEK    $1200, 18       ; B1
        PEEK    $12FF, 19       ; B1
        PEEK    $1300, 20       ; 00
        PEEK    $1301, 21       ; 00

        ; --- 4. vdp_fill_blank, display blanked --------------------------
        JSR     vdp_display_off ; an R1 write moves the address: set it after
        SET_W   $1400
        LDA     #$C3
        LDX     #$02
        LDY     #$03            ; 515
        JSR     vdp_fill_blank
        STX     MB+25           ; FF
        JSR     vdp_display_on
        PEEK    $1400, 22       ; C3
        PEEK    $1602, 23       ; C3
        PEEK    $1603, 24       ; 00

        ; --- 5. vdp_copy: 300 bytes, pointer + tmp on exit; zero count ---
        SRC_TABLE
        SET_W   $3000
        LDX     #$01
        LDA     #$2C
        JSR     vdp_copy
        LDA     src+1
        SEC
        SBC     #>table
        STA     MB+29           ; 01: one page
        LDA     tmp
        STA     MB+31           ; 2C
        PEEK    $3000, 26       ; table[0]   = 03
        PEEK    $30FF, 27       ; table[255] = FC
        PEEK    $3100, 28       ; table[256] = 03
        PEEK    $312C, 30       ; 00 fencepost
        SRC_TABLE
        SET_W   $3200
        LDX     #$00
        LDA     #$00
        JSR     vdp_copy
        PEEK    $3200, 32       ; 00

        ; --- 6. vdp_copy_short: A = 0 -> 256 bytes, X kept ---------------
        SRC_TABLE
        SET_W   $3300
        LDX     #$77
        LDA     #$00
        JSR     vdp_copy_short
        STX     MB+36           ; 77
        PEEK    $3300, 33       ; table[0]   = 03
        PEEK    $33FF, 34       ; table[255] = FC
        PEEK    $3400, 35       ; 00 fencepost

        ; --- 7. vdp_copy_blank, display blanked --------------------------
        JSR     vdp_display_off
        SRC_TABLE
        SET_W   $3500
        LDX     #$01
        LDA     #$03            ; 259
        JSR     vdp_copy_blank
        STX     MB+40           ; 00
        JSR     vdp_display_on
        PEEK    $3500, 37       ; table[0]   = 03
        PEEK    $35FF, 38       ; table[255] = FC
        PEEK    $3602, 39       ; table[258] = 11
        PEEK    $3603, 41       ; 00 fencepost

        LDA     #$A5
        STA     MB
spin:   JMP     spin

.segment "RODATA"
; table[i] = (i*7 + 3) & $FF: 300 bytes
table:
.repeat 300, I
        .byte   <(I * 7 + 3)
.endrepeat

.segment "CODE"
.include "tms9918_xfer.asm"
