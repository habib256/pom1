; =============================================
; hgr_tables.inc - HGR pixel routines + lookup tables (umbrella bundle)
; GEN2 Color Graphics Card (280x192, Apple II layout)
; =============================================
; Convenience bundle: pulls in plot_pixel + clear_hgr + all four lookup tables
; in the original order. plot_pixel and clear_hgr are assembled on demand
; (dev/lib/README.md, "Assembled on demand"): include this bundle AFTER the
; code that calls them, or the build stops with "Symbol ... is undefined".
;
; To adopt only PART of it, include the modules directly instead:
;   hgr_clear.asm        - clear_hgr            (needs ZP ptr_lo/ptr_hi)
;   hgr_scanline.inc     - hgr_lo / hgr_hi tables
;   hgr_plot_tables.inc  - hgr_col / hgr_mask tables
;   hgr_plot.asm         - plot_pixel           (needs cur_x/cur_y/ptr + both table sets)
;
; Required zero-page for the full bundle (declare in your program):
;   cur_x, cur_y, ptr_lo, ptr_hi
;
; (umul8 lives in dev/lib/m6502/multiply.asm - include it separately if needed.)
; =============================================
.include "hgr_plot.asm"
.include "hgr_clear.asm"
.include "hgr_scanline.inc"
.include "hgr_plot_tables.inc"
