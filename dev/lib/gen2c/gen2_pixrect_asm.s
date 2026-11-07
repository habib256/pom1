; gen2_pixrect_asm.s — gen2c archive member: _gen2_pixrect_asm, the PIXEL
; rectangle fill/erase behind gen2_hgr_fill_pixrect / clear_pixrect /
; fill_box (gen2_rect.c) and gen2_hgr_cell (gen2_cell_asm.s jumps here).
;
; Derives the byte columns + edge masks for the rectangle [x..xr] from the
; col7/mask7 LUTs (no division), builds per-region (keep,set) values for the
; requested mode, then for each scanline applies (byte & keep) | set to the left
; partial byte, stores setF into the full bytes between TWO PER LOOP PASS (21
; cycles a pair, the pair/odd counts computed once per call — ported from
; pom2games dev/lib/hgrc/hgr_pixrect_asm.s, same author, GPL-3.0), and
; (byte & keep) | set to the right partial byte. colL == colR means the rect
; fits in one byte (only the left step runs). One uniform op does fill (mode=1, set pixels) or erase
; (mode=0, clear pixels). Replaces a per-pixel gen2_hgr_plot loop: a 6x6 block is
; one call instead of 36, and the inner run is a tight STA loop.
; Note: every keep mask is ANDed with $7F so bit 7 (the byte's HIRES palette
; bit) is ALWAYS cleared on both edges — without this, a previous draw's
; orange/blue palette would survive a clear and ghost into the next colour cycle
; (the row-at-a-time colour blitter only sets the palette bit when hi != 0).
;
; Owns its 7-byte zero-page argument block. The per-call derived values
; (columns, masks, pair/odd counts) live in cc65 scratch (tmp3, tmp4,
; ptr2..ptr4) and one BSS byte: the routine makes no call, so they need no
; private zero page. Pinned by micro-test t43_c_rect. Destroys A, X, Y, ptr1..ptr4,
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

colL  = tmp3                    ; left byte column
colR  = tmp4                    ; right byte column (== colL for one byte)
keepL = ptr3                    ; left byte: AND mask
setL  = ptr3+1                  ; left byte: OR mask
keepR = ptr4                    ; right byte: AND mask
setR  = ptr4+1                  ; right byte: OR mask
                                ; ptr2 / ptr2+1: full-byte pairs / odd byte

        .bss
setF:   .res 1                  ; full bytes: $7F fill / $00 erase

        .segment "CODE"
_gen2_pixrect_asm:
        ; --- left edge: colL = col7[x], leftMask (bits x%7..6) -> tmp1 ------
        lda _gen2_r_x+1
        bne @xhi
        ldx _gen2_r_x
        lda _gen2_col7,x
        sta colL
        lda _gen2_mask7,x
        jmp @xmask
@xhi:   ldx _gen2_r_x
        lda _gen2_col7+256,x
        sta colL
        lda _gen2_mask7+256,x
@xmask: sec                     ; A = 1<<bitL ; leftMask = ~(A-1) & $7F
        sbc #1
        eor #$7F                ; A-1 is 0..63
        sta tmp1
        ; --- right edge: colR = col7[xr], rightMask (bits 0..xr%7) -> tmp2 --
        lda _gen2_r_xr+1
        bne @rhi
        ldx _gen2_r_xr
        lda _gen2_col7,x
        sta colR
        lda _gen2_mask7,x
        jmp @rmask
@rhi:   ldx _gen2_r_xr
        lda _gen2_col7+256,x
        sta colR
        lda _gen2_mask7+256,x
@rmask: asl a                   ; A = 1<<bitR ; rightMask = (A<<1) - 1
        sbc #0                  ; ASL cleared C: subtract one -> 1..127
        sta tmp2
        ; --- keep/set per region ----------------------------------------------
        lda colR
        cmp colL                ; C = 1 (ordered edges), kept to the count
        bne @masks
        lda tmp1                ; one byte: both edges' masks combined
        and tmp2
        sta tmp1
@masks: lda _gen2_r_mode
        bne @set
        lda tmp1                ; erase: keep = ~mask & $7F, set = 0
        eor #$7F
        sta keepL
        lda tmp2
        eor #$7F
        sta keepR
        lda #0
        sta setL
        sta setF
        sta setR
        beq @fill               ; always
@set:   lda #$7F                ; fill: keep = $7F (drops the palette bit)
        sta keepL
        sta keepR
        sta setF
        lda tmp1
        sta setL
        lda tmp2
        sta setR
@fill:  lda _gen2_r_y0
        sta tmp1                ; current scanline
        lda _gen2_r_rows
        sta tmp2                ; rows remaining
        ; full bytes between the edges: pairs in ptr2, the odd one in ptr2+1
        ; (a one-byte rectangle uses neither). C = 1 from the edge compare.
        lda colR
        sbc colL
        beq @row
        sbc #1                  ; C = 1: colR - colL - 1
        lsr a
        sta ptr2
        lda #0
        rol a
        sta ptr2+1
@row:   ldy tmp1
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        ldy colL                ; left (or only) byte: (b & keepL) | setL
        lda (ptr1),y
        and keepL
        ora setL
        sta (ptr1),y
        cpy colR
        beq @next               ; one-byte rectangle: row done
        iny
        ldx ptr2
        beq @odd                ; no pair between the edges
        lda setF
@pair:  sta (ptr1),y            ; 21 cycles a pair
        iny
        sta (ptr1),y
        iny
        dex
        bne @pair
@odd:   ldx ptr2+1
        beq @right
        lda setF
        sta (ptr1),y
        iny
@right: lda (ptr1),y            ; right byte: (b & keepR) | setR
        and keepR
        ora setR
        sta (ptr1),y
@next:  inc tmp1
        dec tmp2
        bne @row
        rts
