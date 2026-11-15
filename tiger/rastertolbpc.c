/* CUPS raster filter for the Canon LBP6200 on Mac OS X 10.4 Tiger (CUPS 1.1.23).  Pure transform: CUPS raster on stdin -> LBPC records on stdout
 * (lbpc/lbpc.h).  No USB, no CAPT conversation: Tiger's CUPS has no back-channel, so the class-driver plug-in that owns the USB pipe replays the
 * records into the CAPT engine.  Status (STATE:/INFO:/PAGE:) is written by the plug-in, not here.  GPL-3. */
#include "../cups/rasterpage.h"
#include "../lbpc/lbpc.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* CUPS cancels with SIGTERM.  Stop reading input, finish the page in progress (rasterpage blanks the rest: the printer always gets complete
 * streams), then tell the plug-in with JOB_END(cancelled) (docs/12). */
static volatile int g_term;
static void on_term(int s) { (void)s; g_term = 1; }

static int out_write(void *c, const uint8_t *b, size_t n)
{
	(void)c;
	while (n) { ssize_t k = write(1, b, n); if (k < 0) { if (errno == EINTR) continue; return -1; } b += k; n -= (size_t)k; }
	return 0;
}

int main(int argc, char **argv)
{
	cups_raster_t *ras; cups_page_header2_t hdr; struct lbpc_writer w; struct capt_page_opts po;
	cups_option_t *opts = NULL; int nopts, page = 0, e = 0, started = 0, input_err = 0, fd = 0;
	uint8_t when[8]; time_t now = time(NULL); struct tm *tm = localtime(&now);
	if (argc < 6 || argc > 7) { fprintf(stderr, "ERROR: usage: %s job user title copies options [file]\n", argv[0]); return 1; }
	if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) { perror("ERROR: open"); return 1; }
	nopts = cupsParseOptions(argv[5], 0, &opts);
	memset(&w, 0, sizeof w); w.write = out_write;
	{ struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_term; sigemptyset(&sa.sa_mask); sigaction(SIGTERM, &sa, NULL); }
	signal(SIGPIPE, SIG_IGN);                  /* a vanished backend shows up as a failed write */
	capt_cancel_flag = &g_term;                /* rasterpage blanks the remaining rows once cancelled */
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
			char host[64] = ""; long gm = tm->tm_gmtoff / 60; int std = (int)(-(gm - (tm->tm_isdst > 0 ? 60 : 0)));
			gethostname(host, sizeof host - 1);
			e = lbpc_w_job_begin(&w, 0, po.duplex != CAPT_SIMPLEX, std, std - 60, when, host, argv[2], argv[3]);
			if (e) { fprintf(stderr, "ERROR: cannot write to the printer backend\n"); rp_close(&pg); break; }
			started = 1;
		}
		e = lbpc_w_page(&w, &po, rp_fetch, &pg);
		rp_drain(&pg);
		rp_close(&pg);
		if (e) { fprintf(stderr, "ERROR: page %d failed (%d)\n", page, e); break; }
		if (pg.err && !g_term) { fprintf(stderr, "ERROR: raster input ended early on page %d\n", page); input_err = 1; break; }   /* the page was completed with white */
	}
	if (started) {
		unsigned st = e ? LBPC_END_FAILED : g_term ? LBPC_END_CANCELLED : input_err ? LBPC_END_INCOMPLETE : LBPC_END_DONE;
		int e2 = lbpc_w_job_end(&w, st);
		if (!e) e = e2;
		if (!e && (st == LBPC_END_CANCELLED || st == LBPC_END_INCOMPLETE)) e = 1;
	}
	cupsRasterClose(ras);
	return e ? 1 : (page ? 0 : 1);
}
