/* gen2_rect.c — pixel-aligned rectangle fills (fill/clear_pixrect, fill_box).
 *
 * Routes through gen2_pixrect_asm.s for the inner loop. The C side clips the
 * rectangle and stuffs the parameter block; the asm derives byte columns +
 * edge masks and applies (byte & keep) | set on each scanline. The other
 * RECT-family entries are their own modules so the archive links only what a
 * program calls: gen2_fill_rect.c (byte-aligned fill), gen2_cell.c (8x8-grid
 * cell) and gen2_colorize.c (NTSC artifact tint). */

#include "gen2.h"
#include "gen2_internal.h"

/* Shared core for fill_pixrect (set=1, white) and clear_pixrect (set=0, erase).
 * The C side only clips the rectangle to the screen and picks the mode; the asm
 * derives the byte columns and edge masks (col7/mask7 LUTs) and does the fill. */
static void gen2_pixrect(unsigned x, unsigned char y,
                         unsigned char w, unsigned char h, unsigned char set)
{
    unsigned xr;
    gen2_build_tables();
    if (w == 0u || h == 0u || x > 279u || y > 191u) return;
    xr = x + (unsigned)w - 1u;                 /* rightmost pixel               */
    if (xr > 279u) xr = 279u;                  /* clip right edge               */
    if ((unsigned)y + h > 192u) h = (unsigned char)(192u - y);   /* clip bottom */
    gen2_r_x    = x;
    gen2_r_xr   = xr;
    gen2_r_y0   = y;
    gen2_r_rows = h;
    gen2_r_mode = set;
    gen2_pixrect_asm();
}

/* Fill the inclusive box (x0,y0)-(x1,y1), corners in any order, through the
 * same asm core. Both x edges are 16-bit, so a box may span the full 280-px
 * width in one call -- gen2_hgr_fill_pixrect's width is a single byte, which
 * is what truncated gfx_filled_rect(0,0,279,191) to a 24-px band and made
 * gen2_hgr_hline split its runs into 255-px chunks. */
void gen2_hgr_fill_box(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    gen2_build_tables();
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    if (x0 > 279u || y0 > 191u) return;
    if (x1 > 279u) x1 = 279u;
    if (y1 > 191u) y1 = 191u;
    gen2_r_x    = x0;
    gen2_r_xr   = x1;
    gen2_r_y0   = y0;
    gen2_r_rows = (unsigned char)(y1 - y0 + 1u);
    gen2_r_mode = 1u;
    gen2_pixrect_asm();
}

void gen2_hgr_fill_pixrect(unsigned x, unsigned char y, unsigned char w, unsigned char h)
{
    gen2_pixrect(x, y, w, h, 1u);
}

void gen2_hgr_clear_pixrect(unsigned x, unsigned char y, unsigned char w, unsigned char h)
{
    gen2_pixrect(x, y, w, h, 0u);
}
