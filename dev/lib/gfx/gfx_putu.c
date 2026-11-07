/*
 * gfx_putu.c — unsigned decimal at the gfx_text cursor. See gfx.h.
 *
 * Its own member (not in gfx_text.c) so a program that only positions strings
 * does not link the decimal converter (gfx_num_dec.s). Split as in pom2games
 * commit 4dcede8.
 */
#include "gfx.h"

void gfx_putu(unsigned value)
{
    char buf[6];                     /* 65535 = 5 digits + NUL */
    gfx_utoa(buf, value);
    gfx_text(buf);
}
