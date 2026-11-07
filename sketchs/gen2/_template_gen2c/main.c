/* main.c — smallest GEN2 HGR program in C.
 *
 * Clears the framebuffer, draws a violet "HELLO" plus a small rectangle, then
 * waits for a key and returns to Wozmon. About 12 lines of "real" code.
 *
 * COPY THIS FOLDER to start a colour-graphics project. Build with `make`,
 * load the .bin in POM1 and run with 6000R.
 *
 * Why this is small:
 *   - The Makefile links the GEN2 runtime as an ARCHIVE (gen2c.lib): ld65
 *     extracts only the members this program references — one per kernel —
 *     so the pixel / sprite / lores / geom code never lands in the binary.
 *     See dev/lib/gen2c/gen2c.mk.
 *   - puts_apple1 is a MACRO around woz_puts — zero bytes added.
 *   - No printf, no malloc, no floats. 16-bit `unsigned` is plenty here. */

#include "gen2.h"      /* pulls in apple1c.h automatically */

void main(void)
{
    gen2_hgr_init();                                /* graphics + hires + page 1 */
    gen2_hgr_clear(0);                              /* black framebuffer */
    gen2_hgr_puts_color(20, 30, "HELLO GEN2", GEN2_VIOLET);
    gen2_hgr_fill_pixrect(40, 80, 100, 20);         /* a white box */
    gen2_hgr_colorize(40, 80, 100, 20, GEN2_GREEN); /* tint it green */
    (void)apple1_getkey();                          /* wait for a keypress */
    woz_mon();                                      /* return to Wozmon */
}
