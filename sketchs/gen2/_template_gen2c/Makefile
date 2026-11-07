# _template_gen2c — minimal GEN2 HGR (C / cc65) starter.
#
# Build:    make            -> ./main.bin (and software/Graphic HGR/...)
# Clean:    make clean
# Load:     POM1 preset 11, Load Memory > main.bin, then 6000R

PROJECT  := main
CC65DIR  := ../../../dev/cc65
LIBDIR   := ../../../dev/lib
GEN2CFG  := $(CC65DIR)/apple1_gen2_c.cfg
GEN2C    := $(LIBDIR)/gen2c
APPLE1C  := $(LIBDIR)/apple1c
GFX      := $(LIBDIR)/gfx

include $(APPLE1C)/apple1c.mk      # APPLE1C_SRCS / APPLE1C_INCS
include $(GEN2C)/gen2c.mk          # GEN2C_*_SRCS / GEN2C_INCS

CL65   := cl65
CFLAGS := -t none -Oirs
INCS   := $(GEN2C_INCS) $(APPLE1C_INCS) -I $(GFX)

# The program + the apple1c text base as objects; the GEN2 runtime (gen2c and
# its gfx half) as ONE archive, so ld65 links only the members this program
# calls (dev/lib/gen2c/gen2c.mk).
SRCS := $(PROJECT).c $(APPLE1C_SRCS)

# Need the full GEN2 runtime instead? Swap the SRCS line above for:
#     SRCS := $(PROJECT).c $(GEN2C_ALL_SRCS) $(APPLE1C_SRCS)

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

clean:
	rm -f $(PROJECT).bin *.o
