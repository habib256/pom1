; ----------------------------------------------------------------------------
; apple1io_str.s — woz_puts and woz_print_hexword (see apple1io.h).
;
; Formerly half of apple1io.c, whose four C functions compiled to ONE 101-byte
; object: calling any of them linked all four. They are now hand-written and
; split by family, so the DevBench's archive link (and any ar65 consumer)
; pulls only what a program calls:
;   apple1io_str.s  woz_puts + woz_print_hexword    (this file, 32 bytes)
;   apple1io_key.s  apple1_iskeypressed + apple1_readkey
; Model: the pom2games edition's apple2c/apple2io_str.s.
;
; [C] Woz Monitor listing: ECHO ($FFEF) is BIT DSP / BMI ECHO / STA DSP / RTS
;     -- it keeps A, X, Y and C; PRBYTE ($FFDC) clobbers only A (and flags).
;     woz_puts keeps its index in Y across ECHO on that basis.
; ----------------------------------------------------------------------------
        .export _woz_puts, _woz_print_hexword
        .importzp ptr1

ECHO    = $FFEF
PRBYTE  = $FFDC

        .segment "CODE"

; void woz_puts(const unsigned char *s): A/X = s, NUL-terminated. Each byte is
; sent with bit 7 set (the PIA display latches only when PB7 = 1, as in
; woz_putc). Strings may cross pages. ptr1 is cc65's scratch pointer.
_woz_puts:
        sta     ptr1
        stx     ptr1+1
        ldy     #0
@next:  lda     (ptr1),y
        beq     @done
        ora     #$80
        jsr     ECHO
        iny
        bne     @next
        inc     ptr1+1          ; Y wrapped: next page
        bne     @next           ; always (a string does not wrap past $FFFF)
@done:  rts

; void woz_print_hexword(unsigned w): A = low byte, X = high. High byte first.
_woz_print_hexword:
        pha
        txa
        jsr     PRBYTE
        pla
        jmp     PRBYTE
