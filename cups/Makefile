CFLAGS=-std=gnu99 -Wall -Wextra -O2
SRC=rastertocapt6200.c ../core/capt.c ../hiscoa/hiscoa-compress.c ../hiscoa/hiscoa-common.c
rastertocapt6200: $(SRC) ../core/capt.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lcups
