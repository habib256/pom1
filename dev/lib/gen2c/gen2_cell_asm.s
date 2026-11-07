; gen2_cell_asm.s — gen2c archive member: _gen2_cell_asm, the 8x8-grid cell
; blitter behind gen2_hgr_cell (gen2_cell.c): fill/erase a 6x6 block.
;
; The C wrapper stores cx/cy/set as bytes. Derive the pixel rect (cx*8 ..
; cx*8+5, y=cy*8, 6 rows) directly with shifts — no cc65 aslax3 16-bit helper —
; and JMP into _gen2_pixrect_asm (gen2_pixrect_asm.s). No clip: grid cells are
; always on-screen (cx<=34 -> xr<=277 ; cy<=23 -> y0+5<=189). gen2_hgr_cell()
; builds the tables before calling, so the col7/mask7/row LUTs are ready.
;
; No zero page of its own: gen2_c_cx / cy / set ALIAS the pixrect slots each is
; converted into (cx -> r_x, cy -> r_y0, set -> r_mode). Every alias is read
; before its slot is overwritten, and the C wrapper writes nothing else in
; between. Split from the former monolithic gen2_blit.s (which fell through).

        .export   _gen2_cell_asm
        .exportzp _gen2_c_cx, _gen2_c_cy, _gen2_c_set
        .import   _gen2_pixrect_asm
        .importzp _gen2_r_x, _gen2_r_xr, _gen2_r_y0, _gen2_r_rows, _gen2_r_mode

_gen2_c_cx  = _gen2_r_x      ; cell column (px = cx*8)
_gen2_c_cy  = _gen2_r_y0     ; cell row    (py = cy*8)
_gen2_c_set = _gen2_r_mode   ; 1 = fill, 0 = clear

        .segment "CODE"

_gen2_cell_asm:
        lda     #0           ; r_x = cx (already in place, low byte) -> *8
        sta     _gen2_r_x+1
        asl     _gen2_r_x
        rol     _gen2_r_x+1
        asl     _gen2_r_x
        rol     _gen2_r_x+1
        asl     _gen2_r_x
        rol     _gen2_r_x+1          ; gen2_r_x = cx*8
        clc
        lda     _gen2_r_x
        adc     #5
        sta     _gen2_r_xr
        lda     _gen2_r_x+1
        adc     #0
        sta     _gen2_r_xr+1         ; gen2_r_xr = gen2_r_x + 5
        lda     _gen2_c_cy           ; == r_y0
        asl     a
        asl     a
        asl     a
        sta     _gen2_r_y0           ; gen2_r_y0 = cy*8
        lda     #6
        sta     _gen2_r_rows         ; 6-row block
        ; r_mode already holds c_set
        jmp     _gen2_pixrect_asm
