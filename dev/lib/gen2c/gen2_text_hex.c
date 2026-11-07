/* gen2_text_hex.c — gen2_hgr_putx, uppercase hex on top of the glyph core.
 * Its own module (split from gen2_text_num.c) so a decimal-only program does
 * not link the hex converter (gfx_num_hex.s), and vice versa. */

#include "gen2.h"
#include "gfx.h"   /* gfx_hexstr */

/* Unsigned hexadecimal at (x, y), uppercase, no leading zeros (1-4 digits).
 * OR-drawn like gen2_hgr_putu. Handy for addresses / bit masks in a HUD. */
void gen2_hgr_putx(unsigned x, unsigned char y, unsigned value)
{
    char buf[5];
    gfx_hexstr(buf, value);
    gen2_hgr_puts(x, y, buf);
}
