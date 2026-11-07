; ============================================================================
; at.asm -- AT-command framing helpers on top of acia.asm
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"): each routine
; is emitted only when referenced EARLIER in the program and not yet defined,
; so .include this file after the last call. Included too early, the build
; stops with "Symbol '<routine>' is undefined" -- never a silent drop.
; at_send_cr comes with at_send_cmd (which falls into it) and at_send_dial
; (its tail call).
; ============================================================================
; Hayes-style AT commands are just ASCII strings ending in CR ($0D —
; NOT the Apple-1 $8D form, the wire wants pure 7-bit ASCII). The
; ESP8266 firmware on the Wi-Fi Modem card recognises:
;
;   AT          ping → "OK"
;   ATDT host:port  dial → "CONNECT" or "NO CARRIER"
;   ATH         hang up
;   ATE0 / ATE1 disable / enable local echo
;   ATI         identify firmware
;   ATZ         reset
;   +++         (1 s guard, no CR) → command mode while connected
;
; Routines:
;
;   at_send_cmd  -- A = lo, X = hi → ASCIIZ command WITHOUT the leading
;                   "AT" or trailing CR. Module prepends "AT", appends
;                   CR. Common case: at_send_cmd points at "Z" → wire
;                   sees "ATZ\r".
;                   Clobbers A, Y. X preserved (acia_send_str preserves X).
;
;   at_send_dial -- A = lo, X = hi → ASCIIZ "host:port". Wire sees
;                   "ATDT<host:port>\r".
;                   Clobbers A, Y. X preserved.
;
;   at_send_cr   -- send a single CR ($0D). Convenience for terminating
;                   custom commands assembled byte-by-byte.
;                   Clobbers A.
;
;   at_recv_until_cr -- receive bytes until CR (or LF) into the caller-
;                       provided buffer at (acia_str_lo). Y=0 starts
;                       fresh. Stores 0 after the last byte. Returns
;                       Y = length (excluding the terminator). Caller
;                       sets up acia_str_lo/hi before the call.
;                       Clobbers A. X preserved.
;
; Caller responsibility:
;   - acia.asm included BEFORE this file (its ZP pair acia_str_lo/hi) and
;     AGAIN AFTER it: this module calls acia_send_byte / acia_send_str /
;     acia_recv_byte, and acia.asm emits a routine only once it has been
;     referenced (see below).
;   - The card must be in command mode (not in the middle of a
;     connection sending raw data) for AT commands to be recognised.
;     +++ guard pulls a connected session back to command mode.
; ============================================================================

.include "acia.inc"

.segment "CODE"

; ----------------------------------------------------------------------------
; at_send_cmd -- send "AT" + (string at A:X) + CR.
; ----------------------------------------------------------------------------
.if .referenced(at_send_cmd) .and (.not .defined(at_send_cmd))
at_send_cmd:
        PHA                             ; stash low ptr byte
        LDA     #'A'
        JSR     acia_send_byte
        LDA     #'T'
        JSR     acia_send_byte
        PLA                             ; restore A = lo ptr
        JSR     acia_send_str           ; send body (NUL-terminated)
.ifdef at_send_cr
        JMP     at_send_cr              ; emitted by an earlier include
.endif                                  ; (otherwise: fall through to it)
.endif

; ----------------------------------------------------------------------------
; at_send_cr -- send a single CR ($0D).
; ----------------------------------------------------------------------------
.if (.referenced(at_send_cr) .or .referenced(at_send_cmd) .or .referenced(at_send_dial)) .and (.not .defined(at_send_cr))
at_send_cr:
        LDA     #$0D
        JMP     acia_send_byte          ; tail call
.endif

; ----------------------------------------------------------------------------
; at_send_dial -- send "ATDT<host:port>\r".
; ----------------------------------------------------------------------------
.if .referenced(at_send_dial) .and (.not .defined(at_send_dial))
at_send_dial:
        PHA                             ; stash low ptr
        LDA     #'A'
        JSR     acia_send_byte
        LDA     #'T'
        JSR     acia_send_byte
        LDA     #'D'
        JSR     acia_send_byte
        LDA     #'T'
        JSR     acia_send_byte
        PLA
        JSR     acia_send_str
        JMP     at_send_cr              ; tail call: append CR
.endif

; ----------------------------------------------------------------------------
; at_recv_until_cr -- read bytes into (acia_str_lo),Y until CR or LF.
;   Caller pre-sets acia_str_lo:hi to the destination buffer.
;   Returns Y = number of bytes received (NOT including the terminator).
;   The buffer has a trailing $00 written after the last byte.
;   Max receive = 254 bytes (Y wraps at 255 → last slot reserved for NUL).
; ----------------------------------------------------------------------------
.if .referenced(at_recv_until_cr) .and (.not .defined(at_recv_until_cr))
at_recv_until_cr:
        LDY     #0
@lp:    JSR     acia_recv_byte
        CMP     #$0D
        BEQ     @done
        CMP     #$0A
        BEQ     @done
        STA     (acia_str_lo),Y
        INY
        CPY     #254
        BCC     @lp                     ; overflow guard
@done:  LDA     #$00
        STA     (acia_str_lo),Y
        RTS
.endif
