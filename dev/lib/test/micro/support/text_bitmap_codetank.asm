; ============================================================================
; text_bitmap_codetank.asm — micro-test support: text_bitmap.asm as built for
; a CodeTank cartridge (the module assembles empty without CODETANK_BUILD,
; and the harness passes no -D). Linked through a driver's LIBS: line.
; ============================================================================
CODETANK_BUILD = 1
.include "text_bitmap.asm"
