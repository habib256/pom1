/* gen2_colorize.c — NTSC artifact tint of a pixel rectangle
 * (gen2_hgr_colorize). Split from gen2_rect.c so a monochrome program does
 * not link it. Inner loop: gen2_colorize_asm.s. */

#include "gen2.h"
#include "gen2_internal.h"

/* Map a GEN2_* colour constant onto the colorize carrier parameter block
 * (carrier bytes + palette high bit, verified against the GEN2 renderer):
 *   violet even=$55 odd=$2A hi=0 ; green even=$2A odd=$55 hi=0 ;
 *   orange = green|hi ; blue = violet|hi.
 * Returns 1 for a known artifact colour, 0 for white/unknown. Duplicated in
 * gen2_text.c (same name, both static) so a text-only program does not pull
 * gen2_colorize.c, and a graphics-only program does not pull gen2_text.c. The
 * function is ~30 bytes after cc65 codegen — duplicating costs less than
 * either module being force-linked. */
static unsigned char gen2_set_carrier(unsigned char color)
{
    switch (color) {
        case GEN2_GREEN:  gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x00u; break;
        case GEN2_ORANGE: gen2_z_ce = 0x2Au; gen2_z_co = 0x55u; gen2_z_hi = 0x80u; break;
        case GEN2_BLUE:   gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x80u; break;
        case GEN2_VIOLET: gen2_z_ce = 0x55u; gen2_z_co = 0x2Au; gen2_z_hi = 0x00u; break;
        default: return 0u;                 /* unknown / white -> leave as drawn */
    }
    return 1u;
}

/* Tint an arbitrary PIXEL rectangle to one of the four artifact colours — the
 * graphics analogue of gen2_hgr_puts_color (for food / sprites / bars, not text).
 * Draw the shape WHITE first, then call this to recolour it. x,w,y,h are pixels;
 * the covering byte-column box [x..x+w) is tinted (HIRES colour is byte-granular,
 * so keep ~1 empty cell between differently-coloured shapes). Black pixels in the
 * box stay black (only the palette bit flips), so an isolated shape tints clean. */
void gen2_hgr_colorize(unsigned x, unsigned char y, unsigned char w,
                       unsigned char h, unsigned char color)
{
    unsigned right;

    /* gen2_colorize_asm() indexes gen2_rowlo/gen2_rowhi by scanline; those
     * tables are zero-init BSS until gen2_build_tables() fills them. Every other
     * drawing entry point builds them first, so colorize must too — otherwise,
     * as the first gen2c call, each scanline base reads $0000 and writes land in
     * low RAM. Idempotent (guarded by gen2_tables_ready). */
    gen2_build_tables();

    if (w == 0u || h == 0u || y > 191u) return;
    if (!gen2_set_carrier(color)) return;
    if ((unsigned)y + h > 192u) h = (unsigned char)(192u - y);

    right = x + (unsigned)w - 1u;
    if (right > 279u) right = 279u;
    gen2_z_col0  = (unsigned char)(x / 7u);
    gen2_z_ncols = (unsigned char)(right / 7u - x / 7u + 1u);
    gen2_z_y0    = y;
    gen2_z_rows  = h;
    gen2_colorize_asm();
}
