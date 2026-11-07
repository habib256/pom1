; ============================================================================
; hgr_pages.inc -- GEN2 draw page chosen by patching the row-table operands
; ============================================================================
; The other way to draw on page 2 (hgr_flip.asm is the first): instead of
; rewriting the 192 entries of hgr_hi (~2 360 cycles a change, any number of
; readers), every instruction that reads hgr_hi gets its OPERAND rewritten
; to hgr_hi (page 1) or hgr_hi2 (the same rows + $20, page 2). ~8 cycles per
; site per page change, nothing per access, and hgr_hi stays read-only.
; Pick this one when the readers are few and known (a game's own kernels);
; hgr_flip when library routines you do not mark read the table too.
;
; Include at the TOP of the program (before any site). Then mark every
; instruction that reads the high-byte row table:
;
;           HGR_HI_SITE
;           LDA     hgr_hi,X            ; any abs,X / abs,Y opcode
;
; A site defines symbols, and ca65 ends the scope of @ labels at any symbol
; definition: treat HGR_HI_SITE like a label (no @ label used across it).
;
; Include hgr_pages.asm AFTER the last site: hgr_select_page rewrites the
; operand of every marked instruction.
;
; ORA variant, for a program with no room for hgr_hi2: define
; HGR_PAGES_ORA = 1 before the include, let hgr_hi hold page-free offsets
; ($00-$1F) and read through HGR_LDA_HI_X. Each read is followed by an
; ORA #$20/#$40 whose immediate hgr_select_page rewrites: 2 cycles more a
; read, 4 a site per change, no table. HGR_LDA_HI_X also suits the default
; mode (a site + LDA hgr_hi,X).
;
; The page shown is a separate matter: read GEN2_PAGE1 / GEN2_PAGE2
; (gen2.inc) to show one. SELF-MODIFYING (patched operands): RAM only,
; never in a CodeTank / Juke-Box cartridge running from ROM.
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_pages.inc).
; ============================================================================

.ifndef _HGR_PAGES_INC_LOADED_
_HGR_PAGES_INC_LOADED_ = 1
; Boolean switches: zero and undefined select the default implementation.
.ifndef HGR_PAGES_ORA
HGR_PAGES_ORA = 0
.endif

HGR_HI_SITES .set 0

.macro HGR_HI_SITE
.ifdef _HGR_PAGES_ASM_LOADED_
        .error "HGR_HI_SITE after hgr_pages.asm: that site would never be patched"
.endif
        ; Count first: the site symbol, defined last, is unique and so opens
        ; a fresh @ scope (the counter's .set would share one between sites).
HGR_HI_SITES .set HGR_HI_SITES + 1
        .ident(.sprintf("hgr_hi_site%d", HGR_HI_SITES - 1)) = *
.endmacro

.macro HGR_LDA_HI_X
.if HGR_PAGES_ORA
        LDA     hgr_hi,X
        HGR_HI_SITE                     ; the ORA immediate is patched
        ORA     #$20
.else
        HGR_HI_SITE
        LDA     hgr_hi,X
.endif
.endmacro

.endif  ; _HGR_PAGES_INC_LOADED_
