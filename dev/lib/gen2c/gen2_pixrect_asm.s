; gen2_pixrect_asm.s — gen2c archive member: _gen2_pixrect_asm, the PIXEL
; rectangle fill/erase behind gen2_hgr_fill_pixrect / clear_pixrect /
; fill_box (gen2_rect.c) and gen2_hgr_cell (gen2_cell_asm.s jumps here).
;
; Derives the byte columns + edge masks for the rectangle [x..xr] from the
; col7/mask7 LUTs (no division), builds per-region (keep,set) values for the
; requested mode, then for each scanline applies (byte & keep) | set to the left
; partial byte, stores setF into the full bytes between, and (byte & keep) | set
; to the right partial byte. colL == colR means the rect fits in one byte (only
; the left step runs). One uniform op does fill (mode=1, set pixels) or erase
; (mode=0, clear pixels). Replaces a per-pixel gen2_hgr_plot loop: a 6x6 block is
; one call instead of 36, and the inner run is a tight STA loop.
; Note: every keep mask is ANDed with $7F so bit 7 (the byte's HIRES palette
; bit) is ALWAYS cleared on both edges — without this, a previous draw's
; orange/blue palette would survive a clear and ghost into the next colour cycle
; (the row-at-a-time colour blitter only sets the palette bit when hi != 0).
;
; Owns its 7-byte zero-page argument block. The 7 per-call derived values
; (columns + masks) live in cc65 scratch (tmp3, tmp4, ptr2..ptr4): the routine
; makes no call, so they need no private slots. Destroys A, X, Y, ptr1..ptr4,
; tmp1..tmp4. Split from the former monolithic gen2_blit.s.

        .export   _gen2_pixrect_asm
        .exportzp _gen2_r_x, _gen2_r_xr, _gen2_r_y0, _gen2_r_rows, _gen2_r_mode
        .import   _gen2_rowlo, _gen2_rowhi, _gen2_col7, _gen2_mask7
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

        .segment "ZEROPAGE"
; The C wrapper only passes the geometry + mode; the asm derives the rest.
_gen2_r_x:     .res 2        ; left pixel x (0..279)
_gen2_r_xr:    .res 2        ; right pixel x (0..279, clipped)
_gen2_r_y0:    .res 1        ; top scanline
_gen2_r_rows:  .res 1        ; number of scanlines (>= 1)
_gen2_r_mode:  .res 1        ; 1 = fill (white), 0 = clear (black)

; derived per call, persist across the row loop (cc65 scratch):
gr_colL  = tmp3              ; left  byte column
gr_colR  = tmp4              ; right byte column (== colL if rect fits one byte)
gr_keepL = ptr2              ; left  byte: AND mask
gr_setL  = ptr2+1            ; left  byte: OR  mask
gr_setF  = ptr3              ; full bytes: stored directly ($7F fill / $00 erase)
gr_keepR = ptr3+1            ; right byte: AND mask
gr_setR  = ptr4              ; right byte: OR  mask

        .segment "CODE"

_gen2_pixrect_asm:
        ; --- left edge: gr_colL = col7[x], leftMask (bits x%7..6) -> tmp1 ----
        lda _gen2_r_x+1
        bne @xhi
        ldx _gen2_r_x
        lda _gen2_col7,x
        sta gr_colL
        lda _gen2_mask7,x
        jmp @xmask
@xhi:
        ldx _gen2_r_x
        lda _gen2_col7+256,x
        sta gr_colL
        lda _gen2_mask7+256,x
@xmask:
        sec                  ; A = 1<<bitL ; leftMask = (~(A-1)) & $7F
        sbc #1
        eor #$ff
        and #$7f
        sta tmp1             ; tmp1 = leftMask
        ; --- right edge: gr_colR = col7[xr], rightMask (bits 0..xr%7) -> tmp2 -
        lda _gen2_r_xr+1
        bne @rhi
        ldx _gen2_r_xr
        lda _gen2_col7,x
        sta gr_colR
        lda _gen2_mask7,x
        jmp @rmask
@rhi:
        ldx _gen2_r_xr
        lda _gen2_col7+256,x
        sta gr_colR
        lda _gen2_mask7+256,x
@rmask:
        asl a                ; A = 1<<bitR ; rightMask = ((A<<1)-1) & $7F
        sec
        sbc #1
        and #$7f
        sta tmp2             ; tmp2 = rightMask
        ; --- build keep/set from leftMask/rightMask + mode ------------------
        lda gr_colL
        cmp gr_colR
        bne @multi
        ; single byte: combined mask = leftMask & rightMask
        lda tmp1
        and tmp2
        sta tmp1             ; tmp1 = combined mask
        lda _gen2_r_mode
        bne @one_set
        lda tmp1             ; clear: keepL = ~mask & $7F (also drops palette bit)
        eor #$ff
        and #$7f
        sta gr_keepL
        lda #0
        sta gr_setL
        jmp @fill
@one_set:
        lda #$7f             ; fill: keepL = $7F (clears palette), setL = mask
        sta gr_keepL
        lda tmp1
        sta gr_setL
        jmp @fill
@multi:
        lda _gen2_r_mode
        bne @multi_set
        ; clear: keep = ~mask & $7F (drop palette too), set/full = 0
        lda tmp1
        eor #$ff
        and #$7f
        sta gr_keepL
        lda tmp2
        eor #$ff
        and #$7f
        sta gr_keepR
        lda #0
        sta gr_setL
        sta gr_setF
        sta gr_setR
        jmp @fill
@multi_set:
        lda #$7f             ; fill: clear palette + OR edge masks, full = $7F
        sta gr_keepL
        sta gr_keepR
        lda tmp1
        sta gr_setL
        lda tmp2
        sta gr_setR
        lda #$7f
        sta gr_setF
@fill:
        lda _gen2_r_y0
        sta tmp1             ; current scanline
        lda _gen2_r_rows
        sta tmp2             ; rows remaining
@prow:
        ldy tmp1
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ; left (or single) byte: (byte & keepL) | setL
        ldy gr_colL
        lda (ptr1),y
        and gr_keepL
        ora gr_setL
        sta (ptr1),y
        ; single-byte rectangle? then this row is done
        lda gr_colL
        cmp gr_colR
        beq @prowend
        ; full bytes colL+1 .. colR-1 (A holds setF across the run)
        ldy gr_colL
        iny
        lda gr_setF
@pfull:
        cpy gr_colR
        bcs @pright
        sta (ptr1),y
        iny
        bne @pfull           ; Y is a column 1..39, never 0 -> always loops
@pright:
        ldy gr_colR
        lda (ptr1),y
        and gr_keepR
        ora gr_setR
        sta (ptr1),y
@prowend:
        inc tmp1             ; next scanline
        dec tmp2
        bne @prow
        rts
