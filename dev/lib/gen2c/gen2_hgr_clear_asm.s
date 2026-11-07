; gen2_hgr_clear_asm.s — gen2c archive member: void gen2_hgr_clear(fill).
;
; Fills the 8 KB HIRES draw page (gen2_hgr_base: $20 page 1 / $40 page 2) with
; the byte in A (cc65 fastcall: the unsigned char argument arrives in A). The
; byte stays in A for every STA; ptr1 walks the pages by its high byte.
; Split from the former monolithic gen2_blit.s.
;
; _gen2_hgr_base is BSS (0 = unset, gen2_init.c) and gen2_hgr_clear() is a C
; entry callable BEFORE any table build — lazy-default it here too, or a
; pre-init call would scrub $0000-$1FFF (zero page + stack).

        .export   _gen2_hgr_clear
        .import   _gen2_hgr_base          ; HIRES draw-page hi base ($20/$40)
        .importzp ptr1

        .segment "CODE"

_gen2_hgr_clear:
        ldx _gen2_hgr_base   ; draw-page high byte ($20 page1 / $40 page2)
        bne @baseok          ; 0 = unset (BSS) -> default to page 1
        ldx #$20
        stx _gen2_hgr_base
@baseok:
        stx ptr1+1
        ldy #$00
        sty ptr1             ; ptr1 = base<<8, Y = 0
        ldx #$20             ; 32 pages = 8 KB (page-base independent)
@page:
        sta (ptr1),y         ; store the fill byte (A preserved throughout)
        iny
        bne @page            ; 256 bytes of this page (Y wraps back to 0)
        inc ptr1+1           ; next page
        dex
        bne @page            ; 32 pages -> done (8 KB framebuffer)
        rts
