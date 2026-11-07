/* ===========================================================================
 * t18_c_line.c — micro-test: GEN2 gfx_line (gfx/gfx_line_gen2.s) ON-TARGET
 * ===========================================================================
 * GUARDS the asm Bresenham that replaced gfx_line.c in gfx-gen2.lib: it must
 * light EXACTLY the pixels the portable C lit (same ties, same rejection of
 * off-screen diagonals, same hline/vline shortcuts and clipping), in either
 * draw page.
 *
 * For each of 56 lines (40 hand-picked: all octants, 45 degrees, steep and
 * shallow extremes, the x = 255/256 table split, byte-column edges, axis runs,
 * points, clipped runs, rejected endpoints; + 16 pseudo-random), page 1 is
 * drawn by ref_line() — gfx_line.c's body verbatim, plotting through
 * gen2_hgr_plot — and page 2 by gfx_line(). The scanlines the line spans are
 * compared between the two pages, then erased on both. Then every line is
 * drawn into a clear page 2 by gfx_line(), and the whole page is summed (which
 * also catches a store outside the spanned scanlines).
 *
 * Mailbox at $1000 (idle RAM below the framebuffers):
 *   +0 magic $A5   +1 lines whose pages differ (00)   +2 first such line+1 (00)
 *   +3 lines run (56 = $38)
 *   +4/+5 16-bit sum of page 2's bytes with all 56 lines drawn (7F8C;
 *         $17F8C untruncated) — computed independently by a Python model of
 *         gfx_line.c, so an empty or identical-but-wrong pair of drawings
 *         cannot pass.
 *
 * POM1-LIB-MICRO-TEST
 * MODE: gen2
 * LIBS: gen2c/gen2_init.c gen2c/gen2_blit.s gen2c/gen2_tables.s gen2c/gen2_page.s gen2c/gen2_pixel.c gen2c/gen2_rect.c gen2c/gen2_geom.c gfx/gfx_line_gen2.s gfx/gfx_rect.c gfx/gfx_circle.c gfx/gfx_ellipse.c gfx/gfx_backend_gen2.c apple1c/apple1io.c apple1c/apple1io_asm.s
 * CFG: ../cc65/apple1_gen2_c.cfg
 * PRESET: 11
 * LOAD: 6000
 * RUN: 6000
 * STEPS: 9000000
 * EXPECT: 1000 A5 00 00 38 8C 7F
 * =========================================================================== */

#include "gen2.h"
#include "gfx.h"
#include <string.h>

#define MB ((volatile unsigned char *)0x1000)

/* gfx_line.c's body, verbatim, on the GEN2 primitives (the reference). */
static void ref_line(unsigned x0, unsigned char y0, unsigned x1, unsigned char y1)
{
    int x, y, xe, ye, dx, dy, sx, sy, err, e2;
    if (y0 == y1) { gen2_hgr_hline(x0, x1, y0); return; }
    if (x0 == x1) { gen2_hgr_vline(x0, y0, y1); return; }
    if (x0 >= 280u || x1 >= 280u || y0 >= 192u || y1 >= 192u) return;
    x = (int)x0; y = (int)y0; xe = (int)x1; ye = (int)y1;
    dx = xe - x; if (dx < 0) { dx = -dx; sx = -1; } else sx = 1;
    dy = ye - y; if (dy < 0) { dy = -dy; sy = -1; } else sy = 1;
    err = dx - dy;
    for (;;) {
        gen2_hgr_plot((unsigned)x, (unsigned char)y);
        if (x == xe && y == ye) break;
        e2 = err + err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
}

typedef struct { unsigned x0; unsigned char y0; unsigned x1; unsigned char y1; } line_t;

static const line_t lines[] = {
    {    0u,   0,  279u, 191},
    {  279u, 191,    0u,   0},
    {  279u,   0,    0u, 191},
    {    0u, 191,  279u,   0},
    {   10u,  10,  100u, 100},
    {  100u,  10,   10u, 100},
    {  100u, 100,   10u,  10},
    {   10u, 100,  100u,  10},
    {    5u,   0,    6u, 191},
    {  270u, 191,  279u,   0},
    {    1u,   1,    2u,   2},
    {    2u,   2,    1u,   1},
    {  250u,   5,  262u,  30},
    {  262u,  30,  250u,   5},
    {  255u,   0,  256u,   1},
    {  256u,   1,  255u,   0},
    {    0u,   0,    7u,   1},
    {    7u,   1,    0u,   0},
    {  279u,   0,  278u, 191},
    {    0u,   0,  279u,   1},
    {  279u, 190,    0u, 191},
    {    0u,   0,    1u, 191},
    {    6u,   0,    7u,   1},
    {    7u,   0,    6u,   1},
    {   13u,  50,   14u,  51},
    {   14u,  51,   13u,  50},
    {    0u,  95,  279u,  96},
    {  140u,   0,  139u, 191},
    {    0u,  50,  279u,  50},
    {  279u,  60,    0u,  60},
    {  100u,   0,  100u, 191},
    {  100u, 191,  100u,   0},
    {    5u,   5,    5u,   5},
    {  270u,  70,  300u,  70},
    {  277u,   3,  277u, 250},
    {    0u,   0,  280u, 100},
    {  300u,  10,   10u,  20},
    {   10u, 192,   20u,  10},
    {   10u,  10,   20u, 200},
    {65535u,   5,   10u,   6},
    {  151u,  11,   25u, 102},
    {  214u, 190,   68u,  52},
    {  144u,  71,   58u,  47},
    {  164u,  65,   78u,  81},
    {  140u, 138,  125u,  86},
    {   29u,  60,   19u,  60},
    {  219u,  52,   84u, 125},
    {  218u,  13,  115u,  22},
    {  269u, 165,  166u, 131},
    {  201u,  86,  118u, 191},
    {  226u, 125,   90u,   8},
    {   83u,  53,  277u,  88},
    {  162u, 156,  122u, 171},
    {    9u,  12,  230u, 191},
    {  172u,  98,   68u,  78},
    {  217u, 121,  170u,  85},
};
#define NLINES (sizeof lines / sizeof lines[0])

/* Page-1 address of scanline y (Apple II interleave). */
static unsigned char *row1(unsigned char y)
{
    return (unsigned char *)(0x2000u + ((unsigned)(y & 7u) << 10)
        + ((unsigned)((y >> 3) & 7u) << 7) + (unsigned)(y >> 6) * 40u);
}

void main(void)
{
    unsigned char i, j, p, y, ylo, yhi, bad = 0u, first = 0u;
    unsigned sum = 0u;
    register const unsigned char *b;

    gen2_hgr_init();
    gen2_set_draw_page(2u);
    gen2_hgr_clear(0u);
    gen2_set_draw_page(1u);
    gen2_hgr_clear(0u);
    for (i = 0u; i < NLINES; ++i) {
        ylo = lines[i].y0; yhi = lines[i].y1;
        if (yhi < ylo) { y = ylo; ylo = yhi; yhi = y; }
        if (yhi > 191u) yhi = 191u;
        if (ylo > 191u) ylo = 191u;
        gen2_set_draw_page(1u);
        ref_line(lines[i].x0, lines[i].y0, lines[i].x1, lines[i].y1);
        gen2_set_draw_page(2u);
        gfx_line(lines[i].x0, lines[i].y0, lines[i].x1, lines[i].y1);
        y = ylo;
        do {
            b = row1(y);
            if (memcmp(b, b + 0x2000u, 40u) != 0) {
                ++bad;
                if (!first) first = (unsigned char)(i + 1u);
                break;
            }
        } while (y++ != yhi);
        gen2_hgr_fill_rect(ylo, (unsigned char)(yhi - ylo + 1u), 0u, 40u, 0u);
        gen2_set_draw_page(1u);
        gen2_hgr_fill_rect(ylo, (unsigned char)(yhi - ylo + 1u), 0u, 40u, 0u);
    }
    gen2_set_draw_page(2u);
    gen2_hgr_clear(0u);                  /* page 2: every line at once */
    for (i = 0u; i < NLINES; ++i)
        gfx_line(lines[i].x0, lines[i].y0, lines[i].x1, lines[i].y1);
    b = GEN2_HGR2;
    for (p = 0u; p < 32u; ++p) {
        j = 0u;
        do { sum += b[j]; } while (++j != 0u);
        b += 256;
    }
    MB[1] = bad;
    MB[2] = first;
    MB[3] = (unsigned char)NLINES;
    MB[4] = (unsigned char)(sum & 0xFFu);
    MB[5] = (unsigned char)(sum >> 8);
    MB[0] = 0xA5u;                       /* magic LAST */
    for (;;) { }
}
