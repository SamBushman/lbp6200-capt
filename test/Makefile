CFLAGS=-std=c99 -Wall -Wextra -O2
all: replay_test status_test
replay_test: replay_test.c ../core/capt.c ../core/capt.h
	$(CC) $(CFLAGS) -o $@ replay_test.c ../core/capt.c ../hiscoa/hiscoa-compress.c ../hiscoa/hiscoa-common.c
status_test: status_test.c ../core/capt.c ../core/capt.h
	$(CC) $(CFLAGS) -o $@ status_test.c ../core/capt.c ../hiscoa/hiscoa-compress.c ../hiscoa/hiscoa-common.c
check: replay_test status_test
	./replay_test
	./status_test
L ?= $(HOME)/lbp6200/ref/canon-common/cndrvcups-common-3.21/libs
W ?= ../../evidence/capture-002/wire-c0a0.bin
raster_test: raster_test.c ../core/capt.c ../hiscoa/hiscoa-compress.c ../hiscoa/hiscoa-common.c
	gcc -m32 -std=gnu99 -O1 -Wall -o $@ raster_test.c ../core/capt.c ../hiscoa/hiscoa-compress.c ../hiscoa/hiscoa-common.c -ldl
check32: raster_test
	LD_LIBRARY_PATH=$(L) ./raster_test $(L)/libcanon_slim.so.1.0.0 $(W)
