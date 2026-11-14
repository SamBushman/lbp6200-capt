/* CUPS backend for the Canon LBP6200 on a Linux usblp node (/dev/usb/lpN): bidirectional, no libusb.
 * Job stream (stdin/file) -> device; device replies -> back-channel fd 3 (read by rastertocapt6200).  GPL-3.
 * DEVICE_URI: lbp6200:/dev/usb/lp0   Run with no arguments: prints a discovery line for /dev/usb/lp*. */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static int wr(int fd, const char *b, size_t n)
{
	while (n) { ssize_t k = write(fd, b, n); if (k < 0) { if (errno == EINTR || errno == EAGAIN) { fd_set w; FD_ZERO(&w); FD_SET(fd, &w); select(fd + 1, NULL, &w, NULL, NULL); continue; } return -1; } b += k; n -= (size_t)k; }
	return 0;
}

int main(int argc, char **argv)
{
	const char *uri = getenv("DEVICE_URI");
	int in = 0, dev, eof = 0, bc = 3;
	char buf[65536];
	if (argc == 1) {
		glob_t g; size_t i;
		if (glob("/dev/usb/lp*", 0, NULL, &g) == 0) {
			for (i = 0; i < g.gl_pathc; i++) printf("direct lbp6200:%s \"Canon LBP6200\" \"Canon LBP6200 (usblp)\" \"MFG:Canon;MDL:LBP6200;CMD:CAPT;\"\n", g.gl_pathv[i]);
			globfree(&g);
		}
		return 0;
	}
	if (argc < 6 || !uri || strncmp(uri, "lbp6200:", 8)) { fprintf(stderr, "ERROR: usage: backend job user title copies options [file]\n"); return 1; }
	if (argc == 7) { in = open(argv[6], O_RDONLY); if (in < 0) { perror("ERROR: input"); return 1; } }
	dev = open(uri + 8, O_RDWR | O_NONBLOCK);
	if (dev < 0) { fprintf(stderr, "ERROR: cannot open %s: %s\n", uri + 8, strerror(errno)); return 1; }
	for (;;) {
		fd_set r; int mx = dev, n;
		FD_ZERO(&r); FD_SET(dev, &r);
		if (!eof) { FD_SET(in, &r); if (in > mx) mx = in; }
		/* after the filter closes its end, wait a little for trailing replies then finish */
		{ struct timeval tv = { 2, 0 }; n = select(mx + 1, &r, NULL, NULL, eof ? &tv : NULL); }
		if (n < 0) { if (errno == EINTR) continue; break; }
		if (n == 0) break;
		if (!eof && FD_ISSET(in, &r)) {
			ssize_t k = read(in, buf, sizeof buf);
			if (k <= 0) eof = 1; else if (wr(dev, buf, (size_t)k)) { fprintf(stderr, "ERROR: write to device failed\n"); return 1; }
		}
		if (FD_ISSET(dev, &r)) {
			ssize_t k = read(dev, buf, sizeof buf);
			if (k > 0) { if (write(bc, buf, (size_t)k) < 0 && errno != EBADF && errno != EPIPE) { /* no back-channel reader: drop */ } }
		}
	}
	close(dev);
	return 0;
}
