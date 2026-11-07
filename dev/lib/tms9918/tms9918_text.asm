; ============================================================================
; tms9918_text.asm -- TMS9918 Text Mode F1 (40x24 monochrome) driver
;                     for Apple-1 + P-LAB Graphic Card.
;
; Forks tms9918m1.asm. The TMS9918's Text Mode F1 has the *exact same*
; geometry as the Apple-1 PIA display (40 columns x 24 rows), so it's the
; natural drop-in replacement for the slow $D012-driven character output
; once the charmap is uploaded to the VDP pattern table.
;
; Public symbols:
;   init_vdp_text   -- 8 registers + tail-call disable_sprites_text
;   upload_charmap  -- copy the 1 KB Apple-1 charmap (pre-reversed at build
;                      time, charmap_tms.inc) to the pattern table at $0000.
;                      Display is blanked during the burst (14c a byte).
;   clear_screen_text -- fill 960 bytes at $0800 with $20 (space)
;   vdp_set_write   -- prep VRAM auto-increment write at vdp_lo:hi
;   vdp_set_read    -- prep VRAM read at vdp_lo:hi
;   name_at_rc_text -- load vdp_lo:hi with name-table addr for
;                      (vdp_row, vdp_col) -- 40-col arithmetic
;   print_at_rc_text-- write char A at (vdp_row, vdp_col) -- full sequence
;
; Owns ZP slots: vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col.
;                Same layout as tms9918m1.asm so a project never links both.
;
; Imports (caller must define):
;   tmp                 -- 1 ZP scratch byte (upload_charmap's copy count).
;
; Memory map (Text Mode F1, fixed by the register table below):
;   $0000-$03FF  Pattern table (1 KB = 128 chars * 8 bytes)
;   $0800-$0BBF  Name table   (40 cols * 24 rows = 960 bytes)
;   (no sprites, no colour table -- text mode uses R7 for fg/bg)
;
; Silicon-strict timing: data stores go through vdp_write_a / vdp_fill
; (22c store-to-store, the pad18 contract) except upload_charmap's 1 KB
; burst, which runs with the display blanked (R1 = $80, a free zone) through
; vdp_copy_blank at 14c a byte. The private helpers come from
; tms9918_xfer.asm, included at the end of this file.
; `-D TMS_PAIR_SEI=1` masks IRQs around vdp_set_write/read's control pair.
;
; Font: charmap_tms.inc is roms/charmap.rom bit-reversed by
; tools/build_charmap_tms.py (Apple-1 ROM: bit 0 = leftmost pixel; TMS9918:
; bit 7). Reversing it at build time instead of per byte at run time cut
; upload_charmap from ~165 000 to ~14 500 cycles.
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

; --- exported ZP slots -----------------------------------------------------
.exportzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col

; --- imports ---------------------------------------------------------------
.importzp tmp

; --- exported routines -----------------------------------------------------
.export init_vdp_text, upload_charmap, clear_screen_text
.export vdp_set_write, vdp_set_read
.export name_at_rc_text, print_at_rc_text

; --- ZP layout -------------------------------------------------------------
.segment "ZEROPAGE"
vdp_lo:         .res 1
vdp_hi:         .res 1
vdp_src_lo:     .res 1
vdp_src_hi:     .res 1
vdp_row:        .res 1
vdp_col:        .res 1

; ============================================================================
.segment "CODE"
; ============================================================================

; ----------------------------------------------------------------------------
; init_vdp_text: write 8 Text-Mode-F1 registers.
;   Display is kept OFF during the register pass (R1 committed as $90 —
;   the $D0 table value with bit 6 masked): the pattern and name tables
;   are still power-on garbage at this point, and real DRAM flashes that
;   garbage on screen if the display is enabled first (blank-first
;   discipline, mirrors init_vdp_g1/g2 and basicrt's lr_text_mode).
;   Call enable_text_display (or let upload_charmap's tail do it) once
;   the tables are filled.
; ----------------------------------------------------------------------------
init_vdp_text:
        LDX     #0
@rg:    LDA     vdpt_regs,X
        CPX     #1
        BNE     @nomask
        AND     #$BF            ; R1: keep display OFF over uninit VRAM
@nomask:
        JSR     vdp_ctrl_a      ; value (22c to the command byte)
        TXA
        ORA     #$80
        JSR     vdp_ctrl_a      ; command: register X
        INX
        CPX     #8
        BNE     @rg
        RTS

; ----------------------------------------------------------------------------
; upload_charmap: copy the 1024-byte Apple-1 charmap to pattern table $0000.
;   The table is already in TMS order (bit 7 = leftmost pixel: charmap_tms.inc,
;   generated from roms/charmap.rom), so the bytes go straight to VRAM. The
;   display is blanked for the burst (a free zone: ScreenOff access slots),
;   so the copy runs unpadded at 14c a byte -- ~14 500 cycles in all, against
;   ~165 000 when every byte was bit-reversed (8 LSR/ROL rounds) and padded.
;
;   Clobbers: A, X, Y, tmp, vdp_src_lo/hi.
; ----------------------------------------------------------------------------
upload_charmap:
        ; 1) Blank display (R1 = $80: 16K=1, blank=0/screen off, M1=0 — the
        ;    chip idles in blanked Graphics I during the burst; the mode
        ;    bits come back with the $D0 re-arm at the end).
        LDA     #$80
        JSR     vdp_ctrl_a
        LDA     #$81            ; reg 1
        JSR     vdp_ctrl_a
        ; 2) Set VRAM write pointer to $0000 (pattern table base).
        LDA     #$00
        JSR     vdp_ctrl_a
        LDA     #$40            ; $00 | $40 = write at $0000
        JSR     vdp_ctrl_a
        ; 3) Stream 1024 bytes (display blanked: no pad).
        LDA     #<tms_charmap
        STA     vdp_src_lo
        LDA     #>tms_charmap
        STA     vdp_src_hi
        LDX     #>1024          ; X:A = 1024
        LDA     #<1024
        JSR     vdp_copy_blank
        ; 4) Re-enable display (R1 = $D0).
        LDA     #$D0
        JSR     vdp_ctrl_a
        LDA     #$81
        JMP     vdp_ctrl_a      ; tail call

; ----------------------------------------------------------------------------
; clear_screen_text: fill the 960-byte name table at $0800 with $20 (space).
;   Display may be on: padded fill, 27c a byte. Clobbers: A, X, Y.
; ----------------------------------------------------------------------------
clear_screen_text:
        LDA     #$00
        JSR     vdp_ctrl_a
        LDA     #$48            ; $08 | $40 = write at $0800
        JSR     vdp_ctrl_a
        LDA     #$20            ; space
        LDX     #>960           ; X:Y = 960
        LDY     #<960
        JMP     vdp_fill

; ----------------------------------------------------------------------------
; vdp_set_write: prep VDP for auto-incrementing writes at vdp_lo:hi.
; ----------------------------------------------------------------------------
vdp_set_write:
        TMS_PAIR_BEGIN
        LDA     vdp_lo
        JSR     vdp_ctrl_a
        LDA     vdp_hi
        ORA     #$40            ; bit 6 = write
        JSR     vdp_ctrl_a      ; returns 16c after the cmd byte (was 6c)
        TMS_PAIR_END
        RTS

; ----------------------------------------------------------------------------
; vdp_set_read: prep VDP for reads at vdp_lo:hi.
; ----------------------------------------------------------------------------
vdp_set_read:
        TMS_PAIR_BEGIN
        LDA     vdp_lo
        JSR     vdp_ctrl_a
        LDA     vdp_hi          ; bit 6 = 0 -> read
        JSR     vdp_ctrl_a
        TMS_PAIR_END
        RTS

; ----------------------------------------------------------------------------
; name_at_rc_text: compute name-table VRAM address for (vdp_row, vdp_col),
;   store in vdp_lo:hi (vdp_set_write/read OR in the bit 6 write flag later).
;     addr = $0800 + 40 * row + col
;   Inputs:   vdp_row (0..23), vdp_col (0..39)
;   Output:   vdp_lo:hi loaded.
;   Clobbers: A, X.
;
;   Now LUT-driven (was: 32*row + 8*row shift-and-add, ~46 cycles). 24-entry
;   tables pre-bake `$0800 + 40*row`; the call cost drops to ~21 cycles. Over
;   a full console_scroll (23 row reads + 23 row writes + 1 blank = 47 calls)
;   that is ~1175 cycles saved per scroll. Net ROM: +24 bytes (48-byte LUT
;   minus ~24 bytes of dropped arithmetic + the freed `tmp` clobber).
; ----------------------------------------------------------------------------
name_at_rc_text:
        LDX     vdp_row
        LDA     name_row_lo,X
        CLC
        ADC     vdp_col
        STA     vdp_lo
        LDA     name_row_hi,X
        ADC     #0              ; just propagate the col-carry
        STA     vdp_hi
        RTS

; 24-entry tables: name_row_lo[i] / name_row_hi[i] = lo/hi of ($0800 + 40*i).
name_row_lo:
        .byte $00, $28, $50, $78, $A0, $C8, $F0, $18
        .byte $40, $68, $90, $B8, $E0, $08, $30, $58
        .byte $80, $A8, $D0, $F8, $20, $48, $70, $98
name_row_hi:
        .byte $08, $08, $08, $08, $08, $08, $08, $09
        .byte $09, $09, $09, $09, $09, $0A, $0A, $0A
        .byte $0A, $0A, $0A, $0A, $0B, $0B, $0B, $0B

; ----------------------------------------------------------------------------
; print_at_rc_text: write char A at (vdp_row, vdp_col) -- full sequence.
;   Caller is responsible for col<40 / row<24 bounds. The console runtime
;   on top of this driver handles wrap and scroll.
; ----------------------------------------------------------------------------
print_at_rc_text:
        PHA                     ; save char
        JSR     name_at_rc_text
        JSR     vdp_set_write
        PLA                     ; vdp_set_write returned 22c after its cmd
        JMP     vdp_write_a     ; byte: store lands 29c after it, then the
                                ; helper's 12c pad + RTS

; ============================================================================
; Text Mode F1 register table.
;
;   Reg 0: $00  External video off, M3=0.
;   Reg 1: $D0  M1=1 (text mode), screen on, IRQ off, 16K, no sprites.
;   Reg 2: $02  Name table at $0800 ($02 << 10).
;   Reg 3: $00  (colour table unused in F1)
;   Reg 4: $00  Pattern table at $0000.
;   Reg 5: $00  (sprite attribute table unused in F1)
;   Reg 6: $00  (sprite pattern table unused in F1)
;   Reg 7: $F0  fg = white (15), bg = black (0).
; ============================================================================
vdpt_regs:
        .byte   $00, $D0, $02, $00, $00, $00, $00, $F0

; ============================================================================
; tms_charmap: 1024-byte 8x8 monochrome font = roms/charmap.rom (the Apple-1
;   character set, 128 glyphs) with every byte bit-reversed at build time by
;   tools/build_charmap_tms.py, so bit 7 = leftmost pixel as the TMS9918
;   reads it. Lives inside the driver so any project linking it gets the
;   font for free (1 KB of CODE).
; ============================================================================
.include "charmap_tms.inc"

; ----------------------------------------------------------------------------
; Private port helpers + fill/copy (only the referenced ones assemble).
; ----------------------------------------------------------------------------
VDP_XFER_PTR    = vdp_src_lo
.include "tms9918_xfer.asm"
