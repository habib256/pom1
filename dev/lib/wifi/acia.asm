; ============================================================================
; acia.asm -- ACIA 65C51 byte-level send/receive primitives
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"): each routine
; is emitted only when referenced EARLIER in the program and not yet defined,
; so .include this file after the last call. Included too early, the build
; stops with "Symbol '<routine>' is undefined" -- never a silent drop.
; acia_send_byte comes with acia_send_str (its caller). at.asm calls
; acia_send_byte / acia_send_str / acia_recv_byte: include this file once
; before at.asm (its ZP pair) and again after it (the routines).
; ============================================================================
;   acia_init        -- A = baud rate code (ACIA_BAUD_*), set up 8N1 +
;                       internal clock + DTR enabled. Polled mode (no IRQ).
;                       Clobbers A. X, Y preserved.
;
;   acia_send_byte   -- A = byte. Block on TDRE, transmit. (W65C51N's
;                       TDRE is always set in POM1 mirroring the silicon
;                       bug — the poll is a single-pass no-op but kept
;                       for correctness on real hardware.)
;                       Clobbers A. X, Y preserved.
;
;   acia_recv_byte   -- block until RDRF, return A = byte (RDRF cleared
;                       as side-effect of the read).
;                       Clobbers A. X, Y preserved.
;
;   acia_recv_avail  -- non-blocking poll. Returns:
;                         A = byte, Z=0 if a byte was available
;                         A = 0,    Z=1 if not
;                       The byte is consumed (RDRF cleared) when available.
;                       Clobbers A. X, Y preserved.
;
;   acia_send_str    -- A = lo, X = hi → ASCIIZ string. Sends each byte
;                       via acia_send_byte. Stops on $00. Bytes are
;                       transmitted as-is (no bit-7 OR — the wire wants
;                       7-bit ASCII for AT commands).
;                       Clobbers A, Y. X preserved.
;
; ZP usage: 2 bytes (acia_str_lo / acia_str_hi). Default declared via
; .ifndef so ZP-tight projects can alias to existing scratch slots.
;
; Caller responsibility: acia.inc included (this module includes it).
; ============================================================================

.include "acia.inc"

.ifndef acia_str_lo
.segment "ZEROPAGE"
acia_str_lo:    .res 1
acia_str_hi:    .res 1
.endif

.segment "CODE"

; ----------------------------------------------------------------------------
; acia_init -- A = baud rate code, set 8N1 + DTR.
; ----------------------------------------------------------------------------
.if .referenced(acia_init) .and (.not .defined(acia_init))
acia_init:
        ORA     #ACIA_CTL_8N1_INT       ; OR in 8N1 + internal clock framing
        STA     ACIA_CONTROL
        LDA     #ACIA_CMD_DTR | ACIA_CMD_IRQ_DIS | ACIA_CMD_TX_NORMAL
        STA     ACIA_COMMAND
        ; Drain a possible stale Rx byte by reading data once.
        LDA     ACIA_DATA
        RTS
.endif

; ----------------------------------------------------------------------------
; acia_send_byte -- A = byte, blocks on TDRE, transmits.
; ----------------------------------------------------------------------------
.if (.referenced(acia_send_byte) .or .referenced(acia_send_str)) .and (.not .defined(acia_send_byte))
acia_send_byte:
        PHA
@wait:  LDA     ACIA_STATUS
        AND     #ACIA_ST_TDRE
        BEQ     @wait                   ; TDRE = 0 → still busy
        PLA
        STA     ACIA_DATA
        RTS
.endif

; ----------------------------------------------------------------------------
; acia_recv_byte -- blocks on RDRF, returns A = received byte.
; ----------------------------------------------------------------------------
.if .referenced(acia_recv_byte) .and (.not .defined(acia_recv_byte))
acia_recv_byte:
@wait:  LDA     ACIA_STATUS
        AND     #ACIA_ST_RDRF
        BEQ     @wait                   ; no byte yet
        LDA     ACIA_DATA               ; read consumes RDRF
        RTS
.endif

; ----------------------------------------------------------------------------
; acia_recv_avail -- non-blocking. A = byte (Z=0) or 0 (Z=1).
; ----------------------------------------------------------------------------
.if .referenced(acia_recv_avail) .and (.not .defined(acia_recv_avail))
acia_recv_avail:
        LDA     ACIA_STATUS
        AND     #ACIA_ST_RDRF
        BEQ     @none                   ; no byte
        LDA     ACIA_DATA
        RTS                             ; A = byte (a received $00 is indistinguishable
                                        ; from @none — gate on ACIA_ST_RDRF if NUL is valid)
@none:  LDA     #$00
        RTS                             ; A = 0, Z = 1
.endif

; ----------------------------------------------------------------------------
; acia_send_str -- A=lo, X=hi → ASCIIZ ptr. Sends bytes raw (no bit-7).
; ----------------------------------------------------------------------------
.if .referenced(acia_send_str) .and (.not .defined(acia_send_str))
acia_send_str:
        STA     acia_str_lo
        STX     acia_str_hi
        LDY     #$00
@lp:    LDA     (acia_str_lo),Y
        BEQ     @done
        JSR     acia_send_byte
        INY
        BNE     @lp
@done:  RTS
.endif
