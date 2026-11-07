; ============================================================================
; hgr_clear_rows.asm -- clear visible HGR scanlines, keep the HUD and the holes
; ============================================================================
; Clears the 40 visible bytes of scanlines on the DRAW page and nothing else:
; the 8 "screen holes" at the end of each 128-byte group ($xx78-$xx7F,
; $xxF8-$xxFF) survive (a program may keep data there), and so does the
; other page. Faster than clear_hgr where only part of the screen moves.
;
;   hgr_clear_rows         A = first scanline, X = end (exclusive);
;                          0 <= first < end <= 192. ~350 cycles a row
;                          (40 unrolled STA (zp),Y per row; 3 rows: 1 085
;                          cycles with the call).
;   hgr_clear_visible      scanlines 0..191, 39 550 cycles (clear_hgr
;                          writes all 8 KB in 46 345 and wipes the holes).
;   hgr_clear_viewport160  scanlines 0..159: a game area above a 32-line
;                          HUD, which is left alone. 33 080 cycles.
;   hgr_clear_hud32        scanlines 160..191 only. 6 835 cycles.
; (Measured on a scratch 6502 core, JSR/RTS included.) The last three run
; one unrolled block of 64 + 32 absolute STA abs,Y whose operands are
; patched (EOR #PAGE2_EOR, 96 stores, ~2 100 cycles) only when the draw
; page changed since the previous call. They leave hc_* alone.
;
; The draw page:
;   - default: hgr_draw_page from hgr_flip.asm (0 / PAGE2_EOR) -- the
;     viewport block follows it, hgr_clear_rows follows the flipped hgr_hi;
;   - HGR_CLEAR_ROWS_PAGE = <byte> defined before the include: a selector
;     byte of the program's own, $00 / $60, over FIXED tables (the
;     ht_page / bl_page convention of hgr_text8 / hgr_blit2). The viewport
;     block follows it, and hgr_clear_rows EORs it into hgr_hi (+3 B, +3
;     cycles a row).
;
; SELF-MODIFYING (patched store operands and a patched JMP): RAM only, never
; in a CodeTank / Juke-Box cartridge running from ROM. Not reentrant.
; Clobbers A, X, Y (and hc_row/hc_end/hc_ptr for hgr_clear_rows). Zero page
; 4 B (hc_row, hc_end, hc_ptr[2]), skipped if hc_row is defined before the
; include (then define all four). Code 562 B.
;
; Ported from the pom2games Apple II edition (lib/hgr/hgr_clear_rows.asm,
; extracted there from Maze3D); the viewport labels gained an hcr_ prefix.
; ============================================================================
.ifndef _HGR_CLEAR_ROWS_LOADED_
_HGR_CLEAR_ROWS_LOADED_ = 1

.ifndef PAGE2_EOR
PAGE2_EOR = $60
.endif
.ifdef HGR_CLEAR_ROWS_PAGE
_HCR_SELECTOR_ = 1
.else
_HCR_SELECTOR_ = 0
HGR_CLEAR_ROWS_PAGE = hgr_draw_page
.endif

.pushseg
.zeropage
.ifndef hc_row
hc_row: .res 1
hc_end: .res 1
hc_ptr: .res 2
.endif
.popseg

hgr_clear_viewport160:
        ; Column-major absolute stores limited to 160 rows. The first 64
        ; addresses cover two thirds (Y = 0..79), then 32 addresses cover
        ; rows 128..159 (Y = 80..119). HUD and holes survive.
        JSR hcr_sync
        LDA #<hcr_first
        LDX #>hcr_first
        JSR hcr_set_loop
        LDX #80
        JSR hcr_run
        LDY #80
        LDX #40
hcr_last:
.repeat 32, I
        STA $2000 + (I .mod 8) * $400 + (I / 8) * $80,Y
.endrepeat
        INY
        DEX
        BNE hcr_last
        RTS
hgr_clear_visible:
        ; The 64 addresses of one third for Y = 0..119: all three thirds.
        JSR hcr_sync
        LDA #<hcr_first
        LDX #>hcr_first
        JSR hcr_set_loop
        LDX #120
        BNE hcr_run
hgr_clear_hud32:
        ; Rows 160..191 = the 32 last addresses of the 64 for Y = 80..119:
        ; enter the block and loop back at its half.
        JSR hcr_sync
        LDA #<hcr_half
        LDX #>hcr_half
        JSR hcr_set_loop
        LDY #80
        LDX #40
        LDA #0
        JMP hcr_loop
hcr_set_loop:
        STA hcr_loop+1
        STX hcr_loop+2
        LDY #0
        RTS
hcr_run:
        LDA #0
hcr_first:
.repeat 64, I
.if I = 32
hcr_half:
.endif
        STA $2000 + (I .mod 8) * $400 + (I / 8) * $80,Y
.endrepeat
        INY
        DEX
        BEQ hcr_done
hcr_loop:
        JMP hcr_first           ; operand patched: hcr_first / hcr_half
hcr_done:
        RTS
hcr_sync:
        ; Point the 96 absolute stores at the draw page (on a change only).
        LDA HGR_CLEAR_ROWS_PAGE
        CMP hcr_page
        BEQ @ready
        STA hcr_page
        LDX #0
@patch64:
        LDA hcr_first+2,X
        EOR #PAGE2_EOR
        STA hcr_first+2,X
        INX
        INX
        INX
        CPX #192
        BCC @patch64
        LDX #0
@patch32:
        LDA hcr_last+2,X
        EOR #PAGE2_EOR
        STA hcr_last+2,X
        INX
        INX
        INX
        CPX #96
        BCC @patch32
@ready: RTS
hcr_page: .byte 0               ; the page the stores address now

hgr_clear_rows:
        STA hc_row              ; current scanline
        STX hc_end              ; end scanline (exclusive)
@row:   LDY hc_row
        LDA hgr_lo,Y
        STA hc_ptr
        LDA hgr_hi,Y
.if _HCR_SELECTOR_
        EOR HGR_CLEAR_ROWS_PAGE
.endif
        STA hc_ptr+1
        LDA #0
        LDY #39
        ; Forty stores are fixed for every scanline. Unrolling removes
        ; 39 taken branches per row.
.repeat 39
        STA (hc_ptr),Y
        DEY
.endrepeat
        STA (hc_ptr),Y
        INC hc_row
        LDA hc_row
        CMP hc_end
        BEQ @done
        JMP @row
@done:
        RTS

.endif  ; _HGR_CLEAR_ROWS_LOADED_
