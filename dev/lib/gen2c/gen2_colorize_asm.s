; gen2_colorize_asm.s — gen2c archive member: _gen2_colorize_asm, the tint
; pass behind gen2_hgr_colorize (gen2_colorize.c).
;
; Apple-1 GEN2 HIRES has no per-pixel colour — colour is an NTSC artifact of the
; bit pattern + the byte's high bit. So a shape is drawn WHITE first, then this
; pass masks each byte down to the chosen colour's carrier and sets the high
; bit: b = (b & carrier[col&1]) | hibit. carrier alternates by ABSOLUTE byte
; column parity (the artifact phase flips every 7px byte).
; Arguments: its own 4-byte box block + the carrier block (gen2_carrier_params.s).
; Destroys A, X, Y, ptr1, tmp1, tmp2. Split from the former monolithic gen2_blit.s.

        .export   _gen2_colorize_asm
        .exportzp _gen2_z_col0, _gen2_z_ncols, _gen2_z_y0, _gen2_z_rows
        .import   _gen2_rowlo, _gen2_rowhi
        .importzp _gen2_z_ce, _gen2_z_co, _gen2_z_hi
        .importzp ptr1, tmp1, tmp2

        .segment "ZEROPAGE"
_gen2_z_col0:  .res 1        ; first byte column of the box
_gen2_z_ncols: .res 1        ; number of byte columns
_gen2_z_y0:    .res 1        ; top scanline
_gen2_z_rows:  .res 1        ; number of scanlines

        .segment "CODE"

_gen2_colorize_asm:
        lda _gen2_z_y0
        sta tmp1             ; current scanline
        lda _gen2_z_rows
        sta tmp2             ; rows remaining
@zrow:
        ldy tmp1
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ldx _gen2_z_ncols    ; column counter
        ldy _gen2_z_col0     ; current (absolute) byte column
@zcol:
        tya                  ; carrier = (col & 1) ? co : ce
        and #1
        bne @zodd
        lda _gen2_z_ce
        jmp @zhave
@zodd:
        lda _gen2_z_co
@zhave:
        and (ptr1),y         ; keep only the carrier bits of the white glyph
        ora _gen2_z_hi       ; set the palette high bit (orange/blue) or nothing
        sta (ptr1),y
        iny
        dex
        bne @zcol
        inc tmp1             ; next scanline
        dec tmp2
        bne @zrow
        rts
