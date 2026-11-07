; gen2_mode_clear_asm.s — gen2c archive member: gen2_hgr_init_clear.
; Blank-first HIRES init from dev/lib/gen2/gen2_init.asm: park the display on
; TEXT, scrub page 1 with 32 unrolled absolute stores, then flip. ~110 bytes,
; so it is its own member: a program that calls plain gen2_hgr_init (then its
; own clear) does not pay for it. The tail JMP lands on gen2_mode_asm.s's
; _gen2_hgr_init (one copy in the link). No zero page.

        .export   _gen2_hgr_init_clear
        .import   _gen2_hgr_init

gen2_hgr_init = _gen2_hgr_init  ; target of the tail JMP
GEN2_INIT_SELECT = $04          ; hgr_init_clear only
.include "../gen2/gen2_init.asm"
