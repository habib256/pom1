/*
 * gfx_rect.c — card-neutral rectangle outline. See gfx.h.
 *
 * Up to four spans, interior untouched, each outline pixel written once.
 * Corners normalised so x0<=x1, y0<=y1; flat boxes use one span.
 * Split from the former gfx_draw.c so a rect-only program doesn't drag in gfx_plot
 * (used by line/circle/ellipse) — every span routes through the card's
 * fast hline/vline.
 */
#include "gfx.h"

void gfx_rect(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    if (x0 >= gfx_width || y0 >= gfx_height) return;
    if (x0 == x1) { gfx_vline(x0, y0, y1); return; }
    if (y0 == y1) { gfx_hline(x0, x1, y0); return; }
    gfx_hline(x0, x1, y0);
    if (y1 < gfx_height) gfx_hline(x0, x1, y1);
    /* Horizontal spans already own the corners. Preserve the original
     * outline when the far edges are off-screen; do not move those edges. */
    if ((unsigned)y0 + 1u < (unsigned)y1) {
        gfx_vline(x0, (unsigned char)(y0 + 1u), (unsigned char)(y1 - 1u));
        if (x1 < gfx_width)
            gfx_vline(x1, (unsigned char)(y0 + 1u), (unsigned char)(y1 - 1u));
    }
}
