; gfx_num_signed.s — card-neutral signed decimal -> ASCII (gfx_itoa).
;
;   unsigned char gfx_itoa(char *buf, int value)   cc65 fastcall
;       '-' then the magnitude (-32768 prints "-32768"), NUL-terminated;
;       returns the character count (sign included) in A, X = 0.
;
; Its own member so an unsigned-only program does not link it. Shares the one
; converter (gfx_u16_dec, gfx_num_dec.s). cc65 scratch only, no self-modifying
; code, no DATA writes (CodeTank-ROM safe). Ported from pom2games
; dev/lib/gfx/gfx_num_signed.s.

        .export   _gfx_itoa
        .import   _gfx_utoa, gfx_u16_dec, gfx_num_buffer
        .importzp ptr1, tmp1, tmp2

        .segment "CODE"

_gfx_itoa:
        cpx #$80
        bcc positive
        eor #$FF                ; 0 - value (-32768 stays 32768)
        adc #0                  ; CPX left C=1: complement plus one
        sta tmp1
        txa
        eor #$FF
        adc #0                  ; propagate the low-byte carry
        sta tmp2
        jsr gfx_num_buffer      ; ptr1 = buf
        lda #'-'
        ldy #0
        sta (ptr1),y
        inc ptr1
        bne :+
        inc ptr1+1
:       jsr gfx_u16_dec
        iny                     ; the sign counts
        tya
        ldx #0
        rts

positive:
        jmp _gfx_utoa
