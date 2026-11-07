; ============================================================================
; hgr_flip.asm -- GEN2 double buffering by rewriting the hgr_hi scanline table
; ============================================================================
; Every drawing routine that reads hgr_lo / hgr_hi (hgr_scanline.inc or
; hgr_scanline_ram.asm, both plain RAM in a POM1 program) follows the draw
; page for free: switching it is one pass over hgr_hi, $2x <-> $4x being
; EOR #$60 per entry (~2 360 cycles, about 1/7 of a 60 Hz frame), far cheaper
; than parameterising every routine.
;
;   hgr_draw_hidden    draw page := the page NOT on screen (nothing changes
;                      on screen; the previous picture stays up).
;                      Clobbers A, X.
;   hgr_show_draw      show the draw page (one GEN2_PAGE1 / GEN2_PAGE2 read).
;                      It stays the draw page, so small updates that follow
;                      a full screen land on the visible page. Clobbers A.
;   hgr_set_draw_page  A = 0 (page 1) or PAGE2_EOR (page 2); no-op if the
;                      table already addresses that page. Clobbers A, X.
;   hgr_draw_page      byte: the page hgr_hi addresses now (0 / PAGE2_EOR).
;                      Kept next to the code, not in zero page, so it stays
;                      true for as long as the table itself (a re-entry
;                      without reloading the file finds both consistent).
;   hgr_front_page     byte: the page on screen (0 / PAGE2_EOR). Reserved
;                      next to the code unless the program defined it before
;                      the include (a zero-page alias is fine). Initialise it
;                      at boot with the first hgr_set_draw_page.
;
; A frame:  JSR hgr_draw_hidden / draw / JSR gen2_waitvbl / JSR hgr_show_draw
;
; GEN2 specifics (the only change from the Apple II module):
;   - [B] GEN2 card documentation (Uncle Bernie, transcribed in gen2.inc,
;     Q1-Q10): the page switches are READ-only toggles ($C254 / $C255); a
;     read flips the latch, so the show is an LDA, never an STA;
;   - gen2_sync.asm polls GEN2_POLL for HST0, and that poll IS a toggle:
;     its default, GEN2_PAGE1, would put page 1 back on screen every frame.
;     A double-buffered program sets GEN2_POLL = GEN2_HIRES (or another
;     switch that is part of its steady state) before including gen2_sync;
;   - hgr_text8's ht_page and hgr_blit2's bl_page EOR the page into the same
;     high byte: keep them at $00 when hgr_flip drives the page, or the two
;     selections cancel out.
;
; PAGE2_EOR defaults to $60. Needs hgr_scanline.inc (or hgr_scanline_ram.asm)
; in RAM and gen2.inc. No zero page. Code 68 B + 2 B of state;
; hgr_set_draw_page 2 412 cycles when the page changes (JSR/RTS included).
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_flip.asm, used by
; Maze3D and MICRO-SOKOBAN there); LOWSCR/HISCR became GEN2_PAGE1/GEN2_PAGE2.
; ============================================================================

.ifndef _HGR_FLIP_LOADED_
_HGR_FLIP_LOADED_ = 1

.include "gen2.inc"

.ifndef PAGE2_EOR
PAGE2_EOR = $60
.endif

hgr_draw_hidden:
        LDA     hgr_front_page
        EOR     #PAGE2_EOR      ; the page that is not on screen
        ; fall through

hgr_set_draw_page:
        CMP     hgr_draw_page
        BEQ     @done
        STA     hgr_draw_page
        LDX     #48             ; four quarters of 48 entries per turn
@lp:
.repeat 4, Q
        LDA     hgr_hi-1+Q*48,X
        EOR     #PAGE2_EOR
        STA     hgr_hi-1+Q*48,X
.endrepeat
        DEX
        BNE     @lp
@done:  RTS

hgr_show_draw:
        LDA     hgr_draw_page
        STA     hgr_front_page
        BNE     @p2
        LDA     GEN2_PAGE1      ; show page 1 (read = toggle)
        RTS
@p2:    LDA     GEN2_PAGE2      ; show page 2
        RTS

hgr_draw_page:
        .byte   0               ; 0 = hgr_hi addresses page 1
.ifndef hgr_front_page
hgr_front_page:
        .byte   0
.endif

.endif  ; _HGR_FLIP_LOADED_
