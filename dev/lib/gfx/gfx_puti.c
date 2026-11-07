/*
 * gfx_puti.c — signed decimal at the gfx_text cursor. See gfx.h.
 *
 * Its own member (not in gfx_text.c) so a program that only positions strings
 * does not link the signed converter (gfx_num_signed.s). Split as in pom2games
 * commit 4dcede8.
 */
#include "gfx.h"

void gfx_puti(int value)
{
    char buf[7];                     /* '-' + 5 digits + NUL */
    gfx_itoa(buf, value);
    gfx_text(buf);
}
