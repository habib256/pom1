; ----------------------------------------------------------------------------
; apple1io_asm.s — shared Apple-1 Wozmon text + keyboard hooks for cc65.
;
; Named *_asm.s (not apple1io.s) on purpose: a hand-written apple1io.s would be
; clobbered by cc65's intermediate file of any apple1io.c. This mirrors
; nippur72/apple1-videocard-lib's apple1.c + apple1_asm.s split.
;
; Asm shim ported from nippur72/apple1-videocard-lib (Antonino "Nino"
;   Porcino).  https://github.com/nippur72/apple1-videocard-lib
; Upstream license unspecified at fork time (2026); preserve attribution.
; Trimmed since (2026-11, from the pom2games edition's apple2io_asm.s):
; woz_putc tail-calls ECHO, woz_print_hex and woz_mon ARE the ROM entries.
;
; cc65 calling convention: an `unsigned char` argument arrives in A, an
; `unsigned char` return value leaves in A — so each routine is a thin wrapper
; around the WOZ Monitor ROM entry points. The C-level rest of apple1io.h lives
; in apple1io_str.s (woz_puts, woz_print_hexword) and apple1io_key.s
; (apple1_iskeypressed, apple1_readkey).
; ----------------------------------------------------------------------------
.export _woz_putc, _woz_print_hex, _woz_mon, _apple1_getkey

ECHO    = $FFEF
PRBYTE  = $FFDC
; $FF1A is the Wozmon prompt entry: prints "\" + CR then drops into the line
; editor — so woz_mon() leaves the familiar "\" prompt (matches dev/lib/apple1's
; WOZMON). $FF1F is the silent post-prompt warm-restart (no "\").
WOZMON  = $FF1A
KEYCR   = $D011
KEYDATA = $D010

_woz_putc:
        ora     #$80            ; WozMon ECHO does not set bit 7; the PIA display
        jmp     ECHO            ; latches only when PB7=1, so OR it in (cf. print.asm)

; woz_print_hex(c): A = c is exactly PRBYTE's input, so the C symbol IS the ROM
; entry -- no wrapper, the caller's JSR lands on $FFDC (was JSR PRBYTE / RTS).
_woz_print_hex = PRBYTE

_woz_mon = WOZMON              ; never returns: the C call lands on the ROM entry

_apple1_getkey:
wait:   lda     KEYCR
        bpl     wait
        lda     KEYDATA
        and     #$7F
        rts
