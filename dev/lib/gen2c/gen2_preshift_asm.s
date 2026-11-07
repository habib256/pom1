; gen2_preshift_asm.s — gen2c archive member: the pre-shift XOR kernels behind
; gen2_hgr_sprite (GEN2_XOR) and gen2_hgr_sprite_xor.
;
;   _gen2_xs_run           100% asm entry for gen2_hgr_sprite_xor: derives the
;                          byte column, phase offset and clip, then falls into
;   _gen2_preshift_xor_run dedicated XOR row loop (no per-byte mode test).
;
; Arguments: gen2_xs_x/y/spr (owned here, 5 bytes of zero page) for _xs_run;
; the gen2_b_* block (gen2_blit_params.s) for the row loop. Destroys A, X, Y,
; ptr1, ptr2, tmp1..tmp3. Split from the former monolithic gen2_blit.s.

        .export   _gen2_xs_run, _gen2_preshift_xor_run
        .exportzp _gen2_xs_x, _gen2_xs_y, _gen2_xs_spr
        .import   _gen2_rowlo, _gen2_rowhi, _gen2_col7, _gen2_phase7
        .importzp _gen2_b_col, _gen2_b_w, _gen2_b_h
        .importzp _gen2_b_stride, _gen2_b_y, _gen2_b_src
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3

        .segment "ZEROPAGE"
; gen2_hgr_sprite_xor parameter block — the thin C entry only stores these;
; _gen2_xs_run does ALL the work (tables, phase offset, clip) in asm, so the
; cc65 wrapper cost (16-bit divide/multiply/clip) is gone.
_gen2_xs_x:    .res 2        ; pixel x (0..279)
_gen2_xs_y:    .res 1        ; pixel y (0..191)
_gen2_xs_spr:  .res 2        ; pointer to the gen2_sprite_t {bits, stride, h}

        .segment "CODE"

; --- _gen2_xs_run : 100% asm entry for gen2_hgr_sprite_xor ----------------------
; Everything the cc65 wrapper used to do with a software divide (x/7, x%7), a
; software multiply (phase*h*stride) and 16-bit clip ternaries is done here,
; then we fall straight through into the XOR row loop. This keeps a sprite draw
; to a few hundred cycles so a single-buffer erase+redraw pair fits inside
; V-blank. XOR mode only; clips to the right/bottom edges.
_gen2_xs_run:
        ; col = gen2_col7[x] -> _gen2_b_col ; phase = gen2_phase7[x] -> tmp1
        lda _gen2_xs_x+1
        bne @xhi
        ldx _gen2_xs_x
        lda _gen2_col7,x
        ldy _gen2_phase7,x
        jmp @xset
@xhi:
        ldx _gen2_xs_x
        lda _gen2_col7+256,x
        ldy _gen2_phase7+256,x
@xset:
        sta _gen2_b_col
        sty tmp1                  ; tmp1 = phase (0..6)
        ; deref spr -> ptr2 ; read bits/stride/h
        lda _gen2_xs_spr
        sta ptr2
        lda _gen2_xs_spr+1
        sta ptr2+1
        ldy #0
        lda (ptr2),y              ; bits low
        sta _gen2_b_src
        iny
        lda (ptr2),y              ; bits high
        sta _gen2_b_src+1
        iny
        lda (ptr2),y              ; stride
        sta _gen2_b_stride
        sta _gen2_b_w             ; default w = stride
        sta tmp2                  ; tmp2 = stride
        iny
        lda (ptr2),y              ; h
        sta _gen2_b_h             ; default h
        sta tmp3                  ; tmp3 = full h (for the phase offset)
        lda _gen2_xs_y
        sta _gen2_b_y
        ; right clip: col + stride > 40 ? w = 40 - col
        lda _gen2_b_col
        clc
        adc tmp2
        cmp #41
        bcc @wok
        lda #40
        sec
        sbc _gen2_b_col
        sta _gen2_b_w
@wok:
        ; bottom clip: y + h > 192 ? h = 192 - y
        lda _gen2_xs_y
        clc
        adc tmp3
        bcs @clipb                ; y+h >= 256 -> clip
        cmp #193
        bcc @hok
@clipb:
        lda #192
        sec
        sbc _gen2_xs_y
        sta _gen2_b_h
@hok:
        ; src += phase * (h * stride). phaseblock = h*stride via loop-add -> ptr1
        lda #0
        sta ptr1
        sta ptr1+1
        ldx tmp3                  ; full h
@hloop:
        clc
        lda ptr1
        adc tmp2                  ; + stride
        sta ptr1
        bcc @h2
        inc ptr1+1
@h2:
        dex
        bne @hloop
        ldx tmp1                  ; phase
        beq @offdone
@ploop:
        clc
        lda _gen2_b_src
        adc ptr1
        sta _gen2_b_src
        lda _gen2_b_src+1
        adc ptr1+1
        sta _gen2_b_src+1
        dex
        bne @ploop
@offdone:
        ; fall through into the XOR row loop below.

; --- _gen2_preshift_xor_run : dedicated XOR blit for the pre-shift engine -------
; Same as _gen2_blit7_run but mode is hardwired to XOR, so there is no per-byte
; mode dispatch: the inner loop is the Buzzard-Bait minimum -- lda (src),y /
; eor (dst),y / sta (dst),y. gen2_hgr_sprite() calls this for GEN2_XOR (the hot
; animation path); SET/CLEAR still go through _gen2_blit7_run.
_gen2_preshift_xor_run:
@row:
        ldy _gen2_b_y           ; ptr1 = rowbase(y) + col
        lda _gen2_rowlo,y
        clc
        adc _gen2_b_col
        sta ptr1
        lda _gen2_rowhi,y
        adc #0
        sta ptr1+1
        ldy #0                  ; Y indexes src[j] AND dest[col+j] together
@col:
        lda (_gen2_b_src),y     ; pre-shifted source byte
        eor (ptr1),y            ; dest ^= src  (no mode test)
        sta (ptr1),y
        iny
        cpy _gen2_b_w
        bne @col
        clc                     ; src += stride ; y += 1 ; rows--
        lda _gen2_b_src
        adc _gen2_b_stride
        sta _gen2_b_src
        bcc @nyc
        inc _gen2_b_src+1
@nyc:
        inc _gen2_b_y
        dec _gen2_b_h
        bne @row
        rts
