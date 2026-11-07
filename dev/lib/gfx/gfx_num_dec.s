; gfx_num_dec.s — card-neutral unsigned decimal -> ASCII (gfx_utoa) and the
; one 16-bit binary-to-decimal converter every POM1 C runtime shares.
;
;   unsigned char gfx_utoa(char *buf, unsigned value)   cc65 fastcall
;       NUL-terminated, no leading zeros ("0" for 0); returns the digit count
;       (1..5) in A, X = 0.
;   gfx_u16_dec   asm entry: tmp1/tmp2 = value lo/hi, ptr1 = output buffer.
;       Writes the digits + NUL; Y = digit count. Consumes tmp1/tmp2, clobbers
;       A, X, tmp3, tmp4 and NZCV. Enter with D = 0. Also exported to gen2c as
;       _gen2_utoa (gen2c/gen2_utoa_asm.s), so a binary carries ONE converter.
;   gfx_num_buffer  pops the cc65 buffer argument into ptr1 (gfx_itoa reuses it).
;
; Repeated subtraction of the powers of ten (10000, 1000, 100, 10), the current
; digit held in Y, the units digit written straight from the remainder: no
; cc65 udiv16 runtime (the former C `/10 %10` pulled it in), no private zero
; page (cc65 scratch only), no self-modifying code and no DATA writes — safe in
; CodeTank ROM, which is where tms9918c (printlib.c) runs it from.
; Ported from pom2games dev/lib/math/dec.asm (u16_to_dec) and
; dev/lib/gfx/gfx_num_dec.s; pom2games measured it ~31 % faster than POM1's
; former _gen2_utoa (gen2c/gen2_blit.s, 81 B -> 69 B).
; A HEX-only program (gfx_hexstr, gfx_num_hex.s) links neither this member
; nor the conversion.

        .export   _gfx_utoa, gfx_u16_dec, gfx_num_buffer
        .import   popax
        .importzp ptr1, tmp1, tmp2, tmp3, tmp4

dec_lo  = tmp1               ; value low byte  (consumed)
dec_hi  = tmp2               ; value high byte (consumed)
dec_ptr = ptr1               ; output buffer
dec_t   = tmp3               ; low byte of the trial subtraction
dec_d   = tmp4               ; output index, nonzero once a digit is out

        .segment "CODE"

_gfx_utoa:
        sta dec_lo
        stx dec_hi
        jsr gfx_num_buffer
        jsr gfx_u16_dec
        tya                     ; digit count
        ldx #0
        rts

gfx_num_buffer:
        jsr popax               ; char *buf (popax touches only sp)
        sta dec_ptr
        stx dec_ptr+1
        rts

gfx_u16_dec:
        ldx #3                  ; descending indexes: 10000, 1000, 100, 10
        lda #0
        sta dec_d
@pw:    ldy #'0'                ; current digit, held during subtraction
        sec
@sub:   lda dec_lo              ; value -= power while it fits
        sbc dec_pw_lo,x
        sta dec_t
        lda dec_hi
        sbc dec_pw_hi,x
        bcc @digit
        sta dec_hi
        lda dec_t
        sta dec_lo
        iny                     ; register-held current digit
        bcs @sub                ; C still set
@digit: tya
        ldy dec_d               ; output index; the digit remains in A
        bne @emit
        cmp #'0'
        beq @skip               ; leading zero
@emit:  sta (dec_ptr),y
        iny
        sty dec_d
@skip:  dex
        bpl @pw
        lda dec_lo              ; units, always written
        ora #'0'
        sta (dec_ptr),y
        iny
        lda #0
        sta (dec_ptr),y
        rts

        .segment "RODATA"
dec_pw_lo: .byte <10, <100, <1000, <10000
dec_pw_hi: .byte >10, >100, >1000, >10000
