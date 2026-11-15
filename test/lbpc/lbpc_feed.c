/* Host test tool: stands in for the class-driver plug-in + usb backend.  Reads an LBPC stream on stdin in 2048-byte pieces (what Apple's usb backend
 * passes to WritePipe), replays it with lbpc_drive over a device node (a pty mock printer in the tests), CUPS back-channel lines to stderr.
 * usage: lbpc_feed DEVICE [CHUNK]        Not shipped. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>
#include "../../lbpc/lbpc.h"

static int dev;
static int io_write(void *c, const uint8_t *b, size_t n)
{
	(void)c;
	while (n) {
		ssize_t k = write(dev, b, n);
		if (k < 0) { if (errno == EAGAIN || errno == EINTR) { fd_set fs; FD_ZERO(&fs); FD_SET(dev, &fs); select(dev + 1, NULL, &fs, NULL, NULL); continue; } return -1; }
		b += k; n -= (size_t)k;
	}
	return 0;
}
static int io_read(void *c, uint8_t *b, size_t n, int to)
{
	fd_set fs; struct timeval tv; int r; (void)c;
	tv.tv_sec = to / 1000; tv.tv_usec = (to % 1000) * 1000;
	FD_ZERO(&fs); FD_SET(dev, &fs);
	while ((r = select(dev + 1, &fs, NULL, NULL, &tv)) < 0 && errno == EINTR) { FD_ZERO(&fs); FD_SET(dev, &fs); }
	if (r <= 0) return r < 0 ? -1 : 0;
	{ ssize_t k = read(dev, b, n); return k < 0 ? (errno == EAGAIN ? 0 : -1) : (int)k; }
}
static void io_sleep(void *c, unsigned ms) { (void)c; usleep(ms * 1000u); }
static void lg(void *c, const char *line) { (void)c; fprintf(stderr, "%s\n", line); }

int main(int argc, char **argv)
{
	struct capt_io io; struct lbpc_drive d; uint8_t buf[65536]; size_t chunk = 2048; int e = 0;
	if (argc < 2) { fprintf(stderr, "usage: lbpc_feed DEVICE [CHUNK]\n"); return 2; }
	if (argc > 2) chunk = (size_t)atoi(argv[2]);
	if (chunk < 1 || chunk > sizeof buf) chunk = 2048;
	dev = open(argv[1], O_RDWR | O_NONBLOCK);
	if (dev < 0) { fprintf(stderr, "ERROR: cannot open %s\n", argv[1]); return 1; }
	io.ctx = NULL; io.write = io_write; io.read = io_read; io.sleep_ms = io_sleep;
	lbpc_drive_init(&d, &io, lg, NULL);
	for (;;) {
		ssize_t k = read(0, buf, chunk);
		if (k < 0 && errno == EINTR) continue;
		if (k <= 0) break;
		if ((e = lbpc_drive_feed(&d, buf, (size_t)k)) != 0) break;
	}
	{ int e2 = lbpc_drive_end(&d); if (!e) e = e2; }
	return e ? 1 : 0;
}
