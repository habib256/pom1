; ============================================================================
; hgr_text8.asm -- byte-aligned 8x8 text for the GEN2 HGR framebuffer
; ----------------------------------------------------------------------------
; THE shared glyph emitter for HGR games. Before this module existed,
; GEN2_Chess (putc_hgr), HGR_Rogue (hgr_emit_a) and HGR_Maze3D
; (write_char) each carried a private copy of the same idea: STORE one
; 8x8 glyph byte per scanline at a byte column, with a text cursor.
;
; The glyph is drawn in STORE mode (a space genuinely blanks its cell),
; 8 scanlines from ht_sl, at byte column ht_col; the cursor advances one
; column per char and wraps VDP-style: when ht_col reaches ht_wrap it
; snaps back to ht_left and ht_sl advances 8 -- so a stream of chars
; walks the screen exactly like the TMS9918 auto-increment write
; pointer, which is what lets TMS-ported print loops run unchanged.
; Set ht_wrap = 40 if you never want the wrap.
;
; Fonts: ht_font_lo/hi -> 8-bytes-per-glyph table for chars $20.. ;
; ht_rev picks the bit order:
;   ht_rev = 0   HGR order (bit 0 = leftmost) -- bbfont_ascii5f.inc etc.
;   ht_rev = 1   TMS order (bit 7 = leftmost) -- rows pass through
;                rev7_tab (the glyph's rightmost pixel column is dropped;
;                fine for fonts, which keep a blank right column).
; A program whose font is in HGR order can define HGR_TEXT8_HGR_ORDER = 1
; before the include: ht_rev, the rev7 path and the 256-byte rev7_tab go away.
; Chars < $20 render as space, $60..$7F fold to uppercase: every char lands
; in the 64-glyph window $20..$5F. ht_sl >= 192 skips the draw (the cursor
; still advances). A glyph started at scanline 185..191 is clipped at the
; bottom edge, and a cursor already off-screen stays off-screen when it
; wraps (it used to wrap 248+8 back to scanline 0).
;
; API:
;   hgr_putc8   A = char (bit 7 tolerated). PRESERVES A, X and Y --
;               ported print loops keep counters in all three, and the
;               TMS write-data macros they emulate preserved everything.
;               Glyph bytes pass through the artifact-colour attributes
;               ht_cm_ev / ht_cm_od (pixel-parity masks, alternating by
;               byte-column parity) + ht_cbit (palette bit): WHITE =
;               $7F/$7F/$00 is the pass-through -- INIT THEM AT BOOT.
;               Glyph bit 7 is always stripped: the palette bit comes
;               from ht_cbit, never from the font data.
;               HGR_TEXT8_FILTER = <label> (optional): called before each
;               draw with ht_a/col/sl/page set; C=1 skips the glyph while
;               the cursor still advances (incremental HUDs). It may
;               destroy A/X/Y, never ht_a or the ht_* cursor/attributes.
;   hgr_puts8   NUL-terminated string at ht_src_lo/hi, may span pages.
;               Preserves X and the source pointer; clobbers A, Y.
;               HGR_TEXT8_NO_PUTS = 1 leaves it out.
;   hgr_puts8_at  inline arguments, one 7-byte call site per string:
;                 JSR hgr_puts8_at / .byte col, scanline / .addr string
;               (macro PUTS8_AT string, col, scanline). Sets ht_col, ht_sl
;               and ht_src_lo/hi, then runs hgr_puts8. Clobbers A, X, Y.
;               40 B once, against 13 B per site for the LDA/STA sequence.
;               HGR_TEXT8_AT_PAGE = <label>: copy that byte into ht_page
;               first (double-buffered programs keeping their own page).
;               Assembled only when referenced (.ifref): include this file
;               a second time after the last caller to get it.
;
; Caller provides hgr_lo / hgr_hi (include hgr_scanline.inc). The module
; allocates its own ZP (~12 B) and pulls rev7.inc (shared, guarded).
;
; History: the row loop, bottom clip, off-screen wrap fix, HGR_ORDER /
; FILTER / NO_PUTS switches and hgr_puts8_at were ported back from the
; pom2games Apple II edition of this module (2026-11-07): about 51 cycles
; per glyph row instead of 85, i.e. ~270 cycles saved per glyph.
;
; First consumers: sketchs/gen2/game_rogue (bbfont, HGR order) and
; sketchs/gen2/game_maze3d (its own font, TMS order) -- one module, both
; bit orders exercised.
; ============================================================================

.ifndef _HGR_TEXT8_LOADED_
_HGR_TEXT8_LOADED_ = 1
; Boolean switches: zero and undefined select the default implementation.
.ifndef HGR_TEXT8_HGR_ORDER
HGR_TEXT8_HGR_ORDER = 0
.endif
.ifndef HGR_TEXT8_NO_PUTS
HGR_TEXT8_NO_PUTS = 0
.endif

.zeropage
ht_col:     .res 1      ; cursor: dest byte column 0..39 (public)
ht_sl:      .res 1      ; cursor: glyph top scanline (public)
ht_left:    .res 1      ; wrap: column the cursor snaps back to (public)
ht_wrap:    .res 1      ; wrap: first column PAST the text window (public)
ht_font_lo: .res 1      ; -> font, 8 B/glyph, first char $20 (public)
ht_font_hi: .res 1
.if !HGR_TEXT8_HGR_ORDER
ht_rev:     .res 1      ; 0 = HGR bit order, 1 = TMS order via rev7_tab
.endif
ht_src_lo:  .res 1      ; hgr_puts8 string pointer (public)
ht_src_hi:  .res 1
ht_a:       .res 1      ; char cache (A preserved across hgr_putc8)
ht_t:       .res 1      ; this glyph's pixel mask (colour parity)
ht_g_lo:    .res 1      ; glyph pointer, biased by -ht_sl
ht_g_hi:    .res 1
ht_lin_lo:  .res 1      ; scanline pointer (column folded in)
ht_lin_hi:  .res 1
ht_end:     .res 1      ; last scanline + 1 of this glyph (bottom clip)
ht_cm_ev:   .res 1      ; colour: pixel mask for EVEN byte columns (public)
ht_cm_od:   .res 1      ; colour: pixel mask for ODD byte columns (public)
ht_cbit:    .res 1      ; colour: palette bit $00/$80 (public)
ht_page:    .res 1      ; HGR page selector, EORed into the scanline
                        ; high byte ($00 = page 1, $60 = page 2). INIT
                        ; AT BOOT; double-buffered games flip it

.code

; ----------------------------------------------------------------------------
; hgr_putc8: draw char A at the cursor, advance + wrap. Preserves A/X/Y.
; ----------------------------------------------------------------------------
hgr_putc8:
        STA ht_a
        TXA
        PHA
        TYA
        PHA
        ; VDP-style wrap at ENTRY: a stream that just filled the window
        ; draws its next char at the head of the next text row.
        LDA ht_col
        CMP ht_wrap
        LDA ht_sl
        BCC @nowrap
        LDX ht_left
        STX ht_col
        CMP #192
        BCS @out                ; off-screen rows stay off-screen
        ADC #8                  ; C = 0
        STA ht_sl
@nowrap:
        CMP #192
        BCS @out                ; off-screen: skip draw, still advance
.ifdef HGR_TEXT8_FILTER
        JSR HGR_TEXT8_FILTER    ; C = 1: skip this glyph, still advance
        BCS @out
        LDA ht_sl
.endif
        ADC #8                  ; C = 0: last scanline + 1; partial glyphs
        CMP #192                ; clip at the bottom edge
        BCC @end
        LDA #192
@end:   STA ht_end
        ; glyph index: fold lowercase, control chars to space
        LDA ht_a
        AND #$7F
        CMP #$60
        BCC @nofold
        SBC #$21                ; $60..$7F -> uppercase - 1 (C stays set)
@nofold:
        SBC #$1F                ; - $20 with C = 0, the fold's - $1F with C = 1
        BCS @idx_ok             ; $00..$3F: at most $5F - $20 after the fold
        LDA #0                  ; control chars -> space
@idx_ok:
        ; glyph ptr = font + idx * 8: idx < 64, so the product is 9 bits and
        ; three shifts in A leave its bit 8 in the carry. The pointer is
        ; biased by -ht_sl, so the scanline indexes the glyph and the row
        ; tables alike.
        ASL A
        ASL A
        ASL A
        TAX
        LDA ht_font_hi
        ADC #0                  ; + bit 8 of the product
        STA ht_g_hi
        TXA
        CLC
        ADC ht_font_lo
        BCC @g_lo
        INC ht_g_hi
@g_lo:  SEC
        SBC ht_sl
        STA ht_g_lo
        BCS @c_par
        DEC ht_g_hi
@c_par:
        ; artifact colour: the pixel mask follows the byte-column parity and
        ; is constant for the 8 rows. WHITE ($7F/$7F/$00) is a pass-through --
        ; the caller's boot MUST init the three attributes (garbage masks =
        ; invisible text on a real cold boot).
        LDA ht_col
        LSR A                   ; C = column parity
        LDA ht_cm_ev
        BCC @c_m
        LDA ht_cm_od
@c_m:   STA ht_t
        LDY ht_sl
        LDX #0                  ; (ht_lin_lo,X): Y stays the scanline
        CLC
@row:   LDA hgr_lo,Y
        ADC ht_col              ; C = 0; hgr_lo + 39 never carries
        STA ht_lin_lo
        LDA hgr_hi,Y
        EOR ht_page
        STA ht_lin_hi
        LDA (ht_g_lo),Y         ; glyph row byte
.if !HGR_TEXT8_HGR_ORDER
        LDX ht_rev
        BEQ @put
        TAX
        LDA rev7_tab,X          ; TMS bit order -> HGR
        LDX #0
@put:
.endif
        AND ht_t                ; glyph bit 7 is always stripped: the palette
        ORA ht_cbit             ; bit comes from ht_cbit, never from the font
        STA (ht_lin_lo,X)       ; STORE: text overwrites its cell
        INY
        CPY ht_end
        BCC @row                ; (C = 0 for the ADC)
@out:
        INC ht_col
        PLA
        TAY
        PLA
        TAX
        LDA ht_a
        RTS

; ----------------------------------------------------------------------------
; hgr_puts8: print the NUL-terminated string at ht_src_lo/hi. Clobbers A,Y.
; ----------------------------------------------------------------------------
.if !HGR_TEXT8_NO_PUTS
hgr_puts8:
        LDA ht_src_hi
        PHA                     ; keep the caller's source pointer
        LDY #0
@lp:    LDA (ht_src_lo),Y
        BEQ @done
        JSR hgr_putc8           ; preserves Y
        INY
        BNE @lp
        INC ht_src_hi           ; Y wrapped: continue with the next 256 bytes
        JMP @lp
@done:  PLA
        STA ht_src_hi
        LDA #0                  ; keep the historical NUL return value
        RTS
.endif

.if !HGR_TEXT8_HGR_ORDER
.include "rev7.inc"
.endif

; PUTS8_AT string, col, scanline: one hgr_puts8_at call site (7 bytes).
.macro PUTS8_AT str, col, sl
        JSR hgr_puts8_at
        .byte col, sl
        .addr str
.endmacro

.endif  ; _HGR_TEXT8_LOADED_

; ----------------------------------------------------------------------------
; hgr_puts8_at: inline-argument hgr_puts8 (see API above). Outside the load
; guard: a later include of this file assembles it once it is referenced.
; ----------------------------------------------------------------------------
.ifndef _HGR_PUTS8_AT_LOADED_
.ifref hgr_puts8_at
_HGR_PUTS8_AT_LOADED_ = 1
.if HGR_TEXT8_NO_PUTS
.error "hgr_puts8_at needs hgr_puts8 (HGR_TEXT8_NO_PUTS is enabled)"
.endif
.code
hgr_puts8_at:
        PLA                     ; return address = last byte of the JSR
        STA ht_src_lo
        CLC
        ADC #4                  ; push it back + 4: RTS lands after the data
        TAY
        PLA
        STA ht_src_hi
        ADC #0
        PHA
        TYA
        PHA
        LDY #1
        LDA (ht_src_lo),Y       ; .byte col
        STA ht_col
        INY
        LDA (ht_src_lo),Y       ; .byte scanline
        STA ht_sl
        INY
        LDA (ht_src_lo),Y       ; .addr string
        TAX
        INY
        LDA (ht_src_lo),Y
        STA ht_src_hi
        STX ht_src_lo
.ifdef HGR_TEXT8_AT_PAGE
        LDA HGR_TEXT8_AT_PAGE
        STA ht_page
.endif
        JMP hgr_puts8
.endif
.endif
