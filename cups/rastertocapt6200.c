/* CUPS raster filter for Canon LBP6200 (CAPT 3.0).  Reads CUPS raster on stdin (1-bit K/W or 8-bit gray, 600 dpi),
 * talks CAPT through the backend: packets -> stdout, replies <- back-channel (fd 3).  GPL-3. */
#include <cups/cups.h>
#include <cups/raster.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include "../core/capt.h"

static int io_write(void *c, const uint8_t *b, size_t n)
{
	(void)c;
	while (n) { ssize_t k = write(1, b, n); if (k < 0) { if (errno == EINTR) continue; return -1; } b += k; n -= (size_t)k; }
	return 0;
}
static int io_read(void *c, uint8_t *b, size_t n, int to)
{
	fd_set fs; struct timeval tv; tv.tv_sec = to / 1000; tv.tv_usec = (to % 1000) * 1000; (void)c;
	FD_ZERO(&fs); FD_SET(3, &fs);
	int r = select(4, &fs, NULL, NULL, &tv);
	if (r <= 0) return r < 0 ? -1 : 0;
	ssize_t k = read(3, b, n);
	return k < 0 ? -1 : (int)k;
}
static void io_sleep(void *c, unsigned ms) { (void)c; usleep(ms * 1000u); }

static void report(void *c, const struct capt_status *s)
{
	static int last = -1; int st = (int)capt_status_summary(s); (void)c;
	if (st == last) return;
	last = st;
	fprintf(stderr, "STATE: %cmedia-empty-error %cother\n", st == CAPT_ST_NO_PAPER ? '+' : '-', st == CAPT_ST_ERROR ? '+' : '-');
	fprintf(stderr, "INFO: %s\n", capt_state_name((enum capt_state)st));
}

static int paper_from_pts(unsigned w, unsigned h, enum capt_paper *p)
{
	static const struct { unsigned w, h; enum capt_paper p; } t[] = {
		{ 595, 842, CAPT_A4 }, { 612, 792, CAPT_LETTER }, { 612, 1008, CAPT_LEGAL },
		{ 420, 595, CAPT_A5 }, { 516, 729, CAPT_B5 }, { 522, 756, CAPT_EXECUTIVE } };
	unsigned i;
	for (i = 0; i < sizeof t / sizeof *t; i++)
		if ((w > t[i].w ? w - t[i].w : t[i].w - w) <= 3 && (h > t[i].h ? h - t[i].h : t[i].h - h) <= 3) { *p = t[i].p; return 0; }
	return -1;
}

static const uint8_t bayer[8][8] = {
	{ 0, 32, 8, 40, 2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 }, { 12, 44, 4, 36, 14, 46, 6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 },
	{ 3, 35, 11, 43, 1, 33, 9, 41 }, { 51, 19, 59, 27, 49, 17, 57, 25 }, { 15, 47, 7, 39, 13, 45, 5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 } };

struct pg { cups_raster_t *ras; cups_page_header2_t hdr; uint8_t *line; unsigned next_row; unsigned wbytes; unsigned y_off; int err; };

static void read_row(struct pg *p)
{
	if (p->next_row < p->hdr.cupsHeight) {
		if (cupsRasterReadPixels(p->ras, p->line, p->hdr.cupsBytesPerLine) != p->hdr.cupsBytesPerLine) { p->err = 1; memset(p->line, 0xff, p->hdr.cupsBytesPerLine); }
	} else memset(p->line, p->hdr.cupsColorSpace == CUPS_CSPACE_K ? 0 : 0xff, p->hdr.cupsBytesPerLine);
	p->next_row++;
}

static int fetch(void *ctx, uint8_t *dst, unsigned y)
{
	struct pg *p = ctx; unsigned row = y + p->y_off, x;
	int black_is_one = p->hdr.cupsColorSpace == CUPS_CSPACE_K;
	while (p->next_row <= row) read_row(p);
	memset(dst, 0, p->wbytes);
	if (p->hdr.cupsBitsPerPixel == 1) {
		unsigned off = CAPT_X_OFF / 8, avail = p->hdr.cupsBytesPerLine > off ? p->hdr.cupsBytesPerLine - off : 0;
		unsigned n = avail < p->wbytes ? avail : p->wbytes;
		for (x = 0; x < n; x++) dst[x] = black_is_one ? p->line[off + x] : (uint8_t)~p->line[off + x];
	} else if (p->hdr.cupsBitsPerPixel == 8) {
		for (x = 0; x < p->wbytes * 8 && x + CAPT_X_OFF < p->hdr.cupsWidth; x++) {
			unsigned v = p->line[x + CAPT_X_OFF]; if (black_is_one) v = 255 - v;     /* v: 0 = black .. 255 = white */
			if ((v >> 2) < bayer[row & 7][(x + CAPT_X_OFF) & 7]) dst[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
		}
	}
	return 0;
}

static int opt_int(int n, cups_option_t *o, const char *k, int def)
{ const char *v = cupsGetOption(k, n, o); return v ? atoi(v) : def; }

int main(int argc, char **argv)
{
	cups_raster_t *ras; cups_page_header2_t hdr; struct capt_io io; struct capt_job job;
	cups_option_t *opts = NULL; int nopts, page = 0, e = 0, started = 0;
	struct capt_page_opts po;
	uint8_t when[8]; time_t now = time(NULL); struct tm *tm = localtime(&now);
	int fd = 0;
	if (argc < 6 || argc > 7) { fprintf(stderr, "ERROR: usage: %s job user title copies options [file]\n", argv[0]); return 1; }
	if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) { perror("ERROR: open"); return 1; }
	nopts = cupsParseOptions(argv[5], 0, &opts);
	io.ctx = NULL; io.write = io_write; io.read = io_read; io.sleep_ms = io_sleep;
	capt_put16(when, (unsigned)tm->tm_year + 1900); when[2] = (uint8_t)(tm->tm_mon + 1); when[3] = (uint8_t)tm->tm_mday;
	when[4] = (uint8_t)tm->tm_hour; when[5] = (uint8_t)tm->tm_min; when[6] = (uint8_t)tm->tm_sec; when[7] = 3;
	ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
	if (!ras) { fprintf(stderr, "ERROR: cannot open raster stream\n"); return 1; }
	while (cupsRasterReadHeader2(ras, &hdr)) {
		struct pg pg; struct capt_geom g; const char *be = cupsGetOption("BindEdge", nopts, opts);
		page++;
		capt_page_opts_default(&po);
		if (paper_from_pts((unsigned)hdr.PageSize[0], (unsigned)hdr.PageSize[1], &po.paper)) { fprintf(stderr, "ERROR: unsupported page size %ux%u pt\n", (unsigned)hdr.PageSize[0], (unsigned)hdr.PageSize[1]); e = 1; break; }
		po.density = (unsigned)opt_int(nopts, opts, "CNTonerDensity", 4);
		{ const char *d = cupsGetOption("CNDraftMode", nopts, opts); po.toner_save = d && (!strcasecmp(d, "True") || !strcasecmp(d, "on")); }
		{ const char *d = cupsGetOption("CNSuperSmooth", nopts, opts); po.smooth = !(d && !strcasecmp(d, "False")); }
		if (hdr.Duplex) po.duplex = (hdr.Tumble || (be && !strcasecmp(be, "Top"))) ? CAPT_DUPLEX_SHORT : CAPT_DUPLEX_LONG;
		po.back_side = hdr.Duplex && (page % 2 == 0);
		capt_paper_geom(po.paper, &g);
		memset(&pg, 0, sizeof pg); pg.ras = ras; pg.hdr = hdr; pg.wbytes = g.print_bytes_w; pg.y_off = g.y_off;
		pg.line = malloc(hdr.cupsBytesPerLine + 8);
		if (!pg.line) { e = 1; break; }
		if (!started) {
			e = capt_wait_ready_for_data(&io, 30000); if (e) { fprintf(stderr, "ERROR: printer not responding (%d)\n", e); free(pg.line); break; }
			e = capt_job_begin(&job, &io, when, argv[3]); if (e) { fprintf(stderr, "ERROR: job begin failed (%d)\n", e); free(pg.line); break; }
			started = 1;
		}
		fprintf(stderr, "INFO: Sending page %d\n", page);
		e = capt_job_page_lines(&job, &po, fetch, &pg);
		while (pg.next_row < hdr.cupsHeight) read_row(&pg);                 /* drain */
		free(pg.line);
		if (e) { fprintf(stderr, "ERROR: page %d failed (%d)\n", page, e); break; }
		e = capt_job_wait_page(&job, (unsigned)page, 120000, report, NULL);
		if (e) { fprintf(stderr, "ERROR: page %d not completed (%d)\n", page, e); break; }
		fprintf(stderr, "PAGE: %d 1\n", page);
	}
	if (started) capt_job_end(&job);
	cupsRasterClose(ras);
	return e ? 1 : (page ? 0 : 1);
}
