; ============================================================================
; t17_hgr_text8_clip.s — micro-test: gen2/hgr_text8.asm edge cases
; ============================================================================
; GUARDS the two hgr_text8 defects fixed on 2026-11-07 (ported back from the
; pom2games Apple II edition), plus the inline-argument entry point:
;
;   - BOTTOM CLIP: a glyph started at scanline 188 draws rows 188..191 only.
;     The old loop drew all 8 rows and indexed hgr_lo/hgr_hi[192..195], past
;     the 192-entry tables. The 4 bytes placed right after hgr_hi below are
;     $0F, so the old code stored the glyph at $0F20/$0F24/$0F28/$0F2C
;     (hgr_lo[192+i] = hgr_hi[i] = $20/$24/$28/$2C): they must stay $00.
;     Rows 188 ($33D0) and 191 ($3FD0) must hold the glyph ($43).
;   - OFF-SCREEN WRAP: a cursor at scanline 248 that wraps used to add 8 and
;     land on scanline 0. It must stay at 248 and draw nothing: ht_sl reads
;     back $F8, $2002 stays $00, ht_col = ht_left + 1.
;   - hgr_puts8_at: JSR + .byte col, scanline + .addr string draws '!' at
;     column 6, scanline 16 ($2106 = $43) and returns past its 4 data bytes
;     (the driver reaching the mailbox proves the return address).
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro.cfg
; PRESET: 11
; LOAD: 0300
; RUN: 0300
; STEPS: 30000
; EXPECT: 0F00 A5
; EXPECT: 0F01 43 43 F8 03 00 43
; EXPECT: 0F20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
; ============================================================================

MB = $0F00

.segment "CODE"

start:
        LDA #0                  ; the old code's stray-store landing zone
        LDX #15
@z:     STA MB+$20,X
        DEX
        BPL @z
        LDA #<font
        STA ht_font_lo
        LDA #>font
        STA ht_font_hi
        LDA #0
        STA ht_rev              ; HGR bit order
        STA ht_cbit
        STA ht_page             ; page 1
        STA ht_left
        LDA #40
        STA ht_wrap
        LDA #$7F                ; WHITE pass-through
        STA ht_cm_ev
        STA ht_cm_od

        ; --- bottom clip: '!' at column 0, scanline 188 ----------------
        LDA #0
        STA ht_col
        LDA #188
        STA ht_sl
        LDA #'!'
        JSR hgr_putc8

        ; --- off-screen wrap: cursor at the wrap column, scanline 248 ---
        LDA #2
        STA ht_left
        LDA #40
        STA ht_col              ; = ht_wrap: this char wraps
        LDA #248
        STA ht_sl
        LDA #'!'
        JSR hgr_putc8

        LDA ht_sl               ; stayed off-screen
        STA MB+3
        LDA ht_col              ; ht_left + 1
        STA MB+4

        ; --- inline-argument entry --------------------------------------
        JSR hgr_puts8_at
        .byte 6, 16
        .addr bang

        ; --- Mailbox -----------------------------------------------------
        LDA $33D0               ; scanline 188, column 0
        STA MB+1
        LDA $3FD0               ; scanline 191, column 0
        STA MB+2
        LDA $2002               ; scanline 0, column 2: untouched
        STA MB+5
        LDA $2106               ; hgr_puts8_at: scanline 16, column 6
        STA MB+6

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

bang:   .byte "!", 0

; --- Local 2-glyph font: ' ' (blank) + '!' (8 rows of $C3) -------------
font:
.repeat 8
        .byte $00
.endrepeat
.repeat 8
        .byte $C3
.endrepeat

; --- Libs under test (textual includes) ---------------------------------
; hgr_scanline.inc is hgr_lo[192] then hgr_hi[192]; the 4 bytes right after
; are what the old loop read as hgr_hi[192..195].
.include "hgr_scanline.inc"
        .byte $0F, $0F, $0F, $0F
.include "hgr_text8.asm"
