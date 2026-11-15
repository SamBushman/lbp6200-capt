# Big-endian PPC32 emulation test (host only, no G5): builds the UNMODIFIED filter against Tiger's CUPS 1.1.23 headers with a tiny libcups shim and
# runs it under qemu-ppc through tools/baseline/run_baseline.py, comparing every wire frame with the Linux golden capture.
# needs: qemu-user, gcc-powerpc-linux-gnu, libc6-dev-powerpc-cross ;  TIGER_INC = copy of the G5's /usr/include (ref/tiger/usr/include)
TIGER_INC ?= $(HOME)/lbp6200/ref/tiger/usr/include
OUT ?= $(HOME)/lbp6200/baseline/ppc
rastertocapt6200-ppc: ../../cups/rastertocapt6200.c ../../cups/rasterpage.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c cupsshim.c
	mkdir -p $(OUT)
	powerpc-linux-gnu-gcc -static -std=gnu99 -O2 -Wall -Wextra -I$(TIGER_INC) -o $(OUT)/rastertocapt6200 \
	  ../../cups/rastertocapt6200.c ../../cups/rasterpage.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c cupsshim.c \
	  $(HOME)/lbp6200/tools/baseline/fakeclock_static.c
check-ppc: rastertocapt6200-ppc
	cd $(HOME)/lbp6200/tools/baseline && cc -O2 -o ras2plain ras2plain.c -lcups && \
	./run_baseline.py run --bin ../../src/cups --out $(OUT)/run --emu qemu-ppc --filter-bin $(OUT)/rastertocapt6200 >/dev/null && \
	./run_baseline.py compare ../../baseline/golden $(OUT)/run
