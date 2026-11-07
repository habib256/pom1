/* ===========================================================================
 * t20_c_hgr_clear.c — micro-test: gen2c gen2_hgr_clear ON-TARGET
 * ===========================================================================
 * GUARDS the unrolled, self-modifying HIRES clear (gen2_hgr_clear_asm.s,
 * ported from pom2games HGR_CLEAR_LOOP: eight STA abs,Y per turn, the store
 * pages patched per group of eight pages):
 *   1. called BEFORE any table build / draw-page call it must lazy-default
 *      gen2_hgr_base to $20 and fill exactly $2000-$3FFF (a 0 base would
 *      scrub zero page + stack);
 *   2. after gen2_set_draw_page(2) it fills exactly $4000-$5FFF, page 1 kept;
 *   3. back on page 1 (a SECOND run of the patched loop, from new operands)
 *      it refills page 1 and leaves page 2 alone;
 *   4. the bytes just outside the 8 KB window ($1FFF, $6000 is this very
 *      program) are never touched.
 * Each page of both framebuffers is sampled (see count_not; must count 0).
 *
 * POM1-LIB-MICRO-TEST
 * MODE: gen2
 * LIBS: gen2c/gen2_init.c gen2c/gen2_hgr_clear_asm.s
 * CFG: ../cc65/apple1_gen2_c.cfg
 * PRESET: 11
 * LOAD: 6000
 * RUN: 6000
 * STEPS: 250000
 * EXPECT: 1000 A5 20 00 00 C3 00 00 55 40 00 00 00 00 00 00 20
 * ===========================================================================
 * Mailbox at $1000 (idle user RAM below the HIRES framebuffers):
 *   +0 magic  +1 gen2_hgr_base after the first clear ($20)
 *   +2/+3 step 1: bytes of page 1 != $55 (lo, hi)   +4 sentinel $1FFF ($C3)
 *   +5/+6 step 2: bytes of page 2 != $AA            +7 page-1 byte kept ($55)
 *   +8 gen2_hgr_base on page 2 ($40)
 *   +9/+10 step 3: bytes of page 1 != $00           +11/+12 bytes of page 2 != $AA
 *   +13/+14 reserved 0                              +15 base back to $20
 */

#include "gen2.h"

#define MB ((volatile unsigned char *)0x1000)

extern unsigned char gen2_hgr_base;      /* gen2_init.c (BSS draw-page base) */

/* Count, over the 8 KB page starting at hi<<8, the sampled bytes that differ
 * from v: in each of the 32 256-byte pages, bytes 0, 1, every 15th, and 255.
 * The loop under test stores whole 256-byte pages (Y = 0..255 against eight
 * patched page operands), so a wrong page, a missing group or a short Y run
 * all show up in the sample; checking all 8192 bytes in C costs ~10x more
 * --step time for no extra coverage of that structure. */
static unsigned count_not(unsigned char hi, unsigned char v)
{
    unsigned bad = 0u;
    unsigned char page, i;
    const unsigned char *p;
    for (page = 0u; page < 32u; ++page) {
        p = (const unsigned char *)((unsigned)(hi + page) << 8);
        if (p[1] != v) ++bad;
        if (p[255] != v) ++bad;
        for (i = 0u; i < 255u; i = (unsigned char)(i + 15u)) {
            if (p[i] != v) ++bad;
        }
    }
    return bad;
}

void main(void)
{
    unsigned n;

    *(volatile unsigned char *)0x1FFF = 0xC3u;   /* sentinel below page 1 */

    gen2_hgr_clear(0x55u);                       /* 1: lazy page 1 */
    MB[1] = gen2_hgr_base;
    n = count_not(0x20u, 0x55u);
    MB[2] = (unsigned char)n;  MB[3] = (unsigned char)(n >> 8);
    MB[4] = *(volatile unsigned char *)0x1FFF;

    gen2_set_draw_page(2u);                      /* 2: page 2 */
    gen2_hgr_clear(0xAAu);
    n = count_not(0x40u, 0xAAu);
    MB[5] = (unsigned char)n;  MB[6] = (unsigned char)(n >> 8);
    MB[7] = *(volatile unsigned char *)0x3FFF;
    MB[8] = gen2_hgr_base;

    gen2_set_draw_page(1u);                      /* 3: page 1 again */
    gen2_hgr_clear(0x00u);
    n = count_not(0x20u, 0x00u);
    MB[9] = (unsigned char)n;  MB[10] = (unsigned char)(n >> 8);
    n = count_not(0x40u, 0xAAu);
    MB[11] = (unsigned char)n; MB[12] = (unsigned char)(n >> 8);
    MB[13] = 0u; MB[14] = 0u;
    MB[15] = gen2_hgr_base;

    MB[0] = 0xA5u;                               /* magic LAST */
    for (;;) { }
}
