; =============================================
; hgr_clear.asm - clear the GEN2 HGR framebuffer
; =============================================
; clear_hgr     zero $2000-$3FFF (page 1, 8 KB). ~46 400 cycles.
; hgr_fill_page A = fill byte, X = first page high byte ($20 = page 1,
;               $40 = page 2): fill those 8 KB with A. ~46 400 cycles.
; Both: no zero page; A kept, X = first page + $20, Y = 0 on exit (the old
; ptr-based loop left the same A/X/Y; it also trashed ptr_lo/ptr_hi, which
; this one leaves alone -- callers may still declare them, nothing reads
; them here any more).
;
; HGR_CLEAR_LOOP [groups] (macro, usable on its own): fill groups x 8 pages
; (default 4 = 8 KB) from page X with the byte in A. Eight absolute
; STA abs,Y per turn whose high operand bytes are patched once per group of
; eight pages: 4 groups x 256 turns x (8 x 5 + 5) = ~46 000 cycles, against
; ~90 000 for the STA (zp),Y / INY / BNE loop this replaced (11 cycles a
; byte). 79 bytes for clear_hgr + hgr_fill_page, against 23.
;   HGR_CLEAR_LOOP 8 with X = $20 fills both pages ($2000-$5FFF), ~92 000.
; HGR_CLEAR_SMALL = 1 before the include keeps the old 23-byte ptr loop
; (no hgr_fill_page; needs and trashes ptr_lo/ptr_hi): for a program that
; cannot spare 56 bytes (HGR_Bestiary).
;
; SELF-MODIFYING: the loop patches its own STA operands, so this module
; must run from RAM. Never .include it into code a CodeTank / Juke-Box
; cartridge runs in place from ROM (no ROM program includes it today).
;
; Assembled on demand (dev/lib/README.md, "Assembled on demand"): clear_hgr
; and hgr_fill_page are emitted only when referenced EARLIER in the program
; and not yet defined, so .include this file after the last call (the macro
; and the switch above are available from the first include, which may come
; early: include it again at the end). Included too early, the build stops
; with "Symbol 'clear_hgr' is undefined" -- never a silent drop. clear_hgr
; brings hgr_fill_page (it falls into it).
;
; Ported from the pom2games Apple II edition (lib/apple2/hgr.asm
; HGR_CLEAR_LOOP, lib/hgr/hgr_clear.asm); the framebuffer layout is the same.
; =============================================

.ifndef _HGR_CLEAR_LOADED_
_HGR_CLEAR_LOADED_ = 1

.macro HGR_CLEAR_LOOP groups
        .local @grp, @lp, @s0, @s1, @s2, @s3, @s4, @s5, @s6, @s7, @cnt, @n
.ifblank groups
        @n = 4                  ; groups of 8 pages (4 = 8 KB, one HGR page)
.else
        @n = groups             ; e.g. 8: both HGR pages from X = $20
.endif
        LDY     #@n
        STY     @cnt+1
@grp:   STX     @s0+2
        INX
        STX     @s1+2
        INX
        STX     @s2+2
        INX
        STX     @s3+2
        INX
        STX     @s4+2
        INX
        STX     @s5+2
        INX
        STX     @s6+2
        INX
        STX     @s7+2
        INX
        LDY     #0
@lp:
@s0:    STA     $2000,Y
@s1:    STA     $2100,Y
@s2:    STA     $2200,Y
@s3:    STA     $2300,Y
@s4:    STA     $2400,Y
@s5:    STA     $2500,Y
@s6:    STA     $2600,Y
@s7:    STA     $2700,Y
        INY
        BNE     @lp
@cnt:   LDY     #@n
        DEY
        STY     @cnt+1
        BNE     @grp
.endmacro

.ifndef HGR_CLEAR_SMALL
HGR_CLEAR_SMALL = 0
.endif

.endif  ; _HGR_CLEAR_LOADED_

; The routines: outside the load guard, each emitted at the first include
; that follows a reference to it, in the caller's current segment.
.if HGR_CLEAR_SMALL
; HGR_CLEAR_SMALL = 1: the original 23-byte loop, for a program with no room
; for the 56 extra bytes (HGR_Bestiary fills its 4 KB to the byte). ~90 500
; cycles, needs ZP ptr_lo / ptr_hi (trashed), no hgr_fill_page.
.if .referenced(clear_hgr) .and (.not .defined(clear_hgr))
clear_hgr:
        LDA #$00
        TAY
        STA ptr_lo
        LDX #$20
        STX ptr_hi
@clr:   STA (ptr_lo),Y
        INY
        BNE @clr
        INC ptr_hi
        LDX ptr_hi
        CPX #$40
        BNE @clr
        RTS
.endif
.else
.if .referenced(clear_hgr) .and (.not .defined(clear_hgr))
clear_hgr:
        LDA #$00
        LDX #$20
.ifdef hgr_fill_page
        JMP hgr_fill_page       ; emitted by an earlier include
.endif                          ; (otherwise it follows: fall through)
.endif
.if (.referenced(hgr_fill_page) .or .referenced(clear_hgr)) .and (.not .defined(hgr_fill_page))
hgr_fill_page:
        HGR_CLEAR_LOOP
        RTS
.endif
.endif
