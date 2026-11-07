/* gen2_cell.c — 8x8-grid cell fill/erase (gen2_hgr_cell). Split from
 * gen2_rect.c; the asm (gen2_cell_asm.s) jumps into the pixel-rect core. */

#include "gen2.h"
#include "gen2_internal.h"

/* Fill (set=1, white) or erase (set=0) a 6x6 block inside the 8x8 grid cell
 * (cx, cy). One call: the cx*8 / cy*8 happen in asm (no cc65 aslax3 16-bit
 * shift helper) and the pixrect clip is skipped (grid cells are always
 * on-screen). For 8px-grid games (Snake, tiles) that draw one cell at a time. */
void gen2_hgr_cell(unsigned char cx, unsigned char cy, unsigned char set)
{
    gen2_build_tables();
    gen2_c_cx  = cx;
    gen2_c_cy  = cy;
    gen2_c_set = set;
    gen2_cell_asm();
}
