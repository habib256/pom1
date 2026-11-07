; ============================================================================
; t19_hgr_flip_pages.s — micro-test: gen2/hgr_flip.asm + gen2/hgr_pages.inc/.asm
; ============================================================================
; GUARDS the two GEN2 double-buffering schemes ported from pom2games:
;
; hgr_flip (rewrites hgr_hi in place):
;   - hgr_set_draw_page A = $60: hgr_hi[0] = $40, hgr_hi[191] = $5F,
;     hgr_draw_page = $60; a store through the tables at row 5, column 3
;     lands at $5403 ($11), not $3403 ($00);
;   - hgr_show_draw: hgr_front_page = $60 (and GEN2_PAGE2 read);
;   - hgr_draw_hidden: back to page 1, hgr_hi[0] = $20, hgr_draw_page = 0;
;   - hgr_set_draw_page with the page already set is a no-op: hgr_hi[0]
;     stays $20 (a second EOR pass would have made it $40).
; hgr_pages (patched operands, two HGR_HI_SITE sites, one via HGR_LDA_HI_X):
;   - after hgr_pages_init + hgr_select_page A = 1: site A row 10 reads
;     $48, site B row 191 reads $5F; after A = 0: site A reads $28.
;
; POM1-LIB-MICRO-TEST
; LIBS:
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 20000
; EXPECT: 0F00 A5
; EXPECT: 0F01 40 5F 60 11 00 60 20 00 20
; EXPECT: 0F0A 48 5F 28
; ============================================================================

MB = $0F00

.include "hgr_pages.inc"

.zeropage
ptr:    .res 2

.segment "ENTRY"
start:
        LDA #0
        STA $3403
        STA $5403
        STA hgr_front_page
        ; --- hgr_flip ---------------------------------------------------
        LDA #$60
        JSR hgr_set_draw_page
        LDA hgr_hi
        STA MB+1
        LDA hgr_hi+191
        STA MB+2
        LDA hgr_draw_page
        STA MB+3
        LDX #5                  ; a store through the tables: row 5, col 3
        LDA hgr_lo,X
        STA ptr
        LDA hgr_hi,X
        STA ptr+1
        LDY #3
        LDA #$11
        STA (ptr),Y
        LDA $5403
        STA MB+4
        LDA $3403
        STA MB+5
        JSR hgr_show_draw
        LDA hgr_front_page
        STA MB+6
        JSR hgr_draw_hidden     ; front = page 2: draw page 1
        LDA hgr_hi
        STA MB+7
        LDA hgr_draw_page
        STA MB+8
        LDA #0
        JSR hgr_set_draw_page   ; already page 1: no-op
        LDA hgr_hi
        STA MB+9
        JSR hgr_show_draw       ; leave page 1 on screen

        ; --- hgr_pages --------------------------------------------------
        JSR hgr_pages_init
        LDA #1
        JSR hgr_select_page
        LDX #10
        JSR site_a
        STA MB+$0A
        LDX #191
        JSR site_b
        STA MB+$0B
        LDA #0
        JSR hgr_select_page
        LDX #10
        JSR site_a
        STA MB+$0C

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

.code
site_a:
        HGR_HI_SITE
        LDA hgr_hi,X
        RTS
site_b:
        HGR_LDA_HI_X
        RTS

.include "hgr_scanline.inc"
.include "hgr_flip.asm"
.include "hgr_pages.asm"
