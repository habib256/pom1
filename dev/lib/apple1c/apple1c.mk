# apple1c.mk — Makefile fragment for the Apple-1 C base.
#
# A consumer (project Makefile) includes this and references the variables
# below. Set APPLE1C in the consumer to the relative path of dev/lib/apple1c
# from the project directory BEFORE including this file. Example:
#
#     LIBDIR  := ../../../lib
#     APPLE1C := $(LIBDIR)/apple1c
#     include $(APPLE1C)/apple1c.mk
#
#     SRCS    := main.c $(APPLE1C_SRCS)
#     INCS    := $(APPLE1C_INCS)
#
# The runtime is three hand-written objects (no C): the Wozmon shim, the
# string/hexword printers and the non-blocking key readers. Objects named on
# a cl65 line are always linked; an ar65 archive of them (the DevBench's link)
# pulls only the families a program calls. The finer sets below let a
# Makefile name just what it uses.

APPLE1C_SHIM_SRCS := $(APPLE1C)/apple1io_asm.s   # woz_putc/print_hex/mon, apple1_getkey
APPLE1C_STR_SRCS  := $(APPLE1C)/apple1io_str.s   # woz_puts, woz_print_hexword
APPLE1C_KEY_SRCS  := $(APPLE1C)/apple1io_key.s   # apple1_iskeypressed, apple1_readkey
APPLE1C_SRCS := $(APPLE1C_SHIM_SRCS) $(APPLE1C_STR_SRCS) $(APPLE1C_KEY_SRCS)
APPLE1C_INCS := -I $(APPLE1C)
