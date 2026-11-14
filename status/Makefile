CFLAGS=-std=gnu99 -Wall -Wextra -O2
lbp6200-status: lbp6200-status.c ../core/capt.c ../core/capt.h ../hiscoa/hiscoa-compress.c ../hiscoa/hiscoa-common.c
	$(CC) $(CFLAGS) -o $@ lbp6200-status.c ../core/capt.c ../hiscoa/hiscoa-compress.c ../hiscoa/hiscoa-common.c
