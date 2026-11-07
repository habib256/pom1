# demo_sprite_animals — GEN2 HGR x2 colour-sprite fauna (C / cc65).
#
# HGR port of sketchs/tms9918/demo_sprite_animals: four SCROLL-O-SPRITES "Fauna"
# silhouettes as x2 colour sprites, bobbing in double-buffer.
#
# Build:    make            -> ./GEN2Animals.bin (+ software/Graphic HGR/*.bin,.txt)
# Clean:    make clean
# Load:     POM1 preset 11, Load Memory > GEN2Animals.bin, then 6000R

PROJECT  := GEN2Animals
CC65DIR  := ../../../dev/cc65
LIBDIR   := ../../../dev/lib
TOOLS    := ../../../tools
GEN2CFG  := $(CC65DIR)/apple1_gen2_c.cfg
GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
GFX      := $(LIBDIR)/gfx
OUTDIR   := ../../../software/Graphic HGR

include $(APPLE1C)/apple1c.mk      # APPLE1C_SRCS / APPLE1C_INCS
include $(GEN2C)/gen2c.mk          # GEN2C_*_SRCS / GEN2C_INCS

CL65   := cl65
CFLAGS := -t none -Oirs
INCS   := $(GEN2C_INCS) $(APPLE1C_INCS) -I $(GFX)

# Minimal link set: CORE (init/clear/page-flip/vbl + blit.s) + TEXT (puts8) +
# SPRITES (blit7). The x2 sprites are baked into the .c (no runtime inflate), so
# the x2/preshift/sprengine/geom/lores modules are NOT linked — that also keeps
# the 7KB of pre-shift banks from squeezing BSS out of the $6000-$BEFF budget.
# The program + the apple1c text base as objects; the GEN2 runtime (gen2c and
# its gfx half) as ONE archive, so ld65 links only the members this program
# calls (dev/lib/gen2c/gen2c.mk).
SRCS := $(PROJECT).c $(APPLE1C_SRCS)

.PHONY: all clean FORCE
.DEFAULT_GOAL := all

# Always ask the library's own Makefile: it rebuilds only stale members.
$(GEN2C_LIB): FORCE
	@$(MAKE) --no-print-directory -C $(GEN2C) lib
FORCE:

all: $(PROJECT).bin

$(PROJECT).bin: $(PROJECT).c $(GEN2C)/gen2.h $(GEN2C_LIB) $(GEN2CFG)
	$(CL65) $(CFLAGS) -C $(GEN2CFG) $(INCS) $(SRCS) $(GEN2C_LIB) -o $(PROJECT).bin
	@ls -l $(PROJECT).bin
	@python3 $(TOOLS)/emit_gen2_txt.py $(PROJECT).bin "$(OUTDIR)/$(PROJECT).bin" "$(OUTDIR)/$(PROJECT).txt" 2>/dev/null || \
	 cp $(PROJECT).bin "$(OUTDIR)/$(PROJECT).bin"

clean:
	rm -f $(PROJECT).bin *.o
