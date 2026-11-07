; gen2_mode_asm.s — gen2c archive member: gen2_hgr_init + gen2_text_restore.
;
; The GEN2 soft-switch mode routines for C, from the shared asm source of truth
; dev/lib/gen2/gen2_init.asm (Bernie Q8: the $C250-$C257 latch is indeterminate
; at power-on, so a program sets every pair itself). One archive member per
; entry family so a program links only what it calls: gen2_hgr_init_clear is
; gen2_mode_clear_asm.s, gen2_lores_init is gen2_lores_init_asm.s.
;
; Why asm and not C: a soft-switch READ is the toggle and its value is
; discarded; cc65's optimiser (-Oirs) drops a volatile read cast to void.
; No zero page. Split from the former monolithic gen2_blit.s (member layout
; ported from pom2games dev/lib/hgrc/hgr_mode_asm.s).

        .export   _gen2_hgr_init, _gen2_text_restore

GEN2_INIT_SELECT = $09          ; hgr_init + text_restore
.include "../gen2/gen2_init.asm"
