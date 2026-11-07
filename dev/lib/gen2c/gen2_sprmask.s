; gen2_sprmask.s -- masked pre-shifted sprite kernels for GEN2 HGR (cc65).
;
; The SPRMASK family: the save-under complement to the XOR pre-shift engine
; (gen2_blit.s _gen2_xs_run). XOR is self-erasing but TRASHES a non-uniform
; background while the sprite overlaps it (decor shows through the sprite as
; toggled garbage). These kernels instead do a MASKED blit
;
;     dst[j] = (dst[j] & mask[j]) | data[j]
;
; over a 7-phase pre-shifted bank (gen2_mspr_t in gen2.h: `data` = sprite
; pixels, bit 7 clear; `mask` = ~coverage with bit 7 ALWAYS 1, so the
; background byte's HIRES palette-group bit survives every draw), paired with
; a save-under/restore of the covered byte rectangle. Draw over anything,
; restore, and the background is byte-identical -- what the sprite ENGINE
; (gen2_sprengine.c) is built on.
;
; All four entries share one setup (ms_setup): column x/7 and phase x%7 come
; from the gen2_col7[]/gen2_phase7[] LUTs (no runtime divide, +256 split for
; x >= 256), the gen2_mspr_t is dereferenced, the sprite is right-clipped
; (col + stride > 40) and bottom-clipped (y + h > 192) against the REMAINING
; columns/rows (40-col, 192-y), so no sum can overflow. The drawing entries
; then apply the phase offset phase*(h*stride) to the data and mask banks by
; loop-add (no software multiply); phase 0 skips the product entirely, and
; save/restore skip the data/mask pointers and the product altogether (they
; only need the geometry). Scanline bases come from _gen2_rowlo/_gen2_rowhi,
; which the C runtime keeps pointed at the CURRENT DRAW PAGE
; (gen2_set_draw_page rewrites the hi table), so these kernels are
; page-agnostic.
;
; The byte loops count DOWN (Y = width-1 .. 0, DEY/BPL), so Y is both the
; offset and the counter: per byte draw 26, save/restore 16, save+draw 32
; cycles (were 29 / 19 / 35 counting up with CPY). Restore and save+draw are
; unrolled for the clipped widths 2 and 4, the common game sprite sizes.
;
; The under-buffer is a plain linear stride*h byte block: row r of the sprite
; rectangle occupies under[r*stride .. r*stride+w) (w = clipped width; the
; clipped tail bytes of a row are simply never touched). Save and restore walk
; it with the SAME geometry, so a restore at the same (x, y, spr) is exact.
;
; Interface -- the C wrapper (gen2_sprengine.c) stores these, then JSRs:
;     _gen2_ms_x     : pixel x (0..279, 16-bit)
;     _gen2_ms_y     : pixel y (0..191)
;     _gen2_ms_spr   : pointer to the gen2_mspr_t {data, mask, stride, h}
;     _gen2_ms_under : pointer to the caller's stride*h save-under buffer
; The caller guarantees x <= 279 / y <= 191, stride, h >= 1 and built tables
; (the kernels still edge-clip on the right/bottom). No self-modification.
;
; Ported from pom2games dev/lib/hgrc/hgr_sprmask.s (same author, GPL-3.0),
; minus its fixed row tables + draw-base OR and its one-call product hint:
; POM1 keeps the per-page mutable row tables every other gen2c kernel reads.

        .export   _gen2_ms_run
        .export   _gen2_ms_save_run
        .export   _gen2_ms_restore_run
        .export   _gen2_msu_run
        .exportzp _gen2_ms_x, _gen2_ms_y, _gen2_ms_spr, _gen2_ms_under
        .import   _gen2_rowlo, _gen2_rowhi, _gen2_col7, _gen2_phase7
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp3, tmp4

; --- interface variables (zero page) ----------------------------------------
        .segment "ZEROPAGE"
_gen2_ms_x:     .res 2       ; pixel x (0..279)
_gen2_ms_y:     .res 1       ; pixel y (0..191)
_gen2_ms_spr:   .res 2       ; -> gen2_mspr_t {data lo/hi, mask lo/hi, stride, h}
_gen2_ms_under: .res 2       ; -> linear save-under buffer (stride*h bytes)
; asm-internal state (derived by ms_setup, persists across the row loop):
ms_col:    .res 1            ; start byte column (x / 7)
ms_w:      .res 1            ; bytes drawn per row (right-clipped)
ms_h:      .res 1            ; rows remaining (bottom-clipped)
ms_stride: .res 1            ; source bytes per row (uniform across phases)

; cc65 scratch, setup-time roles then row-loop roles (all leaf, no calls):
;   tmp1 = phase during setup, then ms_yy (current scanline)
;   tmp3 = full sprite h, then the phase block's high byte
;   tmp4 = phase block's low byte
;   ptr1 = descriptor during setup, then framebuffer row base + col
;   ptr2 = data row pointer  (phase-adjusted, advanced by stride per row)
;   ptr3 = mask row pointer  (phase-adjusted, advanced by stride per row)
;   ptr4 = under row pointer (advanced by stride per row)
ms_yy     = tmp1
ms_blk    = tmp4
ms_blk_hi = tmp3

        .segment "CODE"

; --- ms_geom : column/phase, stride/h, clips, under pointer -------------------
; Leaves tmp1 = phase, tmp3 = full h, ptr1 = descriptor; ms_yy NOT yet set.
ms_geom:
        lda _gen2_ms_x+1
        bne @xhi
        ldx _gen2_ms_x
        lda _gen2_col7,x
        ldy _gen2_phase7,x
        jmp @xset
@xhi:   ldx _gen2_ms_x
        lda _gen2_col7+256,x
        ldy _gen2_phase7+256,x
@xset:  sta ms_col
        sty tmp1                  ; phase (0..6)
        lda _gen2_ms_spr
        sta ptr1
        lda _gen2_ms_spr+1
        sta ptr1+1
        ldy #4
        lda (ptr1),y              ; stride
        sta ms_stride
        sta ms_w                  ; default w = stride
        iny
        lda (ptr1),y              ; h
        sta ms_h                  ; default h
        sta tmp3                  ; full h (for the phase block)
        lda #40                   ; right clip against the columns left
        sec
        sbc ms_col
        cmp ms_stride
        bcs @wok
        sta ms_w
@wok:   lda #192                  ; bottom clip against the rows left
        sec
        sbc _gen2_ms_y
        cmp tmp3
        bcs @hok
        sta ms_h
@hok:   lda _gen2_ms_under
        sta ptr4
        lda _gen2_ms_under+1
        sta ptr4+1
        rts

; --- ms_setup_under : save/restore — geometry only ---------------------------
ms_setup_under:
        jsr ms_geom
        lda _gen2_ms_y
        sta ms_yy
        rts

; --- ms_setup : drawing entries — geometry + phase-adjusted data/mask --------
ms_setup:
        jsr ms_geom
        ldy #0
        lda (ptr1),y              ; data low
        sta ptr2
        iny
        lda (ptr1),y              ; data high
        sta ptr2+1
        iny
        lda (ptr1),y              ; mask low
        sta ptr3
        iny
        lda (ptr1),y              ; mask high
        sta ptr3+1
        lda tmp1                  ; phase 0: no product, no offset
        beq @done
        ldx tmp3                  ; full h
        lda #0
        sta ms_blk_hi             ; (tmp3: h already in X)
@hloop: clc                       ; block = h * stride by loop-add
        adc ms_stride
        bcc @h2
        inc ms_blk_hi
@h2:    dex
        bne @hloop
        sta ms_blk
        ldx tmp1                  ; data += phase*blk ; mask += phase*blk
@ploop: clc
        lda ptr2
        adc ms_blk
        sta ptr2
        lda ptr2+1
        adc ms_blk_hi
        sta ptr2+1
        clc
        lda ptr3
        adc ms_blk
        sta ptr3
        lda ptr3+1
        adc ms_blk_hi
        sta ptr3+1
        dex
        bne @ploop
@done:  lda _gen2_ms_y            ; tmp1 is free now: it becomes ms_yy
        sta ms_yy
        rts

; --- ms_rowbase : ptr1 = rowbase(ms_yy) + ms_col -------------------------------
; The row tables carry the current DRAW page. Row low <= $D0 and a clipped
; column <= 39: the low add cannot carry, so no high-byte add, and C = 0 on
; return (ms_next_row / ms_next_under rely on it).
ms_rowbase:
        ldy ms_yy
        lda _gen2_rowlo,y
        clc
        adc ms_col
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        rts

; --- ms_next_row : data/mask/under += stride, yy += 1, rows -= 1 --------------
; Entered with C = 0 (from ms_rowbase; the byte loops keep it). Returns Z set
; when the last row is done (callers loop on BNE).
ms_next_row:
        lda ptr2                ; data += stride
        adc ms_stride
        sta ptr2
        bcc @m
        inc ptr2+1
@m:     clc
        lda ptr3                ; mask += stride
        adc ms_stride
        sta ptr3
        bcc @u
        inc ptr3+1
@u:     clc
ms_next_under:                  ; save/restore enter here, C = 0
        lda ptr4                ; under += stride (linear, row-major)
        adc ms_stride
        sta ptr4
        bcc @y
        inc ptr4+1
@y:     inc ms_yy
        dec ms_h                ; Z = done (flags survive the RTS)
        rts

; --- _gen2_ms_run : masked pre-shifted blit (no save-under) -------------------
; Per byte: dst = (dst & mask) | data, 26 cycles:
;     lda (ptr1),y 5 / and (ptr3),y 5 / ora (ptr2),y 5 / sta (ptr1),y 6
;     dey 2 / bpl 3
_gen2_ms_run:
        jsr ms_setup
@row:   jsr ms_rowbase
        ldy ms_w
        dey                     ; width <= 40: Y is offset and counter
@col:   lda (ptr1),y
        and (ptr3),y
        ora (ptr2),y
        sta (ptr1),y
        dey
        bpl @col
        jsr ms_next_row
        bne @row
        rts

; --- _gen2_ms_save_run : framebuffer rect -> under buffer, 16 cyc/byte --------
_gen2_ms_save_run:
        jsr ms_setup_under
@row:   jsr ms_rowbase
        ldy ms_w
        dey
@col:   lda (ptr1),y
        sta (ptr4),y
        dey
        bpl @col
        jsr ms_next_under
        bne @row
        rts

; --- _gen2_ms_restore_run : under buffer -> framebuffer, 16 cyc/byte ----------
; Exact inverse of the save. Call with the SAME (x, y, spr, under) the
; save/draw used and the background is byte-identical.
_gen2_ms_restore_run:
        jsr ms_setup_under
        lda ms_w
        cmp #2
        beq restore2
        cmp #4
        beq restore4
@row:   jsr ms_rowbase
        ldy ms_w
        dey
@col:   lda (ptr4),y
        sta (ptr1),y
        dey
        bpl @col
        jsr ms_next_under
        bne @row
        rts

; Narrow rectangles dominate game sprites: unroll the clipped widths 2 and 4
; (ms_w picks the visible width; ms_stride still advances the pool rows).
.macro restore_rows count
@row:   jsr ms_rowbase
        ldy #0
        .repeat count, col
          .if col > 0
            iny
          .endif
          lda (ptr4),y
          sta (ptr1),y
        .endrepeat
        jsr ms_next_under
        bne @row
        rts
.endmacro
restore2:
        restore_rows 2
restore4:
        restore_rows 4

; --- _gen2_msu_run : save-under + masked draw, ONE pass -----------------------
; The engine's hot path: per byte the background is copied to the under buffer
; and replaced by (dst & mask) | data, 32 cycles:
;     lda (ptr1),y 5 / sta (ptr4),y 6 / and (ptr3),y 5 / ora (ptr2),y 5 /
;     sta (ptr1),y 6 / dey 2 / bpl 3
; (vs 16 + 26 for separate save + draw passes, plus a second setup).
_gen2_msu_run:
        jsr ms_setup
        lda ms_w
        cmp #2
        beq draw2
        cmp #4
        beq draw4
@row:   jsr ms_rowbase
        ldy ms_w
        dey
@col:   lda (ptr1),y
        sta (ptr4),y
        and (ptr3),y
        ora (ptr2),y
        sta (ptr1),y
        dey
        bpl @col
        jsr ms_next_row
        bne @row
        rts
.macro draw_rows count
@row:   jsr ms_rowbase
        ldy #0
        .repeat count, col
          .if col > 0
            iny
          .endif
          lda (ptr1),y
          sta (ptr4),y
          and (ptr3),y
          ora (ptr2),y
          sta (ptr1),y
        .endrepeat
        jsr ms_next_row
        bne @row
        rts
.endmacro
draw2:
        draw_rows 2
draw4:
        draw_rows 4
