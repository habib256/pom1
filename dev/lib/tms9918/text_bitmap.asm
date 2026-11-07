; ============================================================================
; text_bitmap.asm  --  8x8 monochrome glyph blitter for TMS9918 Mode-2
; ----------------------------------------------------------------------------
; Renders an Apple-1 charmap glyph into the bitmap pattern table at the
; cell aligned to (pix_x, pix_y), and tints the matching colour cell with
; pen_color. Lifted from TMS_Logo_16k.asm so any Mode-2 project that wants
; on-bitmap text overlays (scores, "GAME OVER", tutorial captions, narrator
; bubbles) can .import the routine instead of re-implementing it.
;
; The whole module is gated by CODETANK_BUILD because the 1 024-byte
; charmap.rom .incbin doesn't fit any 8 KB or 16 KB DRAM CODE slot --
; only the cartridge ROM has the headroom. Callers building for a DRAM
; target should not pass `-D CODETANK_BUILD`; the .o file then assembles
; empty and adds no bytes.
;
; ----------------------------------------------------------------------------
; API:
;   text_blit_glyph        A = ASCII char (bit 7 ignored). Reads pix_x/pix_y as
;                     the cell-aligned pixel coordinate of the glyph's
;                     top-left corner; the routine masks low 3 bits to
;                     guarantee cell alignment, so callers can pass
;                     unaligned values. Tints the cell with pen_color.
;                     Clobbers A, X, Y, mptr_lo/hi, tmp2, pix_addr_lo/hi
;                     (and tmp through calc_pix_addr).
;
; Caller-provided ZP (must be .exportzp'd by the caller):
;   tmp, tmp2         scratch (1 byte each)
;   mptr_lo, mptr_hi  16-bit cursor into tms_charmap (1 byte each)
;
; Lib-provided ZP (already exported by tms9918m2.asm):
;   pix_x, pix_y                 input position (cell-aligned on entry/exit)
;   pix_addr_lo/hi               clobbered, holds VRAM cursor
;   pen_color                    foreground colour for the colour cell
;
; Font: charmap_tms.inc = roms/charmap.rom bit-reversed at build time by
; tools/build_charmap_tms.py (bit 7 = leftmost pixel, as the TMS9918 reads
; it), so each glyph row goes to VRAM as is -- the per-row 8-round LSR/ROL
; reversal (~95 cycles a row) is gone. Port pacing goes through the private
; vdp_write_a / vdp_ctrl_a of tms9918_xfer.asm (included at the end).
; ============================================================================

.ifdef CODETANK_BUILD

.include "tms9918.inc"          ; VDP_DATA / VDP_CTRL

.export   text_blit_glyph

.import   calc_pix_addr, vdp_set_write
.importzp pix_x, pix_y, pix_addr_lo, pix_addr_hi, pen_color
.importzp tmp, tmp2, mptr_lo, mptr_hi

.segment "CODE"

; ----------------------------------------------------------------------------
; text_blit_glyph: paint one 8x8 glyph + its colour cell. See module header for
;   the full register / ZP contract.
; ----------------------------------------------------------------------------
text_blit_glyph:
        AND #$7F                  ; 128 glyphs: codes $00-$7F all valid
        LDX #0                    ; A:tmp2 = A * 8 -- the low byte stays in
        STX tmp2                  ; A, only the high byte needs memory
        ASL A                     ; (index trick ported from pom2games
        ROL tmp2                  ; dev/lib/tms9918/text_bitmap.asm @ c0c66fd)
        ASL A
        ROL tmp2
        ASL A
        ROL tmp2                  ; C = old tmp2 bit 7 = 0 (tmp2 <= 3): no CLC
        ADC #<tms_charmap
        STA mptr_lo
        LDA tmp2
        ADC #>tms_charmap
        STA mptr_hi
        ; force cell-aligned write addr (low 3 bits of pix_y = 0)
        LDA pix_x
        AND #$F8
        STA pix_x
        LDA pix_y
        AND #$F8
        STA pix_y
        JSR calc_pix_addr
        JSR vdp_set_write
        LDY #0
@row:   LDA (mptr_lo),Y           ; already bit 7 = leftmost pixel
        JSR vdp_write_a           ; 5 + 22 + 2 + 2 + 3 = 34c a row
        INY
        CPY #8
        BNE @row
        ; --- paint the cell's 8 colour-table bytes with pen_color so callers
        ;     also tint LABEL/SAY text. The colour table mirrors the
        ;     pattern table layout at base $2000, so we can re-prime the
        ;     VDP cursor with the same pix_addr but the $2000 base bit set.
        LDA pix_addr_lo
        JSR vdp_ctrl_a
        LDA pix_addr_hi
        ORA #$60                  ; $20 (colour base) | $40 (write enable)
        JSR vdp_ctrl_a            ; cmd byte -> first @col store: 12c + the
                                  ; 15c LDA/ASLx4/ORA/LDX bridge + 6c JSR =
                                  ; 37c (LOGO LABEL/SAY drop site, May 2026,
                                  ; needed >= 22c)
        LDA pen_color
        ASL
        ASL
        ASL
        ASL                       ; pen_color in high nibble
        ORA #$01                  ; transparent background ($x1)
        LDX #8
@col:   JSR vdp_write_a           ; 22 + 2 + 3 = 27c a byte
        DEX
        BNE @col
        RTS

; ============================================================================
; tms_charmap: 1024-byte 8x8 monochrome font = roms/charmap.rom (the Apple-1
;   character set, 128 glyphs, codes $00-$7F, 8 rows a glyph) bit-reversed
;   at build time (charmap_tms.inc). Local label -- callers blit through the
;   API, not through direct font reads.
; ============================================================================
.include "charmap_tms.inc"

.include "tms9918_xfer.asm"       ; private vdp_write_a / vdp_ctrl_a

.endif  ; CODETANK_BUILD
