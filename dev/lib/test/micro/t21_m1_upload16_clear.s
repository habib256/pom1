; ============================================================================
; t21_m1_upload16_clear.s — micro-test: tms9918m1 vdp_upload + clear_name_table
; ============================================================================
; GUARDS:
;   - vdp_upload (new: 16-bit X:A count, no 256-byte limit) moves exactly
;     300 bytes ($01:$2C) from (vdp_src) to $0000-$012B, fencepost $012C
;     stays 0, vdp_src_hi advances by one page.
;   - clear_name_table (now a vdp_fill tail call) zeroes exactly the 768
;     bytes $1800-$1AFF: a sentinel at $17FF survives, both ends of the
;     table come back 0, and SAT[0].Y at $1B00 keeps init's $D0.
;   - disable_sprites (also on vdp_fill now): SAT $1B01 / $1B7F = $D1.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918m1.asm tms9918/tms9918_pad.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 200000
; EXPECT: 0F00 A5 03 FC 03 11 00 01
; EXPECT: 0F07 5E 00 00 D0 D1 D1
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_g1, vdp_set_write, vdp_set_read, vdp_upload
.import clear_name_table, tms9918_pad18
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi

.segment "ZEROPAGE"
tmp:    .res 1
.exportzp tmp

MB = $0F00

.macro SET_W addr
        LDA     #<(addr)
        STA     vdp_lo
        LDA     #>(addr)
        STA     vdp_hi
        JSR     vdp_set_write
.endmacro
.macro PEEK addr, slot
        LDA     #<(addr)
        STA     vdp_lo
        LDA     #>(addr)
        STA     vdp_hi
        JSR     vdp_set_read
        LDA     VDP_DATA
        STA     MB+slot
.endmacro
.macro SRC_TABLE
        LDA     #<table
        STA     vdp_src_lo
        LDA     #>table
        STA     vdp_src_hi
.endmacro

.segment "CODE"
main:
        APPLE1_PREAMBLE
        JSR     init_vdp_g1     ; VRAM wiped, SAT parked, display ON

        ; --- vdp_upload: 300 bytes to $0000 --------------------------------
        SRC_TABLE
        SET_W   $0000
        LDX     #$01
        LDA     #$2C
        JSR     vdp_upload
        LDA     vdp_src_hi
        SEC
        SBC     #>table
        STA     MB+6            ; 01
        PEEK    $0000, 1        ; table[0]   = 03
        PEEK    $00FF, 2        ; table[255] = FC
        PEEK    $0100, 3        ; table[256] = 03
        PEEK    $0102, 4        ; table[258] = 11
        PEEK    $012C, 5        ; 00 fencepost

        ; --- clear_name_table: dirty both ends, sentinel before ------------
        SET_W   $17FF
        JSR     tms9918_pad18
        LDA     #$5E
        STA     VDP_DATA        ; $17FF = 5E (sentinel)
        JSR     tms9918_pad18
        STA     VDP_DATA        ; $1800 = 5E (dirty)
        SET_W   $1AFF
        JSR     tms9918_pad18
        LDA     #$5E
        STA     VDP_DATA        ; $1AFF = 5E (dirty)
        JSR     tms9918_pad18
        JSR     clear_name_table
        PEEK    $17FF, 7        ; 5E
        PEEK    $1800, 8        ; 00
        PEEK    $1AFF, 9        ; 00
        PEEK    $1B00, 10       ; D0
        PEEK    $1B01, 11       ; D1
        PEEK    $1B7F, 12       ; D1

        LDA     #$A5
        STA     MB
spin:   JMP     spin

.segment "RODATA"
table:
.repeat 300, I
        .byte   <(I * 7 + 3)
.endrepeat
