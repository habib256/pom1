# gen2c.mk — Makefile fragment for the GEN2 HGR C runtime.
#
# RECOMMENDED: link the ARCHIVE. ld65 links an object named on the command
# line whole, but pulls from a .lib only the members a program references —
# and the runtime is now one member per kernel (each asm member owns the zero
# page it needs), so the archive is what makes the split pay. The archive also
# carries the GEN2 half of dev/lib/gfx (geometry, numbers, cell text, the GEN2
# backends): gen2c and gfx reference each other, and ld65 does not rescan a
# library listed earlier, so ONE archive is the only order-proof link.
#
#     LIBDIR  := ../../../dev/lib
#     GEN2C   := $(LIBDIR)/gen2c
#     APPLE1C := $(LIBDIR)/apple1c
#     GFX     := $(LIBDIR)/gfx
#     include $(APPLE1C)/apple1c.mk
#     include $(GEN2C)/gen2c.mk
#
#     $(GEN2C_LIB):                       # (re)build when stale
#             $(MAKE) -C $(GEN2C) lib
#     prog.bin: prog.c $(GEN2C_LIB)
#             cl65 -t none -Oirs -C apple1_gen2_c.cfg $(GEN2C_INCS) $(APPLE1C_INCS) \
#                  -I $(GFX) prog.c $(APPLE1C_SRCS) $(GEN2C_LIB) -o prog.bin
#
# The per-family *_SRCS variables below still name the sources of each family
# (the archive is built from GEN2C_ALL_SRCS; dev/lib's compile gate walks them).
# Linking them as OBJECTS still works but links every listed kernel whole.
# Set GEN2C (and GFX for GEN2C_LIB_SRCS) BEFORE including this fragment.
#
# KEEP IN SYNC: dev/bench/gen2c.json and its compiled-in copy kBenchCSpecGen2c
# (src/Pom1BenchCc65.cpp) list the same sources for the DevBench rt.lib.

# CORE: soft-switch sink, wait_vbl (gen2_init.c), the lookup tables + their
# builder + gen2_hgr_row (gen2_tables.s), the draw-page setter + flip
# (gen2_page.s), the soft-switch mode entries, the HIRES clear and every asm
# kernel member. A
# member no program references is never extracted from the archive.
GEN2C_ASM_SRCS     := $(addprefix $(GEN2C)/,gen2_mode_asm.s gen2_mode_clear_asm.s \
                      gen2_lores_init_asm.s gen2_hgr_clear_asm.s gen2_pen_asm.s \
                      gen2_text_params.s gen2_carrier_params.s gen2_text16_asm.s \
                      gen2_text8_asm.s gen2_fill_rect_asm.s gen2_pixel_asm.s \
                      gen2_pixrect_asm.s gen2_cell_asm.s gen2_colorize_asm.s \
                      gen2_blit_params.s gen2_blit_asm.s gen2_blit7_asm.s \
                      gen2_preshift_asm.s gen2_tables.s gen2_page.s)
GEN2C_CORE_SRCS    := $(GEN2C)/gen2_init.c $(GEN2C_ASM_SRCS)

# Drawing families — pick the ones your program actually calls.
GEN2C_PIXEL_SRCS   := $(GEN2C)/gen2_pixel.c     # gen2_hgr_plot/unplot
# RECT: fill/clear_pixrect + fill_box / fill_rect / cell / colorize, one module each
GEN2C_RECT_SRCS    := $(addprefix $(GEN2C)/,gen2_rect.c gen2_fill_rect.c gen2_cell.c gen2_colorize.c)
# TEXT: the glyph core (gen2_text.c: puts / puts_color / puts8 + BBFont) and
# the number writers, one module per entry family (putu/puti/putu8, putu_field,
# putx) so a string-only program links no converter. gen2_utoa_asm.s is the
# legacy _gen2_utoa alias of gfx's converter (needs the gfx members).
GEN2C_TEXT_SRCS    := $(addprefix $(GEN2C)/,gen2_text.c gen2_text_num.c gen2_text_field.c \
                      gen2_text_hex.c gen2_utoa_asm.s)
GEN2C_SPRITES_SRCS := $(GEN2C)/gen2_sprites.c   # hgr_blit / hgr_blit7
GEN2C_X2_SRCS      := $(GEN2C)/gen2_hgr_x2.s    # gen2_hgr_inflate_x2 — hand-asm (the C miscompiles under -Oirs; gen2_hgr_x2_ref.c is the host-only ref)
GEN2C_X2BLIT_SRCS  := $(GEN2C)/gen2_hgr_blit_x2.c # gen2_hgr_blit_x2 (au-vol; needs X2 + CORE, owns a 256 B buffer)
GEN2C_PRESHIFT_SRCS:= $(GEN2C)/gen2_preshift.c  # hgr_sprite (Buzzard-Bait 7-phase pre-shift; needs only CORE -- reuses gen2_blit7_run)
GEN2C_SPRMASK_SRCS := $(GEN2C)/gen2_sprmask.s   # masked pre-shifted blit + save-under/restore kernels (needs only CORE)
GEN2C_SPRENGINE_SRCS:= $(GEN2C)/gen2_sprengine.c # gen2_spr_* sprite engine (needs SPRMASK + CORE; owns a 1.5 KB BSS under-buffer pool)
GEN2C_GEOM_SRCS    := $(GEN2C)/gen2_geom.c      # hline / vline / line / rect / circle / ellipse (uses dev/lib/gfx)
# LORES 40x48 colour blocks, all asm: row-table builder / clear / set+get one
# block / hlin+vlin+fill_rect (one clipped kernel), one member each.
GEN2C_LORES_SRCS   := $(addprefix $(GEN2C)/,gen2_lores_tab.s gen2_lores_clr.s \
                      gen2_lores_dot.s gen2_lores_run.s)

# Umbrella — every family at once.
GEN2C_ALL_SRCS := $(GEN2C_CORE_SRCS) \
                  $(GEN2C_PIXEL_SRCS) \
                  $(GEN2C_RECT_SRCS) \
                  $(GEN2C_TEXT_SRCS) \
                  $(GEN2C_SPRITES_SRCS) \
                  $(GEN2C_X2_SRCS) \
                  $(GEN2C_X2BLIT_SRCS) \
                  $(GEN2C_PRESHIFT_SRCS) \
                  $(GEN2C_SPRMASK_SRCS) \
                  $(GEN2C_SPRENGINE_SRCS) \
                  $(GEN2C_GEOM_SRCS) \
                  $(GEN2C_LORES_SRCS)

# The archive (built by `make -C $(GEN2C) lib`, see gen2c/Makefile): every
# gen2c member + the GEN2 half of dev/lib/gfx (gfx/gfx.mk).
GEN2C_LIB := $(GEN2C)/gen2c.lib

# Include paths. GEN2C_GEOM_SRCS *and* GEN2C_TEXT_SRCS call gfx_* — the archive
# carries those members; an object-list consumer must also link gfx-gen2.lib
# (built by `make -C dev/lib/gfx gen2`) when it includes either.
GEN2C_INCS := -I $(GEN2C)
