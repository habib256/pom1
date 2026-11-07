/*
 * gfx_backend_gen2_rect.c — gfx_filled_rect + gfx_clear forwarders for the
 * GEN2 HGR backend.
 *
 * Split from gfx_backend_gen2.c so a GEN2 program that only draws
 * lines/circles/ellipses never references gen2_hgr_fill_box or
 * gen2_hgr_clear, keeping ld65 dead-strip symmetric with the TMS side.
 */
#include "gfx.h"
#define GEN2_NO_APPLE1
#include "gen2.h"          /* -I dev/lib/gen2c */

/* Both x edges go to the asm core as 16-bit values. The width used to be
 * squeezed into gen2_hgr_fill_pixrect's single byte, so
 * gfx_filled_rect(0,0,279,191) drew a 24-px band (280 mod 256). */
void gfx_filled_rect(unsigned x0, unsigned char y0,
                     unsigned x1, unsigned char y1)
{
    gen2_hgr_fill_box(x0, y0, x1, y1);
}

void gfx_clear(unsigned char color) { gen2_hgr_clear(color); }
