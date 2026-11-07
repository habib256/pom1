; ===========================================================================
; t18_gfx_utoa.s — micro-test: gfx_utoa / gfx_itoa, wide sweep
; ===========================================================================
; GUARDS the one shared 16-bit decimal converter (gfx/gfx_num_dec.s, ported
; from pom2games math/dec.asm u16_to_dec — also behind gen2c's _gen2_utoa)
; and its signed wrapper (gfx/gfx_num_signed.s), through their real cc65
; entry points. Checks digits, NUL, no leading zeros ("0" for 0) and the
; returned count for:
;   gfx_utoa  v = 0..1009 (every value: all 1-, 2-, 3-digit forms and the
;             1000 boundary), then every 197th value up to 65500 (197 is prime
;             to 10, so the units digit cycles through all ten digits),
;             then 65500..65535 (the top edge);
;   gfx_itoa  v = -1..-1009, every 197th magnitude up to 32768, then
;             -32740..-32768 (-32768: its two's-complement negation is itself).
; Set STRIDE = 1 (and add_st to 0,0,0,0,1, STEPS to 40000000) for the
; EXHAUSTIVE run — every unsigned and every negative value; it passed when the
; converter was ported, but at POM1's --step rate it takes ~100 s, too slow
; for the suite.
; The oracle is independent of the converter: an ASCII decimal counter the
; driver advances in lock-step (digit-wise carry, no division anywhere). The
; C buffer argument arrives through popax: the driver supplies a popax stub
; returning the buffer, and the cc65 scratch zero page the converter imports.
;
; Mailbox: +0 magic $A5 | +1/+2 utoa error count | +3/+4 first bad value
; ($FFFF = none) | +5/+6 itoa error count | +7/+8 first bad magnitude |
; +9.. last utoa string "65535",0 + count 5 | +$10.. "-32768",0 + count 6.
;
; POM1-LIB-MICRO-TEST
; LIBS: gfx/gfx_num_dec.s gfx/gfx_num_signed.s
; CFG: micro.cfg
; PRESET: 1
; STEPS: 1100000
; EXPECT: 0F00 A5 00 00 FF FF 00 00 FF FF 36 35 35 33 35 00 05
; EXPECT: 0F10 2D 33 32 37 36 38 00 06
; ===========================================================================

        .exportzp ptr1, tmp1, tmp2, tmp3, tmp4
        .export   popax
        .import   _gfx_utoa, _gfx_itoa

MB      = $0F00
STRIDE  = 197

        .segment "ZEROPAGE"
ptr1:   .res 2
tmp1:   .res 1
tmp2:   .res 1
tmp3:   .res 1
tmp4:   .res 1
val:    .res 2          ; value (unsigned) / magnitude (signed) under test
errs:   .res 2          ; error count
first:  .res 2          ; first failing value
lead:   .res 1          ; index of the oracle's first significant digit
cnt:    .res 1          ; count returned by the converter (digits only)
signed: .res 1          ; 0 = test gfx_utoa, 1 = gfx_itoa(-val)

        .segment "BSS"
buf:    .res 8          ; converter output
ref:    .res 5          ; oracle: 5 ASCII digits, most significant first

        .segment "RODATA"
add_st: .byte 0, 0, 1, 9, 7     ; STRIDE as plain digits
top_u:  .byte "65500"
top_s:  .byte "32740"

        .segment "ENTRY"
main:
        cld
        ; ---------------- unsigned ----------------------------------------
        lda #0
        sta signed
        jsr ref_reset
        lda #0
        sta val
        sta val+1
@u1:    jsr test_one            ; v = 0..1009, every value
        lda val
        cmp #<1009
        bne :+
        lda val+1
        cmp #>1009
        beq @u2
:       jsr val_inc
        jmp @u1
@u2:    clc                     ; v += STRIDE while it fits in 16 bits
        lda val
        adc #STRIDE
        sta val
        bcc :+
        inc val+1
        beq @u3                 ; wrapped past 65535
:       jsr ref_add_st
        jsr test_one
        jmp @u2
@u3:    ldx #4                  ; v = 65500..65535
:       lda top_u,x
        sta ref,x
        dex
        bpl :-
        lda #0
        sta lead
        lda #<65500
        sta val
        lda #>65500
        sta val+1
@u4:    jsr test_one
        lda val
        and val+1
        cmp #$FF
        beq @udone
        jsr val_inc
        jmp @u4
@udone: ldx #1                  ; mailbox +1..+4, +9..+15
        jsr save_errs
        ldx #5
:       lda buf,x
        sta MB+9,x
        dex
        bpl :-
        lda cnt
        sta MB+15
        ; ---------------- signed (magnitudes) -----------------------------
        inc signed
        jsr ref_reset
        lda #0
        sta val
        sta val+1
@s1:    jsr val_inc             ; m = 1..1009, every value
        jsr test_one
        lda val
        cmp #<1009
        bne @s1
        lda val+1
        cmp #>1009
        bne @s1
@s2:    clc                     ; m += STRIDE while m <= 32768
        lda val
        adc #STRIDE
        sta val
        lda val+1
        adc #0
        sta val+1
        cmp #$80
        bcc :+
        bne @s3
        lda val
        bne @s3                 ; > $8000
:       jsr ref_add_st
        jsr test_one
        jmp @s2
@s3:    ldx #4                  ; m = 32740..32768
:       lda top_s,x
        sta ref,x
        dex
        bpl :-
        lda #0
        sta lead
        lda #<32740
        sta val
        lda #>32740
        sta val+1
@s4:    jsr test_one
        lda val+1
        cmp #$80
        beq @sdone              ; 32768 = $8000 was the last
        jsr val_inc
        jmp @s4
@sdone: ldx #5                  ; mailbox +5..+8, +$10..+$17
        jsr save_errs
        ldx #6
:       lda buf,x
        sta MB+$10,x
        dex
        bpl :-
        ldx cnt
        inx                     ; the sign counts
        stx MB+$17
        lda #$A5
        sta MB                  ; magic LAST
@spin:  jmp @spin

; Convert val (or -val) and compare with the oracle.
test_one:
        lda signed
        bne @neg
        lda val
        ldx val+1
        jsr _gfx_utoa
        sta cnt
        ldy #0
        jmp check_from_y
@neg:   lda #0                  ; A/X = -val
        sec
        sbc val
        pha
        lda #0
        sbc val+1
        tax
        pla
        jsr _gfx_itoa
        sta cnt
        dec cnt                 ; digits only
        lda buf
        cmp #'-'
        beq :+
        jsr bad
:       ldy #1
        ; fall through
; Compare buf[Y..] against ref[lead..4] + NUL, and cnt against 5 - lead.
check_from_y:
        ldx lead
@c:     lda buf,y
        cmp ref,x
        bne bad
        iny
        inx
        cpx #5
        bne @c
        lda buf,y
        bne bad
        lda #5
        sec
        sbc lead
        cmp cnt
        bne bad
        rts

; Record a failure: errs++, first = val if unset (first starts at $FFFF).
bad:
        lda first
        and first+1
        cmp #$FF
        bne :+
        lda val
        sta first
        lda val+1
        sta first+1
:       inc errs
        bne :+
        inc errs+1
:       rts

save_errs:                      ; X = mailbox offset
        lda errs
        sta MB,x
        lda errs+1
        sta MB+1,x
        lda first
        sta MB+2,x
        lda first+1
        sta MB+3,x
        rts

ref_reset:
        ldx #4
        lda #'0'
:       sta ref,x
        dex
        bpl :-
        lda #4
        sta lead
        lda #0
        sta errs
        sta errs+1
        lda #$FF
        sta first
        sta first+1
        rts

val_inc:
        inc val
        bne :+
        inc val+1
:       ; fall through: ref += 1
; ref += 1 with decimal carry; lead follows the most significant digit.
ref_inc:
        ldx #4
@i:     inc ref,x
        lda ref,x
        cmp #'9'+1
        bne fix_lead
        lda #'0'
        sta ref,x
        dex
        bpl @i
        rts

; ref += STRIDE (digit-wise, decimal carry).
ref_add_st:
        ldx #4
        clc
@a:     lda ref,x
        adc add_st,x
        cmp #'9'+1
        bcc @nc
        sbc #10                 ; C=1 from cmp: subtract 10, carry out
        sta ref,x
        sec
        bcs @nx
@nc:    sta ref,x
        clc
@nx:    dex
        bpl @a
        ; lead = first non-'0' digit (or 4)
        ldx #0
@l:     lda ref,x
        cmp #'0'
        bne fix_lead2
        inx
        cpx #4
        bne @l
fix_lead2:
        stx lead
        rts
fix_lead:
        cpx lead
        bcs :+
        stx lead
:       rts

; cc65 popax stub: the "C stack" argument is always the buffer.
popax:
        lda #<buf
        ldx #>buf
        rts
