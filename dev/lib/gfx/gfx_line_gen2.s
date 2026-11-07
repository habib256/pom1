; gfx_line_gen2.s — GEN2 HGR backend of gfx_line (replaces gfx_line.c in
; gfx-gen2.lib; the TMS9918 archive keeps the portable C).
;
; void gfx_line(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1);
; cc65 fastcall: A = y1, C stack = x1lo, x1hi, y0, x0lo, x0hi.
;
; Same contract as gfx_line.c, pixel for pixel:
;   - y0 == y1          -> gen2_hgr_hline(x0, x1, y0)  (clips, any order)
;   - x0 == x1          -> gen2_hgr_vline(x0, y0, y1)  (clips, any order)
;   - otherwise, any endpoint off-screen (x >= 280, y >= 192) draws nothing;
;     else the Bresenham walk below, with gfx_line.c's ties.
; The axis cases reuse the caller's frame: the arguments are shuffled into the
; callee's layout and the callee is tail-called, so it pops them.
;
; Ported from pom2games dev/lib/hgrc/hgr_line_asm.s + gfx/gfx_line_hgr.s (same
; author, GPL-3.0; the Apple II HGR framebuffer is the GEN2 one byte for byte).
; It reads POM1's own gen2c tables (gen2_col7/gen2_mask7 for x, the
; gen2_rowlo/gen2_rowhi scanline bases of the CURRENT draw page), built by
; gen2_build_tables(). The C version walked gfx_plot -> gen2_hgr_plot per
; pixel: a C call, a clip, a table-build check and a table lookup each.
;
; Ties. gfx_line.c steps x when 2*err > -dy and y when 2*err < dx, both tests on
; the same err. Along the major axis the first test always holds when dx >= dy,
; the second when dy > dx, so only the minor step is decided, by the sign of
; D = 2*minor - major - 1, += 2*minor per pixel and -= 2*major per minor step.
; The pixel moves incrementally: a mask shift, the row reloaded on y steps only.
; Pixel identity with gfx_line.c is pinned by dev/lib/test/micro/t18_c_line.c.
;
; OR drawing: bit 7 (palette) and the screen holes are preserved. Main RAM,
; non-reentrant, no self-modification (ROM-safe). Destroys A/X/Y, ptr1-ptr4,
; tmp1-tmp4 (cc65 runtime scratch: no new zero-page byte).

        .export   _gfx_line
        .import   _gen2_hgr_hline, _gen2_hgr_vline, _gen2_build_tables
        .import   _gen2_col7, _gen2_mask7, _gen2_rowlo, _gen2_rowhi
        .import   incsp5
        .importzp sp, ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

mask    = tmp1
endcol  = tmp2                  ; x-major: last pixel's column and mask
endmask = tmp3
sy      = tmp4                  ; bit 7: y decreases
d       = ptr2
flat    = ptr3                  ; D increment, major step only
minor   = ptr4                  ; D increment, major and minor steps

        .bss
lx0:    .res 2
lx1:    .res 2
ly0:    .res 1
ly1:    .res 1
dx:     .res 2
sx:     .res 1                  ; bit 7: x decreases

        .code
_gfx_line:
        sta ly1
        ldy #2
        cmp (sp),y              ; y1 == y0 ?
        beq horizontal
        ldy #0
        lda (sp),y
        ldy #3
        cmp (sp),y              ; x1lo == x0lo ?
        bne diagonal
        ldy #1
        lda (sp),y
        ldy #4
        cmp (sp),y              ; x1hi == x0hi ?
        beq vertical
diagonal:
        lda ly1                 ; reject any off-screen endpoint
        cmp #192
        bcs reject
        ldy #2
        lda (sp),y
        cmp #192
        bcs reject
        sta ly0
        ldy #3
        lda (sp),y
        sta lx0
        iny
        lda (sp),y
        sta lx0+1
        beq @x0ok
        lsr a                   ; hi must be 1 (A=1 -> 0, C=1)
        bne reject
        lda lx0
        cmp #24                 ; 256 + 24 = 280
        bcs reject
@x0ok:  ldy #0
        lda (sp),y
        sta lx1
        iny
        lda (sp),y
        sta lx1+1
        beq @x1ok
        lsr a
        bne reject
        lda lx1
        cmp #24
        bcs reject
@x1ok:  jsr incsp5
        jsr _gen2_build_tables  ; idempotent; a C call, so scratch after it
        jmp line_core
reject: jmp incsp5

horizontal:
        ; gen2_hgr_hline(x0, x1, y): A = y, stack = x1lo, x1hi, x0lo, x0hi.
        ; Move x1 up one slot over y0, then drop the first slot.
        ldy #1
        lda (sp),y
        iny
        sta (sp),y
        ldy #0
        lda (sp),y
        iny
        sta (sp),y
        inc sp
        bne :+
        inc sp+1
:       lda ly1
        jmp _gen2_hgr_hline

vertical:
        ; gen2_hgr_vline(x, y0, y1): A = y1, stack = y0, xlo, xhi — drop x1.
        clc
        lda sp
        adc #2
        sta sp
        bcc :+
        inc sp+1
:       lda ly1
        jmp _gen2_hgr_vline

; --- the Bresenham walk, endpoints validated, tables built ------------------
line_core:
        lda #0
        sta sx
        sta sy
        sec
        lda lx1
        sbc lx0
        sta dx
        lda lx1+1
        sbc lx0+1
        sta dx+1
        bcs @dx_done
        dec sx
        lda #0
        sec
        sbc dx
        sta dx
        lda #0
        sbc dx+1
        sta dx+1
@dx_done:
        sec
        lda ly1
        sbc ly0
        bcs @dy_done
        dec sy
        eor #$FF
        adc #1                  ; C=0 here
@dy_done:
        sta tmp2                ; dy, 0..191
        ; Start pixel and row.
        ldx lx0
        lda lx0+1
        beq @start_low
        lda _gen2_mask7+256,x
        sta mask
        ldy _gen2_col7+256,x
        jmp @start_row
@start_low:
        lda _gen2_mask7,x
        sta mask
        ldy _gen2_col7,x
@start_row:
        ldx ly0
        lda _gen2_rowlo,x
        sta ptr1
        lda _gen2_rowhi,x
        sta ptr1+1
        lda dx+1
        bne @x_major
        lda dx
        cmp tmp2
        bcs @x_major
        jmp y_major
@x_major:
        ; flat = 2*dy, minor = 2*dy - 2*dx, D = 2*dy - dx - 1.
        lda tmp2
        asl
        sta flat
        lda #0
        rol
        sta flat+1
        lda dx
        asl
        sta minor
        lda dx+1
        rol
        sta minor+1
        sec
        lda flat
        sbc minor
        sta minor
        lda flat+1
        sbc minor+1
        sta minor+1
        clc                     ; D = flat - dx - 1
        lda flat
        sbc dx
        sta d
        lda flat+1
        sbc dx+1
        sta d+1
        ; Last pixel.
        stx tmp2                ; (dy no longer needed) y0 back in X below
        ldx lx1
        lda lx1+1
        beq @end_low
        lda _gen2_mask7+256,x
        sta endmask
        lda _gen2_col7+256,x
        jmp @end_col
@end_low:
        lda _gen2_mask7,x
        sta endmask
        lda _gen2_col7,x
@end_col:
        ldx tmp2
        sta endcol
        bit sx
        bpl x_right
        jmp x_left

; x-major, increasing x. X = row, Y = column.
x_right:
        lda mask
        ora (ptr1),y
        sta (ptr1),y
        cpy endcol
        beq @maybe_end
@next:  lda d+1
        bmi @flat
        clc
        lda d
        adc minor
        sta d
        lda d+1
        adc minor+1
        sta d+1
        bit sy
        bmi @up
        inx
        bne @row                ; always (row < 192)
@up:    dex
@row:   lda _gen2_rowlo,x
        sta ptr1
        lda _gen2_rowhi,x
        sta ptr1+1
        jmp @step
@flat:  clc
        lda d
        adc flat
        sta d
        lda d+1
        adc flat+1
        sta d+1
@step:  asl mask
        bpl x_right
        lda #1
        sta mask
        iny
        bne x_right             ; always (column < 40)
@maybe_end:
        lda mask
        cmp endmask
        bne @next
        rts

; x-major, decreasing x.
x_left:
        lda mask
        ora (ptr1),y
        sta (ptr1),y
        cpy endcol
        beq @maybe_end
@next:  lda d+1
        bmi @flat
        clc
        lda d
        adc minor
        sta d
        lda d+1
        adc minor+1
        sta d+1
        bit sy
        bmi @up
        inx
        bne @row
@up:    dex
@row:   lda _gen2_rowlo,x
        sta ptr1
        lda _gen2_rowhi,x
        sta ptr1+1
        jmp @step
@flat:  clc
        lda d
        adc flat
        sta d
        lda d+1
        adc flat+1
        sta d+1
@step:  lsr mask
        bne x_left
        lda #$40
        sta mask
        dey
        jmp x_left
@maybe_end:
        lda mask
        cmp endmask
        bne @next
        rts

; y-major (dy > dx, so dx < 192): flat = 2*dx, minor = 2*dx - 2*dy,
; D = 2*dx - dy - 1. X = row, Y = column.
y_major:
        lda dx
        asl
        sta flat
        lda #0
        rol
        sta flat+1
        lda tmp2
        asl
        sta minor
        lda #0
        rol
        sta minor+1
        sec
        lda flat
        sbc minor
        sta minor
        lda flat+1
        sbc minor+1
        sta minor+1
        clc
        lda flat
        sbc tmp2
        sta d
        lda flat+1
        sbc #0
        sta d+1
@pixel: lda mask
        ora (ptr1),y
        sta (ptr1),y
        cpx ly1
        beq @done
        lda d+1
        bmi @flat
        clc
        lda d
        adc minor
        sta d
        lda d+1
        adc minor+1
        sta d+1
        bit sx
        bmi @left
        asl mask
        bpl @y
        lda #1
        sta mask
        iny
        bne @y                  ; always
@left:  lsr mask
        bne @y
        lda #$40
        sta mask
        dey
        jmp @y
@flat:  clc
        lda d
        adc flat
        sta d
        lda d+1
        adc flat+1
        sta d+1
@y:     bit sy
        bmi @up
        inx
        bne @row                ; always
@up:    dex
@row:   lda _gen2_rowlo,x
        sta ptr1
        lda _gen2_rowhi,x
        sta ptr1+1
        jmp @pixel
@done:  rts
