; ============================================================================
; hgr_hud8.asm -- differential 8x8 numeric HUD fields on hgr_text8 (GEN2 HGR)
; ============================================================================
; A score / lives / level counter redrawn every frame costs a full glyph
; per digit (~560 cycles each with hgr_putc8). These routines redraw only
; the digits that CHANGED since that field was last drawn on the same page.
; Measured on a 5-digit field (scratch 6502 core, JSR/RTS included): an
; unchanged value 128 cycles, a +1 that changes one digit 1 372, a +2 1 409,
; against 4 120 to draw all five cells.
;
;   hgr_hud8_putu        hh_fld -> a field, hh_val = 16-bit unsigned value.
;                        Draws the value right-aligned in the field's cells
;                        (leading cells blank) on the current HUD page and
;                        returns A = number of cells drawn (0 = nothing
;                        changed), or A = $FF if the field's width is not
;                        1..5 or the value needs more digits than the field
;                        has (then nothing is drawn or recorded). X, Y
;                        clobbered. ht_col / ht_sl (the text cursor) are
;                        saved and restored around the drawing.
;   hgr_hud8_invalidate  hh_fld -> a field, A = pages to forget (bit 0 =
;                        page 1, bit 1 = page 2, 3 = both): the next putu
;                        on such a page redraws every cell. Call it after
;                        anything else wrote into the field's box (a full
;                        clear, a dialog). Clobbers A, Y.
;   hgr_putu8            hh_val printed at the text cursor with hgr_putc8,
;                        no padding (the plain counterpart, no cache).
;                        Clobbers A, X, Y; the cursor advances.
;
; A field is 18 bytes of the program's RAM, laid out by the HUD8_FIELD macro:
;   HUD8_FIELD col, scanline, width   ; byte column 0..39, top scanline,
;                                     ; 1..5 cells (8 px each, one per byte)
;   +0 col  +1 scanline  +2 width  +3 drawn flags (bit 0 page 1, bit 1
;   page 2)  +4/+5 value on page 1  +6/+7 value on page 2  +8..+12 cells on
;   page 1  +13..+17 cells on page 2 (ASCII, as drawn).
; The flags start at 0 (the macro), so the first putu on each page draws
; every cell. Keep col + width <= ht_wrap (hgr_putc8 would wrap the cell).
; HUD8_FIELD is a macro of this file: declare fields AFTER the include.
;
; Glyphs come from hgr_text8.asm (hgr_putc8): its font, bit order, colour
; attributes and page selector apply, and so does its HGR_TEXT8_FILTER.
; The HUD page is the byte HGR_HUD8_PAGE (0 = page 1, nonzero = page 2),
; ht_page by default; a program flipping with hgr_flip.asm (ht_page left at
; $00) sets HGR_HUD8_PAGE = hgr_draw_page before the include.
;
; Why on hgr_putc8 and not a glyph kernel of its own: POM1's asm HUDs
; (HGR_Rogue, HGR_Maze3D) are byte-aligned and already print with
; hgr_text8, so a field reuses their font, colours and page selection for
; 0 extra bytes of glyph data; the pom2games C HUD (hgrc hgr_hud8.s) draws
; at any pixel X from a 1 232-byte pre-shifted digit bank instead. Not on
; the HGR_TEXT8_FILTER hook either: a filter is global, would tax every
; glyph the program prints, and needs the field's context anyway.
;
; Cost: zero page 4 B (hh_fld, hh_val: public, skipped if defined before the
; include); code 447 B (17 B of scratch and 14 B of tables included). Needs
; hgr_text8.asm (include it before this file) and hgr_scanline.inc.
; Not reentrant. The scratch lives next to the code: RAM only (it is not
; self-modifying, but a ROM copy would have no writable scratch).
;
; A port of the idea of the pom2games hgrc differential HUD (hgr_hud8.s,
; hgr_hud_putu.inc: per-page value + digit cache, +1/+2 steps applied to
; the cached digits instead of a full conversion), rewritten as a plain asm
; module for POM1's text8 cursor; the C ABI and the pixel-aligned digit
; bank were left out.
; ============================================================================
.ifndef _HGR_HUD8_LOADED_
_HGR_HUD8_LOADED_ = 1

.ifndef _HGR_TEXT8_LOADED_
.error "hgr_hud8.asm: include hgr_text8.asm first"
.endif
.ifndef HGR_HUD8_PAGE
HGR_HUD8_PAGE = ht_page
.endif

.macro HUD8_FIELD col, sl, width
        .byte col, sl, width, 0
        .res 14, 0
.endmacro

.pushseg
.zeropage
.ifndef hh_fld
hh_fld: .res 2                  ; -> the field (public)
.endif
.ifndef hh_val
hh_val: .res 2                  ; the value (public)
.endif
.popseg

; ----------------------------------------------------------------------------
; hgr_hud8_putu
; ----------------------------------------------------------------------------
hgr_hud8_putu:
        LDY #2
        LDA (hh_fld),Y          ; width 1..5
        BEQ @bad
        CMP #6
        BCS @bad
        STA hh_w
        LDA #5
        SEC
        SBC hh_w
        STA hh_first            ; index of the first shown cell in hh_dig
        LDX #0                  ; page 1: flag 1, value +4, cells +8
        LDA HGR_HUD8_PAGE
        BEQ @pg
        INX                     ; page 2: flag 2, value +6, cells +13
@pg:    LDA hh_pbit,X
        STA hh_pb
        LDA hh_voff,X
        STA hh_vo
        LDA hh_doff,X
        STA hh_do
        INY                     ; Y = 3: drawn flags
        LDA (hh_fld),Y
        AND hh_pb
        STA hh_valid
        BEQ @conv
        LDY hh_vo               ; same value as on this page last time?
        SEC
        LDA hh_val
        SBC (hh_fld),Y
        TAX                     ; X = low byte of the step
        INY
        LDA hh_val+1
        SBC (hh_fld),Y
        BCC @diff               ; went down (0 - 65535 is +1 in 16 bits!)
        BNE @diff               ; up by 256 or more
        TXA
        BNE @small
        RTS                     ; A = 0: unchanged
@bad:   LDA #$FF
        RTS
@diff:  JMP @conv
@small: CPX #3                  ; +1 / +2: step the cached cells
        BCS @conv
        STX hh_i
        LDA #' '
        LDX #4
@sp:    STA hh_dig,X
        DEX
        BPL @sp
        LDA hh_do               ; cells (do + w - 1 .. do) -> dig[4 .. 5-w]
        CLC
        ADC hh_w
        TAY
        DEY
        LDX #4
@cp:    LDA (hh_fld),Y
        STA hh_dig,X
        DEY
        DEX
        CPX hh_first            ; (signed: X - first runs down to -1)
        BPL @cp
@step:  LDX #4                 ; + 1, decimal, on the characters
@cy:    LDA hh_dig,X
        CMP #'9'
        BNE @up
        LDA #'0'
        STA hh_dig,X
        DEX
        BPL @cy
        BMI @conv               ; (99999 + 1: cannot happen in 16 bits)
@up:    CMP #' '
        BNE @dg
        LDA #'0'
@dg:    TAY
        INY
        TYA
        STA hh_dig,X
        DEC hh_i
        BNE @step
        BEQ @check
@conv:  JSR hh_utoa             ; the general case: convert hh_val
@check: LDX hh_first            ; the cell left of the field must be blank
        BEQ @draw
        LDA hh_dig-1,X
        CMP #' '
        BNE @bad
@draw:  LDA ht_col              ; keep the caller's text cursor
        PHA
        LDA ht_sl
        PHA
        LDA #0
        STA hh_chg
        STA hh_i
@cell:  LDA hh_i
        CLC
        ADC hh_first
        TAX
        LDA hh_dig,X
        STA hh_ch
        LDA hh_do
        CLC
        ADC hh_i
        TAY
        LDA hh_valid
        BEQ @paint
        LDA (hh_fld),Y
        CMP hh_ch
        BEQ @next
@paint: LDA hh_ch
        STA (hh_fld),Y          ; record the cell as drawn
        LDY #0
        LDA (hh_fld),Y
        CLC
        ADC hh_i
        STA ht_col
        INY
        LDA (hh_fld),Y
        STA ht_sl
        LDA hh_ch
        JSR hgr_putc8
        INC hh_chg
@next:  INC hh_i
        LDA hh_i
        CMP hh_w
        BNE @cell
        PLA
        STA ht_sl
        PLA
        STA ht_col
        LDY hh_vo               ; record the value and the page
        LDA hh_val
        STA (hh_fld),Y
        INY
        LDA hh_val+1
        STA (hh_fld),Y
        LDY #3
        LDA (hh_fld),Y
        ORA hh_pb
        STA (hh_fld),Y
        LDA hh_chg
        RTS

; ----------------------------------------------------------------------------
; hgr_hud8_invalidate: A = pages to forget (1, 2 or 3)
; ----------------------------------------------------------------------------
hgr_hud8_invalidate:
        EOR #$FF
        LDY #3
        AND (hh_fld),Y
        STA (hh_fld),Y
        RTS

; ----------------------------------------------------------------------------
; hgr_putu8: hh_val in decimal at the text cursor, no padding
; ----------------------------------------------------------------------------
hgr_putu8:
        JSR hh_utoa
        LDX #0
@skip:  LDA hh_dig,X            ; hh_dig[4] is never a space
        CMP #' '
        BNE @out
        INX
        BNE @skip
@out:   LDA hh_dig,X
        JSR hgr_putc8           ; preserves X
        INX
        CPX #5
        BNE @out
        RTS

; ----------------------------------------------------------------------------
; hh_utoa: hh_val -> hh_dig[0..4], right-aligned, leading spaces ("    0").
; Repeated subtraction of 10000 / 1000 / 100 / 10: ~250-1 100 cycles.
; Clobbers A, X, Y.
; ----------------------------------------------------------------------------
hh_utoa:
        LDA hh_val
        STA hh_n
        LDA hh_val+1
        STA hh_n+1
        LDA #' '
        STA hh_pad
        LDX #0
@digit: LDY #'0'
@sub:   SEC
        LDA hh_n
        SBC hh_plo,X
        STA hh_t
        LDA hh_n+1
        SBC hh_phi,X
        BCC @got
        STA hh_n+1
        LDA hh_t
        STA hh_n
        INY
        BNE @sub                ; (always)
@got:   CPY #'0'
        BNE @nz
        LDY hh_pad              ; a leading zero stays blank
        BNE @put                ; (always)
@nz:    LDA #'0'                ; from the first nonzero digit on, zeros
        STA hh_pad              ; are digits
@put:   TYA
        STA hh_dig,X
        INX
        CPX #4
        BNE @digit
        LDA hh_n                ; units: always a digit
        ORA #'0'
        STA hh_dig+4
        RTS

hh_plo:  .byte <10000, <1000, <100, <10
hh_phi:  .byte >10000, >1000, >100, >10
hh_pbit: .byte 1, 2
hh_voff: .byte 4, 6
hh_doff: .byte 8, 13

; --- scratch (next to the code) ---------------------------------------------
hh_w:     .byte 0               ; field width
hh_first: .byte 0               ; 5 - width
hh_pb:    .byte 0               ; this page's flag bit
hh_vo:    .byte 0               ; this page's value offset
hh_do:    .byte 0               ; this page's cells offset
hh_valid: .byte 0               ; nonzero: the cache holds what is on screen
hh_i:     .byte 0
hh_chg:   .byte 0
hh_ch:    .byte 0
hh_n:     .byte 0, 0
hh_t:     .byte 0
hh_pad:   .byte 0
hh_dig:   .byte 0, 0, 0, 0, 0

.endif  ; _HGR_HUD8_LOADED_
