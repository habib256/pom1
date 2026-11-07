/* ===========================================================================
 * t42_c_tables.c — micro-test: gen2c tables, gen2_hgr_row, draw page (asm)
 * ===========================================================================
 * GUARDS gen2_tables.s + gen2_page.s, which replaced the C table builder,
 * gen2_hgr_row and gen2_set_draw_page of gen2_init.c. Every value is checked
 * against the interleave formula evaluated in C:
 *   - gen2_hgr_row(y) for ALL y 0..255 (192..255 included, as the C formula
 *     answered them), before any table build (page base defaulted to $20),
 *     on page 2 and back on page 1;
 *   - gen2_rowlo/rowhi[192] after the build, after gen2_set_draw_page(2), a
 *     repeated (2), an out-of-range page (7 -> page 1) and (1);
 *   - gen2_col7/mask7/phase7[280] against x/7, 1<<(x%7), x%7;
 *   - the LORES row table (built by a setblock) following the page flips.
 *
 * Mailbox at $1000:  +0 magic $A5
 *   +1 gen2_hgr_row mismatches   +2 HIRES table mismatches
 *   +3 x-table mismatches        +4 LORES table mismatches   (all 00)
 *   +5/+6 sum of rowhi on page 2 (3BA0)  +7/+8 sum of rowlo (4E00)
 *   +9/+10 sum of col7 (1554)    +11/+12 sum of mask7 (13D8)
 *   +13 sum of the LORES high bytes on page 2 (E4)
 *
 * POM1-LIB-MICRO-TEST
 * MODE: gen2
 * LIBS: gen2c/gen2_init.c gen2c/gen2_tables.s gen2c/gen2_page.s gen2c/gen2_mode_asm.s gen2c/gen2_lores_tab.s gen2c/gen2_lores_dot.s apple1c/apple1io_asm.s apple1c/apple1io_str.s apple1c/apple1io_key.s
 * CFG: ../cc65/apple1_gen2_c.cfg
 * PRESET: 11
 * LOAD: 6000
 * RUN: 6000
 * STEPS: 1500000
 * EXPECT: 1000 A5 00 00 00 00 A0 3B 00 4E 54 15 D8 13 E4
 * =========================================================================== */

#include "gen2.h"
#include "gen2_internal.h"

#define MB ((volatile unsigned char *)0x1000)

static unsigned char bad_row, bad_tab, bad_x, bad_lo;

static unsigned ref_row(unsigned char base, unsigned char y)
{
    return ((unsigned)base << 8) + ((unsigned)(y & 7u) << 10)
         + ((unsigned)((y >> 3) & 7u) << 7) + (unsigned)(y >> 6) * 40u;
}

static void check_rows(unsigned char base)
{
    unsigned char y = 0u;
    do {
        if ((unsigned)gen2_hgr_row(y) != ref_row(base, y)) ++bad_row;
        if (y < 192u &&
            (((unsigned)gen2_rowhi[y] << 8) | gen2_rowlo[y]) != ref_row(base, y))
            ++bad_tab;
    } while (++y != 0u);
}

static void check_lores(unsigned char base)
{
    unsigned char r;
    unsigned a;
    for (r = 0u; r < 24u; ++r) {
        a = ((unsigned)base << 8) + ((unsigned)(r & 7u) << 7) + (unsigned)(r >> 3) * 40u;
        if ((((unsigned)gen2_lo_rowhi[r] << 8) | gen2_lo_rowlo[r]) != a) ++bad_lo;
    }
    if (gen2_lo_base != base) ++bad_lo;
}

static void out16(unsigned char at, unsigned v)
{
    MB[at] = (unsigned char)v;
    MB[at + 1u] = (unsigned char)(v >> 8);
}

void main(void)
{
    unsigned x, s;
    unsigned char y;

    gen2_hgr_init();
    for (y = 0u; ; ++y) {                       /* no table built yet */
        if ((unsigned)gen2_hgr_row(y) != ref_row(0x20u, y)) ++bad_row;
        if (y == 255u) break;
    }
    gen2_build_tables();
    check_rows(0x20u);
    for (x = 0u; x < 280u; ++x) {
        if (gen2_col7[x] != (unsigned char)(x / 7u)) ++bad_x;
        if (gen2_mask7[x] != (unsigned char)(1u << (x % 7u))) ++bad_x;
        if (gen2_phase7[x] != (unsigned char)(x % 7u)) ++bad_x;
    }
    gen2_lores_setblock(0u, 0u, 0u);            /* builds the LORES rows */
    check_lores(0x04u);

    gen2_set_draw_page(2u);
    check_rows(0x40u);
    check_lores(0x08u);
    s = 0u;
    for (y = 0u; y < 192u; ++y) s += gen2_rowhi[y];
    out16(5u, s);
    s = 0u;
    for (y = 0u; y < 192u; ++y) s += gen2_rowlo[y];
    out16(7u, s);
    s = 0u;
    for (y = 0u; y < 24u; ++y) s += gen2_lo_rowhi[y];
    MB[13] = (unsigned char)s;
    gen2_set_draw_page(2u);                      /* no-op */
    check_rows(0x40u);
    check_lores(0x08u);
    gen2_set_draw_page(7u);                      /* out of range -> page 1 */
    check_rows(0x20u);
    check_lores(0x04u);
    gen2_set_draw_page(1u);
    check_rows(0x20u);

    s = 0u;
    for (x = 0u; x < 280u; ++x) s += gen2_col7[x];
    out16(9u, s);
    s = 0u;
    for (x = 0u; x < 280u; ++x) s += gen2_mask7[x];
    out16(11u, s);
    MB[1] = bad_row;
    MB[2] = bad_tab;
    MB[3] = bad_x;
    MB[4] = bad_lo;
    MB[0] = 0xA5u;                       /* magic LAST */
    for (;;) { }
}
