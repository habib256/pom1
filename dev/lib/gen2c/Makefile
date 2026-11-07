# dev/lib/gen2c/Makefile — build gen2c.lib, the GEN2 HGR C runtime archive.
#
#   make -C dev/lib/gen2c lib     -> dev/lib/gen2c/gen2c.lib
#   make -C dev/lib/gen2c check   archive + member list
#   make -C dev/lib/gen2c clean
#
# The archive = every gen2c member (GEN2C_ALL_SRCS, gen2c.mk) + the GEN2 half
# of the card-neutral gfx layer (GFX_NEUTRAL_SRCS + GFX_GEN2_SRCS, gfx/gfx.mk).
# Both libraries reference each other and ld65 never rescans a library listed
# earlier, so one archive is what makes any link order work. A program links
# only the members it references (each asm kernel is its own member, owning
# the zero page it needs).
#
# Objects go to obj/ (gitignored). C sources are compiled with -S INTO obj/
# and assembled from there: cl65 -c on a .c writes its intermediate x.s next
# to the SOURCE, which would overwrite a hand-written .s of the same stem
# (gen2_hgr_x2.c / gen2_hgr_x2.s live side by side here).

include ../common.mk

GEN2C   := .
GFX     := ../gfx
APPLE1C := ../apple1c
include gen2c.mk
include $(GFX)/gfx.mk

LIB_SRCS := $(GEN2C_ALL_SRCS) $(GFX_NEUTRAL_SRCS) $(GFX_GEN2_SRCS)
OBJDIR   := obj
CINC     := -I $(GEN2C) -I $(APPLE1C) -I $(GFX)
CCFLAGS  := -t none -Oirs
DEPS     := $(wildcard *.h *.inc $(GFX)/*.h $(APPLE1C)/*.h ../gen2/*.inc) \
            ../gen2/gen2_init.asm gen2c.mk $(GFX)/gfx.mk Makefile ../common.mk

obj_of = $(OBJDIR)/$(notdir $(basename $(1))).o
LIB_OBJS := $(foreach s,$(LIB_SRCS),$(call obj_of,$(s)))
ifneq ($(words $(LIB_OBJS)),$(words $(sort $(LIB_OBJS))))
$(error gen2c.lib: two sources share an archive member name)
endif

.PHONY: lib check clean
lib: $(GEN2C_LIB)

define compile_one
$(call obj_of,$(1)): $(1) $(DEPS) | $(OBJDIR)
$(if $(filter %.c,$(1)),	$$(CL65) $$(CCFLAGS) $$(CINC) -S -o $$(@:.o=.s) $$<
	$$(CL65) $$(CCFLAGS) -c -o $$@ $$(@:.o=.s)
	@rm -f $$(@:.o=.s),	$$(CL65) $$(CCFLAGS) -c $$(CINC) -o $$@ $$<)
endef
$(foreach s,$(LIB_SRCS),$(eval $(call compile_one,$(s))))

$(OBJDIR):
	mkdir -p $@

# rm first so ar65 a never accumulates stale members.
$(GEN2C_LIB): $(LIB_OBJS)
	rm -f $@
	$(AR65) a $@ $(LIB_OBJS)

check: $(GEN2C_LIB)
	@echo "== gen2c.lib members =="; $(AR65) t $(GEN2C_LIB)
	@echo "[gen2c] archive OK ($(words $(LIB_OBJS)) members)"

clean:
	rm -rf $(OBJDIR) $(GEN2C_LIB)
