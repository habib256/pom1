/*
 * gfx_circle.c — card-neutral midpoint circle outline. See gfx.h.
 *
 * 8-way symmetry, every plotted point clipped to [0,gfx_width) x
 * [0,gfx_height) (int coords so a centre±radius that runs off an edge is
 * dropped, not wrapped — a uchar/unsigned cast of a negative coord would
 * alias back into a valid pixel).
 *
 * Split from the former gfx_draw.c so a line-only program skips the symmetry loop.
 */
#include "gfx.h"

static void gfx_plot_clip(int x, int y)
{
    /* Negative coordinates become large unsigned values: one test per axis. */
    if ((unsigned)x < gfx_width && (unsigned)y < gfx_height)
        gfx_plot((unsigned)x, (unsigned char)y);
}

void gfx_circle(unsigned xc, unsigned char yc, unsigned char r)
{
    int cx, cy = (int)yc;
    unsigned char x = 0, y = r;
    int d = 1 - (int)r;
    /* Reject circles wholly to the right before narrowing an unsigned
     * centre to cc65's signed 16-bit int. Otherwise e.g. 65535 becomes -1
     * and its right-hand arc paints the left edge of the screen. Subtract
     * only after comparing, so neither unsigned subtraction nor addition
     * can wrap. Centres just beyond the edge still have visible arcs. */
    if (xc >= gfx_width && xc - gfx_width >= (unsigned)r) return;
    if (yc >= gfx_height && (unsigned)(yc - gfx_height) >= (unsigned)r) return;
    if (r == 0u) { gfx_plot(xc, yc); return; }
    /* After the rejections above, xc + r cannot wrap. */
    if (xc >= r && xc + r < gfx_width && yc >= r && (unsigned)yc + r < gfx_height) {
        /* Wholly on screen: same points and order, without clipping. */
        while (x <= y) {
            if (x != 0) {
                gfx_plot(xc + x, yc + y);
                gfx_plot(xc - x, yc - y);
            }
            gfx_plot(xc - x, yc + y);
            gfx_plot(xc + x, yc - y);
            if (x != y) {
                gfx_plot(xc + y, yc + x);
                gfx_plot(xc - y, yc + x);
                if (x != 0) {
                    gfx_plot(xc + y, yc - x);
                    gfx_plot(xc - y, yc - x);
                }
            }
            if (d < 0) { d += ((int)x << 1) + 3; }
            else       { d += ((int)x - (int)y) * 2 + 5; --y; }
            ++x;
        }
        return;
    }
    cx = (int)xc;
    while (x <= y) {
        /* At x=0, opposite x signs coincide. At x=y, exchanging the
         * coordinates repeats the first quartet. y is always positive. */
        if (x != 0) {
            gfx_plot_clip(cx + x, cy + y);
            gfx_plot_clip(cx - x, cy - y);
        }
        gfx_plot_clip(cx - x, cy + y);
        gfx_plot_clip(cx + x, cy - y);
        if (x != y) {
            gfx_plot_clip(cx + y, cy + x);
            gfx_plot_clip(cx - y, cy + x);
            if (x != 0) {
                gfx_plot_clip(cx + y, cy - x);
                gfx_plot_clip(cx - y, cy - x);
            }
        }
        if (d < 0) { d += ((int)x << 1) + 3; }
        else       { d += ((int)x - (int)y) * 2 + 5; --y; }
        ++x;
    }
}
