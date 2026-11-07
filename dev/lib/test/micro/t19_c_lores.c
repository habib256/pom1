/* ===========================================================================
 * t19_c_lores.c — micro-test: gen2c LORES family (asm) ON-TARGET
 * ===========================================================================
 * GUARDS the asm LORES modules (gen2_lores_tab/clr/dot/run.s) that replaced
 * gen2_lores.c: block addressing (low nibble = upper/even block), the other
 * block preserved by setblock and by a span's unmatched top/bottom block,
 * colour masked to 4 bits, every clip rule of the C (inclusive hlin/vlin,
 * reversed spans empty, w/h trimmed at the edges incl. 255, w or h 0 empty,
 * off-screen dropped), getblock (0 off-screen), page 2 through
 * gen2_set_draw_page, and the clear leaving the 64 screen-hole bytes alone.
 *
 * $0400-$0BFF is pre-filled with $5A so a hole or an untouched nibble is
 * visible. Expected values come from a Python model of the C semantics.
 *
 * Mailbox at $1000 (idle RAM; the LORES pages are $0400-$0BFF):
 *   +0 magic $A5
 *   +1..+10  ten getblock reads: (0,0) (39,47) (7,1) (40,0) (20,33) (2,4)
 *            (31,21) (0,47) (3,2) (3,3)
 *   +11/+12  16-bit sum of $0400-$07FF (page 1, holes included)
 *   +13/+14  16-bit sum of $0800-$0BFF (page 2)
 *   +15/+16  hole bytes $0478 and $0BFF (still $5A)
 *
 * POM1-LIB-MICRO-TEST
 * MODE: gen2
 * LIBS: gen2c/gen2_init.c gen2c/gen2_blit.s gen2c/gen2_tables.s gen2c/gen2_page.s gen2c/gen2_lores_tab.s gen2c/gen2_lores_clr.s gen2c/gen2_lores_dot.s gen2c/gen2_lores_run.s apple1c/apple1io.c apple1c/apple1io_asm.s
 * CFG: ../cc65/apple1_gen2_c.cfg
 * PRESET: 11
 * LOAD: 6000
 * RUN: 6000
 * STEPS: 400000
 * EXPECT: 1000 A5 0F 0B 0C 00 08 03 0B 0E 02 04 AD 4B FA 9E 5A 5A
 * =========================================================================== */

#include <string.h>
#include "gen2.h"

#define MB ((volatile unsigned char *)0x1000)

static unsigned sum1k(const unsigned char *p)
{
    unsigned s = 0u;
    unsigned char i, k;
    for (k = 0u; k < 4u; ++k) {
        i = 0u;
        do { s += p[i]; } while (++i != 0u);
        p += 256;
    }
    return s;
}

void main(void)
{
    unsigned s;

    gen2_lores_init();
    memset((void *)0x0400, 0x5A, 0x0800);

    gen2_lores_clear(3u);
    gen2_lores_setblock(0u, 0u, 15u);
    gen2_lores_setblock(39u, 47u, 9u);
    gen2_lores_setblock(40u, 0u, 1u);           /* dropped */
    gen2_lores_setblock(5u, 48u, 1u);           /* dropped */
    gen2_lores_setblock(7u, 1u, 0x1Cu);         /* masked to 12 */
    gen2_lores_hlin(2u, 10u, 3u, 4u);
    gen2_lores_hlin(35u, 60u, 10u, 5u);         /* x1 clipped */
    gen2_lores_hlin(10u, 2u, 11u, 6u);          /* reversed: nothing */
    gen2_lores_hlin(0u, 39u, 46u, 7u);
    gen2_lores_vlin(20u, 0u, 47u, 8u);
    gen2_lores_vlin(21u, 5u, 5u, 9u);           /* one block, odd row */
    gen2_lores_vlin(22u, 40u, 100u, 10u);       /* y1 clipped */
    gen2_lores_vlin(40u, 0u, 10u, 1u);          /* dropped */
    gen2_lores_vlin(23u, 10u, 5u, 1u);          /* reversed: nothing */
    gen2_lores_fill_rect(30u, 20u, 20u, 30u, 11u);  /* both clipped */
    gen2_lores_fill_rect(1u, 13u, 3u, 4u, 12u);     /* odd top row */
    gen2_lores_fill_rect(1u, 1u, 0u, 5u, 1u);       /* w 0: nothing */
    gen2_lores_fill_rect(5u, 7u, 255u, 1u, 13u);    /* w 255 */
    gen2_lores_fill_rect(0u, 30u, 1u, 255u, 14u);   /* h 255 */
    gen2_lores_fill_rect(3u, 2u, 2u, 1u, 2u);       /* even single row */

    MB[1]  = gen2_lores_getblock(0u, 0u);
    MB[2]  = gen2_lores_getblock(39u, 47u);
    MB[3]  = gen2_lores_getblock(7u, 1u);
    MB[4]  = gen2_lores_getblock(40u, 0u);
    MB[5]  = gen2_lores_getblock(20u, 33u);
    MB[6]  = gen2_lores_getblock(2u, 4u);
    MB[7]  = gen2_lores_getblock(31u, 21u);
    MB[8]  = gen2_lores_getblock(0u, 47u);
    MB[9]  = gen2_lores_getblock(3u, 2u);
    MB[10] = gen2_lores_getblock(3u, 3u);
    s = sum1k((const unsigned char *)0x0400);
    MB[11] = (unsigned char)s;
    MB[12] = (unsigned char)(s >> 8);

    gen2_set_draw_page(2u);
    gen2_lores_clear(6u);
    gen2_lores_setblock(1u, 1u, 2u);
    gen2_lores_hlin(0u, 39u, 0u, 13u);
    gen2_lores_fill_rect(10u, 10u, 5u, 5u, 1u);
    gen2_lores_vlin(39u, 3u, 44u, 15u);
    s = sum1k((const unsigned char *)0x0800);
    MB[13] = (unsigned char)s;
    MB[14] = (unsigned char)(s >> 8);
    MB[15] = *(volatile unsigned char *)0x0478;
    MB[16] = *(volatile unsigned char *)0x0BFF;
    MB[0] = 0xA5u;                       /* magic LAST */
    for (;;) { }
}
