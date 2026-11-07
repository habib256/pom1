/*
 * P-LAB TMS9918 (Apple-1) — gfx layer backend (Graphics II bitmap)
 * Hardware: P-LAB TMS9918 graphic card, Claudio Parmigiani (P-LAB).
 * Software: forwards to screen2 via Antonino "Nino" Porcino's tms9918c runtime
 *   (apple1-videocard-lib, https://github.com/nippur72/apple1-videocard-lib).
 *
 * gfx_backend_tms_rect.c — gfx_filled_rect + gfx_clear forwarders for the
 * TMS9918 bitmap backend.
 *
 * Split from gfx_backend_tms.c so a TMS program that only calls
 * line/circle/ellipse never references screen2_filled_rect or screen2_clear,
 * which lets ld65 dead-strip screen2_ext.c (and the burst-fill asm in
 * tms_fast.s it pulls). A program that DOES call gfx_filled_rect or
 * gfx_clear opts in by linking screen2_ext.c (which it would already need
 * anyway to call the underlying screen2 helpers).
 */
#include "gfx.h"
#include "screen2.h"       /* -I dev/lib/tms9918c */

void gfx_filled_rect(unsigned x0, unsigned char y0,
                     unsigned x1, unsigned char y1)
{
    /* Sort and clip here: screen2_filled_rect sorts but never clips, so an
     * x past 255 wrapped through the byte cast and a y past 191 wrote
     * beyond the pattern table. */
    if (x1 < x0) { unsigned t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { unsigned char t = y0; y0 = y1; y1 = t; }
    if (x0 > 255u || y0 > 191u) return;
    if (x1 > 255u) x1 = 255u;
    if (y1 > 191u) y1 = 191u;
    screen2_filled_rect((unsigned char)x0, y0, (unsigned char)x1, y1);
}

/* TMS bitmap clear is always 0 — `color` honoured for API symmetry but the
 * underlying primitive (screen2_clear via tms_fill_vram) writes 0x00 into
 * the pattern table. A future colour-mask extension would land here. */
void gfx_clear(unsigned char color) { (void)color; screen2_clear(); }
