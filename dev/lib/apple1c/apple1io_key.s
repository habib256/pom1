; ----------------------------------------------------------------------------
; apple1io_key.s — apple1_iskeypressed and apple1_readkey (see apple1io.h).
;
; Formerly C in apple1io.c (see apple1io_str.s for why it was split). A
; separate object from apple1io_asm.s, so a program that only prints pays
; nothing for them; apple1_getkey stays in the shim, which
; tools/check_wozmon_shims.py pins against dev/lib/tms9918c/apple1_asm.s.
;
; cc65: a char return leaves in A; the CALLER emits `ldx #0` when it widens
; it, so nothing here clears X.
; ----------------------------------------------------------------------------
        .export _apple1_iskeypressed, _apple1_readkey

KEYCR   = $D011
KEYDATA = $D010

        .segment "CODE"

; unsigned char apple1_iskeypressed(void): $80 if a key is waiting, else 0.
_apple1_iskeypressed:
        lda     KEYCR
        and     #$80            ; [B] MC6821: CRA bit 7 = the CA1 (key strobe) flag
        rts

; unsigned char apple1_readkey(void): 0 if no key, else key & $7F (no wait).
; Reading KEYDATA acknowledges the key, as apple1_getkey does.
_apple1_readkey:
        lda     KEYCR
        and     #$80
        beq     @none           ; A = 0: no key
        lda     KEYDATA
        and     #$7F
@none:  rts
