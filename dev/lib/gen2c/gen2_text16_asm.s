; gen2_text16_asm.s — gen2c archive member: 16x16 (pixel-doubled) string
; renderer behind gen2_hgr_puts / gen2_hgr_puts_color / gen2_hgr_putu.
;
;   _gen2_puts_run         whole NUL-terminated string in one asm loop
;   _gen2_blit_glyph       one glyph, white, pixel-doubled to 16x16
;   _gen2_blit_glyph_color one glyph, already tinted (NTSC artifact colour)
;
; The pure-C version computed px/7 and px%7 (software division — cc65 has no
; DIV) for EVERY plotted pixel. Here the C wrapper does one division per
; STRING; the blitters advance the column/mask incrementally. Scanline bases
; come from the gen2_rowlo/gen2_rowhi tables (gen2_init.c).
;
; Geometry: each of the glyph's 8 source rows is drawn on TWO scanlines and each
; set source bit lights TWO horizontal pixels (pixel-doubling -> solid white,
; no NTSC colour fringe), giving 16x16 white cells. bit 0 of each glyph byte is
; the leftmost pixel (matches dev/lib/gen2/bbfont_cp437.inc).
;
; Arguments: the gen2_g_* / gen2_t_* block (gen2_text_params.s) and, for the
; colour blitter, the gen2_z_ce/co/hi carrier block (gen2_carrier_params.s).
; The caller guarantees the whole 16x16 cell is on-screen (the C wrapper clips).
;
; Zero page: none of its own. The colour blitter's per-glyph scratch lives in
; cc65's ptr3/ptr4/tmp1..tmp3 (it makes no nested call), aliased as in
; pom2games dev/lib/hgrc/hgr_text16_asm.s; the carrier for cell 2/3 is the one
; for cell 0/1 (parity repeats every two byte columns), so it needs no slot.
; Destroys A, X, Y, ptr1..ptr4, tmp1..tmp4. Non-reentrant.
; Split from the former monolithic gen2_blit.s.

        .export   _gen2_puts_run, _gen2_blit_glyph, _gen2_blit_glyph_color
        .import   _gen2_rowlo, _gen2_rowhi
        .import   gen2_plot_one, gen2_advance
        .importzp _gen2_g_glyph, _gen2_g_col, _gen2_g_mask, _gen2_g_y
        .importzp _gen2_t_col, _gen2_t_bit, _gen2_t_n, _gen2_t_color
        .importzp _gen2_t_s, _gen2_t_font
        .importzp _gen2_z_ce, _gen2_z_co, _gen2_z_hi
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

; white blitter pen (shared with gen2_pen_asm.s):
curcol  = tmp1               ; current byte column
curmask = tmp2               ; current bit mask
curbits = tmp3               ; remaining source bits of the current row
rowcnt  = tmp4               ; row counter 0..7
; colour blitter scratch (no nested calls -> cc65 scratch is free):
gr_or    = tmp1              ; byte to OR into the framebuffer (cell|hi)
gbc_bit  = tmp2              ; shift count derived from g_mask (0..6)
gbc_m0   = tmp3              ; row's shifted doubled mask, byte 0 (bits 0..7)
gbc_car0 = ptr3              ; carrier byte for cells at g_col + 0 and + 2
gbc_car1 = ptr3+1            ; carrier byte for cells at g_col + 1 and + 3
gbc_m1   = ptr4              ;   byte 1 (bits 8..15)
gbc_m2   = ptr4+1            ;   byte 2 (bits 16..23 — only bits 0..5 ever set)
; ptr1 = base of scanline yy ; ptr2 = base of scanline yy+1

        .segment "CODE"

; --- _gen2_blit_glyph : draw the 8x8 glyph, pixel-doubled to 16x16 -----------
_gen2_blit_glyph:
        lda #0
        sta rowcnt
@rowloop:
        ; ptr1 = gen2_row(y + 2*row), ptr2 = gen2_row(y + 2*row + 1)
        lda rowcnt
        asl a                ; row * 2
        clc
        adc _gen2_g_y        ; yy = y + 2*row
        tay
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        iny                  ; yy + 1
        lda _gen2_rowlo,y
        sta ptr2
        lda _gen2_rowhi,y
        sta ptr2+1
        ; load this row's 8 source bits
        ldy rowcnt
        lda (_gen2_g_glyph),y
        sta curbits
        ; reset pen to the glyph's left edge
        lda _gen2_g_col
        sta curcol
        lda _gen2_g_mask
        sta curmask
        ; walk 8 source bits, each lighting (or skipping) 2 doubled pixels
        ldx #8
@bitloop:
        lsr curbits          ; bit 0 (leftmost) -> carry
        bcc @skip
        jsr gen2_plot_one    ; lit: 2 doubled pixels
        jsr gen2_plot_one
        jmp @next
@skip:
        jsr gen2_advance     ; clear: skip 2 pixels
        jsr gen2_advance
@next:
        dex
        bne @bitloop
        ; next glyph row
        inc rowcnt
        lda rowcnt
        cmp #8
        bne @rowloop
        rts

; --- _gen2_blit_glyph_color : row-at-a-time tinted glyph blitter ------------
; Expand the 8-bit source row to a 16-bit doubled mask in two nibble LUT
; lookups (dbl4tbl), shift left by `bit` to align with the pen offset, then OR
; each of the 3-4 spanning bytes (one per 7-bit cell) into both scanlines —
; carrier and palette bit applied per BYTE, not per pixel. Reads the same
; gen2_g_glyph/col/mask/y + gen2_z_ce/co/hi parameter blocks as the white
; blitter; produces the NTSC artifact colour as the glyph is laid down (no
; white-then-recolorize pass). Cell carriers + the shift count are computed
; once per glyph (they depend on g_col parity and g_mask, both fixed across
; the 8 source rows).
_gen2_blit_glyph_color:
        ; Derive bit shift count from g_mask (= 1 << bit), bit in 0..6.
        ldx #0
        lda _gen2_g_mask
@bcnt:  lsr a
        beq @bcd
        inx
        bne @bcnt              ; bit <= 6, never wraps X
@bcd:   stx gbc_bit
        ; Carriers for cells 0..3. Carrier parity alternates from g_col:
        ; even col -> ce/co/ce/co ; odd col -> co/ce/co/ce.
        lda _gen2_g_col
        and #1
        bne @codd
        lda _gen2_z_ce
        sta gbc_car0
        lda _gen2_z_co
        sta gbc_car1
        jmp @cdn
@codd:  lda _gen2_z_co
        sta gbc_car0
        lda _gen2_z_ce
        sta gbc_car1
@cdn:   lda #0
        sta rowcnt
@crow:
        ; ptr1 = scanline yy ; ptr2 = scanline yy+1 (paired pixel-doubling).
        lda rowcnt
        asl a                  ; row * 2
        clc
        adc _gen2_g_y          ; yy = y + 2*row
        tay
        lda _gen2_rowlo,y
        sta ptr1
        lda _gen2_rowhi,y
        sta ptr1+1
        iny                    ; yy + 1
        lda _gen2_rowlo,y
        sta ptr2
        lda _gen2_rowhi,y
        sta ptr2+1
        ; Expand 8 source bits to 16 doubled bits via two nibble lookups.
        ldy rowcnt
        lda (_gen2_g_glyph),y
        pha
        and #$0F
        tay
        lda _gen2_dbl4tbl,y
        sta gbc_m0             ; doubled bits 0..7
        pla
        lsr a
        lsr a
        lsr a
        lsr a
        tay
        lda _gen2_dbl4tbl,y
        sta gbc_m1             ; doubled bits 8..15
        lda #0
        sta gbc_m2             ; doubled bits 16..23 (filled in by the shift)
        ; Shift (m2:m1:m0) left by `bit` positions; bit = 0 leaves them as-is.
        ; A 3-byte chain is enough: the original 16 bits occupy positions
        ; bit..bit+15 (<= 21), so m2 bits 6..7 stay zero and m3 isn't needed.
        ldy gbc_bit
        beq @nosh
@sh:    asl gbc_m0
        rol gbc_m1
        rol gbc_m2
        dey
        bne @sh
@nosh:
        ; --- Cell 0 (column g_col): doubled bits 0..6 = m0 & $7F ------------
        lda gbc_m0
        and #$7F
        beq @c0sk
        and gbc_car0
        ora _gen2_z_hi
        sta gr_or
        ldy _gen2_g_col
        ora (ptr1),y
        sta (ptr1),y
        lda gr_or
        ora (ptr2),y
        sta (ptr2),y
@c0sk:
        ; --- Cell 1 (column g_col+1): doubled bits 7..13 -------------------
        ; bits 7..13 = ((m1 << 1) | (m0 >> 7)) & $7F
        lda gbc_m0
        asl a                  ; CF = m0 bit 7 (= doubled bit 7)
        lda gbc_m1
        rol a                  ; A = (m1 << 1) | (m0 bit 7)
        and #$7F
        beq @c1sk
        and gbc_car1
        ora _gen2_z_hi
        sta gr_or
        ldy _gen2_g_col
        iny
        ora (ptr1),y
        sta (ptr1),y
        lda gr_or
        ora (ptr2),y
        sta (ptr2),y
@c1sk:
        ; --- Cell 2 (column g_col+2): doubled bits 14..20 ------------------
        ; bits 14..20 = ((m2 << 2) & $7C) | ((m1 >> 6) & $03)
        ; m2 bit 5 (= doubled bit 21) belongs to cell 3, so $7C drops it.
        lda gbc_m1
        lsr a
        lsr a
        lsr a
        lsr a
        lsr a
        lsr a                  ; A = m1 >> 6 (low 2 bits = m1 bits 6..7)
        sta gr_or              ; partial cell 2 in bits 0..1
        lda gbc_m2
        asl a
        asl a                  ; A = m2 << 2 (m2 bit 5 spilled to bit 7)
        and #$7C               ; keep bits 2..6 only
        ora gr_or
        beq @c2sk
        and gbc_car0           ; cell 2 has cell 0's column parity
        ora _gen2_z_hi
        sta gr_or
        ldy _gen2_g_col
        iny
        iny
        ora (ptr1),y
        sta (ptr1),y
        lda gr_or
        ora (ptr2),y
        sta (ptr2),y
@c2sk:
        ; --- Cell 3 (column g_col+3): doubled bit 21 only, set iff bit = 6 -
        ; (m2 bits 5..7, but bits 6..7 stay 0 by design)
        lda gbc_m2
        lsr a
        lsr a
        lsr a
        lsr a
        lsr a                  ; A = m2 >> 5 (low 3 bits, but bits 1..2 = 0)
        and #$07
        beq @c3sk
        and gbc_car1           ; cell 3 has cell 1's column parity
        ora _gen2_z_hi
        sta gr_or
        ldy _gen2_g_col
        iny
        iny
        iny
        ora (ptr1),y
        sta (ptr1),y
        lda gr_or
        ora (ptr2),y
        sta (ptr2),y
@c3sk:
        inc rowcnt
        lda rowcnt
        cmp #8
        beq @cdone
        jmp @crow
@cdone: rts

; 4-bit -> 8-bit doubled bit LUT: dbl4tbl[n] has each of n's bits 0..3
; replicated horizontally (bit k of n -> bits 2k and 2k+1 of the entry).
        .segment "RODATA"
_gen2_dbl4tbl:
        .byte $00, $03, $0C, $0F, $30, $33, $3C, $3F
        .byte $C0, $C3, $CC, $CF, $F0, $F3, $FC, $FF

        .segment "CODE"
; --- _gen2_puts_run : draw a whole NUL-terminated string in ONE asm loop -------
; Replaces the per-character C loop of gen2_hgr_puts / gen2_hgr_puts_color. Walks
; gen2_t_s, drawing at most gen2_t_n glyph cells (the C wrapper precomputes how
; many fit from x, so there is NO per-char 16-bit clip here). Per glyph: form the
; font address font+(c-$20)*8, set gen2_g_col/mask, then JSR the white or colour
; blitter — chosen ONCE per char via gen2_t_color (not per pixel: a per-pixel
; dispatch would cost more than keeping the two blitters separate saves). Then
; step the 18px pen (bit+=4, col+=2, +1 col on wrap). gen2_g_y set by the caller.
_gen2_puts_run:
@loop:
        lda _gen2_t_n
        beq @done
        ldy #0
        lda (_gen2_t_s),y       ; c = *s
        bne @go
@done:
        rts
@go:
        cmp #$20                ; clamp non-printable -> space
        bcc @space
        cmp #$80
        bcc @okc
@space:
        lda #$20
@okc:
        sec                     ; gen2_g_glyph = font + (c-$20)*8
        sbc #$20
        sta tmp1
        lda #0
        sta tmp2
        asl tmp1
        rol tmp2
        asl tmp1
        rol tmp2
        asl tmp1
        rol tmp2
        lda tmp1
        clc
        adc _gen2_t_font
        sta _gen2_g_glyph
        lda tmp2
        adc _gen2_t_font+1
        sta _gen2_g_glyph+1
        lda _gen2_t_col
        sta _gen2_g_col
        ldx _gen2_t_bit         ; gen2_g_mask = 1 << bit
        lda #1
@mk:
        dex
        bmi @mkd
        asl a
        jmp @mk
@mkd:
        sta _gen2_g_mask
        lda _gen2_t_color       ; draw white or colour
        beq @white
        jsr _gen2_blit_glyph_color
        jmp @adv
@white:
        jsr _gen2_blit_glyph
@adv:
        lda _gen2_t_bit         ; pen: bit += 4 (+ carry into col on wrap)
        clc
        adc #4
        cmp #7
        bcc @nowrap
        sbc #7                  ; carry set by cmp (>=7): A = bit+4-7
        sta _gen2_t_bit
        lda _gen2_t_col
        clc
        adc #3                  ; col += 2, +1 for the bit wrap
        sta _gen2_t_col
        jmp @adv2
@nowrap:
        sta _gen2_t_bit
        lda _gen2_t_col
        clc
        adc #2
        sta _gen2_t_col
@adv2:
        inc _gen2_t_s           ; ++s
        bne @sok
        inc _gen2_t_s+1
@sok:
        dec _gen2_t_n
        jmp @loop
