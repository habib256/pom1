/* gen2_init.c — the C half of the CORE: soft-switch sink, page bases, LORES
 * state, wait-VBL.
 *
 * Always linked. Carries the soft-switch sink, the HIRES page base, the LORES
 * table + state (kept here so the asm gen2_set_draw_page can shift them
 * without linking LORES) and gen2_wait_vbl. The lookup tables, their builder
 * and gen2_hgr_row are asm in gen2_tables.s; gen2_set_draw_page /
 * gen2_show_page in gen2_page.s (both CORE too). See gen2.h for the public
 * API doc; see gen2_internal.h for what the other modules import.
 *
 * NO INITIALIZED GLOBALS (the BSS-zero-default rule). The cc65 -t none crt0
 * used by every consumer has NO copydata: a DATA-segment initializer is only
 * "applied" because the loader happened to place the bytes — it is one-shot
 * under load==run configs (a warm re-run inherits the previous run's values)
 * and pure garbage under ROM configs (DATA in ROM is never copied to RAM).
 * Even `= 0` lands a variable in DATA. So every global here is uninitialized
 * (BSS, zeroed by zerobss at EVERY entry) and encodes its default as zero:
 * flags default clear, gen2_draw_page2 (gen2_page.s) zero = page 1, and the
 * page bases (for which 0 is never a legal value) are lazily defaulted at
 * their read sites (gen2_hgr_row / gen2_build_tables in gen2_tables.s,
 * gen2_lores_tab.s / _clr.s for LORES, _gen2_hgr_clear in gen2_blit.s). */

#include "gen2.h"
#include "gen2_internal.h"

/* Sink for the individual soft-switch macros. A soft-switch READ is the toggle;
 * its value is meaningless. cc65 -Oirs drops a volatile read cast to void, so
 * the macros store the read into this sink — a store cc65 cannot prove dead, so
 * the (volatile) load that feeds it is always emitted. The whole-mode init
 * routines gen2_hgr_init / gen2_lores_init live in asm (gen2_blit.s) for the
 * same reason; see the note there and gen2.h. */
volatile unsigned char gen2_ss_sink;

/* HIRES draw-page high-byte base: $20 = page 1 ($2000), $40 = page 2 ($4000).
 * GLOBAL (not static) on purpose: the asm _gen2_hgr_clear imports _gen2_hgr_base
 * so it erases whichever page is currently being drawn. Set by
 * gen2_set_draw_page. BSS (no initializer — see the header): 0 means "unset",
 * lazily defaulted to $20 (page 1) at every read site that can run before
 * gen2_set_draw_page — gen2_hgr_row / gen2_build_tables (gen2_tables.s)
 * and _gen2_hgr_clear in gen2_blit.s. 0 is never a legal base, so the encoding is unambiguous. */
unsigned char gen2_hgr_base;

/* gen2_hgr_row (the scanline base of the CURRENT draw page) and the lookup
 * tables + their builder are asm: gen2_tables.s. The draw-page setter and the
 * display flip are asm: gen2_page.s. */

/* Double-sampled HST0 (the burst notch can zero one of two close reads). */
static unsigned char gen2_blank(void)
{
    return (unsigned char)((GEN2_SS[7] | GEN2_SS[7]) & GEN2_HST0);
}

/* Tell V-blank (lines 192+, ~4550 CPU cycles of sustained blank) from a per-line
 * H-blank (~25 cycles) by counting CONSECUTIVE blank samples. The threshold must
 * sit between the two: gen2_blank() is a ~40-50 cycle call (pointer rebuild + two
 * indirect reads + jsr/rts), so one H-blank yields at most ~1-2 samples while a
 * V-blank yields ~90 (4550/50). 4 is safely above the H-blank noise yet returns
 * EARLY in V-blank (~3 lines in) -- which maximises the V-blank time left for the
 * caller's redraw, the difference between a single-buffer erase+redraw fitting
 * (no beam-race tearing) or spilling into the visible scan. NB: the original
 * value (400) was UNREACHABLE (needs ~20000 cyc of blank vs ~4550) so wait_vbl
 * hung; 16 worked but returned ~30 lines into V-blank, eating the redraw budget. */
#define GEN2_VBL_SAMPLES 4u
void gen2_wait_vbl(void)
{
    unsigned n;
    while (gen2_blank()) { }            /* wait for the live raster */
    for (;;) {
        while (!gen2_blank()) { }        /* wait for a blanking edge */
        for (n = 0; n < GEN2_VBL_SAMPLES && gen2_blank(); ++n) { }
        if (n >= GEN2_VBL_SAMPLES) return;   /* sustained blank => V-blank */
        /* short blank => it was just an H-blank; find the next edge */
    }
}

/* LORES tables + state live here so gen2_set_draw_page can shift them in
 * place without forcing the LORES modules (gen2_lores_*.s) to link. A HIRES-only program pays the
 * 50 bytes (gen2_lo_rowlo[24] + gen2_lo_rowhi[24] + 2 flags) but does NOT
 * pull in the 8 LORES drawing functions (~200 bytes). gen2_lo_ready stays 0
 * until gen2_lores_tab.s's first build call, so gen2_set_draw_page leaves the
 * table alone for HIRES-only programs and only records gen2_lo_base.
 * gen2_lo_base is BSS like gen2_hgr_base: 0 = unset, lazily defaulted to $04
 * (page 1) by its readers in gen2_lores_tab.s / gen2_lores_clr.s. */
unsigned char gen2_lo_rowlo[24];
unsigned char gen2_lo_rowhi[24];
unsigned char gen2_lo_base;
unsigned char gen2_lo_ready;
