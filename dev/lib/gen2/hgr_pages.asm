; ============================================================================
; hgr_pages.asm -- the routines of hgr_pages.inc (include after every site)
; ============================================================================
;   hgr_pages_init    build hgr_hi2 from hgr_hi (once, ~3 600 cycles).
;                     Clobbers A, X.
;   hgr_select_page   A = 0: draw on page 1, otherwise page 2. Rewrites the
;                     operand of every HGR_HI_SITE (with HGR_PAGES_ORA: the
;                     immediate of its ORA, X = $20/$40 on exit). Clobbers
;                     X, Y; keeps A. 8 cycles a site (4 with HGR_PAGES_ORA)
;                     + ~14.
;
; hgr_hi2 (192 bytes) is reserved in segment "BSS" unless the program
; defined it before the include (e.g. `hgr_hi2 = $0E00`, or a .res of its
; own): most POM1 GEN2 cfgs have no BSS segment, so either declare one or
; give hgr_hi2 an address. Needs hgr_scanline.inc (hgr_hi). Both routines
; are assembled only when referenced (.ifref). See hgr_pages.inc.
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_pages.asm).
; ============================================================================

.ifndef _HGR_PAGES_ASM_LOADED_
_HGR_PAGES_ASM_LOADED_ = 1

.ifndef _HGR_PAGES_INC_LOADED_
        .error "hgr_pages.asm: include hgr_pages.inc at the top first"
.endif

.if !HGR_PAGES_ORA
.ifndef hgr_hi2
.pushseg
.segment "BSS"
hgr_hi2: .res 192
.popseg
.endif
.endif

.ifref hgr_pages_init
.if HGR_PAGES_ORA
        .error "hgr_pages_init: the HGR_PAGES_ORA variant has no hgr_hi2 to build"
.endif
hgr_pages_init:
        LDX     #191
@hi:    LDA     hgr_hi,X
        CLC
        ADC     #$20
        STA     hgr_hi2,X
        DEX
        CPX     #$FF
        BNE     @hi
        RTS
.endif

.ifref hgr_select_page
.if HGR_PAGES_ORA
hgr_select_page:                ; ORA variant: patch each site's immediate
        LDX     #$20
        CMP     #0
        BEQ     @set
        LDX     #$40
@set:
.repeat HGR_HI_SITES, I
        STX     .ident(.sprintf("hgr_hi_site%d", I)) + 1
.endrepeat
        RTS
.else
hgr_select_page:
        LDX     #<hgr_hi
        LDY     #>hgr_hi
        CMP     #0
        BEQ     @set
        LDX     #<hgr_hi2
        LDY     #>hgr_hi2
@set:
.repeat HGR_HI_SITES, I
        STX     .ident(.sprintf("hgr_hi_site%d", I)) + 1
        STY     .ident(.sprintf("hgr_hi_site%d", I)) + 2
.endrepeat
        RTS
.endif
.endif

.endif  ; _HGR_PAGES_ASM_LOADED_
