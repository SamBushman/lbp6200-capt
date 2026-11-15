# Big-endian PPC32 emulation test (host only, no Mac needed): builds the UNMODIFIED filter against Mac OS X 10.4's CUPS 1.1.23 headers with a tiny
# libcups shim and runs it under qemu-ppc through tools/baseline/run_baseline.py, comparing every wire frame with the Linux golden capture (test/golden).
# needs: qemu-user, gcc-powerpc-linux-gnu, libc6-dev-powerpc-cross (Debian names), libcups-dev for the native raster tools
# TIGER_INC must point at a copy of a Tiger system's /usr/include (only the cups/ headers are used); it is not part of this repository.
TIGER_INC ?= $(HOME)/lbp6200/ref/tiger/usr/include
OUT ?= out
TOOLS = ../../tools/baseline
FILTER_SRC = ../../cups/rastertocapt6200.c ../../cups/rasterpage.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c
$(OUT)/rastertocapt6200: $(FILTER_SRC) cupsshim.c $(TOOLS)/fakeclock_static.c ../../cups/rasterpage.h ../../cups/cups_compat.h ../../core/capt.h
	mkdir -p $(OUT)
	powerpc-linux-gnu-gcc -static -std=gnu99 -O2 -Wall -Wextra -I$(TIGER_INC) -o $@ $(FILTER_SRC) cupsshim.c $(TOOLS)/fakeclock_static.c
check-ppc: $(OUT)/rastertocapt6200
	$(MAKE) -C ../../cups
	cc -O2 -o $(TOOLS)/ras2plain $(TOOLS)/ras2plain.c -lcups
	cc -O2 -o $(TOOLS)/gen_rasters $(TOOLS)/gen_rasters.c -lcups
	cc -shared -fPIC -O2 -o $(TOOLS)/fakeclock.so $(TOOLS)/fakeclock.c -ldl
	$(TOOLS)/run_baseline.py run --bin ../../cups --out $(OUT)/run --emu qemu-ppc --filter-bin $(CURDIR)/$(OUT)/rastertocapt6200 > /dev/null
	$(TOOLS)/run_baseline.py compare ../golden $(OUT)/run
