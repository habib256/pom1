; ============================================================================
; t19_tms_text_charmap.s — micro-test: tms9918_text upload_charmap + fill
; ============================================================================
; GUARDS: the build-time bit-reversed font (charmap_tms.inc, generated from
;   roms/charmap.rom by tools/build_charmap_tms.py) and the unpadded,
;   display-blanked 1 KB copy that replaced the per-byte LSR/ROL reversal.
;   - pattern table $0000-$03FF = the Apple-1 glyphs with bit 7 = leftmost
;     pixel: 'A' ($41 -> $0208) reads 10 28 44 44 7C 44 44 00 (ROM bytes
;     08 14 22 22 3E 22 22 00 reversed), glyph $01 starts 7E, '~' ($7E ->
;     $03F0) starts 34 58, the last byte $03FF is 00, and $0400 (one past
;     the 1024 bytes) keeps its sentinel;
;   - upload_charmap re-arms R1 = $D0 (text mode, display ON);
;   - clear_screen_text fills exactly $0800-$0BBF with $20 (sentinels at
;     $07FF and $0BC0 survive);
;   - print_at_rc_text lands (23,39) at $0BBF.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918_text.asm tms9918/tms9918_pad.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 200000
; EXPECT: 0F00 A5 10 28 44 44 7C 44 44 00
; EXPECT: 0F09 7E 34 58 00 5E
; EXPECT: 0F0E 5E 20 20 5A 5E
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_text, upload_charmap, clear_screen_text
.import vdp_set_write, vdp_set_read, print_at_rc_text
.import tms9918_pad18
.importzp vdp_lo, vdp_hi, vdp_row, vdp_col

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
.macro SET_R addr
        LDA     #<(addr)
        STA     vdp_lo
        LDA     #>(addr)
        STA     vdp_hi
        JSR     vdp_set_read
        JSR     tms9918_pad18   ; read-prefetch cushion
.endmacro
.macro PEEK addr, slot
        SET_R   addr
        LDA     VDP_DATA
        STA     MB+slot
.endmacro
.macro POKE addr, val
        SET_W   addr
        JSR     tms9918_pad18
        LDA     #val
        STA     VDP_DATA
        JSR     tms9918_pad18
.endmacro

.segment "CODE"
main:
        APPLE1_PREAMBLE
        JSR     init_vdp_text   ; registers, display OFF
        POKE    $0400, $5E      ; sentinel one past the 1 KB pattern table
        POKE    $07FF, $5E      ; sentinels around the 960-byte name table
        POKE    $0BC0, $5E
        JSR     upload_charmap  ; blanked 1 KB burst, then R1 = $D0
        JSR     clear_screen_text
        LDA     #23
        STA     vdp_row
        LDA     #39
        STA     vdp_col
        LDA     #$5A            ; 'Z'
        JSR     print_at_rc_text

        ; 'A' = glyph $41 at $0208: slots 1..8
        SET_R   $0208
        LDX     #1
@a:     LDA     VDP_DATA
        STA     MB,X
        JSR     tms9918_pad18
        INX
        CPX     #9
        BNE     @a
        PEEK    $0008, 9        ; glyph $01 row 0: 7E
        PEEK    $03F0, 10       ; '~' row 0: 34
        PEEK    $03F1, 11       ; '~' row 1: 58
        PEEK    $03FF, 12       ; last byte: 00
        PEEK    $0400, 13       ; sentinel: 5E
        PEEK    $07FF, 14       ; sentinel: 5E
        PEEK    $0800, 15       ; 20
        PEEK    $0BBE, 16       ; 20
        PEEK    $0BBF, 17       ; 'Z' = 5A
        PEEK    $0BC0, 18       ; sentinel: 5E

        LDA     #$A5
        STA     MB
spin:   JMP     spin
