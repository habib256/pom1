; ============================================================================
; t14_hgr_sprite16.s — micro-test: gen2/hgr_sprite16.asm blits + colour
; ============================================================================
; GUARDS: the lossless row bit-stream repack (7 px per HGR byte) and the
;   NTSC artifact-colour attributes — the two things a "simpler" rewrite
;   of the blitter historically got wrong (the naive byte-column mapping
;   dropped one pixel column per source byte and skewed the x2 art;
;   parity masks that don't ALTERNATE between byte columns tint the
;   wrong pixels).
;
;   Test pattern: 16 rows of left = $C3 / right = $81 (asymmetric edges,
;   sensitive to bit-order and seam mistakes).
;
;   Blit 1 — x1 WHITE at (8, 0): stream $C3 $81 packs to HGR bytes
;   $43 $03 $02 at byte columns 5..7 of scanline 0 ($2005..) and,
;   rows all being identical, the same on scanline 1 ($2405..).
;
;   Blit 2 — x2 GREEN at (8, 16): dblnib doubles the nibbles to
;   $F0 $0F $C0 $03; packed = $0F $60 $0F $00 $0C, then the GREEN
;   parity mask (odd-x pixels; $55 on odd byte cols 5/7/9, $2A on even
;   cols 6/8) leaves $05 $20 $05 $00 $04 at $2105.. (y=16) and $2505..
;   (y=17, the vertical double of the same source row).
;
;   Blit 3 — x4 WHITE at (248, 56): byte columns 35..44, clipped at 40.
;   Column 35 holds $7F (the first 7 of $C3's quadbits). Columns 40..44
;   of scanline 56 ($2380+40 = $23A8) are really the start of scanline
;   120, which the sprite never covers: they must stay $00 (before the
;   2026-11-07 fix the blitter stored there).
;
;   Blit 4 — x4 WHITE at (128, 248): entirely below the screen. Nothing
;   may be drawn; before the fix sp_yy wrapped 255 -> 0 and the sprite
;   reappeared at the top, byte columns 20.. of scanline 0 ($2014).
;
;   Expected values independently derived by tools-side simulation of
;   the pack + rev7 + mask pipeline.
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro.cfg
; PRESET: 11
; LOAD: 0300
; RUN: 0300
; STEPS: 250000
; EXPECT: 0F00 A5
; EXPECT: 0F01 43 03 02
; EXPECT: 0F04 43 03 02
; EXPECT: 0F07 05 20 05 00 04
; EXPECT: 0F0C 05 20 05 00 04
; EXPECT: 0F11 7F 00 00 00 00 00 00
; ============================================================================

MB = $0F00

.segment "CODE"

start:
        ; --- Blit 1: x1 WHITE at (8, 0) --------------------------------
        LDA #<pat
        STA sp_ptr
        LDA #>pat
        STA sp_ptr+1
        LDA #HSPR_WHITE
        JSR hgr_spr16_color_a
        LDA #8
        STA sp_x
        LDA #0
        STA sp_y
        JSR hgr_spr16_x1

        ; --- Blit 2: x2 GREEN at (8, 16) -------------------------------
        LDA #HSPR_GREEN
        JSR hgr_spr16_color_a
        LDA #8
        STA sp_x
        LDA #16
        STA sp_y
        JSR hgr_spr16_x2

        ; --- Blit 3: x4 WHITE at (248, 56) -- right-edge clip -----------
        LDA #HSPR_WHITE
        JSR hgr_spr16_color_a
        LDA #248
        STA sp_x
        LDA #56
        STA sp_y
        JSR hgr_spr16_x4

        ; --- Blit 4: x4 WHITE at (128, 248) -- fully off-screen ---------
        LDA #128
        STA sp_x
        LDA #248
        STA sp_y
        JSR hgr_spr16_x4

        ; --- Mailbox: framebuffer probes -------------------------------
        ; x1: scanline 0 = $2000, scanline 1 = $2400; byte cols 5..7.
        LDA $2005
        STA MB+1
        LDA $2006
        STA MB+2
        LDA $2007
        STA MB+3
        LDA $2405
        STA MB+4
        LDA $2406
        STA MB+5
        LDA $2407
        STA MB+6
        ; x2: scanline 16 = $2100, scanline 17 = $2500; byte cols 5..9.
        LDA $2105
        STA MB+7
        LDA $2106
        STA MB+8
        LDA $2107
        STA MB+9
        LDA $2108
        STA MB+10
        LDA $2109
        STA MB+11
        LDA $2505
        STA MB+12
        LDA $2506
        STA MB+13
        LDA $2507
        STA MB+14
        LDA $2508
        STA MB+15
        LDA $2509
        STA MB+16

        ; x4 right edge: scanline 56 = $2380, byte column 35 drawn,
        ; columns 40..44 ($23A8.., scanline 120) untouched.
        LDA $23A3
        STA MB+17
        LDX #4
@edge:  LDA $23A8,X
        STA MB+18,X
        DEX
        BPL @edge
        ; x4 off-screen: scanline 0, byte column 20.
        LDA $2014
        STA MB+23

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

; --- Test pattern: 16 rows, left column $C3, right column $81 ---------
pat:
.repeat 16
        .byte $C3
.endrepeat
.repeat 16
        .byte $81
.endrepeat

; --- Lib under test (textual includes; module-local symbols) -----------
.include "hgr_scanline.inc"
.include "hgr_sprite16.asm"
