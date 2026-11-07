; gen2_lores_init_asm.s — gen2c archive member: gen2_lores_init.
; GRAPHICS + LORES + PAGE1 + full screen, from dev/lib/gen2/gen2_init.asm (see
; gen2_mode_asm.s for why the mode routines are asm). Its own member so a
; HIRES-only program does not link it. No zero page.

        .export   _gen2_lores_init

GEN2_INIT_SELECT = $02          ; lores_init only
.include "../gen2/gen2_init.asm"
