/*
 * gfx_putx.c — uppercase hexadecimal at the gfx_text cursor. See gfx.h.
 *
 * Its own member (not in gfx_text.c) so a program that only positions strings
 * does not link the hex converter (gfx_num_hex.s). Split as in pom2games
 * commit 4dcede8.
 */
#include "gfx.h"

void gfx_putx(unsigned value)
{
    char buf[5];                     /* 4 hex digits + NUL */
    gfx_hexstr(buf, value);
    gfx_text(buf);
}
