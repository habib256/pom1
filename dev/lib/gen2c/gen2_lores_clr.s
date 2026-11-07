; gen2_lores_clr.s — void gen2_lores_clear(unsigned char color)
;
; cc65 fastcall: A = colour (low nibble used, both blocks of a byte get it).
; Fills the 960 VISIBLE bytes of the current LORES draw page ($0400 page 1 /
; $0800 page 2, from gen2_lo_base; BSS 0 = unset -> page 1). The 8 bytes at
; the end of each 128-byte region (the "screen holes", 64 per page) are not
; displayed and are left alone — the C version filled the whole 1 KB.
; Eight interleaved absolute stores per index cover offsets 0..119 of every
; 128-byte region: ~4 900 cycles a page, no pointer set-up.
;
; Port of pom2games dev/lib/hgrc/hgr_lores_clear.s (same author, GPL-3.0).

        .export   _gen2_lores_clear
        .import   _gen2_lo_base
        .importzp tmp1

        .code
_gen2_lores_clear:
        and #$0F
        sta tmp1
        asl a
        asl a
        asl a
        asl a
        ora tmp1                ; both nibbles = colour
        ldx _gen2_lo_base
        bne @base
        ldx #$04                ; BSS default: page 1
        stx _gen2_lo_base
@base:  cpx #$08
        beq @page2
        ldy #119
@p1:    sta $0400,y
        sta $0480,y
        sta $0500,y
        sta $0580,y
        sta $0600,y
        sta $0680,y
        sta $0700,y
        sta $0780,y
        dey
        bpl @p1
        rts
@page2: ldy #119
@p2:    sta $0800,y
        sta $0880,y
        sta $0900,y
        sta $0980,y
        sta $0A00,y
        sta $0A80,y
        sta $0B00,y
        sta $0B80,y
        dey
        bpl @p2
        rts
