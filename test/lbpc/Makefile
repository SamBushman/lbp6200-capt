all: lbpc_feed
lbpc_feed: lbpc_feed.c ../../lbpc/lbpc_drive.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c ../../lbpc/lbpc.h
	$(CC) -std=gnu99 -Wall -Wextra -O2 -o $@ lbpc_feed.c ../../lbpc/lbpc_drive.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c
lbpc_fuzz: lbpc_fuzz.c mockprinter.h ../../lbpc/lbpc_drive.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c ../../lbpc/lbpc.h
	$(CC) -std=gnu99 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ lbpc_fuzz.c ../../lbpc/lbpc_drive.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c

# Host checks of the Tiger code path (no Mac, no CUPS scheduler): native filter -> records -> driver library over a pty mock printer, every wire frame
# compared with the Linux golden capture; then the sanitizer mutation fuzz of the record parser.  (Big-endian PPC32: make -C ../ppc check-ppc-lbpc)
TOOLS = ../../tools/baseline
check: lbpc_feed lbpc_fuzz
	cc -std=gnu99 -w -o out/xact_stale xact_stale.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c && out/xact_stale
	$(MAKE) -C ../../tiger
	$(MAKE) -C ../../cups
	cc -O2 -o $(TOOLS)/ras2plain $(TOOLS)/ras2plain.c -lcups
	cc -O2 -o $(TOOLS)/gen_rasters $(TOOLS)/gen_rasters.c -lcups
	cc -shared -fPIC -O2 -o $(TOOLS)/fakeclock.so $(TOOLS)/fakeclock.c -ldl
	$(TOOLS)/run_baseline.py run --bin ../../cups --out out/run --filter-bin $(CURDIR)/../../tiger/rastertolbpc --feed $(CURDIR)/lbpc_feed > /dev/null
	$(TOOLS)/run_baseline.py compare ../golden out/run
	mkdir -p out && TZ=PST8PDT $(TOOLS)/gen_rasters a5 1 2 1 | ../../tiger/rastertolbpc 1 u t 1 "" > out/a5_2p_dx.lbpc
	for s in 1 2 3; do ./lbpc_fuzz out/a5_2p_dx.lbpc 1000 $$s; done
