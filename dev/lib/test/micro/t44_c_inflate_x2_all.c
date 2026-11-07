/* ===========================================================================
 * t44_c_inflate_x2_all.c — micro-test: gen2_hgr_inflate_x2, every colour
 * ===========================================================================
 * GUARDS the byte-wise gen2_hgr_x2.s (16-entry spread table, palette bit on
 * non-zero output bytes only) against the C reference rule (gen2_hgr_x2_ref.c:
 * each lit source pixel lights dot 2x and/or 2x+1 on both doubled rows, ORing
 * the palette bit with the dot). t13 pins WHITE on the Fauna dog; this pins
 * all eight colour codes (BLACK, WHITE, VIOLET, GREEN, BLUE, ORANGE, and the
 * unknown 6/7 = nothing lit) on a 3-byte x 5-row random master whose bytes
 * have bit 7 set at random (it must be ignored), into a buffer pre-filled with
 * $5A (every output byte must be written, unlit ones to 0), with guard bytes
 * on both sides. A wbytes of 0 must write nothing (the old kernel wrote 256
 * bytes per row). Expected values from a Python model of the C rule; the old
 * per-pixel kernel passes this test too.
 *
 * Mailbox at $1000:  +0 magic $A5
 *   +1 + 3*c  (c = colour 0..7): 16-bit sum, then XOR of (byte + index), of
 *             the 60 output bytes
 *   +25 guards intact + wbytes-0 buffer untouched ($00 = all good)
 *
 * POM1-LIB-MICRO-TEST
 * MODE: gen2
 * LIBS: gen2c/gen2_hgr_x2.s apple1c/apple1io_asm.s apple1c/apple1io_str.s apple1c/apple1io_key.s
 * CFG: ../cc65/apple1_gen2_c.cfg
 * PRESET: 11
 * LOAD: 6000
 * RUN: 6000
 * STEPS: 600000
 * EXPECT: 1000 A5 00 00 00 F8 0F F0 F8 07 E0 00 08 D8 F8 20 E0 00 22 D8 00 00 00 00 00 00 00
 * =========================================================================== */

#include <string.h>
#include "gen2.h"

#define MB ((volatile unsigned char *)0x1000)

static const unsigned char mono[] = {0x82,0xB7,0x0E,0xEE,0x7F,0x1A,0x50,0x39,0xBE,0xF0,0x7E,0xC2,0x34,0x7F,0x06};
static unsigned char buf[64];            /* guard, 60 output bytes, guard x3 */

static void sums(unsigned char at)
{
    unsigned s = 0u;
    unsigned char x = 0u, i;
    for (i = 0u; i < 60u; ++i) {
        s += buf[1u + i];
        x ^= (unsigned char)(buf[1u + i] + i);
    }
    MB[at] = (unsigned char)s;
    MB[at + 1u] = (unsigned char)(s >> 8);
    MB[at + 2u] = x;
}

void main(void)
{
    unsigned char c, bad = 0u;

    for (c = 0u; c < 8u; ++c) {
        memset(buf, 0x5A, sizeof buf);
        gen2_hgr_inflate_x2(mono, 3u, 5u, c, buf + 1);
        if (buf[0] != 0x5Au || buf[61] != 0x5Au) ++bad;
        sums((unsigned char)(1u + 3u * c));
    }
    memset(buf, 0x5A, sizeof buf);
    gen2_hgr_inflate_x2(mono, 0u, 5u, GEN2_X2_WHITE, buf + 1);
    for (c = 0u; c < sizeof buf; ++c)
        if (buf[c] != 0x5Au) { ++bad; break; }
    MB[25] = bad;
    MB[0] = 0xA5u;                       /* magic LAST */
    for (;;) { }
}
