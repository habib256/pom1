# gfx.mk — source lists of the card-neutral gfx layer. Set GFX before including.
#
# One list per role, shared by gfx/Makefile (gfx-gen2.lib / gfx-tms.lib) and
# gen2c/Makefile (gen2c.lib carries the GEN2 half). KEEP IN SYNC with the
# DevBench specs dev/bench/gen2c.json + tms9918c.json (and their compiled-in
# copies in src/Pom1BenchCc65.cpp) and tools/build_codetank_rom.py.
#
# Never give a .c here the stem of a hand-written .s: cl65 -c writes its
# intermediate x.s next to the source (gfx/Makefile refuses to parse if so).

# gfx_line is per card: GEN2 takes the asm gfx_line_gen2.s (same contract and
# pixels as gfx_line.c, pinned by micro-test t39_c_line), TMS the portable C.
GFX_NEUTRAL_SRCS := $(addprefix $(GFX)/,gfx_rect.c gfx_circle.c gfx_ellipse.c \
                    gfx_num_dec.s gfx_num_signed.s gfx_num_hex.s \
                    gfx_text.c gfx_putu.c gfx_puti.c gfx_putx.c)
GFX_GEN2_SRCS    := $(addprefix $(GFX)/,gfx_line_gen2.s gfx_backend_gen2.c gfx_backend_gen2_rect.c \
                    gfx_text_backend_gen2.c)
GFX_TMS_SRCS     := $(addprefix $(GFX)/,gfx_line.c gfx_backend_tms.c gfx_backend_tms_rect.c \
                    gfx_text_backend_tms.c)
