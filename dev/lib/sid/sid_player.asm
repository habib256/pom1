; ============================================================================
; sid_player.asm -- data-driven single-voice (voice 1) SID sequencer.
; ----------------------------------------------------------------------------
; The SID analogue of beep/beep_sfx.asm: a compact "song" is a flat table of
; ROWS the player walks one per frame. Unlike the 1-bit beeper the SID sustains
; each note in hardware (ADSR), so a row just RE-PROGRAMS the voice and is then
; held for its duration -- no CPU toggling. This is the runtime the SID tracker
; editor exports to; voices 2 and 3 extend by the same pattern (duplicate the
; register block, as sid_play.asm notes).
;
;   sid_play_init        -- master volume on + a default voice-1 ADSR so notes
;                           are audible. Call once before starting a song.
;   sid_play_start  X=lo,Y=hi of a song table -- arm it (first note next tick).
;   sid_play_tick        -- advance the sequencer by ONE frame: hold the current
;                           row until its duration elapses, then program the next
;                           row. NON-BLOCKING -- call once per video frame; no-op
;                           when idle. Clobbers A,X,Y.
;   sid_play_active      -- A != 0 while a song is still playing.
;   sid_play_stop        -- gate voice 1 off (release) and go idle.
;
; SONG DATA FORMAT -- repeated 3-byte ROWS, terminated by a row whose duration
; byte is 0:
;       .byte note, ctrl, frames
;   note   : 0..95  play this note (index into sid_notes.inc), RETRIGGER the
;                   gate with `ctrl`'s waveform;
;            $FE    gate OFF (start the release phase -- a rest);
;            $FF    TIE  (leave the voice untouched -- sustain the previous note).
;   ctrl   : waveform mask (SID_TRI/SID_SAW/SID_PULSE/SID_NOISE). Only used when
;            note < 96; the gate bit is added by the player.
;   frames : how many sid_play_tick calls to hold this row. 0 = end of song.
;
; ZERO PAGE: sid_ptr/sid_frames/sid_ctrl (.exportzp'd). The player owns them.
; Self-contained: includes sid.inc + its note table, which lives in this object
; (the labels stay local -- no clash with a program that also includes
; sid_notes.inc in its own TU).
;
; NOTE TABLE -- option SID_PLAYER_FAST_NOTES (assemble with -D to change it):
;   0 (default)  sid_notes12.inc: the 12 notes of octave 7 (24 bytes); a lower
;                note is that value shifted right once per octave down, rounded
;                on the last bit out. ~130 cycles at worst (octave 0) once per
;                new row; 5 of the 96 notes come out 1 LSB from sid_notes.inc
;                (< 0.1 cent; worst error vs equal temperament 1.58 cents, the
;                same as the 96-note table). Pattern of the pom2games edition's
;                AYS player (12-note period table + octave shifts by default).
;   1            sid_notes.inc: the 96-note table (192 bytes), a straight
;                lookup: 140 bytes more in all, 18 cycles a new note.
; ============================================================================

.include "sid.inc"

.ifndef SID_PLAYER_FAST_NOTES
SID_PLAYER_FAST_NOTES = 0
.endif

.exportzp sid_ptr, sid_frames, sid_ctrl
.export   sid_play_init, sid_play_start, sid_play_tick, sid_play_active, sid_play_stop

.segment "ZEROPAGE"
sid_ptr:    .res 2          ; -> current row; hi = 0 => idle
sid_frames: .res 1          ; frames left on the current row (0 => load next)
sid_ctrl:   .res 1          ; last waveform mask (gate cleared) for gate-off

.segment "CODE"

; ----------------------------------------------------------------------------
; sid_play_init -- master volume + a usable default voice-1 ADSR envelope.
; ----------------------------------------------------------------------------
sid_play_init:
        lda     #$0F            ; master volume max, filter mode off
        sta     SID_VOLUME
        lda     #$28            ; attack 2 / decay 8
        sta     SID_V1_AD
        lda     #$A8            ; sustain 10 / release 8
        sta     SID_V1_SR
        rts

; ----------------------------------------------------------------------------
; sid_play_start -- X=lo, Y=hi of a song table. Arm; first row on next tick.
; ----------------------------------------------------------------------------
sid_play_start:
        stx     sid_ptr
        sty     sid_ptr+1
        lda     #0
        sta     sid_frames      ; 0 => load row 0 on the next tick
        rts

; ----------------------------------------------------------------------------
; sid_play_active -- A = ptr hi: 0 once the song has ended.
; ----------------------------------------------------------------------------
sid_play_active:
        lda     sid_ptr+1
        rts

; ----------------------------------------------------------------------------
; sid_play_tick -- hold the current row, or load the next one. No-op when idle.
; ----------------------------------------------------------------------------
sid_play_tick:
        lda     sid_ptr+1
        bne     @go
        rts                     ; idle
@go:
        lda     sid_frames
        beq     @load           ; row duration elapsed -> program the next row
        dec     sid_frames
        rts                     ; still holding the current note
@load:
        ldy     #0
        lda     (sid_ptr),y     ; note
        cmp     #$FF
        beq     @after          ; $FF = tie: leave the voice as-is
        cmp     #$FE
        beq     @gateoff        ; $FE = gate off (rest)
        ; play a note: set the 16-bit frequency, retrigger the gate.
.if SID_PLAYER_FAST_NOTES
        tax
        lda     sid_notes_lo,x
        sta     SID_V1_FREQLO
        lda     sid_notes_hi,x
        sta     SID_V1_FREQHI
.else
        ; A = note 0..95 -> Y = semitone, X = 1 + octaves below octave 7.
        ldx     #8
@oct:   cmp     #12
        bcc     @semi           ; (leaves C = 0)
        sbc     #12
        dex
        bne     @oct            ; always: 7 subtractions at most leave X >= 1
@semi:  tay
        lda     sid_top_lo,y
        sta     sid_ctrl        ; scratch for the low byte: the ctrl byte below
        lda     sid_top_hi,y    ;   overwrites it before anything reads it
        dex
        beq     @round          ; octave 7: no shift (C = 0, nothing to round)
@shift: lsr     a
        ror     sid_ctrl        ; C = the last bit shifted out
        dex
        bne     @shift
@round: bcc     @freq
        inc     sid_ctrl        ; round half up; sid_notes12.inc asserts no carry
@freq:  sta     SID_V1_FREQHI
        lda     sid_ctrl
        sta     SID_V1_FREQLO
.endif
        ldy     #1
        lda     (sid_ptr),y     ; ctrl (waveform, no gate)
        sta     sid_ctrl        ; remember for a later gate-off
        ora     #SID_GATE
        sta     SID_V1_CR       ; gate edge -> ADSR attack
        jmp     @after
@gateoff:
        lda     sid_ctrl        ; same waveform, gate bit clear -> release
        sta     SID_V1_CR
@after:
        ldy     #2
        lda     (sid_ptr),y     ; frames (row duration)
        beq     @done           ; 0 = terminator
        sta     sid_frames
        lda     sid_ptr         ; advance to the next 3-byte row
        clc
        adc     #3
        sta     sid_ptr
        bcc     @ret
        inc     sid_ptr+1
@ret:
        rts
@done:
        jmp     sid_play_stop

; ----------------------------------------------------------------------------
; sid_play_stop -- gate voice 1 off (release) and go idle.
; ----------------------------------------------------------------------------
sid_play_stop:
        lda     sid_ctrl        ; waveform without gate -> release phase
        sta     SID_V1_CR
        lda     #0
        sta     sid_ptr+1       ; idle
        rts

; --- note table (local to this object) ---------------------------------------
.if SID_PLAYER_FAST_NOTES
.include "sid_notes.inc"        ; 96 notes
.else
.include "sid_notes12.inc"      ; octave 7 only, see NOTE TABLE above
.endif
