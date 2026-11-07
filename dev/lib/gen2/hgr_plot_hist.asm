; ============================================================================
; hgr_plot_hist.asm -- monochrome HGR lines that remember what they lit (GEN2)
; ============================================================================
; Moving thin shapes over a fixed backdrop (a floor, a hill, a grid): each
; pixel plotted sets only the bits of its mask that were dark and records
; (address, those bits) in the page's history; pl_erase gives them back
; (EOR), so the backdrop under a shape comes back exactly as it was and
; nothing is redrawn. Two pages (double buffering: draw the hidden one).
; Bits 0-6 only: the palette bit is never touched.
;
;   pl_page      A = 0 / 1: the page drawn from now on (history pointers,
;                pl_pg = $00 / $60 EORed into hgr_hi). Clobbers A, X.
;   pl_erase     give back what the current page's history lit, empty it.
;   pl_line      pl_x0, pl_y0 -> pl_x1, pl_y1 (x 0..255, y 0..191; both
;                ends drawn, Bresenham on the dominant axis).
;   pl_plot      pl_x, pl_y.
;   pl_n         (zero page, 2 B) entries recorded per page: ZERO BOTH at
;                start (and after clearing a page by other means).
;
; Needs hgr_lo / hgr_hi with PAGE-1 values (hgr_scanline.inc; do not let
; hgr_flip.asm rewrite them -- pl_page does its own page selection) and
; hgr_div7 / hgr_mod7 (hgr_div7_ram.asm, or 256-byte tables of those names).
; PL_HIST = the address of a page-aligned 1.5 KB area (6 pages of 256 B:
; per page, one page of address low bytes, one of high bytes, one of bits),
; defined by the program.
; Zero page: 23 B (pl_x0 .. pl_cur, pl_n), reserved at the first include;
; pl_n is skipped if defined before (an absolute 2-byte alias is fine).
; Clobbers A, X, Y; not reentrant (no IRQ use). SELF-MODIFYING (patched
; operands in pl_line, and everywhere in the FAST variant): RAM only, never
; in a CodeTank / Juke-Box cartridge running from ROM.
;
; Plain variant (default), measured on a scratch 6502 core, JSR/RTS in:
;   pl_line ~170 cycles a pixel (166 for a 60 x 5 line, 172 on the
;   diagonal); pl_erase ~43 cycles an entry; up to 255 PIXELS a page
;   (pl_plot then draws nothing). Code 280 B for the four routines
;   (pl_bits included).
;
; HGR_PLOT_HIST_FAST (define it before the FIRST include) -- the same lit
; pixels, faster, and two more routines:
;   pl_line      steps a byte column and a bit mask, no row / div7 / mod7
;                lookup a pixel: x major (dx > dy) keeps the run of bits
;                lit in the current byte and writes it once, as ONE
;                history entry, when the line leaves the byte; y major
;                writes each pixel through patched absolute operands. The
;                history then counts BYTES, not pixels: 255 entries a page;
;                past them the x-major loops stop, the y-major loop draws
;                at most 255 - pl_n pixels.
;   pl_erase     count down, history read absolute,Y.
;   pl_erase_to  A = a mark (pl_n,X taken before drawing an object): gives
;                back only the entries above it, newest first -- a page's
;                history as a stack of objects, the still ones left drawn.
;                That is what took the pom2games CATAPULT from ~38 200 to
;                ~3 700 cycles an idle frame: only the objects that moved
;                (and those drawn after them) are erased and redrawn.
;   pl_sprite    a preshifted sprite, its top left pixel at pl_x, pl_y,
;                OR'ed in, the bits it lit recorded a byte at a time.
;                pl_sp (zero page, +2 B) -> .byte W (bytes a row), H (rows)
;                then .addr shift 0 .. shift 6, each H rows of W bytes
;                (bit 0 = the leftmost pixel). Rows outside PL_SPRITE_YMIN
;                (default 0) .. 191 are skipped; columns are not clipped
;                (x / 7 + W <= 40).
; Measured, same core: pl_line 53 cycles a pixel on a 60 x 5 line, 115 on
; 60 x 40, 106 on the diagonal, 91 steep (60 x 5 wide); pl_erase 38 an
; entry; pl_sprite ~2 100 cycles for a 9-row, 3-byte sprite. Code 936 B
; with all five routines.
; The fast routines patch their history and page operands themselves when
; the page changed since their last call (pl_page is unchanged).
;
; Placement: the zero page is reserved where the file is FIRST included, so
; include it before the callers. The routines are .ifref: assembled at the
; first include where they are referenced and not yet defined (pl_plot also
; with the plain pl_line). Either HGR_PLOT_HIST_ALL = 1 before one include
; (all four plain routines there, wherever execution does not fall into
; them), or include the file early for the zero page and a second time
; after the last caller.
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_plot_hist.asm,
; used by its CATAPULT, ROPE, LANDER and HALFPIPE examples). The pixel
; logic is unchanged; POM1 keeps pl_n in zero page (its GEN2 cfgs have no
; BSS segment) and leaves the caller's segment alone.
; ============================================================================

.ifndef _HGR_PLOT_HIST_LOADED_
_HGR_PLOT_HIST_LOADED_ = 1

.pushseg
.zeropage
pl_x0:  .res 1
pl_y0:  .res 1
pl_x1:  .res 1
pl_y1:  .res 1
pl_x:   .res 1
pl_y:   .res 1
pl_dx:  .res 1
pl_dy:  .res 1
pl_err: .res 1
pl_cnt: .res 1
pl_m:   .res 1
pl_pg:  .res 1                          ; $00 or $60: hgr_hi EOR it = page 2
pl_ptr: .res 2
pl_hl:  .res 2                          ; the page's history: addresses lo,
pl_hh:  .res 2                          ; hi, bits
pl_hb:  .res 2
pl_cur: .res 1                          ; the page (0 / 1)
.ifdef HGR_PLOT_HIST_FAST
pl_sp:  .res 2                          ; pl_sprite: -> its descriptor
.endif

.ifndef pl_n
pl_n:   .res 2                          ; entries recorded, per page: ZERO
.endif                                  ; BOTH before the first draw
.popseg

.endif  ; _HGR_PLOT_HIST_LOADED_

.if (.referenced(pl_page) .or .defined(HGR_PLOT_HIST_ALL)) .and (.not .defined(pl_page))
pl_page:
        STA     pl_cur
        TAX
        LDA     #0
        STA     pl_hl
        STA     pl_hh
        STA     pl_hb
        TXA
        BEQ     :+
        LDA     #$60
:       STA     pl_pg
        TXA                             ; history: PL_HIST + 3 pages * page
        ASL     A
        ADC     pl_cur                  ; (C = 0)
        ADC     #>PL_HIST
        STA     pl_hl+1
        ADC     #1
        STA     pl_hh+1
        ADC     #1
        STA     pl_hb+1
        RTS
.endif

.if (.referenced(pl_erase) .or .referenced(pl_erase_to) .or .defined(HGR_PLOT_HIST_ALL)) .and (.not .defined(pl_erase))
.ifdef HGR_PLOT_HIST_FAST
; pl_erase_to: A = entries kept (a mark taken from pl_n before drawing):
; the entries above it are given back, newest first. The history operands
; (absolute,Y) follow the page lazily: patched when pl_cur changed.
pl_erase:
        LDA     #0
pl_erase_to:
        STA     pl_cnt
        LDA     pl_cur
pl_e_pg: CMP    #$FF                    ; the page the operands are set for
        BEQ     pl_e_go
        STA     pl_e_pg+1
        LDA     pl_hl+1
        STA     pl_e_hl+2
        LDA     pl_hh+1
        STA     pl_e_hh+2
        LDA     pl_hb+1
        STA     pl_e_hb+2
pl_e_go: LDX    pl_cur
        LDY     pl_n,X
        LDA     pl_cnt
        STA     pl_n,X
        CPY     pl_cnt
        BEQ     pl_e_rts
        BCC     pl_e_rts
        LDX     #0
pl_e_lp: DEY                            ; ~38 cycles an entry
pl_e_hl: LDA    $FF00,Y
        STA     pl_ptr
pl_e_hh: LDA    $FF00,Y
        STA     pl_ptr+1
pl_e_hb: LDA    $FF00,Y
        EOR     (pl_ptr,X)
        STA     (pl_ptr,X)
        CPY     pl_cnt
        BNE     pl_e_lp
pl_e_rts: RTS
.else
pl_erase:
        LDX     pl_cur
        LDA     pl_n,X
        BEQ     @rts
        STA     pl_cnt
        LDA     #0
        STA     pl_n,X
        LDY     #0
@p:     LDA     (pl_hl),Y
        STA     pl_ptr
        LDA     (pl_hh),Y
        STA     pl_ptr+1
        LDA     (pl_hb),Y
        LDX     #0
        EOR     (pl_ptr,X)
        STA     (pl_ptr,X)
        INY
        CPY     pl_cnt
        BNE     @p
@rts:   RTS
.endif  ; HGR_PLOT_HIST_FAST
.endif

.if (.referenced(pl_plot) .or (.referenced(pl_line) .and (.not .defined(HGR_PLOT_HIST_FAST))) .or .defined(HGR_PLOT_HIST_ALL)) .and (.not .defined(pl_plot))
; one pixel at pl_x, pl_y: light the dark bits of its mask, record them
pl_plot:
        LDX     pl_cur
        LDA     pl_n,X
        CMP     #255
        BEQ     @rts                    ; history full: nothing drawn
        LDY     pl_y
        LDA     hgr_lo,Y
        STA     pl_ptr
        LDA     hgr_hi,Y
        EOR     pl_pg
        STA     pl_ptr+1
        LDX     pl_x
        LDA     hgr_mod7,X
        LDY     hgr_div7,X
        TAX
        LDA     (pl_ptr),Y
        EOR     #$FF
        AND     pl_bits,X
        BEQ     @rts                    ; already lit
        STA     pl_m
        ORA     (pl_ptr),Y
        STA     (pl_ptr),Y
        TYA                             ; the byte's address (no carry: a row
        CLC                             ; starts at $x00, $x28, $x50, ..)
        ADC     pl_ptr
        LDX     pl_cur
        LDY     pl_n,X
        STA     (pl_hl),Y
        LDA     pl_ptr+1
        STA     (pl_hh),Y
        LDA     pl_m
        STA     (pl_hb),Y
        INC     pl_n,X
@rts:   RTS
.endif

.if (.referenced(pl_line) .or .defined(HGR_PLOT_HIST_ALL)) .and (.not .defined(pl_line))
.ifdef HGR_PLOT_HIST_FAST
; The fast line: the same pixels as the per-pixel pl_line below, drawn by
; stepping a column and a bit mask (no row, div7 or mod7 per pixel) and
; recorded a byte at a time. Registers in the loops: X = the row, Y = the
; history index (pl_n), A = the error (x major). The x-major loops keep the
; run of bits lit in the current byte (pl_y = its first bits' mask) and
; write it once, when the line leaves the byte (pl_fl); the y-major loop
; writes each pixel (a new row each step). The screen and history operands
; are absolute and patched: the row's address per byte or pixel, the
; history pages and the page EOR when pl_cur changed. Full history: the
; x-major loops stop at 255 entries, the y-major loop draws at most
; 255 - pl_n pixels. pl_x0..pl_y1 are kept; pl_x = the column, pl_y, pl_m,
; pl_err, pl_cnt, pl_dx, pl_dy, pl_ptr scratch.
pl_line:
        LDA     pl_cur
pl_l_pg: CMP    #$FF                    ; the page the operands are set for
        BEQ     pl_l_go
        STA     pl_l_pg+1
        LDA     pl_pg
        STA     pl_l_ype+1
        STA     pl_f_pe+1
        LDA     pl_hl+1
        STA     pl_l_yhl+2
        STA     pl_f_hl+2
        LDA     pl_hh+1
        STA     pl_l_yhh+2
        STA     pl_f_hh+2
        LDA     pl_hb+1
        STA     pl_l_yhb+2
        STA     pl_f_hb+2
pl_l_go:
        LDX     pl_x0                   ; the first pixel: column, mask
        LDA     hgr_div7,X
        STA     pl_x
        LDY     hgr_mod7,X
        LDA     pl_bits,Y
        STA     pl_m
        SEC                             ; dy = |y1 - y0|, the row step
        LDA     pl_y1
        SBC     pl_y0
        LDX     #$E8                    ; INX
        BCS     :+
        EOR     #$FF
        ADC     #1
        LDX     #$CA                    ; DEX
:       STA     pl_dy
        STX     pl_l_ys
        STX     pl_l_xiyy
        STX     pl_l_xdyy
        SEC                             ; dx = |x1 - x0|
        LDA     pl_x1
        SBC     pl_x0
        BCC     :+
        STA     pl_dx
        CMP     pl_dy                   ; dy >= dx: y major
        BCC     pl_l_ymaj_i
        BEQ     pl_l_ymaj_i
        JMP     pl_l_xinc
:       EOR     #$FF
        ADC     #1
        STA     pl_dx
        CMP     pl_dy
        BCC     pl_l_ymaj_d
        BEQ     pl_l_ymaj_d
        JMP     pl_l_xdec

; ---- y major (dy >= dx): a pixel a row --------------------------------
pl_l_ymaj_d:
        LDA     #$46                    ; LSR zp
        LDX     #$F0                    ; BEQ
        LDY     #$C6                    ; DEC zp
        STY     pl_l_yxc
        LDY     #$40
        BNE     pl_l_ym
pl_l_ymaj_i:
        LDA     #$06                    ; ASL zp
        LDX     #$30                    ; BMI
        LDY     #$E6                    ; INC zp
        STY     pl_l_yxc
        LDY     #$01
pl_l_ym:
        STA     pl_l_yxs
        STX     pl_l_yxb
        STY     pl_l_yxm+1
        LDA     pl_dx
        STA     pl_l_yadc+1
        LDA     pl_dy
        STA     pl_l_ycmp+1
        STA     pl_l_ysbc+1
        LSR     A
        STA     pl_err
        LDX     pl_cur                  ; pixels: dy + 1, at most 255 - pl_n
        LDY     pl_n,X
        TYA
        EOR     #$FF                    ; 255 - pl_n
        BNE     :+
        RTS                             ; (full)
:       CMP     pl_dy
        BCC     :+
        BEQ     :+
        LDA     pl_dy
        CLC
        ADC     #1
:       SEC                             ; the last row: y0 +- (pixels - 1)
        SBC     #1
        LDX     pl_l_ys                 ; (CPX: C = 1 going up, the loop
        CPX     #$E8                    ; then enters at a CLC)
        BNE     :+
        CLC
        ADC     pl_y0
        LDX     #<(pl_l_yl - (pl_l_ybne + 2))
        BCC     :++                     ; (always: y0 + n <= 191)
:       EOR     #$FF
        SEC
        ADC     pl_y0
        LDX     #<(pl_l_ylc - (pl_l_ybne + 2))
:       STA     pl_l_yend+1
        STX     pl_l_ybne+1
        LDX     pl_y0
        CLC
        BCC     pl_l_yplot
pl_l_ylc: CLC
pl_l_yl:
pl_l_ys: INX                            ; (INX or DEX, set above)
        LDA     pl_err
pl_l_yadc: ADC  #0                      ; + dx (C = 0)
        BCS     pl_l_yx
pl_l_ycmp: CMP  #0                      ; dy
        BCS     pl_l_yx
        STA     pl_err
pl_l_yplot:                             ; C = 0
        LDA     hgr_lo,X
        ADC     pl_x                    ; (no carry: a row starts at $x00,
        STA     pl_l_yr+1               ; $x28, .. $xD0)
        STA     pl_l_yw+1
pl_l_yhl: STA   $FF00,Y
        LDA     hgr_hi,X
pl_l_ype: EOR   #0
        STA     pl_l_yr+2
        STA     pl_l_yw+2
pl_l_yhh: STA   $FF00,Y
pl_l_yr: LDA    $FFFF
        STA     pl_ptr
        ORA     pl_m
pl_l_yw: STA    $FFFF
        EOR     pl_ptr                  ; the bits it lit
        BEQ     pl_l_yn
pl_l_yhb: STA   $FF00,Y
        INY
pl_l_yn:
pl_l_yend: CPX  #0                      ; the last row
pl_l_ybne: BNE  pl_l_yl                 ; (pl_l_yl or pl_l_ylc, set above)
        JMP     pl_l_done
pl_l_yx:                                ; the x step (C = 1)
pl_l_ysbc: SBC  #0                      ; - dy
        STA     pl_err
pl_l_yxs: ASL   pl_m                    ; (ASL or LSR)
pl_l_yxb: BMI   pl_l_yxw                ; (BMI or BEQ: out of the byte)
        CLC
        BCC     pl_l_yplot
pl_l_yxw:
pl_l_yxc: INC   pl_x                    ; (INC or DEC)
pl_l_yxm: LDA   #$01                    ; ($01 or $40)
        STA     pl_m
        CLC
        BCC     pl_l_yplot


; ---- x major, x increasing: the run pl_y .. pl_m in the byte ---------------
pl_l_xinc:
        LDA     pl_dy
        STA     pl_l_xia+1
        LDA     pl_dx
        STA     pl_l_xic+1
        STA     pl_l_xisb+1
        CLC                             ; pixels: dx + 1 (0 = 256)
        ADC     #1
        STA     pl_cnt
        SEC                             ; the run starts: bits m..6
        LDA     #$80
        SBC     pl_m
        STA     pl_y
        LDX     pl_cur
        LDY     pl_n,X
        LDX     pl_y0
        LDA     pl_dx
        LSR     A
        CLC
        BCC     pl_l_xin
pl_l_xi:                                ; A = err, C = 0
pl_l_xia: ADC   #0                      ; + dy
        BCS     pl_l_xiy
pl_l_xic: CMP   #0                      ; dx
        BCS     pl_l_xiy
        ASL     pl_m                    ; the next pixel (C = 0)
        BMI     pl_l_xiw
pl_l_xin: DEC   pl_cnt
        BNE     pl_l_xi
        LDA     pl_m                    ; the end: bits pl_y .. m
        ASL     A
        SBC     #0                      ; (C = 0) 2 m - 1: bits 0..m
        AND     pl_y
        JSR     pl_fl
        JMP     pl_l_done
pl_l_xiw:                               ; out of the byte: write the run
        STA     pl_err
        LDA     pl_y
        JSR     pl_fl
        INC     pl_x
        LDA     #$01
        STA     pl_m
        LDA     #$7F
        STA     pl_y
        LDA     pl_err
        CLC
        BCC     pl_l_xin
pl_l_xiy:                               ; the row step (C = 1)
pl_l_xisb: SBC  #0                      ; - dx
        STA     pl_err
        LDA     pl_m                    ; the run to this pixel
        ASL     A
        SBC     #0
        AND     pl_y
        JSR     pl_fl
pl_l_xiyy: INX                          ; (INX or DEX)
        ASL     pl_m
        BMI     pl_l_xiyw
        SEC
        LDA     #$80
        SBC     pl_m
        STA     pl_y
        LDA     pl_err
        CLC
        BCC     pl_l_xin
pl_l_xiyw:
        INC     pl_x
        LDA     #$01
        STA     pl_m
        LDA     #$7F
        STA     pl_y
        LDA     pl_err
        CLC
        BCC     pl_l_xin

; ---- x major, x decreasing: the run pl_m .. pl_y ---------------------------
pl_l_xdec:
        LDA     pl_dy
        STA     pl_l_xda+1
        LDA     pl_dx
        STA     pl_l_xdc+1
        STA     pl_l_xdsb+1
        CLC
        ADC     #1
        STA     pl_cnt
        LDA     pl_m                    ; the run starts: bits 0..m
        ASL     A
        SBC     #0
        STA     pl_y
        LDX     pl_cur
        LDY     pl_n,X
        LDX     pl_y0
        LDA     pl_dx
        LSR     A
        CLC
        BCC     pl_l_xdn
pl_l_xd:
pl_l_xda: ADC   #0
        BCS     pl_l_xdy
pl_l_xdc: CMP   #0
        BCS     pl_l_xdy
        LSR     pl_m                    ; (C = 0 unless out of the byte)
        BEQ     pl_l_xdw
pl_l_xdn: DEC   pl_cnt
        BNE     pl_l_xd
        SEC                             ; the end: bits m .. pl_y
        LDA     #$80
        SBC     pl_m                    ; bits m..6
        AND     pl_y
        JSR     pl_fl
        JMP     pl_l_done
pl_l_xdw:
        STA     pl_err
        LDA     pl_y
        JSR     pl_fl
        DEC     pl_x
        LDA     #$40
        STA     pl_m
        LDA     #$7F
        STA     pl_y
        LDA     pl_err
        CLC
        BCC     pl_l_xdn
pl_l_xdy:
pl_l_xdsb: SBC  #0
        STA     pl_err
        SEC
        LDA     #$80
        SBC     pl_m
        AND     pl_y
        JSR     pl_fl
pl_l_xdyy: INX
        LSR     pl_m
        BEQ     pl_l_xdyw
        LDA     pl_m
        ASL     A
        SBC     #0                      ; (C = 0)
        STA     pl_y
        LDA     pl_err
        CLC
        BCC     pl_l_xdn
pl_l_xdyw:
        DEC     pl_x
        LDA     #$40
        STA     pl_m
        LDA     #$7F
        STA     pl_y
        LDA     pl_err
        CLC
        BCC     pl_l_xdn

pl_l_done:
        LDX     pl_cur
        TYA
        STA     pl_n,X
        RTS

; A = bits of row X, column pl_x: light the dark ones, record them at Y
pl_fl:  CPY     #255
        BEQ     pl_f_full
        STA     pl_ptr+1
        CLC
        LDA     hgr_lo,X
        ADC     pl_x
        STA     pl_f_r+1
        STA     pl_f_w+1
pl_f_hl: STA    $FF00,Y
        LDA     hgr_hi,X
pl_f_pe: EOR    #0
        STA     pl_f_r+2
        STA     pl_f_w+2
pl_f_hh: STA    $FF00,Y
pl_f_r: LDA     $FFFF
        STA     pl_ptr
        ORA     pl_ptr+1
pl_f_w: STA     $FFFF
        EOR     pl_ptr
        BEQ     :+
pl_f_hb: STA    $FF00,Y
        INY
:       RTS
pl_f_full:                              ; history full: the line stops
        PLA
        PLA
        JMP     pl_l_done
.else
; a line, Bresenham on the dominant axis (both ends drawn)
pl_line:
        LDA     pl_x0
        STA     pl_x
        LDA     pl_y0
        STA     pl_y
        SEC                             ; dx = |x1 - x0|, the x step
        LDA     pl_x1
        SBC     pl_x0
        LDX     #$E6                    ; INC zp
        BCS     :+
        EOR     #$FF
        ADC     #1
        LDX     #$C6                    ; DEC zp
:       STA     pl_dx
        STX     @sx1
        STX     @sx2
        SEC
        LDA     pl_y1
        SBC     pl_y0
        LDX     #$E6
        BCS     :+
        EOR     #$FF
        ADC     #1
        LDX     #$C6
:       STA     pl_dy
        STX     @sy1
        STX     @sy2
        CMP     pl_dx
        BCS     @ymaj
        LDA     pl_dx                   ; x major: dx + 1 pixels
        STA     pl_cnt
        LSR     A
        STA     pl_err
@xl:    JSR     pl_plot
        LDA     pl_cnt
        BEQ     @rts
        DEC     pl_cnt
@sx1:   INC     pl_x                    ; (INC or DEC, set above)
        LDA     pl_err
        CLC
        ADC     pl_dy
        BCS     :+
        CMP     pl_dx
        BCC     @xs
:       SBC     pl_dx
@sy1:   INC     pl_y
@xs:    STA     pl_err
        JMP     @xl
@ymaj:  LDA     pl_dy                   ; y major: dy + 1 pixels
        STA     pl_cnt
        LSR     A
        STA     pl_err
@yl:    JSR     pl_plot
        LDA     pl_cnt
        BEQ     @rts
        DEC     pl_cnt
@sy2:   INC     pl_y
        LDA     pl_err
        CLC
        ADC     pl_dx
        BCS     :+
        CMP     pl_dy
        BCC     @ys
:       SBC     pl_dy
@sx2:   INC     pl_x
@ys:    STA     pl_err
        JMP     @yl
@rts:   RTS
.endif  ; HGR_PLOT_HIST_FAST
.endif

.if .referenced(pl_sprite) .and (.not .defined(pl_sprite))
.ifndef HGR_PLOT_HIST_FAST
.error "hgr_plot_hist.asm: pl_sprite needs HGR_PLOT_HIST_FAST"
.endif
.ifndef PL_SPRITE_YMIN
PL_SPRITE_YMIN = 0
.endif
; pl_sprite -- a preshifted sprite, its top left pixel at pl_x, pl_y: its
; bits OR'ed in, those it lit recorded a byte at a time. pl_sp -> its
; descriptor: .byte W (bytes a row), H (rows), then .addr shift 0 .. shift
; 6, each H rows of W bytes (bit 0 = the leftmost pixel, bit 7 = 0). Rows
; outside PL_SPRITE_YMIN..191 are skipped (pl_y may be "negative": 256 - n);
; columns are not clipped (x / 7 + W <= 40). History full (a row that might
; not fit: pl_n > 255 - W): the rest is not drawn. Clobbers pl_sp, pl_x,
; pl_y, pl_m, pl_dx, pl_dy, pl_cnt, pl_err, pl_ptr.
pl_sprite:
        LDA     pl_cur
pl_s_pg: CMP    #$FF                    ; the page the operands are set for
        BEQ     pl_s_go
        STA     pl_s_pg+1
        LDA     pl_pg
        STA     pl_s_pe+1
        LDA     pl_hl+1
        STA     pl_s_hl+2
        LDA     pl_hh+1
        STA     pl_s_hh+2
        LDA     pl_hb+1
        STA     pl_s_hb+2
pl_s_go:
        LDY     #0
        LDA     (pl_sp),Y
        STA     pl_dx                   ; W
        EOR     #$FF                    ; 255 - W: the last history index
        STA     pl_dy                   ; a row may start at
        INY
        LDA     (pl_sp),Y
        STA     pl_cnt                  ; H
        LDX     pl_x
        LDA     hgr_div7,X
        STA     pl_m                    ; the column
        LDA     hgr_mod7,X              ; the shift's rows
        ASL     A
        ADC     #2                      ; (C = 0)
        TAY
        LDA     (pl_sp),Y
        PHA
        INY
        LDA     (pl_sp),Y
        STA     pl_sp+1
        PLA
        STA     pl_sp
        LDX     pl_cur
        LDA     pl_n,X
        STA     pl_err                  ; the history index
pl_s_row:
        LDX     pl_y
        CPX     #192
        BCS     pl_s_next
.if PL_SPRITE_YMIN > 0
        CPX     #PL_SPRITE_YMIN
        BCC     pl_s_next
        CLC
.endif
        LDA     hgr_lo,X                ; (C = 0)
        ADC     pl_m
        STA     pl_ptr
        LDA     hgr_hi,X
pl_s_pe: EOR    #0
        STA     pl_ptr+1
        LDX     pl_err
        CPX     pl_dy                   ; room for W entries?
        BEQ     :+
        BCS     pl_s_full
:       CLC                             ; (C = 0 through the bytes: no
        LDY     pl_dx                   ; carry out of a row's addresses)
        DEY
pl_s_b: LDA     (pl_sp),Y
        BEQ     pl_s_nb
        STA     pl_x
        LDA     (pl_ptr),Y
        EOR     #$FF
        AND     pl_x                    ; its dark bits
        BEQ     pl_s_nb
pl_s_hb: STA    $FF00,X
        ORA     (pl_ptr),Y
        STA     (pl_ptr),Y
        TYA
        ADC     pl_ptr
pl_s_hl: STA    $FF00,X
        LDA     pl_ptr+1
pl_s_hh: STA    $FF00,X
        INX
pl_s_nb: DEY
        BPL     pl_s_b
        STX     pl_err
pl_s_next:
        CLC
        LDA     pl_sp
        ADC     pl_dx
        STA     pl_sp
        BCC     :+
        INC     pl_sp+1
        CLC
:       INC     pl_y
        DEC     pl_cnt
        BNE     pl_s_row
pl_s_end:
        LDX     pl_cur
        LDA     pl_err
        STA     pl_n,X
        RTS
pl_s_full:
        STX     pl_err
        JMP     pl_s_end
.endif

.if .referenced(pl_bits) .and (.not .defined(pl_bits))   ; with pl_plot
pl_bits: .byte $01, $02, $04, $08, $10, $20, $40
.endif
