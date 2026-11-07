; gfx_num_hex.s — card-neutral unsigned hex -> ASCII (gfx_hexstr).
;
;   unsigned char gfx_hexstr(char *buf, unsigned value)   cc65 fastcall
;       Uppercase, no leading zeros, 1..4 digits ("0" for 0), NUL-terminated;
;       returns the length in A, X = 0.
;
; Skip an empty high byte, then share the two-nibble conversion; the upper
; nibble is omitted only before the first digit. No table, no 16-bit shifts,
; no private zero page (cc65 ptr1/tmp1/tmp2), no self-modifying code, no DATA
; writes (CodeTank-ROM safe). Enter with D = 0. 64 bytes vs 168 for the former
; C version. Ported from pom2games dev/lib/gfx/gfx_num_hex.s.

        .export   _gfx_hexstr
        .import   popax
        .importzp ptr1, tmp1, tmp2

        .segment "CODE"

_gfx_hexstr:
        sta tmp1
        stx tmp2
        jsr popax
        sta ptr1
        stx ptr1+1
        ldy #0
        lda tmp2
        beq low_byte
        jsr byte
low_byte:
        lda tmp1
        jsr byte
        lda #0
        sta (ptr1),y
        tya
        ldx #0
        rts
byte:
        sta tmp2
        lsr a
        lsr a
        lsr a
        lsr a
        bne emit
        cpy #0
        beq low_nibble
emit:
        jsr nibble
low_nibble:
        lda tmp2
        and #$0f
nibble:
        cmp #10
        bcc decimal
        adc #6                  ; C=1: map 10..15 to 17..22
decimal:
        adc #'0'                ; both paths have C=0 (ADC #6 cannot carry)
        sta (ptr1),y
        iny
        rts
