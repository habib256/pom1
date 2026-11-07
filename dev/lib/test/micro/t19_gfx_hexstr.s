; ===========================================================================
; t19_gfx_hexstr.s — micro-test: gfx_hexstr (asm), wide sweep
; ===========================================================================
; GUARDS gfx/gfx_num_hex.s (ported from pom2games gfx/gfx_num_hex.s, replacing
; the 168 B C version): uppercase, no leading zeros, "0" for 0, interior
; zeros kept ($0A00 -> "A00"), NUL, and the returned length — through the
; real cc65 entry point (buffer via a popax stub, cc65 scratch zero page
; supplied by the driver). Values:
;   $0000..$0110  every value (1-, 2-, 3-digit forms, the $100 boundary)
;   $0FF0..$1010  every value (the 3 -> 4 digit boundary)
;   then every $F1-th value up to $FFFF ($F1 is odd: the low nibble cycles
;   through all sixteen digits), then $FF00..$FFFF (the top edge).
; Oracle independent of the converter: the value kept as four nibbles the
; driver advances with its own carry, mapped through a literal digit table.
;
; Mailbox: +0 magic $A5 | +1/+2 error count | +3/+4 first bad value
; ($FFFF = none) | +5.. last string "FFFF",0 + length 4.
;
; POM1-LIB-MICRO-TEST
; LIBS: gfx/gfx_num_hex.s
; CFG: micro.cfg
; PRESET: 1
; STEPS: 150000
; EXPECT: 0F00 A5 00 00 FF FF 46 46 46 46 00 04
; ===========================================================================

        .exportzp ptr1, tmp1, tmp2
        .export   popax
        .import   _gfx_hexstr

MB      = $0F00
STRIDE  = $F1

        .segment "ZEROPAGE"
ptr1:   .res 2
tmp1:   .res 1
tmp2:   .res 1
val:    .res 2          ; value under test
errs:   .res 2
first:  .res 2
lead:   .res 1          ; index of the oracle's first significant nibble
cnt:    .res 1          ; length returned by the converter
endlo:  .res 1          ; inclusive end of an every-value run
endhi:  .res 1

        .segment "BSS"
buf:    .res 6
nib:    .res 4          ; oracle: four nibbles, most significant first

        .segment "RODATA"
hexch:  .byte "0123456789ABCDEF"
add_st: .byte 0, 0, STRIDE >> 4, STRIDE & $0F
start2: .byte $0, $F, $F, $0    ; $0FF0
start3: .byte $F, $F, $0, $0    ; $FF00

        .segment "ENTRY"
main:
        cld
        lda #0
        sta errs
        sta errs+1
        lda #$FF
        sta first
        sta first+1
        ; ---- $0000..$0110 ----
        ldx #3
        lda #0
:       sta nib,x
        dex
        bpl :-
        sta val
        sta val+1
        lda #<$0110
        sta endlo
        lda #>$0110
        sta endhi
        jsr run
        ; ---- $0FF0..$1010 ----
        ldx #3
:       lda start2,x
        sta nib,x
        dex
        bpl :-
        lda #<$0FF0
        sta val
        lda #>$0FF0
        sta val+1
        lda #<$1010
        sta endlo
        lda #>$1010
        sta endhi
        jsr run
        ; ---- stride up to $FFFF ----
@st:    clc
        lda val
        adc #STRIDE
        sta val
        bcc :+
        inc val+1
        beq @top                ; wrapped past $FFFF
:       jsr nib_add_st
        jsr test_one
        jmp @st
        ; ---- $FF00..$FFFF ----
@top:   ldx #3
:       lda start3,x
        sta nib,x
        dex
        bpl :-
        lda #<$FF00
        sta val
        lda #>$FF00
        sta val+1
        lda #$FF
        sta endlo
        sta endhi
        jsr run
        ; ---- mailbox ----
        lda errs
        sta MB+1
        lda errs+1
        sta MB+2
        lda first
        sta MB+3
        lda first+1
        sta MB+4
        ldx #4
:       lda buf,x
        sta MB+5,x
        dex
        bpl :-
        lda cnt
        sta MB+10
        lda #$A5
        sta MB                  ; magic LAST
@spin:  jmp @spin

; Test every value from val to endhi:endlo inclusive (nib tracks val).
run:    jsr test_one
        lda val
        cmp endlo
        bne :+
        lda val+1
        cmp endhi
        beq @done
:       inc val
        bne :+
        inc val+1
:       jsr nib_inc
        jmp run
@done:  rts

test_one:
        lda val
        ldx val+1
        jsr _gfx_hexstr
        sta cnt
        ldx #0                  ; lead = first nonzero nibble (or 3)
:       lda nib,x
        bne :+
        inx
        cpx #3
        bne :-
:       stx lead
        ldy #0
@c:     lda nib,x
        stx tmp1                ; (scratch: the converter is done with it)
        tax
        lda hexch,x
        ldx tmp1
        cmp buf,y
        bne bad
        iny
        inx
        cpx #4
        bne @c
        lda buf,y
        bne bad
        lda #4
        sec
        sbc lead
        cmp cnt
        bne bad
        rts

bad:    lda first
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

nib_inc:
        ldx #3
@i:     inc nib,x
        lda nib,x
        cmp #16
        bne @r
        lda #0
        sta nib,x
        dex
        bpl @i
@r:     rts

nib_add_st:
        ldx #3
        clc
@a:     lda nib,x
        adc add_st,x
        cmp #16
        bcc @nc
        sbc #16                 ; C=1 from cmp
        sta nib,x
        sec
        bcs @nx
@nc:    sta nib,x
        clc
@nx:    dex
        bpl @a
        rts

popax:  lda #<buf
        ldx #>buf
        rts
