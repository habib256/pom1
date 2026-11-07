; gen2_hgr_clear_asm.s — gen2c archive member: void gen2_hgr_clear(fill).
;
; *** SELF-MODIFYING CODE — RAM ONLY. *** The eight STA operands below are
; patched once per group of eight pages. gen2c programs load and run from RAM
; ($6000, dev/cc65/apple1_gen2_c.cfg); this member must never be linked into a
; ROM image (CodeTank / Juke-Box carts run in place from ROM).
;
; Fills the 8 KB HIRES draw page (gen2_hgr_base: $20 page 1 / $40 page 2) with
; the byte in A (cc65 fastcall: the unsigned char argument arrives in A).
; Eight absolute STA abs,Y per turn: 4 groups x 256 turns x (8 x 5 + 5)
; = 46 338 cycles counted per instruction, vs 90 422 for the former
; STA (ptr),Y / INY / BNE loop (+56 bytes of code).
; No zero page. Ported from pom2games HGR_CLEAR_LOOP (dev/lib/apple2/hgr.asm,
; used by dev/lib/hgrc/hgr_clear_asm.s).
;
; _gen2_hgr_base is BSS (0 = unset, gen2_init.c) and gen2_hgr_clear() is a C
; entry callable BEFORE any table build — lazy-default it here too, or a
; pre-init call would scrub $0000-$1FFF (zero page + stack).

        .export   _gen2_hgr_clear
        .import   _gen2_hgr_base          ; HIRES draw-page hi base ($20/$40)

        .segment "CODE"

_gen2_hgr_clear:
        ldx _gen2_hgr_base   ; draw-page high byte ($20 page1 / $40 page2)
        bne @baseok          ; 0 = unset (BSS) -> default to page 1
        ldx #$20
        stx _gen2_hgr_base
@baseok:
        ldy #4               ; 4 groups of 8 pages = 8 KB
        sty @cnt+1
@grp:   stx @s0+2            ; patch the eight store pages: X .. X+7
        inx
        stx @s1+2
        inx
        stx @s2+2
        inx
        stx @s3+2
        inx
        stx @s4+2
        inx
        stx @s5+2
        inx
        stx @s6+2
        inx
        stx @s7+2
        inx
        ldy #0
@lp:
@s0:    sta $2000,y          ; A (the fill byte) is preserved throughout
@s1:    sta $2100,y
@s2:    sta $2200,y
@s3:    sta $2300,y
@s4:    sta $2400,y
@s5:    sta $2500,y
@s6:    sta $2600,y
@s7:    sta $2700,y
        iny
        bne @lp
@cnt:   ldy #4               ; patched: groups left
        dey
        sty @cnt+1
        bne @grp
        rts
