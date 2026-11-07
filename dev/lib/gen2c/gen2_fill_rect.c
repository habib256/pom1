/* gen2_fill_rect.c — byte-aligned rectangle fill (gen2_hgr_fill_rect).
 * Split from gen2_rect.c so a program that only byte-fills (HUD rules,
 * playfield erase) does not link the pixel-rect, cell or colorize paths, and
 * vice versa. Inner loop: gen2_fill_rect_asm.s. */

#include "gen2.h"
#include "gen2_internal.h"

/* Fast rectangular byte-fill (assembly inner loop). See gen2.h. Clips the
 * rectangle to the screen, then hands the on-screen part to gen2_fill_rect_asm. */
void gen2_hgr_fill_rect(unsigned char y0, unsigned char rows,
                        unsigned char col0, unsigned char ncols,
                        unsigned char val)
{
    gen2_build_tables();
    if (rows == 0u || ncols == 0u || y0 > 191u || col0 >= 40u) return;
    if ((unsigned)y0   + rows  > 192u) rows  = (unsigned char)(192u - y0);
    if ((unsigned)col0 + ncols >  40u) ncols = (unsigned char)(40u  - col0);
    gen2_f_y0   = y0;
    gen2_f_rows = rows;
    gen2_f_col0 = col0;
    gen2_f_cols = ncols;
    gen2_f_val  = val;
    gen2_fill_rect_asm();
}
