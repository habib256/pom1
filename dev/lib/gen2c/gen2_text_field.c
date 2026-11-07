/* gen2_text_field.c — gen2_hgr_putu_field, the flicker-free HUD counter.
 *
 * Its own module (split from gen2_text_num.c) because its self-bounded erase
 * pulls the pixel-rect fill (gen2_rect.c + gen2_pixrect_asm.s) and its field
 * math pulls cc65's soft multiply: a program printing plain numbers does not
 * pay for either. Digits from the shared converter gfx_utoa (gfx_num_dec.s),
 * whose return value is the digit count — no separate strlen. */

#include "gen2.h"
#include "gfx.h"   /* gfx_utoa */

/* Fixed-width, right-aligned unsigned decimal — the flicker-free HUD primitive.
 * The field is `width` glyph cells (18px pitch) wide; this ERASES exactly that
 * box first, then draws the digits flush-right inside it. Because the erase is
 * bounded to the field the function owns, a shrinking value (123 -> 9) leaves no
 * stale digits AND the wipe can't bite into an adjacent label. A value too wide
 * for the field overflows to the right (keep width >= the max digit count).
 * width is clamped to 1..14 (a 14-cell field already fills the 280px line). */
void gen2_hgr_putu_field(unsigned x, unsigned char y, unsigned value,
                         unsigned char width)
{
    char buf[6];
    unsigned char len;
    unsigned dx;
    if (width == 0u) return;
    if (width > 14u) width = 14u;
    len = gfx_utoa(buf, value);
    /* Erase the field box: (width-1) full pitches + one 16px cell, 16px tall. */
    gen2_hgr_clear_pixrect(x, y,
                           (unsigned char)((unsigned)(width - 1u) * 18u + 16u), 16u);
    /* Flush-right: blank the leading (width-len) cells; overflow if len>=width. */
    dx = (len < width) ? (x + (unsigned)(width - len) * 18u) : x;
    gen2_hgr_puts(dx, y, buf);
}
