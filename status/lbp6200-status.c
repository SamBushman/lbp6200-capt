/* lbp6200-status: read-only status/cartridge/info query for the Canon LBP6200 over a usblp node (or any fd-like device).
 * usage: lbp6200-status [-d /dev/usb/lp0] [-r]   (-r = raw hex dump of status record)    GPL-3 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include "../core/capt.h"

static int io_write(void *c, const uint8_t *b, size_t n)
{ int fd = *(int *)c; while (n) { ssize_t k = write(fd, b, n); if (k < 0) { if (errno == EINTR) continue; return -1; } b += k; n -= (size_t)k; } return 0; }
static int io_read(void *c, uint8_t *b, size_t n, int to)
{
	int fd = *(int *)c; fd_set fs; struct timeval tv; tv.tv_sec = to / 1000; tv.tv_usec = (to % 1000) * 1000;
	FD_ZERO(&fs); FD_SET(fd, &fs);
	int r = select(fd + 1, &fs, NULL, NULL, &tv); if (r <= 0) return r < 0 ? -1 : 0;
	ssize_t k = read(fd, b, n); return k < 0 ? -1 : (int)k;
}
static void io_sleep(void *c, unsigned ms) { (void)c; usleep(ms * 1000u); }

int main(int argc, char **argv)
{
	const char *dev = "/dev/usb/lp0"; int raw = 0, i, fd;
	struct capt_io io; struct capt_status s; struct capt_cartridge cr[16]; uint8_t r[128];
	for (i = 1; i < argc; i++) { if (!strcmp(argv[i], "-d") && i + 1 < argc) dev = argv[++i]; else if (!strcmp(argv[i], "-r")) raw = 1; else { fprintf(stderr, "usage: %s [-d device] [-r]\n", argv[0]); return 2; } }
	fd = open(dev, O_RDWR);
	if (fd < 0) { fprintf(stderr, "%s: cannot open %s: %s\n", argv[0], dev, strerror(errno)); return 1; }
	io.ctx = &fd; io.write = io_write; io.read = io_read; io.sleep_ms = io_sleep;
	int n = capt_xact(&io, PC_GET_DEVICEID, NULL, 0, r, sizeof r, 2000);
	if (n >= 6) { printf("Device ID : %.*s\n", n - 6, (char *)r + 6); }
	n = capt_xact(&io, PC_GET_ENGINE_VER, NULL, 0, r, sizeof r, 2000);
	if (n >= 6) printf("Engine    : %.*s\n", n - 6, (char *)r + 6);
	if (capt_get_status(&io, &s)) { fprintf(stderr, "status query failed\n"); close(fd); return 1; }
	printf("State     : %s\n", capt_state_name(capt_status_summary(&s)));
	printf("Status    : S0=%04x S1=%04x S2=%04x S3=%04x S4=%04x S5=%04x S6=%04x\n", s.st[0], s.st[1], s.st[2], s.st[3], s.st[4], s.st[5], s.st[6]);
	printf("Pages     : decoding=%u printing=%u out=%u received=%u  (job %u, in-job %u, printed %u)\n",
	       s.page_decoding, s.page_printing, s.page_out, s.page_received, s.job_no, s.pages_in_job, s.pages_printed);
	n = capt_get_cartridges(&io, cr, 16);
	for (i = 0; i < n; i++) printf("Cartridge : #%d id=%02x flag=%02x flag2=%02x raw=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", i, cr[i].id, cr[i].flag, cr[i].flag2,
		cr[i].raw[0], cr[i].raw[1], cr[i].raw[2], cr[i].raw[3], cr[i].raw[4], cr[i].raw[5], cr[i].raw[6], cr[i].raw[7], cr[i].raw[8], cr[i].raw[9], cr[i].raw[10], cr[i].raw[11]);
	if (raw) { n = capt_xact(&io, PC_GET_EXT_STATUS2, NULL, 0, r, sizeof r, 2000); for (i = 0; i < n; i++) printf("%02x%s", r[i], (i % 16 == 15) ? "\n" : " "); putchar('\n'); }
	close(fd);
	return 0;
}
