# Tiger filter (also builds on a modern host against a current libcups: the CUPS-1.1 differences live in cups/cups_compat.h)
all: rastertolbpc
CFLAGS=-std=gnu99 -Wall -Wextra -O2
SRC=rastertolbpc.c ../cups/rasterpage.c ../lbpc/lbpc_write.c ../core/capt.c ../hiscoa/hiscoa-compress.c ../hiscoa/hiscoa-common.c
rastertolbpc: $(SRC) ../core/capt.h ../cups/rasterpage.h ../cups/cups_compat.h ../lbpc/lbpc.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lcups
