/* CUPS raster filter for Canon LBP6200 (CAPT 3.0).  Reads CUPS raster on stdin (1-bit K/W or 8-bit gray, 600 dpi),
 * talks CAPT through the backend: packets -> stdout, replies <- back-channel (fd 3).  GPL-3. */
#include "rasterpage.h"
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include "../core/capt.h"

/* CUPS cancels a job with SIGTERM to the filter AND the backend and force-kills only after JobKillDelay (scheduler/job.c stop_job).  We stop reading input,
 * let the page in progress finish (white fill: the printer always gets complete streams), start it, then finish the job with state 4 and release (docs/12). */
static volatile int g_term;
static void on_term(int s) { (void)s; g_term = 1; }

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
	int r;
	while ((r = select(4, &fs, NULL, NULL, &tv)) < 0 && errno == EINTR) { FD_ZERO(&fs); FD_SET(3, &fs); }   /* SIGTERM only sets g_term; never abandon a reply (a delivered page must still get its E0A7) */
	if (r <= 0) return r < 0 ? -1 : 0;
	ssize_t k = read(3, b, n);
	return k < 0 ? -1 : (int)k;
}
static void io_sleep(void *c, unsigned ms) { (void)c; usleep(ms * 1000u); }

/* CUPS printer-state-reasons from the decoded conditions (capt_status_conditions / capt_cartridge_conditions, docs/07-status-decode.md);
 * cbctx = the job, whose A0A4 snapshot (taken just before the reserve) supplies the toner conditions. */
static void report(void *c, const struct capt_status *s)
{
	static int last = -1;
	static time_t lastc;
	struct capt_job *j = c;
	/* toner state can change while a job runs (Canon polls A0A4 throughout): refresh the cartridge snapshot at most every 5 s, between transactions */
	if (j && time(NULL) - lastc >= 5) {
		struct capt_cartridge cr[4]; int n = capt_get_cartridges(j->io, cr, 4);
		lastc = time(NULL);
		if (n >= 0) { memcpy(j->cart, cr, sizeof cr); j->ncart = n; }
	}
	unsigned cond = capt_status_conditions(s) | (j ? capt_cartridge_conditions(j->cart, j->ncart) : 0);
	int st = (int)capt_status_summary(s);
	int key = (int)cond << 4 | st;
	if (key == last) return;
	last = key;
	fprintf(stderr, "STATE: %cmedia-empty-error %cmedia-jam-error %cdoor-open-error %cother-error %ctoner-empty-error %ctoner-low-warning %cother-warning\n",
	        cond & CAPT_C_NO_PAPER ? '+' : '-', cond & CAPT_C_JAM ? '+' : '-', cond & CAPT_C_DOOR_OPEN ? '+' : '-',
	        cond & CAPT_C_SERVICE ? '+' : '-', cond & CAPT_C_NO_CARTRIDGE ? '+' : '-', cond & CAPT_C_TONER_LOW ? '+' : '-',
	        cond & CAPT_C_TONER_MEM ? '+' : '-');
	fprintf(stderr, "INFO: %s\n", capt_state_name((enum capt_state)st));
}


int main(int argc, char **argv)
{
	cups_raster_t *ras; cups_page_header2_t hdr; struct capt_io io; struct capt_job job; int input_err = 0;
	cups_option_t *opts = NULL; int nopts, page = 0, e = 0, started = 0;
	struct capt_page_opts po;
	uint8_t when[8]; time_t now = time(NULL); struct tm *tm = localtime(&now);
	int fd = 0;
	if (argc < 6 || argc > 7) { fprintf(stderr, "ERROR: usage: %s job user title copies options [file]\n", argv[0]); return 1; }
	if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) { perror("ERROR: open"); return 1; }
	nopts = cupsParseOptions(argv[5], 0, &opts);
	io.ctx = NULL; io.write = io_write; io.read = io_read; io.sleep_ms = io_sleep;
	{ struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_term; sigemptyset(&sa.sa_mask); sigaction(SIGTERM, &sa, NULL); }
	capt_cancel_flag = &g_term;
	capt_put16(when, (unsigned)tm->tm_year + 1900); when[2] = (uint8_t)(tm->tm_mon + 1); when[3] = (uint8_t)tm->tm_mday;
	when[4] = (uint8_t)tm->tm_hour; when[5] = (uint8_t)tm->tm_min; when[6] = (uint8_t)tm->tm_sec; when[7] = 3;
	ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
	if (!ras) { fprintf(stderr, "ERROR: cannot open raster stream\n"); return 1; }
	while (!g_term && cupsRasterReadHeader2(ras, &hdr)) {
		struct rp_page pg; int oe;
		page++;
		if (rp_paper_from_pts((unsigned)hdr.PageSize[0], (unsigned)hdr.PageSize[1], &po.paper)) { fprintf(stderr, "ERROR: unsupported page size %ux%u pt\n", (unsigned)hdr.PageSize[0], (unsigned)hdr.PageSize[1]); e = 1; break; }
		rp_page_opts(&hdr, nopts, opts, page, &po);
		oe = rp_page_open(&pg, ras, &hdr, &po);
		if (oe == RP_ENOMEM_BACK) fprintf(stderr, "ERROR: out of memory for back page\n");
		if (oe) { e = 1; break; }
		if (!started) {
			memset(&job, 0, sizeof job); job.duplex = po.duplex != CAPT_SIMPLEX; { long gm = tm->tm_gmtoff / 60; int std = (int)(-(gm - (tm->tm_isdst > 0 ? 60 : 0))); job.tz_std = std; job.tz_dst = std - 60; } { char host[64] = ""; gethostname(host, sizeof host - 1); e = capt_job_begin(&job, &io, when, host, argv[2], argv[3]); job.cb = report; job.cbctx = &job; } if (e) { fprintf(stderr, "ERROR: job begin failed (%d)\n", e); rp_close(&pg); break; }
			started = 1;
		}
		fprintf(stderr, "INFO: Sending page %d\n", page);
		e = capt_job_page_lines(&job, &po, rp_fetch, &pg);
		rp_drain(&pg);                                                       /* drain */
		rp_close(&pg);
		if (e) { fprintf(stderr, "ERROR: page %d failed (%d)\n", page, e); break; }
		if (pg.err && !g_term) { fprintf(stderr, "ERROR: raster input ended early on page %d\n", page); input_err = 1; break; }   /* the page was completed with white so the printer got a whole stream */
	}
	/* Canon order (docs/09): lone duplex front page -> its E0A7 when decoded; E1A1(6); E0A9 release; pages keep printing, so wait for the last one afterwards */
	if (!e && started && job.duplex && (page % 2)) e = capt_job_flush(&job);
	if (e && started) {
		fprintf(stderr, "ERROR: starting the print failed (%d)\n", e);
		capt_job_abort(&job, g_term ? CAPT_ECANCEL : e);   /* Canon: always release once reserved; final E1A1 5/4 only while the link is healthy */
	} else if (started) {
		int e2 = capt_job_finish(&job, g_term ? 4 : (input_err ? 5 : 6));   /* 6 = all data delivered, 4 = cancelled, 5 = incomplete (docs/12) */
		if (!e) e = e2;
		if (!e && (g_term || input_err)) e = g_term ? CAPT_ECANCEL : CAPT_EDATA;
		if (!e) {
			e = capt_job_wait_page(&job, (unsigned)page, 120000, report, &job);
			if (e) fprintf(stderr, "ERROR: page %d not completed (%d)\n", page, e);
			else { int i; for (i = 1; i <= page; i++) fprintf(stderr, "PAGE: %d 1\n", i); }
		}
	}
	if (started) fprintf(stderr, "STATE: -media-empty-error -media-jam-error -door-open-error -other-error -toner-empty-error -toner-low-warning -other-warning\n");   /* clear what this job set */
	cupsRasterClose(ras);
	return e ? 1 : (page ? 0 : 1);
}
