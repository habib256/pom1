# Makefile — build the card-neutral gfx layer (axis 1 of the GEN2/TMS9918
# graphics-library factoring; see README.md and dev/TODO6502.md).
#
# Output = two cc65 LIBRARY ARCHIVES (ar65), one per video card:
#
#   gfx-gen2.lib = gfx geometry (line/rect/circle/ellipse) + gfx_num (dec/signed/hex) + cell text + gfx_backend_gen2   (GEN2 HGR,   280x192)
#   gfx-tms.lib  = gfx geometry (line/rect/circle/ellipse) + gfx_num (dec/signed/hex) + cell text + gfx_backend_tms    (TMS9918 bm, 256x192)
#
# (GEN2 Makefile programs normally link gen2c.lib instead, which carries the
# GEN2 half of this layer: see dev/lib/gen2c/gen2c.mk.)
#
# WHY AN ARCHIVE (not a list of .o on the link line). ld65 pulls only the
# modules a program actually references out of a .lib. Until the destructive
# rewiring lands (gen2.c / screen2.c forwarding to gfx_*), NOTHING references
# gfx_line/gfx_circle/gfx_utoa/..., so linking gfx-<card>.lib contributes
# *zero* bytes — verified: GEN2Snake is byte-identical with vs without the lib
# on its link line. Force-listing the .c sources instead would force-link the
# whole layer (+2888 bytes of dead code, pulling cc65's soft mul/divide for the
# ellipse and gfx_num). The archive is therefore the only *purely additive*
# way to wire gfx into a size-sensitive build path before rewiring.
#
# A GEN2 program links gfx-gen2.lib; a TMS9918 program links gfx-tms.lib. They
# never coexist (Parmigiani's "one board at a time"), so the backend symbols
# (gfx_plot/hline/vline/width/height) resolve unambiguously at link time.
#
# Usage:
#   make            # build both archives
#   make gen2       # just gfx-gen2.lib
#   make tms        # just gfx-tms.lib
#   make check      # compile-verify every TU against both backends + list modules
#   make clean
#
# Consumers add  -I <this dir>  and put  gfx-<card>.lib  on their cl65/ld65 line
# AFTER their own sources (see README "Build integration").

# Toolchain (CL65/AR65) + the -t none -c -Oirs convention + the cl65 "-o before
# input" quirk: single source of truth, shared with dev/lib/Makefile. Every rule
# below writes "-o $@ $<", never "$< -o $@".
include ../common.mk

GFXINC  := -I .
GEN2INC := -I ../gen2c                       # gen2.h  (GEN2 backend)
TMSINC  := -I ../tms9918c  # screen2.h (TMS backend)

.PHONY: all gen2 tms check clean

all: gfx-gen2.lib gfx-tms.lib
gen2: gfx-gen2.lib
tms:  gfx-tms.lib

# --- card-neutral translation units (include only gfx.h, backend-agnostic) ---
# Geometry split per primitive — a rect-only program skips line + circle +
# ellipse code via ld65 dead-strip (per-.o granularity).
gfx_line.o: gfx_line.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_rect.o: gfx_rect.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_circle.o: gfx_circle.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_ellipse.o: gfx_ellipse.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
# Number formatters: hand-written 6502, one member per entry so a hex-only
# program skips the decimal converter and vice versa. gfx_num_dec.s also holds
# the ONE 16-bit decimal converter (gfx_u16_dec) gen2c aliases as _gen2_utoa.
gfx_num_dec.o: gfx_num_dec.s
	$(CL65) $(CFLAGS) -o $@ $<
gfx_num_signed.o: gfx_num_signed.s
	$(CL65) $(CFLAGS) -o $@ $<
gfx_num_hex.o: gfx_num_hex.s
	$(CL65) $(CFLAGS) -o $@ $<
# Positioned-text cursor façade (axis 3). Card-neutral: the cursor + advance/wrap
# + number formatting; the per-card cell blit is in gfx_text_backend_<card>.c.
gfx_text.o: gfx_text.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
# Number writers at the cursor, one member each (pom2games 4dcede8): a
# string-only gfx_text program links no converter.
gfx_putu.o: gfx_putu.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_puti.o: gfx_puti.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_putx.o: gfx_putx.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<

# --- per-card backends (need the card's header) ---
# Each card has TWO backend TUs: a "core" with the per-pixel + span primitives
# (plot/hline/vline + extents) and a "rect" with the filled_rect + clear
# forwarders. A program that only draws lines/circles links the core but not
# the rect TU — ld65 dead-strips the rect TU and the heavier card helpers
# (gen2_hgr_fill_pixrect or screen2_filled_rect/screen2_clear) it would
# otherwise drag in.
gfx_backend_gen2.o: gfx_backend_gen2.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(GEN2INC) -o $@ $<
gfx_backend_gen2_rect.o: gfx_backend_gen2_rect.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(GEN2INC) -o $@ $<
gfx_backend_tms.o: gfx_backend_tms.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(TMSINC) -o $@ $<
gfx_backend_tms_rect.o: gfx_backend_tms_rect.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(TMSINC) -o $@ $<
# Per-card cell-glyph backends for the gfx_text façade (axis 3): a cell -> the
# card's native 8x8 blit (GEN2 gen2_hgr_puts8 / TMS9918 screen2_putc).
gfx_text_backend_gen2.o: gfx_text_backend_gen2.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(GEN2INC) -o $@ $<
gfx_text_backend_tms.o: gfx_text_backend_tms.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(TMSINC) -o $@ $<

# Shared per-card object list (every program either gets these via dead-strip
# or skips them — that's the whole point of the split).
# The member lists come from gfx.mk (shared with gen2c/Makefile).
GFX := .
include gfx.mk
gfx_objs = $(notdir $(addsuffix .o,$(basename $(1))))
GFX_NEUTRAL_OBJS := $(call gfx_objs,$(GFX_NEUTRAL_SRCS))

# --- archives (rm first so ar65 a never accumulates stale members) ---
gfx-gen2.lib: $(GFX_NEUTRAL_OBJS) $(call gfx_objs,$(GFX_GEN2_SRCS))
	rm -f $@
	$(AR65) a $@ $^
gfx-tms.lib: $(GFX_NEUTRAL_OBJS) $(call gfx_objs,$(GFX_TMS_SRCS))
	rm -f $@
	$(AR65) a $@ $^

# Compile-verification: building both archives compiles every shared TU against
# both backends (the blocking "compile-check the shared TUs" step). The
# module dump confirms the expected exports are present.
check: gfx-gen2.lib gfx-tms.lib
	@echo "== gfx-gen2.lib modules =="; $(AR65) t gfx-gen2.lib
	@echo "== gfx-tms.lib  modules =="; $(AR65) t gfx-tms.lib
	@echo "[gfx] compile-verify OK (both backends)."

# NEVER `rm *.s` here: gfx_num_*.s are hand-written sources. (cl65 -c on a .c
# writes its intermediate x.s next to the source and deletes it, so a .c must
# never share its stem with a hand-written .s in this directory — guarded below.)
clean:
	rm -f *.o *.lib

$(foreach c,$(wildcard *.c),$(if $(wildcard $(c:.c=.s)),$(error $(c) shares its stem with a hand-written $(c:.c=.s): cl65 -c would overwrite it)))
