; ============================================================================
; t52_ondemand_gen2_late.s — micro-test: on-demand hgr_clear + gen2_init
; ============================================================================
; GUARDS the "Assembled on demand" convention (dev/lib/README.md) where a
; routine of a later include must reach code emitted by an earlier one:
;   - hgr_clear.asm: include #1 follows the hgr_fill_page calls only, so it
;     emits hgr_fill_page alone; include #2 follows a clear_hgr call and
;     emits clear_hgr, which cannot fall into a body lying behind it and
;     must end on JMP hgr_fill_page. Exit registers as before (A = $00,
;     X = $40, Y = $00), page 1 cleared, page 2 and the guard bytes kept.
;   - gen2_init.asm: include #1 follows a gen2_hgr_init call only; include
;     #2 emits gen2_hgr_init_clear, whose tail JMP lands on #1's routine.
;     Page 1 is filled with $55 first: the blank-first clear must zero it.
;   - gen2_lores_init / gen2_text_restore have no caller: not emitted (an
;     asm sketch used to get all four).
;
;   MB+1/+2   $2000/$3FFF after gen2_hgr_init_clear        00 00
;   MB+3..5   A X Y after clear_hgr                         00 40 00
;   MB+6..9   $2000 $3FFF (cleared) $4000 $5FFF ($AA kept)  00 00 AA AA
;   MB+10/11  $1FFF $1FFE (outside both pages)              5A 5A
;
; POM1-LIB-MICRO-TEST
; INCLUDES: gen2/hgr_clear.asm gen2/gen2_init.asm
; CFG: micro_gen2.cfg
; PRESET: 11
; LOAD: 6000
; RUN: 6000
; STEPS: 80000
; EXPECT: 0F00 A5 00 00 00 40 00 00 00 AA AA 5A 5A
; ============================================================================

MB = $0F00

.segment "ENTRY"
start:
        LDA #$5A
        STA $1FFF
        STA $1FFE
        LDA #$AA
        LDX #$40
        JSR hgr_fill_page       ; page 2 := $AA
        JSR gen2_hgr_init

; Include #1: hgr_fill_page and gen2_hgr_init are the only references yet.
.code
.include "hgr_clear.asm"
.include "gen2_init.asm"
.ifdef clear_hgr
.error "t52: clear_hgr emitted before any reference"
.endif
.ifdef gen2_hgr_init_clear
.error "t52: gen2_hgr_init_clear emitted before any reference"
.endif

.segment "ENTRY"
        LDA #$55
        LDX #$20
        JSR hgr_fill_page       ; page 1 := $55
        JSR gen2_hgr_init_clear ; blank-first: page 1 := $00, then HIRES
        LDA $2000
        STA MB+1
        LDA $3FFF
        STA MB+2

        LDA #$77
        LDX #$20
        JSR hgr_fill_page       ; page 1 := $77
        JSR clear_hgr           ; page 1 := $00 through the JMP tail
        STA MB+3                ; exit registers
        STX MB+4
        STY MB+5
        LDA $2000
        STA MB+6
        LDA $3FFF
        STA MB+7
        LDA $4000
        STA MB+8
        LDA $5FFF
        STA MB+9
        LDA $1FFF
        STA MB+10
        LDA $1FFE
        STA MB+11

        LDA #$A5                ; magic written LAST
        STA MB
spin:   JMP spin

; Include #2: clear_hgr and gen2_hgr_init_clear are referenced now.
.code
.include "hgr_clear.asm"
.include "gen2_init.asm"
.assert clear_hgr > hgr_fill_page, error, "t52: clear_hgr must come from the second include"
.assert gen2_hgr_init_clear > gen2_hgr_init, error, "t52: gen2_hgr_init_clear must come from the second include"
.ifdef gen2_lores_init
.error "t52: gen2_lores_init emitted with no caller"
.endif
.ifdef gen2_text_restore
.error "t52: gen2_text_restore emitted with no caller"
.endif
