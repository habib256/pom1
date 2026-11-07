/* ===========================================================================
 * t43_c_rect.c — micro-test: gen2c rectangle kernels (paired stores)
 * ===========================================================================
 * GUARDS _gen2_pixrect_asm / _gen2_fill_rect_asm (gen2_pixrect_asm.s /
 * gen2_fill_rect_asm.s) after their rewrite to paired full-byte stores. 60
 * rectangles (24 hand-picked: one byte, adjacent edges, 0/1/2/odd/even full
 * bytes between the edges, x = 255/256, the right edge 279, erase and fill,
 * byte fills of 1..40 columns incl. odd widths and bit-7 values; + 36 random)
 * are applied over a pseudo-random background (palette bits set) to page 2 by
 * the kernels and to page 1 by a C reference of the documented maths:
 *     edge bytes  (b & keep) | set, keep/set from the edge masks, bit 7
 *                 cleared (fill: keep $7F, set mask; erase: keep ~mask & $7F)
 *     full bytes  $7F (fill) / $00 (erase); byte fill: the value as is
 * and the spanned scanlines are compared after every rectangle.
 *
 * Mailbox at $1000:  +0 magic $A5   +1 rectangles whose rows differ (00)
 *   +2 first such rectangle + 1 (00)   +3 rectangles run (3C)
 *   +4/+5 background sum   +6/+7 page-2 sum at the end (Python model)
 *
 * POM1-LIB-MICRO-TEST
 * MODE: gen2
 * LIBS: gen2c/gen2_init.c gen2c/gen2_tables.s gen2c/gen2_page.s gen2c/gen2_mode_asm.s gen2c/gen2_hgr_clear_asm.s gen2c/gen2_pixrect_asm.s gen2c/gen2_fill_rect_asm.s apple1c/apple1io_asm.s apple1c/apple1io_str.s apple1c/apple1io_key.s
 * CFG: ../cc65/apple1_gen2_c.cfg
 * PRESET: 11
 * LOAD: 6000
 * RUN: 6000
 * STEPS: 3000000
 * EXPECT: 1000 A5 00 00 3C 5A 02 57 6D
 * =========================================================================== */

#include <string.h>
#include "gen2.h"
#include "gen2_internal.h"

#define MB ((volatile unsigned char *)0x1000)

/* kind 0: pixrect(x, xr, y0, rows, mode)  kind 1: fill(col0, cols, y0, rows, val) */
typedef struct { unsigned char k; unsigned a, b; unsigned char y0, rows, m; } op_t;
static const op_t ops[] = {
    {0,   0,   0,   0,  1, 0x01},
    {0,   0, 279,   1,  2, 0x01},
    {0,   0, 279,   3,  2, 0x00},
    {0,   3,   3,   5,  4, 0x01},
    {0,   3,   5,   9,  4, 0x00},
    {0,   6,   7,  13,  3, 0x01},
    {0,   7,  13,  17,  3, 0x01},
    {0,   7,  20,  20,  2, 0x00},
    {0,   5,  22,  25,  2, 0x01},
    {0,   1,  27,  28,  2, 0x01},
    {0, 250, 270,  30,  6, 0x01},
    {0, 255, 256,  40,  3, 0x00},
    {0, 256, 279,  44,  5, 0x01},
    {0, 272, 279,  50,  4, 0x00},
    {0,  13,  14,  60,  3, 0x01},
    {0,   2, 278,  64,  1, 0x00},
    {0, 140, 279,  70, 16, 0x01},
    {0,   0, 139, 186,  6, 0x00},
    {1,   0,  40,  90,  3, 0x55},
    {1,   0,   1,  94,  2, 0xAA},
    {1,  39,   1, 100,  4, 0x7F},
    {1,   3,   2, 104,  3, 0x80},
    {1,  10,   7, 110,  5, 0x00},
    {1,  20,  20, 115,  1, 0xFF},
    {1,  20,   7,  61,  4, 0x3A},
    {0, 121, 130, 142, 11, 0x00},
    {1,   1,  32,  44,  4, 0xC6},
    {0, 164, 171,  22,  9, 0x01},
    {0, 104, 118,  49,  2, 0x00},
    {1,  35,   4, 152, 12, 0x27},
    {0, 108, 155,  84,  2, 0x00},
    {0,  40,  98, 137,  2, 0x01},
    {0,  62,  84, 135,  1, 0x01},
    {1,  38,   1,  56, 10, 0xF1},
    {0,  70, 119,  98,  1, 0x00},
    {0,  20,  67,  91,  7, 0x00},
    {0, 258, 276, 176,  8, 0x00},
    {0,  48,  57,  76,  9, 0x00},
    {0, 248, 270, 106, 10, 0x00},
    {0, 246, 255,   6, 12, 0x00},
    {0, 238, 245,  99,  8, 0x00},
    {1,   8,  16,  66,  4, 0x2C},
    {0,  76,  77,  12,  4, 0x01},
    {1,  36,   4, 125,  9, 0x89},
    {0,  52,  82, 159,  7, 0x01},
    {0, 147, 176,  48,  1, 0x01},
    {0,   9,  56,  36,  1, 0x00},
    {0,  55,  87,  52, 11, 0x00},
    {1,  10,   4, 170,  7, 0x91},
    {0, 250, 267, 177,  7, 0x00},
    {1,  27,   5,  72,  2, 0xA5},
    {0,  99, 148,  71,  6, 0x01},
    {1,  33,   4, 129,  9, 0xCD},
    {1,  16,  13, 191,  1, 0x28},
    {0,  52,  61,  35,  6, 0x00},
    {0,  29,  62,   5, 12, 0x00},
    {0,  65,  82,  41, 11, 0x01},
    {0, 264, 276,  22, 12, 0x01},
    {0, 232, 273, 158, 12, 0x00},
    {0,  76, 107, 103,  9, 0x00},
};
#define NOPS (sizeof ops / sizeof ops[0])

static unsigned char lmask, rmask;

static void ref_op(const op_t *o)
{
    unsigned char y, c, cl, cr, mk;
    unsigned char *row;
    cl = (unsigned char)(o->a / 7u);
    cr = (unsigned char)(o->b / 7u);
    lmask = (unsigned char)(~((1u << (o->a % 7u)) - 1u) & 0x7Fu);
    rmask = (unsigned char)(((1u << (o->b % 7u + 1u)) - 1u) & 0x7Fu);
    if (cl == cr) lmask &= rmask;
    for (y = o->y0; y < (unsigned char)(o->y0 + o->rows); ++y) {
        row = gen2_hgr_row(y);
        if (o->k) {
            memset(row + (unsigned char)o->a, o->m, (unsigned char)o->b);
            continue;
        }
        for (c = cl; c <= cr; ++c) {
            if (c == cl || c == cr) {
                mk = (c == cl) ? lmask : rmask;
                if (o->m) row[c] = (unsigned char)((row[c] & 0x7Fu) | mk);
                else      row[c] = (unsigned char)(row[c] & (mk ^ 0x7Fu));
            } else {
                row[c] = o->m ? 0x7Fu : 0u;
            }
        }
    }
}

static void kern_op(const op_t *o)
{
    if (o->k) {
        gen2_f_col0 = (unsigned char)o->a;
        gen2_f_cols = (unsigned char)o->b;
        gen2_f_y0 = o->y0; gen2_f_rows = o->rows; gen2_f_val = o->m;
        gen2_fill_rect_asm();
    } else {
        gen2_r_x = o->a; gen2_r_xr = o->b;
        gen2_r_y0 = o->y0; gen2_r_rows = o->rows; gen2_r_mode = o->m;
        gen2_pixrect_asm();
    }
}

static unsigned page_sum(const unsigned char *p)
{
    unsigned s = 0u;
    unsigned char k, i;
    for (k = 0u; k < 32u; ++k) {
        i = 0u;
        do { s += p[i]; } while (++i != 0u);
        p += 256;
    }
    return s;
}

static void out16(unsigned char at, unsigned v)
{
    MB[at] = (unsigned char)v;
    MB[at + 1u] = (unsigned char)(v >> 8);
}

void main(void)
{
    unsigned char i, y, c, bad = 0u, first = 0u;
    unsigned s = 0x1234u;
    unsigned char *row;

    gen2_hgr_init();
    gen2_set_draw_page(1u);
    gen2_hgr_clear(0u);
    for (y = 0u; y < 192u; ++y) {
        row = gen2_hgr_row(y);
        for (c = 0u; c < 40u; ++c) {
            s = s * 5u + 1u;
            row[c] = (unsigned char)(s >> 8);
        }
    }
    memcpy(GEN2_HGR2, GEN2_HGR1, 0x2000u);
    out16(4u, page_sum(GEN2_HGR1));

    for (i = 0u; i < NOPS; ++i) {
        gen2_set_draw_page(1u);
        ref_op(&ops[i]);
        gen2_set_draw_page(2u);
        kern_op(&ops[i]);
        gen2_set_draw_page(1u);
        for (y = ops[i].y0; y < (unsigned char)(ops[i].y0 + ops[i].rows); ++y) {
            row = gen2_hgr_row(y);
            if (memcmp(row, row + 0x2000u, 40u) != 0) {
                ++bad;
                if (!first) first = (unsigned char)(i + 1u);
                break;
            }
        }
    }
    out16(6u, page_sum(GEN2_HGR2));
    MB[1] = bad;
    MB[2] = first;
    MB[3] = (unsigned char)NOPS;
    MB[0] = 0xA5u;                       /* magic LAST */
    for (;;) { }
}
