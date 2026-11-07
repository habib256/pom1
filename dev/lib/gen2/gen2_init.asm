; ============================================================================
; gen2_init.asm -- deterministic GEN2 soft-switch init (Bernie Q8)
; ============================================================================
; The latch at $C250-$C257 is INDETERMINATE at power-on and survives Apple-1
; RESET. Call gen2_hgr_init (or gen2_lores_init) before any GEN2 drawing, and
; again after DevBench Run if a prior sketch left TEXT / MIXED / PAGE2 / LORES.
;
; Each switch is read-only: LDA/BIT toggles; never STA. These routines touch
; all four pairs (TEXT/graphics, MIXED, PAGE, RES) so the mode is known.
;
;   JSR gen2_hgr_init        GRAPHICS + HIRES + PAGE1 + full screen
;   JSR gen2_hgr_init_clear  same, but BLANK-FIRST: clears the page-1
;                            framebuffer ($2000-$3FFF) while the display is
;                            parked on TEXT, THEN flips to HIRES. The card has
;                            no display-enable bit and its framebuffer SRAM is
;                            indeterminate at power-on — plain gen2_hgr_init
;                            shows one frame of SRAM garbage until the program
;                            clears the page itself. ORDER MATTERS: clear
;                            first, flip after. (Existing callers that clear
;                            immediately after init keep plain gen2_hgr_init.)
;   JSR gen2_lores_init      GRAPHICS + LORES + PAGE1 + full screen
;   JSR gen2_text_restore    polite exit: back to TEXT + PAGE1 (the monitor-
;                            visible state) — toggles only, then RTS
;
; Blessed program exit — restore a monitor-visible mode, THEN leave:
;
;       jsr gen2_text_restore
;       jmp WOZMON              ; $FF1A prompt entry (dev/lib/apple1/apple1.inc)
;
; gen2_text_restore deliberately does NOT jump to Wozmon itself: this file is
; a pure GEN2 layer (imports nothing from dev/lib/apple1, and is textually
; .include'd by both bare asm sketches and gen2c's gen2_mode*_asm.s members), so the JMP
; target stays with the caller, which already owns the apple1.inc equates.
;
; C consumers link the gen2c archive members that .include this file
; (gen2c/gen2_mode_asm.s, gen2_lores_init_asm.s, gen2_mode_clear_asm.s) as
; _gen2_hgr_init / _gen2_lores_init / _gen2_hgr_init_clear / _gen2_text_restore.
; Asm sketches: .include "gen2_init.asm" and JSR gen2_hgr_init.
;
; Optional selection, for those archive members only: define GEN2_INIT_SELECT
; before the include to assemble a subset (bit 0 hgr_init, bit 1 lores_init,
; bit 2 hgr_init_clear, bit 3 text_restore). Undefined = all four, which is
; what every asm sketch gets (byte-identical to the unselected file). A
; selection without bit 0 that keeps bit 2 must define gen2_hgr_init itself
; (e.g. as an alias of an imported _gen2_hgr_init): hgr_init_clear's tail JMP
; lands on it.
; ============================================================================

.ifndef _GEN2_INIT_LOADED_
_GEN2_INIT_LOADED_ = 1

.include "gen2.inc"

.ifndef GEN2_INIT_SELECT
GEN2_INIT_SELECT = $0F
.endif

.segment "CODE"

.if GEN2_INIT_SELECT & $01
gen2_hgr_init:
_gen2_hgr_init:
        lda GEN2_TEXTOFF
        lda GEN2_HIRES
        lda GEN2_PAGE1
        lda GEN2_MIXOFF
        rts
.endif

.if GEN2_INIT_SELECT & $02
gen2_lores_init:
_gen2_lores_init:
        lda GEN2_TEXTOFF
        lda GEN2_LORES
        lda GEN2_PAGE1
        lda GEN2_MIXOFF
        rts
.endif

; --- blank-first HIRES init ---------------------------------------------------
; The GEN2 has no display-enable bit (unlike the TMS9918's R1 BLANK), so the
; "init to a clean screen" discipline is: park the display on TEXT, scrub the
; page-1 framebuffer ($2000-$3FFF, indeterminate SRAM at power-on), and only
; THEN flip to HIRES. Flipping first would display 8 KB of SRAM junk for as
; long as the clear takes (~0.1 s — very visible). Page 2 ($4000) is left
; untouched: init selects PAGE1; double-buffer programs clear page 2 through
; gen2_set_draw_page/gen2_hgr_clear before ever showing it.
; No zero-page use (the loop is 32 unrolled absolute-indexed stores), so this
; is safe from bare asm sketches and from the cc65 runtime alike.
.if GEN2_INIT_SELECT & $04
gen2_hgr_init_clear:
_gen2_hgr_init_clear:
        lda GEN2_TEXTON      ; park the display on TEXT while we scrub
        lda GEN2_PAGE1
        lda #$00
        tax
@clr:
.repeat 32, I
        sta $2000 + (I * $100), x
.endrepeat
        inx
        bne @clr
        jmp gen2_hgr_init    ; now flip: GRAPHICS + HIRES + PAGE1 + full
.endif

; --- polite exit: restore the monitor-visible mode ----------------------------
; Leaves the card on TEXT + PAGE1 so whatever runs next (Wozmon, BASIC) is
; visible on the GEN2 monitor. Toggles only + RTS — pair it with the caller's
; own JMP WOZMON (see the header idiom).
.if GEN2_INIT_SELECT & $08
gen2_text_restore:
_gen2_text_restore:
        lda GEN2_TEXTON
        lda GEN2_PAGE1
        rts
.endif

.endif
