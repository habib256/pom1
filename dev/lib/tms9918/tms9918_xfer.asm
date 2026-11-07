; ============================================================================
; tms9918_xfer.asm -- store-and-pad helpers + 16-bit VRAM fill/copy
; ============================================================================
; INCLUDE-STYLE fragment (not a standalone object): `.include` it at the END
; of a module, after every reference. Each routine is `.ifref`-guarded, so a
; module assembles -- and pays bytes for -- only the routines it calls (an
; `.export` counts as a reference). The labels stay local to the including
; object: tms9918m1/m2/_text/text_bitmap each carry a private copy of what they
; use, so no new object enters any consumer's link. tms9918_helpers.asm
; includes it to export vdp_write_a / vdp_ctrl_a to application code.
;
; No self-modifying code: safe in CodeTank / Juke-Box ROM banks.
;
; Requires from the including module:
;   VDP_DATA / VDP_CTRL  (tms9918.inc)
;   tmp                  1 ZP scratch byte      -- vdp_copy*, vdp_copy_short
;   VDP_XFER_PTR         a 2-byte ZP pointer    -- vdp_copy*, vdp_copy_short
;                        (define it BEFORE this include, e.g.
;                         `VDP_XFER_PTR = vdp_src_lo`)
;
; Routines (all leave the VDP address auto-incrementing; set it first):
;   vdp_write_a     A -> VDP_DATA, then pad. JSR+STA+3 NOP+RTS: chained back
;                   to back the stores are 22 cycles apart (the pad18
;                   contract). A/X/Y and flags preserved. 3-byte call site
;                   instead of the 6-byte `STA VDP_DATA / JSR tms9918_pad18`,
;                   same 22 cycles.
;   vdp_ctrl_a      A -> VDP_CTRL, same pad, same preservation.
;     CAVEAT: when either helper RETURNS, only 16 cycles have passed since
;     its store began (4 STA + 6 NOP + 6 RTS). A following INLINE
;     `STA VDP_DATA` needs >= 6 more cycles of other instructions first;
;     another JSR vdp_write_a needs nothing (its own JSR is the 6 cycles).
;     tools/lint_tms_lib.py models both helpers and catches the mixed style.
;   vdp_fill        A = byte, X:Y = count (X = high byte, 0..$7F; Y = low).
;                   Padded, 27 cycles a byte. A kept; X = $FF, Y = 0 after.
;   vdp_fill_blank  Same, unpadded: 9 cycles a byte. Display blanked or
;                   VBlank only (free zone).
;   vdp_copy        X:A = count (X = high byte), source (VDP_XFER_PTR).
;                   Padded, 35 cycles a byte. VDP_XFER_PTR+1 advances by X;
;                   tmp = count low byte. Clobbers A, X, Y.
;   vdp_copy_short  A = count (1..255, 0 = 256), source (VDP_XFER_PTR).
;                   Padded, 35 cycles a byte. X preserved; tmp = count.
;                   (The tail of vdp_copy; tms9918m1's vdp_upload_a.)
;   vdp_copy_blank  X:A = count, unpadded: 14 cycles a byte. Display blanked
;                   only (free zone). VDP_XFER_PTR+1 advances by X; X = 0
;                   after, tmp = count low byte.
;
; Timing sources:
; [A] 22c store-to-store in active Mode I/II display -- measured on Claudio
;     Parmigiani's Replica-1 (drop floor ~16c; see tms9918.inc).
; [D] blanked display serves a CPU slot every ~8 ticks (~2 CPU cycles):
;     openMSX VDPAccessSlots.cc slotsMsx1ScreenOff, as copied into POM1's
;     src/TMS9918.cpp (the upstream revision was not recorded at the copy;
;     settled by citing it there). The 9c/14c unpadded loops clear it 4x over.
; The helper shape (STA port / NOP x3 / RTS) and the fill/copy loops are a
; port of pom2games dev/lib/apple2/ezcolor.asm (ez_data_w, ez_ctrl_w,
; ez_fill, ez_fill_blank, ez_copy, ez_copy_blank), itself derived from this
; library.
;
; This file uses no @cheap locals: a normal label inside it would end the
; including module's cheap-local scope anyway, so every label is prefixed.
; ============================================================================

; --- composite routines first: their references pull in the primitives ----

.ifref vdp_fill
; A = byte, X:Y = count. JSR vdp_write_a (22) + DEY (2) + BNE (3) = 27c/byte.
vdp_fill:
        CPY     #0
        BEQ     vxf_page
vxf_loop:
        JSR     vdp_write_a
        DEY
        BNE     vxf_loop
vxf_page:
        DEX
        BPL     vxf_loop        ; X full pages of 256 (Y = 0)
        RTS
.endif

.ifref vdp_fill_blank
; Display blanked / VBlank only: STA (4) + DEY (2) + BNE (3) = 9c/byte, far
; above the ~2c ScreenOff drain but below the active-display floor.
vdp_fill_blank:
        CPY     #0
        BEQ     vxb_page
vxb_loop:               ; lint: free-zone caller contract: display blanked or VBlank
        STA     VDP_DATA        ; no pad: display blanked (free zone)
        DEY
        BNE     vxb_loop
vxb_page:
        DEX
        BPL     vxb_loop
        RTS
.endif

.ifref vdp_copy
; X:A = count. Full pages first, then the low-byte remainder through
; vdp_copy_short (which treats a count of 0 as 256 -- hence the BEQ).
vdp_copy:
        STA     tmp
        LDY     #0
        TXA
        BEQ     vxc_tail
vxc_page:
        LDA     (VDP_XFER_PTR),Y
        JSR     vdp_write_a     ; 5 + 22 + 2 + 3 = 32c/byte
        INY
        BNE     vxc_page
        INC     VDP_XFER_PTR+1
        DEX
        BNE     vxc_page
vxc_tail:
        LDA     tmp
        BEQ     vxs_done        ; no remainder
        ; fall through: vdp_copy_short copies the remaining A bytes
.endif

.if .referenced(vdp_copy) .or .referenced(vdp_copy_short)
vdp_copy_short:
        STA     tmp             ; count (0 -> 256 by wrap)
        LDY     #0
vxs_loop:
        LDA     (VDP_XFER_PTR),Y
        JSR     vdp_write_a     ; 5 + 22 + 2 + 3 + 3 = 35c/byte
        INY
        CPY     tmp
        BNE     vxs_loop
vxs_done:
        RTS
.endif

.ifref vdp_copy_blank
; Display blanked only: LDA (5) + STA (4) + INY (2) + BNE (3) = 14c/byte.
vdp_copy_blank:
        STA     tmp
        LDY     #0
        TXA
        BEQ     vxk_tail
vxk_page:               ; lint: free-zone caller contract: display blanked
        LDA     (VDP_XFER_PTR),Y
        STA     VDP_DATA        ; no pad: display blanked (free zone)
        INY
        BNE     vxk_page
        INC     VDP_XFER_PTR+1
        DEX
        BNE     vxk_page
vxk_tail:
        LDX     tmp
        BEQ     vxk_done
vxk_rest:               ; lint: free-zone caller contract: display blanked
        LDA     (VDP_XFER_PTR),Y
        STA     VDP_DATA        ; no pad: display blanked (free zone)
        INY
        DEX
        BNE     vxk_rest
vxk_done:
        RTS
.endif

; --- primitives -------------------------------------------------------------
; NOP filler only: registers and flags survive (callers branch on a compare
; made before the call, exactly as with tms9918_pad18).

.ifref vdp_write_a
vdp_write_a:
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        NOP                     ; 2c
        NOP                     ; 2c
        RTS                     ; 6c -- + the caller's 6c JSR = 22c chained
.endif

.ifref vdp_ctrl_a
vdp_ctrl_a:
        STA     VDP_CTRL        ; 4c
        NOP
        NOP
        NOP
        RTS                     ; same 22c chained
.endif
