; ============================================================================
; t29_sid_player_notes.s -- micro-test: sid_player.asm note -> frequency, x96
; ============================================================================
; GUARDS: the default (12-note + octave shift) build of sid_player.asm writes,
;   for every note 0..95, the frequency the 96-note table sid_notes.inc holds
;   -- except the five notes where shifting the octave-7 value and rounding on
;   the last bit out lands 1 LSB above the direct rounding (< 0.1 cent; see
;   tools/build_sid_notes.py). Pinned exactly: which notes, by how much, and
;   the 16-bit sum of all 96 values, so a wrong octave count, a lost rounding
;   bit or a carry the generator's .assert should have caught moves a byte.
;
;   How the frequency is read back: preset 1 has no A1-SID plugged, so the
;   player's stores to $C800/$C801 land in plain memory and read back. (On a
;   machine with the card they are write-only.)
;
;   Mailbox: $0F01 = notes off the table (5), $0F02.. = those notes in order
;   (55 75 76 80 83 = $37 $4B $4C $50 $53), $0F07 = the largest difference
;   (+1), $0F08/09 = sum of the 96 values ($8D67; the table's own is $8D62).
;
; POM1-LIB-MICRO-TEST
; LIBS: sid/sid_player.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 40000
; EXPECT: 0F00 A5 05 37 4B 4C 50 53 01 67 8D
; ============================================================================

.include "sid.inc"

.import   sid_play_start, sid_play_tick

MB = $0F00

.segment "ZEROPAGE"
note:     .res 1
nbad:     .res 1
maxd:     .res 1
sum:      .res 2
diff:     .res 1

.segment "BSS"
row:      .res 6                ; [note, ctrl, frames] + terminator

.segment "CODE"
main:
        SEI
        CLD
        LDX     #$FF
        TXS
        LDA     #0
        STA     note
        STA     nbad
        STA     maxd
        STA     sum
        STA     sum+1
        STA     row+3           ; terminator row
        STA     row+4
        STA     row+5
        LDA     #SID_TRI
        STA     row+1
        LDA     #1
        STA     row+2

@next:  LDA     note
        STA     row
        LDX     #<row
        LDY     #>row
        JSR     sid_play_start
        JSR     sid_play_tick   ; frames = 0 -> programs row 0 now

        CLC                     ; sum += freq
        LDA     sum
        ADC     SID_V1_FREQLO
        STA     sum
        LDA     sum+1
        ADC     SID_V1_FREQHI
        STA     sum+1

        LDX     note            ; diff = freq - table (low byte is enough:
        SEC                     ; the high bytes must match, checked below)
        LDA     SID_V1_FREQLO
        SBC     sid_notes_lo,X
        STA     diff
        LDA     SID_V1_FREQHI
        SBC     sid_notes_hi,X
        BEQ     @hi_ok
        LDA     #$EE            ; a high-byte difference: flag it loudly
        STA     maxd
        BNE     @off
@hi_ok: LDA     diff
        BEQ     @same
        CMP     maxd
        BCC     @off
        STA     maxd
@off:   LDY     nbad
        CPY     #5
        BCS     @full
        LDA     note
        STA     MB+2,Y
@full:  INC     nbad
@same:  INC     note
        LDA     note
        CMP     #96
        BNE     @next

        LDA     nbad
        STA     MB+1
        LDA     maxd
        STA     MB+7
        LDA     sum
        STA     MB+8
        LDA     sum+1
        STA     MB+9
        LDA     #$A5
        STA     MB
spin:   JMP     spin

; The 96-note reference table (the player keeps its own copy local).
.include "sid_notes.inc"
