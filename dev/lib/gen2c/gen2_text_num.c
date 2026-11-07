/* gen2_text_num.c — decimal NUMBER rendering on top of the glyph core
 * (gen2_text.c): gen2_hgr_putu / gen2_hgr_puti / gen2_hgr_putu8.
 *
 * Split out of gen2_text.c so a string-only program stops paying for the
 * formatters and for what they pull. The fixed-width HUD field
 * (gen2_hgr_putu_field, which pulls the pixel-rect erase) is gen2_text_field.c
 * and the hex writer (gen2_hgr_putx) is gen2_text_hex.c, each its own archive
 * member for the same reason.
 *
 * The digits come from the ONE shared converter, gfx_utoa / gfx_itoa
 * (dev/lib/gfx/gfx_num_dec.s / gfx_num_signed.s: hand-written repeated
 * subtraction, no cc65 udiv16); the former private asm gen2_utoa is now an
 * alias of the same routine, so a binary never carries two.
 *
 * Everything here funnels through gen2_hgr_puts / gen2_hgr_puts8, so this
 * module pulling gen2_text.o is the intended dependency direction; the glyph
 * core must never call back into this file. */

#include "gen2.h"
#include "gfx.h"   /* gfx_utoa / gfx_itoa */

/* Unsigned decimal at (x, y) via gen2_hgr_puts, no leading zeros. */
void gen2_hgr_putu(unsigned x, unsigned char y, unsigned value)
{
    char buf[6];                     /* up to 5 digits + NUL */
    gfx_utoa(buf, value);
    gen2_hgr_puts(x, y, buf);
}

/* Signed decimal at (x, y): a leading '-' then the magnitude (OR-drawn like
 * gen2_hgr_putu — transparent, no field erase). -32768 prints correctly. */
void gen2_hgr_puti(unsigned x, unsigned char y, int value)
{
    char buf[7];                     /* '-' + up to 5 digits + NUL */
    gfx_itoa(buf, value);
    gen2_hgr_puts(x, y, buf);
}

/* Unsigned decimal at the native 8x8 size (the small-font twin of gen2_hgr_putu).
 * Handy for dense HUDs where the 16x16 digits are too big. */
void gen2_hgr_putu8(unsigned x, unsigned char y, unsigned value)
{
    char buf[6];
    gfx_utoa(buf, value);
    gen2_hgr_puts8(x, y, buf);
}
